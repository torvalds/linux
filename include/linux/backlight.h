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

	/* Notify the backlight driver some property has changed */
	int (*update_status)(struct backlight_device *);
	/* Return the current backlight brightness (accounting for power,
	   fb_blank etc.) */
	int (*get_brightness)(struct backlight_device *);
	/* Check if given framebuffer device is the one bound to this backlight;
	   return 0 if not, !=0 if it is. If NULL, backlight always matches the fb. */
	int (*check_fb)(struct fb_info *);

	/* Current User requested brightness (0 - max_brightness) */
	int brightness;
	/* Maximal value for brightness (read-only) */
	int max_brightness;
	/* Current FB Power mode (0: full on, 1..3: power saving
	   modes; 4: full off), see FB_BLANK_XXX */
	int power;
	/* FB Blanking active? (values as for power) */
	int fb_blank;
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
