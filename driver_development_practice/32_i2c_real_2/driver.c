/***************************************************************************//**
*  \file       driver_bus.c
*  \details    I2C Bus Driver using i2c-gpio (kernel's bit-bang algorithm)
*
*  APPROACH:
*    Part 39: wrote ALL bit-bang code manually (300+ lines) — START, STOP,
*             ACK, NACK, data bits — all custom, missing clock stretching.
*    Part 40: provide 4 simple GPIO callbacks to i2c-gpio kernel subsystem.
*             i2c-gpio handles ALL of: START, STOP, ACK, NACK, clock stretching,
*             arbitration — fully tested production-quality code.
*
*  HARDWARE:
*    GPIO 20 → SCL (I2C clock line)
*    GPIO 21 → SDA (I2C data line)
*    Both connected to SSD1306 OLED I2C pins
*
*******************************************************************************/

#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/i2c.h>           /* i2c_adapter, i2c_del_adapter              */
#include <linux/delay.h>
#include <linux/kernel.h>
#include <linux/gpio.h>          /* gpio_request, gpio_free, gpio_set_value,
                                    gpio_get_value, gpio_direction_*           */
#include <linux/i2c-algo-bit.h>  /* struct i2c_algo_bit_data,
                                    i2c_bit_add_numbered_bus — NEW header      */

#define ADAPTER_NAME  "ETX_I2C_ADAPTER"

/* GPIO 20 = SCL (clock), GPIO 21 = SDA (data) — same as Part 39 */
#define SCL_GPIO  20
#define SDA_GPIO  21


/* ── GPIO CALLBACK FUNCTIONS ─────────────────────────────────────────────── */
/*
 * These 4 functions are the ONLY things you implement in this approach.
 * i2c-gpio calls them internally whenever it needs to:
 *   - Generate START: setsda(0) while setscl(1)
 *   - Generate STOP:  setsda(1) while setscl(1)
 *   - Send a bit:     setsda(bit), setscl(1), delay, setscl(0)
 *   - Read ACK/NACK:  setscl(1), getsda(), setscl(0)
 *   - Clock stretch:  getscl() until HIGH
 *
 * Compare to Part 39: you had to implement all this yourself manually!
 */

/*
 * ETX_I2C_Read_SCL() — read current state of SCL GPIO
 * Called by i2c-gpio for clock stretching detection.
 * Set as input to read the actual driven state on the wire.
 * Returns: 0=LOW, 1=HIGH
 */
static int ETX_I2C_Read_SCL(void *data)
{
    gpio_direction_input(SCL_GPIO);   /* switch to input to read */
    return gpio_get_value(SCL_GPIO);
}

/*
 * ETX_I2C_Read_SDA() — read current state of SDA GPIO
 * Called by i2c-gpio to read ACK/NACK from slave, or arbitration check.
 * Slave pulls SDA LOW for ACK, leaves HIGH for NACK.
 * Returns: 0=LOW (ACK), 1=HIGH (NACK/idle)
 */
static int ETX_I2C_Read_SDA(void *data)
{
    gpio_direction_input(SDA_GPIO);   /* switch to input to read */
    return gpio_get_value(SDA_GPIO);
}

/*
 * ETX_I2C_Set_SCL() — drive SCL GPIO to a specific state
 * Called by i2c-gpio to generate clock pulses.
 * @state: 1=HIGH (clock pulse), 0=LOW (between pulses)
 */
static void ETX_I2C_Set_SCL(void *data, int state)
{
    gpio_direction_output(SCL_GPIO, state);   /* switch to output */
    gpio_set_value(SCL_GPIO, state);          /* drive HIGH or LOW */
}

/*
 * ETX_I2C_Set_SDA() — drive SDA GPIO to a specific state
 * Called by i2c-gpio to send data bits, START condition, STOP condition.
 * @state: 1=HIGH, 0=LOW
 */
static void ETX_I2C_Set_SDA(void *data, int state)
{
    gpio_direction_output(SDA_GPIO, state);   /* switch to output */
    gpio_set_value(SDA_GPIO, state);          /* drive HIGH or LOW */
}


/* ── GPIO INIT / DEINIT ───────────────────────────────────────────────────── */

