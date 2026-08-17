#include <linux/init.h>
#include <linux/module.h>
#include <linux/proc_fs.h>
#include <linux/string.h>
#include <linux/uaccess.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Ezhil");
MODULE_DESCRIPTION("A sample dynamically loadable kernel module");

static struct proc_dir_entry *custom_proc_node;

/*
 * Called when the user reads:
 * cat  /proc/sample_driver
 */
static ssize_t sample_read(struct file *file,
                           char __user *user_space_buffer,
                           size_t count,
                           loff_t *offset)
{
    const char msg[] = "Ack!\n";
    size_t len = strlen(msg);
    size_t bytes_to_copy;

    if (*offset >= len)
        return 0;

    bytes_to_copy = min(count, len - (size_t)*offset);

    if (copy_to_user(user_space_buffer,
                     msg + *offset,
                     bytes_to_copy))
        return -EFAULT;

    *offset += bytes_to_copy;

    pr_info("sample_read\n");

    return bytes_to_copy;
}

static ssize_t sample_write(struct file *file,
							const char __user *user_space_buffer,
							size_t count,
							loff_t *offset)
{
	pr_info("sample_write function: entry\n");
	pr_info("sample_write function: exit\n");
	return count;
}

/* procfs operation sturcture */
static const struct proc_ops driver_proc_ops = {
    .proc_read = sample_read,
	.proc_write = sample_write,
};

static int __init sample_module_init(void)
{
    pr_info("sample_module_init: entry\n");

    custom_proc_node = proc_create("sample_driver",
                                   0444,
                                   NULL,
                                   &driver_proc_ops);

    if (!custom_proc_node) {
        pr_err("Failed to create /proc/sample_driver\n");
        return -ENOMEM;
    }

    pr_info("sample_module_init: exit\n");

    return 0;
}

static void __exit sample_module_exit(void)
{
    pr_info("sample_module_exit: entry\n");

    if (custom_proc_node)
        proc_remove(custom_proc_node);

    pr_info("sample_module_exit: exit\n");
}

module_init(sample_module_init);
module_exit(sample_module_exit);