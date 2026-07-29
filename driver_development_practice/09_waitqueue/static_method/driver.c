/***************************************************************************//**
*  \details    Simple linux driver (Waitqueue Static method)

sudo insmod driver.ko
Send the event by reading the driver using sudo cat /dev/etx_device
Now check the dmesg
*******************************************************************************/
/* ── HEADERS ─────────────────────────────────────────────────────────────── */
#include <linux/kernel.h>    /* pr_info, pr_err                                */
#include <linux/init.h>      /* __init, __exit                                 */
#include <linux/module.h>    /* module_init, module_exit, THIS_MODULE           */
#include <linux/kdev_t.h>    /* dev_t, MAJOR(), MINOR()                        */
#include <linux/fs.h>        /* file_operations, alloc_chrdev_region            */
#include <linux/cdev.h>      /* cdev_init, cdev_add, cdev_del                  */
#include <linux/device.h>    /* class_create, device_create                    */
#include <linux/slab.h>      /* kmalloc(), kfree()                             */
#include <linux/uaccess.h>   /* copy_to_user(), copy_from_user()               */
#include <linux/kthread.h>   /* kthread_create(), wake_up_process(), do_exit() */
#include <linux/wait.h>      /* DECLARE_WAIT_QUEUE_HEAD, wait_event_interruptible,
                                wake_up_interruptible — CORE waitqueue header  */
#include <linux/err.h>       /* IS_ERR()                                        */

uint32_t read_count = 0;	// Counts how many times etx_read() has been called (i.e., how many times cat /dev/etx_device was run)

/*
 * Pointer to the kernel thread structure.
 * kthread_create() returns this — we use it to start the thread with
 * wake_up_process() and to track the thread's state.
 */
static struct task_struct *wait_thread;

//STATIC METHOD: Declare and initialize the waitqueue at compile time
DECLARE_WAIT_QUEUE_HEAD(wait_queue_etx);
 
dev_t dev = 0;
static struct class *dev_class;
static struct cdev etx_cdev;

int wait_queue_flag = 0;	//shared condition variable between the thread and driver.

/* Function Prototypes */
static int      __init etx_driver_init(void);
static void     __exit etx_driver_exit(void);
/*************** Driver functions **********************/
static int      etx_open(struct inode *inode, struct file *file);
static int      etx_release(struct inode *inode, struct file *file);
static ssize_t  etx_read(struct file *filp, char __user *buf, size_t len,loff_t * off);
static ssize_t  etx_write(struct file *filp, const char *buf, size_t len, loff_t * off);

/* File operation sturcture */
static struct file_operations fops =
{
        .owner          = THIS_MODULE,
        .read           = etx_read,
        .write          = etx_write,
        .open           = etx_open,
        .release        = etx_release,
};

/* Kernel Thread function */
static int wait_function(void *unused)
{
        // Infinite loop to keep the thread alive and waiting for events
        while(1) {
                pr_info("Waiting For Event...\n");
		// Put the thread to sleep until wait_queue_flag becomes non-zero
                // This is interruptible, meaning it can be woken up by signals
                wait_event_interruptible(wait_queue_etx, wait_queue_flag != 0 );

                if(wait_queue_flag == 2) {
                        pr_info("Event Came From Exit Function\n");
                        return 0;
                }

                pr_info("Event Came From Read Function - %d\n", ++read_count);
                wait_queue_flag = 0;	// Reset the flag so the thread goes back to waiting state
        }
	// This line is never reached due to the infinite loop and return above
        // Included for safety/clarity, but effectively redundant
        do_exit(0);
        return 0;
}

/* This function will be called when we open the Device file */
static int etx_open(struct inode *inode, struct file *file)
{
        pr_info("Device File Opened...!!!\n");
        return 0;
}

/* This function will be called when we close the Device file */
static int etx_release(struct inode *inode, struct file *file)
{
        pr_info("Device File Closed...!!!\n");
        return 0;
}

/* This function will be called when we read the Device file */
static ssize_t etx_read(struct file *filp, char __user *buf, size_t len, loff_t *off)
{
        pr_info("Read Function\n");
        wait_queue_flag = 1;	// Set the flag to indicate that a read event has occurred
        wake_up_interruptible(&wait_queue_etx);	// Wake up any process/thread sleeping on this wait queue
        return 0;
}

/* This function will be called when we write the Device file */
static ssize_t etx_write(struct file *filp, const char __user *buf, size_t len, loff_t *off)
{
        pr_info("Write function\n");
        return len;
}
 
/* Module Init function */
static int __init etx_driver_init(void)
{
        /*Allocating Major number*/
        if((alloc_chrdev_region(&dev, 0, 1, "etx_Dev")) <0){
                pr_info("Cannot allocate major number\n");
                return -1;
        }
        pr_info("Major = %d Minor = %d \n",MAJOR(dev), MINOR(dev));
 
        /*Creating cdev structure*/
        cdev_init(&etx_cdev,&fops);
        etx_cdev.owner = THIS_MODULE;
        etx_cdev.ops = &fops;
 
        /*Adding character device to the system*/
        if((cdev_add(&etx_cdev,dev,1)) < 0){
            pr_info("Cannot add the device to the system\n");
            goto r_class;
        }
 
        /*Creating struct class*/
        if(IS_ERR(dev_class = class_create("etx_class"))){
            pr_info("Cannot create the struct class\n");
            goto r_class;
        }
 
        /*Creating device*/
        if(IS_ERR(device_create(dev_class,NULL,dev,NULL,"etx_device"))){
            pr_info("Cannot create the Device 1\n");
            goto r_device;
        }

 
        //Create the kernel thread with name "myThread"
        wait_thread = kthread_create(wait_function, NULL, "myThread");
        if (wait_thread) {
                pr_info("Thread Created successfully\n");
                wake_up_process(wait_thread);
        } else
                pr_info("Thread creation failed\n");
 
        pr_info("Device Driver Insert...Done!!!\n");
        return 0;
 
r_device:
        class_destroy(dev_class);
r_class:
        unregister_chrdev_region(dev,1);
        return -1;
}

/*
** Module exit function
*/ 
static void __exit etx_driver_exit(void)
{
        wait_queue_flag = 2;	//Signal the thread to exit by setting flag to 2.
        wake_up_interruptible(&wait_queue_etx);	//Wake up the sleeping thread one last time.
        device_destroy(dev_class,dev);
        class_destroy(dev_class);
        cdev_del(&etx_cdev);
        unregister_chrdev_region(dev, 1);
        pr_info("Device Driver Remove...Done!!!\n");
}
 
module_init(etx_driver_init);
module_exit(etx_driver_exit);
 
MODULE_LICENSE("GPL");
MODULE_AUTHOR("EmbeTronicX <embetronicx@gmail.com>");
MODULE_DESCRIPTION("Simple linux driver (Waitqueue Static method)");
MODULE_VERSION("1.7");
