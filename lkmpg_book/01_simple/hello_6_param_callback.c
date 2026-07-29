#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>

static int my_value = 0;

/* 🔹 Custom SET function */
static int set_my_value(const char *val, const struct kernel_param *kp)
{
    int temp, ret;

    // Convert input string to integer
    ret = kstrtoint(val, 10, &temp);
    if (ret < 0)
        return ret;

    // Validate range
    if (temp < 0 || temp > 100) {
        pr_err("Value must be between 0 and 100\n");
        return -EINVAL;
    }

    // Assign value if valid
    my_value = temp;

    // Side effect: log change
    pr_info("my_value updated to %d\n", my_value);

    return 0;
}

/* 🔹 Custom GET function */
static int get_my_value(char *buffer, const struct kernel_param *kp)
{
    // Format output
    return sprintf(buffer, "Current value: %d\n", my_value);
}

/* 🔹 Define operations */
static const struct kernel_param_ops my_param_ops = {
    .set = set_my_value,
    .get = get_my_value,
};

/* 🔹 Register parameter */
module_param_cb(my_value, &my_param_ops, &my_value, 0644);

/* 🔹 Init & Exit */
static int __init my_init(void){
    pr_info("Module loaded\n");
    return 0;
}

static void __exit my_exit(void){
    pr_info("Module unloaded\n");
}

module_init(my_init);
module_exit(my_exit);

MODULE_LICENSE("GPL");

/*
 * How to test it
  load module -	sudo insmod hello_6_param_callback.ko
  write value - echo 50 > /sys/module/my_module/parameters/my_value
  read value -	cat /sys/module/my_module/parameters/my_value

 */
