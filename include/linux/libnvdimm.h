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
extern struct attribute_group nvdimm_bus_attribute_group;

struct nvdimm;
struct nvdimm_bus_descriptor;
typedef int (*ndctl_fn)(struct nvdimm_bus_descriptor *nd_desc,
		struct nvdimm *nvdimm, unsigned int cmd, void *buf,
		unsigned int buf_len);

struct nvdimm_bus_descriptor {
	const struct attribute_group **attr_groups;
	unsigned long dsm_mask;
	char *provider_name;
	ndctl_fn ndctl;
};

struct device;
struct nvdimm_bus *nvdimm_bus_register(struct device *parent,
		struct nvdimm_bus_descriptor *nfit_desc);
void nvdimm_bus_unregister(struct nvdimm_bus *nvdimm_bus);
struct nvdimm_bus *to_nvdimm_bus(struct device *dev);
struct nvdimm_bus_descriptor *to_nd_desc(struct nvdimm_bus *nvdimm_bus);
#endif /* __LIBNVDIMM_H__ */
