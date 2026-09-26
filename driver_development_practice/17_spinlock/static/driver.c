/***************************************************************************//**
*  \file       driver.c
*  \details    Simple Linux device driver (Spinlock — Approach 1)
*
*  FLOW:
*    insmod → two threads start → both compete for spinlock
*    Thread that gets lock → increments variable → prints → unlocks
*    Other thread spins (busy-waits) → gets lock → increments → prints
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
#include <linux/kthread.h>       /* kthread_run, kthread_stop                 */
#include <linux/sched.h>         /* task_struct                               */
#include <linux/delay.h>         /* msleep()                                  */
#include <linux/err.h>
/* Note: spinlock API is included via linux/kernel.h / linux/spinlock.h */


/* ── SPINLOCK SETUP (Static Method) ─────────────────────────────────────── */
/*
 * DEFINE_SPINLOCK(name) — Static initialization at compile time.
 * Creates spinlock_t variable 'etx_spinlock' in UNLOCKED state.
 *
 * Alternative DYNAMIC METHOD (commented out):
 *   spinlock_t etx_spinlock;
 *   spin_lock_init(&etx_spinlock);   ← call this in init() before threads start
 *
 * Static is simpler and preferred for global spinlocks.
 * This driver uses APPROACH 1 — between two kernel threads (user context only).
 */
DEFINE_SPINLOCK(etx_spinlock);
/* spinlock_t etx_spinlock; */  /* ← uncomment for dynamic method */

/*
 * Shared variable — accessed by BOTH Thread1 and Thread2 concurrently.
 * Without spinlock: both threads could read/write simultaneously → race condition.
 * With spinlock: only ONE thread accesses it at a time → always correct.
 */
unsigned long etx_global_variable = 0;

/* Standard char driver globals */
dev_t dev = 0;
static struct class *dev_class;
static struct cdev etx_cdev;

/* Pointers to our two competing kernel threads */
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
 * thread_function1() — runs in [eTx Thread1]
 *
 * This function also demonstrates spin_is_locked() for status checking.
 *
 * APPROACH 1: spin_lock / spin_unlock
 *   Used when sharing data ONLY between kernel threads (user context).
 *   If the spinlock is held by Thread2 when Thread1 tries to lock it:
 *     → Thread1 SPINS (loops continuously) checking the lock
 *     → Thread1 does NOT sleep — it stays on CPU burning cycles
 *     → As soon as Thread2 unlocks → Thread1 gets the lock immediately
 *
 * This "spinning" is why spinlocks are faster than mutexes for very
 * short critical sections — no context switch overhead.
 */
int thread_function1(void *pv)
{
    while(!kthread_should_stop()) {

        /*
         * spin_is_locked() — check if spinlock is currently held.
         * Returns non-zero if locked, 0 if free.
         * Used here just for demonstration/debugging.
         * In production: don't check before locking — just call spin_lock().
         * The check+lock is NOT atomic — race can occur between them.
         */
        if(!spin_is_locked(&etx_spinlock)) {
            pr_info("Spinlock is not locked in Thread Function1\n");
        }

        /*
         * spin_lock(&etx_spinlock):
         *   If lock is FREE → acquire immediately, continue
         *   If lock is HELD → SPIN (busy-wait loop) until free, then acquire
         *
         * KEY DIFFERENCE from mutex_lock():
         *   mutex_lock → thread SLEEPS (CPU goes to other tasks)
         *   spin_lock  → thread SPINS (CPU stays here, checking in loop)
         *
         * Spinlock is better when:
         *   ✅ Critical section is very short (microseconds)
         *   ✅ You need to use it in interrupt context (can't sleep)
         * Spinlock is worse when:
         *   ❌ Critical section is long (wastes CPU spinning)
         *   ❌ Only one CPU available (spinning = deadlock on uniprocessor)
         */
        spin_lock(&etx_spinlock);

        /* spin_is_locked confirms we now own the lock */
        if(spin_is_locked(&etx_spinlock)) {
            pr_info("Spinlock is locked in Thread Function1\n");
        }

        /* ── CRITICAL SECTION START ── */
        etx_global_variable++;   /* safe — only Thread1 is here right now     */
        pr_info("In EmbeTronicX Thread Function1 %lu\n", etx_global_variable);
        /* ── CRITICAL SECTION END ── */

        /*
         * spin_unlock(&etx_spinlock):
         *   Releases the spinlock.
         *   If Thread2 was spinning waiting → it immediately acquires it.
         *   MUST be called by the SAME context that called spin_lock().
         */
        spin_unlock(&etx_spinlock);

        msleep(1000);   /* sleep 1 second — CPU freed for other tasks        */
    }
    return 0;
}

