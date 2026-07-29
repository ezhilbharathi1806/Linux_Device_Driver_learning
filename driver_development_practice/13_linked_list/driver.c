/***************************************************************************//**
*  \file       driver.c
*
*  \details    Simple Linux device driver (Kernel Linked List)
*
*  \author     EmbeTronicX
*
* *******************************************************************************/
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/kdev_t.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include<linux/slab.h>                 //kmalloc(), kfree()
#include<linux/uaccess.h>              //copy_to/from_user()
#include<linux/sysfs.h> 
#include<linux/kobject.h> 
#include <linux/interrupt.h>		//request_irq(), free_irq(), IRQF_SHARED
#include <asm/io.h>
#include <linux/workqueue.h>            // create_workqueue, queue_work
#include <linux/err.h>
 
#define IRQ_NO 11

/* ── GLOBAL VARIABLES ────────────────────────────────────────────────────── */
volatile int etx_value = 0;	//shared variable between write and workqueue.
dev_t dev = 0;
static struct class *dev_class;
static struct cdev etx_cdev;
struct kobject *kobj_ref;

/* Our own dedicated workqueue (same as Part 16) */
static struct workqueue_struct *own_workqueue;

/* ── LINKED LIST SETUP ───────────────────────────────────────────────────── */
/*Linked List Node*/
struct my_list{
     struct list_head list;     //linux kernel list implementation
     int data;
};
 
/*Declare and init the head node of the linked list*/
LIST_HEAD(Head_Node);

/* ── FUNCTION PROTOTYPES ─────────────────────────────────────────────────── */
static int __init etx_driver_init(void);
static void __exit etx_driver_exit(void);
/*************** Driver functions **********************/
static int etx_open(struct inode *inode, struct file *file);
static int etx_release(struct inode *inode, struct file *file);
static ssize_t etx_read(struct file *filp, char __user *buf, size_t len,loff_t * off);
static ssize_t etx_write(struct file *filp, const char *buf, size_t len, loff_t * off);
/*************** Sysfs functions **********************/
static ssize_t sysfs_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf);
static ssize_t sysfs_store(struct kobject *kobj, struct kobj_attribute *attr,const char *buf, size_t count);
/* Sysfs attribute — kept empty in this tutorial, included for completeness */
struct kobj_attribute etx_attr = __ATTR(etx_value, 0660, sysfs_show, sysfs_store);

/* ── WORKQUEUE FUNCTION (Bottom Half) ────────────────────────────────────── */
/* Forward declaration — needed because DECLARE_WORK uses it before definition */
static void workqueue_fn(struct work_struct *work);

/* work_struct initialized at compile time — static method */
static DECLARE_WORK(work, workqueue_fn);

/*Workqueue Function*/
static void workqueue_fn(struct work_struct *work)
{
        struct my_list *temp_node = NULL;

        pr_info("Executing Workqueue Function\n");

        /*Creating Node*/
        temp_node = kmalloc(sizeof(struct my_list), GFP_KERNEL);

        /*Assgin the data that is received*/
        temp_node->data = etx_value;

        /*Init the list within the struct*/
        INIT_LIST_HEAD(&temp_node->list);

        /*Add Node to Linked List*/
        list_add_tail(&temp_node->list, &Head_Node);
}

 
/* ── INTERRUPT HANDLER (Top Half) ───────────────────────────────────────── */
//Interrupt handler for IRQ 11. 
static irqreturn_t irq_handler(int irq,void *dev_id) {

	pr_info("Shared IRQ: Interrupt Occurred\n");
        queue_work(own_workqueue, &work);	/*Allocating work to queue*/
        
        return IRQ_HANDLED;
}

/* ── FILE OPERATIONS TABLE ───────────────────────────────────────────────── */
static struct file_operations fops =
{
        .owner          = THIS_MODULE,
        .read           = etx_read,
        .write          = etx_write,
        .open           = etx_open,
        .release        = etx_release,
};

/* ── SYSFS FUNCTIONS (empty — kept for completeness) ─────────────────────── */
static ssize_t sysfs_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
        pr_info("Sysfs - Read!!!\n");
        return sprintf(buf, "%d", etx_value);
}

static ssize_t sysfs_store(struct kobject *kobj, struct kobj_attribute *attr,const char *buf, size_t count)
{
        pr_info("Sysfs - Write!!!\n");
        return count;
}

/* ── DEVICE FUNCTIONS ────────────────────────────────────────────────────── */
/* Called when /dev/etx_device is opened */
static int etx_open(struct inode *inode, struct file *file)
{
        pr_info("Device File Opened...!!!\n");
        return 0;
}

/* Called when /dev/etx_device is closed */
static int etx_release(struct inode *inode, struct file *file)
{
        pr_info("Device File Closed...!!!\n");
        return 0;
}

/*
 * etx_read() — traverses the linked list and prints all nodes
 * Triggered by: cat /dev/etx_device
 *
 * list_for_each_entry(pos, head, member):
 *   pos    = loop cursor — pointer to our struct (my_list*)
 *   head   = the list head anchor (&Head_Node)
 *   member = name of list_head field inside our struct ("list")
 */
static ssize_t etx_read(struct file *filp, 
                char __user *buf, size_t len, loff_t *off)
{
        struct my_list *temp;	/* loop cursor — points to each node in turn  */
        int count = 0;
        pr_info("Read function\n");
 
	/* Traverse list forward and print each node's data */
        list_for_each_entry(temp, &Head_Node, list) {
            pr_info("Node %d data = %d\n", count++, temp->data);
        }
 
        pr_info("Total Nodes = %d\n", count);	 /* Print total count — 0 if no nodes added yet */
        return 0;	/* EOF — no data sent to user */
}

