#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/kthread.h>
#include <linux/wait.h>
#include <linux/delay.h>

static struct task_struct *wait_thread;
static wait_queue_head_t wq;
static int condition = 0;

// Thread function that waits for condition
static int thread_fn(void *data)
{
    pr_info("Thread: Waiting for condition...\n");

    // Sleep until condition becomes true
    wait_event_interruptible(wq, condition != 0);

    pr_info("Thread: Woken up! Condition = %d\n", condition);

    return 0;
}

// Module init
static int __init waitqueue_demo_init(void)
{
    pr_info("Module Loaded\n");

    // Initialize wait queue
    init_waitqueue_head(&wq);

    // Create and run kernel thread
    wait_thread = kthread_run(thread_fn, NULL, "my_thread");

    // Simulate some delay before waking thread
    msleep(3000);

    pr_info("Main: Setting condition and waking thread\n");

    // Set condition and wake up thread
    condition = 1;
    wake_up_interruptible(&wq);	//triggers the wake-up call to threads present in(or using) shared object wq

    return 0;
}

// Module exit
static void __exit waitqueue_demo_exit(void)
{
    pr_info("Module Unloaded\n");
}

module_init(waitqueue_demo_init);
module_exit(waitqueue_demo_exit);

MODULE_LICENSE("GPL");
