/* Licensing and module documentation*/
#include <linux/init.h>
#include <linux/modules.h>  //Needed by all modules
#include <linux/printk.h>   //Needed for pr_info()

MODULE_LICENSE("GPL");
MODULE_AUTHOR("LKMPG");
MODULE_DESCRIPTION("A sample driver");

static int __init hello_init(void){
    pr_info("Hello world, data\n");
    return 0;
}

static void __exit hello_exit(void){
    pr_info("Goodbye\n");
}

module_init(hello_init);
module_exit(hello_exit);