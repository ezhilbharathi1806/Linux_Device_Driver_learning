/***************************************************************************//**
*  \file       driver_bus.c
*  \details    Dummy I2C Bus Driver
*
*  PURPOSE: Intercepts ALL I2C transfers from client drivers and prints them.
*  Does NOT perform any real hardware I2C signaling (no START/STOP/ACK on wire).
*  Used to understand what data flows between client driver and bus driver.
*
*  When client calls: i2c_master_send() → this driver's master_xfer() is called
*  When client calls: i2c_smbus_*()    → this driver's smbus_xfer() is called
*
*******************************************************************************/

#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/i2c.h>       /* i2c_adapter, i2c_algorithm, i2c_msg,
                                i2c_add_adapter, i2c_del_adapter,
                                I2C_CLASS_HWMON, I2C_FUNC_* flags            */
#include <linux/delay.h>
#include <linux/kernel.h>

#define ADAPTER_NAME "ETX_I2C_ADAPTER"   /* visible in sysfs as adapter name */


/* ── ALGORITHM FUNCTIONS ─────────────────────────────────────────────────── */

/*
 * etx_func() — reports what this bus driver/algorithm supports
 *
 * Called by the I2C core to check capabilities before using the bus.
 * Returns a bitmask of I2C_FUNC_* flags.
 *
 * If a client calls a transfer type NOT listed here → kernel returns -EOPNOTSUPP.
 * For our dummy driver: we claim to support basic I2C + all common SMBus types.
 *
 * In real bus driver: return only what your hardware actually supports.
 */
static u32 etx_func(struct i2c_adapter *adapter)
{
    return (I2C_FUNC_I2C             |   /* basic I2C read/write             */
            I2C_FUNC_SMBUS_QUICK     |   /* SMBus quick command              */
            I2C_FUNC_SMBUS_BYTE      |   /* SMBus byte send/receive          */
            I2C_FUNC_SMBUS_BYTE_DATA |   /* SMBus byte data r/w              */
            I2C_FUNC_SMBUS_WORD_DATA |   /* SMBus word data r/w              */
            I2C_FUNC_SMBUS_BLOCK_DATA);  /* SMBus block data r/w             */
}

/*
 * etx_i2c_xfer() — master transfer function (core of bus driver)
 *
 * Called automatically whenever client driver calls:
 *   i2c_master_send(), i2c_master_recv(), i2c_transfer()
 *
 * @adap : pointer to our i2c_adapter structure
 * @msgs : array of i2c_msg structures — each represents one transaction
 * @num  : number of messages in the msgs array
 * Returns: 0=success (normally returns num for real drivers), negative=error
 *
 * In a REAL bus driver, this function would:
 *   1. Acquire bus lock
 *   2. Generate I2C START condition on the wire
 *   3. Send slave address + R/W bit
 *   4. Send/receive data bytes one by one
 *   5. Generate ACK/NACK for each byte
 *   6. Generate STOP condition
 *   7. Release bus lock
 *   8. Return number of messages processed
 *
 * In our DUMMY driver: just print each message's content — no wire activity.
 *
 * struct i2c_msg fields:
 *   .addr  = 7-bit slave address (e.g., 0x3C for SSD1306)
 *   .flags = I2C_M_RD for read, 0 for write, etc.
 *   .len   = number of bytes in this message
 *   .buf   = pointer to data buffer (bytes to send or receive)
 */
static s32 etx_i2c_xfer(struct i2c_adapter *adap,
                         struct i2c_msg *msgs, int num)
{
    int i;

    for (i = 0; i < num; i++) {
        int j;
        struct i2c_msg *msg_temp = &msgs[i];   /* pointer to current message */

        /* Print message header: which message, function name, address, length */
        pr_info("[Count: %d] [%s]: [Addr = 0x%x] [Len = %d] [Data] = ",
                i, __func__,
                msg_temp->addr,    /* slave I2C address                      */
                msg_temp->len);    /* number of data bytes                   */

        /* Print each data byte in the message */
        for (j = 0; j < msg_temp->len; j++) {
            /* pr_cont continues the same dmesg line (no newline) */
            pr_cont("[0x%02x] ", msg_temp->buf[j]);
        }
        /* pr_cont with no args just adds a newline at end of byte list */
    }

    return 0;   /* success — real driver returns 'num' (messages processed) */
}

/*
 * etx_smbus_xfer() — SMBus transfer function
 *
 * Called automatically whenever client driver calls:
 *   i2c_smbus_read_byte(), i2c_smbus_write_byte_data(), etc.
 *
 * @adap       : our adapter
 * @addr       : slave address
 * @flags      : I2C flags
 * @read_write : I2C_SMBUS_READ or I2C_SMBUS_WRITE
 * @command    : SMBus command byte
 * @size       : transaction type (I2C_SMBUS_BYTE, I2C_SMBUS_WORD_DATA, etc.)
 * @data       : union containing byte/word/block data
 * Returns: 0=success, negative=error
 *
 * If smbus_xfer is NULL in algorithm → I2C core tries to emulate SMBus
 * using regular I2C messages via master_xfer().
 * Providing it explicitly is more efficient.
 *
 * Dummy implementation: just print that we were called.
 */
static s32 etx_smbus_xfer(struct i2c_adapter *adap,
                           u16 addr,
                           unsigned short flags,
                           char read_write,
                           u8 command,
                           int size,
                           union i2c_smbus_data *data)
{
    pr_info("In %s\n", __func__);
    return 0;
}