/*
 * thread_function2() — runs in [eTx Thread2]
 *
 * Simpler version — no spin_is_locked() checks (not needed in production).
 * Same pattern: lock → critical section → unlock → sleep.
 */
int thread_function2(void *pv)
{
    while(!kthread_should_stop()) {

        spin_lock(&etx_spinlock);         /* acquire lock — spin if Thread1 has it */

        /* ── CRITICAL SECTION START ── */
        etx_global_variable++;
        pr_info("In EmbeTronicX Thread Function2 %lu\n", etx_global_variable);
        /* ── CRITICAL SECTION END ── */

        spin_unlock(&etx_spinlock);       /* release lock for Thread1            */

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
 * Standard char device setup (Steps 1-5) + TWO kernel threads.
 *
 * NOTE: DEFINE_SPINLOCK already initialized the spinlock globally.
 * If using dynamic method, spin_lock_init() would go here BEFORE threads.
 *
 * ⚠️ The commented spin_lock_init() at the bottom of init() is WRONG placement
 *    — it comes AFTER kthread_run() which means threads could use
 *    uninitialized spinlock. Always init BEFORE creating threads.
 *    (This is a bug in the original tutorial code — we fix it here.)
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

        /* If using dynamic method, call spin_lock_init() HERE (before threads): */
        /* spin_lock_init(&etx_spinlock); */

        /* Step 6a: Create Thread1 — starts spinning/locking immediately */
        etx_thread1 = kthread_run(thread_function1, NULL, "eTx Thread1");
        if(etx_thread1) {
            pr_err("Kthread1 Created Successfully...\n");
        } else {
            pr_err("Cannot create kthread1\n");
            goto r_device;
        }

        /* Step 6b: Create Thread2 — now two threads compete for spinlock */
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
 * kthread_stop() sets kthread_should_stop()=true → threads exit their loops.
 *
 * NOTE: No explicit spinlock destroy in Linux kernel — unlike userspace.
 *       Just ensure spinlock is not held when module exits.
 */
static void __exit etx_driver_exit(void)
{
        kthread_stop(etx_thread1);   /* stop Thread1 — wait for it to exit   */
        kthread_stop(etx_thread2);   /* stop Thread2 — wait for it to exit   */
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
MODULE_DESCRIPTION("A simple device driver - Spinlock");
MODULE_VERSION("1.18");

/*
insmod driver.ko
  ├── DEFINE_SPINLOCK → etx_spinlock initialized (UNLOCKED) at compile time
  ├── kthread_run(Thread1) → [eTx Thread1] starts
  └── kthread_run(Thread2) → [eTx Thread2] starts

Both threads run concurrently (Approach 1):

[eTx Thread1]                       [eTx Thread2]
─────────────                       ─────────────
spin_lock() ← gets lock             spin_lock() ← SPINS (busy-wait loop)
etx_global_variable++ (→1)          ← still spinning...
pr_info("Thread1: 1")               ← still spinning...
spin_unlock()                       ← GETS LOCK immediately
msleep(1000)                        etx_global_variable++ (→2)
                                    pr_info("Thread2: 2")
                                    spin_unlock()
spin_lock() ← gets lock             msleep(1000)
etx_global_variable++ (→3)
...

rmmod driver
  ├── kthread_stop(thread1)  → Thread1 exits while loop
  └── kthread_stop(thread2)  → Thread2 exits while loop
 */
