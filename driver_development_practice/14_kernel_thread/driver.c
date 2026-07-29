/***************************************************************************//**
*  \file       driver.c
*  \details    Simple Linux device driver (Kernel Thread)
*
*  FLOW:
*    insmod driver.ko
*      → etx_driver_init() creates and starts [eTx Thread]
*      → thread runs thread_function() — prints every second
*
*    rmmod driver
*      → etx_driver_exit() calls kthread_stop()
*      → kthread_should_stop() returns true inside thread
*      → thread exits its while loop and returns 0
*      → kthread_stop() unblocks and cleanup continues
*
*  \Tested with Linux raspberrypi 5.10.27-v7l-embetronicx-custom+
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
#include <linux/kthread.h>       /* kthread_create, kthread_run, kthread_stop,
                                    kthread_should_stop, wake_up_process       */
#include <linux/sched.h>         /* task_struct — the thread/process descriptor*/
#include <linux/delay.h>         /* msleep() — sleep in milliseconds           */
#include <linux/err.h>           /* IS_ERR(), ERR_PTR()                        */


/* ── GLOBAL VARIABLES ────────────────────────────────────────────────────── */
dev_t dev = 0;
static struct class *dev_class;
static struct cdev etx_cdev;

/*
 * Pointer to our kernel thread's task_struct.
 * kthread_create() or kthread_run() fills this in.
 * We store it so kthread_stop(etx_thread) can stop it during exit.
 */
static struct task_struct *etx_thread;


/* ── FUNCTION PROTOTYPES ─────────────────────────────────────────────────── */
static int      __init etx_driver_init(void);
static void     __exit etx_driver_exit(void);
static int      etx_open(struct inode *inode, struct file *file);
static int      etx_release(struct inode *inode, struct file *file);
static ssize_t  etx_read(struct file *filp, char __user *buf, size_t len, loff_t *off);
static ssize_t  etx_write(struct file *filp, const char *buf, size_t len, loff_t *off);

int thread_function(void *pv);   /* forward declaration for thread function   */


/* ── KERNEL THREAD FUNCTION ───────────────────────────────────────────────── */
/*
 * thread_function() — runs inside the [eTx Thread] kernel thread
 *
 * This is a LONG-RUNNING thread — loops until told to stop.
 * Runs in PROCESS CONTEXT — CAN sleep, use mutex, etc.
 *
 * @pv : void* argument passed from kthread_create()/kthread_run().
 *       NULL here — not used. In real drivers, cast to your struct:
 *         struct my_data *d = (struct my_data *)pv;
 *
 * HOW THE LOOP WORKS:
 *   kthread_should_stop() returns FALSE → keep looping (normal state)
 *   kthread_should_stop() returns TRUE  → exit loop, return 0
 *
 * kthread_should_stop() becomes TRUE when kthread_stop(etx_thread)
 * is called from etx_driver_exit().
 *
 * msleep(1000):
 *   Sleeps for 1000ms (1 second) — CPU freed during sleep.
 *   kthread_stop() wakes the sleeping thread immediately when called,
 *   so the thread doesn't have to wait 1 second to respond to stop.
 */
int thread_function(void *pv)
{
    int i = 0;

    /* Loop runs every second until kthread_stop() is called from exit() */
    while(!kthread_should_stop()) {
        pr_info("In EmbeTronicX Thread Function %d\n", i++);
        msleep(1000);   /* sleep 1 second — releases CPU to other tasks     */
    }

    return 0;   /* return value is passed to kthread_stop() caller          */
}


/* ── FILE OPERATIONS TABLE ───────────────────────────────────────────────── */
static struct file_operations fops =
{
        .owner   = THIS_MODULE,
        .read    = etx_read,
        .write   = etx_write,
        .open    = etx_open,
        .release = etx_release,
};


/* ── DEVICE FILE HANDLER FUNCTIONS ───────────────────────────────────────── */
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
static ssize_t etx_read(struct file *filp, char __user *buf, size_t len, loff_t *off)
{
        pr_info("Read function\n");
        return 0;
}

/* Called when user writes /dev/etx_device — empty in this tutorial */
static ssize_t etx_write(struct file *filp, const char __user *buf, size_t len, loff_t *off)
{
        pr_info("Write Function\n");
        return len;
}


