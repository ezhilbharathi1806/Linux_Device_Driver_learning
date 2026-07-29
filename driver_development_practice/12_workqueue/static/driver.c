/***************************************************************************//**
*  \file       driver.c
*
*  \details    Simple Linux device driver (Global Workqueue - Static method)
*
*******************************************************************************/
/* ── HEADERS ─────────────────────────────────────────────────────────────── */
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/kdev_t.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/slab.h>          /* kmalloc()                                 */
#include <linux/uaccess.h>       /* copy_to/from_user()                       */
#include <linux/sysfs.h>         /* sysfs_create_file()                       */
#include <linux/kobject.h>       /* kobject_create_and_add()                  */
#include <linux/interrupt.h>     /* request_irq(), free_irq(), IRQF_SHARED    */
#include <asm/io.h>
#include <linux/workqueue.h>     /* DECLARE_WORK, schedule_work() — NEW       */
#include <linux/err.h>

#define IRQ_NO 11                /* IRQ number we register and trigger        */
 
 
void workqueue_fn(struct work_struct *work); 
 
/*Creating work by Static Method */
DECLARE_WORK(workqueue,workqueue_fn);
 
/*Workqueue Function - the Bottom Half function */
void workqueue_fn(struct work_struct *work)
{
        printk(KERN_INFO "Executing Workqueue Function\n");
}
 
 
//Interrupt handler for IRQ 11. (the Top half)
static irqreturn_t irq_handler(int irq,void *dev_id) {
        printk(KERN_INFO "Shared IRQ: Interrupt Occurred");
        schedule_work(&workqueue);	/* Schedule bottom half — offload heavy work to workqueue */
        
        return IRQ_HANDLED;	/* Interrupt handled successfully */
}
 
 
/* ── GLOBAL VARIABLES ────────────────────────────────────────────────────── */
volatile int etx_value = 0;   /* value exposed via sysfs                      */
dev_t dev = 0;
static struct class *dev_class;
static struct cdev etx_cdev;
struct kobject *kobj_ref;     /* kobject for /sys/kernel/etx_sysfs/           */

/* ── FUNCTION PROTOTYPES ─────────────────────────────────────────────────── */
static int __init etx_driver_init(void);
static void __exit etx_driver_exit(void);
/*************** Driver Fuctions **********************/
static int etx_open(struct inode *inode, struct file *file);
static int etx_release(struct inode *inode, struct file *file);
static ssize_t etx_read(struct file *filp, char __user *buf, size_t len,loff_t * off);
static ssize_t etx_write(struct file *filp, const char *buf, size_t len, loff_t * off);
/*************** Sysfs Fuctions **********************/
static ssize_t sysfs_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf);
static ssize_t sysfs_store(struct kobject *kobj, struct kobj_attribute *attr,const char *buf, size_t count);

/* ── SYSFS ATTRIBUTE ─────────────────────────────────────────────────────── */
/* /sys/kernel/etx_sysfs/etx_value — read/write etx_value from user space */
struct kobj_attribute etx_attr = __ATTR(etx_value, 0660, sysfs_show, sysfs_store);

/* ── FILE OPERATIONS TABLE ───────────────────────────────────────────────── */
static struct file_operations fops =
{
        .owner          = THIS_MODULE,
        .read           = etx_read,
        .write          = etx_write,
        .open           = etx_open,
        .release        = etx_release,
};

/* ── SYSFS FUNCTIONS ─────────────────────────────────────────────────────── */
/* cat /sys/kernel/etx_sysfs/etx_value → sends etx_value to user */
static ssize_t sysfs_show(struct kobject *kobj, 
                struct kobj_attribute *attr, char *buf)
{
        printk(KERN_INFO "Sysfs - Read!!!\n");
        return sprintf(buf, "%d", etx_value);
}

/* echo 10 > /sys/kernel/etx_sysfs/etx_value → stores value in etx_value */
static ssize_t sysfs_store(struct kobject *kobj, 
                struct kobj_attribute *attr,const char *buf, size_t count)
{
        printk(KERN_INFO "Sysfs - Write!!!\n");
        sscanf(buf,"%d",&etx_value);
        return count;
}
/* ── DEVICE FUNCTIONS ────────────────────────────────────────────────────── */
static int etx_open(struct inode *inode, struct file *file)
{
        printk(KERN_INFO "Device File Opened...!!!\n");
        return 0;
}

static int etx_release(struct inode *inode, struct file *file)
{
        printk(KERN_INFO "Device File Closed...!!!\n");
        return 0;
}

/*
 * etx_read() — fires a software IRQ when user reads the device
 * Triggered by: sudo cat /dev/etx_device
*/
static ssize_t etx_read(struct file *filp, 
                char __user *buf, size_t len, loff_t *off)
{
        printk(KERN_INFO "Read function\n");
        asm("int $0x3B");  // Corresponding to irq 11
        return 0;
}

