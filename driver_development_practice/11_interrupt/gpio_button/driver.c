#include <linux/module.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/gpio/consumer.h>
#include <linux/platform_device.h>
#include <linux/mod_devicetable.h>

// Module metadata
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Developer");
MODULE_DESCRIPTION("A simple Linux kernel module for a GPIO button interrupt");
MODULE_VERSION("1.0");

// Private structure to hold our device data
struct gpio_btn_data {
    struct gpio_desc *btn_gpio;
    int irq_num;
};

// 1. The Interrupt Service Routine (ISR)
static irqreturn_t button_isr(int irq, void *dev_id)
{
    struct gpio_btn_data *priv = (struct gpio_btn_data *)dev_id;
    int val;

    // Read the physical pin value
    val = gpiod_get_value(priv->btn_gpio);

    // Spurious check: If we trigger on rising edge, value should be high (1)
    if (val == 0) {
        // Glitch or noise detected, ignore it
        return IRQ_NONE;
    }

    pr_info("gpio_button: Button pressed! (Pin value: %d)\n", val);
    
    return IRQ_HANDLED;
}

// 2. The Platform Probe Function (Runs when hardware matches the driver)
static int gpio_btn_probe(struct platform_device *pdev)
{
    struct gpio_btn_data *priv;
    struct device *dev = &pdev->dev;
    int ret;

    pr_info("gpio_button: Probing driver...\n");

    // Allocate memory for our private driver data
    priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
    if (!priv)
        return -ENOMEM;

    // Fetch the GPIO lookup using the "gpios" property inside the device tree node
    // "gpiod_get" automatically looks for a property named "[label]-gpios" or "[label]-gpio"
    priv->btn_gpio = devm_gpiod_get(dev, "button", GPIOD_IN);
    if (IS_ERR(priv->btn_gpio)) {
        dev_err(dev, "Failed to get GPIO descriptor\n");
        return PTR_ERR(priv->btn_gpio);
    }

    // Convert the GPIO descriptor into a system-wide IRQ number
    priv->irq_num = gpiod_to_irq(priv->btn_gpio);
    if (priv->irq_num < 0) {
        dev_err(dev, "Failed to map GPIO to IRQ\n");
        return priv->irq_num;
    }

    // Request the interrupt line
    // IRQF_TRIGGER_RISING: fires when voltage goes from LOW to HIGH (button press)
    ret = devm_request_irq(dev, priv->irq_num, button_isr, 
                           IRQF_TRIGGER_RISING, "gpio_btn_interrupt", priv);
    if (ret) {
        dev_err(dev, "Failed to request IRQ line: %d\n", ret);
        return ret;
    }

    // Store private data in platform device structure
    platform_set_drvdata(pdev, priv);
    
    pr_info("gpio_button: Driver loaded. Successfully mapped to IRQ %d\n", priv->irq_num);
    return 0;
}

// 3. The Platform Remove Function (Runs when module is unloaded)
static int gpio_btn_remove(struct platform_device *pdev)
{
    pr_info("gpio_button: Driver unloaded cleanly.\n");
    return 0;
}

// 4. Device Tree matching table
static const struct of_device_id gpio_btn_of_match[] = {
    { .compatible = "custom,gpio-button-interrupt" },
    { /* Sentinel */ }
};
MODULE_DEVICE_TABLE(of, gpio_btn_of_match);

// 5. Driver structure definition
static struct platform_driver gpio_btn_driver = {
    .probe = gpio_btn_probe,
    .remove = gpio_btn_remove,
    .driver = {
        .name = "gpio_button_driver",
        .of_match_table = gpio_btn_of_match,
    },
};

// Register the platform driver macro
module_platform_driver(gpio_btn_driver);
