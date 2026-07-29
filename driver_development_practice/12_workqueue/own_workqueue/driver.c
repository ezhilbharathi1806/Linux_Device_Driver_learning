/***************************************************************************//**
*  \file       driver.c
*  \details    Simple Linux device driver (Own Workqueue)
*
*  KEY DIFFERENCE from Parts 14 & 15:
*    Parts 14/15: schedule_work(&work)       → global kernel workqueue
*    Part 16:     queue_work(own_wq, &work)  → OUR OWN workqueue
*
*  FLOW:
*    sudo cat /dev/etx_device
*      → etx_read() fires software IRQ 11
*      → irq_handler() runs (Top Half) → calls queue_work(own_workqueue)
*      → workqueue_fn() runs later (Bottom Half) in our own worker thread
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
#include <linux/workqueue.h>     /* create_workqueue, destroy_workqueue,
                                    queue_work — core header for own workqueue */
#include <linux/err.h>

#define IRQ_NO 11                /* IRQ number we register and fire           */


/* ── OWN WORKQUEUE SETUP ─────────────────────────────────────────────────── */

/* Pointer to OUR OWN workqueue.*/
static struct workqueue_struct *own_workqueue;

/* Forward declaration needed because DECLARE_WORK references workqueue_fn */
static void workqueue_fn(struct work_struct *work);

/* Static method: DECLARE_WORK creates + initializes work_struct at compile time. */
static DECLARE_WORK(work, workqueue_fn);

/* workqueue_fn() — the Bottom Half function running in our own worker thread */
static void workqueue_fn(struct work_struct *work)
{
        printk(KERN_INFO "Executing Workqueue Function\n");
        /* Real driver: heavy processing, I/O, protocol handling, etc. */
        return;
}


/* ── INTERRUPT HANDLER (Top Half) ───────────────────────────────────────── */
static irqreturn_t irq_handler(int irq, void *dev_id)
{
        printk(KERN_INFO "Shared IRQ: Interrupt Occurred\n");

        /*
         * queue_work(wq, work) — add work to OUR workqueue.
         *   own_workqueue = our dedicated workqueue (not the global one)
         *   &work         = the work_struct (job) to execute
         *
         * Returns true  = work was newly queued (not already pending)
         * Returns false = work was already queued (skipped — no duplicate)
         */
        queue_work(own_workqueue, &work);

        return IRQ_HANDLED;
}


/* ── GLOBAL VARIABLES ────────────────────────────────────────────────────── */
volatile int etx_value = 0;   /* value exposed via sysfs                      */
dev_t dev = 0;
static struct class *dev_class;
static struct cdev etx_cdev;
struct kobject *kobj_ref;     /* kobject for /sys/kernel/etx_sysfs/           */


/* ── FUNCTION PROTOTYPES ─────────────────────────────────────────────────── */
static int      __init etx_driver_init(void);
static void     __exit etx_driver_exit(void);
static int      etx_open(struct inode *inode, struct file *file);
static int      etx_release(struct inode *inode, struct file *file);
static ssize_t  etx_read(struct file *filp, char __user *buf, size_t len, loff_t *off);
static ssize_t  etx_write(struct file *filp, const char *buf, size_t len, loff_t *off);
static ssize_t  sysfs_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf);
static ssize_t  sysfs_store(struct kobject *kobj, struct kobj_attribute *attr,
                             const char *buf, size_t count);


/* ── SYSFS ATTRIBUTE ─────────────────────────────────────────────────────── */
/* Creates /sys/kernel/etx_sysfs/etx_value with rw permissions */
struct kobj_attribute etx_attr = __ATTR(etx_value, 0660, sysfs_show, sysfs_store);


/* ── FILE OPERATIONS TABLE ───────────────────────────────────────────────── */
static struct file_operations fops =
{
        .owner   = THIS_MODULE,
        .read    = etx_read,
        .write   = etx_write,
        .open    = etx_open,
        .release = etx_release,
};


/* ── SYSFS FUNCTIONS ─────────────────────────────────────────────────────── */

/* cat /sys/kernel/etx_sysfs/etx_value → sends etx_value to user */
static ssize_t sysfs_show(struct kobject *kobj,
                           struct kobj_attribute *attr, char *buf)
{
        printk(KERN_INFO "Sysfs - Read!!!\n");
        return sprintf(buf, "%d", etx_value);
}

/* echo 10 > /sys/kernel/etx_sysfs/etx_value → stores value */
static ssize_t sysfs_store(struct kobject *kobj,
                            struct kobj_attribute *attr,
                            const char *buf, size_t count)
{
        printk(KERN_INFO "Sysfs - Write!!!\n");
        sscanf(buf, "%d", &etx_value);
        return count;
}


/* ── DEVICE FUNCTIONS ────────────────────────────────────────────────────── */

/* Called when /dev/etx_device is opened */
static int etx_open(struct inode *inode, struct file *file)
{
        printk(KERN_INFO "Device File Opened...!!!\n");
        return 0;
}

/* Called when /dev/etx_device is closed */
static int etx_release(struct inode *inode, struct file *file)
{
        printk(KERN_INFO "Device File Closed...!!!\n");
        return 0;
}

