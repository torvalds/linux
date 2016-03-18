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
#include <linux/fs.h>
#include <linux/ndctl.h>
#include <linux/device.h>

enum nvdimm_event {
	NVDIMM_REVALIDATE_POISON,
};

struct nd_device_driver {
	struct device_driver drv;
	unsigned long type;
	int (*probe)(struct device *dev);
	int (*remove)(struct device *dev);
	void (*notify)(struct device *dev, enum nvdimm_event event);
};

static inline struct nd_device_driver *to_nd_device_driver(
		struct device_driver *drv)
{
	return container_of(drv, struct nd_device_driver, drv);
};

/**
 * struct nd_namespace_common - core infrastructure of a namespace
 * @force_raw: ignore other personalities for the namespace (e.g. btt)
 * @dev: device model node
 * @claim: when set a another personality has taken ownership of the namespace
 * @rw_bytes: access the raw namespace capacity with byte-aligned transfers
 */
struct nd_namespace_common {
	int force_raw;
	struct device dev;
	struct device *claim;
	int (*rw_bytes)(struct nd_namespace_common *, resource_size_t offset,
			void *buf, size_t size, int rw);
};

static inline struct nd_namespace_common *to_ndns(struct device *dev)
{
	return container_of(dev, struct nd_namespace_common, dev);
}

/**
 * struct nd_namespace_io - infrastructure for loading an nd_pmem instance
 * @dev: namespace device created by the nd region driver
 * @res: struct resource conversion of a NFIT SPA table
 */
struct nd_namespace_io {
	struct nd_namespace_common common;
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
 * @alt_name: namespace name supplied in the dimm label
 * @uuid: namespace name supplied in the dimm label
 * @id: ida allocated id
 * @lbasize: blk namespaces have a native sector size when btt not present
 * @size: sum of all the resource ranges allocated to this namespace
 * @num_resources: number of dpa extents to claim
 * @res: discontiguous dpa extents for given dimm
 */
struct nd_namespace_blk {
	struct nd_namespace_common common;
	char *alt_name;
	u8 *uuid;
	int id;
	unsigned long lbasize;
	resource_size_t size;
	int num_resources;
	struct resource **res;
};

static inline struct nd_namespace_io *to_nd_namespace_io(struct device *dev)
{
	return container_of(dev, struct nd_namespace_io, common.dev);
}

static inline struct nd_namespace_pmem *to_nd_namespace_pmem(struct device *dev)
{
	struct nd_namespace_io *nsio = to_nd_namespace_io(dev);

	return container_of(nsio, struct nd_namespace_pmem, nsio);
}

static inline struct nd_namespace_blk *to_nd_namespace_blk(struct device *dev)
{
	return container_of(dev, struct nd_namespace_blk, common.dev);
}

/**
 * nvdimm_read_bytes() - synchronously read bytes from an nvdimm namespace
 * @ndns: device to read
 * @offset: namespace-relative starting offset
 * @buf: buffer to fill
 * @size: transfer length
 *
 * @buf is up-to-date upon return from this routine.
 */
static inline int nvdimm_read_bytes(struct nd_namespace_common *ndns,
		resource_size_t offset, void *buf, size_t size)
{
	return ndns->rw_bytes(ndns, offset, buf, size, READ);
}

/**
 * nvdimm_write_bytes() - synchronously write bytes to an nvdimm namespace
 * @ndns: device to read
 * @offset: namespace-relative starting offset
 * @buf: buffer to drain
 * @size: transfer length
 *
 * NVDIMM Namepaces disks do not implement sectors internally.  Depending on
 * the @ndns, the contents of @buf may be in cpu cache, platform buffers,
 * or on backing memory media upon return from this routine.  Flushing
 * to media is handled internal to the @ndns driver, if at all.
 */
static inline int nvdimm_write_bytes(struct nd_namespace_common *ndns,
		resource_size_t offset, void *buf, size_t size)
{
	return ndns->rw_bytes(ndns, offset, buf, size, WRITE);
}

#define MODULE_ALIAS_ND_DEVICE(type) \
	MODULE_ALIAS("nd:t" __stringify(type) "*")
#define ND_DEVICE_MODALIAS_FMT "nd:t%d"

struct nd_region;
void nvdimm_region_notify(struct nd_region *nd_region, enum nvdimm_event event);
int __must_check __nd_driver_register(struct nd_device_driver *nd_drv,
		struct module *module, const char *mod_name);
#define nd_driver_register(driver) \
	__nd_driver_register(driver, THIS_MODULE, KBUILD_MODNAME)
#endif /* __LINUX_ND_H__ */