/*
 * ETX_I2C_Init() — validate and request GPIO 20 (SCL) and GPIO 21 (SDA)
 *
 * MUST be called BEFORE i2c_bit_add_numbered_bus().
 * i2c-gpio immediately starts using the GPIO callbacks — they must be valid.
 * Both GPIOs configured as outputs initially at HIGH (I2C idle state).
 */
static int ETX_I2C_Init(void)
{
    int ret = 0;

    pr_info("In %s\n", __func__);

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
            gpio_free(SCL_GPIO);   /* free already-requested SCL */
            ret = -1; break;
        }

        /* Set both lines HIGH initially — I2C bus idle state */
        gpio_direction_output(SCL_GPIO, 1);
        gpio_direction_output(SDA_GPIO, 1);

    } while(false);

    return ret;
}

/*
 * ETX_I2C_DeInit() — release both GPIO pins back to the system
 * Called in exit() BEFORE i2c_del_adapter().
 */
static void ETX_I2C_DeInit(void)
{
    gpio_free(SCL_GPIO);   /* release SCL GPIO */
    gpio_free(SDA_GPIO);   /* release SDA GPIO */
}


/* ── I2C BIT ALGORITHM STRUCTURE ─────────────────────────────────────────── */
/*
 * etx_bit_data — connects our GPIO callbacks to the i2c-gpio algorithm.
 *
 * i2c-gpio reads this structure and uses these callbacks to:
 *   - Generate START/STOP conditions (setscl + setsda combinations)
 *   - Send each address and data bit (setsda bit, pulse setscl)
 *   - Read ACK/NACK (getsda after releasing SDA)
 *   - Handle clock stretching (getscl polling until HIGH)
 *   - Handle arbitration (getsda while sending)
 *
 * .udelay = 5:
 *   Half clock cycle = 5 microseconds → full cycle = 10us → 100kHz (standard I2C)
 *   For 400kHz (fast mode): udelay = 2
 *   For SMBus: min 5us, max 50us
 *
 * .timeout = 100:
 *   100 jiffies timeout for clock stretching — if slave holds SCL LOW
 *   longer than this, transfer fails with error.
 *
 * ⚠️ This structure goes in adapter's .algo_data (NOT .algo like Part 39).
 *    i2c_bit_add_numbered_bus() reads .algo_data to set up the algorithm.
 */
struct i2c_algo_bit_data etx_bit_data = {
    .setsda  = ETX_I2C_Set_SDA,    /* drive SDA — called for every data bit   */
    .setscl  = ETX_I2C_Set_SCL,    /* drive SCL — called for every clock pulse */
    .getscl  = ETX_I2C_Read_SCL,   /* read SCL  — for clock stretching detect  */
    .getsda  = ETX_I2C_Read_SDA,   /* read SDA  — for ACK/NACK and arbitration */
    .udelay  = 5,                   /* 5us half-clock = 100kHz bus speed        */
    .timeout = 100,                 /* 100 jiffies clock-stretch timeout        */
};


/* ── I2C ADAPTER STRUCTURE ───────────────────────────────────────────────── */
/*
 * etx_i2c_adapter — represents our I2C bus to the kernel.
 *
 * KEY DIFFERENCES from Part 39:
 *   Part 39: .algo = &etx_i2c_algorithm  (our full custom algorithm)
 *   Part 40: .algo_data = &etx_bit_data  (our GPIO callbacks for i2c-gpio)
 *            .algo is NOT set — i2c_bit_add_numbered_bus() sets it automatically
 *                              to the built-in i2c-algo-bit algorithm
 *
 * .nr = 5 → requests bus number 5 → /sys/bus/i2c/devices/i2c-5
 *           client driver uses I2C_BUS_AVAILABLE = 5
 */
static struct i2c_adapter etx_i2c_adapter = {
    .owner      = THIS_MODULE,
    .class      = I2C_CLASS_HWMON | I2C_CLASS_SPD,   /* hardware monitor class */
    .name       = ADAPTER_NAME,                        /* "ETX_I2C_ADAPTER"     */
    .algo_data  = &etx_bit_data,  /* GPIO callbacks — i2c-gpio reads this      */
    .nr         = 5,              /* request bus 5 → i2c-5                     */
};


