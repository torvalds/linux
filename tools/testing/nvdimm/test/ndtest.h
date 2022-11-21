/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef NDTEST_H
#define NDTEST_H

#include <linux/platform_device.h>
#include <linux/libnvdimm.h>

/* SCM device is unable to persist memory contents */
#define PAPR_PMEM_UNARMED                   (1ULL << (63 - 0))
/* SCM device failed to persist memory contents */
#define PAPR_PMEM_SHUTDOWN_DIRTY            (1ULL << (63 - 1))
/* SCM device contents are not persisted from previous IPL */
#define PAPR_PMEM_EMPTY                     (1ULL << (63 - 3))
#define PAPR_PMEM_HEALTH_CRITICAL           (1ULL << (63 - 4))
/* SCM device will be garded off next IPL due to failure */
#define PAPR_PMEM_HEALTH_FATAL              (1ULL << (63 - 5))
/* SCM contents cannot persist due to current platform health status */
#define PAPR_PMEM_HEALTH_UNHEALTHY          (1ULL << (63 - 6))

/* Bits status indicators for health bitmap indicating unarmed dimm */
#define PAPR_PMEM_UNARMED_MASK (PAPR_PMEM_UNARMED |		\
				PAPR_PMEM_HEALTH_UNHEALTHY)

#define PAPR_PMEM_SAVE_FAILED                (1ULL << (63 - 10))

/* Bits status indicators for health bitmap indicating unflushed dimm */
#define PAPR_PMEM_BAD_SHUTDOWN_MASK (PAPR_PMEM_SHUTDOWN_DIRTY)

/* Bits status indicators for health bitmap indicating unrestored dimm */
#define PAPR_PMEM_BAD_RESTORE_MASK  (PAPR_PMEM_EMPTY)

/* Bit status indicators for smart event notification */
#define PAPR_PMEM_SMART_EVENT_MASK (PAPR_PMEM_HEALTH_CRITICAL | \
				    PAPR_PMEM_HEALTH_FATAL |	\
				    PAPR_PMEM_HEALTH_UNHEALTHY)

#define PAPR_PMEM_SAVE_MASK                (PAPR_PMEM_SAVE_FAILED)

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

struct ndtest_blk_mmio {
	void __iomem *base;
	u64 size;
	u64 base_offset;
	u32 line_size;
	u32 num_lines;
	u32 table_size;
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

struct ndtest_mapping {
	u64 start;
	u64 size;
	u8 position;
	u8 dimm;
};

struct ndtest_region {
	struct nd_region *region;
	struct ndtest_mapping *mapping;
	u64 size;
	u8 type;
	u8 num_mappings;
	u8 range_index;
};

struct ndtest_config {
	struct ndtest_dimm *dimms;
	struct ndtest_region *regions;
	unsigned int dimm_count;
	unsigned int dimm_start;
	u8 num_regions;
};

#endif /* NDTEST_H */
