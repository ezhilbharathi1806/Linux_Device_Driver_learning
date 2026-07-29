/*
 * ioctl.c
 * ioctl system calls to communicate between user space and kernel space.
 */
#include <linux/cdev.h>		//for character device registration
#include <linux/fs.h>		// file operations structure
#include <linux/init.h>		// macro for module init/exit
#include <linux/ioctl.h>	// ioctl macro
#include <linux/module.h>	// core module definitions
#include <linux/slab.h>		// kmalloc, kfree
#include <linux/uaccess.h>	// copy_to_user and copy_from_user
#include <linux/version.h>

struct ioctl_arg {	/* Structure used to exchange data between user space and kernel */
    unsigned int val;	//value passed between user and kernel
};

/* Documentation/userspace-api/ioctl/ioctl-number.rst */
#define IOC_MAGIC '\x66'	/* Unique magic number for ioctl commands */

/*
 * IOCTL command definitions:
 * _IOW → user writes data to kernel
 * _IOR → user reads data from kernel
 * _IO  → no data transfer, just command
 */
#define IOCTL_VALSET _IOW(IOC_MAGIC, 0, struct ioctl_arg)	//write value
#define IOCTL_VALGET _IOR(IOC_MAGIC, 1, struct ioctl_arg)	//read value
#define IOCTL_VALGET_NUM _IOR(IOC_MAGIC, 2, int)		//read global number
#define IOCTL_VALSET_NUM _IO(IOC_MAGIC, 3)			//set global number

#define IOCTL_VAL_MAXNR 3		// maximum command number
#define DRIVER_NAME "ioctltest"

/* Device-related global variables */
static unsigned int test_ioctl_major = 0;	//major number of device
static unsigned int num_of_dev = 1;		// number of devices
static struct cdev test_ioctl_cdev;		//character device structure
static int ioctl_num = 0;			// global integer (shared)

/*
 * Per-file/device data structure
 * Each open() call gets its own instance
 */
struct test_ioctl_data {
    unsigned char val;	// stored value for this file instance
    rwlock_t lock;	//read-write lock for concurrency protection
};

/*
 * IOCTL handler function
 * Called when user program invokes ioctl()
 */
static long test_ioctl_ioctl(struct file *filp, unsigned int cmd,
                             unsigned long arg)
{
    struct test_ioctl_data *ioctl_data = filp->private_data;	// Get device-specific data stored in file structure
								//
    int retval = 0;
    unsigned char val;

    struct ioctl_arg data;	//structure for copying data to/from user
    memset(&data, 0, sizeof(data));	//Initialize structure

    switch (cmd) {
    case IOCTL_VALSET:
	/*
         * Copy data from user space to kernel space
         * arg is a pointer to user data
         */
        if (copy_from_user(&data, (int __user *)arg, sizeof(data))) {
            retval = -EFAULT;
            goto done;
        }

        pr_alert("IOCTL set val:%x .\n", data.val);
        write_lock(&ioctl_data->lock);	//Acquire write lock before modifying shared data
        ioctl_data->val = data.val;	//update stored value
        write_unlock(&ioctl_data->lock);
        break;

    case IOCTL_VALGET:
        read_lock(&ioctl_data->lock);	//Acquire read lock before reading shared data
        val = ioctl_data->val;		//read stored value
        read_unlock(&ioctl_data->lock);
        data.val = val;

	/* copy data from kernel space to user space */
        if (copy_to_user((int __user *)arg, &data, sizeof(data))) {
            retval = -EFAULT;
            goto done;
        }

        break;

    case IOCTL_VALGET_NUM:
        retval = __put_user(ioctl_num, (int __user *)arg);	//Return global variable to user space
        break;

    case IOCTL_VALSET_NUM:
        ioctl_num = arg;	//set global variable directly from arg
        break;

    default:
        retval = -ENOTTY;	// Invalid ioctl command
    }

done:
    return retval;
}

