/***************************************************************************//**
*  \file       driver.c
*  \details    Threaded IRQ driver — simplest bottom half mechanism
*
*  FLOW:
*    Button press → gpio_irq_handler() (Top Half, atomic)
*      → returns IRQ_WAKE_THREAD
*      → kernel wakes dedicated thread
*      → gpio_interrupt_thread_fn() (Bottom Half, process context)
*      → toggles LED
*
*  ONLY NEW API vs regular GPIO interrupt (Part 36):
*    request_irq()           → replaced by request_threaded_irq()
*    No manual workqueue/tasklet setup needed — kernel handles all of it!
*
*  Hardware: RPi4 — GPIO 21=LED (output), GPIO 25=Button (input+interrupt)
*  Tested:   Linux raspberrypi 5.10.27-v7l-embetronicx-custom+
*******************************************************************************/

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/kdev_t.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/delay.h>
#include <linux/uaccess.h>    /* copy_to/from_user()                          */
#include <linux/gpio.h>       /* GPIO APIs                                     */
#include <linux/interrupt.h>  /* request_threaded_irq, free_irq,
                                 IRQ_HANDLED, IRQ_WAKE_THREAD — core header   */
#include <linux/err.h>

/* Software debounce — RPi doesn't support hardware gpio_set_debounce() */
#define EN_DEBOUNCE

#ifdef EN_DEBOUNCE
#include <linux/jiffies.h>
extern unsigned long volatile jiffies;
unsigned long old_jiffie = 0;
#endif

#define GPIO_21_OUT  (21)    /* LED — output                                  */
#define GPIO_25_IN   (25)    /* Button/Sensor — input with interrupt           */

unsigned int led_toggle    = 0;   /* current LED state                        */
unsigned int GPIO_irqNumber;      /* IRQ number from gpio_to_irq()            */


/* ── TOP HALF (ISR) ──────────────────────────────────────────────────────── */
/*
 * gpio_irq_handler() — called IMMEDIATELY when GPIO 25 detects rising edge
 *
 * This is the TOP HALF. Runs in ATOMIC CONTEXT:
 *   ❌ NO sleep    ❌ NO mutex    ❌ NO heavy work
 *   ✅ Must be FAST (< 100μs ideally)
 *   ✅ Just acknowledge hardware and wake the thread
 *
 * Software debounce: ignore interrupts that come too quickly (< 20 jiffies apart).
 *
 * RETURN VALUE is KEY:
 *   IRQ_HANDLED    → all done, DON'T call thread_fn
 *   IRQ_WAKE_THREAD → done with top half, PLEASE call gpio_interrupt_thread_fn()
 *
 * Here: we just debounce and return IRQ_WAKE_THREAD.
 * All real work (LED toggle) is done in the thread (bottom half).
 *
 * Compare to other bottom halves:
 *   Workqueue:  call schedule_work()        in ISR
 *   Tasklet:    call tasklet_schedule()     in ISR
 *   Softirq:    call raise_softirq()        in ISR
 *   Threaded:   return IRQ_WAKE_THREAD      from ISR ← cleanest!
 */
static irqreturn_t gpio_irq_handler(int irq, void *dev_id)
{
#ifdef EN_DEBOUNCE
    unsigned long diff = jiffies - old_jiffie;
    if (diff < 20) {
        return IRQ_HANDLED;   /* bounce detected → ignore, DON'T wake thread  */
    }
    old_jiffie = jiffies;
#endif

    pr_info("Interrupt(IRQ Handler)\n");

    /*
     * Return IRQ_WAKE_THREAD → tells kernel: "wake the thread to run thread_fn"
     * After returning this, the dedicated kernel thread for this IRQ is scheduled.
     * The ISR returns immediately — interrupts remain enabled.
     * gpio_interrupt_thread_fn() will run when the thread gets CPU time.
     */
    return IRQ_WAKE_THREAD;
}


/* ── BOTTOM HALF (Thread Function) ──────────────────────────────────────── */
/*
 * gpio_interrupt_thread_fn() — bottom half running in dedicated kernel thread
 *
 * Called by the kernel after gpio_irq_handler() returns IRQ_WAKE_THREAD.
 * Runs in PROCESS CONTEXT (a kernel thread created automatically):
 *   ✅ CAN sleep       ✅ CAN use mutex      ✅ CAN do heavy processing
 *
 * This is the SIMPLEST bottom half — no setup code needed!
 * Compare to workqueue (need DECLARE_WORK + schedule_work + create_workqueue).
 * Here: just write this function and pass it to request_threaded_irq().
 *
 * Must return IRQ_HANDLED — tells kernel this execution of thread_fn is done.
 * After returning, thread goes back to sleep until next IRQ_WAKE_THREAD.
 *
 * @irq    : IRQ number (same as GPIO_irqNumber)
 * @dev_id : device id passed to request_threaded_irq (NULL in our case)
 */