/* ── MODULE INIT ──────────────────────────────────────────────────────────── */
/*
 * etx_driver_init() — runs on: sudo insmod driver.ko
 *
 * Standard char device setup (Steps 1-5) + NEW: create and start kernel thread.
 *
 * TWO METHODS shown (Method 1 is active, Method 2 in #if 0 block):
 *
 * Method 1: kthread_create() + wake_up_process()
 *   Step 1: kthread_create() → creates thread in TASK_UNINTERRUPTIBLE state
 *   Step 2: wake_up_process() → moves thread to TASK_RUNNING → thread starts
 *   Use when: you need to do setup between create and start
 *
 * Method 2: kthread_run() = kthread_create() + wake_up_process() in one call
 *   Use when: no setup needed between create and start (simpler)
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
            pr_err("Cannot create the Device\n");
            goto r_device;
        }

        /*
         * Step 6: METHOD 1 — Create then manually start the thread.
         *
         * kthread_create(threadfn, data, name):
         *   thread_function = function to run in the thread
         *   NULL            = no data argument (pv will be NULL)
         *   "eTx Thread"    = thread name (visible in ps as [eTx Thread])
         *
         * Returns: task_struct pointer on success
         *          ERR_PTR(-ENOMEM) on failure — do NOT use IS_ERR() directly,
         *          the tutorial checks for NULL/truthy which also works here.
         *
         * After kthread_create(): thread exists but is SLEEPING (not running).
         * After wake_up_process(): thread starts executing thread_function().
         */
        etx_thread = kthread_create(thread_function, NULL, "eTx Thread");
        if(etx_thread) {
            /* Thread created successfully — now start it */
            wake_up_process(etx_thread);   /* thread begins running thread_function() */
        } else {
            pr_err("Cannot create kthread\n");
            goto r_device;
        }

#if 0
        /*
         * Step 6: METHOD 2 — Create and start in ONE call (alternative).
         *
         * kthread_run() = kthread_create() + wake_up_process() combined.
         * Thread starts immediately after this line.
         * Use this when no setup is needed between create and start.
         *
         * This block is disabled with #if 0 — enable to use Method 2.
         */
        etx_thread = kthread_run(thread_function, NULL, "eTx Thread");
        if(etx_thread) {
            pr_info("Kthread Created Successfully...\n");
        } else {
            pr_err("Cannot create kthread\n");
            goto r_device;
        }
#endif

        pr_info("Device Driver Insert...Done!!!\n");
        return 0;

/* Cleanup labels — reverse order */
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
 * KEY STEP: kthread_stop() FIRST — always stop thread before device cleanup.
 * If device is removed while thread is still running → thread may access
 * freed/invalid resources → kernel panic!
 *
 * kthread_stop(etx_thread):
 *   1. Sets kthread_should_stop() flag to TRUE for etx_thread
 *   2. Wakes the thread if it is sleeping in msleep()
 *   3. WAITS (blocks) until thread_function() returns
 *   4. Returns the return value of thread_function() (0 in our case)
 *
 * After kthread_stop() returns → [eTx Thread] is completely gone.
 * Then safe to destroy device, class, cdev, chrdev region.
 */
static void __exit etx_driver_exit(void)
{
        /*
         * Stop the kernel thread FIRST.
         * This sets kthread_should_stop()=true, wakes thread from msleep(),
         * waits for thread to exit its while loop and return.
         */
        kthread_stop(etx_thread);

        /* Standard cleanup — reverse order of init */
        device_destroy(dev_class, dev);
        class_destroy(dev_class);
        cdev_del(&etx_cdev);
        unregister_chrdev_region(dev, 1);
        pr_info("Device Driver Remove...Done!!\n");
}

module_init(etx_driver_init);
module_exit(etx_driver_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("EmbeTronicX <embetronicx@gmail.com>");
MODULE_DESCRIPTION("A simple device driver - Kernel Thread");
MODULE_VERSION("1.14");

/*Complete flow summary

insmod driver.ko
  ├── Standard char device setup
  └── kthread_create("eTx Thread")   → thread created (sleeping)
        └── wake_up_process()         → thread starts running
              └── thread_function()
                    ├── "In EmbeTronicX Thread Function 0"
                    ├── msleep(1000)
                    ├── "In EmbeTronicX Thread Function 1"
                    ├── msleep(1000)
                    └── ... (every second, forever)

rmmod driver
  └── kthread_stop(etx_thread)
        ├── sets kthread_should_stop() = TRUE
        ├── wakes thread from msleep() immediately
        └── waits for thread to return...
              └── thread: while(!kthread_should_stop()) → FALSE → exit loop
              └── thread returns 0
        └── kthread_stop() unblocks → device cleanup continues
 */
