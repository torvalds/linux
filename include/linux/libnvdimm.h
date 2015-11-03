/*
 * libnvdimm - Non-volatile-memory Devices Subsystem
 *
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
#ifndef __LIBNVDIMM_H__
#define __LIBNVDIMM_H__
#include <linux/kernel.h>
#include <linux/sizes.h>
#include <linux/types.h>

enum {
	/* when a dimm supports both PMEM and BLK access a label is required */
	NDD_ALIASING = 1 << 0,
	/* unarmed memory devices may not persist writes */
	NDD_UNARMED = 1 << 1,

	/* need to set a limit somewhere, but yes, this is likely overkill */
	ND_IOCTL_MAX_BUFLEN = SZ_4M,
	ND_CMD_MAX_ELEM = 4,
	ND_CMD_MAX_ENVELOPE = 16,
	ND_CMD_ARS_STATUS_MAX = SZ_4K,
	ND_MAX_MAPPINGS = 32,

	/* region flag indicating to direct-map persistent memory by default */
	ND_REGION_PAGEMAP = 0,

	/* mark newly adjusted resources as requiring a label update */
	DPA_RESOURCE_ADJUSTED = 1 << 0,
};

extern struct attribute_group nvdimm_bus_attribute_group;
extern struct attribute_group nvdimm_attribute_group;
extern struct attribute_group nd_device_attribute_group;
extern struct attribute_group nd_numa_attribute_group;
extern struct attribute_group nd_region_attribute_group;
extern struct attribute_group nd_mapping_attribute_group;

struct nvdimm;
struct nvdimm_bus_descriptor;
typedef int (*ndctl_fn)(struct nvdimm_bus_descriptor *nd_desc,
		struct nvdimm *nvdimm, unsigned int cmd, void *buf,
		unsigned int buf_len);

struct nd_namespace_label;
struct nvdimm_drvdata;
struct nd_mapping {
	struct nvdimm *nvdimm;
	struct nd_namespace_label **labels;
	u64 start;
	u64 size;
	/*
	 * @ndd is for private use at region enable / disable time for
	 * get_ndd() + put_ndd(), all other nd_mapping to ndd
	 * conversions use to_ndd() which respects enabled state of the
	 * nvdimm.
	 */
	struct nvdimm_drvdata *ndd;
};

struct nvdimm_bus_descriptor {
	const struct attribute_group **attr_groups;
	unsigned long dsm_mask;
	char *provider_name;
	ndctl_fn ndctl;
};

struct nd_cmd_desc {
	int in_num;
	int out_num;
	u32 in_sizes[ND_CMD_MAX_ELEM];
	int out_sizes[ND_CMD_MAX_ELEM];
};

struct nd_interleave_set {
	u64 cookie;
};

struct nd_region_desc {
	struct resource *res;
	struct nd_mapping *nd_mapping;
	u16 num_mappings;
	const struct attribute_group **attr_groups;
	struct nd_interleave_set *nd_set;
	void *provider_data;
	int num_lanes;
	int numa_node;
	unsigned long flags;
};

struct nvdimm_bus;
struct module;
struct device;
struct nd_blk_region;
struct nd_blk_region_desc {
	int (*enable)(struct nvdimm_bus *nvdimm_bus, struct device *dev);
	void (*disable)(struct nvdimm_bus *nvdimm_bus, struct device *dev);
	int (*do_io)(struct nd_blk_region *ndbr, resource_size_t dpa,
			void *iobuf, u64 len, int rw);
	struct nd_region_desc ndr_desc;
};

static inline struct nd_blk_region_desc *to_blk_region_desc(
		struct nd_region_desc *ndr_desc)
{
	return container_of(ndr_desc, struct nd_blk_region_desc, ndr_desc);

}

struct nvdimm_bus *__nvdimm_bus_register(struct device *parent,
		struct nvdimm_bus_descriptor *nfit_desc, struct module *module);
#define nvdimm_bus_register(parent, desc) \
	__nvdimm_bus_register(parent, desc, THIS_MODULE)
void nvdimm_bus_unregister(struct nvdimm_bus *nvdimm_bus);
struct nvdimm_bus *to_nvdimm_bus(struct device *dev);
struct nvdimm *to_nvdimm(struct device *dev);
struct nd_region *to_nd_region(struct device *dev);
struct nd_blk_region *to_nd_blk_region(struct device *dev);
struct nvdimm_bus_descriptor *to_nd_desc(struct nvdimm_bus *nvdimm_bus);
const char *nvdimm_name(struct nvdimm *nvdimm);
void *nvdimm_provider_data(struct nvdimm *nvdimm);
struct nvdimm *nvdimm_create(struct nvdimm_bus *nvdimm_bus, void *provider_data,
		const struct attribute_group **groups, unsigned long flags,
		unsigned long *dsm_mask);
const struct nd_cmd_desc *nd_cmd_dimm_desc(int cmd);
const struct nd_cmd_desc *nd_cmd_bus_desc(int cmd);
u32 nd_cmd_in_size(struct nvdimm *nvdimm, int cmd,
		const struct nd_cmd_desc *desc, int idx, void *buf);
u32 nd_cmd_out_size(struct nvdimm *nvdimm, int cmd,
		const struct nd_cmd_desc *desc, int idx, const u32 *in_field,
		const u32 *out_field);
int nvdimm_bus_check_dimm_count(struct nvdimm_bus *nvdimm_bus, int dimm_count);
struct nd_region *nvdimm_pmem_region_create(struct nvdimm_bus *nvdimm_bus,
		struct nd_region_desc *ndr_desc);
struct nd_region *nvdimm_blk_region_create(struct nvdimm_bus *nvdimm_bus,
		struct nd_region_desc *ndr_desc);
struct nd_region *nvdimm_volatile_region_create(struct nvdimm_bus *nvdimm_bus,
		struct nd_region_desc *ndr_desc);
void *nd_region_provider_data(struct nd_region *nd_region);
void *nd_blk_region_provider_data(struct nd_blk_region *ndbr);
void nd_blk_region_set_provider_data(struct nd_blk_region *ndbr, void *data);
struct nvdimm *nd_blk_region_to_dimm(struct nd_blk_region *ndbr);
unsigned int nd_region_acquire_lane(struct nd_region *nd_region);
void nd_region_release_lane(struct nd_region *nd_region, unsigned int lane);
u64 nd_fletcher64(void *addr, size_t len, bool le);
#endif /* __LIBNVDIMM_H__ */