static irqreturn_t gpio_interrupt_thread_fn(int irq, void *dev_id)
{
    led_toggle = (0x01 ^ led_toggle);              /* XOR toggle: 0↔1         */
    gpio_set_value(GPIO_21_OUT, led_toggle);        /* drive LED HIGH or LOW   */
    pr_info("Interrupt(Threaded Handler) : GPIO_21_OUT : %d ",
             gpio_get_value(GPIO_21_OUT));

    return IRQ_HANDLED;   /* done — thread sleeps until next IRQ_WAKE_THREAD   */
}


/* ── STANDARD CHAR DRIVER GLOBALS + FILE OPERATIONS ─────────────────────── */
dev_t dev = 0;
static struct class *dev_class;
static struct cdev etx_cdev;

static int  __init etx_driver_init(void);
static void __exit etx_driver_exit(void);
static int     etx_open(struct inode *inode, struct file *file);
static int     etx_release(struct inode *inode, struct file *file);
static ssize_t etx_read(struct file *filp, char __user *buf, size_t len, loff_t *off);
static ssize_t etx_write(struct file *filp, const char *buf, size_t len, loff_t *off);

static struct file_operations fops =
{
    .owner   = THIS_MODULE,
    .read    = etx_read,
    .write   = etx_write,
    .open    = etx_open,
    .release = etx_release,
};

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

/* Read current LED state */
static ssize_t etx_read(struct file *filp, char __user *buf, size_t len, loff_t *off)
{
    uint8_t gpio_state = gpio_get_value(GPIO_21_OUT);
    len = 1;
    if (copy_to_user(buf, &gpio_state, len) > 0) {
        pr_err("ERROR: Not all the bytes have been copied to user\n");
    }
    pr_info("Read function : GPIO_21 = %d \n", gpio_state);
    return 0;
}

/* Manually set LED: echo 1/0 > /dev/etx_device */
static ssize_t etx_write(struct file *filp, const char __user *buf, size_t len, loff_t *off)
{
    uint8_t rec_buf[10] = {0};
    if (copy_from_user(rec_buf, buf, len) > 0) {
        pr_err("ERROR: Not all the bytes have been copied from user\n");
    }
    pr_info("Write Function : GPIO_21 Set = %c\n", rec_buf[0]);
    if      (rec_buf[0] == '1') { gpio_set_value(GPIO_21_OUT, 1); }
    else if (rec_buf[0] == '0') { gpio_set_value(GPIO_21_OUT, 0); }
    else    { pr_err("Unknown command : Please provide either 1 or 0 \n"); }
    return len;
}


/* ── MODULE INIT ──────────────────────────────────────────────────────────── */
/*
 * etx_driver_init() — runs on: sudo insmod driver.ko
 *
 * Same as Part 36 GPIO interrupt driver, with ONE change:
 *   Part 36: request_irq(irq, handler, flags, name, dev_id)
 *   Part 46: request_threaded_irq(irq, handler, thread_fn, flags, name, dev_id)
 *             ↑ just adds thread_fn parameter — everything else identical!
 *
 * request_threaded_irq() creates a kernel thread automatically.
 * That thread is scheduled whenever handler() returns IRQ_WAKE_THREAD.
 * The thread is named "irq/N-etx_device" visible in ps -aef.
 */
