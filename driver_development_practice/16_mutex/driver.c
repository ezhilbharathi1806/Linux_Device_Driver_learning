/***************************************************************************//**
*  \file       driver.c
*  \details    Simple Linux device driver (Mutex)
*
*  FLOW:
*    insmod driver.ko
*      → mutex_init() initializes the mutex
*      → Thread1 and Thread2 created — both run concurrently
*      → Each thread locks mutex → increments shared variable → unlocks
*      → Only ONE thread can access the variable at a time
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
#include <linux/kthread.h>       /* kthread_run, kthread_stop, kthread_should_stop */
#include <linux/sched.h>         /* task_struct                               */
#include <linux/delay.h>         /* msleep()                                  */
#include <linux/mutex.h>         /* mutex_init, mutex_lock, mutex_unlock — NEW */
#include <linux/err.h>


/* ── GLOBAL VARIABLES ────────────────────────────────────────────────────── */

/*
 * etx_mutex: the mutex that protects etx_global_variable.
 *
 * DYNAMIC METHOD: declared as struct, initialized later in init()
 * with mutex_init(&etx_mutex).
 *
 * Alternative STATIC METHOD (compile time):
 *   DEFINE_MUTEX(etx_mutex);  ← one line, no mutex_init() needed
 *
 * Use dynamic when mutex is inside a per-device runtime struct.
 * Use static for global module-level mutexes.
 */
struct mutex etx_mutex;

/*
 * Shared variable accessed by BOTH Thread1 and Thread2.
 * Without mutex: both threads could read/modify it simultaneously
 *   → race condition → wrong/unpredictable value.
 * With mutex: only ONE thread accesses it at a time → always correct.
 */
unsigned long etx_global_variable = 0;

/* Standard char driver globals */
dev_t dev = 0;
static struct class *dev_class;
static struct cdev etx_cdev;

/* Pointers to our two kernel threads */
static struct task_struct *etx_thread1;
static struct task_struct *etx_thread2;


/* ── FUNCTION PROTOTYPES ─────────────────────────────────────────────────── */
static int      __init etx_driver_init(void);
static void     __exit etx_driver_exit(void);
static int      etx_open(struct inode *inode, struct file *file);
static int      etx_release(struct inode *inode, struct file *file);
static ssize_t  etx_read(struct file *filp, char __user *buf, size_t len, loff_t *off);
static ssize_t  etx_write(struct file *filp, const char *buf, size_t len, loff_t *off);
int thread_function1(void *pv);
int thread_function2(void *pv);


/* ── THREAD FUNCTIONS ────────────────────────────────────────────────────── */

/*
 * thread_function1() — runs in [eTx Thread1] kernel thread
 *
 * Loops until kthread_stop() is called from exit().
 * Each iteration: lock mutex → increment shared variable → unlock → sleep.
 *
 * CRITICAL SECTION (between lock and unlock):
 *   Only this thread (or thread2 if it got the lock) can be here.
 *   Other thread SLEEPS in mutex_lock() waiting for unlock.
 *
 * WHY lock before the pr_info too?
 *   Because we want to print WHILE holding the lock — to ensure
 *   the printed value matches what THIS thread incremented.
 *   If we printed after unlock, thread2 could increment before we print.
 */
int thread_function1(void *pv)
{
    while(!kthread_should_stop()) {

        /*
         * mutex_lock(&etx_mutex):
         * If mutex is FREE   → lock it immediately, continue
         * If mutex is HELD   → sleep here until the other thread unlocks
         *
         * This guarantees only ONE thread runs the code between
         * mutex_lock() and mutex_unlock() at any given time.
         */
        mutex_lock(&etx_mutex);

        /* ── CRITICAL SECTION START ── */
        etx_global_variable++;                                  /* safe now — we own the mutex */
        pr_info("In EmbeTronicX Thread Function1 %lu\n", etx_global_variable);
        /* ── CRITICAL SECTION END ── */

        /*
         * mutex_unlock(&etx_mutex):
         * Releases the mutex — wakes up any thread sleeping in mutex_lock().
         * MUST be the SAME thread that called mutex_lock() above.
         * MUST always call unlock — forgetting causes deadlock!
         */
        mutex_unlock(&etx_mutex);

        msleep(1000);   /* sleep 1 second — let thread2 have a turn */
    }
    return 0;
}

/*
 * thread_function2() — runs in [eTx Thread2] kernel thread
 *
 * Identical structure to thread_function1.
 * Both threads compete for the same mutex — only one runs the
 * critical section at a time.
 *
 * Without mutex: both threads could do:
 *   Thread1 reads etx_global_variable = 5
 *   Thread2 reads etx_global_variable = 5  ← same value!
 *   Thread1 writes 6
 *   Thread2 writes 6  ← should be 7! RACE CONDITION!
 *
 * With mutex: one increments fully before other even reads.
 */