/* Called when user writes to /dev/etx_device — not used here */
static ssize_t etx_write(struct file *filp, 
                const char __user *buf, size_t len, loff_t *off)
{
        printk(KERN_INFO "Write Function\n");
        return len;
}
 
/* ── MODULE INIT ──────────────────────────────────────────────────────────── */
/*
 * etx_driver_init() — runs on: sudo insmod driver.ko
 *
 * Same setup as Part 13 (char dev + sysfs + IRQ).
 * No extra workqueue init needed here — DECLARE_WORK() already did it.
 *
 * Steps:
 *  1-5: Standard char device setup (chrdev + cdev + class + device)
 *  6-7: Sysfs setup (kobject + sysfs file)
 *  8:   request_irq() → register IRQ 11 handler
 */
static int __init etx_driver_init(void)
{
	/* Step 1: Get dynamic Major:Minor */
        if((alloc_chrdev_region(&dev, 0, 1, "etx_Dev")) < 0){
                printk(KERN_INFO "Cannot allocate major number\n");
                return -1;
        }
        printk(KERN_INFO "Major = %d Minor = %d \n", MAJOR(dev), MINOR(dev));

        /* Step 2: Init cdev */
        cdev_init(&etx_cdev, &fops);

        /* Step 3: Register cdev */
        if((cdev_add(&etx_cdev, dev, 1)) < 0){
            printk(KERN_INFO "Cannot add the device to the system\n");
            goto r_class;
        }

        /* Step 4: Create device class */
        if(IS_ERR(dev_class = class_create("etx_class"))){
            printk(KERN_INFO "Cannot create the struct class\n");
            goto r_class;
        }

        /* Step 5: Create /dev/etx_device */
        if(IS_ERR(device_create(dev_class, NULL, dev, NULL, "etx_device"))){
            printk(KERN_INFO "Cannot create the Device 1\n");
            goto r_device;
        }

        /* Step 6: Create /sys/kernel/etx_sysfs/ */
        kobj_ref = kobject_create_and_add("etx_sysfs", kernel_kobj);

        /* Step 7: Create /sys/kernel/etx_sysfs/etx_value */
        if(sysfs_create_file(kobj_ref, &etx_attr.attr)){
                printk(KERN_INFO "Cannot create sysfs file......\n");
                goto r_sysfs;
        }

        /* Step 8: Register IRQ 11 handler.
         * IRQF_SHARED = IRQ 11 may be shared with other devices.
         * dev_id = (void*)(irq_handler) used as unique cookie for free_irq().
         */
        if(request_irq(IRQ_NO, irq_handler, IRQF_SHARED, "etx_device",
                        (void *)(irq_handler))) {
            printk(KERN_INFO "my_device: cannot register IRQ ");
            goto irq;
        }

        printk(KERN_INFO "Device Driver Insert...Done!!!\n");
        return 0;

/* Cleanup labels — reverse order */
irq:
        free_irq(IRQ_NO, (void *)(irq_handler));

r_sysfs:
        kobject_put(kobj_ref);
        sysfs_remove_file(kernel_kobj, &etx_attr.attr);

r_device:
        class_destroy(dev_class);

r_class:
        unregister_chrdev_region(dev, 1);
        cdev_del(&etx_cdev);
        return -1;
}

/*
** Module exit function
*/ 
static void __exit etx_driver_exit(void)
{
        free_irq(IRQ_NO,(void *)(irq_handler));		/* stop IRQ 11 first!      */
        kobject_put(kobj_ref); 				/* free kobject */
        sysfs_remove_file(kernel_kobj, &etx_attr.attr);
        device_destroy(dev_class,dev);
        class_destroy(dev_class);
        cdev_del(&etx_cdev);
        unregister_chrdev_region(dev, 1);
        printk(KERN_INFO "Device Driver Remove...Done!!!\n");
}
 
module_init(etx_driver_init);
module_exit(etx_driver_exit);
 
MODULE_LICENSE("GPL");
MODULE_AUTHOR("EmbeTronicX <embetronicx@gmail.com>");
MODULE_DESCRIPTION("Simple Linux device driver (Global Workqueue - Static method)");
MODULE_VERSION("1.10");

/* Complete Flow Summary
insmod driver.ko
  ├── DECLARE_WORK already set up 'workqueue' → workqueue_fn at compile time
  ├── /dev/etx_device + /sys/kernel/etx_sysfs/etx_value created
  └── IRQ 11 handler registered

sudo cat /dev/etx_device
  ├── etx_open()
  ├── etx_read()
  │     └── asm("int $0x3B") → fires IRQ 11
  │               └── irq_handler() [TOP HALF — fast]
  │                     ├── prints "Shared IRQ: Interrupt Occurred"
  │                     └── schedule_work(&workqueue) → queues job, returns immediately
  │                               └── workqueue_fn() [BOTTOM HALF — later]
  │                                     └── prints "Executing Workqueue Function"
  └── etx_release()

rmmod driver
  └── free_irq() → workqueue jobs complete → cleanup
 */
