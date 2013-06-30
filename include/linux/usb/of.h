/*
 * OF helpers for usb devices.
 *
 * This file is released under the GPLv2
 */

#ifndef __LINUX_USB_OF_H
#define __LINUX_USB_OF_H

#include <linux/usb/ch9.h>
#include <linux/usb/otg.h>
#include <linux/usb/phy.h>

#if IS_ENABLED(CONFIG_OF)
enum usb_dr_mode of_usb_get_dr_mode(struct device_node *np);
enum usb_device_speed of_usb_get_maximum_speed(struct device_node *np);
#else
static inline enum usb_dr_mode of_usb_get_dr_mode(struct device_node *np)
{
	return USB_DR_MODE_UNKNOWN;
}

static inline enum usb_device_speed
of_usb_get_maximum_speed(struct device_node *np)
{
	return USB_SPEED_UNKNOWN;
}
#endif

#if IS_ENABLED(CONFIG_OF) && IS_ENABLED(CONFIG_USB_PHY)
enum usb_phy_interface of_usb_get_phy_mode(struct device_node *np);
#else
static inline enum usb_phy_interface of_usb_get_phy_mode(struct device_node *np)
{
	return USBPHY_INTERFACE_MODE_UNKNOWN;
}

#endif

#endif /* __LINUX_USB_OF_H */
