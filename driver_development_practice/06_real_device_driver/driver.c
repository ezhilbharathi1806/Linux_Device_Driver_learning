/***************************************************************************//**
*  \details    Simple Linux device driver (Real Linux Device Driver)

******************************************************************************/
#include <linux/kernel.h>   /* pr_info, pr_err — kernel logging functions      */
#include <linux/init.h>     /* __init, __exit — memory optimization macros      */
#include <linux/module.h>   /* module_init, module_exit, THIS_MODULE            */
#include <linux/kdev_t.h>   /* dev_t, MAJOR(), MINOR() — device number helpers  */
#include <linux/fs.h>       /* file_operations, alloc_chrdev_region             */
#include <linux/cdev.h>     /* struct cdev, cdev_init, cdev_add, cdev_del       */
#include <linux/device.h>   /* class_create, device_create — auto /dev entry    */
#include <linux/slab.h>     /* kmalloc(), kfree() — kernel memory allocation    */
#include <linux/uaccess.h>  /* copy_to_user(), copy_from_user() — safe transfer */
#include <linux/err.h>      /* IS_ERR() — error checking on pointer returns     */
#include <linux/string.h>               //memset, strlen

/*
 * Size of the kernel buffer that stores data written by the user.
 * 1024 bytes = 1KB — enough for most test strings.
 */
#define mem_size        1024
 
dev_t dev = 0;		//dev_t holds the assigned Major:Minor number. Initialized to 0 So the kernel fills in the real value via alloc_chrdev_region()
static struct class *dev_class;
static struct cdev etx_cdev;
uint8_t *kernel_buffer;	//Pointer to the dynamically allocated kernel buffer.This is where user-written data is stored in kernel memory.
static size_t kernel_buffer_len = 0;

/*	FILE OPERATIONS TABLE
 * fops is the "jump table" that maps system calls to driver functions.
 *
 * When user calls:   open()  → etx_open()    is invoked
 *                    read()  → etx_read()    is invoked
 *                    write() → etx_write()   is invoked
 *                    close() → etx_release() is invoked
 *
 * .owner = THIS_MODULE prevents the module from being unloaded
 * while the device is actively being used by any process.
 */
static int      __init etx_driver_init(void);
static void     __exit etx_driver_exit(void);
static int      etx_open(struct inode *inode, struct file *file);
static int      etx_release(struct inode *inode, struct file *file);
static ssize_t  etx_read(struct file *filp, char __user *buf, size_t len,loff_t * off);
static ssize_t  etx_write(struct file *filp, const char *buf, size_t len, loff_t * off);


/* File Operations structure*/
static struct file_operations fops =
{
        .owner          = THIS_MODULE,
        .read           = etx_read,
        .write          = etx_write,
        .open           = etx_open,
        .release        = etx_release,
};
 
/* This function will be called when we open the Device file*/
//Triggered by: open("/dev/etx_device", O_RDWR) in user app or implicitly by echo, cat commands
static int etx_open(struct inode *inode, struct file *file)
{
        pr_info("Device File Opened...!!!\n");
        return 0;
}

/* This function will be called when we close the Device file*/
// Triggered by: close(fd) in user app, or when the process exits
static int etx_release(struct inode *inode, struct file *file)
{
        pr_info("Device File Closed...!!!\n");
        return 0;
}

/* This function will be called when we read the Device file*/
// Triggered by: read(fd, buf, size) in user app or: cat /dev/etx_device from terminal
static ssize_t etx_read(struct file *filp, char __user *buf, size_t len, loff_t *off)
{
        //Copy the data from the kernel space to the user-space
        if( copy_to_user(buf, kernel_buffer, kernel_buffer_len) )
        {
                pr_err("Data Read : Err!\n");
        }
        pr_info("Data Read : Done!\n");

        if (*off >= kernel_buffer_len){
                return 0;       //EOF
        }
        *off += len;
        
        return kernel_buffer_len;
}

/* This function will be called when we write the Device file*/
// Triggered by: write(fd, data, len) in user app or: echo "hello" | sudo tee /dev/etx_device from terminal
static ssize_t etx_write(struct file *filp, const char __user *buf, size_t len, loff_t *off)
{
        //Clear the buffer before writing
        memset(kernel_buffer, 0, mem_size);
        //Copy the data to kernel space from the user-space
        if( copy_from_user(kernel_buffer, buf, len) )
        {
                pr_err("Data Write : Err!\n");
        }
        kernel_buffer_len = len;
        pr_info("Data Write : Done!\n");
        return len;
}

/* Module Init function*/
/*
 * etx_driver_init() — runs when module is loaded: sudo insmod driver.ko
 *
 * __init tells the kernel: free this function's memory after module loads,
 * since it's never called again. Saves RAM.
 *
 * Initialization steps (order matters — each step depends on the previous):
 *  Step 1: alloc_chrdev_region → dynamically get a Major:Minor number
 *  Step 2: cdev_init           → link our fops table to the cdev structure
 *  Step 3: cdev_add            → register cdev with kernel (device goes LIVE)
 *  Step 4: class_create        → create /sys/class/etx_class/ (udev watches this)
 *  Step 5: device_create       → udev auto-creates /dev/etx_device
 *  Step 6: kmalloc             → allocate 1024-byte kernel buffer for data storage
 *  Step 7: strcpy              → pre-fill buffer with "Hello_World" as default data
 *
 * On failure: goto labels clean up already-done steps in reverse order (no leaks).
 */
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
        
        /*Creating Physical memory*/
        if((kernel_buffer = kmalloc(mem_size , GFP_KERNEL)) == 0){
            pr_info("Cannot allocate memory in kernel\n");
            goto r_device;
        }
        
        memset(kernel_buffer, 0, mem_size);	//clears the allocated memory by setting all bytes(mem_size =1024) to zero, ensuring no garbage data and safe reads
        strcpy(kernel_buffer, "Hello_World");
        kernel_buffer_len = strlen("Hello_World");
        
        pr_info("Device Driver Insert...Done!!!\n");
        return 0;
 
r_device:
        class_destroy(dev_class);
r_class:
        unregister_chrdev_region(dev,1);
        return -1;
}

/* Module exit function*/
static void __exit etx_driver_exit(void)
{
	kfree(kernel_buffer);
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
MODULE_DESCRIPTION("Simple Linux device driver (Real Linux Device Driver)");
MODULE_VERSION("1.4");
