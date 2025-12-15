/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2024-2025, NVIDIA CORPORATION & AFFILIATES
 */
#ifndef __GENERIC_PT_IOMMU_H
#define __GENERIC_PT_IOMMU_H

#include <linux/generic_pt/common.h>
#include <linux/iommu.h>
#include <linux/mm_types.h>

struct iommu_iotlb_gather;
struct pt_iommu_ops;
struct pt_iommu_driver_ops;
struct iommu_dirty_bitmap;

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
	 * @driver_ops: Function pointers provided by the HW driver to help
	 * manage HW details like caches.
	 */
	const struct pt_iommu_driver_ops *driver_ops;

	/**
	 * @nid: Node ID to use for table memory allocations. The IOMMU driver
	 * may want to set the NID to the device's NID, if there are multiple
	 * table walkers.
	 */
	int nid;

	/**
	 * @iommu_device: Device pointer used for any DMA cache flushing when
	 * PT_FEAT_DMA_INCOHERENT. This is the iommu device that created the
	 * page table which must have dma ops that perform cache flushing.
	 */
	struct device *iommu_device;
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
	 * @set_dirty: Make the iova write dirty
	 * @iommu_table: Table to manipulate
	 * @iova: IO virtual address to start
	 *
	 * This is only used by iommufd testing. It makes the iova dirty so that
	 * read_and_clear_dirty() will see it as dirty. Unlike all the other ops
	 * this one is safe to call without holding any locking. It may return
	 * -EAGAIN if there is a race.
	 */
	int (*set_dirty)(struct pt_iommu *iommu_table, dma_addr_t iova);

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

/**
 * struct pt_iommu_driver_ops - HW IOTLB cache flushing operations
 *
 * The IOMMU driver should implement these using container_of(iommu_table) to
 * get to it's iommu_domain derived structure. All ops can be called in atomic
 * contexts as they are buried under DMA API calls.
 */
struct pt_iommu_driver_ops {
	/**
	 * @change_top: Update the top of table pointer
	 * @iommu_table: Table to operate on
	 * @top_paddr: New CPU physical address of the top pointer
	 * @top_level: IOMMU PT level of the new top
	 *
	 * Called under the get_top_lock() spinlock. The driver must update all
	 * HW references to this domain with a new top address and
	 * configuration. On return mappings placed in the new top must be
	 * reachable by the HW.
	 *
	 * top_level encodes the level in IOMMU PT format, level 0 is the
	 * smallest page size increasing from there. This has to be translated
	 * to any HW specific format. During this call the new top will not be
	 * visible to any other API.
	 *
	 * This op is only used by PT_FEAT_DYNAMIC_TOP, and is required if
	 * enabled.
	 */
	void (*change_top)(struct pt_iommu *iommu_table, phys_addr_t top_paddr,
			   unsigned int top_level);

	/**
	 * @get_top_lock: lock to hold when changing the table top
	 * @iommu_table: Table to operate on
	 *
	 * Return a lock to hold when changing the table top page table from
	 * being stored in HW. The lock will be held prior to calling
	 * change_top() and released once the top is fully visible.
	 *
	 * Typically this would be a lock that protects the iommu_domain's
	 * attachment list.
	 *
	 * This op is only used by PT_FEAT_DYNAMIC_TOP, and is required if
	 * enabled.
	 */
	spinlock_t *(*get_top_lock)(struct pt_iommu *iommu_table);
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
	int pt_iommu_##fmt##_map_pages(struct iommu_domain *domain,            \
				       unsigned long iova, phys_addr_t paddr,  \
				       size_t pgsize, size_t pgcount,          \
				       int prot, gfp_t gfp, size_t *mapped);   \
	size_t pt_iommu_##fmt##_unmap_pages(                                   \
		struct iommu_domain *domain, unsigned long iova,               \
		size_t pgsize, size_t pgcount,                                 \
		struct iommu_iotlb_gather *iotlb_gather);                      \
	int pt_iommu_##fmt##_read_and_clear_dirty(                             \
		struct iommu_domain *domain, unsigned long iova, size_t size,  \
		unsigned long flags, struct iommu_dirty_bitmap *dirty);        \
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
#define IOMMU_PT_DOMAIN_OPS(fmt)                        \
	.iova_to_phys = &pt_iommu_##fmt##_iova_to_phys, \
	.map_pages = &pt_iommu_##fmt##_map_pages,       \
	.unmap_pages = &pt_iommu_##fmt##_unmap_pages
#define IOMMU_PT_DIRTY_OPS(fmt) \
	.read_and_clear_dirty = &pt_iommu_##fmt##_read_and_clear_dirty

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

/* amdv1_mock is used by the iommufd selftest */
#define pt_iommu_amdv1_mock pt_iommu_amdv1
#define pt_iommu_amdv1_mock_cfg pt_iommu_amdv1_cfg
struct pt_iommu_amdv1_mock_hw_info;
IOMMU_PROTOTYPES(amdv1_mock);

struct pt_iommu_vtdss_cfg {
	struct pt_iommu_cfg common;
	/* 4 is a 57 bit 5 level table */
	unsigned int top_level;
};

struct pt_iommu_vtdss_hw_info {
	u64 ssptptr;
	u8 aw;
};

IOMMU_FORMAT(vtdss, vtdss_pt);

struct pt_iommu_x86_64_cfg {
	struct pt_iommu_cfg common;
	/* 4 is a 57 bit 5 level table */
	unsigned int top_level;
};

struct pt_iommu_x86_64_hw_info {
	u64 gcr3_pt;
	u8 levels;
};

IOMMU_FORMAT(x86_64, x86_64_pt);

#undef IOMMU_PROTOTYPES
#undef IOMMU_FORMAT
#endif
