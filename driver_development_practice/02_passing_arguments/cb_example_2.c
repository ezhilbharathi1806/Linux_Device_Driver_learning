#include <linux/init.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/kernel.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Ezhil");
MODULE_DESCRIPTION("Simple module_param_cb Example");

static const char *device_name = "Param_Callback_Example";  //driver name for logging

static int cb_value = 0;    // Variable to hold the parameter value

/* 1. Custom 'set' function triggered when the user writes to sysfs (/sys/module/<module_name>/parameters/<param_name>)*/
static int notify_param_set(const char *val, const struct kernel_param *kp){
    int res, temp_val;

    // Convert string input from sysfs to integer
    res = kstrtoint(val, 10, &temp_val);
    if (res < 0) {
        pr_err("%s: Invalid input string\n", device_name);
        return res;
    }

    // Example of input validation (reject values greater than 100)
    if (temp_val > 100) {
        pr_warn("%s: Value %d is too high! Rejecting.\n", device_name, temp_val);
        return -EINVAL;
    }
    pr_info("%s: Parameter changing from %d to %d\n", device_name, cb_value, temp_val);
    
    // Update the actual variable using the standard kernel helper
    return param_set_int(val, kp);
}

/* 2. Custom 'get' function triggered when the user reads from sysfs (/sys/module/<module_name>/parameters/<param_name>) */
static int notify_param_get(char *buffer, const struct kernel_param *kp){
    pr_info("%s: Getting parameter value\n", device_name);
    return param_get_int(buffer, kp);
}

/* 3. Bind the custom operations */
static const struct kernel_param_ops my_param_ops = {
    .set = notify_param_set,        // echo <value> > /sys/module/<your_module_name>/parameters/cb_value
    .get = notify_param_get,      // cat /sys/module/<your_module_name>/parameters/cb_value
};

/* 4. Register the callback macro */
module_param_cb(cb_value_param, &my_param_ops, &cb_value, 0644);      // Arguments: (sysfs_name, kernel_param_ops, pointer_to_variable, permissions)
MODULE_PARM_DESC(cb_value_param, "An integer parameter with a custom modify callback");

static int __init cb_example_init(void){
    pr_info("%s: Module Loaded. Initial value = %d\n", device_name, cb_value);
    return 0;
}

static void __exit cb_example_exit(void){
    pr_info("%s: Module Unloaded.\n", device_name);
}

module_init(cb_example_init);
module_exit(cb_example_exit);
