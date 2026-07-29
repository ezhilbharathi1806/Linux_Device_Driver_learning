/***************************************************************************//**
*  \file       driver.c
*  \details    Simple Linux device driver (Seqlock)
*
*  FLOW:
*    Thread1 (WRITER): write_seqlock → increment variable → write_sequnlock
*    Thread2 (READER): do { seq=read_seqbegin; read var; } while(read_seqretry)
*
*  KEY POINT:
*    Writer NEVER waits for readers — reader retries if write happened.
*    This gives writer priority — no writer starvation like in rwlock.
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
#include <linux/seqlock.h>       /* seqlock_t, seqlock_init, write_seqlock,
                                    write_sequnlock, read_seqbegin,
                                    read_seqretry — CORE seqlock header       */
#include <linux/err.h>


/* ── SEQLOCK SETUP ───────────────────────────────────────────────────────── */
/*
 * etx_seq_lock: the seqlock variable.
 *
 * Internally contains:
 *   - unsigned int sequence  → counter: even=idle, odd=writer active
 *   - spinlock_t lock        → used internally by writers for exclusion
 *
 * Initialized via seqlock_init() in init().
 * Writers use write_seqlock/write_sequnlock (exclusive via internal spinlock).
 * Readers use read_seqbegin/read_seqretry (no locking — just check seq number).
 */
seqlock_t etx_seq_lock;

/*
 * etx_global_variable: shared data protected by seqlock.
 * Thread1 WRITES (increments) it.
 * Thread2 READS it.
 *
 * ⚠️ Must be simple type — NO pointers.
 * Seqlock cannot safely protect pointer-based structs because reader may
 * follow a pointer that writer is changing → segfault/crash.
 */
unsigned long etx_global_variable = 0;

/* standard char driver globals */
dev_t dev = 0;
static struct class *dev_class;
static struct cdev etx_cdev;

/* Writer thread and reader thread pointers */
static struct task_struct *etx_thread1;   /* WRITER */
static struct task_struct *etx_thread2;   /* READER */


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
 * thread_function1() — WRITER [eTx Thread1]
 *
 * Increments etx_global_variable every second using seqlock write protection.
 *
 * write_seqlock(&etx_seq_lock):
 *   1. Acquires the internal spinlock (blocks other WRITERS — not readers)
 *   2. Increments sequence counter (makes it ODD → signals "writing in progress")
 *   → Readers will see odd seq → know write is happening → start retrying
 *
 * write_sequnlock(&etx_seq_lock):
 *   1. Increments sequence counter again (makes it EVEN → signals "write done")
 *   2. Releases the internal spinlock
 *   → Readers will see seq changed → read_seqretry returns 1 → they retry
 *   → On retry: they read the NEW value and see seq is now consistent → done
 *
 * KEY ADVANTAGE:
 *   Reader NEVER blocks this writer — writer always proceeds immediately.
 *   Compare to rwlock: if 10 readers are reading, writer must WAIT for all.
 *   With seqlock: writer proceeds, readers just retry with new value.
 */
int thread_function1(void *pv)
{
    while(!kthread_should_stop()) {

        write_seqlock(&etx_seq_lock);   /* lock + seq becomes ODD (writing) */

        /* ── WRITE CRITICAL SECTION ── */
        etx_global_variable++;          /* increment the shared variable     */
        /* ── END CRITICAL SECTION ── */

        write_sequnlock(&etx_seq_lock); /* seq becomes EVEN (done) + unlock  */

        msleep(1000);   /* sleep 1 second before next write                  */
    }
    return 0;
}

/*
 * thread_function2() — READER [eTx Thread2]
 *
 * Reads etx_global_variable every second using seqlock read protocol.
 *
 * The do-while loop implements the seqlock read pattern:
 *
 * Step 1: seq_no = read_seqbegin(&etx_seq_lock)
 *   Reads current sequence number.
 *   If seq is ODD (writer active) → read_seqbegin WAITS until seq is EVEN.
 *   Returns the even sequence number → our "baseline" for comparison.
 *
 * Step 2: read_value = etx_global_variable
 *   Read the data WITHOUT any lock.
 *   Writer may be writing RIGHT NOW → data may be inconsistent → that's OK.
 *   We'll detect and retry if that happened.
 *
 * Step 3: while (read_seqretry(&etx_seq_lock, seq_no))
 *   Compare current seq with our saved seq_no.
 *   Returns 1 (retry) if:
 *     a) Current seq is ODD (write in progress during our read), OR
 *     b) Current seq != seq_no (write happened between Step 1 and Step 3)
 *   Returns 0 (done) if seq is even AND matches seq_no → clean read.
 *
 * NOTE: Reader never acquires any lock — pure optimistic concurrency.
 *       May retry many times if writer is very active — but never blocks writer.
 */
