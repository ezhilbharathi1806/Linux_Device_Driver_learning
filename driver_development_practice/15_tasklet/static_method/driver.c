/***************************************************************************//**
*  \file       driver.c
*  \details    Simple Linux device driver (Tasklet — Static Method)
*
*  FLOW:
*    sudo cat /dev/etx_device
*      → etx_read() fires software IRQ 11
*      → irq_handler() runs (Top Half) → tasklet_schedule()
*      → tasklet_fn() runs later (Bottom Half) in atomic context
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
#include <linux/err.h>

#define IRQ_NO 11                /* IRQ number we register and fire           */


/* ── TASKLET SETUP (Static Method) ──────────────────────────────────────── */

/* Forward declaration — DECLARE_TASKLET references tasklet_fn before
 * its actual function definition below */
void tasklet_fn(unsigned long);

/* DECLARE_TASKLET(name, func, data) — Static initialization at compile time.
 *
 * Creates and initializes a tasklet_struct named 'tasklet':
 *   name = "tasklet"    → variable name of the tasklet_struct
 *   func = tasklet_fn   → bottom half function to run
 *   data = 1            → unsigned long argument passed to tasklet_fn
 *
 * Tasklet starts in ENABLED state (count=0).
 * Equivalent to:
 *   struct tasklet_struct tasklet = { NULL, 0, 0, tasklet_fn, 1 };
 *
 * ⚠️ Newer kernels (6.x+): use DECLARE_TASKLET_OLD(tasklet, tasklet_fn)
 *    The data argument was removed from the newer API.
 */
//DECLARE_TASKLET(tasklet, tasklet_fn, 1);
DECLARE_TASKLET_OLD(tasklet, tasklet_fn);

/* tasklet_fn() — the Bottom Half function (runs in ATOMIC context)
 *
 * Called by the kernel's softirq mechanism after tasklet_schedule().
 * Runs on the SAME CPU that scheduled it.
 *
 * RULES — because it runs in atomic/interrupt context:
 *   ❌ NO sleep()    ❌ NO mutex    ❌ NO user space access
 *   ✅ CAN use spinlock
 *   ✅ CAN do quick processing
 *
 * @arg : the 'data' value passed in DECLARE_TASKLET (1 in our case)
 *        In real drivers, cast this to a pointer for richer context:
 *        struct my_dev *dev = (struct my_dev *)arg;
 */
void tasklet_fn(unsigned long arg)
{
        printk(KERN_INFO "Executing Tasklet Function : arg = %ld\n", arg);
        /* Real driver: fast data processing, hardware register update, etc. */
}


/* ── INTERRUPT HANDLER (Top Half) ───────────────────────────────────────── */
/*
 * irq_handler() — runs immediately when IRQ 11 fires
 *
 * Top Half: must be fast. Just schedules tasklet and returns.
 * All deferred work happens in tasklet_fn() (bottom half).
 *
 * tasklet_schedule(&tasklet):
 *   Adds 'tasklet' to the NORMAL priority tasklet queue.
 *   Returns immediately — does NOT wait for tasklet_fn() to run.
 *   If tasklet is already scheduled (not yet run) → silently ignored.
 *
 * KEY DIFFERENCE from workqueue:
 *   workqueue  → queue_work()      → runs in process context (can sleep)
 *   tasklet    → tasklet_schedule()→ runs in atomic context (NO sleep)
 */
static irqreturn_t irq_handler(int irq, void *dev_id)
{
        printk(KERN_INFO "Shared IRQ: Interrupt Occurred");

        /* Schedule bottom half — tasklet_fn runs soon in atomic context */
        tasklet_schedule(&tasklet);

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
static ssize_t  sysfs_store(struct kobject *kobj, struct kobj_attribute *attr, const char *buf, size_t count);


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
static ssize_t sysfs_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
        printk(KERN_INFO "Sysfs - Read!!!\n");
        return sprintf(buf, "%d", etx_value);
}