/* ── MODULE INIT ──────────────────────────────────────────────────────────── */
/*
 * etx_driver_init() — runs on: sudo insmod driver_bus.ko
 *
 * Step 1: ETX_I2C_Init()                → request + configure GPIOs 20 and 21
 * Step 2: i2c_bit_add_numbered_bus()    → register adapter with i2c-gpio
 *           → i2c-gpio reads .algo_data (etx_bit_data) for GPIO callbacks
 *           → i2c-gpio fills adapter's .algo with its own algorithm
 *           → creates /sys/bus/i2c/devices/i2c-5
 *
 * MUST load i2c-gpio kernel module first:
 *   sudo modprobe i2c-gpio
 *
 * vs Part 39:
 *   Part 39: i2c_add_numbered_adapter() — registered our own custom algorithm
 *   Part 40: i2c_bit_add_numbered_bus() — registers with i2c-gpio subsystem
 */
static int __init etx_driver_init(void)
{
    int ret = -1;

    /* Step 1: Request and configure GPIO 20 (SCL) and GPIO 21 (SDA) */
    ETX_I2C_Init();

    /*
     * Step 2: Register our adapter with the i2c-algo-bit subsystem.
     * i2c_bit_add_numbered_bus() does:
     *   - Reads etx_bit_data from adapter's .algo_data
     *   - Sets adapter's .algo to the built-in i2c-algo-bit algorithm
     *   - Registers adapter as i2c-5 (from .nr = 5)
     *   - Creates /sys/bus/i2c/devices/i2c-5/
     *
     * After this: any i2c_master_send() on bus 5 will call i2c-gpio's
     * master_xfer(), which in turn calls our setsda/setscl/getsda/getscl.
     */
    ret = i2c_bit_add_numbered_bus(&etx_i2c_adapter);

    pr_info("Bus Driver Added!!!\n");
    return ret;
}


/* ── MODULE EXIT ──────────────────────────────────────────────────────────── */
/*
 * etx_driver_exit() — runs on: sudo rmmod driver_bus
 *
 * Step 1: ETX_I2C_DeInit() → release GPIO 20 and 21
 * Step 2: i2c_del_adapter() → unregister from i2c subsystem
 *
 * Order: GPIOs FIRST, then adapter — same pattern as init (LIFO).
 * Client driver MUST be unloaded first (rmmod driver_client).
 */
static void __exit etx_driver_exit(void)
{
    ETX_I2C_DeInit();             /* release GPIOs                           */
    i2c_del_adapter(&etx_i2c_adapter);  /* unregister from i2c subsystem   */
    pr_info("Bus Driver Removed!!!\n");
}

module_init(etx_driver_init);
module_exit(etx_driver_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("EmbeTronicX <embetronicx@gmail.com>");
MODULE_DESCRIPTION("I2C Bus Driver using i2c-gpio (bit-bang)");
MODULE_VERSION("1.39");

/* Complete flow summary
 *
sudo modprobe i2c-gpio               # load kernel's i2c-gpio module first

sudo insmod driver_bus.ko
  ├── ETX_I2C_Init()
  │     ├── gpio_request(20, "SCL_GPIO") → claim SCL
  │     └── gpio_request(21, "SDA_GPIO") → claim SDA
  └── i2c_bit_add_numbered_bus()
        ├── reads etx_bit_data (our GPIO callbacks)
        ├── sets adapter .algo = i2c-algo-bit built-in algorithm
        └── creates /sys/bus/i2c/devices/i2c-5

sudo insmod driver_client.ko (I2C_BUS_AVAILABLE=5)
  └── probe() → SSD1306_DisplayInit() → SSD1306_Fill(0xFF)
        Each i2c_master_send() flows:
          → i2c-algo-bit master_xfer()
              → calls ETX_I2C_Set_SCL / ETX_I2C_Set_SDA (our GPIO callbacks)
              → generates real I2C START/DATA/ACK/STOP on GPIO 20/21 pins
              → OLED receives commands and lights up!

sudo rmmod driver_client
sudo rmmod driver_bus
  ├── ETX_I2C_DeInit() → gpio_free(20, 21)
  └── i2c_del_adapter() → removes i2c-5

sudo modprobe -r i2c-gpio
*/
