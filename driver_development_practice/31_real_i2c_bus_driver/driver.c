/***************************************************************************//**
*  \file       driver_bus.c
*  \details    Real I2C Bus Driver using GPIO bit-banging
*
*  Implements actual I2C protocol in software (bit-banging):
*    GPIO 20 = SCL (clock)
*    GPIO 21 = SDA (data)
*
*  The master_xfer() function generates real I2C signals on the GPIO pins.
*  This replaces Part 38's dummy that just printed data.
*
*  LIMITATIONS (for demonstration only):
*    - READ not implemented (ETX_I2C_Read_Byte is TODO)
*    - No clock stretching, no arbitration
*    - Write-only bus driver
*
*******************************************************************************/

#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/i2c.h>     /* i2c_adapter, i2c_algorithm, i2c_msg            */
#include <linux/delay.h>   /* usleep_range()                                  */
#include <linux/kernel.h>
#include <linux/gpio.h>    /* gpio_request, gpio_free, gpio_direction_output,
                              gpio_direction_input, gpio_set_value, gpio_get_value */

#define ADAPTER_NAME  "ETX_I2C_ADAPTER"

/* ── GPIO PIN DEFINITIONS ─────────────────────────────────────────────────── */
#define SCL_GPIO  20   /* Raspberry Pi GPIO 20 → I2C clock line (SCL)         */
#define SDA_GPIO  21   /* Raspberry Pi GPIO 21 → I2C data line  (SDA)         */

/*
 * I2C_DELAY: 5-10 microsecond delay between signal transitions.
 * Required for proper I2C timing — each bit must be held stable.
 * usleep_range() is preferred over udelay() — allows kernel scheduler to run.
 */
#define I2C_DELAY  usleep_range(5, 10)


/* ── GPIO HELPER FUNCTIONS ───────────────────────────────────────────────── */

/* Read current state of SCL — set as input first to read */
static bool ETX_I2C_Read_SCL(void)
{
    gpio_direction_input(SCL_GPIO);
    return gpio_get_value(SCL_GPIO);
}

/* Read current state of SDA — set as input first to read.
 * Used for ACK/NACK detection (slave drives SDA low for ACK) */
static bool ETX_I2C_Read_SDA(void)
{
    gpio_direction_input(SDA_GPIO);
    return gpio_get_value(SDA_GPIO);
}

/* Drive SCL LOW — set as output and write 0 */
static void ETX_I2C_Clear_SCL(void)
{
    gpio_direction_output(SCL_GPIO, 0);
    gpio_set_value(SCL_GPIO, 0);
}

/* Drive SDA LOW — set as output and write 0 */
static void ETX_I2C_Clear_SDA(void)
{
    gpio_direction_output(SDA_GPIO, 0);
    gpio_set_value(SDA_GPIO, 0);
}

/* Drive SCL HIGH — set as output and write 1 */
static void ETX_I2C_Set_SCL(void)
{
    gpio_direction_output(SCL_GPIO, 1);
    gpio_set_value(SCL_GPIO, 1);
}

/* Drive SDA HIGH — set as output and write 1 */
static void ETX_I2C_Set_SDA(void)
{
    gpio_direction_output(SDA_GPIO, 1);
    gpio_set_value(SDA_GPIO, 1);
}


/* ── GPIO INIT/DEINIT ─────────────────────────────────────────────────────── */

/*
 * ETX_I2C_Init() — validate and request GPIO 20 (SCL) and GPIO 21 (SDA)
 *
 * Called at START of every master_xfer() transaction.
 * Configures both pins as outputs initially (start condition needs output).
 * Uses do-while(false) as a structured error-exit block.
 * Returns: 0=success, -1=failure
 */
