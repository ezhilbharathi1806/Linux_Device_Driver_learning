/***************************************************************************//**
*  \file       driver.c
*  \details    GPIO Driver with Interrupt — GPIO 25 (button) triggers IRQ
*              → toggles GPIO 21 (LED) on every rising edge
*
*  Hardware: Raspberry Pi 4B
*    GPIO 21 → LED (output)
*    GPIO 25 → Push button / Vibration sensor (input with interrupt)
*
*  Tested: Linux raspberrypi 5.4.51-v7l+
*******************************************************************************/

/* ── HEADERS ─────────────────────────────────────────────────────────────── */
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/kdev_t.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/delay.h>
#include <linux/uaccess.h>   /* copy_to/from_user()                           */
#include <linux/gpio.h>      /* all GPIO APIs                                 */
#include <linux/interrupt.h> /* request_irq, free_irq, IRQF_TRIGGER_RISING    */
#include <linux/err.h>

/* ── SOFTWARE DEBOUNCE ───────────────────────────────────────────────────── */
/*
 * EN_DEBOUNCE: enables software-based debounce via jiffies comparison.
 * Raspberry Pi does NOT support gpio_set_debounce() (hardware debounce).
 *
 * Without debounce: one button press can trigger ISR 5-50 times due to
 * mechanical bouncing — LED would rapidly flicker instead of cleanly toggling.
 *
 * To use hardware debounce (on boards that support it):
 *   1. Comment out #define EN_DEBOUNCE
 *   2. Uncomment the gpio_set_debounce() section in init
 */
#define EN_DEBOUNCE

#ifdef EN_DEBOUNCE
#include <linux/jiffies.h>
extern unsigned long volatile jiffies;   /* kernel tick counter — always increasing */
unsigned long old_jiffie = 0;            /* stores jiffies value of last valid IRQ  */
#endif


/* ── GPIO PIN DEFINITIONS ────────────────────────────────────────────────── */
#define GPIO_21_OUT  (21)   /* OUTPUT — LED connected here                   */
#define GPIO_25_IN   (25)   /* INPUT  — button/sensor connected here          */

/* LED state toggle variable — 0=OFF, 1=ON, flips each interrupt */
unsigned int led_toggle = 0;

/* Stores the IRQ number mapped from GPIO 25 via gpio_to_irq() */
unsigned int GPIO_irqNumber;


/* ── INTERRUPT SERVICE ROUTINE ───────────────────────────────────────────── */
/*
 * gpio_irq_handler() — called when GPIO 25 sees a RISING edge
 * (button press pulls line HIGH or sensor triggers)
 *
 * Runs in INTERRUPT CONTEXT — must be fast:
 *   ❌ NO sleep    ❌ NO mutex    ✅ CAN use local_irq_save/restore
 *
 * SOFTWARE DEBOUNCE LOGIC (when EN_DEBOUNCE is defined):
 *   jiffies    = current kernel tick count
 *   old_jiffie = jiffies at last accepted interrupt
 *   diff       = time elapsed since last valid interrupt
 *   if diff < 20 jiffies (~80ms at HZ=250) → bounce! → ignore this trigger
 *   if diff >= 20 → valid press → update old_jiffie and process
 *
 * LED TOGGLE LOGIC:
 *   led_toggle = XOR with 1 → flips between 0 and 1
 *   gpio_set_value() → drives LED HIGH or LOW
 *
 * local_irq_save(flags):
 *   Saves current interrupt state and DISABLES all IRQs momentarily.
 *   Protects the toggle + set_value from being interrupted mid-way.
 *
 * local_irq_restore(flags):
 *   Restores IRQ state back to what it was before save.
 */
static irqreturn_t gpio_irq_handler(int irq, void *dev_id)
{
        static unsigned long flags = 0;

#ifdef EN_DEBOUNCE
        /* Calculate time since last valid interrupt */
        unsigned long diff = jiffies - old_jiffie;

        /* If less than 20 jiffies (~80ms) have passed → bounce → ignore */
        if (diff < 20) {
            return IRQ_HANDLED;   /* pretend we handled it — discard bounce   */
        }

        /* Valid press — update timestamp for next comparison */
        old_jiffie = jiffies;
#endif

        /*
         * local_irq_save: disable all interrupts on this CPU, save their state.
         * Ensures toggle + gpio_set_value happens atomically — not interrupted.
         */
        local_irq_save(flags);

        /* Toggle LED: XOR with 0x01 flips bit 0: 0→1, 1→0 */
        led_toggle = (0x01 ^ led_toggle);

        /* Drive GPIO 21 to match new toggle state → LED flips */
        gpio_set_value(GPIO_21_OUT, led_toggle);

        pr_info("Interrupt Occurred : GPIO_21_OUT : %d ", gpio_get_value(GPIO_21_OUT));

        /* Re-enable interrupts — restore state saved by local_irq_save */
        local_irq_restore(flags);

        return IRQ_HANDLED;   /* interrupt was handled by us */
}


