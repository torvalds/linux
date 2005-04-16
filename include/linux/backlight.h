/*
 * Backlight Lowlevel Control Abstraction
 *
 * Copyright (C) 2003,2004 Hewlett-Packard Company
 *
 */

#ifndef _LINUX_BACKLIGHT_H
#define _LINUX_BACKLIGHT_H

#include <linux/device.h>
#include <linux/notifier.h>

struct backlight_device;
struct fb_info;

/* This structure defines all the properties of a backlight
   (usually attached to a LCD). */
struct backlight_properties {
	/* Owner module */
	struct module *owner;
	/* Get the backlight power status (0: full on, 1..3: power saving
	   modes; 4: full off), see FB_BLANK_XXX */
	int (*get_power)(struct backlight_device *);
	/* Enable or disable power to the LCD (0: on; 4: off, see FB_BLANK_XXX) */
	int (*set_power)(struct backlight_device *, int power);
	/* Maximal value for brightness (read-only) */
	int max_brightness;
	/* Get current backlight brightness */
	int (*get_brightness)(struct backlight_device *);
	/* Set backlight brightness (0..max_brightness) */
	int (*set_brightness)(struct backlight_device *, int brightness);
	/* Check if given framebuffer device is the one bound to this backlight;
	   return 0 if not, !=0 if it is. If NULL, backlight always matches the fb. */
	int (*check_fb)(struct fb_info *);
};

struct backlight_device {
	/* This protects the 'props' field. If 'props' is NULL, the driver that
	   registered this device has been unloaded, and if class_get_devdata()
	   points to something in the body of that driver, it is also invalid. */
	struct semaphore sem;
	/* If this is NULL, the backing module is unloaded */
	struct backlight_properties *props;
	/* The framebuffer notifier block */
	struct notifier_block fb_notif;
	/* The class device structure */
	struct class_device class_dev;
};

extern struct backlight_device *backlight_device_register(const char *name,
	void *devdata, struct backlight_properties *bp);
extern void backlight_device_unregister(struct backlight_device *bd);

#define to_backlight_device(obj) container_of(obj, struct backlight_device, class_dev)

#endif
