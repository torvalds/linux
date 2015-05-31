/*
 * Copyright(c) 2013-2015 Intel Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 */
#ifndef __LINUX_ND_H__
#define __LINUX_ND_H__
#include <linux/ndctl.h>
#include <linux/device.h>

struct nd_device_driver {
	struct device_driver drv;
	unsigned long type;
	int (*probe)(struct device *dev);
	int (*remove)(struct device *dev);
};

static inline struct nd_device_driver *to_nd_device_driver(
		struct device_driver *drv)
{
	return container_of(drv, struct nd_device_driver, drv);
};

struct nd_namespace_io {
	struct device dev;
	struct resource res;
};

static inline struct nd_namespace_io *to_nd_namespace_io(struct device *dev)
{
	return container_of(dev, struct nd_namespace_io, dev);
}

#define MODULE_ALIAS_ND_DEVICE(type) \
	MODULE_ALIAS("nd:t" __stringify(type) "*")
#define ND_DEVICE_MODALIAS_FMT "nd:t%d"

int __must_check __nd_driver_register(struct nd_device_driver *nd_drv,
		struct module *module, const char *mod_name);
#define nd_driver_register(driver) \
	__nd_driver_register(driver, THIS_MODULE, KBUILD_MODNAME)
#endif /* __LINUX_ND_H__ */
