/***************************************************************************//**
*  \file       driver.c
*  \details    Softirq example — GPIO interrupt triggers softirq bottom half
*
*  FLOW:
*    Button press → gpio_irq_handler() (Top Half)
*      → raise_softirq(EMBETRONICX_SOFT_IRQ) → marks as pending
*      → return from ISR
*    → do_softirq() runs → gpio_interrupt_softirq_handler() (Bottom Half)
*      → toggle LED
*
*  Requires custom kernel with EMBETRONICX_SOFT_IRQ in enum + EXPORT_SYMBOL
*
*  Hardware: RPi4 — GPIO 21 = LED (output), GPIO 25 = Button (input+interrupt)
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
#include <linux/interrupt.h>  /* request_irq, IRQF_TRIGGER_RISING,
                                 raise_softirq, open_softirq,
                                 struct softirq_action — core softirq header   */
#include <linux/err.h>

/* Software debounce — RPi doesn't support hardware gpio_set_debounce() */
#define EN_DEBOUNCE

#ifdef EN_DEBOUNCE
#include <linux/jiffies.h>
extern unsigned long volatile jiffies;
unsigned long old_jiffie = 0;   /* stores jiffies at last valid interrupt      */
#endif

#define GPIO_21_OUT  (21)    /* LED — output GPIO                              */
#define GPIO_25_IN   (25)    /* Button/sensor — input GPIO with interrupt      */

unsigned int led_toggle = 0;    /* current LED state: 0=OFF, 1=ON             */
unsigned int GPIO_irqNumber;    /* IRQ number from gpio_to_irq()              */


/* ── SOFTIRQ BOTTOM HALF HANDLER ─────────────────────────────────────────── */
/*
 * gpio_interrupt_softirq_handler() — softirq bottom half
 *
 * Runs AFTER the ISR returns, when do_softirq() is called.
 * Runs in ATOMIC CONTEXT (softirq context):
 *   ❌ NO sleep    ❌ NO mutex
 *   ✅ CAN use spinlock
 *   ✅ CAN run on multiple CPUs simultaneously (unlike tasklets!)
 *      → If shared data is accessed, MUST use spinlock for safety.
 *
 * @action: pointer to struct softirq_action (usually unused in simple cases)
 *
 * Here: toggles LED connected to GPIO 21.
 * In real driver: process data from device buffers, update statistics, etc.
 *
 * KEY DIFFERENCE from tasklet bottom half (Part 20/21):
 *   Tasklet:  static or dynamic init, DECLARE_TASKLET or tasklet_init()
 *   Softirq:  static enum + open_softirq() + raise_softirq()
 *   Tasklet can never run simultaneously with itself → safer but slower
 *   Softirq CAN run simultaneously on multiple CPUs → faster but needs locking
 */
static void gpio_interrupt_softirq_handler(struct softirq_action *action)
{
    led_toggle = (0x01 ^ led_toggle);              /* XOR flip: 0↔1           */
    gpio_set_value(GPIO_21_OUT, led_toggle);        /* apply to LED GPIO       */
    pr_info("Interrupt Occurred : GPIO_21_OUT : %d ",
             gpio_get_value(GPIO_21_OUT));
}


/* ── INTERRUPT HANDLER (TOP HALF) ────────────────────────────────────────── */
/*
 * gpio_irq_handler() — ISR called on GPIO 25 rising edge (button press)
 *
 * Software debounce: if less than 20 jiffies (~80ms) since last IRQ → bounce → ignore.
 *
 * raise_softirq(EMBETRONICX_SOFT_IRQ):
 *   Marks our softirq as PENDING.
 *   Does NOT call the handler immediately — returns to ISR right away.
 *   The handler runs later when kernel calls do_softirq() after ISR returns.
 *
 * KEY DIFFERENCE from tasklet (Part 20/21):
 *   Tasklet:  tasklet_schedule(&tasklet)
 *   Softirq:  raise_softirq(EMBETRONICX_SOFT_IRQ)
 *   Both are fast — but softirq is the lower-level mechanism.
 */
