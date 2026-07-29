/*
 * chardev.c: Creates a read-only char device that says how many times you have read from the dev file
 * Key concepts:
 *  - register_chrdev()   : older/simpler way to register a char device
 *  - atomic_t            : safe shared variable between concurrent accesses
 *  - atomic_cmpxchg()    : enforce single-process exclusive device access
 *  - put_user()          : byte-by-byte copy from kernel to user space
 *  - offset tracking     : proper EOF handling for repeated reads
 *  - Version-safe code   : handles API differences between kernel versions
 */

#include <linux/atomic.h>    /* atomic_t, atomic_cmpxchg(), atomic_set() Used to safely manage the "device in use" flag  */
#include <linux/cdev.h>      /* character device structures (cdev)              */
#include <linux/delay.h>     /* delay utilities (mdelay, udelay) */
#include <linux/device.h>    /* class_create(), device_create() — auto /dev     */
#include <linux/fs.h>        /* file_operations, register_chrdev(), inode, file */
#include <linux/init.h>      /* __init, __exit macros                           */
#include <linux/kernel.h>    /* sprintf() — kernel-safe string formatting       */
#include <linux/module.h>    /* module_init(), module_exit(), THIS_MODULE        */
#include <linux/printk.h>    /* pr_info(), pr_alert(), pr_err()                 */
#include <linux/types.h>     /* standard kernel types (ssize_t, loff_t, etc.)   */
#include <linux/uaccess.h>   /* put_user(), get_user() — safe kernel↔user copy  */
#include <linux/version.h>   /* LINUX_VERSION_CODE, KERNEL_VERSION() macros     */
                             /* Used to write code compatible with multiple kernel versions */
#include <asm/errno.h>       /* error codes: -EBUSY, -EINVAL, etc.              */

/*  Prototypes - this would normally go in a .h file */
static int device_open(struct inode *, struct file *);
static int device_release(struct inode *, struct file *);
static ssize_t device_read(struct file *, char __user *, size_t, loff_t *);
static ssize_t device_write(struct file *, const char __user *, size_t,
                            loff_t *);

#define DEVICE_NAME "chardev" /* Dev name as it appears in /proc/devices   */
#define BUF_LEN 80 /* Max length of the message from the device */

/* Global variables are declared as static, so are global within the file. */

static int major; /* major number assigned to our device driver */

/*
 * Enum for device open state — makes the code readable.
 * CDEV_NOT_USED      = 0 → device is free, anyone can open it
 * CDEV_EXCLUSIVE_OPEN = 1 → device is currently in use, reject new opens
 */
enum {
    CDEV_NOT_USED,
    CDEV_EXCLUSIVE_OPEN,
};

/*
 * atomic_t — a special integer type that is SAFE to read/write from
 * multiple processes or interrupt contexts WITHOUT needing a mutex/lock.
 *
 * Why atomic? Without atomics, two processes could both check "is device open?"
 * at the same time, both see "no", and both open it — causing a race condition.
 * atomic_t guarantees the check-and-set happens as one indivisible operation.
 *
 * ATOMIC_INIT(CDEV_NOT_USED) = initialized to 0 (device free) at load time.
 */
/* Is device open? Used to prevent multiple access to device */
static atomic_t already_open = ATOMIC_INIT(CDEV_NOT_USED);

static char msg[BUF_LEN + 1]; /* The msg the device will give when asked */

/*
 * Pointer to the device class — creates /sys/class/chardev/
 * udev watches this and auto-creates /dev/chardev when device_create() is called.
 */
static struct class *cls;

static struct file_operations chardev_fops = {
    .read = device_read,
    .write = device_write,
    .open = device_open,
    .release = device_release,
};

/*
 * chardev_init() — runs when module is loaded: sudo insmod chardev.ko
 *
 * Uses register_chrdev() — the OLDER, simpler API (vs alloc_chrdev_region + cdev_add).
 * register_chrdev() does three things in one call:
 *   1. Allocates a major number dynamically (since we pass 0)
 *   2. Registers ALL 256 minor numbers under that major
 *   3. Links the file_operations table to the device
 *
 * Modern drivers prefer alloc_chrdev_region() + cdev_init() + cdev_add()
 * for finer control, but register_chrdev() is simpler for learning.
 */
