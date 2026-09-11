#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/mod_devicetable.h>
#include <linux/property.h>

#define DRIVER_NAME "my_custom_device"

static int my_probe(struct platform_device *pdev)
{
    struct device *dev = &pdev->dev;
    u32 my_prop_val = 0;

    dev_info(dev, "Probed successfully!\n");

    /* Read a custom integer property from Device Tree */
    if (device_property_read_u32(dev, "my-integer-prop", &my_prop_val)) {
        dev_warn(dev, "Failed to read 'my-integer-prop', using default\n");
    } else {
        dev_info(dev, "Read 'my-integer-prop' = %u\n", my_prop_val);
    }

    return 0;
}

static void my_remove(struct platform_device *pdev)
{
    dev_info(&pdev->dev, "Removing platform driver\n");
}

/* Match table for Device Tree binding */
static const struct of_device_id my_of_match[] = {
    { .compatible = "vendor,my-custom-device", },
    { /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, my_of_match);

static struct platform_driver my_platform_driver = {
    .probe = my_probe,
    .remove = my_remove, /* Use .remove for older kernel versions */
    .driver = {
        .name = DRIVER_NAME,
        .of_match_table = my_of_match,
    },
};

module_platform_driver(my_platform_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("platform example");
MODULE_DESCRIPTION("Basic Device Tree Platform Driver Skeleton");
