/***************************************************************************//**
*  \file       driver.c
*  \details    Simple Linux device driver (Tasklet — Dynamic Method)
*
*  KEY DIFFERENCE from Static Method (Part 20):
*    Static:  DECLARE_TASKLET(tasklet, tasklet_fn, 1)  ← compile time, global struct
*    Dynamic: struct tasklet_struct *tasklet = NULL;   ← pointer declared globally
*             tasklet = kmalloc(...)                   ← allocated in init()
*             tasklet_init(tasklet, tasklet_fn, 0)     ← initialized in init()
*             kfree(tasklet)                           ← freed in exit()
*
*  FLOW:
*    sudo cat /dev/etx_device
*      → etx_read() fires software IRQ 11
*      → irq_handler() (Top Half) → tasklet_schedule(tasklet)
*      → tasklet_fn() (Bottom Half) runs in atomic context
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
#include <linux/slab.h>          /* kmalloc(), kfree() — needed for dynamic   */
#include <linux/uaccess.h>       /* copy_to/from_user()                       */
#include <linux/sysfs.h>
#include <linux/kobject.h>
#include <linux/interrupt.h>     /* request_irq(), free_irq(), IRQF_SHARED    */
#include <asm/io.h>
#include <linux/err.h>

#define IRQ_NO 11


/* ── TASKLET SETUP (Dynamic Method) ─────────────────────────────────────── */

/* Forward declaration — irq_handler uses tasklet_fn before its definition */
void tasklet_fn(unsigned long);

/* DYNAMIC METHOD: Declare a POINTER to tasklet_struct.
 *
 * Unlike static method which creates the struct directly:
 *   struct tasklet_struct tasklet;   ← static (stack/BSS — always exists)
 *
 * Here we just declare a pointer — no memory allocated yet.
 * Memory is allocated via kmalloc() in etx_driver_init().
 * Initialized to NULL — good practice to detect use-before-init bugs.
 *
 * Use dynamic method when:
 *   - tasklet_struct is inside a kmalloc'd device context struct
 *   - You need runtime flexibility on when/whether to create the tasklet
 */
struct tasklet_struct *tasklet = NULL;

/*
 * tasklet_fn() — Bottom Half (runs in ATOMIC context)
 *
 * Same rules as static method:
 *   ❌ NO sleep    ❌ NO mutex    ✅ CAN use spinlock
 *
 * @arg : the 'data' value passed in tasklet_init() — 0 in this example.
 *        In real drivers: cast to pointer for richer context:
 *          struct my_dev *dev = (struct my_dev *)arg;
 */
void tasklet_fn(unsigned long arg)
{
        printk(KERN_INFO "Executing Tasklet Function : arg = %ld\n", arg);
}


/* ── INTERRUPT HANDLER (Top Half) ───────────────────────────────────────── */
/*
 * irq_handler() — runs immediately when IRQ 11 fires
 *
 * KEY SYNTAX DIFFERENCE from static method:
 *   Static:  tasklet_schedule(&tasklet)  ← & because tasklet is a struct
 *   Dynamic: tasklet_schedule(tasklet)   ← no & because tasklet IS a pointer
 *
 * tasklet_schedule(tasklet):
 *   Adds tasklet to normal priority queue — returns immediately.
 *   tasklet_fn() runs later in atomic context.
 *   If already scheduled (not yet run) → silently ignored.
 */
static irqreturn_t irq_handler(int irq, void *dev_id)
{
        printk(KERN_INFO "Shared IRQ: Interrupt Occurred");

        /* Schedule bottom half — pass pointer directly (no & needed) */
        tasklet_schedule(tasklet);

        return IRQ_HANDLED;
}


/* ── GLOBAL VARIABLES ────────────────────────────────────────────────────── */
volatile int etx_value = 0;
dev_t dev = 0;
static struct class *dev_class;
static struct cdev etx_cdev;
struct kobject *kobj_ref;


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

/* cat /sys/kernel/etx_sysfs/etx_value → returns etx_value */
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


/* ── DEVICE FILE HANDLER FUNCTIONS ───────────────────────────────────────── */

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
 * etx_read() — fires software IRQ 11 when user reads the device
 * Triggered by: sudo cat /dev/etx_device
 *
 * asm("int $0x3B") = vector 59 = FIRST_EXTERNAL_VECTOR(0x20) + 0x10 + 11
 * → CPU runs irq_handler() → tasklet_schedule(tasklet) → tasklet_fn()
 */
