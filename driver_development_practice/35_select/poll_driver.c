/***************************************************************************//**
*  \file       poll_driver.c
*  \details    Poll driver — demonstrates poll() support in Linux device driver
*
*  FLOW:
*    App runs poll() → driver's etx_poll() called → poll_wait() registers wq
*    If sysfs READ  → can_write=true  → wake_up → poll returns POLLOUT
*    If sysfs WRITE → can_read=true   → wake_up → poll returns POLLIN
*
*  Tested with Linux raspberrypi 5.4.51-v7l+
*******************************************************************************/

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/kdev_t.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/slab.h>
#include <linux/uaccess.h>   /* copy_to/from_user()                           */
#include <linux/kthread.h>
#include <linux/wait.h>      /* waitqueue APIs                                */
#include <linux/poll.h>      /* poll_wait(), __poll_t, POLLIN, POLLOUT etc.   */
#include <linux/sysfs.h>
#include <linux/kobject.h>
#include <linux/err.h>


/* ── WAITQUEUE SETUP ─────────────────────────────────────────────────────── */
/*
 * DECLARE_WAIT_QUEUE_HEAD — static method, compile-time initialization.
 * This waitqueue is shared between:
 *   - etx_poll()    → adds it to poll_table via poll_wait()
 *   - sysfs_show()  → calls wake_up() to signal POLLOUT event
 *   - sysfs_store() → calls wake_up() to signal POLLIN event
 *
 * When wake_up() is called, kernel re-invokes etx_poll() for all
 * processes that registered this queue via poll().
 */
DECLARE_WAIT_QUEUE_HEAD(wait_queue_etx_data);


/* ── GLOBAL STATE VARIABLES ──────────────────────────────────────────────── */
/*
 * can_read:  true = data is available → app should call read()
 * can_write: true = space is available → app should call write()
 *
 * Set by sysfs handlers, checked and cleared in etx_poll().
 * Cleared after each poll cycle to prevent repeated notifications.
 */
static bool can_write = false;
static bool can_read  = false;

/* Buffer holding data exchanged between driver and user app */
static char etx_value[20];

/* Standard char device globals */
dev_t dev = 0;
static struct class *dev_class;
static struct cdev etx_cdev;
struct kobject *kobj_ref;


/* ── FUNCTION PROTOTYPES ─────────────────────────────────────────────────── */
static int      __init etx_driver_init(void);
static void     __exit etx_driver_exit(void);
static int      etx_open(struct inode *inode, struct file *file);
static int      etx_release(struct inode *inode, struct file *file);
static ssize_t  etx_read(struct file *filp, char __user *buf, size_t len, loff_t *off);
static ssize_t  etx_write(struct file *filp, const char *buf, size_t len, loff_t *off);
static unsigned int etx_poll(struct file *filp, struct poll_table_struct *wait);
static ssize_t  sysfs_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf);
static ssize_t  sysfs_store(struct kobject *kobj, struct kobj_attribute *attr,
                             const char *buf, size_t count);

/* Sysfs attribute — /sys/kernel/etx_sysfs/etx_value */
struct kobj_attribute etx_attr = __ATTR(etx_value, 0660, sysfs_show, sysfs_store);


/* ── FILE OPERATIONS TABLE ───────────────────────────────────────────────── */
/*
 * KEY ADDITION: .poll = etx_poll
 * This links the poll() system call to our etx_poll() function.
 * Without this, poll() on our device would fail or return immediately.
 */
static struct file_operations fops = {
    .owner   = THIS_MODULE,
    .read    = etx_read,
    .write   = etx_write,
    .open    = etx_open,
    .release = etx_release,
    .poll    = etx_poll,   /* ← MUST have this to support poll() from user space */
};


/* ── SYSFS FUNCTIONS ─────────────────────────────────────────────────────── */

/*
 * sysfs_show() — called when user reads: cat /sys/kernel/etx_sysfs/etx_value
 *
 * EFFECT: tells user app that it can WRITE data to the driver.
 * sets can_write=true and wakes poll → etx_poll() returns POLLOUT
 * → user app sees POLLOUT in revents → calls write() on /dev/etx_device
 * → driver stores whatever user writes in etx_value
 *
 * Analogy: "I (kernel) just freed some space, you can write now!"
 */
static ssize_t sysfs_show(struct kobject *kobj,
                           struct kobj_attribute *attr, char *buf)
{
    pr_info("Sysfs Show - Write Permission Granted!!!\n");

    can_write = true;   /* signal: app can now write to driver */

    /* Wake up poll waitqueue — kernel re-calls etx_poll() for all pollers.
     * etx_poll() will see can_write=true → return POLLOUT → app writes. */
    wake_up(&wait_queue_etx_data);

