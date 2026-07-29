/***************************************************************************//**
*  \file       driver.c
*  \details    Simple I2C driver — interfaces SSD1306 OLED via I2C
*              insmod → fills OLED with 0xFF (all pixels ON)
*              rmmod  → fills OLED with 0x00 (all pixels OFF)
*
*  Hardware: Raspberry Pi 4B + SSD1306 OLED on I2C-1 (GPIO2=SDA, GPIO3=SCL)
*  Tested: Linux raspberrypi 5.4.51-v7l+
*******************************************************************************/

/* ── HEADERS ─────────────────────────────────────────────────────────────── */
#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>      /* kmalloc/kfree                                 */
#include <linux/i2c.h>       /* i2c_adapter, i2c_client, i2c_driver,
                                i2c_master_send, i2c_master_recv,
                                i2c_get_adapter, i2c_new_device,
                                i2c_add_driver, i2c_del_driver               */
#include <linux/delay.h>     /* msleep()                                      */
#include <linux/kernel.h>    /* pr_info                                        */


/* ── CONFIGURATION ───────────────────────────────────────────────────────── */
#define I2C_BUS_AVAILABLE    ( 1 )          /* I2C bus 1 on Raspberry Pi      */
#define SLAVE_DEVICE_NAME    ( "ETX_OLED" ) /* must match i2c_device_id name  */
#define SSD1306_SLAVE_ADDR   ( 0x3C )       /* SSD1306 I2C slave address      */


/* ── I2C ADAPTER AND CLIENT POINTERS ────────────────────────────────────── */
/*
 * etx_i2c_adapter: handle to I2C bus 1 (the physical bus on Raspberry Pi).
 * Filled by i2c_get_adapter(I2C_BUS_AVAILABLE) in init().
 *
 * etx_i2c_client_oled: handle to SSD1306 slave device on the bus.
 * Filled by i2c_new_device() in init().
 * All transfer functions use this client to address the right slave.
 */
static struct i2c_adapter *etx_i2c_adapter     = NULL;
static struct i2c_client  *etx_i2c_client_oled = NULL;


/* ── LOW-LEVEL I2C TRANSFER WRAPPERS ────────────────────────────────────── */

/*
 * I2C_Write() — send bytes to the I2C slave (SSD1306)
 *
 * i2c_master_send(client, buf, len):
 *   Sends buf[0..len-1] to the slave addressed by client.
 *   Internally handles START, slave address+W bit, data bytes, ACK, STOP.
 *   Returns: number of bytes sent on success, negative errno on failure.
 *
 * For SSD1306: always send [control_byte, data_byte] pairs.
 */
static int I2C_Write(unsigned char *buf, unsigned int len)
{
    int ret = i2c_master_send(etx_i2c_client_oled, buf, len);
    return ret;
}

/*
 * I2C_Read() — read bytes from the I2C slave
 *
 * i2c_master_recv(client, buf, len):
 *   Reads len bytes from slave into buf.
 *   Returns: number of bytes read on success, negative errno on failure.
 *
 * Not used in this example but provided as a reference.
 */
static int I2C_Read(unsigned char *out_buf, unsigned int len)
{
    int ret = i2c_master_recv(etx_i2c_client_oled, out_buf, len);
    return ret;
}


/* ── SSD1306-SPECIFIC FUNCTIONS ──────────────────────────────────────────── */

/*
 * SSD1306_Write() — send command or data to SSD1306 OLED
 *
 * SSD1306 protocol requires a CONTROL BYTE before each data byte:
 *   Control byte = 0x00 → next byte is a COMMAND
 *   Control byte = 0x40 → next byte is DATA (written to display RAM)
 *
 * @is_cmd: true=send as command, false=send as display data
 * @data:   the actual command or data byte to send
 *
 * Sends [control_byte, data_byte] as a 2-byte I2C transaction.
 */
static void SSD1306_Write(bool is_cmd, unsigned char data)
{
    unsigned char buf[2] = {0};
    int ret;

    /* Set control byte based on whether sending command or display data */
    buf[0] = is_cmd ? 0x00 : 0x40;   /* 0x00=command control, 0x40=data control */
    buf[1] = data;                     /* actual command or data byte             */

    ret = I2C_Write(buf, 2);          /* send [control, data] pair via I2C       */
}

/*
 * SSD1306_DisplayInit() — send initialization commands to SSD1306
 *
 * These are specific commands from the SSD1306 datasheet to:
 *   - Configure display clock, multiplex ratio, offset
 *   - Enable internal charge pump, set memory addressing mode
 *   - Configure contrast, pre-charge period, Vcomh level
 *   - Turn display ON in normal mode
 *
 * Don't need to understand each command — they're standard init from datasheet.
 * Called once in probe() before any data is displayed.
 */
