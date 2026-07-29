/**************************************************************************//**
*  \file       driver.c
*
*  \details    Interrupt Example
*******************************************************************************/
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/kdev_t.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/slab.h>          /* kmalloc()                                 */
#include <linux/uaccess.h>       /* copy_to/from_user()                       */
#include <linux/interrupt.h>     /* request_irq(), free_irq(), IRQF_SHARED    */
#include <asm/io.h>              /* I/O port access                           */
#include <asm/hw_irq.h>          /* irq_to_desc(), vector_irq — NEW KERNEL    */
#include <linux/err.h>           /* IS_ERR()                                  */

#define IRQ_NO 11	/* We use IRQ 11 — a commonly available shared IRQ on x86 systems */

/* ── INTERRUPT HANDLER (ISR / Top Half) ──────────────────────────────────── */
/* irq_handler() — called by the kernel whenever IRQ 11 fires
 *
 * @irq    : the IRQ number that triggered (11 here)
 * @dev_id : cookie passed during request_irq() — used to identify our handler
 *
 * Rules: MUST be fast. NO sleeping. NO mutex. NO user space access.
 *
 * IRQ_HANDLED → tells kernel "we handled this interrupt"
 * IRQ_NONE    → tells kernel "this wasn't our interrupt" (for shared IRQs)
 */
static irqreturn_t irq_handler(int irq,void *dev_id) {
  printk(KERN_INFO "Shared IRQ: Interrupt Occurred");
  return IRQ_HANDLED;
}

volatile int etx_value = 0;
 
dev_t dev = 0;
static struct class *dev_class;
static struct cdev etx_cdev;

/* ── FUNCTION PROTOTYPES ─────────────────────────────────────────────────── */
static int __init etx_driver_init(void);
static void __exit etx_driver_exit(void);

/*************** Driver Fuctions **********************/
static int etx_open(struct inode *inode, struct file *file);
static int etx_release(struct inode *inode, struct file *file);
static ssize_t etx_read(struct file *filp, char __user *buf, size_t len,loff_t * off);
static ssize_t etx_write(struct file *filp, const char *buf, size_t len, loff_t * off);

/* ── FILE OPERATIONS TABLE ───────────────────────────────────────────────── */
static struct file_operations fops =
{
        .owner          = THIS_MODULE,
        .read           = etx_read,
        .write          = etx_write,
        .open           = etx_open,
        .release        = etx_release,
};

/* ── DEVICE FUNCTIONS ────────────────────────────────────────────────────── */

/* Called when /dev/etx_device is opened */
static int etx_open(struct inode *inode, struct file *file)
{
        printk(KERN_INFO "Device File Opened...!!!\n");
        return 0;
}

/* Called when /dev/etx_device is closed */
static int etx_release(struct inode *inode, struct file *file)
{
        printk(KERN_INFO "Device File Closed...!!!\n");
        return 0;
}

/*
 * etx_read() — triggers a software interrupt when user reads device
 * Called by: sudo cat /dev/etx_device
 *
 * asm("int $0x3B")
 *   → Fires x86 software interrupt at vector 0x3B (59 decimal = IRQ 11)
 *   → CPU looks up vector_irq[59] → finds our IRQ 11 desc → calls irq_handler()
 *
 * WHY 0x3B?
 *   IRQ 11 vector = FIRST_EXTERNAL_VECTOR(0x20) + 0x10 + 11 = 0x3B
 */
static ssize_t etx_read(struct file *filp, 
                char __user *buf, size_t len, loff_t *off)
{
	printk(KERN_INFO "Read function\n");
        asm("int $0x3B");  // Corresponding to irq 11
        return 0;
}

/* Called when user writes to /dev/etx_device — not used here */
static ssize_t etx_write(struct file *filp, 
                const char __user *buf, size_t len, loff_t *off)
{
        printk(KERN_INFO "Write Function\n");
        return len;
}
 
/* ── MODULE INIT ──────────────────────────────────────────────────────────── */
static int __init etx_driver_init(void)
{
        /*Allocating Major number*/
        if((alloc_chrdev_region(&dev, 0, 1, "etx_Dev")) <0){
                printk(KERN_INFO "Cannot allocate major number\n");
                return -1;
        }
        printk(KERN_INFO "Major = %d Minor = %d \n",MAJOR(dev), MINOR(dev));
 
        /*Creating cdev structure*/
        cdev_init(&etx_cdev,&fops);
 
        /*Adding character device to the system*/
        if((cdev_add(&etx_cdev,dev,1)) < 0){
            printk(KERN_INFO "Cannot add the device to the system\n");
            goto r_class;
        }
 
        /*Creating struct class*/
        if(IS_ERR(dev_class = class_create("etx_class"))){
            printk(KERN_INFO "Cannot create the struct class\n");
            goto r_class;
        }
 
        /*Creating device*/
        if(IS_ERR(device_create(dev_class,NULL,dev,NULL,"etx_device"))){
            printk(KERN_INFO "Cannot create the Device 1\n");
            goto r_device;
        }

 	/* Register interrupt handler */
        if (request_irq(IRQ_NO, irq_handler, IRQF_SHARED, "etx_device", (void *)(irq_handler))) {
            printk(KERN_INFO "my_device: cannot register IRQ ");
                    goto irq;
        }
        printk(KERN_INFO "Device Driver Insert...Done!!!\n");
    return 0;

irq:
        free_irq(IRQ_NO,(void *)(irq_handler));		/* release IRQ 11         */

r_device:
        class_destroy(dev_class);
r_class:
        unregister_chrdev_region(dev,1);
        cdev_del(&etx_cdev);
        return -1;
}
 
static void __exit etx_driver_exit(void)
{
        free_irq(IRQ_NO,(void *)(irq_handler));
	
        device_destroy(dev_class,dev);
        class_destroy(dev_class);
        cdev_del(&etx_cdev);
        unregister_chrdev_region(dev, 1);
        printk(KERN_INFO "Device Driver Remove...Done!!!\n");
}
 
module_init(etx_driver_init);
module_exit(etx_driver_exit);
 
MODULE_LICENSE("GPL");
MODULE_AUTHOR("EmbeTronicX <embetronicx@gmail.com>");
MODULE_DESCRIPTION("A simple device driver - Interrupts");
MODULE_VERSION("1.9");
