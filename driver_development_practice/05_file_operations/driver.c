/***************************************************************************//**
*  \details    Simple Linux device driver (File Operations)

echo 1 > /dev/etx_device
or > echo 1 | sudo tee /dev/etx_device
Echo will open the driver and write 1 into the driver and finally close the driver. So if I do echo to our driver, it should call the open, write and release functions. Just check.
ubuntu@primary:/ldd/ file_operations/$ echo 1 > /dev/etx_device

Do cat > /dev/etx_device
cat command will open the driver, read the driver, and close the driver. So if I do cat to our driver, it should call the open, read, and release functions. Just check
ubuntu@primary:/ldd/ file_operations/$ cat > /dev/etx_device

sudo dmesg to view the results
*******************************************************************************/
#include <linux/kernel.h>   /* pr_info, pr_err — kernel print functions       */
#include <linux/init.h>     /* __init, __exit — memory optimization macros     */
#include <linux/module.h>   /* module_init, module_exit, THIS_MODULE           */
#include <linux/kdev_t.h>   /* dev_t type, MAJOR(), MINOR() macros             */
#include <linux/fs.h>       /* file_operations, alloc_chrdev_region, etc.      */
#include <linux/err.h>      /* IS_ERR() — error checking for pointer returns   */
#include <linux/cdev.h>     /* struct cdev, cdev_init, cdev_add, cdev_del      */
#include <linux/device.h>   /* class_create, device_create — auto /dev entry   */

dev_t dev = 0;
static struct class *dev_class;
static struct cdev etx_cdev;

/* Function Prototypes */
static int      __init etx_driver_init(void);
static void     __exit etx_driver_exit(void);
static int      etx_open(struct inode *inode, struct file *file);
static int      etx_release(struct inode *inode, struct file *file);
static ssize_t  etx_read(struct file *filp, char __user *buf, size_t len,loff_t * off);
static ssize_t  etx_write(struct file *filp, const char *buf, size_t len, loff_t * off);

static struct file_operations fops =
{
    .owner      = THIS_MODULE,
    .read       = etx_read,
    .write      = etx_write,
    .open       = etx_open,
    .release    = etx_release,
};

/* This function will be called when we open the Device file */
//Triggered by: open("/dev/etx_device", O_RDWR) or implicitly by: echo, cat, etc.
static int etx_open(struct inode *inode, struct file *file)
{
        pr_info("Driver Open Function Called...!!!\n");
        return 0;
}

/* This function will be called when we close the Device file */
static int etx_release(struct inode *inode, struct file *file)
{
        pr_info("Driver Release Function Called...!!!\n");
        return 0;
}

/* This function will be called when we read the Device file */
// Triggered by: cat /dev/etx_device or: read() system call from a program
static ssize_t etx_read(struct file *filp, char __user *buf, size_t len, loff_t *off)
{
        pr_info("Driver Read Function Called...!!!\n");
        return 0;
}

/*  This function will be called when we write the Device file */
// Triggered by: echo 1 > /dev/etx_device or: write() system call from a program
static ssize_t etx_write(struct file *filp, const char __user *buf, size_t len, loff_t *off)
{
        pr_info("Driver Write Function Called...!!!\n");
        return len;
}

/* ─── MODULE INIT ─────────────────────────────────────────────────────────── */
/*
 * etx_driver_init() — called when module is loaded (insmod)
 *
 * __init: kernel frees this function's memory after loading
 *         since it's never called again — saves RAM.
 *
 * Initialization order (each step depends on the previous):
 *   1. alloc_chrdev_region → get a Major:Minor number
 *   2. cdev_init           → link fops to our cdev structure
 *   3. cdev_add            → register cdev with kernel (device goes LIVE)
 *   4. class_create        → create /sys/class/etx_class/ (udev watches this)
 *   5. device_create       → udev auto-creates /dev/etx_device
 *
 * On any failure → goto labels clean up in reverse order (no resource leaks)
 */
static int __init etx_driver_init(void)
{
        /*Allocating Major number*/
        if((alloc_chrdev_region(&dev, 0, 1, "etx_Dev")) <0){
                pr_err("Cannot allocate major number\n");
                return -1;
        }
        pr_info("Major = %d Minor = %d \n",MAJOR(dev), MINOR(dev));

        /*Creating cdev structure*/
        cdev_init(&etx_cdev,&fops);

        /*Adding character device to the system*/
        if((cdev_add(&etx_cdev,dev,1)) < 0){
            pr_err("Cannot add the device to the system\n");
            goto r_class;
        }

        /*Create a device class → appears as /sys/class/etx_class/ */
        if(IS_ERR(dev_class = class_create("etx_class"))){
            pr_err("Cannot create the struct class\n");
            goto r_class;
        }

        /* Create the actual device node.
	 * This triggers udev to automatically create /dev/etx_device.*/
        if(IS_ERR(device_create(dev_class,NULL,dev,NULL,"etx_device"))){
            pr_err("Cannot create the Device 1\n");
            goto r_device;
        }
        pr_info("Device Driver Insert...Done!!!\n");
      return 0;

r_device:
        class_destroy(dev_class);
r_class:
        unregister_chrdev_region(dev,1);
        return -1;
}

/* Module exit function */
static void __exit etx_driver_exit(void)
{
        device_destroy(dev_class, dev);      /* Remove /dev/etx_device        */
        class_destroy(dev_class);            /* Remove /sys/class/etx_class/  */
        cdev_del(&etx_cdev);                 /* Unregister cdev from kernel   */
        unregister_chrdev_region(dev, 1);    /* Release Major:Minor numbers   */
        pr_info("Device Driver Remove...Done!!!\n");
}

module_init(etx_driver_init);
module_exit(etx_driver_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("EmbeTronicX <embetronicx@gmail.com>");
MODULE_DESCRIPTION("Simple Linux device driver (File Operations)");
MODULE_VERSION("1.3");

/*
 insmod driver.ko
│
├── alloc_chrdev_region()  → assigns Major:Minor  (e.g., 246:0)
├── cdev_init()            → links fops to cdev
├── cdev_add()             → device is now LIVE in kernel
├── class_create()         → creates /sys/class/etx_class/
└── device_create()        → udev creates /dev/etx_device


echo 1 > /dev/etx_device          cat /dev/etx_device
│                                  │
├── etx_open()                     ├── etx_open()
├── etx_write()                    ├── etx_read()
└── etx_release()                  └── etx_release()


rmmod driver
│
├── device_destroy()       → removes /dev/etx_device
├── class_destroy()        → removes /sys/class/etx_class/
├── cdev_del()             → unregisters cdev
└── unregister_chrdev_region() → releases Major:Minor
 */