int thread_function2(void *pv)
{
    unsigned int seq_no;       /* sequence number at start of read attempt    */
    unsigned long read_value;  /* data read from shared variable              */

    while(!kthread_should_stop()) {

        /*
         * Seqlock read pattern:
         *   do {
         *     1. Get seq number (wait if currently odd)
         *     2. Read data (no lock)
         *   } while (seq changed or is odd → write happened → retry)
         */
        do {
            /* Step 1: get sequence number — blocks until even (writer done) */
            seq_no = read_seqbegin(&etx_seq_lock);

            /* Step 2: read the shared data — no lock needed */
            read_value = etx_global_variable;

            /*
             * Step 3: read_seqretry(&etx_seq_lock, seq_no)
             * Returns 1 (true)  → seq changed or is odd → write interfered
             *                     → loop again from Step 1
             * Returns 0 (false) → seq is even AND unchanged → read was clean
             *                     → exit loop, use read_value
             */
        } while (read_seqretry(&etx_seq_lock, seq_no));

        /* Clean read confirmed — print the consistent value */
        pr_info("In EmbeTronicX Thread Function2 : Read value %lu\n", read_value);

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

/* Not used in this tutorial */
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
 * Standard char device setup (Steps 1-5) + seqlock init + two threads.
 *
 * ⚠️ BUG FIX vs original tutorial:
 *    Original calls seqlock_init() AFTER kthread_run() — WRONG!
 *    Thread could use seqlock before it's initialized → undefined behavior.
 *    CORRECT: seqlock_init() BEFORE kthread_run() — fixed below.
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
         * Step 6: Initialize the seqlock — MUST be BEFORE thread creation.
         *
         * seqlock_init(&etx_seq_lock):
         *   Sets internal sequence counter = 0 (even = unlocked state)
         *   Initializes internal spinlock (used by writers)
         *
         * After this: seqlock is ready.
         * Readers see seq=0 (even) → ok to read.
         * Writers can take spinlock and increment seq.
         */
        seqlock_init(&etx_seq_lock);   /* ← FIXED: init BEFORE threads start */

        /* Step 7a: Create WRITER thread */
        etx_thread1 = kthread_run(thread_function1, NULL, "eTx Thread1");
        if(etx_thread1) {
            pr_err("Kthread1 Created Successfully...\n");
        } else {
            pr_err("Cannot create kthread1\n");
            goto r_device;
        }

        /* Step 7b: Create READER thread */
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
 * Stop both threads FIRST, then device cleanup.
 * No explicit seqlock destroy needed — no heap allocation.
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
MODULE_DESCRIPTION("A simple device driver - Seqlock");
MODULE_VERSION("1.28");

/* Complete flow summary
insmod driver.ko
  ├── seqlock_init() → seq=0 (even), spinlock initialized
  ├── kthread_run(Thread1) → WRITER starts
  └── kthread_run(Thread2) → READER starts

Every second:

[Thread1 WRITER]                    [Thread2 READER]
────────────────                    ────────────────
write_seqlock()                     seq_no = read_seqbegin() → seq=0
  → seq becomes 1 (ODD)               (if seq is odd → waits for even)
etx_global_variable++ (→1)
write_sequnlock()                   read_value = etx_global_variable
  → seq becomes 2 (EVEN)
msleep(1000)                        read_seqretry(seq=0) → seq is now 2
                                      → 2 != 0 → returns 1 → RETRY!
                                    seq_no = read_seqbegin() → seq=2
                                    read_value = etx_global_variable (=1)
                                    read_seqretry(seq=2) → seq still 2
                                      → 2 == 2 AND even → returns 0 → DONE
                                    pr_info("Read value 1") ← consistent!

rmmod driver
  ├── kthread_stop(writer)
  └── kthread_stop(reader)
  */
