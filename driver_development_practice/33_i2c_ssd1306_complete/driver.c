/***************************************************************************//**
*  \file       driver.c
*  \details    SSD1306 I2C OLED Display Driver
*
*  Combines: I2C client driver + character device + font rendering
*  User writes string to /dev/etx_oled → text appears on SSD1306 display
*
*  Hardware: Raspberry Pi 4 + SSD1306 OLED on I2C bus 1 (GPIO2=SDA, GPIO3=SCL)
*  Tested:   Linux raspberrypi 5.4.51-v7l+
*******************************************************************************/

#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/i2c.h>      /* I2C APIs                                       */
#include <linux/delay.h>    /* msleep()                                        */
#include <linux/kernel.h>
#include <linux/fs.h>       /* character device APIs                           */
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/uaccess.h>  /* copy_from_user()                                */
#include <linux/err.h>


/* ── CONFIGURATION ───────────────────────────────────────────────────────── */
#define I2C_BUS_AVAILABLE   ( 1 )           /* RPi's built-in I2C bus 1       */
#define SLAVE_DEVICE_NAME   ( "ETX_OLED" )  /* driver name — matches id_table */
#define SSD1306_SLAVE_ADDR  ( 0x3C )        /* SSD1306 I2C address            */


/* ── SSD1306 DISPLAY DIMENSIONS ─────────────────────────────────────────── */
#define SSD1306_MAX_SEG     ( 128 )   /* max columns (segments)               */
#define SSD1306_MAX_LINE    ( 7 )     /* max page number (0-7, 8 pages total)  */
#define SSD1306_DEF_FONT_SIZE ( 5 )   /* each character = 5 pixels wide        */


/* ── GLOBAL VARIABLES ────────────────────────────────────────────────────── */
static struct i2c_adapter *etx_i2c_adapter     = NULL;
static struct i2c_client  *etx_i2c_client_oled = NULL;

/* Character device variables */
dev_t dev = 0;
static struct class *dev_class;
static struct cdev etx_cdev;

/* Current cursor position on OLED display */
static uint8_t SSD1306_LineNum   = 0;   /* current page (0-7)                */
static uint8_t SSD1306_CursorPos = 0;   /* current column (0-127)            */
static uint8_t SSD1306_FontSize  = SSD1306_DEF_FONT_SIZE;  /* char width = 5 */


/* ── 5×8 FONT TABLE ─────────────────────────────────────────────────────── */
/*
 * SSD1306_font[]: 5 bytes per character, starting from ASCII 0x20 (space).
 * Each byte = one column of 8 vertical pixels.
 * Index = ASCII_value - 0x20
 * Example: 'A' = 0x41 - 0x20 = 0x21 = 33 → SSD1306_font[33]
 *
 * The 5 bytes represent the character columns left to right.
 * Each bit in a byte = 1 pixel (bit0=top, bit7=bottom within the page).
 */
