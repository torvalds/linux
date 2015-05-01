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

/**
 * struct nd_namespace_io - infrastructure for loading an nd_pmem instance
 * @dev: namespace device created by the nd region driver
 * @res: struct resource conversion of a NFIT SPA table
 */
struct nd_namespace_io {
	struct device dev;
	struct resource res;
};

/**
 * struct nd_namespace_pmem - namespace device for dimm-backed interleaved memory
 * @nsio: device and system physical address range to drive
 * @alt_name: namespace name supplied in the dimm label
 * @uuid: namespace name supplied in the dimm label
 */
struct nd_namespace_pmem {
	struct nd_namespace_io nsio;
	char *alt_name;
	u8 *uuid;
};

/**
 * struct nd_namespace_blk - namespace for dimm-bounded persistent memory
 * @dev: namespace device creation by the nd region driver
 * @alt_name: namespace name supplied in the dimm label
 * @uuid: namespace name supplied in the dimm label
 * @id: ida allocated id
 * @lbasize: blk namespaces have a native sector size when btt not present
 * @num_resources: number of dpa extents to claim
 * @res: discontiguous dpa extents for given dimm
 */
struct nd_namespace_blk {
	struct device dev;
	char *alt_name;
	u8 *uuid;
	int id;
	unsigned long lbasize;
	int num_resources;
	struct resource **res;
};

static inline struct nd_namespace_io *to_nd_namespace_io(struct device *dev)
{
	return container_of(dev, struct nd_namespace_io, dev);
}

static inline struct nd_namespace_pmem *to_nd_namespace_pmem(struct device *dev)
{
	struct nd_namespace_io *nsio = to_nd_namespace_io(dev);

	return container_of(nsio, struct nd_namespace_pmem, nsio);
}

static inline struct nd_namespace_blk *to_nd_namespace_blk(struct device *dev)
{
	return container_of(dev, struct nd_namespace_blk, dev);
}

#define MODULE_ALIAS_ND_DEVICE(type) \
	MODULE_ALIAS("nd:t" __stringify(type) "*")
#define ND_DEVICE_MODALIAS_FMT "nd:t%d"

int __must_check __nd_driver_register(struct nd_device_driver *nd_drv,
		struct module *module, const char *mod_name);
#define nd_driver_register(driver) \
	__nd_driver_register(driver, THIS_MODULE, KBUILD_MODNAME)
#endif /* __LINUX_ND_H__ */
