#include <linux/init.h>
#include <linux/module.h>
#include <linux/fs.h>

/* Define the major and minor numbers for the device */
#define MAJOR_NUM 300
#define MINOR_NUM 3
#define DEVICE_COUNT 1

/* Device number variable */
dev_t dev;

static int __init my_init(void){
    /* Create the device number using major and minor numbers */
    dev = MKDEV(MAJOR_NUM, MINOR_NUM);
    
    /* Register the character device region */
    if (register_chrdev_region(dev, DEVICE_COUNT, "My_device") < 0){
        printk("Device registration failed \n");
        return -1;
    }
    /* Print success message with major and minor numbers */
    printk("Device registration with Major = %d, Minor = %d\n", MAJOR(dev), MINOR(dev));
    return 0;
}

static void __exit my_exit(void){
    /* Unregister the character device region */
    unregister_chrdev_region(dev, DEVICE_COUNT);
    /* Print unregistration message */
    printk("Device unregistered.\n");
}

/* Module initialization and exit functions */
module_init(my_init);
module_exit(my_exit);

MODULE_LICENSE("gpl");