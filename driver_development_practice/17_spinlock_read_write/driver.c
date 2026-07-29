/***************************************************************************//**
*  \file       driver.c
*  \details    Simple Linux device driver (Read-Write Spinlock)
*
*  FLOW:
*    insmod → two threads start
*    Thread1 (Writer): write_lock → increment → write_unlock
*    Thread2 (Reader): read_lock  → print value → read_unlock
*    Multiple readers can hold read_lock simultaneously.
*    Writer must wait for all readers to finish before writing.
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


/* ── READ-WRITE SPINLOCK SETUP ───────────────────────────────────────────── */
/*
 * STATIC METHOD: DEFINE_RWLOCK(name)
 * Creates and initializes rwlock_t 'etx_rwlock' at compile time (UNLOCKED state).
 *
 * KEY DIFFERENCE from regular spinlock (DEFINE_SPINLOCK):
 *   spinlock  → single lock, one holder at a time (readers AND writers)
 *   rwlock    → separate read/write locks:
 *               read_lock  → multiple holders allowed simultaneously
 *               write_lock → only ONE holder, blocks everyone else
 *
 * DYNAMIC METHOD (alternative — commented out):
 *   rwlock_t etx_rwlock;
 *   rwlock_init(&etx_rwlock);   ← call in init() BEFORE threads start
 */
static DEFINE_RWLOCK(etx_rwlock);
/* rwlock_t etx_rwlock; */   /* ← uncomment for dynamic method */

/*
 * Shared variable — Thread1 WRITES to it, Thread2 READS from it.
 *
 * Without rwlock: writer and reader could access simultaneously → race condition.
 * With rwlock:
 *   Thread1 uses write_lock → exclusive access → no readers during write
 *   Thread2 uses read_lock  → shared access   → multiple readers allowed
 */
unsigned long etx_global_variable = 0;

/* Standard char driver globals */
dev_t dev = 0;
static struct class *dev_class;
static struct cdev etx_cdev;

/* Pointers to our writer thread and reader thread */
static struct task_struct *etx_thread1;   /* WRITER thread */
static struct task_struct *etx_thread2;   /* READER thread */


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
 * thread_function1() — WRITER thread [eTx Thread1]
 *
 * Uses write_lock — exclusive access for modification.
 *
 * write_lock(&etx_rwlock):
 *   Waits (spins) until NO readers and NO writers are holding the lock.
 *   Once acquired: ALL readers and writers are blocked until write_unlock().
 *   Guarantees exclusive access — safe to modify shared data.
 *
 * write_unlock(&etx_rwlock):
 *   Releases the write lock.
 *   Waiting readers can now all enter simultaneously.
 *   Waiting writers continue to compete for exclusive access.
 *
 * NOTE: Uses Approach 1 (user context only — between kernel threads).
 *       If writing from a tasklet/BH: use write_lock_bh().
 *       If writing from ISR: use write_lock_irq() or write_lock_irqsave().
 */
int thread_function1(void *pv)
{
    while(!kthread_should_stop()) {

        /*
         * write_lock: acquires EXCLUSIVE lock.
         * No reader or writer can enter until write_unlock() is called.
         * Spins (busy-waits) if any reader currently holds read_lock.
         */
        write_lock(&etx_rwlock);

        /* ── WRITE CRITICAL SECTION START ── */
        etx_global_variable++;   /* safe — no readers can see partial writes  */
        /* ── WRITE CRITICAL SECTION END ── */

        write_unlock(&etx_rwlock);   /* release — readers can now enter freely */

        msleep(1000);   /* sleep 1 second between writes                      */
    }
    return 0;
}

/*
 * thread_function2() — READER thread [eTx Thread2]
 *
 * Uses read_lock — allows parallel access with other readers.
 *
 * read_lock(&etx_rwlock):
 *   If NO writer holds the lock → acquires immediately (even if other readers hold it).
 *   If a WRITER holds the lock → spins (waits) until writer releases.
 *
 * KEY ADVANTAGE over regular spinlock:
 *   If you had 4 reader threads, ALL four can hold read_lock simultaneously.
 *   With regular spinlock, only 1 reader runs at a time — others spin needlessly.
 *
 * read_unlock(&etx_rwlock):
 *   Releases this reader's hold on the lock.
 *   If writer was waiting and this was the last reader → writer can now proceed.
 */
int thread_function2(void *pv)
{
    while(!kthread_should_stop()) {

        /*
         * read_lock: acquires SHARED read lock.
         * Multiple threads can hold this at the same time — no conflict.
         * Only blocked when a writer currently holds write_lock.
         */
        read_lock(&etx_rwlock);

        /* ── READ CRITICAL SECTION START ── */
        pr_info("In EmbeTronicX Thread Function2 : Read value %lu\n",
                etx_global_variable);   /* safe — data won't change while we read */
        /* ── READ CRITICAL SECTION END ── */

        read_unlock(&etx_rwlock);   /* release — other readers unaffected       */

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

/* Not used in this tutorial — just logs */
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
 * Standard char device setup (Steps 1-5) + TWO threads (writer + reader).
 *
 * DEFINE_RWLOCK already initialized the rwlock globally — no explicit
 * init step needed here (same as DEFINE_SPINLOCK from Part 23).
 *
 * If using dynamic method: call rwlock_init() BEFORE kthread_run().
 *
 * ⚠️ Tutorial has rwlock_init() commented out AFTER kthread_run() — wrong.
 *    Always initialize locks BEFORE starting threads that use them.
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

        /* If using dynamic method: initialize BEFORE threads below */
        /* rwlock_init(&etx_rwlock); */

        /* Step 6a: Create WRITER thread — increments etx_global_variable */
        etx_thread1 = kthread_run(thread_function1, NULL, "eTx Thread1");
        if(etx_thread1) {
            pr_err("Kthread1 Created Successfully...\n");
        } else {
            pr_err("Cannot create kthread1\n");
            goto r_device;
        }

        /* Step 6b: Create READER thread — reads and prints etx_global_variable */
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
 * No explicit rwlock destroy needed in Linux kernel.
 * Ensure rwlock is not held by any thread when module exits.
 */
static void __exit etx_driver_exit(void)
{
        kthread_stop(etx_thread1);   /* stop writer thread                    */
        kthread_stop(etx_thread2);   /* stop reader thread                    */
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
MODULE_DESCRIPTION("A simple device driver - RW Spinlock");
MODULE_VERSION("1.19");

/*
 * Complete flow summary
insmod driver.ko
  ├── DEFINE_RWLOCK → etx_rwlock initialized (UNLOCKED) at compile time
  ├── kthread_run(Thread1) → [eTx Thread1] WRITER starts
  └── kthread_run(Thread2) → [eTx Thread2] READER starts

Both threads run concurrently:

[eTx Thread1] WRITER             [eTx Thread2] READER
────────────────────             ────────────────────
write_lock()  ← exclusive        read_lock()  ← SPINS (writer has lock)
etx_global_variable++ (→1)       ← still waiting...
write_unlock()                   ← GETS read_lock immediately
msleep(1000)                     pr_info("Read value 1")
                                 read_unlock()
                                 msleep(1000)

write_lock()  ← exclusive        read_lock() ← multiple readers OK simultaneously
etx_global_variable++ (→2)
write_unlock()
 */
