/***************************************************************************//**
*  \file       driver.c
*  \details    Simple Linux device driver (High Resolution Timer)
*
*  FLOW:
*    insmod driver.ko
*      → ktime_set(4, 1000000000)  → timeout = 5 seconds (4s + 1s)
*      → hrtimer_init()            → initialize timer structure
*      → hrtimer_start()           → start timer
*      → timer_callback() fires every 5 seconds (re-armed inside callback)
*
*    rmmod driver
*      → hrtimer_cancel()          → stop timer, wait for callback
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
#include <linux/hrtimer.h>   /* hrtimer_init, hrtimer_start, hrtimer_cancel,
                                hrtimer_forward_now, HRTIMER_MODE_REL,
                                HRTIMER_RESTART, HRTIMER_NORESTART          */
#include <linux/ktime.h>     /* ktime_t, ktime_set()                        */
#include <linux/err.h>


/* ── TIMER CONFIGURATION ─────────────────────────────────────────────────── */
/*
 * Timeout = TIMEOUT_SEC seconds + TIMEOUT_NSEC nanoseconds
 *         = 4 seconds + 1,000,000,000 nanoseconds
 *         = 4 seconds + 1 second
 *         = 5 seconds total
 *
 * Why split into seconds + nanoseconds?
 * ktime_set() takes two separate arguments: whole seconds and nanoseconds.
 * 1,000,000,000 ns = 1 second. Both are added together for the total timeout.
 *
 * hrtimer precision: nanoseconds (vs kernel timer: jiffies/milliseconds)
 * Use hrtimer when you need precise timing like 100us, 500ns intervals.
 */
#define TIMEOUT_NSEC   ( 1000000000L )   /* 1 second in nanoseconds          */
#define TIMEOUT_SEC    ( 4 )             /* 4 seconds                        */

/*
 * etx_hr_timer: the hrtimer structure.
 * Unlike kernel timer which uses timer_list, hrtimer uses its own struct.
 * Internally stored in a red-black tree ordered by expiry time.
 */
static struct hrtimer etx_hr_timer;

static unsigned int count = 0;	/* counts callback invocations - for display only */

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


/* ── HRTIMER CALLBACK FUNCTION ───────────────────────────────────────────── */
/*
 * timer_callback() — called by kernel when hrtimer expires
 *
 * KEY DIFFERENCE from kernel timer callback:
 *   Kernel timer: void callback(struct timer_list *) — no return value
 *   hrtimer:      enum hrtimer_restart callback(struct hrtimer *) — MUST return:
 *     HRTIMER_NORESTART  → one-shot: timer stops, does not fire again
 *     HRTIMER_RESTART    → periodic: timer restarts with new expiry time
 *
 * Runs in INTERRUPT CONTEXT (softirq) — same rules as ISR:
 *   ❌ NO sleep   ❌ NO mutex   ❌ NO user space access
 *   ✅ CAN use spinlock, ✅ CAN call schedule_work()
 *
 * HOW TO MAKE PERIODIC:
 *   Call hrtimer_forward_now() to set next expiry BEFORE returning HRTIMER_RESTART.
 *   Without hrtimer_forward_now(): HRTIMER_RESTART will restart from the OLD expiry.
 *
 * @timer: pointer to the hrtimer that expired (same as &etx_hr_timer here)
 */
enum hrtimer_restart timer_callback(struct hrtimer *timer)
{
        pr_info("Timer Callback function Called [%d]\n", count++);

        /*
         * hrtimer_forward_now(timer, interval):
         *   Advances timer expiry to NOW + interval.
         *   "NOW" = current time of the timer's clock.
         *   Must be called BEFORE returning HRTIMER_RESTART.
         *
         * ktime_set(TIMEOUT_SEC, TIMEOUT_NSEC) = 5 seconds interval.
         *
         * Returns: number of overruns (how many intervals were missed
         *           if the callback ran late — useful for diagnostics).
         *
         * Without this call: HRTIMER_RESTART restarts from OLD expiry
         *   → if callback ran late, timer fires immediately again → busy loop!
         */
        hrtimer_forward_now(timer, ktime_set(TIMEOUT_SEC, TIMEOUT_NSEC));

