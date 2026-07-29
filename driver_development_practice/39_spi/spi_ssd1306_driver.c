/***************************************************************************//**
*  \file       spi_ssd1306_driver.c
*  \details    SPI Protocol Driver for SSD1306 OLED (SPI slave)
*
*  SIMILAR TO I2C CLIENT DRIVER but uses SPI APIs instead of I2C APIs:
*    I2C:  i2c_get_adapter → i2c_new_device → i2c_add_driver
*    SPI:  spi_busnum_to_master → spi_new_device → spi_setup
*
*  Hardware: SPI1 on Raspberry Pi 4B, SSD1306 OLED as slave
*******************************************************************************/
#include <linux/init.h>
#include <linux/module.h>
#include <linux/spi/spi.h>   /* spi_master, spi_device, spi_board_info,
                                spi_new_device, spi_setup, spi_sync_transfer,
                                spi_unregister_device, spi_busnum_to_master   */
#include <linux/delay.h>
#include "ssd1306.h"          /* SSD1306 display functions + macros           */

/* Pointer to our SPI slave device — used for all transfers */
static struct spi_device *etx_spi_device;

/*
 * etx_spi_device_info — describes our SPI slave to the SPI controller.
 *
 * Same role as spi_board_info in I2C (i2c_board_info).
 *
 * .modalias     = driver name — used for matching (not strictly needed here)
 * .max_speed_hz = 4MHz — maximum clock speed SSD1306 supports (check datasheet)
 * .bus_num      = SPI_BUS_NUM (1) — we use SPI bus 1 on RPi
 * .chip_select  = 0 — CS0 line (connected to GPIO 18 on RPi SPI1)
 * .mode         = SPI_MODE_0 — Clock polarity=0, phase=0 (check datasheet)
 *   SPI_MODE_0: CPOL=0 (idle LOW), CPHA=0 (sample on rising edge)
 *   SPI_MODE_1: CPOL=0, CPHA=1 | SPI_MODE_2: CPOL=1, CPHA=0 | SPI_MODE_3: CPOL=1, CPHA=1
 */
struct spi_board_info etx_spi_device_info = {
    .modalias     = "etx-spi-ssd1306-driver",
    .max_speed_hz = 4000000,          /* 4 MHz                                */
    .bus_num      = SPI_BUS_NUM,      /* SPI bus 1 (defined in ssd1306.h as 1) */
    .chip_select  = 0,                /* CS0 = GPIO 18 on RPi SPI1            */
    .mode         = SPI_MODE_0        /* CPOL=0, CPHA=0                       */
};

/*
 * etx_spi_write() — send 1 byte to SSD1306 via SPI
 *
 * Uses spi_sync_transfer() — synchronous blocking transfer.
 * Both tx and rx happen simultaneously in SPI (full-duplex).
 * We don't care about rx here (SSD1306 is write-only display).
 *
 * struct spi_transfer fields:
 *   .tx_buf = &data → pointer to byte to send (MOSI line)
 *   .rx_buf = &rx   → buffer for received byte (MISO line, ignored)
 *   .len    = 1     → transfer 1 byte
 *
 * spi_sync_transfer(spi_device, transfers_array, num_transfers):
 *   Blocks until transfer is complete.
 *   Returns 0=success, negative=error.
 */
int etx_spi_write(uint8_t data)
{
    int     ret = -1;
    uint8_t rx  = 0x00;   /* receive buffer — ignored for write-only display */

    if (etx_spi_device) {
        struct spi_transfer tr = {
            .tx_buf = &data,   /* byte to send via MOSI            */
            .rx_buf = &rx,     /* receive buffer (MISO — ignored)  */
            .len    = 1,       /* transfer 1 byte                  */
        };
        spi_sync_transfer(etx_spi_device, &tr, 1);  /* 1 transfer, blocking */
    }
    return ret;
}

/*
 * etx_spi_init() — module init: get SPI master, create slave, setup, init OLED
 *
 * Step 1: spi_busnum_to_master(1) → get handle to SPI bus 1 controller
 * Step 2: spi_new_device(master, &info) → create slave device at CS0
 * Step 3: set bits_per_word + spi_setup() → apply configuration
 * Step 4: ETX_SSD1306_DisplayInit() → send init commands to OLED
 * Step 5: display text, scroll, wait 9s, clear, show logo
 */
