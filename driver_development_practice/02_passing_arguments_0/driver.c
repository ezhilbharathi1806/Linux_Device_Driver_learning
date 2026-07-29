/*simple Linux kernel module that creates a file in the /proc filesystem. 
When a user reads that file, it returns the string "Ack!\n"*/

#include <linux/init.h>
#include <linux/module.h>
#include <linux/proc_fs.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Ezhil");
MODULE_DESCRIPTION("A sample dynamically loadable kernel module");


static struct proc_dir_entry *custom_proc_node;

static ssize_t	sample_read(struct file* fil_pointer,
			char *user_space_buffer,
			size_t count,
			loff_t* offset){
	char msg[] = "Ack!\n";
	size_t len = strlen(msg);
	int result;

	if(*offset >= len)
		return 0;
	result = copy_to_user(user_space_buffer, msg, len);
	*offset += len;
	
	printk("sample_read \n");
	return len;
}

struct proc_ops driver_proc_ops = {
	.proc_read = sample_read
};



static int sample_module_init(void){
	printk(" sample_module_init: entry \n");

	custom_proc_node = proc_create("Sample_driver",
                                    0,
                                    NULL,
                                    &driver_proc_ops);

	printk("sample_module_init: exit \n");
    return 0;
}

static void sample_module_exit(void){
	printk("sample_module_exit: entry\n");

	proc_remove(custom_proc_node);

	printk("sample_module_exit: exit\n");
}

/*
struct proc_dir_entry *proc_create(const char *name,
                                    umode_t mode,
                                    struct proc_dir_entry *parent,
                                    const struct proc_ops *proc_ops);
struct proc_dir_entry *proc_create("Sample - driver",
                                    0,
                                    NULL,
                                    NULL);
*/

module_init(sample_module_init);
module_exit(sample_module_exit);