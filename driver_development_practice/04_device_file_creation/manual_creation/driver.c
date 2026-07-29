/*******************************************************************************
*  \details    Simple linux driver (Manually Creating a Device file)
*******************************************************************************/
#include<linux/kernel.h>
#include<linux/init.h>
#include<linux/module.h>
#include <linux/kdev_t.h>
#include <linux/fs.h>
 
dev_t dev = 0;

/* Module init function */
static int __init hello_world_init(void)
{
        /*Allocating Major number*/
        if((alloc_chrdev_region(&dev, 0, 1, "Embetronicx_Dev")) <0){
                pr_err("Cannot allocate major number for device\n");
                return -1;
        }
        pr_info("Major = %d Minor = %d \n",MAJOR(dev), MINOR(dev));
        
        pr_info("Kernel Module Inserted Successfully...\n");
        return 0;
}

/* Module exit function */
static void __exit hello_world_exit(void)
{
        unregister_chrdev_region(dev, 1);
        pr_info("Kernel Module Removed Successfully...\n");
}
 
module_init(hello_world_init);
module_exit(hello_world_exit);
 
MODULE_LICENSE("GPL");
MODULE_AUTHOR("EmbeTronicX <embetronicx@gmail.com>");
MODULE_DESCRIPTION("Simple linux driver (Manually Creating a Device file)");
MODULE_VERSION("1.1");

/*
Build the driver by using Makefile (sudo make)
Load the driver using sudo insmod
Check the device file using ls -l /dev/. By this time device file is not created for your driver.
Create a device file using mknod and then check using ls -l /dev/.

ubuntu@primary:/home/driver/driver$sudo mknod -m 666 /dev/etx_device c 246 0 
ubuntu@primary:/home/driver/driver$ ls -l /dev/ | grep "etx_device" 
crw-rw-rw-  1  root root  246, 0  Aug 15  13:53  etx_device

Remove the driver using sudo rmmod command.
ubuntu@primary:~/ldd/04_device_file_creation/manual_creation$ sudo rm /dev/etx_device
*/