/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2020-2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2022 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef __LINUX_USB_REDRIVER_H
#define __LINUX_USB_REDRIVER_H

#include <linux/list.h>
#include <linux/device.h>
#include <linux/of.h>

/*
 * design rules,
 * [a] assume pullup operation happen in kretprobe.
 *     in kretprobe function, mutex lock is not allowed;
 *     in kretprobe function, schedule_work() is allowed;
 * [b] this is core driver which service lower redriver and upper user.
 * [c] redriver must probe early than user, or not user will defer probe.
 * [d] redriver can rmmod only when there is no user bind to it.
 * [e] if user rmmod, redirver will change to default state.
 * [f] if redriver module insmod after new change and build,
 *     user module also need insmod to work.
 * [g] when a redriver probe, set to disable state, all control from user.
 *     as ssphy have no eud function which don't need to keep working.
 * [h] user should be ssphy, but current user is dwc3,
 *     as seem some redriver have termination issue,
 *     it need to do pullup operation from controller driver.
 */


#define ORIENTATION_CC1		0
#define ORIENTATION_CC2		1

/**
 * struct usb_redriver - present a redriver chip
 * @list: link all redriver chips
 * @of_node: redriver chip device tree node
 * @release_usb_lanes: put redriver into 2/4 lanes display mode
 * @notify_connect: cable connect
 * @notify_disconnect: cable disconnect
 * @gadget_pullup_enter: operation when enter gadget pullup function
 * @gadget_pullup_exit: operation when exit gadget pullup function
 * @host_powercycle: workaround for host otg case
 * @unbind, change to default state when user unbind it
 * @bounded,  bound to user or not
 */
struct usb_redriver {
	struct list_head list;
	struct device_node *of_node;

	int (*release_usb_lanes)(struct usb_redriver *ur, int ort, int num);
	int (*notify_connect)(struct usb_redriver *ur, int ort);
	int (*notify_disconnect)(struct usb_redriver *ur);
	int (*gadget_pullup_enter)(struct usb_redriver *ur, int is_on);
	int (*gadget_pullup_exit)(struct usb_redriver *ur, int is_on);
	int (*host_powercycle)(struct usb_redriver *ur);
	void (*unbind)(struct usb_redriver *ur);

	bool bounded;
};

#if IS_ENABLED(CONFIG_USB_REDRIVER)
int usb_add_redriver(struct usb_redriver *ur);
int usb_remove_redriver(struct usb_redriver *ur);

struct usb_redriver *usb_get_redriver_by_phandle(
		const struct device_node *np,
		const char *phandle_name, int index);
void usb_put_redriver(struct usb_redriver *ur);
void usb_redriver_release_lanes(struct usb_redriver *ur, int ort, int num);
void usb_redriver_notify_connect(struct usb_redriver *ur, int ort);
void usb_redriver_notify_disconnect(struct usb_redriver *ur);
void usb_redriver_gadget_pullup_enter(struct usb_redriver *ur, int is_on);
void usb_redriver_gadget_pullup_exit(struct usb_redriver *ur, int is_on);
void usb_redriver_host_powercycle(struct usb_redriver *ur);

#else

static inline int usb_add_redriver(struct usb_redriver *ur)
{
	return 0;
}

static inline struct usb_redriver *usb_get_redriver_by_phandle(
		const struct device_node *np,
		const char *phandle_name, int index)
{
	return NULL;
}

static inline int usb_remove_redriver(struct usb_redriver *ur)
{
	return 0;
}

#define usb_put_redriver(ur) do {} while (0)

#define usb_redriver_release_lanes(ur, ort, num) do {} while (0)
#define usb_redriver_notify_connect(ur, ort) do {} while (0)
#define usb_redriver_notify_disconnect(ur) do {} while (0)
#define usb_redriver_gadget_pullup_enter(ur, is_on) do {} while (0)
#define usb_redriver_gadget_pullup_exit(ur, is_on) do {} while (0)
#define usb_redriver_host_powercycle(ur) do {} while (0)

#endif

#endif /*__LINUX_USB_REDRIVER_H */
