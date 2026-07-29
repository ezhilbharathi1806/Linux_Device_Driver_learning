/***************************************************************************//**
*  \file       misc_driver.c
*  \details    Simple misc driver explanation
*
*  WHAT MAKES THIS DIFFERENT FROM CHARACTER DRIVER:
*    Character driver needs 5 steps in init + 4 steps in exit.
*    Misc driver needs only:
*      misc_register()   → ONE call in init (does everything)
*      misc_deregister() → ONE call in exit (does everything)
*
*  RESULT: /dev/simple_etx_misc created automatically with major=10
*
*******************************************************************************/

/* ── HEADERS ─────────────────────────────────────────────────────────────── */
#include <linux/miscdevice.h>  /* struct miscdevice, misc_register,
                                  misc_deregister, MISC_DYNAMIC_MINOR       */
#include <linux/fs.h>          /* file_operations, inode, file structs       */
#include <linux/kernel.h>      /* pr_info, pr_err                            */
#include <linux/module.h>      /* module_init, module_exit, THIS_MODULE      */
#include <linux/init.h>        /* __init, __exit                             */


/* ── DEVICE FILE HANDLER FUNCTIONS ───────────────────────────────────────── */

/*
 * etx_misc_open() — called when user opens /dev/simple_etx_misc
 * Triggered by: cat, echo, or open() system call from user app
 * Returns 0 = success.
 * In real driver: initialize hardware, allocate per-open resources here.
 */
static int etx_misc_open(struct inode *inode, struct file *file)
{
    pr_info("EtX misc device open\n");
    return 0;
}

/*
 * etx_misc_close() — called when user closes /dev/simple_etx_misc
 * Triggered when process calls close() or exits.
 * In real driver: free per-open resources allocated in open().
 */
static int etx_misc_close(struct inode *inodep, struct file *filp)
{
    pr_info("EtX misc device close\n");
    return 0;
}

/*
 * etx_misc_write() — called when user writes to /dev/simple_etx_misc
 * Triggered by: echo "data" > /dev/simple_etx_misc
 *               or: write() system call from user app
 *
 * @file : open file structure
 * @buf  : __user buffer — data FROM user space (use copy_from_user to access)
 * @len  : number of bytes written by user
 * @ppos : current file position
 * Returns: len — MUST return bytes consumed or shell retries forever.
 *
 * In real driver: use copy_from_user(kernel_buf, buf, len) to safely
 * read user data into kernel space.
 */
static ssize_t etx_misc_write(struct file *file, const char __user *buf,
                               size_t len, loff_t *ppos)
{
    pr_info("EtX misc device write\n");
    /* We are not processing data in this example — just log and return */
    return len;   /* tell caller: all bytes consumed */
}

/*
 * etx_misc_read() — called when user reads from /dev/simple_etx_misc
 * Triggered by: cat /dev/simple_etx_misc
 *               or: read() system call from user app
 *
 * @filp  : open file structure
 * @buf   : __user buffer — destination in user space (use copy_to_user)
 * @count : max bytes user wants to read
 * @f_pos : current file position
 * Returns: 0 = EOF (no data to send in this example)
 *
 * In real driver: use copy_to_user(buf, kernel_buf, len) to safely
 * send kernel data to user space.
 */
static ssize_t etx_misc_read(struct file *filp, char __user *buf,
                              size_t count, loff_t *f_pos)
{
    pr_info("EtX misc device read\n");
    return 0;   /* 0 = EOF — no data to send */
}


/* ── FILE OPERATIONS TABLE ───────────────────────────────────────────────── */
/*
 * Maps system calls → driver functions.
 * Identical to character driver fops — same structure, same fields.
 *
 * .llseek = no_llseek: explicitly says seeking is NOT supported.
 *   This prevents the kernel from defaulting to generic_file_llseek,
 *   which would be wrong for this simple device with no file position tracking.
 */
static const struct file_operations fops = {
    .owner   = THIS_MODULE,     /* prevents module unload while device is open */
    .write   = etx_misc_write,  /* called on: echo "data" > /dev/simple_etx_misc */
    .read    = etx_misc_read,   /* called on: cat /dev/simple_etx_misc           */
    .open    = etx_misc_open,   /* called on any open() of the device            */
    .release = etx_misc_close,  /* called on any close() of the device           */
    .llseek  = no_llseek,       /* seeking not supported on this device          */
};