/*
 * etx_read() — fires a software IRQ when user reads device
 * Triggered by: sudo cat /dev/etx_device
 *
 * asm("int $0x3B") → fires IRQ 11 in software
 *   → CPU runs irq_handler() → queue_work(own_workqueue, &work)
 *   → [own_wq] thread runs workqueue_fn() as bottom half
 */
static ssize_t etx_read(struct file *filp,
                         char __user *buf, size_t len, loff_t *off)
{
        printk(KERN_INFO "Read function\n");
        asm("int $0x3B");   /* trigger software interrupt → IRQ 11 fires */
        return 0;
}

/* Called when user writes to /dev/etx_device — not used here */
static ssize_t etx_write(struct file *filp,
                          const char __user *buf, size_t len, loff_t *off)
{
        printk(KERN_INFO "Write Function\n");
        return 0;
}


/* ── MODULE INIT ──────────────────────────────────────────────────────────── */
/*
 * etx_driver_init() — runs on: sudo insmod driver.ko
 *
 * Same 7 steps as Parts 14/15 (char dev + sysfs + IRQ) + NEW Step 8:
 *   Step 8: create_workqueue("own_wq") → creates our own dedicated workqueue
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

        /* Step 6: Create /sys/kernel/etx_sysfs/ directory */
        kobj_ref = kobject_create_and_add("etx_sysfs", kernel_kobj);

        /* Step 7: Create /sys/kernel/etx_sysfs/etx_value sysfs file */
        if(sysfs_create_file(kobj_ref, &etx_attr.attr)){
                printk(KERN_INFO "Cannot create sysfs file......\n");
                goto r_sysfs;
        }

        /*
         * Step 8: Create OUR OWN dedicated workqueue — KEY NEW STEP.
         *
         * create_workqueue("own_wq"):
         *   "own_wq"  = workqueue name (visible as [own_wq] in ps -aef)
         *
         * Internally calls:
         *   alloc_workqueue("%s", WQ_MEM_RECLAIM, 1, "own_wq")
         *
         * Creates ONE worker thread per CPU (multi-threaded).
         * For single-threaded: use create_singlethread_workqueue() instead.
         *
         * Returns: pointer on success, NULL on failure.
         * Work items submitted via queue_work() run in this thread.
         */
        own_workqueue = create_workqueue("own_wq");
        if(!own_workqueue) {
            printk(KERN_INFO "Cannot create workqueue\n");
            goto irq;
        }

	/* Step 9: Register IRQ 11 handler */
        if(request_irq(IRQ_NO, irq_handler, IRQF_SHARED, "etx_device", (void *)(irq_handler))) {
            printk(KERN_INFO "my_device: cannot register IRQ \n");
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


/* ── MODULE EXIT ──────────────────────────────────────────────────────────── */
/*
 * etx_driver_exit() — runs on: sudo rmmod driver
 *
 * KEY DIFFERENCE from Parts 14/15:
 *   Parts 14/15: No workqueue to destroy (kernel owns global workqueue)
 *   Part 16:     Must call destroy_workqueue() — we OWN this workqueue.
 */
static void __exit etx_driver_exit(void)
{
        /*
         * Destroy our workqueue — waits for all pending workqueue_fn()
         * calls to complete, then frees the workqueue and its threads.
         * After this, [own_wq] is gone from ps -aef.
         */
        destroy_workqueue(own_workqueue);

        free_irq(IRQ_NO, (void *)(irq_handler));  /* stop IRQ 11             */
        kobject_put(kobj_ref);                    /* free kobject             */
        sysfs_remove_file(kernel_kobj, &etx_attr.attr);
        device_destroy(dev_class, dev);
        class_destroy(dev_class);
        cdev_del(&etx_cdev);
        unregister_chrdev_region(dev, 1);
        printk(KERN_INFO "Device Driver Remove...Done!!!\n");
}

module_init(etx_driver_init);
module_exit(etx_driver_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("EmbeTronicX <embetronicx@gmail.com>");
MODULE_DESCRIPTION("Simple Linux device driver (Own Workqueue)");
MODULE_VERSION("1.12");

/*
 * Complete flow summary
 
 insmod driver.ko
  ├── create_workqueue("own_wq") → [own_wq] thread created, visible in ps
  ├── request_irq(11)            → IRQ 11 handler registered
  └── /dev/etx_device + sysfs   → created

sudo cat /dev/etx_device
  ├── etx_read()
  │     └── asm("int $0x3B")           → fires IRQ 11
  │               └── irq_handler()    [TOP HALF]
  │                     ├── "Shared IRQ: Interrupt Occurred"
  │                     └── queue_work(own_workqueue, &work) → queued in [own_wq]
  │                               └── workqueue_fn()         [BOTTOM HALF]
  │                                     └── "Executing Workqueue Function"

ps -aef | grep own_wq
  └── [own_wq]   ← our dedicated worker thread is visible here!

rmmod driver
  ├── destroy_workqueue() → waits for pending work, removes [own_wq] thread
  └── free_irq() + device cleanup
  */
