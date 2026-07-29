/***************************************************************************//**
*  \file       driver.c
*  \details    Simple Linux device driver (Signals) — NO interrupt version
*
*  SIMPLIFIED FLOW (vs original):
*    Original: read() → fire IRQ → ISR sends signal
*    This:     read() → sends signal DIRECTLY (simpler, no IRQ needed)
*
*  HOW IT WORKS:
*    1. User app opens driver and calls ioctl(REG_CURRENT_TASK)
*       → driver stores app's task_struct pointer
*    2. User app (or cat) reads from /dev/etx_device
*       → etx_read() directly calls send_sig_info()
*       → signal 44 delivered to user app
*    3. User app receives signal → handler prints "Received signal"
*    4. User app closes driver → task pointer cleared
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
#include <linux/ioctl.h>         /* _IOW macro for IOCTL command definition   */
#include <linux/err.h>
#include <linux/sched/signal.h>  /* send_sig_info(), task_struct              */
                                 /* Use this header for newer kernels (5.x+)  */
                                 /* Older kernels: #include <linux/sched.h>   */


/* ── MACROS ───────────────────────────────────────────────────────────────── */

/*
 * SIGETX = 44: Our custom real-time signal number.
 * Linux real-time signals range from 32 to 64 (SIGRTMIN to SIGRTMAX).
 * Signal 44 is free to use as a custom application signal.
 * Must match the same define in the user app (test_app.c).
 */
#define SIGETX  44

/*
 * REG_CURRENT_TASK: IOCTL command to register the user app with the driver.
 * The app calls ioctl(fd, REG_CURRENT_TASK, &number) to tell the driver
 * "I am the process you should send signals to."
 * _IOW('a', 'a', int32_t*) encodes direction + magic + cmd + type.
 */
#define REG_CURRENT_TASK  _IOW('a', 'a', int32_t*)


/* ── GLOBAL VARIABLES ────────────────────────────────────────────────────── */

/*
 * task: stores the task_struct* of the registered user app process.
 * get_current() returns the task_struct of the currently running process.
 * Set in etx_ioctl() when app calls REG_CURRENT_TASK.
 * Cleared in etx_release() when the app closes the driver.
 * NULL means no app is registered — don't send any signal.
 */
static struct task_struct *task = NULL;

/*
 * signum: stores which signal number to send.
 * Set to SIGETX (44) when app registers.
 * Used in etx_read() when sending the signal.
 */
static int signum = 0;

int32_t value = 0;   /* placeholder — not used for signal logic here         */

/* Standard char driver globals */
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
static long     etx_ioctl(struct file *file, unsigned int cmd, unsigned long arg);


/* ── FILE OPERATIONS TABLE ───────────────────────────────────────────────── */
static struct file_operations fops =
{
        .owner          = THIS_MODULE,
        .read           = etx_read,
        .write          = etx_write,
        .open           = etx_open,
        .unlocked_ioctl = etx_ioctl,   /* handles REG_CURRENT_TASK command   */
        .release        = etx_release,
};


/* ── DRIVER FUNCTIONS ────────────────────────────────────────────────────── */

/* Called when user opens /dev/etx_device */
static int etx_open(struct inode *inode, struct file *file)
{
        pr_info("Device File Opened...!!!\n");
        return 0;
}

/*
 * etx_release() — called when user closes /dev/etx_device
 *
 * Unregisters the user app from the driver.
 * get_current() here gives the task of the process that is closing the file.
 * If it matches the registered task → clear it (app is done).
 * After clearing: etx_read() won't send any signal until re-registered.
 */
static int etx_release(struct inode *inode, struct file *file)
{
        struct task_struct *ref_task = get_current();
        pr_info("Device File Closed...!!!\n");

        /* Clear registered task if the closing process is the registered one */
        if(ref_task == task) {
            task = NULL;   /* unregister — no more signals will be sent      */
        }
        return 0;
}

