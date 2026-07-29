/***************************************************************************//**
*  \file       usb_driver.c
*  \details    Simple USB driver — prints interface and endpoint descriptors
*              when a matching USB device is plugged in.
*
*  Tested with kernel 5.3.0-42-generic
*******************************************************************************/

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/usb.h>    /* all USB driver APIs — usb_driver, usb_register,
                             usb_device_id, USB_DEVICE, MODULE_DEVICE_TABLE  */


/* ── CONFIGURATION ───────────────────────────────────────────────────────── */
/*
 * IS_NEW_METHOD_USED:
 *   0 → use old method: explicit __init/__exit + usb_register/usb_deregister
 *   1 → use new method: single module_usb_driver() macro (kernel >= 3.3)
 *
 * New method is simpler and preferred. Old method shown for learning purposes.
 */
#define IS_NEW_METHOD_USED  ( 1 )

/*
 * USB_VENDOR_ID and USB_PRODUCT_ID — replace these with YOUR device's IDs.
 * Find them by running: lsusb
 *   Bus 001 Device 008: ID 22d9:2764 → VID=0x22d9, PID=0x2764
 *
 * Bus 001 Device 003: ID 04ca:00bd Lite-On Technology Corp. Dell Wireless Device
 * Bus 001 Device 011: ID 04e8:6863 Samsung Electronics Co., Ltd Galaxy series, misc. (tethering mode)

 * These are used in the id_table to tell the kernel which device to match.
 */
#define USB_VENDOR_ID       ( 0x04ca )      /* Vendor ID of target USB device  */
#define USB_PRODUCT_ID      ( 0x00bd )      /* Product ID of target USB device */


/* ── DESCRIPTOR PRINTING MACROS ─────────────────────────────────────────── */

/*
 * PRINT_USB_INTERFACE_DESCRIPTOR(i):
 * Prints all fields of the USB interface descriptor.
 * Called in probe() to show what interface was matched.
 *
 * Key fields:
 *   bInterfaceNumber = which interface (0, 1, 2...)
 *   bNumEndpoints    = how many endpoints this interface has
 *   bInterfaceClass  = device class (0xff = vendor specific)
 */
#define PRINT_USB_INTERFACE_DESCRIPTOR( i )                         \
{                                                                   \
    pr_info("USB_INTERFACE_DESCRIPTOR:\n");                         \
    pr_info("-----------------------------\n");                     \
    pr_info("bLength: 0x%x\n", i.bLength);           /* descriptor size    */ \
    pr_info("bDescriptorType: 0x%x\n", i.bDescriptorType); /* type=0x04    */ \
    pr_info("bInterfaceNumber: 0x%x\n", i.bInterfaceNumber); /* interface# */ \
    pr_info("bAlternateSetting: 0x%x\n", i.bAlternateSetting); /* alt set  */ \
    pr_info("bNumEndpoints: 0x%x\n", i.bNumEndpoints); /* endpoint count   */ \
    pr_info("bInterfaceClass: 0x%x\n", i.bInterfaceClass); /* class code   */ \
    pr_info("bInterfaceSubClass: 0x%x\n", i.bInterfaceSubClass);            \
    pr_info("bInterfaceProtocol: 0x%x\n", i.bInterfaceProtocol);            \
    pr_info("iInterface: 0x%x\n", i.iInterface); /* string descriptor idx   */ \
    pr_info("\n");                                                  \
}

/*
 * PRINT_USB_ENDPOINT_DESCRIPTOR(e):
 * Prints all fields of one USB endpoint descriptor.
 * Called in a loop in probe() for each endpoint.
 *
 * Key fields:
 *   bEndpointAddress = direction (bit7: 0=OUT, 1=IN) + endpoint number
 *     0x81 = IN endpoint 1, 0x01 = OUT endpoint 1, 0x82 = IN endpoint 2
 *   bmAttributes     = transfer type (0=Control, 1=Isochronous, 2=Bulk, 3=Interrupt)
 *   wMaxPacketSize   = max bytes per transaction
 *   bInterval        = polling interval (for interrupt endpoints)
 */
