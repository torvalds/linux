/* SPDX-License-Identifier: GPL-2.0 */

#ifndef __LINUX_USB_ONBOARD_DEV_H
#define __LINUX_USB_ONBOARD_DEV_H

struct usb_device;
struct list_head;

#if IS_ENABLED(CONFIG_USB_ONBOARD_DEV)
void onboard_dev_create_pdevs(struct usb_device *parent_dev, struct list_head *pdev_list);
void onboard_dev_destroy_pdevs(struct list_head *pdev_list);
#else
static inline void onboard_dev_create_pdevs(struct usb_device *parent_dev,
					    struct list_head *pdev_list) {}
static inline void onboard_dev_destroy_pdevs(struct list_head *pdev_list) {}
#endif

#endif /* __LINUX_USB_ONBOARD_DEV_H */