static irqreturn_t gpio_irq_handler(int irq, void *dev_id)
{
#ifdef EN_DEBOUNCE
    unsigned long diff = jiffies - old_jiffie;
    if (diff < 20) {
        return IRQ_HANDLED;   /* too soon — bounce detected → ignore           */
    }
    old_jiffie = jiffies;    /* update timestamp for next comparison           */
#endif

    /*
     * Raise/trigger our softirq — marks EMBETRONICX_SOFT_IRQ as pending.
     * The softirq handler runs when CPU calls do_softirq():
     *   - On return from this ISR (most common — right after return IRQ_HANDLED)
     *   - In ksoftirqd thread if too many softirqs are pending
     */
    raise_softirq(EMBETRONICX_SOFT_IRQ);

    return IRQ_HANDLED;
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

/* Manually set LED state: echo 1 or echo 0 to /dev/etx_device */
static ssize_t etx_write(struct file *filp, const char __user *buf, size_t len, loff_t *off)
{
    uint8_t rec_buf[10] = {0};
    if (copy_from_user(rec_buf, buf, len) > 0) {
        pr_err("ERROR: Not all the bytes have been copied from user\n");
    }
    pr_info("Write Function : GPIO_21 Set = %c\n", rec_buf[0]);
    if      (rec_buf[0] == '1') { gpio_set_value(GPIO_21_OUT, 1); }
    else if (rec_buf[0] == '0') { gpio_set_value(GPIO_21_OUT, 0); }
    else { pr_err("Unknown command : Please provide either 1 or 0 \n"); }
    return len;
}


/* ── MODULE INIT ──────────────────────────────────────────────────────────── */
/*
 * etx_driver_init() — runs on: sudo insmod driver.ko
 *
 * Standard char device + GPIO setup + softirq setup:
 *   Steps 1-5: char device (alloc_chrdev + cdev + class + device)
 *   Steps 6-8: GPIO 21 OUTPUT + GPIO 25 INPUT + IRQ
 *   Step 9:    open_softirq() → register softirq handler
 *
 * open_softirq(EMBETRONICX_SOFT_IRQ, gpio_interrupt_softirq_handler):
 *   Fills softirq_vec[EMBETRONICX_SOFT_IRQ].action = handler
 *   MUST be called BEFORE any raise_softirq() in the ISR.
 *   Called once in init — no per-interrupt registration needed.
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
    if (IS_ERR(dev_class = class_create("etx_class"))) {
        pr_err("Cannot create the struct class\n"); goto r_class;
    }
    if (IS_ERR(device_create(dev_class, NULL, dev, NULL, "etx_device"))) {
        pr_err("Cannot create the Device\n"); goto r_device;
    }

    /* Step 6: GPIO 21 — OUTPUT (LED) */
    if (gpio_is_valid(GPIO_21_OUT) == false) {
        pr_err("GPIO %d is not valid\n", GPIO_21_OUT); goto r_device;
    }
    if (gpio_request(GPIO_21_OUT, "GPIO_21_OUT") < 0) {
        pr_err("ERROR: GPIO %d request\n", GPIO_21_OUT); goto r_gpio_out;
    }
    gpio_direction_output(GPIO_21_OUT, 0);   /* output, initial LOW (LED OFF) */

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

    /* Step 8: Register GPIO interrupt */
    GPIO_irqNumber = gpio_to_irq(GPIO_25_IN);  /* GPIO → IRQ number */
    pr_info("GPIO_irqNumber = %d\n", GPIO_irqNumber);

    if (request_irq(GPIO_irqNumber, (void *)gpio_irq_handler,
                    IRQF_TRIGGER_RISING, "etx_device", NULL)) {
        pr_err("my_device: cannot register IRQ\n"); goto r_gpio_in;
    }

    /*
     * Step 9: Register softirq handler — MUST come BEFORE raise_softirq() calls.
     *
     * open_softirq(softirq_number, handler):
     *   softirq_number = EMBETRONICX_SOFT_IRQ (from custom kernel enum)
     *   handler        = our bottom half function
     *
     * Internally: softirq_vec[EMBETRONICX_SOFT_IRQ].action = handler
     *
     * After this: every raise_softirq(EMBETRONICX_SOFT_IRQ) in the ISR
     * will eventually call gpio_interrupt_softirq_handler().
     */
    open_softirq(EMBETRONICX_SOFT_IRQ, gpio_interrupt_softirq_handler);

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
 *
 * No explicit softirq deregistration API — open_softirq cannot be "closed".
 * Unloading module means handler function is gone — make sure no pending softirq.
 * free_irq() FIRST stops new raise_softirq() calls from ISR.
 */
static void __exit etx_driver_exit(void)
{
    free_irq(GPIO_irqNumber, NULL);   /* stop ISR → no more raise_softirq() calls */
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
MODULE_DESCRIPTION("A simple device driver - SoftIRQ (GPIO Interrupt)");
MODULE_VERSION("1.42");

/* Complete flow Summary
Custom kernel compilation required:
  interrupt.h → add EMBETRONICX_SOFT_IRQ to enum
  softirq.c   → EXPORT_SYMBOL for 3 APIs

insmod driver.ko
  ├── GPIO 21 configured as OUTPUT (LED)
  ├── GPIO 25 configured as INPUT (button)
  ├── request_irq(GPIO_25_IN, gpio_irq_handler, RISING)
  └── open_softirq(EMBETRONICX_SOFT_IRQ, gpio_interrupt_softirq_handler)

Button pressed (GPIO 25 → RISING edge)
  └── gpio_irq_handler() [TOP HALF — runs immediately]
        ├── debounce check — ignore if < 20 jiffies
        └── raise_softirq(EMBETRONICX_SOFT_IRQ)  → mark pending, return fast
              └── [ISR returns]
              └── do_softirq() called automatically
                    └── gpio_interrupt_softirq_handler() [BOTTOM HALF]
                          ├── led_toggle ^= 1
                          └── gpio_set_value(21, led_toggle) → LED toggles!

rmmod driver
  └── free_irq() → no more ISR → no more raise_softirq() → cleanup
 */
