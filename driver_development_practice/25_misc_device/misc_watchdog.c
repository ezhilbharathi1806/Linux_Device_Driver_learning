/* ── HEADERS ─────────────────────────────────────────────────────────────── */
#include <linux/miscdevice.h>  /* struct miscdevice, misc_register,
                                  misc_deregister, MISC_DYNAMIC_MINOR       */
#include <linux/fs.h>          /* file_operations, inode, file structs       */
#include <linux/kernel.h>      /* pr_info, pr_err                            */
#include <linux/module.h>      /* module_init, module_exit, THIS_MODULE      */
#include <linux/init.h>        /* __init, __exit                             */
#include <linux/uapi/linux/watchdog.h> /* Standard watchdog IOCTL definitions */ 

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Embedded Developer");
MODULE_DESCRIPTION("A Simple Watchdog Misc Driver Example");
MODULE_VERSION("1.0");

// Dummy hardware register state
static bool watchdog_enabled = false;

// Simulated hardware "kick" function
static void kick_hardware_watchdog(void) {
    pr_info("my_watchdog: Hardware timer reset (kicked)!\n");
}

// Open system call
static int my_watchdog_open(struct inode *inode, struct file *file) {
    if (watchdog_enabled) {
        return -EBUSY; // Only allow one process to open it at a time
    }
    watchdog_enabled = true;
    pr_info("my_watchdog: Watchdog device opened, monitoring started.\n");
    return 0;
}

// Close system call
static int my_watchdog_release(struct inode *inode, struct file *file) {
    watchdog_enabled = false;
    pr_info("my_watchdog: Watchdog device closed.\n");
    return 0;
}

// Write system call (Kicking the watchdog via echo '1' > /dev/my_watchdog)
static ssize_t my_watchdog_write(struct file *file, const char __user *buf, size_t count, loff_t *ppos) {
    if (count == 0) {
        return 0;
    }

    // Any write to the device counts as a heartbeat (kick)
    kick_hardware_watchdog();
    
    return count; 
}

// IOCTL system call for standard watchdog commands
static long my_watchdog_ioctl(struct file *file, unsigned int cmd, unsigned long arg) {
    switch (cmd) {
        case WDIOC_KEEPALIVE:
            kick_hardware_watchdog();
            return 0;
            
        case WDIOC_GETSTATUS:
            return put_user(0, (int __user *)arg); // Return dummy normal status
            
        default:
            return -ENOTTY; // Command not supported
    }
}

// File operations structure mapping standard syscalls to our functions
static const struct file_operations my_watchdog_fops = {
    .owner          = THIS_MODULE,
    .open           = my_watchdog_open,
    .release        = my_watchdog_release,
    .write          = my_watchdog_write,
    .unlocked_ioctl = my_watchdog_ioctl,
};

// The core Misc Device configuration
static struct miscdevice my_watchdog_miscdev = {
    .minor = MISC_DYNAMIC_MINOR,     // Let the kernel assign a free minor number dynamically
    .name  = "my_watchdog",          // This creates the node at /dev/my_watchdog
    .fops  = &my_watchdog_fops,      // Links our file operations
};

// Module Initialization
static int __init my_watchdog_init(void) {
    int ret;

    // Registers the driver under Major 10
    ret = misc_register(&my_watchdog_miscdev);
    if (ret) {
        pr_err("my_watchdog: Failed to register misc device\n");
        return ret;
    }

    pr_info("my_watchdog: Registered successfully with minor %d\n", my_watchdog_miscdev.minor);
    return 0;
}

// Module Cleanup
static void __exit my_watchdog_exit(void) {
    misc_deregister(&my_watchdog_miscdev);
    pr_info("my_watchdog: Unregistered successfully\n");
}

module_init(my_watchdog_init);
module_exit(my_watchdog_exit);