#define PRINT_USB_ENDPOINT_DESCRIPTOR( e )                          \
{                                                                   \
    pr_info("USB_ENDPOINT_DESCRIPTOR:\n");                          \
    pr_info("------------------------\n");                          \
    pr_info("bLength: 0x%x\n", e.bLength);                          \
    pr_info("bDescriptorType: 0x%x\n", e.bDescriptorType); /* 0x05 */ \
    pr_info("bEndPointAddress: 0x%x\n", e.bEndpointAddress); /* IN/OUT  */ \
    pr_info("bmAttributes: 0x%x\n", e.bmAttributes); /* transfer type   */ \
    pr_info("wMaxPacketSize: 0x%x\n", e.wMaxPacketSize); /* max pkt size */ \
    pr_info("bInterval: 0x%x\n", e.bInterval); /* polling interval (ms)  */ \
    pr_info("\n");                                                  \
}


/* ── PROBE FUNCTION ───────────────────────────────────────────────────────── */
/*
 * etx_usb_probe() — called by USB core when a matching device is plugged in
 *
 * Called when: USB device with USB_VENDOR_ID + USB_PRODUCT_ID is connected.
 * The USB core looks up id_table, finds a match, and calls this function.
 *
 * @interface : the USB interface that matched (one device can have many)
 * @id        : the usb_device_id entry that matched (has VID, PID, etc.)
 *
 * Returns: 0 = we accept and will manage this device
 *          negative error code = we reject this device
 *
 * In this simple driver: just print the interface + endpoint descriptors.
 * In real driver: allocate URBs(USB Request Blocks), set up endpoints for actual data transfer.
 */
static int etx_usb_probe(struct usb_interface *interface,
                          const struct usb_device_id *id)
{
        unsigned int i;
        unsigned int endpoints_count;

        /*
         * interface->cur_altsetting:
         * Points to the currently active alternate setting of this interface.
         * Contains the interface descriptor AND array of endpoint descriptors.
         * "cur" = current, "altsetting" = alternate setting (usually 0).
         */
        struct usb_host_interface *iface_desc = interface->cur_altsetting;

        /*
         * dev_info vs pr_info:
         * dev_info(&interface->dev, ...) prints with device context info:
         *   "EmbeTronicX USB Driver 1-2:1.0: USB Driver Probed: ..."
         *   This includes bus number + device address — very useful for debugging
         *   multiple USB devices.
         */
        dev_info(&interface->dev,
                 "USB Driver Probed: Vendor ID : 0x%02x,\tProduct ID : 0x%02x\n",
                 id->idVendor,   /* VID that matched from id_table */
                 id->idProduct); /* PID that matched from id_table */

        /* Get number of endpoints from the interface descriptor */
        endpoints_count = iface_desc->desc.bNumEndpoints;

        /* Print the interface descriptor fields */
        PRINT_USB_INTERFACE_DESCRIPTOR(iface_desc->desc);

        /*
         * Loop through each endpoint and print its descriptor.
         * iface_desc->endpoint[i].desc = usb_endpoint_descriptor for endpoint i
         * Each endpoint is either IN (device→host) or OUT (host→device).
         * bmAttributes tells the transfer type: Bulk(2), Interrupt(3), etc.
         */
        for (i = 0; i < endpoints_count; i++) {
            PRINT_USB_ENDPOINT_DESCRIPTOR(iface_desc->endpoint[i].desc);
        }

        return 0;   /* 0 = success, we are managing this device */
}


/* ── DISCONNECT FUNCTION ─────────────────────────────────────────────────── */
/*
 * etx_usb_disconnect() — called when USB device is removed/unplugged
 *
 * @interface : the USB interface being disconnected
 *
 * In real driver: free URBs, release endpoints, cleanup resources.
 * This simple driver has nothing to free (probe allocated nothing).
 */
static void etx_usb_disconnect(struct usb_interface *interface)
{
        dev_info(&interface->dev, "USB Driver Disconnected\n");
}


/* ── DEVICE ID TABLE ─────────────────────────────────────────────────────── */
/*
 * etx_usb_table — list of USB devices this driver supports.
 *
 * USB_DEVICE(vid, pid) creates a usb_device_id that matches
 * a device with EXACTLY the given Vendor ID and Product ID.
 *
 * The empty {} at the end is the required terminating sentinel entry.
 * Without it, the kernel would read past the end of the array.
 *
 * If your driver supports multiple devices, add more entries:
 *   { USB_DEVICE(VID1, PID1) },
 *   { USB_DEVICE(VID2, PID2) },
 *   { }
 */
