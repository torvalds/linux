/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2021-2023, Qualcomm Innovation Center, Inc. All rights reserved.
 */
#ifndef __LINUX_USB_REPEATER_H
#define __LINUX_USB_REPEATER_H

#include <linux/errno.h>
#include <linux/device.h>
#include <linux/types.h>

struct usb_repeater  {
	struct device		*dev;
	const char		*label;
	unsigned int		flags;

	struct list_head	head;
	int	(*reset)(struct usb_repeater *x, bool bring_out_of_reset);
	int	(*init)(struct usb_repeater *x);
	int	(*suspend)(struct usb_repeater *r, int suspend);
	int	(*powerup)(struct usb_repeater *r);
	int	(*powerdown)(struct usb_repeater *r);
	int	(*get_version)(struct usb_repeater *r);
};

#if IS_ENABLED(CONFIG_USB_REPEATER)
struct usb_repeater *devm_usb_get_repeater_by_phandle(struct device *dev,
		const char *phandle, u8 index);
struct usb_repeater *devm_usb_get_repeater_by_node(struct device *dev,
		struct device_node *node);
struct usb_repeater *usb_get_repeater_by_phandle(struct device *dev,
			const char *phandle, u8 index);
struct usb_repeater *usb_get_repeater_by_node(struct device_node *node);
void usb_put_repeater(struct usb_repeater *r);
int usb_add_repeater_dev(struct usb_repeater *r);
void usb_remove_repeater_dev(struct usb_repeater *r);
#else
static inline struct usb_repeater *devm_usb_get_repeater_by_phandle(
		struct device *d, const char *phandle, u8 index)
{ return ERR_PTR(-ENXIO); }

static inline struct usb_repeater *devm_usb_get_repeater_by_node(
		struct device *dev, struct device_node *node)
{ return ERR_PTR(-ENXIO); }

static inline struct usb_repeater *usb_get_repeater_by_phandle(
		struct device *d, const char *phandle, u8 index)
{ return ERR_PTR(-ENXIO); }

static inline struct usb_repeater *usb_get_repeater_by_node(
		struct device_node *node)
{ return ERR_PTR(-ENXIO); }

static inline void usb_put_repeater(struct usb_repeater *r)
{ }

static inline int usb_add_repeater_dev(struct usb_repeater *r)
{ return 0; }

static inline void usb_remove_repeater_dev(struct usb_repeater *r)
{ }
#endif

static inline int usb_repeater_reset(struct usb_repeater *r,
				bool bring_out_of_reset)
{
	if (r && r->reset != NULL)
		return r->reset(r, bring_out_of_reset);
	else
		return 0;
}

static inline int usb_repeater_init(struct usb_repeater *r)
{
	if (r && r->init != NULL)
		return r->init(r);
	else
		return 0;
}

static inline int usb_repeater_suspend(struct usb_repeater *r, int suspend)
{
	if (r && r->suspend != NULL)
		return r->suspend(r, suspend);
	else
		return 0;
}

static inline int usb_repeater_powerup(struct usb_repeater *r)
{
	if (r && r->powerup != NULL)
		return r->powerup(r);
	else
		return 0;
}

static inline int usb_repeater_powerdown(struct usb_repeater *r)
{
	if (r && r->powerdown != NULL)
		return r->powerdown(r);
	else
		return 0;
}

static inline int usb_repeater_get_version(struct usb_repeater *r)
{
	if (r && r->get_version != NULL)
		return r->get_version(r);
	else
		return -EINVAL;
}
#endif /* __LINUX_USB_REPEATER_H */