/*
 * etx_read() — SENDS the signal DIRECTLY when user reads the device
 * Triggered by: cat /dev/etx_device  OR  read(fd, ...) in user app
 *
 * SIMPLIFIED vs original:
 *   Original: etx_read() → fire IRQ → ISR → send_sig_info()
 *   Here:     etx_read() → send_sig_info() directly (no IRQ involved)
 *
 * struct kernel_siginfo (newer kernels) or struct siginfo (older kernels):
 *   si_signo = signal number to send (SIGETX = 44)
 *   si_code  = SI_QUEUE means signal sent via queue with data attached
 *   si_int   = integer payload passed to the signal handler in user space
 *              (readable as info->si_int in the user app's signal handler)
 *
 * send_sig_info(sig, info, task):
 *   sig  = signal number
 *   info = siginfo with extra data
 *   task = target process to send signal to
 *   Returns negative on failure.
 */
static ssize_t etx_read(struct file *filp,
                         char __user *buf, size_t len, loff_t *off)
{
        struct kernel_siginfo info;   /* signal info structure               */

        pr_info("Read Function — Sending Signal\n");

        /* Fill siginfo structure */
        memset(&info, 0, sizeof(struct kernel_siginfo));
        info.si_signo = SIGETX;    /* which signal to send (44)             */
        info.si_code  = SI_QUEUE;  /* signal type: queued with data         */
        info.si_int   = 1;         /* integer data payload sent to user app */

        /* Send signal only if an app has registered via IOCTL */
        if(task != NULL) {
            pr_info("Sending signal to app (PID: %d)\n", task->pid);

            /*
             * send_sig_info() delivers the signal to the target process.
             * The target process's signal handler (sig_event_handler) is called.
             * Returns 0 on success, negative error code on failure.
             */
            if(send_sig_info(SIGETX, &info, task) < 0) {
                pr_err("Unable to send signal\n");
            }
        } else {
            pr_info("No app registered — signal not sent\n");
        }

        return 0;   /* EOF — no data returned to user                       */
}

/* Called when user writes to /dev/etx_device — not used here */
static ssize_t etx_write(struct file *filp,
                          const char __user *buf, size_t len, loff_t *off)
{
        pr_info("Write function\n");
        return len;
}

/*
 * etx_ioctl() — handles IOCTL commands from user app
 *
 * REG_CURRENT_TASK:
 *   Called by user app to register itself with the driver.
 *   get_current() returns the task_struct of the calling process.
 *   Driver stores this pointer — uses it later in etx_read() to send signal.
 *
 *   ⚠️ task pointer validity: if the app exits without closing the driver,
 *      task becomes a dangling pointer. Production code should use
 *      get_task_struct()/put_task_struct() for proper reference counting.
 */
static long etx_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
        if(cmd == REG_CURRENT_TASK) {
            pr_info("REG_CURRENT_TASK — app registered\n");
            task   = get_current();  /* store calling process's task_struct *  */
            signum = SIGETX;         /* remember which signal to use         */
        }
        return 0;
}


/* ── MODULE INIT ──────────────────────────────────────────────────────────── */
/*
 * etx_driver_init() — runs on: sudo insmod driver.ko
 * Simple 5-step char device setup. No IRQ registration needed.
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
/* Simple cleanup — no IRQ to free */
static void __exit etx_driver_exit(void)
{
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
MODULE_DESCRIPTION("A simple device driver - Signals (No IRQ)");
MODULE_VERSION("1.20");

/*
 * Complete flow summary
sudo insmod driver.ko
sudo ./test_app
  ├── installs signal handlers (SIGETX=44, SIGINT)
  ├── open(/dev/etx_device)     → etx_open()
  ├── ioctl(REG_CURRENT_TASK)   → etx_ioctl() → task = get_current()
  └── waits in busy loop...

# In another terminal:
sudo cat /dev/etx_device
  └── etx_read()
        ├── fill kernel_siginfo (si_signo=44, si_int=1)
        └── send_sig_info(SIGETX, &info, task)
              └── OS delivers signal to test_app process
                    └── sig_event_handler() called
                          └── prints "Received signal from kernel: Value = 1"

Ctrl+C in app terminal
  └── ctrl_c_handler() → done=1 → exits loop → close(fd) → etx_release()
                                                              └── task = NULL

sudo rmmod driver

