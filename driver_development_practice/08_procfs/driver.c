/***************************************************************************//**
*  \details    Simple Linux device driver (procfs)

ubuntu@primary:~/ldd/08_procfs$ ls /proc/
ubuntu@primary:~/ldd/08_procfs$ cat /proc/etx/etx_proc 
try_proc_array

ubuntu@primary:~/ldd/08_procfs$ echo "device driver proc" > /proc/etx/etx_proc 
ubuntu@primary:~/ldd/08_procfs$ cat /proc/etx/etx_proc 
device driver proc
* *******************************************************************************/

#include <linux/kernel.h>    /* pr_info, pr_err                                */
#include <linux/init.h>      /* __init, __exit                                 */
#include <linux/module.h>    /* module_init, module_exit, THIS_MODULE           */
#include <linux/kdev_t.h>    /* dev_t, MAJOR(), MINOR()                        */
#include <linux/fs.h>        /* file_operations, alloc_chrdev_region            */
#include <linux/cdev.h>      /* cdev_init, cdev_add, cdev_del                  */
#include <linux/device.h>    /* class_create, device_create                    */
#include <linux/slab.h>      /* kmalloc(), kfree()                             */
#include <linux/uaccess.h>   /* copy_to_user(), copy_from_user()               */
#include <linux/ioctl.h>     /* _IOW, _IOR — IOCTL command macros              */
#include <linux/proc_fs.h>   /* proc_mkdir, proc_create, proc_remove — NEW     */
#include <linux/err.h>       /* IS_ERR()                                        */

/* 
** I am using the kernel 5.10.27-v7l. So I have set this as 510.
** If you are using the kernel 3.10, then set this as 310,
** and for kernel 5.1, set this as 501. Because the API proc_create()
** changed in kernel above v5.5.
**
*/ 
#define LINUX_KERNEL_VERSION  510

/* ── IOCTL COMMAND DEFINITIONS ───────────────── */
/*
 * These are kept from the previous IOCTL tutorial.
 * WR_VALUE: user writes an int32_t value into the kernel driver
 * RD_VALUE: user reads the int32_t value back from the kernel driver
 */
#define WR_VALUE _IOW('a','a',int32_t*)
#define RD_VALUE _IOR('a','b',int32_t*)
 
int32_t value = 0;	/* shared variable for IOCTL read/write           */

/*
 * etx_array: the buffer shared via the proc entry.
 * When user writes to /proc/etx/etx_proc → stored here (copy_from_user).
 * When user reads  from /proc/etx/etx_proc → sent from here (copy_to_user).
 * Pre-initialized with "try_proc_array\n" as default content.
 */
char etx_array[20]="try_proc_array\n";
static int len = 1;		//Reset back to 1 after each EOF so next cat starts fresh.
 
/* Standard character driver globals */
dev_t dev = 0;
static struct class *dev_class;
static struct cdev etx_cdev;
static struct proc_dir_entry *parent;

/* Function Prototypes*/
static int      __init etx_driver_init(void);
static void     __exit etx_driver_exit(void);

/*************** Driver Functions **********************/
static int      etx_open(struct inode *inode, struct file *file);
static int      etx_release(struct inode *inode, struct file *file);
static ssize_t  etx_read(struct file *filp, char __user *buf, size_t len,loff_t * off);
static ssize_t  etx_write(struct file *filp, const char *buf, size_t len, loff_t * off);
static long     etx_ioctl(struct file *file, unsigned int cmd, unsigned long arg);
 
/***************** Procfs Functions *******************/
static int      open_proc(struct inode *inode, struct file *file);
static int      release_proc(struct inode *inode, struct file *file);
static ssize_t  read_proc(struct file *filp, char __user *buffer, size_t length,loff_t * offset);
static ssize_t  write_proc(struct file *filp, const char *buff, size_t len, loff_t * off);

/*
** File operation sturcture
*/
static struct file_operations fops =
{
        .owner          = THIS_MODULE,
        .read           = etx_read,
        .write          = etx_write,
        .open           = etx_open,
        .unlocked_ioctl = etx_ioctl,
        .release        = etx_release,
};


#if ( LINUX_KERNEL_VERSION > 505 )

/* procfs operation sturcture */
static struct proc_ops proc_fops = {
        .proc_open = open_proc,
        .proc_read = read_proc,
        .proc_write = write_proc,
        .proc_release = release_proc
};

#else //LINUX_KERNEL_VERSION > 505

/* procfs file operation sturcture */
static struct file_operations proc_fops = {
        .open = open_proc,
        .read = read_proc,
        .write = write_proc,
        .release = release_proc
};

#endif //LINUX_KERNEL_VERSION > 505

/* This function will be called when we open the procfs file */
/* open_proc() — called when /proc/etx/etx_proc is opened */
static int open_proc(struct inode *inode, struct file *file)
{
    pr_info("proc file opend.....\t");
    return 0;
}

/* This function will be called when we close the procfs file */
/* release_proc() — called when /proc/etx/etx_proc is closed */
static int release_proc(struct inode *inode, struct file *file)
{
    pr_info("proc file released.....\n");
    return 0;
}

