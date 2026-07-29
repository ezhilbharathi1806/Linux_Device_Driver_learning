#include <linux/init.h>  // For __init, __exit
#include <linux/module.h>  // Core module headers


static int __init hello_init(void){
    printk(KERN_ALERT"Hello world\n");
    return 0;
}

static void __exit hello_exit(void){
    printk(KERN_ALERT"Goodbye\n");
}

module_init(hello_init);
module_exit(hello_exit);


MODULE_LICENSE("GPL");