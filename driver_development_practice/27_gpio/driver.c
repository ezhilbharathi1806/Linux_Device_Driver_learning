/***************************************************************************//**
*  \file       driver.c
*  \details    Simple GPIO driver — controls LED on GPIO 21
*              Write "1" → LED ON, Write "0" → LED OFF
*              Read → returns current GPIO state
*
*  Hardware: Raspberry Pi 4B, LED on GPIO 21 via resistor
*  Tested:  Linux raspberrypi 5.4.51-v7l+
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
#include <linux/uaccess.h>   /* copy_to_user(), copy_from_user()              */
#include <linux/gpio.h>      /* gpio_is_valid, gpio_request, gpio_free,
                                gpio_direction_output, gpio_set_value,
                                gpio_get_value, gpio_export, gpio_unexport    */
#include <linux/err.h>


/* ── GPIO PIN DEFINITION ─────────────────────────────────────────────────── */
/*
 * GPIO_21 = Raspberry Pi GPIO pin 21 (physical pin 40 on 40-pin header).
 * LED connected: GPIO 21 → Resistor(330Ω) → LED Anode → LED Cathode → GND
 *
 * GPIO 21 is a general purpose I/O pin on RPi — not assigned to any
 * special function (SPI, I2C, UART) so safe to use for GPIO output.
 */
#define GPIO_21 (21)

/* standard char driver globals */
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

/* Called when /dev/etx_device is opened (cat, echo, etc.) */
static int etx_open(struct inode *inode, struct file *file)
{
        pr_info("Device File Opened...!!!\n");
        return 0;
}

/* Called when /dev/etx_device is closed */
static int etx_release(struct inode *inode, struct file *file)
{
        pr_info("Device File Closed...!!!\n");
        return 0;
}

/*
 * etx_read() — reads the current GPIO 21 state and sends to user
 * Triggered by: cat /dev/etx_device
 *
 * gpio_get_value(GPIO_21):
 *   Returns: 0=LOW (LED OFF), 1=HIGH (LED ON)
 *   Works for BOTH input and output configured pins.
 *
 * copy_to_user(buf, &gpio_state, len):
 *   Safely copies 1 byte (gpio_state) from kernel to user buffer.
 *   Returns: 0=success, >0=number of bytes NOT copied (error)
 *   NEVER dereference buf directly — it's a __user pointer.
 */
static ssize_t etx_read(struct file *filp, char __user *buf,
                         size_t len, loff_t *off)
{
        uint8_t gpio_state = 0;

        /* Read current state of GPIO 21 (0 or 1) */
        gpio_state = gpio_get_value(GPIO_21);

        /* Send 1 byte to user — the raw GPIO state (0 or 1 as byte value) */
        len = 1;   /* we send exactly 1 byte */
        if (copy_to_user(buf, &gpio_state, len) > 0) {
            pr_err("ERROR: Not all the bytes have been copied to user\n");
        }

        pr_info("Read function : GPIO_21 = %d \n", gpio_state);

        return 0;   /* 0=EOF — signals no more data to read */
}

/*
 * etx_write() — receives user command and sets GPIO 21 state
 * Triggered by: echo 1 > /dev/etx_device   (LED ON)
 *               echo 0 > /dev/etx_device   (LED OFF)
 *
 * copy_from_user(rec_buf, buf, len):
 *   Safely copies 'len' bytes from user space (buf) to kernel buffer (rec_buf).
 *   Returns: 0=success, >0=bytes NOT copied (error).
 *   rec_buf[0] contains the first character: '1', '0', or other.
 *
 * gpio_set_value(GPIO_21, 1):
 *   Drives GPIO 21 HIGH → LED turns ON
 *   Only valid if GPIO was configured as OUTPUT via gpio_direction_output().
 *
 * gpio_set_value(GPIO_21, 0):
 *   Drives GPIO 21 LOW → LED turns OFF
 *
 * Must return len — otherwise shell retries forever.
 */
static ssize_t etx_write(struct file *filp, const char __user *buf,
                          size_t len, loff_t *off)
{
        uint8_t rec_buf[10] = {0};   /* receive buffer for user command      */

        /* Safely copy user input into kernel buffer */
        if (copy_from_user(rec_buf, buf, len) > 0) {
            pr_err("ERROR: Not all the bytes have been copied from user\n");
        }

        pr_info("Write Function : GPIO_21 Set = %c\n", rec_buf[0]);

        if (rec_buf[0] == '1') {
            gpio_set_value(GPIO_21, 1);   /* drive HIGH → LED ON             */
        } else if (rec_buf[0] == '0') {
            gpio_set_value(GPIO_21, 0);   /* drive LOW  → LED OFF            */
        } else {
            pr_err("Unknown command : Please provide either 1 or 0 \n");
        }

        return len;   /* return bytes consumed — shell needs this            */
}


/* ── MODULE INIT ──────────────────────────────────────────────────────────── */
/*
 * etx_driver_init() — runs on: sudo insmod driver.ko
 *
 * Standard char device setup (Steps 1-5) + NEW GPIO setup steps:
 *   Step 6: gpio_is_valid()          → verify GPIO 21 is usable
 *   Step 7: gpio_request()           → claim GPIO 21 for this driver
 *   Step 8: gpio_direction_output()  → configure as output, initial=LOW
 *   Step 9: gpio_export()            → expose to /sys/class/gpio/gpio21/
 *
 * Cleanup labels (reverse order):
 *   r_gpio   → free GPIO
 *   r_device → destroy device
 *   r_class  → destroy class
 *   r_del    → delete cdev
 *   r_unreg  → unregister chrdev region
 */
