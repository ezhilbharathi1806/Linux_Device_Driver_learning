#include <linux/module.h>
#include <linux/init.h>
#include <linux/kobject.h>
#include <linux/sysfs.h>
#include <linux/string.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("simple sysfs example");
MODULE_DESCRIPTION("A simple Linux driver demonstrating sysfs");
MODULE_VERSION("1.0");

// The variable we want to expose to user space
static int target_value = 0;

// The 'show' function is called when you read from the sysfs file
static ssize_t my_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
    return sprintf(buf, "%d\n", target_value);
}

// The 'store' function is called when you write to the sysfs file
static ssize_t my_store(struct kobject *kobj, struct kobj_attribute *attr, const char *buf, size_t count)
{
    int sscanf_result;

    sscanf_result = sscanf(buf, "%d", &target_value);
    if (sscanf_result == 1) {
        return count; // Return the number of bytes written on success
    }

    return -EINVAL; // Return error if format is invalid
}

// Define the attribute macro (creates struct kobj_attribute attr_my_value)
// 0660 gives read/write permissions to owner and group
static struct kobj_attribute value_attribute = __ATTR(my_value, 0660, my_show, my_store);

// Pointer to our custom kobject directory
static struct kobject *example_kobj;

static int __init sysfs_init(void)
{
    int error = 0;

    pr_info("Sysfs Example: Initializing driver\n");

    // 1. Create a directory named "example_sysfs" under /sys/kernel/
    example_kobj = kobject_create_and_add("example_sysfs", kernel_kobj);
    if (!example_kobj) {
        pr_err("Sysfs Example: Failed to create kobject\n");
        return -ENOMEM;
    }

    // 2. Create the attribute file "my_value" inside /sys/kernel/example_sysfs/
    error = sysfs_create_file(example_kobj, &value_attribute.attr);
    if (error) {
        pr_err("Sysfs Example: Failed to create sysfs file\n");
        kobject_put(example_kobj); // Clean up the directory on failure
        return error;
    }

    return 0;
}

static void __exit sysfs_exit(void)
{
    pr_info("Sysfs Example: Exiting driver\n");

    // Clean up sysfs entries (Order matters)
    sysfs_remove_file(example_kobj, &value_attribute.attr);
    kobject_put(example_kobj);
}

module_init(sysfs_init);
module_exit(sysfs_exit);
