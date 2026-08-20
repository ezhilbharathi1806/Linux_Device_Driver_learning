/* ── HEADERS ─────────────────────────────────────────────────────────────── */
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/kdev_t.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/uaccess.h>
#include <linux/delay.h>         /* msleep()                                  */
#include <linux/semaphore.h>    /* Required header for kernel semaphores */

#define DEVICE_NAME "sem_device"

/* ── GLOBAL VARIABLES ────────────────────────────────────────────────────── */
static dev_t dev_num;
static struct cdev my_cdev;
static struct class *my_class;

/* Declare the semaphore structure */
static struct semaphore my_sem;

/* ── FUNCTION PROTOTYPES ─────────────────────────────────────────────────── */
static int __init my_driver_init(void);
static void __exit my_driver_exit(void);
static int my_open(struct inode *inode, struct file *file);
static int my_release(struct inode *inode, struct file *file);
static ssize_t my_read(struct file *file, char __user *buf, size_t len, loff_t *off);


/* ── DEVICE FILE HANDLER FUNCTIONS ───────────────────────────────────────── */
/* Driver open operation */
static int my_open(struct inode *inode, struct file *file) {
    pr_info("Device opened\n");
    return 0;
}

/* Driver release operation */
static int my_release(struct inode *inode, struct file *file) {
    pr_info("Device closed\n");
    return 0;
}

/* Driver read operation protected by a semaphore 
 * cat /dev/sem_device
 */
static ssize_t my_read(struct file *file, char __user *buf, size_t len, loff_t *off) {
    pr_info("Attempting to acquire semaphore...\n");

    /* 
     * down_interruptible tries to acquire the lock. 
     * If the lock is unavailable, the thread sleeps. 
     * It returns non-zero if interrupted by a signal (e.g., Ctrl+C).
     */
    if (down_interruptible(&my_sem)) {
        pr_alert("Failed to acquire semaphore: interrupted!\n");
        return -ERESTARTSYS; /* Tells the VFS layer to restart the system call */
    }

    /* --- CRITICAL SECTION START --- */
    pr_info("Semaphore ACQUIRED. Inside critical section.\n");
    
    /* Simulate a long hardware or data processing delay (sleep for 5 seconds) */
    ssleep(5); 

    pr_info("Leaving critical section...\n");
    /* --- CRITICAL SECTION END --- */

    /* Release the semaphore so other threads can acquire it */
    up(&my_sem);
    
    return 0; /* EOF */
}

/* ── FILE OPERATIONS TABLE ───────────────────────────────────────────────── */
/* Map file operations to the driver functions */
static struct file_operations fops = {
    .owner   = THIS_MODULE,
    .open    = my_open,
    .release = my_release,
    .read    = my_read,
};

/* ── MODULE INIT ──────────────────────────────────────────────────────────── */
/* Module Initialization */
static int __init my_driver_init(void) {
    int ret;

    /* 1. Allocate major/minor numbers dynamically */
    ret = alloc_chrdev_region(&dev_num, 0, 1, DEVICE_NAME);
    if (ret < 0) return ret;

    /* 2. Initialize the character device structure */
    cdev_init(&my_cdev, &fops);
    ret = cdev_add(&my_cdev, dev_num, 1);
    if (ret < 0) goto unregister_region;

    /* 3. Create the device class for automatic /dev node creation */
    my_class = class_create("sem_class");
    if (IS_ERR(my_class)) {
        ret = PTR_ERR(my_class);
        goto delete_cdev;
    }

    /* 4. Create the device node */
    if (IS_ERR(device_create(my_class, NULL, dev_num, NULL, DEVICE_NAME))) {
        ret = -1;
        goto destroy_class;
    }

    /* 
     * 5. Initialize the semaphore dynamically.
     * Arguments: (struct semaphore *sem, int val)
     * Setting the value to '1' makes it a binary semaphore (mutex mode).
     */
    sema_init(&my_sem, 1);

    pr_info("Semaphore Driver Loaded successfully.\n");
    return 0;

destroy_class:
    class_destroy(my_class);
delete_cdev:
    cdev_del(&my_cdev);
unregister_region:
    unregister_chrdev_region(dev_num, 1);
    return ret;
}

/* ── MODULE EXIT ──────────────────────────────────────────────────────────── */
/* Module Cleanup */
static void __exit my_driver_exit(void) {
    device_destroy(my_class, dev_num);
    class_destroy(my_class);
    cdev_del(&my_cdev);
    unregister_chrdev_region(dev_num, 1);
    pr_info("Semaphore Driver Unloaded.\n");
}

module_init(my_driver_init);
module_exit(my_driver_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Dev");
MODULE_DESCRIPTION("Linux Device Driver Semaphore Example");