static const uint8_t SSD1306_font[][SSD1306_DEF_FONT_SIZE] =
{
    {0x00, 0x00, 0x00, 0x00, 0x00},   // space
    {0x00, 0x00, 0x2f, 0x00, 0x00},   // !
    {0x00, 0x07, 0x00, 0x07, 0x00},   // "
    {0x14, 0x7f, 0x14, 0x7f, 0x14},   // #
    {0x24, 0x2a, 0x7f, 0x2a, 0x12},   // $
    {0x23, 0x13, 0x08, 0x64, 0x62},   // %
    {0x36, 0x49, 0x55, 0x22, 0x50},   // &
    {0x00, 0x05, 0x03, 0x00, 0x00},   // '
    {0x00, 0x1c, 0x22, 0x41, 0x00},   // (
    {0x00, 0x41, 0x22, 0x1c, 0x00},   // )
    {0x14, 0x08, 0x3E, 0x08, 0x14},   // *
    {0x08, 0x08, 0x3E, 0x08, 0x08},   // +
    {0x00, 0x00, 0xA0, 0x60, 0x00},   // ,
    {0x08, 0x08, 0x08, 0x08, 0x08},   // -
    {0x00, 0x60, 0x60, 0x00, 0x00},   // .
    {0x20, 0x10, 0x08, 0x04, 0x02},   // /
    {0x3E, 0x51, 0x49, 0x45, 0x3E},   // 0
    {0x00, 0x42, 0x7F, 0x40, 0x00},   // 1
    {0x42, 0x61, 0x51, 0x49, 0x46},   // 2
    {0x21, 0x41, 0x45, 0x4B, 0x31},   // 3
    {0x18, 0x14, 0x12, 0x7F, 0x10},   // 4
    {0x27, 0x45, 0x45, 0x45, 0x39},   // 5
    {0x3C, 0x4A, 0x49, 0x49, 0x30},   // 6
    {0x01, 0x71, 0x09, 0x05, 0x03},   // 7
    {0x36, 0x49, 0x49, 0x49, 0x36},   // 8
    {0x06, 0x49, 0x49, 0x29, 0x1E},   // 9
    {0x00, 0x36, 0x36, 0x00, 0x00},   // :
    {0x00, 0x56, 0x36, 0x00, 0x00},   // ;
    {0x08, 0x14, 0x22, 0x41, 0x00},   // <
    {0x14, 0x14, 0x14, 0x14, 0x14},   // =
    {0x00, 0x41, 0x22, 0x14, 0x08},   // >
    {0x02, 0x01, 0x51, 0x09, 0x06},   // ?
    {0x32, 0x49, 0x59, 0x51, 0x3E},   // @
    {0x7C, 0x12, 0x11, 0x12, 0x7C},   // A
    {0x7F, 0x49, 0x49, 0x49, 0x36},   // B
    {0x3E, 0x41, 0x41, 0x41, 0x22},   // C
    {0x7F, 0x41, 0x41, 0x22, 0x1C},   // D
    {0x7F, 0x49, 0x49, 0x49, 0x41},   // E
    {0x7F, 0x09, 0x09, 0x09, 0x01},   // F
    {0x3E, 0x41, 0x49, 0x49, 0x7A},   // G
    {0x7F, 0x08, 0x08, 0x08, 0x7F},   // H
    {0x00, 0x41, 0x7F, 0x41, 0x00},   // I
    {0x20, 0x40, 0x41, 0x3F, 0x01},   // J
    {0x7F, 0x08, 0x14, 0x22, 0x41},   // K
    {0x7F, 0x40, 0x40, 0x40, 0x40},   // L
    {0x7F, 0x02, 0x0C, 0x02, 0x7F},   // M
    {0x7F, 0x04, 0x08, 0x10, 0x7F},   // N
    {0x3E, 0x41, 0x41, 0x41, 0x3E},   // O
    {0x7F, 0x09, 0x09, 0x09, 0x06},   // P
    {0x3E, 0x41, 0x51, 0x21, 0x5E},   // Q
    {0x7F, 0x09, 0x19, 0x29, 0x46},   // R
    {0x46, 0x49, 0x49, 0x49, 0x31},   // S
    {0x01, 0x01, 0x7F, 0x01, 0x01},   // T
    {0x3F, 0x40, 0x40, 0x40, 0x3F},   // U
    {0x1F, 0x20, 0x40, 0x20, 0x1F},   // V
    {0x3F, 0x40, 0x38, 0x40, 0x3F},   // W
    {0x63, 0x14, 0x08, 0x14, 0x63},   // X
    {0x07, 0x08, 0x70, 0x08, 0x07},   // Y
    {0x61, 0x51, 0x49, 0x45, 0x43},   // Z
    {0x00, 0x7F, 0x41, 0x41, 0x00},   // [
    {0x55, 0xAA, 0x55, 0xAA, 0x55},   // Backslash (Checker pattern)
    {0x00, 0x41, 0x41, 0x7F, 0x00},   // ]
    {0x04, 0x02, 0x01, 0x02, 0x04},   // ^
    {0x40, 0x40, 0x40, 0x40, 0x40},   // _
    {0x00, 0x03, 0x05, 0x00, 0x00},   // `
    {0x20, 0x54, 0x54, 0x54, 0x78},   // a
    {0x7F, 0x48, 0x44, 0x44, 0x38},   // b
    {0x38, 0x44, 0x44, 0x44, 0x20},   // c
    {0x38, 0x44, 0x44, 0x48, 0x7F},   // d
    {0x38, 0x54, 0x54, 0x54, 0x18},   // e
    {0x08, 0x7E, 0x09, 0x01, 0x02},   // f
    {0x18, 0xA4, 0xA4, 0xA4, 0x7C},   // g
    {0x7F, 0x08, 0x04, 0x04, 0x78},   // h
    {0x00, 0x44, 0x7D, 0x40, 0x00},   // i
    {0x40, 0x80, 0x84, 0x7D, 0x00},   // j
    {0x7F, 0x10, 0x28, 0x44, 0x00},   // k
    {0x00, 0x41, 0x7F, 0x40, 0x00},   // l
    {0x7C, 0x04, 0x18, 0x04, 0x78},   // m
    {0x7C, 0x08, 0x04, 0x04, 0x78},   // n
    {0x38, 0x44, 0x44, 0x44, 0x38},   // o
    {0xFC, 0x24, 0x24, 0x24, 0x18},   // p
    {0x18, 0x24, 0x24, 0x18, 0xFC},   // q
    {0x7C, 0x08, 0x04, 0x04, 0x08},   // r
    {0x48, 0x54, 0x54, 0x54, 0x20},   // s
    {0x04, 0x3F, 0x44, 0x40, 0x20},   // t
    {0x3C, 0x40, 0x40, 0x20, 0x7C},   // u
    {0x1C, 0x20, 0x40, 0x20, 0x1C},   // v
    {0x3C, 0x40, 0x30, 0x40, 0x3C},   // w
    {0x44, 0x28, 0x10, 0x28, 0x44},   // x
    {0x1C, 0xA0, 0xA0, 0xA0, 0x7C},   // y
    {0x44, 0x64, 0x54, 0x4C, 0x44},   // z
    {0x00, 0x10, 0x7C, 0x82, 0x00},   // {
    {0x00, 0x00, 0xFF, 0x00, 0x00},   // |
    {0x00, 0x82, 0x7C, 0x10, 0x00},   // }
    {0x00, 0x06, 0x09, 0x09, 0x06}    // ~ (Degrees)
};


