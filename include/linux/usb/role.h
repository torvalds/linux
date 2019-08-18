// SPDX-License-Identifier: GPL-2.0

#ifndef __LINUX_USB_ROLE_H
#define __LINUX_USB_ROLE_H

#include <linux/device.h>

struct usb_role_switch;

enum usb_role {
	USB_ROLE_NONE,
	USB_ROLE_HOST,
	USB_ROLE_DEVICE,
};

typedef int (*usb_role_switch_set_t)(struct device *dev, enum usb_role role);
typedef enum usb_role (*usb_role_switch_get_t)(struct device *dev);

/**
 * struct usb_role_switch_desc - USB Role Switch Descriptor
 * @fwnode: The device node to be associated with the role switch
 * @usb2_port: Optional reference to the host controller port device (USB2)
 * @usb3_port: Optional reference to the host controller port device (USB3)
 * @udc: Optional reference to the peripheral controller device
 * @set: Callback for setting the role
 * @get: Callback for getting the role (optional)
 * @allow_userspace_control: If true userspace may change the role through sysfs
 *
 * @usb2_port and @usb3_port will point to the USB host port and @udc to the USB
 * device controller behind the USB connector with the role switch. If
 * @usb2_port, @usb3_port and @udc are included in the description, the
 * reference count for them should be incremented by the caller of
 * usb_role_switch_register() before registering the switch.
 */
struct usb_role_switch_desc {
	struct fwnode_handle *fwnode;
	struct device *usb2_port;
	struct device *usb3_port;
	struct device *udc;
	usb_role_switch_set_t set;
	usb_role_switch_get_t get;
	bool allow_userspace_control;
};

int usb_role_switch_set_role(struct usb_role_switch *sw, enum usb_role role);
enum usb_role usb_role_switch_get_role(struct usb_role_switch *sw);
struct usb_role_switch *usb_role_switch_get(struct device *dev);
void usb_role_switch_put(struct usb_role_switch *sw);

struct usb_role_switch *
usb_role_switch_register(struct device *parent,
			 const struct usb_role_switch_desc *desc);
void usb_role_switch_unregister(struct usb_role_switch *sw);

#endif /* __LINUX_USB_ROLE_H */
