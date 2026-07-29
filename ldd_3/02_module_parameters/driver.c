#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Example");
MODULE_DESCRIPTION("Simple Module Parameters Example");

/* Module parameters */
int my_int = 10;
char *my_string = "default";
bool my_bool = 0;

/* Declare parameters module_param(name, type, permission)*/
module_param(my_int, int, 0644);
MODULE_PARM_DESC(my_int, "An integer parameter");

module_param(my_string, charp, 0644);
MODULE_PARM_DESC(my_string, "A string parameter");

module_param(my_bool, bool, 0644);
MODULE_PARM_DESC(my_bool, "A boolean parameter");


/* Module init function */
static int __init param_init(void)
{
    printk(KERN_INFO "Module Loaded\n");
    printk(KERN_INFO "Integer value: %d\n", my_int);
    printk(KERN_INFO "String value: %s\n", my_string);
    printk(KERN_INFO "Boolean value: %d\n", my_bool);
    return 0;
}

/* Module exit function */
static void __exit param_exit(void)
{
    printk(KERN_INFO "Module Removed\n");
}

module_init(param_init);
module_exit(param_exit);


//sudo insmod driver.ko my_int=25 my_string="LDD3" my_bool=1