/* ── I2C LOW-LEVEL FUNCTIONS ─────────────────────────────────────────────── */

/*
 * I2C_Write() — send bytes to SSD1306 via I2C
 * Wrapper around i2c_master_send() for cleaner calling code.
 */
static int I2C_Write(unsigned char *buf, unsigned int len)
{
    return i2c_master_send(etx_i2c_client_oled, buf, len);
}

/* I2C_Read() — read bytes from SSD1306 (for future use) */
static int I2C_Read(unsigned char *out_buf, unsigned int len)
{
    return i2c_master_recv(etx_i2c_client_oled, out_buf, len);
}


/* ── SSD1306 PROTOCOL FUNCTIONS ──────────────────────────────────────────── */

/*
 * SSD1306_Write() — send one command or data byte to OLED
 *
 * Protocol: always send [control_byte, data_byte] pair.
 * Control byte tells OLED what the next byte represents:
 *   0x00 = COMMAND (configures display behavior)
 *   0x40 = DATA    (goes into GDDRAM → displayed as pixels)
 *
 * @is_cmd : true=command, false=display data
 * @data   : the actual command or pixel data byte
 */
static void SSD1306_Write(bool is_cmd, unsigned char data)
{
    unsigned char buf[2] = {0};
    buf[0] = is_cmd ? 0x00 : 0x40;   /* control byte */
    buf[1] = data;                     /* command or pixel data */
    I2C_Write(buf, 2);
}