int thread_function2(void *pv)
{
    while(!kthread_should_stop()) {

        mutex_lock(&etx_mutex);         /* wait for mutex, then lock          */

        /* ── CRITICAL SECTION START ── */
        etx_global_variable++;
        pr_info("In EmbeTronicX Thread Function2 %lu\n", etx_global_variable);
        /* ── CRITICAL SECTION END ── */

        mutex_unlock(&etx_mutex);       /* release mutex for other thread     */

        msleep(1000);
    }
    return 0;
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

/* etx_read and etx_write not used in this tutorial — just log */
static ssize_t etx_read(struct file *filp,
                         char __user *buf, size_t len, loff_t *off)
{
        pr_info("Read function\n");
        return 0;
}

static ssize_t etx_write(struct file *filp,
                          const char __user *buf, size_t len, loff_t *off)
{
        pr_info("Write Function\n");
        return len;
}


/* ── MODULE INIT ──────────────────────────────────────────────────────────── */
/*
 * etx_driver_init() — runs on: sudo insmod driver.ko
 *
 * Standard char device setup (Steps 1–5) + NEW mutex + threads:
 *   Step 6: mutex_init()  → initialize mutex BEFORE threads start
 *   Step 7: kthread_run() → create Thread1 and Thread2
 *
 * ⚠️ ORDERING: mutex_init() MUST come BEFORE kthread_run().
 *    If threads start before mutex is initialized → undefined behavior.
 *    The tutorial correctly places mutex_init() before thread creation.
 */
static int __init etx_driver_init(void)
{
        /* Step 1: Get dynamic Major:Minor */
        if((alloc_chrdev_region(&dev, 0, 1, "etx_Dev")) < 0){
                pr_info("Cannot allocate major number\n");
                return -1;
        }
        pr_info("Major = %d Minor = %d \n", MAJOR(dev), MINOR(dev));

        /* Step 2: Init cdev */
        cdev_init(&etx_cdev, &fops);

        /* Step 3: Register cdev */
        if((cdev_add(&etx_cdev, dev, 1)) < 0){
            pr_info("Cannot add the device to the system\n");
            goto r_class;
        }

        /* Step 4: Create device class */
        if(IS_ERR(dev_class = class_create("etx_class"))){
            pr_info("Cannot create the struct class\n");
            goto r_class;
        }

        /* Step 5: Create /dev/etx_device */
        if(IS_ERR(device_create(dev_class, NULL, dev, NULL, "etx_device"))){
            pr_info("Cannot create the Device\n");
            goto r_device;
        }

        /*
         * Step 6: Initialize the mutex.
         *
         * mutex_init(&etx_mutex):
         *   Sets mutex to UNLOCKED state (count=1).
         *   MUST be called before any mutex_lock() or mutex_unlock().
         *   MUST be called BEFORE threads are created (Step 7).
         *
         * If using STATIC method instead:
         *   Replace this line with: DEFINE_MUTEX(etx_mutex); at global scope
         *   And remove this mutex_init() call entirely.
         */
        mutex_init(&etx_mutex);

        /*
         * Step 7a: Create Thread1 using kthread_run().
         * kthread_run() = kthread_create() + wake_up_process() in one call.
         * Thread immediately starts running thread_function1().
         * On first iteration: mutex_lock → increment → print → unlock → sleep
         */
        etx_thread1 = kthread_run(thread_function1, NULL, "eTx Thread1");
        if(etx_thread1) {
            pr_err("Kthread1 Created Successfully...\n");
        } else {
            pr_err("Cannot create kthread1\n");
            goto r_device;
        }

        /*
         * Step 7b: Create Thread2 — now TWO threads compete for the mutex.
         * Since Thread1 may already hold the mutex, Thread2 will sleep
         * in mutex_lock() until Thread1 calls mutex_unlock().
         */
        etx_thread2 = kthread_run(thread_function2, NULL, "eTx Thread2");
        if(etx_thread2) {
            pr_err("Kthread2 Created Successfully...\n");
        } else {
            pr_err("Cannot create kthread2\n");
            goto r_device;
        }

        pr_info("Device Driver Insert...Done!!!\n");
        return 0;

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
 * Stop BOTH threads FIRST before device cleanup.
 * kthread_stop() sets kthread_should_stop()=true, wakes thread,
 * and WAITS for it to return.
 *
 * ⚠️ If a thread is sleeping in mutex_lock() when kthread_stop() is called:
 *    kthread_should_stop() doesn't automatically wake it from mutex_lock().
 *    The thread wakes when the other thread releases the mutex, THEN
 *    checks kthread_should_stop() at the top of its while loop and exits.
 *
 * NOTE: No explicit mutex destroy needed in Linux kernel —
 *    unlike userspace pthreads, kernel mutex has no destroy function.
 *    Just ensure it's not locked when module exits.
 */
static void __exit etx_driver_exit(void)
{
        kthread_stop(etx_thread1);   /* stop Thread1 — waits for it to return */
        kthread_stop(etx_thread2);   /* stop Thread2 — waits for it to return */

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
MODULE_DESCRIPTION("A simple device driver - Mutex");
MODULE_VERSION("1.17");

/*
insmod driver.ko
  ├── mutex_init(&etx_mutex)     → mutex initialized (unlocked)
  ├── kthread_run(Thread1)       → [eTx Thread1] starts running
  └── kthread_run(Thread2)       → [eTx Thread2] starts running

Both threads run concurrently:

[eTx Thread1]                    [eTx Thread2]
─────────────                    ─────────────
mutex_lock()  ← gets lock        mutex_lock()  ← SLEEPS (Thread1 has it)
etx_global_variable++ (→1)
pr_info("Thread1: 1")
mutex_unlock()                   ← WAKES UP, gets lock
msleep(1000)                     etx_global_variable++ (→2)
                                 pr_info("Thread2: 2")
                                 mutex_unlock()
mutex_lock()  ← gets lock       msleep(1000)
etx_global_variable++ (→3)
...and so on alternating...

rmmod driver
  ├── kthread_stop(etx_thread1)  → Thread1 exits its while loop
  └── kthread_stop(etx_thread2)  → Thread2 exits its while loop
 */