static int SSD1306_DisplayInit(void)
{
    msleep(100);   /* wait 100ms for OLED to power up after connection */

    /* Standard SSD1306 initialization sequence */
    SSD1306_Write(true, 0xAE); /* display OFF                                */
    SSD1306_Write(true, 0xD5); /* set display clock divide ratio             */
    SSD1306_Write(true, 0x80); /* default clock ratio                        */
    SSD1306_Write(true, 0xA8); /* set multiplex ratio                        */
    SSD1306_Write(true, 0x3F); /* 64 COM lines (for 128x64 display)          */
    SSD1306_Write(true, 0xD3); /* set display offset                         */
    SSD1306_Write(true, 0x00); /* no offset                                  */
    SSD1306_Write(true, 0x40); /* set start line to 0                        */
    SSD1306_Write(true, 0x8D); /* charge pump setting                        */
    SSD1306_Write(true, 0x14); /* enable charge pump during display ON       */
    SSD1306_Write(true, 0x20); /* set memory addressing mode                 */
    SSD1306_Write(true, 0x00); /* horizontal addressing mode                 */
    SSD1306_Write(true, 0xA1); /* segment remap — col 127 mapped to seg 0    */
    SSD1306_Write(true, 0xC8); /* COM output scan direction — reversed       */
    SSD1306_Write(true, 0xDA); /* set COM pins hardware configuration        */
    SSD1306_Write(true, 0x12); /* alternative COM pin config                 */
    SSD1306_Write(true, 0x81); /* set contrast control                       */
    SSD1306_Write(true, 0x80); /* contrast = 128 (mid level)                 */
    SSD1306_Write(true, 0xD9); /* set pre-charge period                      */
    SSD1306_Write(true, 0xF1); /* phase 1=15 DCLK, phase 2=1 DCLK           */
    SSD1306_Write(true, 0xDB); /* set Vcomh deselect level                   */
    SSD1306_Write(true, 0x20); /* ~0.77 Vcc                                  */
    SSD1306_Write(true, 0xA4); /* resume to RAM content display              */
    SSD1306_Write(true, 0xA6); /* normal display (1=ON, 0=OFF)               */
    SSD1306_Write(true, 0x2E); /* deactivate scroll                          */
    SSD1306_Write(true, 0xAF); /* display ON in normal mode                  */

    return 0;
}

/*
 * SSD1306_Fill() — fill entire OLED display with a single byte value
 *
 * SSD1306 display RAM:
 *   8 pages × 128 segments × 8 bits = 8192 pixels = 1024 bytes
 *   128 * 8 = 1024 write operations → fills all pixels
 *
 * @data: 0xFF = all pixels ON (white), 0x00 = all pixels OFF (black)
 *
 * Each call to SSD1306_Write(false, data) sends one data byte to GDDRAM.
 * In horizontal addressing mode, address auto-increments after each byte.
 */
static void SSD1306_Fill(unsigned char data)
{
    unsigned int total = 128 * 8;   /* total bytes to fill entire GDDRAM      */
    unsigned int i;

    for (i = 0; i < total; i++) {
        SSD1306_Write(false, data); /* false=data (not command), fills GDDRAM */
    }
}


/* ── I2C DRIVER PROBE AND REMOVE ─────────────────────────────────────────── */

/*
 * etx_oled_probe() — called by I2C core when matching slave is found
 *
 * Triggered when: i2c_add_driver() finds a device whose name matches
 * etx_oled_id[] during kernel device enumeration.
 * Called ONCE when driver is loaded (insmod).
 *
 * @client : i2c_client handle to the slave (SSD1306)
 * @id     : matched i2c_device_id entry
 * Returns : 0=success, negative=failure
 *
 * Here: initialize OLED and fill with 0xFF (all pixels ON).
 */
static int etx_oled_probe(struct i2c_client *client)
{
    SSD1306_DisplayInit();   /* send init commands to set up OLED             */
    SSD1306_Fill(0xFF);      /* fill all pixels ON — entire display lights up */
    pr_info("OLED Probed!!!\n");
    return 0;
}

/*
 * etx_oled_remove() — called by I2C core when driver is unloaded
 *
 * Called ONCE when driver is removed (rmmod).
 * Here: clear the display by filling with 0x00 (all pixels OFF).
 *
 * @client : i2c_client handle to the slave
 * Returns : 0 always
 */
static void etx_oled_remove(struct i2c_client *client)
{
    SSD1306_Fill(0x00);    /* clear display — all pixels OFF                 */
    pr_info("OLED Removed!!!\n");
}


/* ── BOARD INFO STRUCTURE ────────────────────────────────────────────────── */
/*
 * oled_i2c_board_info — describes the I2C slave to the I2C core.
 *
 * I2C_BOARD_INFO(type, addr):
 *   type = "ETX_OLED" → matches SLAVE_DEVICE_NAME in driver + id_table
 *   addr = 0x3C       → SSD1306's 7-bit I2C address on the bus
 *
 * Used by i2c_new_device() to create an i2c_client for this slave.
 */
static struct i2c_board_info oled_i2c_board_info = {
    I2C_BOARD_INFO(SLAVE_DEVICE_NAME, SSD1306_SLAVE_ADDR)
};

