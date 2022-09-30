/* SPDX-License-Identifier: GPL-2.0 */

#ifndef __LINUX_USB_ONBOARD_HUB_H
#define __LINUX_USB_ONBOARD_HUB_H

struct usb_device;
struct list_head;

#if IS_ENABLED(CONFIG_USB_ONBOARD_HUB)
void onboard_hub_create_pdevs(struct usb_device *parent_hub, struct list_head *pdev_list);
void onboard_hub_destroy_pdevs(struct list_head *pdev_list);
#else
static inline void onboard_hub_create_pdevs(struct usb_device *parent_hub,
					    struct list_head *pdev_list) {}
static inline void onboard_hub_destroy_pdevs(struct list_head *pdev_list) {}
#endif

#endif /* __LINUX_USB_ONBOARD_HUB_H */