static int __init etx_driver_init(void)
{
    /* Steps 1-5: Standard char device setup */
    if ((alloc_chrdev_region(&dev, 0, 1, "etx_Dev")) < 0) {
        pr_err("Cannot allocate major number\n"); goto r_unreg;
    }
    pr_info("Major = %d Minor = %d \n", MAJOR(dev), MINOR(dev));

    cdev_init(&etx_cdev, &fops);
    if ((cdev_add(&etx_cdev, dev, 1)) < 0) {
        pr_err("Cannot add the device to the system\n"); goto r_del;
    }
    if (IS_ERR(dev_class = class_create(THIS_MODULE, "etx_class"))) {
        pr_err("Cannot create the struct class\n"); goto r_class;
    }
    if (IS_ERR(device_create(dev_class, NULL, dev, NULL, "etx_device"))) {
        pr_err("Cannot create the Device\n"); goto r_device;
    }

    /* Step 6: GPIO 21 — OUTPUT (LED), initial = LOW */
    if (gpio_is_valid(GPIO_21_OUT) == false) {
        pr_err("GPIO %d is not valid\n", GPIO_21_OUT); goto r_device;
    }
    if (gpio_request(GPIO_21_OUT, "GPIO_21_OUT") < 0) {
        pr_err("ERROR: GPIO %d request\n", GPIO_21_OUT); goto r_gpio_out;
    }
    gpio_direction_output(GPIO_21_OUT, 0);

    /* Step 7: GPIO 25 — INPUT (Button/Sensor) */
    if (gpio_is_valid(GPIO_25_IN) == false) {
        pr_err("GPIO %d is not valid\n", GPIO_25_IN); goto r_gpio_in;
    }
    if (gpio_request(GPIO_25_IN, "GPIO_25_IN") < 0) {
        pr_err("ERROR: GPIO %d request\n", GPIO_25_IN); goto r_gpio_in;
    }
    gpio_direction_input(GPIO_25_IN);

#ifndef EN_DEBOUNCE
    if (gpio_set_debounce(GPIO_25_IN, 200) < 0) {
        pr_err("ERROR: gpio_set_debounce - %d\n", GPIO_25_IN);
    }
#endif

    /* Step 8: Get IRQ number from GPIO 25 */
    GPIO_irqNumber = gpio_to_irq(GPIO_25_IN);
    pr_info("GPIO_irqNumber = %d\n", GPIO_irqNumber);

    /*
     * Step 9: Register THREADED IRQ — KEY DIFFERENCE from Part 36.
     *
     * request_threaded_irq(irq, handler, thread_fn, flags, name, dev_id):
     *   GPIO_irqNumber          = IRQ number from gpio_to_irq()
     *   gpio_irq_handler        = TOP HALF: fast ISR, must return IRQ_WAKE_THREAD
     *   gpio_interrupt_thread_fn= BOTTOM HALF: slow work, runs in kernel thread
     *   IRQF_TRIGGER_RISING     = fire on LOW→HIGH edge
     *   "etx_device"            = device name in /proc/interrupts
     *   NULL                    = dev_id (NULL = not shared)
     *
     * Kernel automatically:
     *   Creates a thread named "irq/N-etx_device"
     *   Schedules thread_fn when handler returns IRQ_WAKE_THREAD
     *   No manual workqueue, no tasklet_schedule, no raise_softirq!
     */
    if (request_threaded_irq(GPIO_irqNumber,
                             (void *)gpio_irq_handler,    /* top half    */
                             gpio_interrupt_thread_fn,    /* bottom half */
                             IRQF_TRIGGER_RISING,
                             "etx_device",
                             NULL)) {
        pr_err("my_device: cannot register IRQ\n"); goto r_gpio_in;
    }

    pr_info("Device Driver Insert...Done!!!\n");
    return 0;

r_gpio_in:  gpio_free(GPIO_25_IN);
r_gpio_out: gpio_free(GPIO_21_OUT);
r_device:   device_destroy(dev_class, dev);
r_class:    class_destroy(dev_class);
r_del:      cdev_del(&etx_cdev);
r_unreg:    unregister_chrdev_region(dev, 1);
    return -1;
}


/* ── MODULE EXIT ──────────────────────────────────────────────────────────── */
/*
 * etx_driver_exit() — runs on: sudo rmmod driver
 * free_irq() FIRST — stops ISR + kills the associated kernel thread.
 * Then GPIO and device cleanup.
 */
static void __exit etx_driver_exit(void)
{
    free_irq(GPIO_irqNumber, NULL);  /* stops top+bottom half, kills thread   */
    gpio_free(GPIO_25_IN);
    gpio_free(GPIO_21_OUT);
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
MODULE_DESCRIPTION("A simple device driver - Threaded IRQ (GPIO Interrupt)");
MODULE_VERSION("1.43");

/* Complete flow summary
insmod driver.ko
  ├── GPIO 21 = OUTPUT (LED), GPIO 25 = INPUT (button)
  └── request_threaded_irq()
        → IRQ registered
        → kernel creates [irq/67-etx_device] kernel thread (sleeping)

Button pressed (GPIO 25 RISING)
  └── gpio_irq_handler() [TOP HALF — atomic, fast]
        ├── debounce check
        ├── pr_info("Interrupt(IRQ Handler)")
        └── return IRQ_WAKE_THREAD → kernel wakes [irq/67-etx_device] thread
              └── gpio_interrupt_thread_fn() [BOTTOM HALF — process context]
                    ├── led_toggle ^= 1
                    ├── gpio_set_value(21, led_toggle) → LED toggles!
                    ├── pr_info("Interrupt(Threaded Handler) : GPIO_21_OUT : 1")
                    └── return IRQ_HANDLED → thread goes back to sleep

rmmod driver
  └── free_irq() → ISR unregistered + [irq/67-etx_device] thread killed
 */