/*
 * etx_write() — reads user value and fires an interrupt
 * Triggered by: echo 10 > /dev/etx_device
 *
 * sscanf(buf, "%d", &etx_value):
 *   buf       = the string user wrote (e.g., "10\n")
 *   "%d"      = parse as integer
 *   &etx_value= store result in global variable
 */
static ssize_t etx_write(struct file *filp, 
                const char __user *buf, size_t len, loff_t *off)
{
        pr_info("Write Function\n");
        /*Copying data from user space*/
        sscanf(buf,"%d",&etx_value);
        /* Triggering Interrupt */
        asm("int $0x3B");  // Corresponding to irq 11
        return len;
}
 
/* ── MODULE INIT ──────────────────────────────────────────────────────────── */
static int __init etx_driver_init(void)
{
	/* Step 1: Get dynamic Major:Minor */
        if((alloc_chrdev_region(&dev, 0, 1, "etx_Dev")) < 0){
                printk(KERN_INFO "Cannot allocate major number\n");
                return -1;
        }
        printk(KERN_INFO "Major = %d Minor = %d \n", MAJOR(dev), MINOR(dev));

        /* Step 2: Init cdev */
        cdev_init(&etx_cdev, &fops);

        /* Step 3: Register cdev */
        if((cdev_add(&etx_cdev, dev, 1)) < 0){
            printk(KERN_INFO "Cannot add the device to the system\n");
            goto r_class;
        }

        /* Step 4: Create device class */
        if(IS_ERR(dev_class = class_create("etx_class"))){
            printk(KERN_INFO "Cannot create the struct class\n");
            goto r_class;
        }

        /* Step 5: Create /dev/etx_device */
        if(IS_ERR(device_create(dev_class, NULL, dev, NULL, "etx_device"))){
            printk(KERN_INFO "Cannot create the Device\n");
            goto r_device;
        }

        /* Step 6: Create sysfs directory and file */
        kobj_ref = kobject_create_and_add("etx_sysfs", kernel_kobj);
        if(sysfs_create_file(kobj_ref, &etx_attr.attr)){
                printk(KERN_INFO "Cannot create sysfs file......\n");
                goto r_sysfs;
        }

        /* Step 7: Register IRQ 11 handler */
        if(request_irq(IRQ_NO, irq_handler, IRQF_SHARED, "etx_device",
                        (void *)(irq_handler))) {
            printk(KERN_INFO "my_device: cannot register IRQ\n");
            goto irq;
        }

        /* Step 8: Create our own dedicated workqueue named "own_wq" */
        own_workqueue = create_workqueue("own_wq");
        if(!own_workqueue) {
            printk(KERN_INFO "Cannot create workqueue\n");
            goto irq;
        }

        printk(KERN_INFO "Device Driver Insert...Done!!!\n");
        return 0;
 
irq:
        free_irq(IRQ_NO,(void *)(irq_handler));
 
r_sysfs:
        kobject_put(kobj_ref); 
        sysfs_remove_file(kernel_kobj, &etx_attr.attr);
 
r_device:
        class_destroy(dev_class);
r_class:
        unregister_chrdev_region(dev,1);
        cdev_del(&etx_cdev);
        return -1;
}

/* ── MODULE EXIT ──────────────────────────────────────────────────────────── */
static void __exit etx_driver_exit(void)
{
 
        /* Go through the list and free the memory. */
        struct my_list *cursor, *temp;
        list_for_each_entry_safe(cursor, temp, &Head_Node, list) {
            list_del(&cursor->list);
            kfree(cursor);
        }
 
        destroy_workqueue(own_workqueue);	/* Delete workqueue */
        free_irq(IRQ_NO,(void *)(irq_handler));
        kobject_put(kobj_ref); 
        sysfs_remove_file(kernel_kobj, &etx_attr.attr);
        device_destroy(dev_class,dev);
        class_destroy(dev_class);
        cdev_del(&etx_cdev);
        unregister_chrdev_region(dev, 1);
        pr_info("Device Driver Remove...Done!!\n");
}
 
module_init(etx_driver_init);
module_exit(etx_driver_exit);
 
MODULE_LICENSE("GPL");
MODULE_AUTHOR("EmbeTronicX <embetronicx@gmail.com>");
MODULE_DESCRIPTION("A simple device driver - Kernel Linked List");
MODULE_VERSION("1.13");

/* Complete flow summary

insmod driver.ko
  ├── LIST_HEAD(Head_Node)  → empty list (head.prev = head.next = &head)
  ├── create_workqueue("own_wq") → [own_wq] thread created
  └── /dev/etx_device + sysfs   → created

echo 10 > /dev/etx_device
  ├── etx_write()
  │     ├── sscanf("10") → etx_value = 10
  │     └── asm("int $0x3B") → fires IRQ 11
  │               └── irq_handler() → queue_work(own_workqueue)
  │                         └── workqueue_fn()
  │                               ├── kmalloc(node)      → allocate node
  │                               ├── node->data = 10    → store value
  │                               ├── INIT_LIST_HEAD()   → init list_head
  │                               └── list_add_tail()    → HEAD → [10]

echo 20 > /dev/etx_device  →  HEAD → [10] → [20]
echo 30 > /dev/etx_device  →  HEAD → [10] → [20] → [30]

cat /dev/etx_device
  └── etx_read()
        ├── list_for_each_entry → Node 0 data = 10
        ├──                    → Node 1 data = 20
        ├──                    → Node 2 data = 30
        └──                    → Total Nodes = 3

rmmod driver
  └── list_for_each_entry_safe → list_del + kfree each node → memory freed
 */