static int __init etx_driver_init(void)
{
        /* Step 1: Get dynamic Major:Minor */
        if ((alloc_chrdev_region(&dev, 0, 1, "etx_Dev")) < 0) {
            pr_err("Cannot allocate major number\n");
            goto r_unreg;
        }
        pr_info("Major = %d Minor = %d \n", MAJOR(dev), MINOR(dev));

        /* Step 2: Init cdev */
        cdev_init(&etx_cdev, &fops);

        /* Step 3: Register cdev */
        if ((cdev_add(&etx_cdev, dev, 1)) < 0) {
            pr_err("Cannot add the device to the system\n");
            goto r_del;
        }

        /* Step 4: Create device class */
        if (IS_ERR(dev_class = class_create("etx_class"))) {
            pr_err("Cannot create the struct class\n");
            goto r_class;
        }

        /* Step 5: Create /dev/etx_device */
        if (IS_ERR(device_create(dev_class, NULL, dev, NULL, "etx_device"))) {
            pr_err("Cannot create the Device\n");
            goto r_device;
        }

        /*
         * Step 6: Validate GPIO 21.
         * gpio_is_valid() checks if the GPIO number is within valid range
         * AND is not temporarily unavailable on this board.
         * Returns false even for valid numbers if pin is reserved/unavailable.
         */
        if (gpio_is_valid(GPIO_21) == false) {
            pr_err("GPIO %d is not valid\n", GPIO_21);
            goto r_device;
        }

        /*
         * Step 7: Request (allocate) GPIO 21 for our driver.
         * "GPIO_21" = label string visible in /sys/kernel/debug/gpio
         *   cat /sys/kernel/debug/gpio  → shows "GPIO_21" next to gpio21
         * MUST call before gpio_direction_output, gpio_set_value, etc.
         * Returns 0=success, negative=already in use or invalid.
         */
        if (gpio_request(GPIO_21, "GPIO_21") < 0) {
            pr_err("ERROR: GPIO %d request\n", GPIO_21);
            goto r_gpio;
        }

        /*
         * Step 8: Configure GPIO 21 as OUTPUT with initial value = 0 (LOW).
         * LED starts OFF when module loads.
         * gpio_direction_output(gpio, initial_value):
         *   Sets direction to output AND drives the pin to initial_value.
         *   0=LOW(LED OFF), 1=HIGH(LED ON)
         */
        gpio_direction_output(GPIO_21, 0);

        /*
         * Step 9: Export GPIO 21 to sysfs — OPTIONAL but useful for debugging.
         * gpio_export(gpio, direction_may_change):
         *   Creates: /sys/class/gpio/gpio21/
         *     value     → read/write: current GPIO state
         *     direction → read-only (false = cannot change from userspace)
         *   direction_may_change = false → user cannot change output→input
         *
         * After this, both /dev/etx_device AND /sys/class/gpio/gpio21/value
         * can control the LED independently.
         */
        //gpio_export(GPIO_21, false);

        pr_info("Device Driver Insert...Done!!!\n");
        return 0;   /* success */

/* ── Cleanup labels — reverse order ── */
r_gpio:
        gpio_free(GPIO_21);           /* release GPIO                        */
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
 * GPIO cleanup FIRST (before device cleanup):
 *   gpio_unexport() → remove /sys/class/gpio/gpio21/
 *   gpio_free()     → release GPIO 21 back to system
 *
 * Then standard device cleanup in reverse order.
 *
 * ⚠️ gpio_free() MUST be called — otherwise GPIO stays claimed
 *    and next insmod will get "ERROR: GPIO 21 request" (already in use).
 */
static void __exit etx_driver_exit(void)
{
        //gpio_unexport(GPIO_21);          /* remove from /sys/class/gpio/      */
        gpio_free(GPIO_21);              /* release GPIO claim                */
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
MODULE_DESCRIPTION("A simple device driver - GPIO Driver");
MODULE_VERSION("1.32");

/* Complete flow summary

insmod driver.ko
  ├── Standard char device setup (/dev/etx_device created)
  ├── gpio_is_valid(21)        → check GPIO 21 is available
  ├── gpio_request(21)         → claim GPIO 21 for this driver
  ├── gpio_direction_output(21, 0) → configure as output, LED=OFF
  └── gpio_export(21, false)   → /sys/class/gpio/gpio21/ created

echo 1 > /dev/etx_device
  ├── copy_from_user → rec_buf[0]='1'
  └── gpio_set_value(21, 1) → GPIO 21 HIGH → LED ON ✅

echo 0 > /dev/etx_device
  └── gpio_set_value(21, 0) → GPIO 21 LOW → LED OFF ✅

cat /dev/etx_device
  ├── gpio_get_value(21) → 0 or 1
  └── copy_to_user → sends raw byte to user

rmmod driver
  ├── gpio_unexport(21) → /sys/class/gpio/gpio21/ removed
  └── gpio_free(21)     → GPIO 21 released
  */