        return HRTIMER_RESTART;   /* restart timer for next 5-second interval */
        /* return HRTIMER_NORESTART; ← use this for one-shot behavior */
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
 * Standard char device setup (Steps 1-5) + hrtimer setup:
 *   Step 6: ktime_set()      → create ktime_t timeout value (5 seconds)
 *   Step 7: hrtimer_init()   → initialize hrtimer structure
 *   Step 8: set .function    → attach callback to timer
 *   Step 9: hrtimer_start()  → arm and start the timer
 *
 * ktime_set(4, 1000000000):
 *   4 sec + 1,000,000,000 ns = 4 sec + 1 sec = 5 seconds total.
 *   This is the delay RELATIVE to NOW (HRTIMER_MODE_REL).
 */
static int __init etx_driver_init(void)
{
        ktime_t ktime;   /* holds the timeout duration as ktime_t value      */

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
         * Step 6: Create ktime_t value for the timeout duration.
         * ktime_set(secs, nsecs) = 4 seconds + 1,000,000,000 ns = 5 seconds.
         * This value is passed to hrtimer_start() as the initial expiry delay.
         */
        ktime = ktime_set(TIMEOUT_SEC, TIMEOUT_NSEC);

        /*
         * Step 7: Initialize the hrtimer structure.
         * hrtimer_init(timer, clock_id, mode):
         *   &etx_hr_timer    = pointer to our hrtimer struct
         *   CLOCK_MONOTONIC  = use monotonic clock (always moves forward)
         *   HRTIMER_MODE_REL = relative mode (expiry = NOW + ktime)
         *
         * After this: timer is initialized but NOT running yet.
         * The callback function must be set AFTER hrtimer_init().
         */
        hrtimer_init(&etx_hr_timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);

        /*
         * Step 8: Attach callback function to the timer.
         * Must be set AFTER hrtimer_init() — init might clear this field.
         * timer_callback() will be called when the timer expires.
         */
        etx_hr_timer.function = &timer_callback;

        /*
         * Step 9: Arm and start the hrtimer.
         * hrtimer_start(timer, time, mode):
         *   &etx_hr_timer    = our hrtimer
         *   ktime            = 5 seconds from NOW
         *   HRTIMER_MODE_REL = relative to current time
         *
         * Timer is now running — expires in 5 seconds.
         * Returns: 0=success, 1=timer was already active (restarted)
         */
        hrtimer_start(&etx_hr_timer, ktime, HRTIMER_MODE_REL);

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
 * hrtimer_cancel() FIRST — stops timer before device cleanup.
 * If callback fires after module unloads → accesses freed memory → crash!
 *
 * hrtimer_cancel(&etx_hr_timer):
 *   Cancels the timer AND waits for callback to complete if currently running.
 *   Safer than hrtimer_try_to_cancel() — guarantees callback is done.
 *   Returns: 0=was not active, 1=was active (cancelled).
 *
 * vs kernel timer exit (del_timer):
 *   del_timer()      → stops timer, does NOT wait for callback
 *   del_timer_sync() → stops timer + waits for callback
 *   hrtimer_cancel() → equivalent to del_timer_sync() for hrtimers
 */
static void __exit etx_driver_exit(void)
{
        hrtimer_cancel(&etx_hr_timer);   /* stop hrtimer + wait for callback  */
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
MODULE_DESCRIPTION("A simple device driver - High Resolution Timer");
MODULE_VERSION("1.22");

/*Complete flow summary
 *
insmod driver.ko
  ├── ktime_set(4, 1000000000)   → ktime = 5 seconds
  ├── hrtimer_init()             → timer initialized, CLOCK_MONOTONIC, REL mode
  ├── .function = timer_callback → callback attached
  └── hrtimer_start(ktime)       → timer started, expires in 5 seconds

After 5 seconds:
  └── timer_callback() fires [interrupt context]
        ├── pr_info("Timer Callback function Called [0]")
        ├── hrtimer_forward_now(timer, ktime_set(4, 1e9))  → advance expiry +5s
        └── return HRTIMER_RESTART  → timer re-armed for next 5 seconds

After 10 seconds:
  └── timer_callback() fires [count=1] ... and so on

rmmod driver
  └── hrtimer_cancel() → stops timer + waits for any running callback → cleanup
 */