/* ── MISC DEVICE STRUCTURE ───────────────────────────────────────────────── */
/*
 * etx_misc_device — describes our misc device to the kernel.
 *
 * .minor = MISC_DYNAMIC_MINOR:
 *   Ask kernel to auto-assign a free minor number (1-255).
 *   ✅ Recommended — avoids conflicts with other misc devices.
 *   Alternatively: hardcode a number (check ls -l /dev/ for free ones first).
 *   After insmod: check with ls -l /dev/simple_etx_misc → shows assigned minor.
 *
 * .name = "simple_etx_misc":
 *   Device file created at /dev/simple_etx_misc automatically.
 *   No class_create() or device_create() needed — misc_register handles it.
 *
 * .fops = &fops:
 *   Pointer to our file operations table above.
 *   Same concept as character driver — links syscalls to our functions.
 */
struct miscdevice etx_misc_device = {
    .minor = MISC_DYNAMIC_MINOR,   /* auto-assign minor number                */
    .name  = "simple_etx_misc",    /* /dev/simple_etx_misc created on insmod  */
    .fops  = &fops,                /* link to our file operations              */
};


/* ── MODULE INIT ──────────────────────────────────────────────────────────── */
/*
 * misc_init() — runs on: sudo insmod misc_driver.ko
 *
 * misc_register(&etx_misc_device) replaces ALL of these:
 *   alloc_chrdev_region() → assigns major=10, auto minor
 *   cdev_init()           → initializes internal cdev
 *   cdev_add()            → registers cdev with kernel
 *   class_create()        → creates /sys/class/ entry
 *   device_create()       → creates /dev/simple_etx_misc via udev
 *
 * Returns 0 on success, negative errno on failure.
 * If it fails → /dev/simple_etx_misc will NOT be created → return error.
 */
static int __init misc_init(void)
{
        int error;

        /*
         * misc_register() — THE ONLY CALL NEEDED TO REGISTER THE DEVICE.
         * Internally does everything a character driver init does in 5 calls.
         * After this succeeds: /dev/simple_etx_misc exists and is usable.
         */
        error = misc_register(&etx_misc_device);
        if (error) {
            pr_err("misc_register failed!!!\n");
            return error;   /* return error code — module fails to load     */
        }

        pr_info("misc_register init done!!!\n");
        return 0;   /* success — /dev/simple_etx_misc is now ready         */
}


/* ── MODULE EXIT ──────────────────────────────────────────────────────────── */
/*
 * misc_exit() — runs on: sudo rmmod misc_driver
 *
 * misc_deregister(&etx_misc_device) replaces ALL of these:
 *   device_destroy()           → removes /dev/simple_etx_misc
 *   class_destroy()            → removes /sys/class/ entry
 *   cdev_del()                 → removes internal cdev
 *   unregister_chrdev_region() → releases major/minor numbers
 *
 * Just ONE call to clean up everything.
 */
static void __exit misc_exit(void)
{
        /*
         * misc_deregister() — THE ONLY CALL NEEDED TO UNREGISTER THE DEVICE.
         * After this: /dev/simple_etx_misc is removed, major 10 slot freed.
         */
        misc_deregister(&etx_misc_device);
        pr_info("misc_register exit done!!!\n");
}

module_init(misc_init);
module_exit(misc_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("EmbeTronicX <embetronicx@gmail.com>");
MODULE_DESCRIPTION("A simple device driver - Misc Driver");
MODULE_VERSION("1.29");

/*
 * Complete flow summary
insmod misc_driver.ko
  └── misc_register(&etx_misc_device)
        ├── assigns major=10, minor=53 (auto)
        ├── registers internally with kernel
        └── udev creates /dev/simple_etx_misc

ls -l /dev/simple_etx_misc
  → crw------- 1 root root 10, 53 → major=10, minor=53

echo 1 > /dev/simple_etx_misc
  ├── etx_misc_open()   → "EtX misc device open"
  ├── etx_misc_write()  → "EtX misc device write"
  └── etx_misc_close()  → "EtX misc device close"

cat /dev/simple_etx_misc
  ├── etx_misc_open()   → "EtX misc device open"
  ├── etx_misc_read()   → "EtX misc device read"
  └── etx_misc_close()  → "EtX misc device close"

rmmod misc_driver
  └── misc_deregister(&etx_misc_device)
        └── /dev/simple_etx_misc removed
 */
