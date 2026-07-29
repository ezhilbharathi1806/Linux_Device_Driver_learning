/*
 * procfs1.c
 * Create a proc file and write "HelloWorld!" to it when read.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/proc_fs.h>
#include <linux/uaccess.h>
#include <linux/version.h>

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 6, 0)
#define HAVE_PROC_OPS
#endif

#define procfs_name "helloworld"

static struct proc_dir_entry *our_proc_file;

static ssize_t procfile_read(struct file *file_pointer, char __user *buffer,
                             size_t buffer_length, loff_t *offset)
{
    char s[13] = "HelloWorld!\n";       // Data to be sent to user space
    int len = sizeof(s);                // Total size of the data (includes null terminator)
    ssize_t ret = len;                   // Return value (number of bytes read)

       /*
     * Check two conditions:
     * 1. If offset >= len → data already read → signal EOF
     * 2. If copy_to_user fails → unable to copy data to user space
     */
    if (*offset >= len || copy_to_user(buffer, s, len)) {
        pr_info("copy_to_user failed\n");       // Log failure message in kernel log
        ret = 0;            // Return 0 → indicates EOF or failure to user space
    } else {
        /* Log successful read operation.*/
        pr_info("procfile read %s\n", file_pointer->f_path.dentry->d_name.name); /* Prints the name of the proc file being accessed.*/
        *offset += len;     /*Update file offset so that next read returns EOF. This ensures the file behaves like a one-time readable file.*/
    }

    return ret;     // Return number of bytes read (or 0 on failure/EOF)
}

#ifdef HAVE_PROC_OPS
static const struct proc_ops proc_file_fops = {
    .proc_read = procfile_read,
};
#else
static const struct file_operations proc_file_fops = {
    .read = procfile_read,
};
#endif

static int __init procfs1_init(void)
{
    our_proc_file = proc_create(procfs_name, 0644, NULL, &proc_file_fops);
    if (NULL == our_proc_file) {
        pr_alert("Error:Could not initialize /proc/%s\n", procfs_name);
        return -ENOMEM;
    }

    pr_info("/proc/%s created\n", procfs_name);
    return 0;
}

static void __exit procfs1_exit(void)
{
    proc_remove(our_proc_file);
    pr_info("/proc/%s removed\n", procfs_name);
}

module_init(procfs1_init);
module_exit(procfs1_exit);

MODULE_LICENSE("GPL");

/*Every time the file /proc/helloworld is read, the function procfile_read is called

$ cat /proc/helloworld
HelloWorld!
*/
