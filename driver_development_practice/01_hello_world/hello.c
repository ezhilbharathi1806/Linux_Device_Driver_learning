#include<linux/init.h>
#include<linux/module.h>
 
MODULE_LICENSE("GPL");
MODULE_AUTHOR("sample");
MODULE_DESCRIPTION("A simple hello world driver");
MODULE_VERSION("2:1.0");

/* Module Init function  */
static int __init hello_world_init(void)
{
    printk("Hello World\n");
    pr_info("Welcome to linux\n");
    pr_info("This is the Simple Module\n");
    pr_info("Kernel Module Inserted Successfully...\n");
    return 0;
}

/* Module Exit function */
static void __exit hello_world_exit(void)
{
    pr_info("Kernel Module Removed Successfully...\n");
}

module_init(hello_world_init);
module_exit(hello_world_exit);