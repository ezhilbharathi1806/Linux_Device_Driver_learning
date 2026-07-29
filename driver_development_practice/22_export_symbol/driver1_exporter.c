/***************************************************************************//**
*  \file       driver1_exporter.c
*  \details    EXPORT_SYMBOL example — Module that EXPORTS symbols
*
*  This module owns etx_count and etx_shared_func.
*  It exports them so driver2.ko can use them directly.
*
*  LOAD ORDER:  insmod driver1.ko FIRST, then insmod driver2.ko
*  UNLOAD ORDER: rmmod driver2 FIRST, then rmmod driver1
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


/* ── EXPORTED SYMBOLS ────────────────────────────────────────────────────── */

/*
 * etx_count — exported global variable.
 * driver2.ko can READ and WRITE this directly using 'extern int etx_count'.
 * Must NOT be static — static variables are module-local only.
 */
int etx_count = 0;

/*
 * etx_shared_func() — exported function.
 * Called directly from driver2's etx_read().
 * Prints a message and increments etx_count each time it's called.
 * Must NOT be static or inline.
 */
void etx_shared_func(void)
{
        pr_info("Shared function been called!!!\n");
        etx_count++;   /* increment shared counter each call */
}

/*
 * EXPORT_SYMBOL(etx_shared_func):
 *   Adds 'etx_shared_func' to the kernel's global symbol table.
 *   Any loaded module can now call it directly — no kernel recompile needed.
 *   Appears in Module.symvers after make, and in /proc/kallsyms after insmod.
 *
 * Alternative: EXPORT_SYMBOL_GPL(etx_shared_func)
 *   Same but ONLY usable by GPL-licensed modules (MODULE_LICENSE("GPL")).
 *   Non-GPL module trying to use it → insmod error at load time.
 */
EXPORT_SYMBOL(etx_shared_func);   /* export function to all modules           */
EXPORT_SYMBOL(etx_count);         /* export variable to all modules           */


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

/* Driver1's own read — just logs, returns 1 (non-zero = some data) */
static ssize_t etx_read(struct file *filp, char __user *buf, size_t len, loff_t *off)
{
        pr_info("Data Read : Done!\n");
        return 1;
}

static ssize_t etx_write(struct file *filp, const char __user *buf, size_t len, loff_t *off)
{
        pr_info("Data Write : Done!\n");
        return len;
}


/* ── MODULE INIT ──────────────────────────────────────────────────────────── */
/*
 * Standard char device setup for /dev/etx_device1.
 * EXPORT_SYMBOL macros are placed ABOVE — they take effect at MODULE BUILD time,
 * not at runtime. Exported symbols are available as soon as this module loads.
 */
static int __init etx_driver_init(void)
{
        if((alloc_chrdev_region(&dev, 0, 1, "etx_Dev1")) < 0){
                pr_err("Cannot allocate major number\n");
                return -1;
        }
        pr_info("Major = %d Minor = %d \n", MAJOR(dev), MINOR(dev));

        cdev_init(&etx_cdev, &fops);

        if((cdev_add(&etx_cdev, dev, 1)) < 0){
            pr_err("Cannot add the device to the system\n");
            goto r_class;
        }

        if(IS_ERR(dev_class = class_create("etx_class1"))){
            pr_err("Cannot create the struct class\n");
            goto r_class;
        }

        if(IS_ERR(device_create(dev_class, NULL, dev, NULL, "etx_device1"))){
            pr_err("Cannot create the Device 1\n");
            goto r_device;
        }

        pr_info("Device Driver 1 Insert...Done!!!\n");
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
 * ⚠️ driver2 MUST be unloaded before driver1.
 * If driver1 is removed while driver2 still uses its symbols:
 *   rmmod: ERROR: Module driver1 is in use by driver2
 * Kernel tracks symbol usage and prevents this — safe by design.
 */
static void __exit etx_driver_exit(void)
{
        device_destroy(dev_class, dev);
        class_destroy(dev_class);
        cdev_del(&etx_cdev);
        unregister_chrdev_region(dev, 1);
        pr_info("Device Driver 1 Remove...Done!!!\n");
}

module_init(etx_driver_init);
module_exit(etx_driver_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("EmbeTronicX <embetronicx@gmail.com>");
MODULE_DESCRIPTION("EXPORT_SYMBOL Driver - 1");
MODULE_VERSION("1.25");
