/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2024-2025, NVIDIA CORPORATION & AFFILIATES
 */
#ifndef __GENERIC_PT_IOMMU_H
#define __GENERIC_PT_IOMMU_H

#include <linux/generic_pt/common.h>
#include <linux/iommu.h>
#include <linux/mm_types.h>

struct pt_iommu_ops;

/**
 * DOC: IOMMU Radix Page Table
 *
 * The IOMMU implementation of the Generic Page Table provides an ops struct
 * that is useful to go with an iommu_domain to serve the DMA API, IOMMUFD and
 * the generic map/unmap interface.
 *
 * This interface uses a caller provided locking approach. The caller must have
 * a VA range lock concept that prevents concurrent threads from calling ops on
 * the same VA. Generally the range lock must be at least as large as a single
 * map call.
 */

/**
 * struct pt_iommu - Base structure for IOMMU page tables
 *
 * The format-specific struct will include this as the first member.
 */
struct pt_iommu {
	/**
	 * @domain: The core IOMMU domain. The driver should use a union to
	 * overlay this memory with its previously existing domain struct to
	 * create an alias.
	 */
	struct iommu_domain domain;

	/**
	 * @ops: Function pointers to access the API
	 */
	const struct pt_iommu_ops *ops;

	/**
	 * @nid: Node ID to use for table memory allocations. The IOMMU driver
	 * may want to set the NID to the device's NID, if there are multiple
	 * table walkers.
	 */
	int nid;
};

/**
 * struct pt_iommu_info - Details about the IOMMU page table
 *
 * Returned from pt_iommu_ops->get_info()
 */
struct pt_iommu_info {
	/**
	 * @pgsize_bitmap: A bitmask where each set bit indicates
	 * a page size that can be natively stored in the page table.
	 */
	u64 pgsize_bitmap;
};

struct pt_iommu_ops {
	/**
	 * @get_info: Return the pt_iommu_info structure
	 * @iommu_table: Table to query
	 *
	 * Return some basic static information about the page table.
	 */
	void (*get_info)(struct pt_iommu *iommu_table,
			 struct pt_iommu_info *info);

	/**
	 * @deinit: Undo a format specific init operation
	 * @iommu_table: Table to destroy
	 *
	 * Release all of the memory. The caller must have already removed the
	 * table from all HW access and all caches.
	 */
	void (*deinit)(struct pt_iommu *iommu_table);
};

static inline void pt_iommu_deinit(struct pt_iommu *iommu_table)
{
	/*
	 * It is safe to call pt_iommu_deinit() before an init, or if init
	 * fails. The ops pointer will only become non-NULL if deinit needs to be
	 * run.
	 */
	if (iommu_table->ops)
		iommu_table->ops->deinit(iommu_table);
}

/**
 * struct pt_iommu_cfg - Common configuration values for all formats
 */
struct pt_iommu_cfg {
	/**
	 * @features: Features required. Only these features will be turned on.
	 * The feature list should reflect what the IOMMU HW is capable of.
	 */
	unsigned int features;
	/**
	 * @hw_max_vasz_lg2: Maximum VA the IOMMU HW can support. This will
	 * imply the top level of the table.
	 */
	u8 hw_max_vasz_lg2;
	/**
	 * @hw_max_oasz_lg2: Maximum OA the IOMMU HW can support. The format
	 * might select a lower maximum OA.
	 */
	u8 hw_max_oasz_lg2;
};

/* Generate the exported function signatures from iommu_pt.h */
#define IOMMU_PROTOTYPES(fmt)                                                  \
	phys_addr_t pt_iommu_##fmt##_iova_to_phys(struct iommu_domain *domain, \
						  dma_addr_t iova);            \
	int pt_iommu_##fmt##_init(struct pt_iommu_##fmt *table,                \
				  const struct pt_iommu_##fmt##_cfg *cfg,      \
				  gfp_t gfp);                                  \
	void pt_iommu_##fmt##_hw_info(struct pt_iommu_##fmt *table,            \
				      struct pt_iommu_##fmt##_hw_info *info)
#define IOMMU_FORMAT(fmt, member)       \
	struct pt_iommu_##fmt {         \
		struct pt_iommu iommu;  \
		struct pt_##fmt member; \
	};                              \
	IOMMU_PROTOTYPES(fmt)

/*
 * A driver uses IOMMU_PT_DOMAIN_OPS to populate the iommu_domain_ops for the
 * iommu_pt
 */
#define IOMMU_PT_DOMAIN_OPS(fmt) \
	.iova_to_phys = &pt_iommu_##fmt##_iova_to_phys,

/*
 * The driver should setup its domain struct like
 *	union {
 *		struct iommu_domain domain;
 *		struct pt_iommu_xxx xx;
 *	};
 * PT_IOMMU_CHECK_DOMAIN(struct mock_iommu_domain, xx.iommu, domain);
 *
 * Which creates an alias between driver_domain.domain and
 * driver_domain.xx.iommu.domain. This is to avoid a mass rename of existing
 * driver_domain.domain users.
 */
#define PT_IOMMU_CHECK_DOMAIN(s, pt_iommu_memb, domain_memb) \
	static_assert(offsetof(s, pt_iommu_memb.domain) ==   \
		      offsetof(s, domain_memb))

struct pt_iommu_amdv1_cfg {
	struct pt_iommu_cfg common;
	unsigned int starting_level;
};

struct pt_iommu_amdv1_hw_info {
	u64 host_pt_root;
	u8 mode;
};

IOMMU_FORMAT(amdv1, amdpt);

#undef IOMMU_PROTOTYPES
#undef IOMMU_FORMAT
#endif
