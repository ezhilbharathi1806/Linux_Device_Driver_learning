/***************************************************************************//**
*  \file       driver.c
*  \details    Simple Linux driver (Completion — Static Method)
*
*  CONCEPT:
*    A kernel thread (wait_function) loops forever, sleeping in completion.
*    Two callers wake it up using complete():
*      1. etx_read()       → sets flag=1 → thread prints read count, sleeps again
*      2. etx_driver_exit()→ sets flag=2 → thread exits cleanly
*
*  vs Waitqueue (Part 10):
*    Waitqueue: condition flag + wake_up_interruptible() — manual management
*    Completion: wait_for_completion() + complete() — cleaner, race-free
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
#include <linux/kthread.h>       /* kthread_create, wake_up_process           */
#include <linux/completion.h>    /* DECLARE_COMPLETION, wait_for_completion,
                                    complete, completion_done — CORE header   */
#include <linux/err.h>


/* ── GLOBAL VARIABLES ────────────────────────────────────────────────────── */

/* counts how many times read() was called and completion was triggered */
uint32_t read_count = 0;

/* pointer to our kernel thread */
static struct task_struct *wait_thread;

/*
 * STATIC METHOD: DECLARE_COMPLETION(name)
 * Creates + initializes completion struct 'data_read_done' at compile time.
 *   done = 0 (not completed)
 *   wait = initialized empty waitqueue
 *
 * Equivalent dynamic method:
 *   struct completion data_read_done;
 *   init_completion(&data_read_done);  ← called in init()
 */
DECLARE_COMPLETION(data_read_done);

/* Standard char driver globals */
dev_t dev = 0;
static struct class *dev_class;
static struct cdev etx_cdev;

/*
 * completion_flag — shared condition between thread and driver functions.
 * Tells the thread WHY it was woken up:
 *   0 = default (reset after handling)
 *   1 = complete() came from read()  → print count, sleep again
 *   2 = complete() came from exit()  → thread should exit
 */
int completion_flag = 0;


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


/* ── KERNEL THREAD FUNCTION ───────────────────────────────────────────────── */
/*
 * wait_function() — body of [WaitThread] kernel thread
 *
 * Loops forever, sleeping via wait_for_completion() each iteration.
 * Woken by complete() calls from etx_read() or etx_driver_exit().
 *
 * HOW wait_for_completion DIFFERS from wait_event_interruptible (Part 10):
 *   wait_event_interruptible: needs condition flag + explicit wake_up()
 *   wait_for_completion:      self-contained — complete() is the signal
 *                             NO separate condition flag needed for sleeping
 *                             (we still use completion_flag to know WHY we woke)
 *
 * IMPORTANT: wait_for_completion() is NOT interruptible by signals.
 *   If you need signal interruption, use wait_for_completion_interruptible().
 */
