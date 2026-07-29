/***************************************************************************//**
*  \details    Simple Linux device driver (sysfs)

ubuntu@primary:~/ldd/10_sysfs$ sudo insmod driver.ko 
ubuntu@primary:~/ldd/10_sysfs$ ls -l /sys/kernel/etx_sysfs/
total 0
-rw-rw---- 1 root root 4096 Jan 31 13:53 etx_value
ubuntu@primary:~/ldd/10_sysfs$ sudo cat /sys/kernel/etx_sysfs/etx_value 
0
ubuntu@primary:~/ldd/10_sysfs$ echo 123 | sudo tee /sys/kernel/etx_sysfs/etx_value 
123
ubuntu@primary:~/ldd/10_sysfs$ sudo cat /sys/kernel/etx_sysfs/etx_value 
123
ubuntu@primary:~/ldd/10_sysfs$ sudo rmmod driver 

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
#include <linux/sysfs.h>     /* sysfs_create_file(), sysfs_remove_file()       */
#include <linux/kobject.h>   /* kobject_create_and_add(), kobject_put()        */
#include <linux/err.h>       /* IS_ERR()                                        */

/*
 * The value exposed through sysfs.
 * cat  /sys/kernel/etx_sysfs/etx_value → reads this
 * echo /sys/kernel/etx_sysfs/etx_value → writes this
 * volatile: prevents compiler from caching it — may change from user writes.
 */
volatile int etx_value = 0;

/* Standard char driver globals */
dev_t dev = 0;
static struct class *dev_class;
static struct cdev etx_cdev;

/* Pointer to our kobject */
struct kobject *kobj_ref;

/* Function Prototypes */
static int      __init etx_driver_init(void);
static void     __exit etx_driver_exit(void);
/*************** Driver functions **********************/
static int      etx_open(struct inode *inode, struct file *file);
static int      etx_release(struct inode *inode, struct file *file);
static ssize_t  etx_read(struct file *filp, char __user *buf, size_t len,loff_t * off);
static ssize_t  etx_write(struct file *filp, const char *buf, size_t len, loff_t * off);
 
/*************** Sysfs functions **********************/
static ssize_t  sysfs_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf);
static ssize_t  sysfs_store(struct kobject *kobj, struct kobj_attribute *attr,const char *buf, size_t count);

/* ── SYSFS ATTRIBUTE DEFINITION ──────────────────────────────────────────── */
/*
 * __ATTR(name, permissions, show_fn, store_fn)
 *
 * Creates a kobj_attribute that defines ONE sysfs file:
 *   name        = "etx_value"  → filename: /sys/kernel/etx_sysfs/etx_value
 *   permissions = 0660         → owner+group: rw, others: none
 *   show_fn     = sysfs_show   → called on cat (read)
 *   store_fn    = sysfs_store  → called on echo (write)
 *
 * The .attr member inside is passed to sysfs_create_file().
 */
struct kobj_attribute etx_attr = __ATTR(etx_value, 0660, sysfs_show, sysfs_store);

/* File operation sturcture */
static struct file_operations fops =
{
        .owner          = THIS_MODULE,
        .read           = etx_read,
        .write          = etx_write,
        .open           = etx_open,
        .release        = etx_release,
};

/* * sysfs_show() — called when user reads the sysfs file
 * Triggered by: cat /sys/kernel/etx_sysfs/etx_value */
static ssize_t sysfs_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
        pr_info("Sysfs - Read!!!\n");
        return sprintf(buf, "%d", etx_value);
}

/* sysfs_store() — called when user writes to the sysfs file
 * Triggered by: echo 100 > /sys/kernel/etx_sysfs/etx_value */
static ssize_t sysfs_store(struct kobject *kobj, struct kobj_attribute *attr,const char *buf, size_t count)
{
        pr_info("Sysfs - Write!!!\n");
        sscanf(buf,"%d",&etx_value);
        return count;
}

/* Called when /dev/etx_device is opened */
static int etx_open(struct inode *inode, struct file *file)
{
        pr_info("Device File Opened...!!!\n");
        return 0;
}

/* Called when /dev/etx_device is closed */
static int etx_release(struct inode *inode, struct file *file)
{
        pr_info("Device File Closed...!!!\n");
        return 0;
}
 
/* Called when user reads /dev/etx_device — empty in this tutorial */
static ssize_t etx_read(struct file *filp, 
                char __user *buf, size_t len, loff_t *off)
{
        pr_info("Read function\n");
        return 0;
}

/* Called when user writes /dev/etx_device — empty in this tutorial */
static ssize_t etx_write(struct file *filp, 
                const char __user *buf, size_t len, loff_t *off)
{
        pr_info("Write Function\n");
        return len;
}
 
/* ── MODULE INIT ──────────────────────────────────────────────────────────── 
 *   Step 6: kobject_create_and_add() → creates /sys/kernel/etx_sysfs/
 *   Step 7: sysfs_create_file()      → creates /sys/kernel/etx_sysfs/etx_value
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
 
        /*Creating a directory in /sys/kernel/ */
        kobj_ref = kobject_create_and_add("etx_sysfs",kernel_kobj);
 
	/* Create sysfs file → /sys/kernel/etx_sysfs/etx_value */
        if(sysfs_create_file(kobj_ref,&etx_attr.attr)){
                pr_err("Cannot create sysfs file......\n");
                goto r_sysfs;
    }
        pr_info("Device Driver Insert...Done!!!\n");
        return 0;
 
r_sysfs:
        kobject_put(kobj_ref); 
        sysfs_remove_file(kernel_kobj, &etx_attr.attr);
 
r_device:
        class_destroy(dev_class);
r_class:
        unregister_chrdev_region(dev,1);
        cdev_del(&etx_cdev);
        return -1;
}

/*
** Module exit function
*/
static void __exit etx_driver_exit(void)
{
	sysfs_remove_file(kobj_ref, &etx_attr.attr);   /* remove etx_value file */
        kobject_put(kobj_ref);                          /* free kobject + dir    */

        device_destroy(dev_class,dev);
        class_destroy(dev_class);
        cdev_del(&etx_cdev);
        unregister_chrdev_region(dev, 1);
        pr_info("Device Driver Remove...Done!!!\n");
}
 
module_init(etx_driver_init);
module_exit(etx_driver_exit);
 
MODULE_LICENSE("GPL");
MODULE_AUTHOR("EmbeTronicX <embetronicx@gmail.com>");
MODULE_DESCRIPTION("Simple Linux device driver (sysfs)");
MODULE_VERSION("1.8");


/*
insmod driver.ko
  ├── /dev/etx_device              → created (via udev)
  ├── /sys/kernel/etx_sysfs/       → created (via kobject_create_and_add)
  └── /sys/kernel/etx_sysfs/etx_value → created (via sysfs_create_file)

cat /sys/kernel/etx_sysfs/etx_value
  └── sysfs_show() → sprintf(buf, "%d\n", etx_value) → prints: 0

echo 100 > /sys/kernel/etx_sysfs/etx_value
  └── sysfs_store() → sscanf(buf, "%d", &etx_value) → etx_value = 100

cat /sys/kernel/etx_sysfs/etx_value
  └── sysfs_show() → prints: 100

rmmod driver
  ├── sysfs_remove_file() → removes etx_value file
  ├── kobject_put()       → removes etx_sysfs/ dir
  └── /dev/etx_device     → removed
 */