/* ── STANDARD CHAR DRIVER GLOBALS ────────────────────────────────────────── */
dev_t dev = 0;
static struct class *dev_class;
static struct cdev etx_cdev;

static int  __init etx_driver_init(void);
static void __exit etx_driver_exit(void);
static int     etx_open(struct inode *inode, struct file *file);
static int     etx_release(struct inode *inode, struct file *file);
static ssize_t etx_read(struct file *filp, char __user *buf, size_t len, loff_t *off);
static ssize_t etx_write(struct file *filp, const char *buf, size_t len, loff_t *off);


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

/*
 * etx_read() — reads current LED state (GPIO 21) and sends to user
 * Also works: cat /dev/etx_device → returns 0 or 1
 */
static ssize_t etx_read(struct file *filp, char __user *buf,
                         size_t len, loff_t *off)
{
        uint8_t gpio_state = 0;
        gpio_state = gpio_get_value(GPIO_21_OUT);   /* read output pin state  */
        len = 1;
        if (copy_to_user(buf, &gpio_state, len) > 0) {
            pr_err("ERROR: Not all the bytes have been copied to user\n");
        }
        pr_info("Read function : GPIO_21 = %d \n", gpio_state);
        return 0;
}

/*
 * etx_write() — manually set LED state (overrides interrupt toggle)
 * echo 1 > /dev/etx_device → LED ON
 * echo 0 > /dev/etx_device → LED OFF
 */
static ssize_t etx_write(struct file *filp, const char __user *buf,
                          size_t len, loff_t *off)
{
        uint8_t rec_buf[10] = {0};
        if (copy_from_user(rec_buf, buf, len) > 0) {
            pr_err("ERROR: Not all the bytes have been copied from user\n");
        }
        pr_info("Write Function : GPIO_21 Set = %c\n", rec_buf[0]);
        if (rec_buf[0] == '1') {
            gpio_set_value(GPIO_21_OUT, 1);   /* LED ON  */
        } else if (rec_buf[0] == '0') {
            gpio_set_value(GPIO_21_OUT, 0);   /* LED OFF */
        } else {
            pr_err("Unknown command : Please provide either 1 or 0 \n");
        }
        return len;
}


/* ── MODULE INIT ──────────────────────────────────────────────────────────── */
/*
 * etx_driver_init() — runs on: sudo insmod driver.ko
 *
 * Steps 1-5: Standard char device setup
 * Steps 6-8: GPIO 21 OUTPUT setup (same as Part 35)
 * Steps 9-14: GPIO 25 INPUT + IRQ setup (NEW in this tutorial)
 *
 * Cleanup labels (reverse order):
 *   r_gpio_in  → free GPIO 25 + free IRQ
 *   r_gpio_out → free GPIO 21
 *   r_device   → destroy device
 *   r_class    → destroy class
 *   r_del      → delete cdev
 *   r_unreg    → unregister chrdev region
 */