static int ETX_I2C_Init(void)
{
    int ret = 0;

    do {   /* break-on-error pattern */
        if (gpio_is_valid(SCL_GPIO) == false) {
            pr_err("SCL GPIO %d is not valid\n", SCL_GPIO);
            ret = -1; break;
        }
        if (gpio_is_valid(SDA_GPIO) == false) {
            pr_err("SDA GPIO %d is not valid\n", SDA_GPIO);
            ret = -1; break;
        }
        if (gpio_request(SCL_GPIO, "SCL_GPIO") < 0) {
            pr_err("ERROR: SCL GPIO %d request\n", SCL_GPIO);
            ret = -1; break;
        }
        if (gpio_request(SDA_GPIO, "SDA_GPIO") < 0) {
            pr_err("ERROR: SDA GPIO %d request\n", SDA_GPIO);
            gpio_free(SCL_GPIO);   /* free already requested SCL */
            ret = -1; break;
        }

        /* Configure as outputs, initial = HIGH (I2C idle state) */
        gpio_direction_output(SCL_GPIO, 1);   /* SCL HIGH = idle */
        gpio_direction_output(SDA_GPIO, 1);   /* SDA HIGH = idle */

    } while(false);

    return ret;
}

/*
 * ETX_I2C_DeInit() — release both GPIOs after transaction completes.
 * Called at END of every master_xfer() transaction.
 * Frees the GPIO claims so they can be used again next transaction.
 */
static void ETX_I2C_DeInit(void)
{
    gpio_free(SCL_GPIO);
    gpio_free(SDA_GPIO);
}


/* ── I2C PROTOCOL SIGNAL GENERATORS ─────────────────────────────────────── */

/*
 * ETX_I2C_Start() — generate I2C START condition on the wire
 *
 * START = SDA goes HIGH→LOW while SCL is HIGH
 *
 * Timing diagram:
 *   SDA: ______|‾‾‾‾|____
 *   SCL: _____|‾‾‾‾|____
 *              ↑
 *         SDA falls while SCL is HIGH = START
 */
static void ETX_I2C_Sart(void)
{
    ETX_I2C_Set_SDA();    /* SDA HIGH */
    ETX_I2C_Set_SCL();    /* SCL HIGH */
    I2C_DELAY;
    ETX_I2C_Clear_SDA();  /* SDA LOW while SCL HIGH = START condition */
    I2C_DELAY;
    ETX_I2C_Clear_SCL();  /* SCL LOW — ready to send data */
    I2C_DELAY;
}

/*
 * ETX_I2C_Stop() — generate I2C STOP condition on the wire
 *
 * STOP = SDA goes LOW→HIGH while SCL is HIGH
 *
 * Timing diagram:
 *   SDA: ____|‾‾‾‾‾‾
 *   SCL:  ___|‾‾‾‾‾‾
 *              ↑
 *         SDA rises while SCL is HIGH = STOP
 */
static void ETX_I2C_Stop(void)
{
    ETX_I2C_Clear_SDA();  /* SDA LOW */
    I2C_DELAY;
    ETX_I2C_Set_SCL();    /* SCL HIGH */
    I2C_DELAY;
    ETX_I2C_Set_SDA();    /* SDA HIGH while SCL HIGH = STOP condition */
    I2C_DELAY;
    ETX_I2C_Clear_SCL();  /* SCL LOW — bus idle */
}

/*
 * ETX_I2C_Read_NACK_ACK() — read slave's ACK or NACK response
 *
 * After sending each byte, master releases SDA and pulses SCL once.
 * Slave pulls SDA LOW for ACK, leaves HIGH for NACK.
 *
 * Returns: 1=ACK received (success), 0=NACK (failure)
 */
static int ETX_I2C_Read_NACK_ACK(void)
{
    int ret = 1;   /* assume ACK */

    I2C_DELAY;
    ETX_I2C_Set_SCL();   /* pulse SCL HIGH — slave drives SDA for ACK/NACK */
    I2C_DELAY;

    if (ETX_I2C_Read_SDA()) {
        /* SDA HIGH = NACK — slave did not acknowledge */
        ret = 0;
    }
    /* SDA LOW = ACK — slave acknowledged (ret stays 1) */

    ETX_I2C_Clear_SCL();   /* SCL LOW — release ACK clock pulse */
    return ret;
}