static ssize_t etx_read(struct file *filp,
                         char __user *buf, size_t len, loff_t *off)
{
        printk(KERN_INFO "Read function\n");
        asm("int $0x3B");   /* fire software interrupt → IRQ 11 */
        return 0;
}

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
 * Same 7 steps as Part 20 + DYNAMIC TASKLET steps at the end:
 *   Step 8a: kmalloc() → allocate memory for tasklet_struct on heap
 *   Step 8b: tasklet_init() → initialize the allocated tasklet
 *
 * ⚠️ ORDERING: tasklet_init() called AFTER request_irq() in this example.
 *    In production: call tasklet_init() BEFORE request_irq() to prevent
 *    IRQ firing before tasklet is initialized → crash risk.
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

        /* Step 6: Create sysfs dir and file */
        kobj_ref = kobject_create_and_add("etx_sysfs", kernel_kobj);
        if(sysfs_create_file(kobj_ref, &etx_attr.attr)){
            printk(KERN_INFO "Cannot create sysfs file......\n");
            goto r_sysfs;
        }

        /* Step 7: Register IRQ 11 handler */
        if(request_irq(IRQ_NO, irq_handler, IRQF_SHARED, "etx_device",
                        (void *)(irq_handler))) {
            printk(KERN_INFO "etx_device: cannot register IRQ\n");
            goto irq;
        }

        /*
         * Step 8a: Allocate heap memory for the tasklet_struct.
         *
         * kmalloc(size, GFP_KERNEL):
         *   sizeof(struct tasklet_struct) = size of the kernel tasklet structure
         *   GFP_KERNEL = normal allocation, can sleep — OK in init()
         *
         * Returns: pointer on success, NULL on failure.
         * NULL check is mandatory — crash if you call tasklet_init on NULL!
         */
        tasklet = kmalloc(sizeof(struct tasklet_struct), GFP_KERNEL);
        if(tasklet == NULL) {
            printk(KERN_INFO "etx_device: cannot allocate Memory\n");
            goto irq;   /* cleanup: free_irq + sysfs + device */
        }

        /*
         * Step 8b: Initialize the tasklet at RUNTIME.
         *
         * tasklet_init(t, func, data):
         *   tasklet     = pointer to the allocated tasklet_struct
         *   tasklet_fn  = bottom half function to call
         *   0           = data argument passed to tasklet_fn (arg=0)
         *
         * Internally sets:
         *   tasklet->func  = tasklet_fn
         *   tasklet->data  = 0
         *   tasklet->state = TASKLET_STATE_SCHED
         *   tasklet->count = 0  (enabled state)
         *
         * After this: tasklet is ready — tasklet_schedule() can be called.
         */
        tasklet_init(tasklet, tasklet_fn, 0);

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
 * KEY DIFFERENCE from Static Method exit:
 *   Static:  tasklet_kill(&tasklet)   ← & because struct
 *            [no kfree — not kmalloc'd]
 *
 *   Dynamic: tasklet_kill(tasklet)    ← no & because pointer
 *            kfree(tasklet)           ← MANDATORY — was kmalloc'd!
 *
 * ORDER:
 *   1. tasklet_kill() → stops tasklet (waits if running)
 *   2. kfree()        → free heap memory
 *   3. free_irq()     → stop IRQ
 *   4. rest of cleanup
 */
static void __exit etx_driver_exit(void)
{
        /*
         * Kill tasklet FIRST — waits for any running tasklet to complete,
         * then removes it from the scheduled queue if pending.
         * Passing pointer directly (no & needed for dynamic method).
         */
        tasklet_kill(tasklet);

        /*
         * Free the kmalloc'd memory — MANDATORY for dynamic method.
         * Static method does NOT need kfree() — its struct is not heap allocated.
         * NULL check prevents crash if kmalloc failed during init.
         */
        if(tasklet != NULL) {
            kfree(tasklet);         /* free heap memory allocated in init()   */
        }

        free_irq(IRQ_NO, (void *)(irq_handler));
        kobject_put(kobj_ref);
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
MODULE_DESCRIPTION("A simple device driver - Tasklet Dynamic Method");
MODULE_VERSION("1.16");

/*
 insmod driver.ko
  ├── kmalloc(tasklet_struct)  → heap memory allocated
  ├── tasklet_init()           → tasklet initialized with tasklet_fn, data=0
  ├── request_irq(11)          → IRQ handler registered
  └── /dev/etx_device + sysfs → created

sudo cat /dev/etx_device
  ├── etx_read()
  │     └── asm("int $0x3B") → fires IRQ 11
  │               └── irq_handler() [TOP HALF]
  │                     ├── "Shared IRQ: Interrupt Occurred"
  │                     └── tasklet_schedule(tasklet) → queued
  │                               └── tasklet_fn(0)  [BOTTOM HALF — atomic]
  │                                     └── "Executing Tasklet Function : arg = 0"

rmmod driver
  ├── tasklet_kill(tasklet)  → stop any running/pending tasklet
  ├── kfree(tasklet)         → free heap memory ← NEW step vs static!
  └── free_irq() + cleanup
 * */