const struct usb_device_id etx_usb_table[] = {
        { USB_DEVICE(USB_VENDOR_ID, USB_PRODUCT_ID) },   /* our target device */
        { }   /* terminating sentinel — MUST be last                           */
};

/*
 * MODULE_DEVICE_TABLE(usb, etx_usb_table):
 * Exports id_table to the kernel's module loading infrastructure.
 * Enables AUTOMATIC loading of this driver when matching device is plugged in.
 * Without this, driver must be manually loaded with insmod before plugging device.
 * The udev hotplug system reads this table to decide which module to load.
 */
MODULE_DEVICE_TABLE(usb, etx_usb_table);


/* ── USB DRIVER STRUCTURE ────────────────────────────────────────────────── */
/*
 * etx_usb_driver — registers our driver with the USB core/subsystem.
 *
 * Minimum required fields: name + probe + disconnect + id_table.
 * If id_table is missing → probe() is NEVER called (kernel requirement).
 */
static struct usb_driver etx_usb_driver = {
        .name       = "EmbeTronicX USB Driver",  /* must be unique across USB drivers */
        .probe      = etx_usb_probe,             /* called on device connect           */
        .disconnect = etx_usb_disconnect,        /* called on device disconnect        */
        .id_table   = etx_usb_table,            /* devices we handle (VID+PID table)  */
};


/* ── MODULE INIT / EXIT ───────────────────────────────────────────────────── */

#if (IS_NEW_METHOD_USED == 0)

/*
 * NEW METHOD (kernel >= 3.3) — just ONE macro replaces everything below.
 *
 * module_usb_driver(etx_usb_driver):
 *   Internally calls usb_register() in module_init
 *   and usb_deregister() in module_exit.
 *   No __init, __exit, module_init(), module_exit() needed.
 *   Each module can only use this macro ONCE.
 */
module_usb_driver(etx_usb_driver);

#else

/*
 * OLD METHOD (kernel < 3.3) — explicit init/exit functions.
 *
 * usb_register(&etx_usb_driver):
 *   Registers our USB driver with the USB core.
 *   After this: kernel uses id_table to match devices.
 *   dmesg shows: "registered new interface driver EmbeTronicX USB Driver"
 *
 * usb_deregister(&etx_usb_driver):
 *   Removes driver from USB core.
 *   Any connected devices get disconnect() called.
 *   dmesg shows: "deregistering interface driver EmbeTronicX USB Driver"
 */
static int __init etx_usb_init(void)
{
        return usb_register(&etx_usb_driver);   /* register with USB core */
}

static void __exit etx_usb_exit(void)
{
        usb_deregister(&etx_usb_driver);        /* unregister from USB core */
}

module_init(etx_usb_init);
module_exit(etx_usb_exit);
#endif

MODULE_LICENSE("GPL");
MODULE_AUTHOR("EmbeTronicX <embetronicx@gmail.com>");
MODULE_DESCRIPTION("A simple device driver - USB Driver");
MODULE_VERSION("1.30");

/* Complete flow summary
sudo insmod usb_driver.ko
  └── usb_register() → registered with USB core
        → dmesg: "registered new interface driver EmbeTronicX USB Driver"

Plug in USB device (VID=0x22d9, PID=0x2764)
  ├── USB core detects connection
  ├── reads device descriptor → gets VID+PID
  ├── checks all registered drivers' id_tables
  ├── finds match in etx_usb_table
  └── calls etx_usb_probe(interface, id)
        ├── dev_info("USB Driver Probed: VID=0x22d9, PID=0x2764")
        ├── PRINT_USB_INTERFACE_DESCRIPTOR → prints all interface fields
        └── for each endpoint → PRINT_USB_ENDPOINT_DESCRIPTOR

Unplug USB device
  └── calls etx_usb_disconnect(interface)
        └── dev_info("USB Driver Disconnected")

sudo rmmod usb_driver
  └── usb_deregister() → removed from USB core
        → dmesg: "deregistering interface driver EmbeTronicX USB Driver"
*/
