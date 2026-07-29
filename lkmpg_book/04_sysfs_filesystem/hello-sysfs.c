/*
 * hello-sysfs.c — Simple sysfs example
 *
 * Creates /sys/kernel/mymodule/myvariable
 * User can read and write 'myvariable' using cat and echo.
 *
 * Test:
 *   cat /sys/kernel/mymodule/myvariable       → prints current value
 *   echo 42 > /sys/kernel/mymodule/myvariable → sets value to 42
 */

/* ── HEADERS ─────────────────────────────────────────────────────────────── */
#include <linux/fs.h>        /* file system support                           */
#include <linux/init.h>      /* __init, __exit                                */
#include <linux/kobject.h>   /* kobject_create_and_add(), kobject_put()       */
#include <linux/module.h>    /* module_init, module_exit, MODULE_LICENSE      */
#include <linux/string.h>    /* string utilities                              */
#include <linux/sysfs.h>     /* sysfs_create_file(), kobj_attribute, __ATTR  */

static struct kobject *mymodule;	// Pointer to our kobject = our directory in /sys/kernel/mymodule/

static int myvariable = 0;	//Integer variable that will be exposed to userspace via sysfs

/* ── SYSFS HANDLER FUNCTIONS ─────────────────────────────────────────────── */
/*
 * myvariable_show() — called when user reads the sysfs file
 * Triggered by: cat /sys/kernel/mymodule/myvariable
 */
static ssize_t myvariable_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
    return sprintf(buf, "%d\n", myvariable);	/* format value as string */
}

/*
 * myvariable_store() — called when user writes to the sysfs file
 * Triggered by: echo 42 > /sys/kernel/mymodule/myvariable
 *
 * Parses the string from buf into myvariable.
 */
static ssize_t myvariable_store(struct kobject *kobj, struct kobj_attribute *attr, const char *buf, size_t count)
{
    sscanf(buf, "%d", &myvariable);	/* parse integer from user string */
    return count;
}

/* ── SYSFS ATTRIBUTE DEFINITION ──────────────────────────────────────────── */
/*
 * __ATTR(name, permissions, show_fn, store_fn)
 * Defines ONE sysfs file:
 *   name        = "myvariable" → filename in /sys/kernel/mymodule/
 *   permissions = 0660         → owner+group: rw, others: none
 *   show_fn     = myvariable_show  → called on cat (read)
 *   store_fn    = myvariable_store → called on echo (write)
 */
static struct kobj_attribute myvariable_attribute = __ATTR(myvariable, 0660, myvariable_show, myvariable_store);

/* ── MODULE INIT ──────────────────────────────────────────────────────────── */
/*
 * mymodule_init() — runs on: sudo insmod hello-sysfs.ko
 *
 * Step 1: kobject_create_and_add() → creates /sys/kernel/mymodule/
 * Step 2: sysfs_create_file()      → creates /sys/kernel/mymodule/myvariable
 */
static int __init mymodule_init(void)
{
    int error = 0;
    pr_info("mymodule: initialized\n");

    mymodule = kobject_create_and_add("mymodule", kernel_kobj);	// Creates directory: /sys/kernel/mymodule
    if (!mymodule)
        return -ENOMEM;

    error = sysfs_create_file(mymodule, &myvariable_attribute.attr);	// Creates file: /sys/kernel/mymodule/myvariable
    if (error) {
        kobject_put(mymodule);
        pr_info("failed to create the myvariable file "
                "in /sys/kernel/mymodule\n");
    }

    return error;	/* 0 = success, negative = failure */
}

static void __exit mymodule_exit(void)
{
    pr_info("mymodule: Exit success\n");
    kobject_put(mymodule);	/* Deletes kobject → removes /sys/kernel/mymodule */
}

module_init(mymodule_init);
module_exit(mymodule_exit);

MODULE_LICENSE("GPL");


/*
insmod hello-sysfs.ko
  ├── kobject_create_and_add() → /sys/kernel/mymodule/   (directory)
  └── sysfs_create_file()     → /sys/kernel/mymodule/myvariable  (file)

cat /sys/kernel/mymodule/myvariable
  └── myvariable_show()  → sprintf(buf, "%d\n", 0) → prints: 0

echo 42 > /sys/kernel/mymodule/myvariable (or) >echo "32" | sudo tee /sys/kernel/mymodule/myvariable
  └── myvariable_store() → sscanf("42", "%d", &myvariable) → myvariable = 42

cat /sys/kernel/mymodule/myvariable
  └── myvariable_show()  → prints: 42

rmmod hello-sysfs
  └── kobject_put() → frees kobject → removes /sys/kernel/mymodule/ entirely
*/