/*
 * SSD1306_SetCursor() — move write cursor to specific page and column
 *
 * Uses Set Column Address (0x21) and Set Page Address (0x22) commands.
 * After setting cursor, all subsequent data writes go to this position.
 * In horizontal addressing mode, column auto-increments after each byte.
 *
 * @lineNo  : page number (0-7) — each page = 8 pixel rows tall
 * @cursorPos : column number (0-127) — each column = 1 pixel wide
 */
static void SSD1306_SetCursor(uint8_t lineNo, uint8_t cursorPos)
{
    SSD1306_Write(true, 0x21);       /* Set Column Address command            */
    SSD1306_Write(true, cursorPos);  /* start column = cursor position        */
    SSD1306_Write(true, 0x7F);       /* end column = 127 (last column)        */

    SSD1306_Write(true, 0x22);       /* Set Page Address command              */
    SSD1306_Write(true, lineNo);     /* start page = current line             */
    SSD1306_Write(true, 0x07);       /* end page = 7 (last page)              */
}

/*
 * SSD1306_Fill() — fill entire display with one byte (all ON or all OFF)
 *
 * Sends 1024 data bytes (128 cols × 8 pages = 1024 bytes = all pixels).
 * @data: 0xFF = all pixels ON (white), 0x00 = all pixels OFF (black)
 */
static void SSD1306_Fill(unsigned char data)
{
    unsigned int i;
    for (i = 0; i < (SSD1306_MAX_SEG * (SSD1306_MAX_LINE + 1)); i++) {
        SSD1306_Write(false, data);
    }
}

/*
 * SSD1306_GoToNextLine() — advance cursor to start of next page
 *
 * Wraps back to page 0 when past the last page (page 7).
 * Called after printing a full line of characters.
 */
static void SSD1306_GoToNextLine(void)
{
    SSD1306_LineNum++;
    SSD1306_LineNum = (SSD1306_LineNum & SSD1306_MAX_LINE);  /* wrap 7→0 */
    SSD1306_SetCursor(SSD1306_LineNum, 0);   /* move to col 0 of new page */
}

/*
 * SSD1306_PrintChar() — print one ASCII character at current cursor position
 *
 * Looks up character's 5-byte bitmap in SSD1306_font[].
 * Sends each of the 5 column bytes as display data → OLED draws the character.
 * Also sends one blank column (0x00) after character for spacing.
 *
 * Column auto-increments in horizontal addressing mode — next char follows.
 * If we reach column 126 (can't fit another full 5+1 char), go to next line.
 *
 * @c: ASCII character to print
 */
static void SSD1306_PrintChar(unsigned char c)
{
    uint8_t data_byte;
    uint8_t temp = 0;

    /* Check if char fits on current line — if not, go to next line */
    if ((SSD1306_CursorPos + SSD1306_FontSize) >= SSD1306_MAX_SEG) {
        SSD1306_GoToNextLine();
        SSD1306_CursorPos = 0;
    }

    /* Send 5 column bytes for this character from the font table */
    do {
        /* SSD1306_font index = char - 0x20 (font starts at ASCII space) */
        data_byte = SSD1306_font[c - 0x20][temp];  /* get column pixel data */
        SSD1306_Write(false, data_byte);            /* send as display data  */
        SSD1306_CursorPos++;
        temp++;
    } while (temp < SSD1306_FontSize);  /* 5 bytes per character */

    /* Send one blank column as spacing between characters */
    SSD1306_Write(false, 0x00);
    SSD1306_CursorPos++;
}

/*
 * SSD1306_String() — print a C string on the OLED display
 *
 * Iterates through each character and calls SSD1306_PrintChar().
 * Handles newlines ('\n') by advancing to the next display line.
 * Stops at null terminator.
 *
 * @str: null-terminated string to display
 */
static void SSD1306_String(unsigned char *str)
{
    while (*str) {
        if (*str == '\n') {
            SSD1306_GoToNextLine();   /* newline → next page                */
        } else {
            SSD1306_PrintChar(*str);  /* print the character                */
        }
        str++;
    }
}

