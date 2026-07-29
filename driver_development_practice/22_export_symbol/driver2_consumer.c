/***************************************************************************//**
*  \file       driver2_consumer.c
*  \details    EXPORT_SYMBOL example — Module that USES exported symbols
*
*  Uses etx_count and etx_shared_func from driver1.ko.
*  driver1.ko MUST be loaded first — driver2 depends on it.
*******************************************************************************/

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/kdev_t.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/err.h>

dev_t dev = 0;
static struct class *dev_class;
static struct cdev etx_cdev;

static int      __init etx_driver_init(void);
static void     __exit etx_driver_exit(void);
static int      etx_open(struct inode *inode, struct file *file);
static int      etx_release(struct inode *inode, struct file *file);
static ssize_t  etx_read(struct file *filp, char __user *buf, size_t len, loff_t *off);
static ssize_t  etx_write(struct file *filp, const char *buf, size_t len, loff_t *off);


/* ── EXTERN DECLARATIONS — using symbols from driver1 ────────────────────── */

/*
 * extern int etx_count:
 *   Tells THIS module's compiler: "etx_count is defined SOMEWHERE ELSE".
 *   At link time: kernel resolves it to driver1's etx_count (via Module.symvers).
 *   Without extern → compiler error: "undeclared identifier".
 *   Without EXPORT_SYMBOL in driver1 → insmod error: "Unknown symbol".
 *
 * NOTE: extern is needed for variables.
 * Functions are extern by default in C — no 'extern' keyword strictly needed,
 * but adding it makes the intent clear.
 */
extern int etx_count;              /* variable exported by driver1            */
void etx_shared_func(void);        /* function exported by driver1 (default extern) */


/* ── FILE OPERATIONS TABLE ───────────────────────────────────────────────── */
static struct file_operations fops =
{
        .owner   = THIS_MODULE,
        .read    = etx_read,
        .write   = etx_write,
        .open    = etx_open,
        .release = etx_release,
};


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
 * etx_read() — directly calls driver1's exported function and reads its variable
 * Triggered by: sudo cat /dev/etx_device2
 *
 * etx_shared_func() — defined in driver1.c, exported via EXPORT_SYMBOL.
 *   Called here AS IF it were a local function — no special syntax needed.
 *   Kernel resolves the address at module load time from the symbol table.
 *
 * etx_count — defined in driver1.c, exported via EXPORT_SYMBOL.
 *   Read directly here — no IOCTL or file I/O needed between modules.
 *   Modified by etx_shared_func() (incremented) — reflects updated value.
 */
static ssize_t etx_read(struct file *filp, char __user *buf, size_t len, loff_t *off)
{
        etx_shared_func();   /* call driver1's exported function directly      */
        pr_info("%d time(s) shared function called!\n", etx_count);  /* driver1's var */
        pr_info("Data Read : Done!\n");
        return 0;
}

static ssize_t etx_write(struct file *filp, const char __user *buf, size_t len, loff_t *off)
{
        pr_info("Data Write : Done!\n");
        return len;
}


/* ── MODULE INIT ──────────────────────────────────────────────────────────── */
/*
 * Standard char device setup for /dev/etx_device2.
 *
 * ⚠️ This module can ONLY be loaded AFTER driver1.ko.
 * At insmod time, the kernel checks Module.symvers / symbol table for
 * etx_shared_func and etx_count. If driver1 isn't loaded:
 *   insmod: ERROR: could not insert module driver2.ko: Unknown symbol in module
 */
static int __init etx_driver_init(void)
{
        if((alloc_chrdev_region(&dev, 0, 1, "etx_Dev2")) < 0){
                pr_err("Cannot allocate major number\n");
                return -1;
        }
        pr_info("Major = %d Minor = %d \n", MAJOR(dev), MINOR(dev));

        cdev_init(&etx_cdev, &fops);

        if((cdev_add(&etx_cdev, dev, 1)) < 0){
            pr_err("Cannot add the device to the system\n");
            goto r_class;
        }

        if(IS_ERR(dev_class = class_create("etx_class2"))){
            pr_err("Cannot create the struct class\n");
            goto r_class;
        }

        if(IS_ERR(device_create(dev_class, NULL, dev, NULL, "etx_device2"))){
            pr_err("Cannot create the Device 1\n");
            goto r_device;
        }

        pr_info("Device Driver 2 Insert...Done!!!\n");
        return 0;

r_device:
        class_destroy(dev_class);
r_class:
        unregister_chrdev_region(dev, 1);
        return -1;
}


/* ── MODULE EXIT ──────────────────────────────────────────────────────────── */
/*
 * Standard cleanup.
 * MUST be unloaded BEFORE driver1 is unloaded.
 * Kernel reference counting prevents driver1 removal while driver2 is loaded.
 */
static void __exit etx_driver_exit(void)
{
        device_destroy(dev_class, dev);
        class_destroy(dev_class);
        cdev_del(&etx_cdev);
        unregister_chrdev_region(dev, 1);
        pr_info("Device Driver 2 Remove...Done!!!\n");
}

module_init(etx_driver_init);
module_exit(etx_driver_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("EmbeTronicX <embetronicx@gmail.com>");
MODULE_DESCRIPTION("EXPORT_SYMBOL Driver - 2");
MODULE_VERSION("1.26");
