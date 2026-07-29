/***************************************************************************//**
*  \details    Simple Linux device driver (IOCTL)
*
*******************************************************************************/
#include <linux/kernel.h>    /* pr_info, pr_err                                */
#include <linux/init.h>      /* __init, __exit                                 */
#include <linux/module.h>    /* module_init, module_exit, THIS_MODULE           */
#include <linux/kdev_t.h>    /* dev_t, MAJOR(), MINOR()                        */
#include <linux/fs.h>        /* file_operations, alloc_chrdev_region            */
#include <linux/cdev.h>      /* cdev_init, cdev_add, cdev_del                  */
#include <linux/device.h>    /* class_create, device_create                    */
#include <linux/slab.h>      /* kmalloc(), kfree()                             */
#include <linux/uaccess.h>   /* copy_to_user(), copy_from_user()               */
#include <linux/ioctl.h>     /* _IOW, _IOR macros for defining IOCTL commands  */
#include <linux/err.h>       /* IS_ERR()                                        */
 
/* ── IOCTL COMMAND DEFINITIONS ───────────────────────────────────────────── */
/*
 * IOCTL commands are defined using kernel macros that encode 4 pieces of info
 * into a single 32-bit number:
 *   - Direction  : read, write, both, or none
 *   - Magic byte : unique ID for this driver ('a' here)
 *   - Cmd number : identifies which command within this driver
 *   - Data size  : size of the data type being transferred
 *
 * _IOW('a', 'a', int32_t*) → Write command (user→kernel)
 *   'a'      = magic number (identifies etx driver's ioctl set)
 *   'a'      = command number 1 (WR_VALUE)
 *   int32_t* = type of data being passed
 *
 * _IOR('a', 'b', int32_t*) → Read command (kernel→user)
 *   'a'      = same magic number
 *   'b'      = command number 2 (RD_VALUE)
 *   int32_t* = type of data being returned
 *
 * IMPORTANT: These exact same macros must be copied into the user app.
 *            The numbers must match perfectly — mismatches → wrong command called.
 */
#define WR_VALUE _IOW('a','a',int32_t*)		//user sends a value to kernel
#define RD_VALUE _IOR('a','b',int32_t*)		//user reads a value from kernel
 
int32_t value = 0;	//This is the global variable that IOCTL will read and write.
 
/* Standard character driver globals */
dev_t dev = 0;                   /* will hold assigned Major:Minor numbers     */
static struct class *dev_class;  /* device class → /sys/class/etx_class/       */
static struct cdev etx_cdev;     /* kernel's internal char device structure    */

/* =================== Function Prototypes ===================*/
static int      __init etx_driver_init(void);
static void     __exit etx_driver_exit(void);
static int      etx_open(struct inode *inode, struct file *file);
static int      etx_release(struct inode *inode, struct file *file);
static ssize_t  etx_read(struct file *filp, char __user *buf, size_t len,loff_t * off);
static ssize_t  etx_write(struct file *filp, const char *buf, size_t len, loff_t * off);
static long     etx_ioctl(struct file *file, unsigned int cmd, unsigned long arg);

/*=================== File operation sturcture ===================*/
static struct file_operations fops =
{
        .owner          = THIS_MODULE,
        .read           = etx_read,
        .write          = etx_write,
        .open           = etx_open,
        .unlocked_ioctl = etx_ioctl,
        .release        = etx_release,
};

/* ── DRIVER FUNCTIONS ────────────────────────────────────────────────────── */
/* This function will be called when we open the Device file*/
static int etx_open(struct inode *inode, struct file *file)
{
        pr_info("Device File Opened...!!!\n");
        return 0;
}

/* This function will be called when we close the Device file*/
static int etx_release(struct inode *inode, struct file *file)
{
        pr_info("Device File Closed...!!!\n");
        return 0;
}

/* This function will be called when we read the Device file*/
static ssize_t etx_read(struct file *filp, char __user *buf, size_t len, loff_t *off)
{
        pr_info("Read Function\n");
        return 0;
}

/* This function will be called when we write the Device file*/
static ssize_t etx_write(struct file *filp, const char __user *buf, size_t len, loff_t *off)
{
        pr_info("Write function\n");
        return len;
}

/* This function will be called when we write IOCTL command on the Device file*/
/*
 * etx_ioctl() — the IOCTL handler — called when user calls ioctl()
 *
 * This is the CORE function of this tutorial.
 *
 * @file : open file structure (not used here)
 * @cmd  : the IOCTL command number sent by the user (WR_VALUE or RD_VALUE)
 *         The kernel matches this against the values defined by our macros.
 * @arg  : the argument from user space — passed as unsigned long.
 *         For pointers, cast it: (int32_t*) arg
 *         For values,   cast it: (int32_t)  arg
 *
 * Returns: 0 on success, negative error code on failure.
 *
 * Flow:
 *   User calls ioctl(fd, WR_VALUE, &number)
 *     → kernel routes to etx_ioctl(file, WR_VALUE, address_of_number)
 *     → switch matches WR_VALUE
 *     → copy_from_user copies the integer from user address into kernel's 'value'
 *
 *   User calls ioctl(fd, RD_VALUE, &value)
 *     → kernel routes to etx_ioctl(file, RD_VALUE, address_of_value)
 *     → switch matches RD_VALUE
 *     → copy_to_user copies kernel's 'value' to user's variable
 */
static long etx_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
         switch(cmd) {
                case WR_VALUE:
                        if( copy_from_user(&value ,(int32_t*) arg, sizeof(value)) )
                        {
                                pr_err("Data Write : Err!\n");
                        }
                        pr_info("Value = %d\n", value);	// log what we receive in kernel
                        break;
                case RD_VALUE:
                        if( copy_to_user((int32_t*) arg, &value, sizeof(value)) )
                        {
                                pr_err("Data Read : Err!\n");
                        }
                        break;
                default:
			 /* Unknown command — log and ignore.
                         * In production, return -EINVAL here to signal error.
                         */
                        pr_info("Default\n");
                        break;
        }
        return 0;
}
 
/* ── MODULE INIT ──────────────────────────────────────────────────────────── */
/*
 * etx_driver_init() — runs on: sudo insmod driver.ko
 *
 * Same 5-step init as previous tutorials:
 *  1. alloc_chrdev_region → get Major:Minor
 *  2. cdev_init           → link fops to cdev
 *  3. cdev_add            → register with kernel
 *  4. class_create        → create /sys/class/etx_class/
 *  5. device_create       → udev creates /dev/etx_device
 *
 * NOTE: No kmalloc() here — this driver uses a simple global int32_t,
 *       not a dynamically allocated buffer.
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
 
        /*Creating struct class*/
        if(IS_ERR(dev_class = class_create("etx_class"))){
            pr_err("Cannot create the struct class\n");
            goto r_class;
        }
 
        /*Creating device*/
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

/* Module exit function*/
static void __exit etx_driver_exit(void)
{
	device_destroy(dev_class, dev);      /* remove /dev/etx_device        */
        class_destroy(dev_class);            /* remove /sys/class/etx_class/  */
        cdev_del(&etx_cdev);                 /* unregister cdev               */
        unregister_chrdev_region(dev, 1);    /* release Major:Minor           */
        pr_info("Device Driver Remove...Done!!!\n");
}
 
module_init(etx_driver_init);
module_exit(etx_driver_exit);
 
MODULE_LICENSE("GPL");
MODULE_AUTHOR("EmbeTronicX <embetronicx@gmail.com>");
MODULE_DESCRIPTION("Simple Linux device driver (IOCTL)");
MODULE_VERSION("1.5");