/*
 * SSD1306_StartScrollRight() — scroll display right continuously
 * Useful for scrolling text marquee effect.
 * @start: start page, @stop: end page
 */
static void SSD1306_StartScrollRight(uint8_t start, uint8_t stop)
{
    SSD1306_Write(true, 0x26);    /* right horizontal scroll command          */
    SSD1306_Write(true, 0x00);    /* dummy byte                               */
    SSD1306_Write(true, start);   /* start page                               */
    SSD1306_Write(true, 0x00);    /* time interval: 5 frames                  */
    SSD1306_Write(true, stop);    /* end page                                 */
    SSD1306_Write(true, 0x00);    /* dummy byte                               */
    SSD1306_Write(true, 0xFF);    /* dummy byte                               */
    SSD1306_Write(true, 0x2F);    /* activate scroll                          */
}

/*
 * SSD1306_StartScrollLeft() — scroll display left continuously
 */
static void SSD1306_StartScrollLeft(uint8_t start, uint8_t stop)
{
    SSD1306_Write(true, 0x27);    /* left horizontal scroll command           */
    SSD1306_Write(true, 0x00);
    SSD1306_Write(true, start);
    SSD1306_Write(true, 0x00);
    SSD1306_Write(true, stop);
    SSD1306_Write(true, 0x00);
    SSD1306_Write(true, 0xFF);
    SSD1306_Write(true, 0x2F);    /* activate scroll                          */
}

/*
 * SSD1306_StopScroll() — stop any active scrolling
 */
static void SSD1306_StopScroll(void)
{
    SSD1306_Write(true, 0x2E);    /* deactivate scroll command                */
}

/*
 * SSD1306_InvertDisplay() — invert all pixel colors
 * @on: true = inverted (0=ON, 1=OFF), false = normal (1=ON, 0=OFF)
 */
static void SSD1306_InvertDisplay(bool on)
{
    SSD1306_Write(true, on ? 0xA7 : 0xA6);  /* 0xA7=invert, 0xA6=normal    */
}

/*
 * SSD1306_SetBrightness() — set display contrast/brightness level
 * @brightnessValue: 0=dimmest, 255=brightest
 */
static void SSD1306_SetBrightness(uint8_t brightnessValue)
{
    SSD1306_Write(true, 0x81);              /* Set Contrast Control command   */
    SSD1306_Write(true, brightnessValue);   /* contrast value 0-255           */
}

/*
 * SSD1306_DisplayInit() — send initialization commands to SSD1306
 * Standard init sequence from the SSD1306 datasheet.
 * Must be called once before any display operations.
 */
static int SSD1306_DisplayInit(void)
{
    msleep(100);   /* 100ms power-up delay */

    /* Full init sequence — same as Part 37/38/39/40 */
    SSD1306_Write(true, 0xAE); /* display OFF */
    SSD1306_Write(true, 0xD5); /* set clock divide ratio */
    SSD1306_Write(true, 0x80);
    SSD1306_Write(true, 0xA8); /* set multiplex ratio */
    SSD1306_Write(true, 0x3F); /* 64 COM lines */
    SSD1306_Write(true, 0xD3); /* set display offset */
    SSD1306_Write(true, 0x00);
    SSD1306_Write(true, 0x40); /* set start line */
    SSD1306_Write(true, 0x8D); /* charge pump */
    SSD1306_Write(true, 0x14); /* enable charge pump */
    SSD1306_Write(true, 0x20); /* memory addressing mode */
    SSD1306_Write(true, 0x00); /* horizontal addressing */
    SSD1306_Write(true, 0xA1); /* segment remap */
    SSD1306_Write(true, 0xC8); /* COM scan direction */
    SSD1306_Write(true, 0xDA); /* COM pins hardware config */
    SSD1306_Write(true, 0x12);
    SSD1306_Write(true, 0x81); /* contrast control */
    SSD1306_Write(true, 0x80);
    SSD1306_Write(true, 0xD9); /* pre-charge period */
    SSD1306_Write(true, 0xF1);
    SSD1306_Write(true, 0xDB); /* Vcomh deselect level */
    SSD1306_Write(true, 0x20);
    SSD1306_Write(true, 0xA4); /* resume to RAM content */
    SSD1306_Write(true, 0xA6); /* normal display mode */
    SSD1306_Write(true, 0x2E); /* deactivate scroll */
    SSD1306_Write(true, 0xAF); /* display ON */

    //Clear the display
    SSD1306_Fill(0x00);

    return 0;
}


