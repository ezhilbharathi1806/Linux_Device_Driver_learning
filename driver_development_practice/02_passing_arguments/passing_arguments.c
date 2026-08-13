/*
command to load driver
> sudo insmod passing_arguments.ko valueETX=14 nameETX="EmbeTronicX" arr_valueETX=100,102,104,106 cb_valueETX=25

> echo 50 | sudo tee /sys/module/passing_arguments/parameters/cb_valueETX
        Call back function called...
        New value of cb_valueETX = 50
> cat /sys/module/passing_arguments/parameters/cb_valueETX
        50
*/
#include<linux/kernel.h>
#include<linux/init.h>
#include<linux/module.h>
#include<linux/moduleparam.h>
 
int valueETX, arr_valueETX[4];
char *nameETX;
int cb_valueETX = 0;
 
module_param(valueETX, int, S_IRUSR|S_IWUSR);                      //integer value
module_param(nameETX, charp, S_IRUSR|S_IWUSR);                     //String
module_param_array(arr_valueETX, int, NULL, S_IRUSR|S_IWUSR);      //Array of integers
 
/*----------------------Module_param_cb()--------------------------------*/
int notify_param(const char *val, const struct kernel_param *kp)
{
        int res = param_set_int(val, kp); // Use helper for write variable
        if(res==0) {
                pr_info("Call back function called...\n");
                pr_info("New value of cb_valueETX = %d\n", cb_valueETX);
                return 0;
        }
        return -1;
}
 
const struct kernel_param_ops my_param_ops = 
{
        .set = &notify_param, // Use our setter | When someone writes a new value to this parameter, call notify_param()
        .get = &param_get_int, //standard getter| When someone reads the parameter, use the kernel's standard integer getter
};
 
module_param_cb(cb_valueETX,    //parameter name
        &my_param_ops,          // what to do when SET/GET happens
        &cb_valueETX,           //variable/storage location
        S_IRUGO|S_IWUSR );
/*-------------------------------------------------------------------------*/

/*
** Module init function
*/
static int __init hello_world_init(void)
{
        int i;
        pr_info("ValueETX = %d  \n", valueETX);
        pr_info("cb_valueETX = %d  \n", cb_valueETX);
        pr_info("NameETX = %s \n", nameETX);
        for (i = 0; i < (sizeof arr_valueETX / sizeof (int)); i++) {
                pr_info("Arr_value[%d] = %d\n", i, arr_valueETX[i]);
        }
        pr_info("Kernel Module Inserted Successfully...\n");
    return 0;
}

/*
** Module Exit function
*/
static void __exit hello_world_exit(void)
{
    pr_info("Kernel Module Removed Successfully...\n");
}
 
module_init(hello_world_init);
module_exit(hello_world_exit);
 
MODULE_LICENSE("GPL");
MODULE_AUTHOR("02_passing arguments");
MODULE_DESCRIPTION("A simple passing arguments driver");
MODULE_VERSION("1.0");