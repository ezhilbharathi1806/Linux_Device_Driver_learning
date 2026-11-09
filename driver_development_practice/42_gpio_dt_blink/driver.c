/* blink led - led is connected to pin no 20 of raspberru pi pico 4b*/
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/mod_devicetable.h>
#include <linux/gpio/consumer.h>

// Module Metadata
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Developer");
MODULE_DESCRIPTION("A simple Linux kernel module for a GPIO 20 blink led");

static struct gpio_desc *led;

static int my_probe(struct platform_device *pdev){

}

static int my_remove(struct platform_device *pdev){
    printk("gpio_led: driver unloaded cleanly \n");
    return 0;
}

static const struct of_device_id gpio_led_match[] = {
    { .compatible = "cutom,gpio_blink_led"},
    {}
};
MODULE_DEVICE_TABLE(of, gpio_led_match);

static struct platform_driver gpio_led_driver = {
    .probe = my_probe;
    .remove = my_remove;
    .driver = {
        .name = "blink_led",
        .of_match_table = gpio_led_match,
    },
};

module_platform_driver(gpio_led_driver);