// SPDX-License-Identifier: GPL-2.0

#include <linux/usb.h>

struct usb_device *rust_helper_interface_to_usbdev(struct usb_interface *intf)
{
	return interface_to_usbdev(intf);
}