static int __init etx_spi_init(void)
{
    int    ret;
    struct spi_master *master;

    /*
     * Step 1: Get the SPI bus 1 controller.
     * spi_busnum_to_master(bus_num):
     *   Returns pointer to spi_controller for bus_num (1 = SPI1 on RPi).
     *   Returns NULL if bus doesn't exist or not enabled.
     *   After use: must be released (driver holds internal reference).
     */
    master = spi_busnum_to_master(etx_spi_device_info.bus_num);
    if (master == NULL) {
        pr_err("SPI Master not found.\n");
        return -ENODEV;   /* SPI bus 1 not enabled — check /boot/config.txt  */
    }

    /*
     * Step 2: Create SPI slave device.
     * spi_new_device(master, &board_info):
     *   Allocates an spi_device + adds it to the SPI controller.
     *   Internally calls spi_alloc_device() + spi_add_device() — no need to call those separately.
     *   Returns: pointer to new spi_device on success, NULL on failure.
     *   After loading: slave visible at /sys/bus/spi/devices/spi1.0
     */
    etx_spi_device = spi_new_device(master, &etx_spi_device_info);
    if (etx_spi_device == NULL) {
        pr_err("FAILED to create slave.\n");
        return -ENODEV;
    }

    /*
     * Step 3a: Set word size for this SPI device.
     * bits_per_word = 8 → each transfer unit is 8 bits (1 byte).
     * Must be set BEFORE spi_setup().
     */
    etx_spi_device->bits_per_word = 8;

    /*
     * Step 3b: Apply configuration to SPI bus.
     * spi_setup(spi_device):
     *   Sends configuration (mode, bits_per_word, max_speed_hz, chip_select)
     *   to the SPI controller hardware.
     *   Must be called after any configuration change to take effect.
     *   Returns 0=success, negative=error.
     */
    ret = spi_setup(etx_spi_device);
    if (ret) {
        pr_err("FAILED to setup slave.\n");
        spi_unregister_device(etx_spi_device);  /* cleanup on error */
        return -ENODEV;
    }

    /* Step 4: Initialize SSD1306 OLED — send standard init commands */
    ETX_SSD1306_DisplayInit();   /* reset + power up + configure display */

    /* Step 5: Display content */
    ETX_SSD1306_SetBrightness(255);            /* max brightness           */
    ETX_SSD1306_InvertDisplay(false);          /* normal display mode      */
    ETX_SSD1306_StartScrollHorizontal(true, 0, 2); /* scroll first 3 lines */

    ETX_SSD1306_SetCursor(0, 0);
    ETX_SSD1306_String("Welcome\nTo\nEmbeTronicX\n");
    ETX_SSD1306_SetCursor(4, 35);
    ETX_SSD1306_String("SPI Linux\n");
    ETX_SSD1306_SetCursor(5, 23);
    ETX_SSD1306_String("Device Driver\n");
    ETX_SSD1306_SetCursor(6, 37);
    ETX_SSD1306_String("Tutorial\n");

    msleep(9000);                              /* show text for 9 seconds  */
    ETX_SSD1306_ClearDisplay();               /* clear before showing logo */
    ETX_SSD1306_DeactivateScroll();           /* stop scrolling            */
    ETX_SSD1306_PrintLogo();                  /* show EmbeTronicX logo     */

    pr_info("SPI driver Registered\n");
    return 0;
}

/*
 * etx_spi_exit() — module exit: clear display + unregister slave
 *
 * spi_unregister_device(spi_device):
 *   Removes the slave device from SPI controller.
 *   Equivalent to i2c_unregister_device() in I2C.
 */
static void __exit etx_spi_exit(void)
{
    if (etx_spi_device) {
        ETX_SSD1306_ClearDisplay();          /* clear OLED before exit    */
        ETX_SSD1306_DisplayDeInit();         /* free RST and DC GPIOs     */
        spi_unregister_device(etx_spi_device); /* remove slave from SPI   */
        pr_info("SPI driver Unregistered\n");
    }
}

module_init(etx_spi_init);
module_exit(etx_spi_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("EmbeTronicX <embetronicx@gmail.com>");
MODULE_DESCRIPTION("A simple device driver - SPI Slave Protocol Driver");
MODULE_VERSION("1.44");

/*
insmod spissd1306.ko
  ├── spi_busnum_to_master(1)     → get SPI1 controller
  ├── spi_new_device(info)        → create slave at CS0
  ├── bits_per_word=8 + spi_setup()
  └── ETX_SSD1306_DisplayInit()   → RST pulse + 27 init commands via SPI
        └── Display text (scrolling) for 9 seconds → shows logo

rmmod spissd1306
  ├── ETX_SSD1306_ClearDisplay()  → clears OLED
  ├── ETX_SSD1306_DisplayDeInit() → frees RST + DC GPIOs
  └── spi_unregister_device()     → removes slave from SPI controller
 */
