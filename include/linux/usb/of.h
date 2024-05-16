// SPDX-License-Identifier: GPL-2.0
/*
 * OF helpers for usb devices.
 */

#ifndef __LINUX_USB_OF_H
#define __LINUX_USB_OF_H

#include <linux/usb.h>
#include <linux/usb/ch9.h>
#include <linux/usb/otg.h>
#include <linux/usb/phy.h>

struct usb_device;

#if IS_ENABLED(CONFIG_OF)
enum usb_dr_mode of_usb_get_dr_mode_by_phy(struct device_node *np, int arg0);
bool of_usb_host_tpl_support(struct device_node *np);
int of_usb_update_otg_caps(struct device_node *np,
			struct usb_otg_caps *otg_caps);
enum usb_port_connect_type usb_of_get_connect_type(struct usb_device *hub, int port1);
struct device_node *usb_of_get_device_node(struct usb_device *hub, int port1);
bool usb_of_has_combined_node(struct usb_device *udev);
struct device_node *usb_of_get_interface_node(struct usb_device *udev,
		u8 config, u8 ifnum);
struct device *usb_of_get_companion_dev(struct device *dev);
#else
static inline enum usb_dr_mode
of_usb_get_dr_mode_by_phy(struct device_node *np, int arg0)
{
	return USB_DR_MODE_UNKNOWN;
}
static inline bool of_usb_host_tpl_support(struct device_node *np)
{
	return false;
}
static inline int of_usb_update_otg_caps(struct device_node *np,
				struct usb_otg_caps *otg_caps)
{
	return 0;
}
static inline enum usb_port_connect_type
usb_of_get_connect_type(const struct usb_device *hub, int port1)
{
	return USB_PORT_CONNECT_TYPE_UNKNOWN;
}
static inline struct device_node *
usb_of_get_device_node(struct usb_device *hub, int port1)
{
	return NULL;
}
static inline bool usb_of_has_combined_node(struct usb_device *udev)
{
	return false;
}
static inline struct device_node *
usb_of_get_interface_node(struct usb_device *udev, u8 config, u8 ifnum)
{
	return NULL;
}
static inline struct device *usb_of_get_companion_dev(struct device *dev)
{
	return NULL;
}
#endif

#if IS_ENABLED(CONFIG_OF) && IS_ENABLED(CONFIG_USB_SUPPORT)
enum usb_phy_interface of_usb_get_phy_mode(struct device_node *np);
#else
static inline enum usb_phy_interface of_usb_get_phy_mode(struct device_node *np)
{
	return USBPHY_INTERFACE_MODE_UNKNOWN;
}

#endif

#endif /* __LINUX_USB_OF_H */
