/*
 *  Copyright (C) 2006 James Simmons <jsimmons@infradead.org>
 *
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or (at
 *  your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful, but
 *  WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place, Suite 330, Boston, MA 02111-1307 USA.
 *
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 */

#ifndef _LINUX_DISPLAY_H
#define _LINUX_DISPLAY_H

#include <linux/device.h>

struct display_device;

/* This structure defines all the properties of a Display. */
struct display_driver {
	int  (*set_contrast)(struct display_device *, unsigned int);
	int  (*get_contrast)(struct display_device *);
	void (*suspend)(struct display_device *, pm_message_t state);
	void (*resume)(struct display_device *);
	int  (*probe)(struct display_device *, void *);
	int  (*remove)(struct display_device *);
	int  max_contrast;
};

struct display_device {
	struct module *owner;			/* Owner module */
	struct display_driver *driver;
	struct device *parent;			/* This is the parent */
	struct device *dev;			/* This is this display device */
	struct mutex lock;
	void *priv_data;
	char type[16];
	char *name;
	int idx;
};

extern struct display_device *display_device_register(struct display_driver *driver,
					struct device *dev, void *devdata);
extern void display_device_unregister(struct display_device *dev);

extern int probe_edid(struct display_device *dev, void *devdata);

#define to_display_device(obj) container_of(obj, struct display_device, class_dev)

#endif