static int wait_function(void *unused)
{
        while(1) {
                pr_info("Waiting For Event...\n");

                /*
                 * wait_for_completion(&data_read_done):
                 *   Puts thread to sleep (TASK_UNINTERRUPTIBLE) until
                 *   someone calls complete(&data_read_done).
                 *
                 *   If done > 0 already (previous complete not consumed):
                 *     returns immediately without sleeping (no lost events!)
                 *   If done == 0:
                 *     thread sleeps here until complete() is called.
                 *
                 *   This is the KEY advantage over waitqueue:
                 *   if complete() is called BEFORE wait_for_completion(),
                 *   the waiter still returns immediately — no event is lost!
                 */
                wait_for_completion(&data_read_done);

                /* Check WHY we were woken up */
                if(completion_flag == 2) {
                        /* Exit signal from etx_driver_exit() */
                        pr_info("Event Came From Exit Function\n");
                        return 0;   /* thread exits cleanly */
                }

                /* Read signal from etx_read() — print count */
                pr_info("Event Came From Read Function - %d\n", ++read_count);

                /* Reset flag — thread loops back and sleeps again */
                completion_flag = 0;
        }
        do_exit(0);   /* safety fallback — normally unreachable */
        return 0;
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

/*
 * etx_read() — wakes the sleeping thread via complete()
 * Triggered by: sudo cat /dev/etx_device
 *
 * completion_done(&data_read_done):
 *   Returns 0 if there ARE waiters (thread is sleeping — safe to complete).
 *   Returns 1 if NO waiters (nobody sleeping — don't call complete again).
 *   Prevents calling complete() when nobody is waiting → avoids done counter
 *   building up unnecessarily.
 *
 * complete(&data_read_done):
 *   Wakes ONE thread waiting in wait_for_completion().
 *   Increments done counter by 1.
 *   Thread checks completion_flag → sees 1 → prints read count.
 */
static ssize_t etx_read(struct file *filp, char __user *buf, size_t len, loff_t *off)
{
        pr_info("Read Function\n");

        completion_flag = 1;   /* set flag BEFORE complete — why we woke up */

        /*
         * Only call complete() if thread is actually waiting.
         * !completion_done() = "there ARE waiters" = safe to signal.
         */
        if(!completion_done(&data_read_done)) {
            complete(&data_read_done);   /* wake the sleeping thread          */
        }

        return 0;
}

static ssize_t etx_write(struct file *filp, const char __user *buf, size_t len, loff_t *off)
{
        pr_info("Write function\n");
        return len;
}


/* ── MODULE INIT ──────────────────────────────────────────────────────────── */
/*
 * etx_driver_init() — runs on: sudo insmod driver.ko
 *
 * Standard char device setup (Steps 1-5) + thread creation.
 * DECLARE_COMPLETION already initialized data_read_done globally — no init step needed.
 *
 * ⚠️ NOTE (Dynamic method bug in original tutorial):
 *    init_completion() is called AFTER kthread_create() — wrong!
 *    Thread could call wait_for_completion() before init → undefined behavior.
 *    Always call init_completion() BEFORE creating the thread.
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
        etx_cdev.owner = THIS_MODULE;
        etx_cdev.ops   = &fops;

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
         * Step 6: Create the kernel thread.
         * kthread_create() → thread created but NOT running.
         * wake_up_process() → thread starts → calls wait_for_completion() → sleeps.
         *
         * Thread immediately sleeps in wait_for_completion() since
         * completion_flag=0 and done=0 (nobody called complete() yet).
         */
        wait_thread = kthread_create(wait_function, NULL, "WaitThread");
        if (wait_thread) {
            pr_info("Thread Created successfully\n");
            wake_up_process(wait_thread);   /* thread starts, immediately sleeps */
        } else {
            pr_err("Thread creation failed\n");
        }

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
 * Must wake the sleeping thread BEFORE device cleanup.
 * If module unloads while thread is in wait_for_completion():
 *   → thread accesses freed completion struct → crash!
 *
 * Sets completion_flag=2 (exit signal) THEN calls complete()
 * → thread wakes, sees flag==2, returns 0 → thread exits.
 * Then safe to destroy device resources.
 */
static void __exit etx_driver_exit(void)
{
        completion_flag = 2;   /* signal: time to exit                        */

        /* Wake thread if it is sleeping — safe guard with completion_done() */
        if(!completion_done(&data_read_done)) {
            complete(&data_read_done);   /* wake thread → sees flag=2 → exits */
        }

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
MODULE_DESCRIPTION("A simple device driver - Completion (Static Method)");
MODULE_VERSION("1.23");

/*Complete flow summary
insmod driver.ko
  ├── DECLARE_COMPLETION → data_read_done.done=0 (not completed)
  ├── kthread_create("WaitThread") → thread created
  └── wake_up_process()            → thread runs wait_function()
          └── wait_for_completion() → SLEEPS (done=0)

sudo cat /dev/etx_device
  ├── etx_read()
  │     ├── completion_flag = 1
  │     └── complete(&data_read_done)   → thread wakes
  │               └── completion_flag==1 → pr_info("Read Function - 1")
  │               └── completion_flag=0  → loops back → SLEEPS again

sudo rmmod driver
  ├── completion_flag = 2
  └── complete(&data_read_done)         → thread wakes
            └── completion_flag==2 → pr_info("Event Came From Exit Function")
            └── return 0 → thread exits
 */