    return sprintf(buf, "%s", "Success\n");
}

/*
 * sysfs_store() — called when user writes: echo "data" > /sys/kernel/etx_sysfs/etx_value
 *
 * EFFECT: stores data in etx_value AND tells user app it can READ that data.
 * sets can_read=true and wakes poll → etx_poll() returns POLLIN
 * → user app sees POLLIN in revents → calls read() on /dev/etx_device
 * → driver sends etx_value to user app
 *
 * Analogy: "I (kernel) just got new data, come read it!"
 */
static ssize_t sysfs_store(struct kobject *kobj,
                            struct kobj_attribute *attr,
                            const char *buf, size_t count)
{
    pr_info("Sysfs Store - Read Permission Granted!!!\n");

    strcpy(etx_value, buf);   /* store the sysfs-written data in etx_value */
    can_read = true;           /* signal: app can now read from driver       */

    /* Wake up poll waitqueue → etx_poll() re-evaluates, returns POLLIN */
    wake_up(&wait_queue_etx_data);

    return count;
}


/* ── DEVICE FILE HANDLER FUNCTIONS ───────────────────────────────────────── */

static int etx_open(struct inode *inode, struct file *file)
{
    pr_info("Device File Opened...!!!\n");
    return 0;
}

static int etx_release(struct inode *inode, struct file *file)
{
    pr_info("Device File Closed...!!!\n");
    return 0;
}

/*
 * etx_read() — called when user app reads after receiving POLLIN
 *
 * Sends etx_value to user app.
 * Note: This code has a bug — uses strcpy(buf, etx_value) directly on
 * __user pointer instead of copy_to_user(). The commented-out section
 * shows the correct approach using copy_to_user().
 */
static ssize_t etx_read(struct file *filp, char __user *buf, size_t len, loff_t *off)
{
    pr_info("Read Function : etx_value = %s\n", etx_value);

    len = strlen(etx_value);
    strcpy(buf, etx_value);   /* Note: should use copy_to_user() in production */

    /* Correct approach (commented out in original):
     * if (copy_to_user(buf, etx_value, len) > 0) {
     *     pr_err("ERROR: Not all the bytes have been copied to user\n");
     * }
     */

    return 0;
}

/*
 * etx_write() — called when user app writes after receiving POLLOUT
 * Stores user's data in etx_value.
 */
static ssize_t etx_write(struct file *filp, const char __user *buf, size_t len, loff_t *off)
{
    strcpy(etx_value, buf);   /* store user's data in kernel buffer */
    pr_info("Write function : etx_value = %s\n", etx_value);
    return len;
}


/* ── POLL FUNCTION — CORE OF THIS TUTORIAL ───────────────────────────────── */
/*
 * etx_poll() — called by kernel whenever user app calls poll()
 *
 * @filp : open file structure for /dev/etx_device
 * @wait : kernel's poll_table — we add our waitqueue here
 * Returns: bitmask of events that are currently ready
 *          0     = nothing ready (user's poll() will block/timeout)
 *          POLLIN/POLLOUT = data or space ready (user's poll() returns)
 *
 * HOW IT WORKS:
 *   1. First call from poll(): poll_wait() registers our waitqueue in poll_table.
 *      Then we check conditions — likely nothing ready → return 0 → user blocks.
 *
 *   2. sysfs read/write happens → can_read/can_write set → wake_up() called.
 *
 *   3. wake_up() wakes the process → kernel calls etx_poll() AGAIN.
 *
 *   4. This time can_read or can_write is true → we return POLLIN or POLLOUT.
 *
 *   5. Kernel writes result to pfd.revents → user's poll() returns > 0.
 *
 *   6. User checks pfd.revents → calls read() or write() on the device.
 *
 * KEY: poll_wait() does NOT block the kernel thread! It just registers the
 * waitqueue so kernel knows to call etx_poll() again when woken up.
 * The BLOCKING happens in the kernel's generic poll infrastructure OUTSIDE
 * this function, between the poll_wait() call and the next etx_poll() call.
 */
