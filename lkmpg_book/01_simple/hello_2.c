#include <linux/init.h>  // For __init, __exit
#include <linux/module.h>  // Core module headers
#include <linux/printk.h>   //Needed for pr_info()

// This function is called when the module is loaded into the kernel
// The __init macro tells the kernel this function is only used during initialization
static int __init hello_init(void){
    pr_info("Hello world, data\n");
    return 0;
}

// This function is called when the module is removed from the kernel
// The __exit macro tells the kernel this function is used during cleanup
static void __exit hello_exit(void){
    pr_info("Goodbye\n");
}

module_init(hello_init);    //register the initialization function with kernel
module_exit(hello_exit);    //register the cleanup function with kernel

MODULE_LICENSE("GPL");