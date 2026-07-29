#include <linux/init.h>
#include <linux/module.h>
#include <linux/fs.h>

/* Device number variable */
dev_t dev;

static int __init my_init(void)
{
    /* Allocate a character device region dynamically */
    if (alloc_chrdev_region(&dev, 0, 1, "my_device") < 0)
    {
        printk("Device allocation failed\n");
        return -1;
    }

    /* Print the allocated major and minor numbers */
    printk("Major=%d Minor=%d\n", MAJOR(dev), MINOR(dev));
    return 0;
}

static void __exit my_exit(void)
{
    /* Unregister the character device region */
    unregister_chrdev_region(dev, 1);
    /* Print unregistration message */
    printk("Device unregistered\n");
}

/* Module initialization and exit functions */
module_init(my_init);
module_exit(my_exit);

/* Module license */
MODULE_LICENSE("GPL");