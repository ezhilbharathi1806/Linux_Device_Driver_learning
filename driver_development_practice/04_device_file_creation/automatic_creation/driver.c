/***************************************************************************//**
*  \details    Simple linux driver (Automatically Creating a Device file)
*  This is a minimal Linux kernel module that demonstrates how to automatically create a device file under /dev/ using the kernel's class and device APIs — no manual mknod needed.
=======================================================================
Module Load (insmod)
      │
      ▼
alloc_chrdev_region()   → Asks kernel for a Major:Minor number
      │
      ▼
class_create()          → Creates /sys/class/etx_class/
      │
      ▼
device_create()         → Triggers udev → auto-creates /dev/etx_device
=======================================================================

ubuntu@primary:/ldd/ automatic_creation/$ ls -l /dev/ | grep "etx_device" 
crw------- 1  root  root  246, 0  Aug 15  13:36  etx_device
*******************************************************************************/

#include <linux/kernel.h>	// Core kernel header - provides printk, pr_info, pr_err etc
#include <linux/init.h>		// provides __init and __exit macros for making init and cleanup functions
#include <linux/module.h>	// Essential for all kernel modules - module_init(), module_exit(), MODULE_* macros
#include <linux/kdev_t.h>	// provides dev_t type and Major()/MINOR() macros to work with device numbers
#include <linux/fs.h>		// File system support - alloc_chrdev_region(), unregister_chrdev_region()
#include <linux/err.h>		// Error handling macro - IS_ERR(), PTR_ERR()
#include <linux/device.h>	// device and class management - class_create(), device_create() etc

/*
 * dev_t is a 32-bit value encoding both Major and Minor numbers.
 *   - Major number → identifies which driver handles this device
 *   - Minor number → identifies the specific device instance
 * Initialized to 0; the kernel will assign the actual value dynamically.
 */
dev_t dev = 0;

/*
 * Pointer to the device class.
 * A class is a higher-level view of a device that abstracts the hardware type.
 * It creates an entry under /sys/class/<class_name>/
 * which udev watches to automatically create /dev/<device_name>.
 */
static struct class *dev_class;

/* Module init function */
static int __init hello_world_init(void)
{
        /*Allocating Major number*/
        if((alloc_chrdev_region(&dev, 0, 1, "etx_Dev")) <0){
                pr_err("Cannot allocate major number for device\n");
                return -1;
        }
        pr_info("Major = %d Minor = %d \n",MAJOR(dev), MINOR(dev));
 
        /*
	 * class_create() — Create a device class visible in /sys/class/
         *
         * Arguments:
         *   "etx_class"  → name of the class → creates /sys/class/etx_class/
         *
         * This is what tells udev "a new class of device exists."
         * udev then watches for devices added under this class.
	 */
        dev_class = class_create("etx_class");
        if(IS_ERR(dev_class)){
            pr_err("Cannot create the struct class for device\n");
            goto r_class;
        }
 
        /*
	 * device_create() — Create the actual device and register it under the class.
         *
         * Arguments:
         *   dev_class   → the class this device belongs to
         *   NULL        → no parent device
         *   dev         → the Major:Minor device number
         *   NULL        → no additional driver data
         *   "etx_device"→ device name → udev creates /dev/etx_device
         *
         * This creates:
         *   /sys/class/etx_class/etx_device/   ← sysfs entry
         *   /dev/etx_device                     ← auto-created by udev
         *
         * Returns a pointer; IS_ERR() used to check for failure.
	 */
        if(IS_ERR(device_create(dev_class,NULL,dev,NULL,"etx_device"))){
            pr_err("Cannot create the Device\n");
            goto r_device;
        }
        pr_info("Kernel Module Inserted Successfully...\n");
        return 0;

/* this is a goto chain - cleanup lables (reverse order of initialization)*/
r_device:
        class_destroy(dev_class);
r_class:
        unregister_chrdev_region(dev,1);
        return -1;
}
 
/* Module exit function */
static void __exit hello_world_exit(void)
{
	/* Cleanup in REVERSE order of initialization — always */

        device_destroy(dev_class,dev);       /* Remove /dev/etx_device and sysfs entry */
        class_destroy(dev_class);            /* Remove /sys/class/etx_class/ */
        unregister_chrdev_region(dev, 1);    /* Release the Major:Minor number back to kernel */

        pr_info("Kernel Module Removed Successfully...\n");
}
 
module_init(hello_world_init);
module_exit(hello_world_exit);
 
MODULE_LICENSE("GPL");
MODULE_AUTHOR("EmbeTronicX <embetronicx@gmail.com>");
MODULE_DESCRIPTION("Simple linux driver (Automatically Creating a Device file)");
MODULE_VERSION("1.2");