/* ── I2C ALGORITHM STRUCTURE ─────────────────────────────────────────────── */
/*
 * etx_i2c_algorithm — links our transfer functions to the algorithm structure.
 *
 * .master_xfer  → called for all i2c_master_send/recv/transfer calls
 * .smbus_xfer   → called for all i2c_smbus_* calls
 * .functionality → called to query supported operations
 *
 * In real bus driver: master_xfer would contain actual I2C state machine
 * that generates START/ADDRESS/DATA/ACK/STOP on the hardware I2C pins.
 */
static struct i2c_algorithm etx_i2c_algorithm = {
    .smbus_xfer   = etx_smbus_xfer,   /* handle SMBus-style transfers       */
    .master_xfer  = etx_i2c_xfer,     /* handle raw I2C transfers           */
    .functionality = etx_func,         /* report supported capabilities      */
};


/* ── I2C ADAPTER STRUCTURE ───────────────────────────────────────────────── */
/*
 * etx_i2c_adapter — represents our virtual/dummy I2C bus.
 *
 * .owner  = THIS_MODULE  → standard ownership
 * .class  = I2C_CLASS_HWMON → type of devices on this bus (hardware monitor)
 *                             Other options: I2C_CLASS_DDC, I2C_CLASS_SPD
 * .algo   = &etx_i2c_algorithm → links to our transfer functions
 * .name   = "ETX_I2C_ADAPTER" → shown in sysfs: /sys/bus/i2c/devices/i2c-X/name
 *
 * Note: .nr is NOT set here → we use i2c_add_adapter() which assigns
 * a dynamic bus number. Set .nr and use i2c_add_numbered_adapter() instead
 * if you need a specific bus number (e.g., .nr = 5 → i2c-5).
 */
static struct i2c_adapter etx_i2c_adapter = {
    .owner  = THIS_MODULE,
    .class  = I2C_CLASS_HWMON,          /* type of I2C class                */
    .algo   = &etx_i2c_algorithm,       /* our transfer functions           */
    .name   = ADAPTER_NAME,             /* "ETX_I2C_ADAPTER"                */
};


/* ── MODULE INIT ──────────────────────────────────────────────────────────── */
/*
 * etx_driver_init() — runs on: sudo insmod driver_bus.ko
 *
 * i2c_add_adapter(&etx_i2c_adapter):
 *   Registers our adapter with the I2C subsystem.
 *   Assigns a FREE dynamic bus number (e.g., i2c-11).
 *   Creates: /sys/bus/i2c/devices/i2c-11/
 *   The bus is now available for client drivers to use.
 *
 * After loading:
 *   tree /sys/bus/i2c/ → shows new entry (e.g., i2c-11)
 *   Note the number → update I2C_BUS_AVAILABLE in client driver!
 */
static int __init etx_driver_init(void)
{
    int ret = -1;

    /* Register our dummy adapter with the I2C subsystem */
    ret = i2c_add_adapter(&etx_i2c_adapter);
    if (ret < 0) {
        pr_err("Failed to add I2C adapter: %d\n", ret);
        return ret;
    }

    pr_info("Bus Driver Added!!!\n");
    return ret;   /* 0 = success */
}


/* ── MODULE EXIT ──────────────────────────────────────────────────────────── */
/*
 * etx_driver_exit() — runs on: sudo rmmod driver_bus
 *
 * MUST unload client driver FIRST before unloading bus driver!
 * If client still loaded → i2c_del_adapter may hang or cause errors.
 *
 * i2c_del_adapter(&etx_i2c_adapter):
 *   Unregisters our adapter from the I2C subsystem.
 *   Removes /sys/bus/i2c/devices/i2c-11/.
 */
static void __exit etx_driver_exit(void)
{
    i2c_del_adapter(&etx_i2c_adapter);   /* remove bus from I2C subsystem   */
    pr_info("Bus Driver Removed!!!\n");
}

module_init(etx_driver_init);
module_exit(etx_driver_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("EmbeTronicX <embetronicx@gmail.com>");
MODULE_DESCRIPTION("Simple I2C Bus driver explanation");
MODULE_VERSION("1.35");

/* Complete flow summary
 *
sudo insmod driver_bus.ko
  └── i2c_add_adapter() → /sys/bus/i2c/devices/i2c-11/ created

tree /sys/bus/i2c/ → find "i2c-11" (number varies!)
Update client driver: #define I2C_BUS_AVAILABLE (11)

sudo insmod driver_client.ko
  ├── i2c_get_adapter(11)      → gets our dummy bus adapter
  ├── i2c_new_device(adapter, &oled_board_info) → create client at 0x3C
  └── i2c_add_driver() → etx_oled_probe() called
          └── SSD1306_DisplayInit() → 27 × SSD1306_Write()
                → 27 × I2C_Write(buf, 2)
                   → 27 × i2c_master_send()
                      → 27 × etx_i2c_xfer() in BUS DRIVER!
                         → prints each [Addr=0x3c][Len=2][0x00][0xAE] etc.

dmesg shows:
  [Count:0][etx_i2c_xfer]: [Addr=0x3c][Len=2][Data]=[0x00][0xae]  ← display OFF cmd
  [Count:0][etx_i2c_xfer]: [Addr=0x3c][Len=2][Data]=[0x00][0xd5]  ← clock config
  ... 27 lines total ...
  OLED Probed!!!
  Client Driver Added!!!

sudo rmmod driver_client  → FIRST (client depends on bus)
sudo rmmod driver_bus     → THEN  (bus can now be removed)
*/