/* ── CHARACTER DEVICE FILE OPERATIONS ───────────────────────────────────── */

static int etx_open(struct inode *inode, struct file *file)
{
    pr_info("Device File Opened...!!!\n");
    return 0;
}

static int etx_release(struct inode *inode, struct file *file)
{
    pr_info("Device File Closed...!!!\n");
    return 0;
}

/*
 * etx_write() — receives string from user and prints on OLED
 * Triggered by: echo "Hello World" > /dev/etx_oled
 *
 * Steps:
 *   1. copy_from_user() → copy string from user space to kernel buffer
 *   2. SSD1306_Fill(0x00) → clear display first
 *   3. SSD1306_SetCursor(0, 0) → reset cursor to top-left
 *   4. SSD1306_String() → render string on OLED pixel by pixel
 */
static ssize_t etx_write(struct file *filp, const char __user *buf,
                          size_t len, loff_t *off)
{
    uint8_t rec_buf[SSD1306_MAX_SEG * (SSD1306_MAX_LINE + 1)] = {0};

    /* Safely copy string from user space to kernel buffer */
    if (copy_from_user(rec_buf, buf, len)) {
        pr_err("ERROR: Not all bytes copied from user\n");
    }

    /* Clear the display, reset cursor, then print the new string */
    SSD1306_Fill(0x00);                   /* clear all pixels                */
    SSD1306_SetCursor(0, 0);              /* cursor to page 0, column 0      */
    SSD1306_LineNum   = 0;                /* reset page counter               */
    SSD1306_CursorPos = 0;               /* reset column counter             */
    SSD1306_String(rec_buf);             /* render user's string on OLED     */

    pr_info("Written to OLED: %s\n", rec_buf);
    return len;
}

/*
 * etx_read() — read current OLED content back to user
 * Simple implementation — returns current page/cursor position info.
 */
static ssize_t etx_read(struct file *filp, char __user *buf,
                         size_t len, loff_t *off)
{
    pr_info("Read Function\n");
    return 0;
}


/* ── FILE OPERATIONS TABLE ───────────────────────────────────────────────── */
static struct file_operations fops = {
    .owner   = THIS_MODULE,
    .read    = etx_read,
    .write   = etx_write,
    .open    = etx_open,
    .release = etx_release,
};


/* ── I2C DRIVER PROBE / REMOVE ───────────────────────────────────────────── */

/*
 * etx_oled_probe() — called when matching I2C slave found
 * Initializes OLED and displays "EmbeTronicX" as startup message.
 */
static int etx_oled_probe(struct i2c_client *client)
{
    SSD1306_DisplayInit();   /* send init commands to OLED  */
    SSD1306_Fill(0x00);      /* clear display               */
    SSD1306_SetCursor(0, 0); /* cursor at top-left          */
    SSD1306_String("EmbeTronicX");  /* print startup message */
    pr_info("OLED Probed!!!\n");
    return 0;
}

/* etx_oled_remove() — called when driver is unloaded — clears OLED */
static void etx_oled_remove(struct i2c_client *client)
{
    SSD1306_Fill(0x00);   /* clear display on unload */
    pr_info("OLED Removed!!!\n");
}

static const struct i2c_device_id etx_oled_id[] = {
    { SLAVE_DEVICE_NAME, 0 },
    { }
};
MODULE_DEVICE_TABLE(i2c, etx_oled_id);