/*
 * ETX_I2C_Send_Addr() — send 7-bit slave address + R/W bit
 *
 * Each bit sent MSB first: set SDA to bit value, pulse SCL HIGH then LOW.
 * 8th bit: is_read=true sets R/W=1 (read), false sets R/W=0 (write).
 * Then read ACK/NACK from slave.
 *
 * @byte    : 7-bit slave address (e.g., 0x3C)
 * @is_read : true=read transaction, false=write transaction
 * Returns: 0=ACK received, -1=NACK (no slave at this address)
 */
static int ETX_I2C_Send_Addr(u8 byte, bool is_read)
{
    int ret  = -1;
    u8  bit;
    u8  i    = 0;
    u8  size = 7;   /* 7-bit address */

    /* Send 7 address bits, MSB first */
    for (i = 0; i < size; i++) {
        /* Extract bit: shift to get bit (size - i - 1), then mask with 0x01 */
        bit = ((byte >> (size - (i + 1))) & 0x01);
        (bit) ? ETX_I2C_Set_SDA() : ETX_I2C_Clear_SDA();  /* set SDA to bit value */
        I2C_DELAY;
        ETX_I2C_Set_SCL();    /* SCL HIGH — slave reads SDA */
        I2C_DELAY;
        ETX_I2C_Clear_SCL();  /* SCL LOW — prepare for next bit */
    }

    /* Send R/W bit (8th bit): 0=write, 1=read */
    (is_read) ? ETX_I2C_Set_SDA() : ETX_I2C_Clear_SDA();
    I2C_DELAY;
    ETX_I2C_Set_SCL();
    I2C_DELAY;
    ETX_I2C_Clear_SCL();
    I2C_DELAY;

    /* Read ACK/NACK — did slave respond? */
    if (ETX_I2C_Read_NACK_ACK()) {
        ret = 0;   /* ACK = slave found and accepted */
    }

    return ret;
}

/*
 * ETX_I2C_Send_Byte() — send 8 data bits to slave, MSB first
 *
 * Each bit: set SDA, pulse SCL HIGH then LOW.
 * After 8 bits: read ACK/NACK from slave.
 *
 * Returns: 0=ACK (byte accepted), -1=NACK
 */
static int ETX_I2C_Send_Byte(u8 byte)
{
    int ret  = -1;
    u8  bit;
    u8  i    = 0;
    u8  size = 7;   /* 8 bits total: bits 7 to 0 */

    /* Send 8 bits MSB first (bit 7 down to bit 0) */
    for (i = 0; i <= size; i++) {
        bit = ((byte >> (size - i)) & 0x01);   /* extract current bit */
        (bit) ? ETX_I2C_Set_SDA() : ETX_I2C_Clear_SDA();
        I2C_DELAY;
        ETX_I2C_Set_SCL();    /* SCL HIGH — slave reads SDA */
        I2C_DELAY;
        ETX_I2C_Clear_SCL();  /* SCL LOW — next bit */
    }

    /* Read slave's ACK/NACK after the 8th data bit */
    if (ETX_I2C_Read_NACK_ACK()) {
        ret = 0;   /* ACK = byte accepted by slave */
    }

    return ret;
}

/*
 * ETX_I2C_Read_Byte() — read 8 data bits from slave
 * NOT implemented yet — TODO for full bidirectional I2C support.
 */
static int ETX_I2C_Read_Byte(u8 *byte)
{
    int ret = 0;
    /* TODO: Implement read — switch SDA to input, read 8 bits, send ACK */
    return ret;
}