static unsigned int etx_poll(struct file *filp, struct poll_table_struct *wait)
{
    __poll_t mask = 0;   /* start with 0 = nothing ready */

    /*
     * poll_wait(filp, &waitqueue, poll_table):
     *   Adds wait_queue_etx_data to the poll_table.
     *   Does NOT sleep here — returns immediately.
     *   Tells kernel: "wake me (call etx_poll again) when this queue is woken."
     *
     *   After poll_wait: kernel may sleep the calling process until
     *   wake_up(&wait_queue_etx_data) is called, OR poll timeout expires.
     */
    poll_wait(filp, &wait_queue_etx_data, wait);

    pr_info("Poll function\n");

    /* Check if read data is available */
    if (can_read) {
        can_read = false;                      /* clear flag — one notification only */
        mask |= (POLLIN | POLLRDNORM);         /* tell user: you can READ now        */
    }

    /* Check if write space is available */
    if (can_write) {
        can_write = false;                     /* clear flag — one notification only */
        mask |= (POLLOUT | POLLWRNORM);        /* tell user: you can WRITE now       */
    }

    return mask;   /* 0=not ready, non-zero=ready (kernel copies to pfd.revents) */
}


/* ── MODULE INIT ──────────────────────────────────────────────────────────── */
/*
 * Standard char device + sysfs setup.
 * Uses DECLARE_WAIT_QUEUE_HEAD at global scope — no init call needed.
 * (The commented-out init_waitqueue_head is for the dynamic method.)
 */
static int __init etx_driver_init(void)
{
    if ((alloc_chrdev_region(&dev, 0, 1, "etx_Dev")) < 0) {
        pr_err("Cannot allocate major number\n"); return -1;
    }
    pr_info("Major = %d Minor = %d \n", MAJOR(dev), MINOR(dev));

    cdev_init(&etx_cdev, &fops);
    etx_cdev.owner = THIS_MODULE;
    etx_cdev.ops   = &fops;

    if ((cdev_add(&etx_cdev, dev, 1)) < 0) {
        pr_err("Cannot add the device to the system\n"); goto r_class;
    }
    if (IS_ERR(dev_class = class_create("etx_class"))) {
        pr_err("Cannot create the struct class\n"); goto r_class;
    }
    if (IS_ERR(device_create(dev_class, NULL, dev, NULL, "etx_device"))) {
        pr_err("Cannot create the Device 1\n"); goto r_device;
    }

    /* Create sysfs: /sys/kernel/etx_sysfs/etx_value */
    kobj_ref = kobject_create_and_add("etx_sysfs", kernel_kobj);
    if (sysfs_create_file(kobj_ref, &etx_attr.attr)) {
        pr_info("Cannot create sysfs file......\n"); goto r_sysfs;
    }

    /* Dynamic waitqueue init (already done statically above via DECLARE_WAIT_QUEUE_HEAD):
     * init_waitqueue_head(&wait_queue_etx_data); */

    pr_info("Device Driver Insert...Done!!!\n");
    return 0;

r_sysfs:
    kobject_put(kobj_ref);
    sysfs_remove_file(kernel_kobj, &etx_attr.attr);
r_device:
    class_destroy(dev_class);
r_class:
    unregister_chrdev_region(dev, 1);
    return -1;
}


/* ── MODULE EXIT ──────────────────────────────────────────────────────────── */
static void __exit etx_driver_exit(void)
{
    kobject_put(kobj_ref);
    sysfs_remove_file(kernel_kobj, &etx_attr.attr);
    device_destroy(dev_class, dev);
    class_destroy(dev_class);
    cdev_del(&etx_cdev);
    unregister_chrdev_region(dev, 1);
    pr_info("Device Driver Remove...Done!!!\n");
}

module_init(etx_driver_init);
module_exit(etx_driver_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("EmbeTronicX <embetronicx@gmail.com>");
MODULE_DESCRIPTION("Simple linux driver (poll)");
MODULE_VERSION("1.41");

/* Complete flow summary
 *
3 terminals: Driver | App | Sysfs

sudo insmod poll_driver.ko

sudo ./poll_app
  └── open("/dev/etx_device", O_RDWR|O_NONBLOCK)
  └── poll() → etx_poll() → poll_wait() → returns 0 → app blocks/waits
  Prints: "Starting poll..." every 5 seconds (timeout)

[Sysfs terminal]: cat /sys/kernel/etx_sysfs/etx_value
  └── sysfs_show() called
        ├── can_write = true
        └── wake_up(&wait_queue_etx_data)
              → etx_poll() called again
              → sees can_write=true → returns POLLOUT
              → poll() returns > 0, revents = POLLOUT
  App sees POLLOUT → write("User Space") to /dev/etx_device
  Prints: "POLLOUT : Kernel_val = User Space"

[Sysfs terminal]: echo "EmbeTronicX" > /sys/kernel/etx_sysfs/etx_value
  └── sysfs_store() called
        ├── strcpy(etx_value, "EmbeTronicX")
        ├── can_read = true
        └── wake_up(&wait_queue_etx_data)
              → etx_poll() called again → returns POLLIN
  App sees POLLIN → read() from /dev/etx_device
  Prints: "POLLIN : Kernel_val = EmbeTronicX"
  */