static int __init chardev_init(void)
{
    major = register_chrdev(0, DEVICE_NAME, &chardev_fops);

    if (major < 0) {
        pr_alert("Registering char device failed with %d\n", major);
        return major;
    }

    pr_info("I was assigned major number %d.\n", major);

    /* Create the device class under /sys/class/chardev/
     *
     * Version-safe code using preprocessor conditionals:
     * In kernel >= 6.4.0, class_create() was changed to take only ONE argument
     * (the name), removing the THIS_MODULE parameter.
     * This #if block handles both old and new kernel APIs cleanly.
     */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 4, 0)
    cls = class_create(DEVICE_NAME);		/* new API: kernel 6.4+ */
#else
    cls = class_create(THIS_MODULE, DEVICE_NAME);	/* old API: kernel < 6.4 */
#endif
    if (IS_ERR(cls)) {
        pr_err("Failed to create class for device\n");
        unregister_chrdev(major, DEVICE_NAME);
        return PTR_ERR(cls);
    }

     /* Create the device node — triggers udev to create /dev/chardev.
     * cls          → class this device belongs to
     * NULL         → no parent device
     * MKDEV(major, 0) → combine major + minor(0) into a dev_t value
     * NULL         → no driver-specific data
     * DEVICE_NAME  → device file name → creates /dev/chardev
     */
    device_create(cls, NULL, MKDEV(major, 0), NULL, DEVICE_NAME);

    pr_info("Device created on /dev/%s\n", DEVICE_NAME);

    return 0;
}

static void __exit chardev_exit(void)
{
    device_destroy(cls, MKDEV(major, 0));  /* Remove /dev/chardev        */
    class_destroy(cls);                    /* Remove /sys/class/chardev/ */
    unregister_chrdev(major, DEVICE_NAME); /* Release major number       */
}

/*===================== Methods ===================== */

/* Called when a process tries to open the device file, like
 * "sudo cat /dev/chardev"
 Triggered by: cat /dev/chardev or: open("/dev/chardev", O_RDONLY) in a program
 */
static int device_open(struct inode *inode, struct file *file)
{
    static int counter = 0;

    // atomic_cmpxchg — Atomic Compare and Exchan
    // atomic_cmpxchg(ptr, old, new) = compare and exchange (atomically) “If *ptr == old, then set it to new
   if (atomic_cmpxchg(&already_open, CDEV_NOT_USED, CDEV_EXCLUSIVE_OPEN))
        return -EBUSY;

    sprintf(msg, "I already told you %d times Hello world!\n", counter++);

    return 0;
}

/* Called when a process closes the device file. */
static int device_release(struct inode *inode, struct file *file)
{
    /* We're now ready for our next caller */
    atomic_set(&already_open, CDEV_NOT_USED);

    return 0;
}

/* Called when a process, which already opened the dev file, attempts to read from it.
 * Triggered by: cat /dev/chardev or: read(fd, buf, size) in a program
 */
static ssize_t device_read(struct file *filp, /* see include/linux/fs.h   */
                           char __user *buffer, /* buffer to fill with data */
                           size_t length, /* length of the buffer     */
                           loff_t *offset)
{
    int bytes_read = 0;          /* tracks how many bytes we actually send    */
    const char *msg_ptr = msg;   /* local pointer to walk through msg buffer  */

    if (!*(msg_ptr + *offset)) { /* we are at the end of message */
        *offset = 0; /* reset the offset */
        return 0; /* signify end of file */
    }

    msg_ptr += *offset;

    /* Actually put the data into the buffer */
    while (length && *msg_ptr) {
        /* The buffer is in the user data segment, not the kernel
         * segment so "*" assignment won't work.  We have to use
         * put_user which copies data from the kernel data segment to
         * the user data segment.
         */
        put_user(*(msg_ptr++), buffer++);	// Copies a single value (usually 1 byte or 1 word) from kernel to user space.
        length--;
        bytes_read++;
    }

    *offset += bytes_read;

    /* Most read functions return the number of bytes put into the buffer. */
    return bytes_read;
}

/* Called when a process writes to dev file: echo "hi" | sudo tee /dev/chardev */
static ssize_t device_write(struct file *filp, const char __user *buff,
                            size_t len, loff_t *off)
{
    pr_alert("Sorry, this operation is not supported.\n");
    return -EINVAL;
}

module_init(chardev_init);
module_exit(chardev_exit);

MODULE_LICENSE("GPL");
