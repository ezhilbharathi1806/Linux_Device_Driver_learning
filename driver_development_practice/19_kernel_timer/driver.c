/***************************************************************************//**
*  \file       driver.c
*  \details    Simple Linux device driver (Kernel Timer)
*
*  FLOW:
*    insmod driver.ko
*      → timer_setup() → initializes timer with callback
*      → mod_timer()   → starts timer, expires in 5 seconds
*      → timer_callback() fires every 5 seconds (re-armed inside callback)
*
*    rmmod driver
*      → del_timer() → stops the timer
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
#include <linux/timer.h>     /* timer_list, timer_setup, mod_timer, del_timer */
#include <linux/jiffies.h>   /* jiffies, msecs_to_jiffies                     */
#include <linux/err.h>


/* ── TIMER CONFIGURATION ─────────────────────────────────────────────────── */
/*
 * TIMEOUT = 5000ms = 5 seconds.
 * Timer fires 5 seconds after being armed.
 * Converted to jiffies using msecs_to_jiffies() in mod_timer() calls.
 */
#define TIMEOUT 5000    /* milliseconds */

/*
 * etx_timer: the kernel timer_list structure.
 * Initialized by timer_setup() in init().
 * Armed by mod_timer() in init() and re-armed in callback for periodic use.
 * Stopped by del_timer() in exit().
 */
static struct timer_list etx_timer;

/* counts how many times the callback has been called — for display only */
static unsigned int count = 0;

/* standard char driver globals */
dev_t dev = 0;
static struct class *dev_class;
static struct cdev etx_cdev;


/* ── FUNCTION PROTOTYPES ─────────────────────────────────────────────────── */
static int      __init etx_driver_init(void);
static void     __exit etx_driver_exit(void);
static int      etx_open(struct inode *inode, struct file *file);
static int      etx_release(struct inode *inode, struct file *file);
static ssize_t  etx_read(struct file *filp, char __user *buf, size_t len, loff_t *off);
static ssize_t  etx_write(struct file *filp, const char *buf, size_t len, loff_t *off);


/* ── FILE OPERATIONS TABLE ───────────────────────────────────────────────── */
static struct file_operations fops =
{
        .owner   = THIS_MODULE,
        .read    = etx_read,
        .write   = etx_write,
        .open    = etx_open,
        .release = etx_release,
};


/* ── TIMER CALLBACK FUNCTION ─────────────────────────────────────────────── */
/*
 * timer_callback() — called by kernel when the timer expires
 *
 * Runs in INTERRUPT CONTEXT (softirq) — same rules as ISR:
 *   ❌ NO sleep   ❌ NO mutex   ❌ NO user space access
 *   ✅ CAN use spinlock
 *   ✅ CAN call schedule_work() to defer heavy processing
 *
 * @data: pointer to the expired timer_list structure.
 *        Can use container_of(data, struct my_struct, timer_member)
 *        to get back to your parent struct if needed.
 *
 * Newer kernel (>= 4.15): argument is struct timer_list *.
 * Older kernel (< 4.15):  argument is unsigned long data.
 *   → Use setup_timer() and change arg to unsigned long for older kernels.
 *
 * HOW TO MAKE PERIODIC TIMER:
 *   By default timer fires ONCE then stops.
 *   Call mod_timer() inside callback to re-arm → becomes periodic.
 *   Without re-arming → timer fires once and never again.
 */
void timer_callback(struct timer_list *data)
{
        pr_info("Timer Callback function Called [%d]\n", count++);

        /*
         * Re-arm the timer for next expiry (makes it periodic).
         * mod_timer(&etx_timer, jiffies + msecs_to_jiffies(TIMEOUT)):
         *   jiffies          = current kernel time in ticks
         *   msecs_to_jiffies = convert 5000ms → jiffies
         *   result           = "expire 5 seconds from NOW"
         *
         * Remove this line if you want a one-shot (fires once only) timer.
         */
        mod_timer(&etx_timer, jiffies + msecs_to_jiffies(TIMEOUT));
}


/* ── DEVICE FILE HANDLER FUNCTIONS ───────────────────────────────────────── */

static int etx_open(struct inode *inode, struct file *file)
{
        pr_info("Device File Opened...!!!\n");
        return 0;
}

static int etx_release(struct inode *inode, struct file *file)
{
        pr_info("Device File Closed...!!!\n");
        return 0;
}

/* Not used in this tutorial — just logs */
static ssize_t etx_read(struct file *filp,
                         char __user *buf, size_t len, loff_t *off)
{
        pr_info("Read Function\n");
        return 0;
}

static ssize_t etx_write(struct file *filp,
                          const char __user *buf, size_t len, loff_t *off)
{
        pr_info("Write function\n");
        return len;
}


