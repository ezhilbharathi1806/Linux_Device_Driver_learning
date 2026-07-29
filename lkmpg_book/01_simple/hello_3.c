#include <linux/init.h>  // For __init, __exit
#include <linux/module.h>  // Core module headers
#include <linux/printk.h>   //Needed for pr_info()

// __initdata marks this variable as used only during initialization
// Its memory can be freed after the init function finishes
static int hello_data __initdata = 42;

static int __init hello_init(void){
    pr_info("Hello world, data = %d\n", hello_data);
    return 0;
}

static void __exit hello_exit(void){
    pr_info("Goodbye\n");
}

module_init(hello_init);
module_exit(hello_exit);

MODULE_LICENSE("GPL");
