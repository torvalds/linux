/*
 * include/linux/nvhost.h
 *
 * Tegra graphics host driver
 *
 * Copyright (c) 2009-2010, NVIDIA Corporation.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#ifndef __LINUX_NVHOST_H
#define __LINUX_NVHOST_H

#include <linux/device.h>

struct nvhost_master;

struct nvhost_device {
	const char		*name;
	struct device		dev;
	int			id;
	u32			num_resources;
	struct resource		*resource;

	struct nvhost_master	*host;
};

extern int nvhost_device_register(struct nvhost_device *);
extern void nvhost_device_unregister(struct nvhost_device *);

extern struct bus_type nvhost_bus_type;

struct nvhost_driver {
	int (*probe)(struct nvhost_device *);
	int (*remove)(struct nvhost_device *);
	void (*shutdown)(struct nvhost_device *);
	int (*suspend)(struct nvhost_device *, pm_message_t state);
	int (*resume)(struct nvhost_device *);
	struct device_driver driver;
};

extern int nvhost_driver_register(struct nvhost_driver *);
extern void nvhost_driver_unregister(struct nvhost_driver *);
extern struct resource *nvhost_get_resource(struct nvhost_device *, unsigned int, unsigned int);
extern int nvhost_get_irq(struct nvhost_device *, unsigned int);
extern struct resource *nvhost_get_resource_byname(struct nvhost_device *, unsigned int, const char *);
extern int nvhost_get_irq_byname(struct nvhost_device *, const char *);

#define to_nvhost_device(x) container_of((x), struct nvhost_device, dev)
#define to_nvhost_driver(drv)	(container_of((drv), struct nvhost_driver, \
				 driver))

#define nvhost_get_drvdata(_dev)	dev_get_drvdata(&(_dev)->dev)
#define nvhost_set_drvdata(_dev,data)	dev_set_drvdata(&(_dev)->dev, (data))

int nvhost_bus_register(struct nvhost_master *host);
#endif