/* echo 10 > /sys/kernel/etx_sysfs/etx_value → stores value */
static ssize_t sysfs_store(struct kobject *kobj, struct kobj_attribute *attr, const char *buf, size_t count)
{
        printk(KERN_INFO "Sysfs - Write!!!\n");
        sscanf(buf, "%d", &etx_value);
        return count;
}


/* ── DEVICE FILE HANDLER FUNCTIONS ───────────────────────────────────────── */

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
 * etx_read() — fires a software IRQ when user reads the device
 * Triggered by: sudo cat /dev/etx_device
 *
 * asm("int $0x3B") fires IRQ 11 in software:
 *   0x3B = 59 decimal = FIRST_EXTERNAL_VECTOR(0x20) + 0x10 + 11
 *   → CPU runs irq_handler() → tasklet_schedule()
 *   → tasklet_fn() runs as bottom half in atomic context
 */
static ssize_t etx_read(struct file *filp,
                         char __user *buf, size_t len, loff_t *off)
{
        printk(KERN_INFO "Read function\n");
        asm("int $0x3B");   /* fire software interrupt → IRQ 11             */
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
 * Standard 7-step setup (char dev + sysfs + IRQ).
 * NO explicit tasklet init needed here — DECLARE_TASKLET() already did it
 * at compile time (same as DECLARE_WORK for workqueue static method).
 *
 * After request_irq(): any cat /dev/etx_device fires IRQ → tasklet scheduled.
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

        /* Step 6: Create sysfs directory and file */
        kobj_ref = kobject_create_and_add("etx_sysfs", kernel_kobj);
        if(sysfs_create_file(kobj_ref, &etx_attr.attr)){
            printk(KERN_INFO "Cannot create sysfs file......\n");
            goto r_sysfs;
        }

        /* Step 7: Register IRQ 11 handler — after this, interrupts can fire */
        if(request_irq(IRQ_NO, irq_handler, IRQF_SHARED, "etx_device",
                        (void *)(irq_handler))) {
            printk(KERN_INFO "my_device: cannot register IRQ\n");
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
 * KEY NEW STEP: tasklet_kill() FIRST.
 *
 * tasklet_kill(&tasklet):
 *   - If tasklet is currently RUNNING → waits for it to finish
 *   - If tasklet is SCHEDULED (queued) → removes it from queue
 *   - Then marks tasklet as dead — will never run again
 *
 * ⚠️ MUST kill tasklet BEFORE free_irq() cleanup.
 *    If IRQ fires after module is gone → tasklet_schedule() on dead
 *    tasklet_struct → undefined behavior / kernel crash!
 *
 * ⚠️ tasklet_kill() CANNOT be called from interrupt context —
 *    it may sleep while waiting for a running tasklet to finish.
 */
static void __exit etx_driver_exit(void)
{
        /* Kill tasklet FIRST — stop any pending/running bottom half */
        tasklet_kill(&tasklet);

        free_irq(IRQ_NO, (void *)(irq_handler));  /* stop IRQ 11             */
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
MODULE_DESCRIPTION("A simple device driver - Tasklet Static Method");
MODULE_VERSION("1.15");


/* Complete flow summary
insmod driver.ko
  ├── DECLARE_TASKLET already set up 'tasklet' → tasklet_fn at compile time
  ├── request_irq(11) → IRQ handler registered
  └── /dev/etx_device + sysfs created

sudo cat /dev/etx_device
  ├── etx_read()
  │     └── asm("int $0x3B") → fires IRQ 11
  │               └── irq_handler() [TOP HALF — fast]
  │                     ├── "Shared IRQ: Interrupt Occurred"
  │                     └── tasklet_schedule(&tasklet) → queued, returns fast
  │                               └── tasklet_fn(1)   [BOTTOM HALF — atomic]
  │                                     └── "Executing Tasklet Function : arg = 1"

rmmod driver
  ├── tasklet_kill()  → wait for running tasklet + remove from queue
  └── free_irq() + device cleanup
 */
