/***************************************************************************//**
*  \file       driver.c
*  \details    Simple Linux device driver (Spinlock — Approach 2) - Dynamic
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
#include <linux/kthread.h>       /* kthread_run, kthread_stop                 */
#include <linux/sched.h>         /* task_struct                               */
#include <linux/delay.h>         /* msleep()                                  */
#include <linux/err.h>
/* Note: spinlock API is included via linux/kernel.h / linux/spinlock.h */

#define DRIVER_NAME "dynamic_spinlock_driver"

/* ── SPINLOCK SETUP (Dynamic Method) ─────────────────────────────────────── */
/*
 *   spinlock_t etx_spinlock;
 *   spin_lock_init(&etx_spinlock);   ← call this in init() before threads start
 */
spinlock_t etx_spinlock;

/*
 * Shared variable — accessed by BOTH Thread1 and Thread2 concurrently.
 * Without spinlock: both threads could read/write simultaneously → race condition.
 * With spinlock: only ONE thread accesses it at a time → always correct.
 */
unsigned long etx_global_variable = 0;

/* Pointers to our two competing kernel threads */
static struct task_struct *etx_thread1;
static struct task_struct *etx_thread2;


/* ──THREAD FUNCTION PROTOTYPES ─────────────────────────────────────────────────── */
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
         */
        if(!spin_is_locked(&etx_spinlock)) {
            pr_info("%s: Spinlock is not locked in Thread Function1\n", DRIVER_NAME);
        }

        /*
         * spin_lock(&etx_spinlock):
         *   If lock is FREE → acquire immediately, continue
         *   If lock is HELD → SPIN (busy-wait loop) until free, then acquire
         */
        spin_lock(&etx_spinlock);

        /* spin_is_locked confirms we now own the lock */
        if(spin_is_locked(&etx_spinlock)) {
            pr_info("%s: Spinlock is locked in Thread Function1\n", DRIVER_NAME);
        }
        
        /* ── CRITICAL SECTION START ── */
        etx_global_variable++;   /* safe — only Thread1 is here right now     */
        pr_info("%s: Thread Function1 %lu\n", DRIVER_NAME, etx_global_variable);
        /* ── CRITICAL SECTION END ── */

        /*
         * spin_unlock(&etx_spinlock): - Releases the spinlock.
         */
        spin_unlock(&etx_spinlock);

        msleep(1000);   /* sleep 1 second — CPU freed for other tasks        */
    }
    return 0;
}

/*
 * thread_function2() — runs in [eTx Thread2]
 
 */
int thread_function2(void *pv)
{
    while(!kthread_should_stop()) {

        spin_lock(&etx_spinlock);         /* acquire lock — spin if Thread1 has it */

        /* ── CRITICAL SECTION START ── */
        etx_global_variable++;
        pr_info("%s: Thread Function2 %lu\n",DRIVER_NAME, etx_global_variable);
        /* ── CRITICAL SECTION END ── */

        spin_unlock(&etx_spinlock);       /* release lock for Thread1            */

        msleep(1000);
    }
    return 0;
}


/* ── MODULE INIT ──────────────────────────────────────────────────────────── */
/*
 * etx_driver_init() — runs on: sudo insmod driver.ko
 * If using dynamic method, spin_lock_init() would go here BEFORE threads.
 *
 */
static int __init etx_driver_init(void)
{

        /* If using dynamic method, call spin_lock_init() HERE (before threads): */
        spin_lock_init(&etx_spinlock);

        /* Create Thread1 — starts spinning/locking immediately */
        etx_thread1 = kthread_run(thread_function1, NULL, "eTx Thread1");
        if(etx_thread1) {
            pr_info("%s: Kthread1 Created Successfully...\n",DRIVER_NAME);
        } else {
            pr_err("%s: Cannot create kthread1\n",DRIVER_NAME);
        }

        /* Create Thread2 — now two threads compete for spinlock */
        etx_thread2 = kthread_run(thread_function2, NULL, "eTx Thread2");
        if(etx_thread2) {
            pr_info("%s: Kthread2 Created Successfully...\n",DRIVER_NAME);
        } else {
            pr_err("%s: Cannot create kthread2\n",DRIVER_NAME);
        }

        pr_info("%s:Device Driver Insert...Done!!!\n",DRIVER_NAME);
        return 0;
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
    
        pr_info("%s:Device Driver Remove...Done!!\n",DRIVER_NAME);
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
