/*
 * LCD Lowlevel Control Abstraction
 *
 * Copyright (C) 2003,2004 Hewlett-Packard Company
 *
 */

#ifndef _LINUX_LCD_H
#define _LINUX_LCD_H

#include <linux/device.h>
#include <linux/notifier.h>

struct lcd_device;
struct fb_info;

/* This structure defines all the properties of a LCD flat panel. */
struct lcd_properties {
	/* Owner module */
	struct module *owner;
	/* Get the LCD panel power status (0: full on, 1..3: controller
	   power on, flat panel power off, 4: full off), see FB_BLANK_XXX */
	int (*get_power)(struct lcd_device *);
	/* Enable or disable power to the LCD (0: on; 4: off, see FB_BLANK_XXX) */
	int (*set_power)(struct lcd_device *, int power);
	/* The maximum value for contrast (read-only) */
	int max_contrast;
	/* Get the current contrast setting (0-max_contrast) */
	int (*get_contrast)(struct lcd_device *);
	/* Set LCD panel contrast */
        int (*set_contrast)(struct lcd_device *, int contrast);
	/* Check if given framebuffer device is the one LCD is bound to;
	   return 0 if not, !=0 if it is. If NULL, lcd always matches the fb. */
	int (*check_fb)(struct fb_info *);
};

struct lcd_device {
	/* This protects the 'props' field. If 'props' is NULL, the driver that
	   registered this device has been unloaded, and if class_get_devdata()
	   points to something in the body of that driver, it is also invalid. */
	struct semaphore sem;
	/* If this is NULL, the backing module is unloaded */
	struct lcd_properties *props;
	/* The framebuffer notifier block */
	struct notifier_block fb_notif;
	/* The class device structure */
	struct class_device class_dev;
};

extern struct lcd_device *lcd_device_register(const char *name,
	void *devdata, struct lcd_properties *lp);
extern void lcd_device_unregister(struct lcd_device *ld);

#define to_lcd_device(obj) container_of(obj, struct lcd_device, class_dev)

#endif