/* ── I2C DEVICE ID TABLE ─────────────────────────────────────────────────── */
/*
 * etx_oled_id — list of slave device names this driver handles.
 * "ETX_OLED" must match the name in i2c_board_info (via I2C_BOARD_INFO macro).
 * Terminating empty entry {} is mandatory — same pattern as USB id_table.
 */
static const struct i2c_device_id etx_oled_id[] = {
    { SLAVE_DEVICE_NAME, 0 },  /* "ETX_OLED", 0=no private driver data */
    { }                         /* terminating entry — required          */
};
MODULE_DEVICE_TABLE(i2c, etx_oled_id);   /* expose to userspace for auto-loading */


/* ── I2C DRIVER STRUCTURE ────────────────────────────────────────────────── */
/*
 * etx_oled_driver — registers this as an I2C driver in the kernel.
 *
 * .driver.name must match SLAVE_DEVICE_NAME and i2c_board_info type.
 * .probe is called when a matching I2C device is found.
 * .remove is called when driver is unloaded.
 * .id_table links to the supported device list above.
 */
static struct i2c_driver etx_oled_driver = {
    .driver = {
        .name  = SLAVE_DEVICE_NAME,   /* "ETX_OLED" */
        .owner = THIS_MODULE,
    },
    .probe    = etx_oled_probe,   /* called on device match during insmod  */
    .remove   = etx_oled_remove,  /* called on driver removal during rmmod */
    .id_table = etx_oled_id,      /* list of supported slave devices       */
};


/* ── MODULE INIT ──────────────────────────────────────────────────────────── */
/*
 * etx_driver_init() — runs on: sudo insmod driver.ko
 *
 * Step 1: i2c_get_adapter(1)              → get handle to I2C bus 1
 * Step 2: i2c_new_device(adapter, &info)  → create client for SSD1306 slave
 * Step 3: i2c_add_driver(&etx_oled_driver)→ register driver → probe() called
 *
 * i2c_add_driver() traverses all known I2C devices, matches by name,
 * and calls etx_oled_probe() for the matched device.
 */
static int __init etx_driver_init(void)
{
    int ret = -1;

    /* Step 1: Get I2C bus 1 adapter — the physical I2C bus on Raspberry Pi */
    etx_i2c_adapter = i2c_get_adapter(I2C_BUS_AVAILABLE);

    if (etx_i2c_adapter != NULL) {

        /* Step 2: Create I2C client for SSD1306 on bus 1 at address 0x3C.
         * i2c_new_device() instantiates the slave from board info.
         * Returns i2c_client pointer — use for all subsequent transfers.
         * For kernel >= 5.2: replace with i2c_new_client_device().
         */
        etx_i2c_client_oled = i2c_new_client_device(etx_i2c_adapter, &oled_i2c_board_info);

        if (etx_i2c_client_oled != NULL) {

            /* Step 3: Register our I2C driver with the I2C subsystem.
             * i2c_add_driver() scans all I2C devices, matches "ETX_OLED",
             * and calls etx_oled_probe() for our SSD1306 client.
             */
            i2c_add_driver(&etx_oled_driver);
            ret = 0;   /* success */
        }

        /* Release our reference to the adapter — driver holds its own ref */
        i2c_put_adapter(etx_i2c_adapter);
    }

    pr_info("Driver Added!!!\n");
    return ret;
}


/* ── MODULE EXIT ──────────────────────────────────────────────────────────── */
/*
 * etx_driver_exit() — runs on: sudo rmmod driver
 *
 * i2c_del_driver() → calls etx_oled_remove() → clears OLED → unregisters driver
 * i2c_unregister_device() → removes the client we created with i2c_new_device()
 */
static void __exit etx_driver_exit(void)
{
    i2c_del_driver(&etx_oled_driver);           /* calls remove() → clears OLED */
    i2c_unregister_device(etx_i2c_client_oled); /* remove SSD1306 client        */
    pr_info("Driver Removed!!!\n");
}

module_init(etx_driver_init);
module_exit(etx_driver_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("EmbeTronicX <embetronicx@gmail.com>");
MODULE_DESCRIPTION("Simple I2C Driver - SSD1306 OLED");
MODULE_VERSION("1.34");

/* Complete flow summaru
 *
insmod driver.ko
  ├── i2c_get_adapter(1)          → get I2C bus 1 handle
  ├── i2c_new_device(adapter, &oled_info) → create i2c_client at 0x3C
  └── i2c_add_driver(&etx_oled_driver)
        └── etx_oled_probe() called
              ├── SSD1306_DisplayInit() → 27 init commands via I2C
              └── SSD1306_Fill(0xFF)   → 1024 data bytes → all pixels ON

rmmod driver
  └── i2c_del_driver()
        └── etx_oled_remove() called
              └── SSD1306_Fill(0x00) → 1024 data bytes → all pixels OFF
  └── i2c_unregister_device() → client removed
  */
