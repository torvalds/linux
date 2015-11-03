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
enum usb_dr_mode of_usb_get_dr_mode_by_phy(struct device_node *phy_np);
bool of_usb_host_tpl_support(struct device_node *np);
int of_usb_update_otg_caps(struct device_node *np,
			struct usb_otg_caps *otg_caps);
#else
enum usb_dr_mode of_usb_get_dr_mode_by_phy(struct device_node *phy_np)
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