static int __init etx_driver_init(void)
{
        /* Steps 1-5: Standard char device setup */
        if ((alloc_chrdev_region(&dev, 0, 1, "etx_Dev")) < 0) {
            pr_err("Cannot allocate major number\n");
            goto r_unreg;
        }
        pr_info("Major = %d Minor = %d \n", MAJOR(dev), MINOR(dev));

        cdev_init(&etx_cdev, &fops);

        if ((cdev_add(&etx_cdev, dev, 1)) < 0) {
            pr_err("Cannot add the device to the system\n");
            goto r_del;
        }

        if (IS_ERR(dev_class = class_create("etx_class"))) {
            pr_err("Cannot create the struct class\n");
            goto r_class;
        }

        if (IS_ERR(device_create(dev_class, NULL, dev, NULL, "etx_device"))) {
            pr_err("Cannot create the Device\n");
            goto r_device;
        }

        /* Step 6-8: OUTPUT GPIO 21 — LED */
        if (gpio_is_valid(GPIO_21_OUT) == false) {
            pr_err("GPIO %d is not valid\n", GPIO_21_OUT);
            goto r_device;
        }
        if (gpio_request(GPIO_21_OUT, "GPIO_21_OUT") < 0) {
            pr_err("ERROR: GPIO %d request\n", GPIO_21_OUT);
            goto r_gpio_out;
        }
        gpio_direction_output(GPIO_21_OUT, 0);   /* output, initial=LOW (LED OFF) */

        /* Step 9: Validate INPUT GPIO 25 */
        if (gpio_is_valid(GPIO_25_IN) == false) {
            pr_err("GPIO %d is not valid\n", GPIO_25_IN);
            goto r_gpio_in;
        }

        /* Step 10: Request INPUT GPIO 25 */
        if (gpio_request(GPIO_25_IN, "GPIO_25_IN") < 0) {
            pr_err("ERROR: GPIO %d request\n", GPIO_25_IN);
            goto r_gpio_in;
        }

        /* Step 11: Configure GPIO 25 as INPUT */
        gpio_direction_input(GPIO_25_IN);

        /*
         * Step 12: Debounce setup.
         * Hardware debounce is NOT supported on Raspberry Pi.
         * If your board supports it, uncomment this and remove EN_DEBOUNCE macro.
         *
         * gpio_set_debounce(GPIO_25_IN, 200):
         *   200ms debounce — ignores transitions faster than 200ms
         */
#ifndef EN_DEBOUNCE
        if (gpio_set_debounce(GPIO_25_IN, 200) < 0) {
            pr_err("ERROR: gpio_set_debounce - %d\n", GPIO_25_IN);
        }
#endif

        /*
         * Step 13: Get the IRQ number assigned to GPIO 25.
         * gpio_to_irq() converts GPIO number → kernel IRQ number.
         * This IRQ number is then used with request_irq().
         * The actual number depends on the platform (e.g., 200).
         */
        GPIO_irqNumber = gpio_to_irq(GPIO_25_IN);
        pr_info("GPIO_irqNumber = %d\n", GPIO_irqNumber);

        /*
         * Step 14: Register the interrupt handler for GPIO 25 IRQ.
         *
         * request_irq(irq, handler, flags, name, dev_id):
         *   GPIO_irqNumber      = IRQ number from gpio_to_irq()
         *   gpio_irq_handler    = our ISR function
         *   IRQF_TRIGGER_RISING = fire ISR on LOW→HIGH edge (button press)
         *   "etx_device"        = IRQ name (visible in /proc/interrupts)
         *   NULL                = dev_id (NULL ok since not IRQF_SHARED)
         *
         * Other trigger options:
         *   IRQF_TRIGGER_FALLING → fire on HIGH→LOW (button release)
         *   IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING → both edges
         *   IRQF_TRIGGER_HIGH / IRQF_TRIGGER_LOW → level triggered
         */
        if (request_irq(GPIO_irqNumber,
                        (void *)gpio_irq_handler,
                        IRQF_TRIGGER_RISING,
                        "etx_device",
                        NULL)) {
            pr_err("my_device: cannot register IRQ\n");
            goto r_gpio_in;
        }

        pr_info("Device Driver Insert...Done!!!\n");
        return 0;

/* Cleanup labels — reverse order of init */
r_gpio_in:
        gpio_free(GPIO_25_IN);    /* free input GPIO 25                        */
r_gpio_out:
        gpio_free(GPIO_21_OUT);   /* free output GPIO 21                       */
r_device:
        device_destroy(dev_class, dev);
r_class:
        class_destroy(dev_class);
r_del:
        cdev_del(&etx_cdev);
r_unreg:
        unregister_chrdev_region(dev, 1);
        return -1;
}


/* ── MODULE EXIT ──────────────────────────────────────────────────────────── */
/*
 * etx_driver_exit() — runs on: sudo rmmod driver
 *
 * MUST free IRQ FIRST — prevents ISR from firing after module unloads.
 * Then free GPIOs, then device cleanup.
 *
 * free_irq(GPIO_irqNumber, NULL):
 *   Unregisters the ISR → GPIO 25 interrupts no longer handled.
 *   dev_id (NULL) must match what was passed to request_irq().
 */
static void __exit etx_driver_exit(void)
{
        free_irq(GPIO_irqNumber, NULL);  /* unregister ISR FIRST — critical!  */
        gpio_free(GPIO_25_IN);           /* release input GPIO                 */
        gpio_free(GPIO_21_OUT);          /* release output GPIO                */
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
MODULE_DESCRIPTION("A simple device driver - GPIO Driver (GPIO Interrupt)");
MODULE_VERSION("1.33");

/* Complete flow summary
 *
insmod driver.ko
  ├── GPIO 21 → OUTPUT, initial LOW (LED OFF)
  ├── GPIO 25 → INPUT (button/sensor)
  ├── GPIO_irqNumber = gpio_to_irq(25) → get IRQ number
  └── request_irq(irqNumber, gpio_irq_handler, RISING) → ISR registered

Button pressed (GPIO 25 goes LOW→HIGH = rising edge)
  └── kernel fires IRQ → gpio_irq_handler() called
        ├── [debounce check: if < 20 jiffies → return, it's a bounce]
        ├── local_irq_save() → protect critical section
        ├── led_toggle = 0x01 ^ led_toggle  → flip 0↔1
        ├── gpio_set_value(21, led_toggle)  → LED toggles
        ├── pr_info("Interrupt Occurred : GPIO_21_OUT : X")
        └── local_irq_restore() → done

rmmod driver
  ├── free_irq()    → ISR unregistered (no more GPIO 25 interrupts)
  ├── gpio_free(25) → release input GPIO
  └── gpio_free(21) → release output GPIO
  */
