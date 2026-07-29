/* Simplest kernel module*/

#include <linux/module.h>  // Core header for loading LKMs into the kernel
#include <linux/printk.h>   //Needed for pr_info()

int init_module(void){
    pr_info("Hello world\n");
    return 0;
}

void cleanup_module(void){
    pr_info("Goodbye\n");
}

MODULE_LICENSE("GPL");