/*
 * Read system call implementation
 * Copies stored value repeatedly into user buffer
 */
static ssize_t test_ioctl_read(struct file *filp, char __user *buf,
                               size_t count, loff_t *f_pos)
{
    struct test_ioctl_data *ioctl_data = filp->private_data;
    unsigned char val;
    int retval;
    int i = 0;
	
    //read value safely using lock
    read_lock(&ioctl_data->lock);
    val = ioctl_data->val;
    read_unlock(&ioctl_data->lock);
	
    //Fill user buffer with same value 'count' times
    for (; i < count; i++) {
        if (copy_to_user(&buf[i], &val, 1)) {
            retval = -EFAULT;
            goto out;
        }
    }

    retval = count;	// number of bytes read
out:
    return retval;
}

/* Called when file/device is closed */
static int test_ioctl_close(struct inode *inode, struct file *filp)
{
    pr_alert("%s call.\n", __func__);
	
    //free memory allocation during open()
    if (filp->private_data) {
        kfree(filp->private_data);
        filp->private_data = NULL;
    }

    return 0;
}

/* called when device is opened */
static int test_ioctl_open(struct inode *inode, struct file *filp)
{
    struct test_ioctl_data *ioctl_data;

    pr_alert("%s call.\n", __func__);
    ioctl_data = kmalloc(sizeof(struct test_ioctl_data), GFP_KERNEL);	//allocate memory for device specific data

    if (ioctl_data == NULL)
        return -ENOMEM;
	
    rwlock_init(&ioctl_data->lock);	//initialize lock
    ioctl_data->val = 0xFF;	//default value
    filp->private_data = ioctl_data;	//store pointer in file structure

    return 0;
}

/*
 * File operations structure
 * Maps system calls to driver functions
 */
static struct file_operations fops = {
    .owner = THIS_MODULE,
    .open = test_ioctl_open,		//open()
    .release = test_ioctl_close,	//close()
    .read = test_ioctl_read,		//read()
    .unlocked_ioctl = test_ioctl_ioctl,	//ioctl()
};

/* Module initialization function | Called when the module is loaded*/
static int __init ioctl_init(void)
{
    dev_t dev;
    int ret;
	
    // Allocate major/minor numbers dynamically
    ret = alloc_chrdev_region(&dev, 0, num_of_dev, DRIVER_NAME);
    if (ret)
        return ret;
    test_ioctl_major = MAJOR(dev);

    // Initialize and add character device to kernel
    cdev_init(&test_ioctl_cdev, &fops);
    ret = cdev_add(&test_ioctl_cdev, dev, num_of_dev);

    if (ret) {
        unregister_chrdev_region(dev, num_of_dev);
        return ret;
    }

    pr_alert("%s driver(major: %d) installed.\n", DRIVER_NAME,
             test_ioctl_major);
    return 0;
}

/* Module cleanup function | Called when module is removed */
static void __exit ioctl_exit(void)
{
    dev_t dev = MKDEV(test_ioctl_major, 0);
	
    // Remove device and free allocated number
    cdev_del(&test_ioctl_cdev);
    unregister_chrdev_region(dev, num_of_dev);
    pr_alert("%s driver removed.\n", DRIVER_NAME);
}

module_init(ioctl_init);
module_exit(ioctl_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("This is test_ioctl module");

/*
>make
>sudo insmod ioctl.ko

>sudo dmesg
	[1824582.739412] ioctltest driver(major: 241) installed.

Create Device File
	>sudo mknod /dev/ioctltest c 241 0

Execute user space code
	>gcc ioctl_user_space_code.c  -o test
	>sudo ./test
		Value from driver: 0x55
	
>sudo dmesg
[1827403.352725] test_ioctl_open call.
[1827403.356051] IOCTL set val:55 .
[1827403.361136] test_ioctl_close call.

>sudo rmmod ioctl
 */