/* ── MODULE INIT ──────────────────────────────────────────────────────────── */
/*
 * etx_driver_init() — runs on: sudo insmod driver.ko
 *
 * Standard char device setup (Steps 1-5) + NEW timer steps:
 *   Step 6: timer_setup()  → initialize timer with callback function
 *   Step 7: mod_timer()    → arm/start timer, first expiry in 5 seconds
 *
 * timer_setup() vs setup_timer():
 *   timer_setup() = newer kernel (>= 4.15) — callback arg: struct timer_list *
 *   setup_timer() = older kernel (< 4.15)  — callback arg: unsigned long
 */
static int __init etx_driver_init(void)
{
        /* Step 1: Get dynamic Major:Minor */
        if((alloc_chrdev_region(&dev, 0, 1, "etx_Dev")) < 0){
                pr_err("Cannot allocate major number\n");
                return -1;
        }
        pr_info("Major = %d Minor = %d \n", MAJOR(dev), MINOR(dev));

        /* Step 2: Init cdev */
        cdev_init(&etx_cdev, &fops);

        /* Step 3: Register cdev */
        if((cdev_add(&etx_cdev, dev, 1)) < 0){
            pr_err("Cannot add the device to the system\n");
            goto r_class;
        }

        /* Step 4: Create device class */
        if(IS_ERR(dev_class = class_create("etx_class"))){
            pr_err("Cannot create the struct class\n");
            goto r_class;
        }

        /* Step 5: Create /dev/etx_device */
        if(IS_ERR(device_create(dev_class, NULL, dev, NULL, "etx_device"))){
            pr_err("Cannot create the Device 1\n");
            goto r_device;
        }

        /*
         * Step 6: Initialize the timer.
         *
         * timer_setup(&etx_timer, timer_callback, 0):
         *   &etx_timer      = pointer to our timer_list struct
         *   timer_callback  = function called when timer expires
         *   0               = flags (usually 0)
         *
         * After this: timer is initialized but NOT yet running.
         * The timer starts running only after mod_timer() or add_timer().
         *
         * For older kernel: replace with:
         *   setup_timer(&etx_timer, timer_callback, 0);
         *   And change callback arg from struct timer_list* to unsigned long.
         */
        timer_setup(&etx_timer, timer_callback, 0);

        /*
         * Step 7: Arm/Start the timer.
         *
         * mod_timer(&etx_timer, jiffies + msecs_to_jiffies(TIMEOUT)):
         *   Sets etx_timer.expires = jiffies + 5000ms worth of jiffies
         *   Activates the timer → kernel will call timer_callback() after 5s.
         *
         * Alternative: could use add_timer() after setting expires manually,
         * but mod_timer() is the cleaner, preferred way.
         */
        mod_timer(&etx_timer, jiffies + msecs_to_jiffies(TIMEOUT));

        pr_info("Device Driver Insert...Done!!!\n");
        return 0;

r_device:
        class_destroy(dev_class);
r_class:
        unregister_chrdev_region(dev, 1);
        return -1;
}


/* ── MODULE EXIT ──────────────────────────────────────────────────────────── */
/*
 * etx_driver_exit() — runs on: sudo rmmod driver
 *
 * KEY: del_timer() FIRST — stop timer before device cleanup.
 * If timer fires AFTER module is unloaded → callback accesses freed memory → crash!
 *
 * del_timer(&etx_timer):
 *   Deactivates the timer immediately.
 *   Does NOT wait for a currently running callback to finish.
 *   Returns 0=was inactive, 1=was active (just deactivated).
 *
 * For safer cleanup (ensures callback is not running):
 *   Use del_timer_sync(&etx_timer) instead.
 *   But ⚠️ del_timer_sync cannot be used if timer re-arms itself AND
 *   del_timer_sync is called from interrupt context.
 */
static void __exit etx_driver_exit(void)
{
        del_timer(&etx_timer);    /* stop timer — MUST do before device cleanup */
        device_destroy(dev_class, dev);
        class_destroy(dev_class);
        cdev_del(&etx_cdev);
        unregister_chrdev_region(dev, 1);
        pr_info("Device Driver Remove...Done!!!\n");
}

module_init(etx_driver_init);
module_exit(etx_driver_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("EmbeTronicX <embetronicx@gmail.com>");
MODULE_DESCRIPTION("A simple device driver - Kernel Timer");
MODULE_VERSION("1.21");

/*
 *Complete flow summary
insmod driver.ko
  ├── timer_setup()          → timer initialized with timer_callback
  └── mod_timer(jiffies+5s)  → timer armed, starts counting down

After 5 seconds:
  └── timer_callback() fires
        ├── pr_info("Timer Callback function Called [0]")
        └── mod_timer(jiffies+5s)  → RE-ARMED → fires again after 5s

After 10 seconds:
  └── timer_callback() fires
        ├── pr_info("Timer Callback function Called [1]")
        └── mod_timer(jiffies+5s)  → RE-ARMED
  ... (repeats every 5 seconds forever)

rmmod driver
  └── del_timer()  → timer stopped → callback never fires again
 */