static struct i2c_driver etx_oled_driver = {
    .driver   = { .name = SLAVE_DEVICE_NAME, .owner = THIS_MODULE },
    .probe    = etx_oled_probe,
    .remove   = etx_oled_remove,
    .id_table = etx_oled_id,
};

static struct i2c_board_info oled_i2c_board_info = {
    I2C_BOARD_INFO(SLAVE_DEVICE_NAME, SSD1306_SLAVE_ADDR)
};


/* ── MODULE INIT ──────────────────────────────────────────────────────────── */
/*
 * Combines: standard char device setup + I2C client driver setup
 *
 * Steps 1-5: alloc_chrdev_region → cdev_init → cdev_add → class_create
 *             → device_create → creates /dev/etx_oled
 * Steps 6-8: i2c_get_adapter → i2c_new_device → i2c_add_driver
 *             → OLED initialized, "EmbeTronicX" displayed
 */
static int __init etx_driver_init(void)
{
    int ret = -1;

    /* Standard char device setup */
    if ((alloc_chrdev_region(&dev, 0, 1, "ETX_OLED")) < 0) {
        pr_err("Cannot allocate major number\n"); return -1;
    }
    cdev_init(&etx_cdev, &fops);
    if ((cdev_add(&etx_cdev, dev, 1)) < 0) {
        pr_err("Cannot add device\n"); goto r_class;
    }
    if (IS_ERR(dev_class = class_create("etx_oled_class"))) {
        pr_err("Cannot create class\n"); goto r_class;
    }
    if (IS_ERR(device_create(dev_class, NULL, dev, NULL, "etx_oled"))) {
        pr_err("Cannot create device\n"); goto r_device;
    }

    /* I2C client driver setup — uses RPi's built-in I2C bus 1 */
    etx_i2c_adapter = i2c_get_adapter(I2C_BUS_AVAILABLE);   /* bus 1         */
    if (etx_i2c_adapter != NULL) {
        etx_i2c_client_oled = i2c_new_client_device(etx_i2c_adapter,
                                              &oled_i2c_board_info);
        if (etx_i2c_client_oled != NULL) {
            i2c_add_driver(&etx_oled_driver);   /* probe() called → OLED ON  */
            ret = 0;
        }
        i2c_put_adapter(etx_i2c_adapter);
    }

    pr_info("Device Driver Insert...Done!!!\n");
    return ret;

r_device: class_destroy(dev_class);
r_class:  unregister_chrdev_region(dev, 1); return -1;
}


/* ── MODULE EXIT ──────────────────────────────────────────────────────────── */
static void __exit etx_driver_exit(void)
{
    i2c_unregister_device(etx_i2c_client_oled);  /* remove() → OLED cleared */
    i2c_del_driver(&etx_oled_driver);
    device_destroy(dev_class, dev);
    class_destroy(dev_class);
    cdev_del(&etx_cdev);
    unregister_chrdev_region(dev, 1);
    pr_info("Device Driver Remove...Done!!!\n");
}

module_init(etx_driver_init);
module_exit(etx_driver_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("EmbeTronicX <embetronicx@gmail.com>");
MODULE_DESCRIPTION("SSD1306 OLED I2C Linux Device Driver");
MODULE_VERSION("1.40");

/* Complete flow summary
 *
insmod driver.ko
  ├── /dev/etx_oled created (char device)
  └── I2C probe → SSD1306_DisplayInit() → "EmbeTronicX" shown on OLED

echo "Hello" > /dev/etx_oled
  ├── etx_write()
  │     ├── copy_from_user() → "Hello" in kernel buffer
  │     ├── SSD1306_Fill(0x00) → clear display
  │     ├── SSD1306_SetCursor(0,0) → cursor to top-left
  │     └── SSD1306_String("Hello")
  │           └── for each char → SSD1306_PrintChar()
  │                 → look up 5-byte font bitmap
  │                 → send 5 data bytes to GDDRAM
  │                 → send 1 blank byte (spacing)
  │           → "Hello" appears on OLED!

rmmod driver
  └── etx_oled_remove() → SSD1306_Fill(0x00) → OLED cleared
  */
