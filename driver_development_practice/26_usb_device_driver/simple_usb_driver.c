#include <linux/usb.h>
#include <linux/module.h>

#define NEW_KERNEL 1

static struct usb_device *usb_dev;

static const struct usb_device_id sample_usb_id_table[] ={
	{USB_DEVICE(0x0781, 0x5567)},
	{}
};
MODULE_DEVICE_TABLE(usb, sample_usb_id_table);

static int sample_probe(struct usb_interface *interface, const struct usb_device_id *id){
	pr_info("pendrive is probed");

	struct usb_host_interface *iface_desc;
	iface_desc = interface->cur_altsetting;
	pr_info("pendrive interface %d now probed: (%04X:%04X)\n",
			iface_desc->desc.bInterfaceNumber,
			id->idVendor, id->idProduct);
	pr_info("ID->->bNumEndpoints: %02X\n",
			iface_desc->desc.bNumEndpoints);
	pr_info("ID->bInterfaceClass: %02X\n",
		       	iface_desc->desc.bInterfaceClass);
	
	struct usb_endpoint_descriptor *endpoint;
	int i;
	for(i = 0 ; i<iface_desc->desc.bNumEndpoints; i++){
		endpoint = &iface_desc->endpoint[i].desc;
		 pr_info("ED[%d]->bEndpointAddress: 0x%02X\n",
				 i, endpoint->bEndpointAddress);
		 pr_info( "ED[%d]->bmAttributes: 0x%02X\n",
				 i, endpoint->bmAttributes);
		 pr_info("ED[%d]->wMaxPacketSize: 0x%04X (%d)\n",
				 i, endpoint->wMaxPacketSize,
				 endpoint->wMaxPacketSize);
	}
	usb_dev = interface_to_usbdev(interface);

	return 0;
}

static void sample_disconnect(struct usb_interface *interface){
	usb_put_dev(usb_dev);
	pr_info("pendrive is removed\n");
}

static struct usb_driver sample_usb_driver = {
	.name = "sample",
	.probe = sample_probe,
	.disconnect = sample_disconnect,
	.id_table = sample_usb_id_table,
	.supports_autosuspend = 1,
};

#if (NEW_KERNEL == 0)
module_usb_driver(sample_usb_driver);

#else
static int __init sample_usb_init(void) {
	return usb_register(&sample_usb_driver);
}

static void __exit sample_usb_exit(void) {
	usb_deregister(&sample_usb_driver);
}
module_init(sample_usb_init);
module_exit(sample_usb_exit);
#endif

MODULE_LICENSE("GPL");
MODULE_AUTHOR("sample usb");
MODULE_DESCRIPTION("A simple usb driver example");