/*
 * ETX_I2C_Send() — send a complete I2C write transaction
 *
 * Sequence: Send address → send each data byte one by one.
 * @slave_addr : 7-bit I2C address of slave (e.g., 0x3C)
 * @buf        : data bytes to send
 * @len        : number of bytes
 * Returns: 0=all sent successfully, -1=error
 */
static int ETX_I2C_Send(u8 slave_addr, u8 *buf, u16 len)
{
    int ret = 0;
    u16 num = 0;

    do {   /* break-on-error */
        /* Send slave address with W bit (is_read=false) */
        if (ETX_I2C_Send_Addr(slave_addr, false) < 0) {
            pr_err("ERROR: ETX_I2C_Send_Byte - Slave Addr\n");
            ret = -1;
            break;
        }

        /* Send each data byte, stop if any fails */
        for (num = 0; num < len; num++) {
            if (ETX_I2C_Send_Byte(buf[num]) < 0) {
                pr_err("ERROR: ETX_I2C_Send_Byte - [Data=0x%02x]\n", buf[num]);
                ret = -1;
                break;
            }
        }
    } while(false);

    return ret;
}

/*
 * ETX_I2C_Read() — receive bytes from slave
 * NOT implemented yet — TODO.
 */
static int ETX_I2C_Read(u8 slave_addr, u8 *buf, u16 len)
{
    int ret = 0;
    /* TODO: Implement I2C read */
    return ret;
}


/* ── I2C ALGORITHM FUNCTIONS ─────────────────────────────────────────────── */

/* Report supported capabilities */
static u32 etx_func(struct i2c_adapter *adapter)
{
    return (I2C_FUNC_I2C              |
            I2C_FUNC_SMBUS_QUICK      |
            I2C_FUNC_SMBUS_BYTE       |
            I2C_FUNC_SMBUS_BYTE_DATA  |
            I2C_FUNC_SMBUS_WORD_DATA  |
            I2C_FUNC_SMBUS_BLOCK_DATA);
}

/*
 * etx_i2c_xfer() — master transfer function (called by all i2c_master_send/recv)
 *
 * THIS IS WHERE BIT-BANGING HAPPENS:
 *   1. ETX_I2C_Init()  → claim GPIOs 20 and 21
 *   2. ETX_I2C_Start() → generate START on the wire
 *   3. For each message: ETX_I2C_Send() → address + data bytes on wire
 *   4. ETX_I2C_Stop()  → generate STOP on the wire
 *   5. ETX_I2C_DeInit()→ release GPIOs
 *
 * vs Part 38 dummy: just pr_info each message (no wire activity)
 * vs Part 39 real:  generates actual voltage transitions on GPIO pins
 *
 * Returns: number of messages successfully processed (or 0 on error)
 */
static s32 etx_i2c_xfer(struct i2c_adapter *adap,
                         struct i2c_msg *msgs, int num)
{
    int i;
    s32 ret = 0;

    do {   /* break-on-error */
        /* Step 1: Claim GPIO 20 (SCL) and GPIO 21 (SDA) */
        if (ETX_I2C_Init() < 0) {
            pr_err("ERROR: ETX_I2C_Init\n");
            break;
        }

        /* Step 2: Generate I2C START condition on the wire */
        ETX_I2C_Sart();

        /* Step 3: Process each message — send address + data bytes */
        for (i = 0; i < num; i++) {
            struct i2c_msg *msg_temp = &msgs[i];

            if (ETX_I2C_Send(msg_temp->addr, msg_temp->buf, msg_temp->len) < 0) {
                ret = 0;   /* failure — stop processing */
                break;
            }
            ret++;   /* count successfully sent messages */
        }
    } while(false);

    /* Step 4: Generate I2C STOP condition — always, even on error */
    ETX_I2C_Stop();

    /* Step 5: Release GPIO 20 and GPIO 21 back to system */
    ETX_I2C_DeInit();

    return ret;   /* number of messages processed */
}

