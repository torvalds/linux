/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef NDTEST_H
#define NDTEST_H

#include <linux/platform_device.h>
#include <linux/libnvdimm.h>

struct ndtest_config;

struct ndtest_priv {
	struct platform_device pdev;
	struct device_node *dn;
	struct list_head resources;
	struct nvdimm_bus_descriptor bus_desc;
	struct nvdimm_bus *bus;
	struct ndtest_config *config;

	dma_addr_t *dcr_dma;
	dma_addr_t *label_dma;
	dma_addr_t *dimm_dma;
};

struct ndtest_dimm {
	struct device *dev;
	struct nvdimm *nvdimm;
	struct ndtest_blk_mmio *mmio;
	struct nd_region *blk_region;

	dma_addr_t address;
	unsigned long long flags;
	unsigned long config_size;
	void *label_area;
	char *uuid_str;

	unsigned int size;
	unsigned int handle;
	unsigned int fail_cmd;
	unsigned int physical_id;
	unsigned int num_formats;
	int id;
	int fail_cmd_code;
	u8 no_alias;
};

struct ndtest_config {
	struct ndtest_dimm *dimms;
	unsigned int dimm_count;
	unsigned int dimm_start;
	u8 num_regions;
};

#endif /* NDTEST_H */
