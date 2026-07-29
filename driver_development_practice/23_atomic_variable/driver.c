/***************************************************************************//**
*  \file       driver.c
*  \details    Simple Linux device driver (Atomic Variables)
*
*  FLOW:
*    Two threads run concurrently, both accessing shared variables.
*    Thread1 and Thread2:
*      - atomic_inc(&etx_global_variable) → safe, no lock needed
*      - test_and_change_bit(1, &etc_bit_check) → flip bit 1, return old value
*      - atomic_read() → safely read and print current value
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


/* ── ATOMIC VARIABLES ────────────────────────────────────────────────────── */

/*
 * etx_global_variable — atomic integer shared between Thread1 and Thread2.
 *
 * Why NOT use plain int?
 *   plain int: read + increment + write = 3 separate CPU instructions
 *   Two threads can interleave these → race condition → wrong result
 *
 * Why atomic_t?
 *   atomic_inc() = get + increment + write in ONE CPU instruction
 *   No two threads can interleave → always correct → NO lock needed
 *
 * ATOMIC_INIT(0): initialize counter to 0 at compile time.
 */
atomic_t etx_global_variable = ATOMIC_INIT(0);

/*
 * etc_bit_check — plain unsigned int used for ATOMIC BITWISE operations.
 * Note: for atomic bit ops, we DON'T need atomic_t — any variable works.
 * The bit operation functions take a void* pointer and operate atomically.
 * Bit 1 of this variable will be toggled by both threads.
 */
unsigned int etc_bit_check = 0;

/* standard char driver globals */
dev_t dev = 0;
static struct class *dev_class;
static struct cdev etx_cdev;

/* pointers to our two kernel threads */
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
 * thread_function1() — [eTx Thread1]
 *
 * Demonstrates TWO types of atomic operations every second:
 *
 * 1. atomic_inc(&etx_global_variable):
 *    Atomically increments the integer by 1 — ONE CPU instruction.
 *    No mutex/spinlock needed — safe even with Thread2 doing the same.
 *    Never produces wrong results like plain int++ would.
 *
 * 2. test_and_change_bit(1, &etc_bit_check):
 *    Atomically FLIPS bit 1 of etc_bit_check AND returns the OLD value.
 *    Arguments:
 *      1            = bit number to operate on (bit 1)
 *      &etc_bit_check = address of variable containing the bit
 *    Returns: the value of bit 1 BEFORE the flip (0 or 1)
 *    → If bit was 0: flips to 1, returns 0
 *    → If bit was 1: flips to 0, returns 1
 *    Both threads toggle the SAME bit — each gets the opposite prev value.
 *
 * 3. atomic_read(&etx_global_variable):
 *    Safely reads the current atomic integer value.
 *    NEVER dereference atomic_t directly (etx_global_variable.counter)
 *    — always use atomic_read() to ensure consistency.
 */
int thread_function1(void *pv)
{
    unsigned int prev_value = 0;

    while(!kthread_should_stop()) {

        /* Atomically increment counter — safe without any lock */
        atomic_inc(&etx_global_variable);

        /* Atomically flip bit 1, get old value before the flip */
        prev_value = test_and_change_bit(1, (void*)&etc_bit_check);

        /* Safely read and print the current atomic integer value */
        pr_info("Function1 [value : %u] [bit:%u]\n",
                atomic_read(&etx_global_variable),   /* safe atomic read    */
                prev_value);                          /* old bit value       */

        msleep(1000);   /* sleep 1 second between iterations                */
    }
    return 0;
}

/*
 * thread_function2() — [eTx Thread2]
 *
 * Identical operations as Thread1 — both safely share atomic variables.
 * Because atomic_inc is one CPU instruction, Thread1 and Thread2 can
 * never corrupt each other's value — each sees a consistent, correct result.
 *
 * With plain int (WRONG approach):
 *   Both threads might read same value → both write same result → count lost
 *
 * With atomic_inc (CORRECT):
 *   Each increment is indivisible → count always increases correctly
 */
int thread_function2(void *pv)
{
    unsigned int prev_value = 0;

    while(!kthread_should_stop()) {

        atomic_inc(&etx_global_variable);   /* safe atomic increment        */

        /* Flip same bit 1 — both threads alternate 0→1 and 1→0 */
        prev_value = test_and_change_bit(1, (void*)&etc_bit_check);

        pr_info("Function2 [value : %u] [bit:%u]\n",
                atomic_read(&etx_global_variable),
                prev_value);

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
 * Standard char device setup (Steps 1-5) + TWO concurrent threads.
 * ATOMIC_INIT(0) already initialized etx_global_variable — no extra step.
 * etc_bit_check is plain int initialized to 0 by C default (BSS segment).
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

        /* Step 6a: Create Thread1 — immediately starts incrementing atomically */
        etx_thread1 = kthread_run(thread_function1, NULL, "eTx Thread1");
        if(etx_thread1) {
            pr_err("Kthread1 Created Successfully...\n");
        } else {
            pr_err("Cannot create kthread1\n");
            goto r_device;
        }

        /* Step 6b: Create Thread2 — also starts, both share atomic variables safely */
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
 * Stop threads FIRST, then device cleanup.
 * No explicit atomic variable cleanup needed — no heap allocation for atomic_t.
 */
static void __exit etx_driver_exit(void)
{
        kthread_stop(etx_thread1);   /* stop Thread1, wait for it to return  */
        kthread_stop(etx_thread2);   /* stop Thread2, wait for it to return  */
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
MODULE_DESCRIPTION("A simple device driver - Atomic Variables");
MODULE_VERSION("1.27");

/*Complete flow summary

insmod driver.ko
  ├── etx_global_variable = ATOMIC_INIT(0)  → atomic counter initialized
  ├── etc_bit_check = 0                     → bit variable initialized
  ├── kthread_run(Thread1) → starts running
  └── kthread_run(Thread2) → starts running, both concurrent

Every 1 second (both threads):

[Thread1]                              [Thread2]
─────────                              ─────────
atomic_inc() → value=1                 atomic_inc() → value=2
test_and_change_bit(1) → prev=0        test_and_change_bit(1) → prev=1
pr_info("Function1 [1] [0]")           pr_info("Function2 [2] [1]")
msleep(1000)                           msleep(1000)
atomic_inc() → value=3                 atomic_inc() → value=4
...

rmmod driver
  ├── kthread_stop(thread1)
  └── kthread_stop(thread2)
 */