/* SMBus transfer — not implemented, just logs */
static s32 etx_smbus_xfer(struct i2c_adapter *adap, u16 addr,
                           unsigned short flags, char read_write,
                           u8 command, int size, union i2c_smbus_data *data)
{
    pr_info("In %s\n", __func__);
    /* TODO: Implement SMBus transfers for smbus_* client APIs */
    return 0;
}


/* ── I2C ALGORITHM AND ADAPTER STRUCTURES ────────────────────────────────── */

static struct i2c_algorithm etx_i2c_algorithm = {
    .smbus_xfer   = etx_smbus_xfer,   /* handles smbus_* client API calls  */
    .master_xfer  = etx_i2c_xfer,     /* handles i2c_master_send/recv      */
    .functionality = etx_func,         /* reports what we support           */
};

/*
 * etx_i2c_adapter — our bus adapter structure.
 *
 * KEY CHANGE from Part 38:
 *   .nr = 5 → request specific bus number 5
 *   Used with i2c_add_numbered_adapter() instead of i2c_add_adapter()
 *   → creates /sys/bus/i2c/devices/i2c-5 (guaranteed, if 5 is free)
 *   → client driver uses I2C_BUS_AVAILABLE = 5 (no guessing the number)
 */
static struct i2c_adapter etx_i2c_adapter = {
    .owner  = THIS_MODULE,
    .class  = I2C_CLASS_HWMON,
    .algo   = &etx_i2c_algorithm,
    .name   = ADAPTER_NAME,
    .nr     = 5,   /* request bus number 5 → i2c-5 */
};


/* ── MODULE INIT / EXIT ───────────────────────────────────────────────────── */

static int __init etx_driver_init(void)
{
    int ret = -1;

    /*
     * i2c_add_numbered_adapter():
     *   Registers adapter with the SPECIFIC bus number in adap->nr (= 5).
     *   Returns 0 if bus 5 is free, negative if bus 5 is already taken.
     *
     * vs i2c_add_adapter() (Part 38):
     *   Assigns ANY free bus number dynamically — you have to check sysfs.
     */
    ret = i2c_add_numbered_adapter(&etx_i2c_adapter);

    pr_info("Bus Driver Added!!!\n");
    return ret;
}

static void __exit etx_driver_exit(void)
{
    i2c_del_adapter(&etx_i2c_adapter);   /* remove from I2C subsystem */
    pr_info("Bus Driver Removed!!!\n");
}

module_init(etx_driver_init);
module_exit(etx_driver_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("EmbeTronicX <embetronicx@gmail.com>");
MODULE_DESCRIPTION("Real I2C Bus driver - GPIO bit-banging");
MODULE_VERSION("1.37");

/* Complete flow summary
 *
insmod driver_bus.ko
  └── i2c_add_numbered_adapter(nr=5)
        → creates /sys/bus/i2c/devices/i2c-5

insmod driver_client.ko (I2C_BUS_AVAILABLE=5)
  └── probe() → SSD1306_DisplayInit() → SSD1306_Fill(0xFF)
        → each i2c_master_send() triggers:
        → etx_i2c_xfer()
              ├── ETX_I2C_Init()    → gpio_request(20,21), set as outputs
              ├── ETX_I2C_Start()   → SDA↓ while SCL HIGH on wire
              ├── ETX_I2C_Send(0x3C, [0x00,0xAE], 2)
              │     ├── ETX_I2C_Send_Addr(0x3C, write)
              │     │     → 7 bits + W bit sent bit-by-bit, read ACK
              │     └── ETX_I2C_Send_Byte(0x00)  → control byte
              │           → ETX_I2C_Send_Byte(0xAE) → command byte
              ├── ETX_I2C_Stop()   → SDA↑ while SCL HIGH on wire
              └── ETX_I2C_DeInit() → gpio_free(20,21)
        → OLED displays all pixels ON!

rmmod driver_client → SSD1306_Fill(0x00) → OLED clears
rmmod driver_bus
*/