/* This function will be called when we read the procfs file
 *
 * read_proc() — called when user reads /proc/etx/etx_proc
 * Triggered by: cat /proc/etx/etx_proc
*/
static ssize_t read_proc(struct file *filp, char __user *buffer, size_t length,loff_t * offset)
{
    pr_info("proc file read.....\n");
    if(len)
    {
        len=0;
    }
    else
    {
        len=1;	/* reset for next cat invocation */
        return 0;
    }
    
    if( copy_to_user(buffer,etx_array,20) )
    {
        pr_err("Data Send : Err!\n");
    }
 
    return length;;
}

/* This function will be called when we write the procfs file
 *  write_proc() — called when user writes to /proc/etx/etx_proc
 * Triggered by: echo "hello" > /proc/etx/etx_proc
*/
static ssize_t write_proc(struct file *filp, const char *buff, size_t len, loff_t * off)
{
    pr_info("proc file wrote.....\n");
    
    if( copy_from_user(etx_array,buff,len) )
    {
        pr_err("Data Write : Err!\n");
    }
    
    return len;
}

/* ── DEVICE FILE HANDLER FUNCTIONS (same as previous tutorials) ──────────── */

/*
** This function will be called when we open the Device file
*/
static int etx_open(struct inode *inode, struct file *file)
{
        pr_info("Device File Opened...!!!\n");
        return 0;
}

/*
** This function will be called when we close the Device file
*/
static int etx_release(struct inode *inode, struct file *file)
{
        pr_info("Device File Closed...!!!\n");
        return 0;
}

/*
** This function will be called when we read the Device file
*/
static ssize_t etx_read(struct file *filp, char __user *buf, size_t len, loff_t *off)
{
        pr_info("Read function\n");
        return 0;
}

/*
** This function will be called when we write the Device file
*/
static ssize_t etx_write(struct file *filp, const char __user *buf, size_t len, loff_t *off)
{
        pr_info("Write Function\n");
        return len;
}

/*
** This function will be called when we write IOCTL on the Device file
*/
static long etx_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
         switch(cmd) {
                case WR_VALUE:
                        if( copy_from_user(&value ,(int32_t*) arg, sizeof(value)) )
                        {
                                pr_err("Data Write : Err!\n");
                        }
                        pr_info("Value = %d\n", value);
                        break;
                case RD_VALUE:
                        if( copy_to_user((int32_t*) arg, &value, sizeof(value)) )
                        {
                                pr_err("Data Read : Err!\n");
                        }
                        break;
                default:
                        pr_info("Default\n");
                        break;
        }
        return 0;
}
 
/* ── MODULE INIT ──────────────────────────────────────────────────────────── */
/*
 * etx_driver_init() — runs on: sudo insmod driver.ko
 *
 * Same 5 steps as before + 2 NEW procfs steps:
 *  1. alloc_chrdev_region → get Major:Minor
 *  2. cdev_init + cdev_add → register char device
 *  3. class_create         → create /sys/class/etx_class/
 *  4. device_create        → udev creates /dev/etx_device
 *  5. proc_mkdir()         → NEW: create /proc/etx/ directory
 *  6. proc_create()        → NEW: create /proc/etx/etx_proc file
 */
static int __init etx_driver_init(void)
{
        /*Allocating Major number*/
        if((alloc_chrdev_region(&dev, 0, 1, "etx_Dev")) <0){
                pr_info("Cannot allocate major number\n");
                return -1;
        }
        pr_info("Major = %d Minor = %d \n",MAJOR(dev), MINOR(dev));
 
        /*Creating cdev structure*/
        cdev_init(&etx_cdev,&fops);
 
        /*Adding character device to the system*/
        if((cdev_add(&etx_cdev,dev,1)) < 0){
            pr_info("Cannot add the device to the system\n");
            goto r_class;
        }
 
        /*Creating struct class*/
        if(IS_ERR(dev_class = class_create("etx_class"))){
            pr_info("Cannot create the struct class\n");
            goto r_class;
        }
 
        /*Creating device*/
        if(IS_ERR(device_create(dev_class,NULL,dev,NULL,"etx_device"))){
            pr_info("Cannot create the Device 1\n");
            goto r_device;
        }
        
        /*Create proc directory. It will create a directory under "/proc" */
        parent = proc_mkdir("etx",NULL);
        
        if( parent == NULL )
        {
            pr_info("Error creating proc entry");
            goto r_device;
        }
        
        /*Creating Proc entry under "/proc/etx/" */
        proc_create("etx_proc", 0666, parent, &proc_fops);
 
        pr_info("Device Driver Insert...Done!!!\n");
        return 0;
 
r_device:
        class_destroy(dev_class);
r_class:
        unregister_chrdev_region(dev,1);
        return -1;
}
 
/*
** Module exit function
*/
static void __exit etx_driver_exit(void)
{
        /* Removes single proc entry */
        //remove_proc_entry("etx/etx_proc", parent);
        
   	proc_remove(parent);	/* remove complete /proc/etx */
        
	device_destroy(dev_class, dev);      /* remove /dev/etx_device        */
        class_destroy(dev_class);            /* remove /sys/class/etx_class/  */
        cdev_del(&etx_cdev);                 /* unregister cdev               */
        unregister_chrdev_region(dev, 1);    /* release Major:Minor numbers   */
        pr_info("Device Driver Remove...Done!!!\n");
}
 
module_init(etx_driver_init);
module_exit(etx_driver_exit);
 
MODULE_LICENSE("GPL");
MODULE_AUTHOR("EmbeTronicX <embetronicx@gmail.com>");
MODULE_DESCRIPTION("Simple Linux device driver (procfs)");
MODULE_VERSION("1.6");
