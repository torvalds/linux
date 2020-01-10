/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __IO_PGTABLE_H
#define __IO_PGTABLE_H

#include <linux/bitops.h>
#include <linux/iommu.h>

/*
 * Public API for use by IOMMU drivers
 */
enum io_pgtable_fmt {
	ARM_32_LPAE_S1,
	ARM_32_LPAE_S2,
	ARM_64_LPAE_S1,
	ARM_64_LPAE_S2,
	ARM_V7S,
	ARM_MALI_LPAE,
	IO_PGTABLE_NUM_FMTS,
};

/**
 * struct iommu_flush_ops - IOMMU callbacks for TLB and page table management.
 *
 * @tlb_flush_all:  Synchronously invalidate the entire TLB context.
 * @tlb_flush_walk: Synchronously invalidate all intermediate TLB state
 *                  (sometimes referred to as the "walk cache") for a virtual
 *                  address range.
 * @tlb_flush_leaf: Synchronously invalidate all leaf TLB state for a virtual
 *                  address range.
 * @tlb_add_page:   Optional callback to queue up leaf TLB invalidation for a
 *                  single page.  IOMMUs that cannot batch TLB invalidation
 *                  operations efficiently will typically issue them here, but
 *                  others may decide to update the iommu_iotlb_gather structure
 *                  and defer the invalidation until iommu_tlb_sync() instead.
 *
 * Note that these can all be called in atomic context and must therefore
 * not block.
 */
struct iommu_flush_ops {
	void (*tlb_flush_all)(void *cookie);
	void (*tlb_flush_walk)(unsigned long iova, size_t size, size_t granule,
			       void *cookie);
	void (*tlb_flush_leaf)(unsigned long iova, size_t size, size_t granule,
			       void *cookie);
	void (*tlb_add_page)(struct iommu_iotlb_gather *gather,
			     unsigned long iova, size_t granule, void *cookie);
};

/**
 * struct io_pgtable_cfg - Configuration data for a set of page tables.
 *
 * @quirks:        A bitmap of hardware quirks that require some special
 *                 action by the low-level page table allocator.
 * @pgsize_bitmap: A bitmap of page sizes supported by this set of page
 *                 tables.
 * @ias:           Input address (iova) size, in bits.
 * @oas:           Output address (paddr) size, in bits.
 * @coherent_walk  A flag to indicate whether or not page table walks made
 *                 by the IOMMU are coherent with the CPU caches.
 * @tlb:           TLB management callbacks for this set of tables.
 * @iommu_dev:     The device representing the DMA configuration for the
 *                 page table walker.
 */
struct io_pgtable_cfg {
	/*
	 * IO_PGTABLE_QUIRK_ARM_NS: (ARM formats) Set NS and NSTABLE bits in
	 *	stage 1 PTEs, for hardware which insists on validating them
	 *	even in	non-secure state where they should normally be ignored.
	 *
	 * IO_PGTABLE_QUIRK_NO_PERMS: Ignore the IOMMU_READ, IOMMU_WRITE and
	 *	IOMMU_NOEXEC flags and map everything with full access, for
	 *	hardware which does not implement the permissions of a given
	 *	format, and/or requires some format-specific default value.
	 *
	 * IO_PGTABLE_QUIRK_TLBI_ON_MAP: If the format forbids caching invalid
	 *	(unmapped) entries but the hardware might do so anyway, perform
	 *	TLB maintenance when mapping as well as when unmapping.
	 *
	 * IO_PGTABLE_QUIRK_ARM_MTK_EXT: (ARM v7s format) MediaTek IOMMUs extend
	 *	to support up to 34 bits PA where the bit32 and bit33 are
	 *	encoded in the bit9 and bit4 of the PTE respectively.
	 *
	 * IO_PGTABLE_QUIRK_NON_STRICT: Skip issuing synchronous leaf TLBIs
	 *	on unmap, for DMA domains using the flush queue mechanism for
	 *	delayed invalidation.
	 */
	#define IO_PGTABLE_QUIRK_ARM_NS		BIT(0)
	#define IO_PGTABLE_QUIRK_NO_PERMS	BIT(1)
	#define IO_PGTABLE_QUIRK_TLBI_ON_MAP	BIT(2)
	#define IO_PGTABLE_QUIRK_ARM_MTK_EXT	BIT(3)
	#define IO_PGTABLE_QUIRK_NON_STRICT	BIT(4)
	unsigned long			quirks;
	unsigned long			pgsize_bitmap;
	unsigned int			ias;
	unsigned int			oas;
	bool				coherent_walk;
	const struct iommu_flush_ops	*tlb;
	struct device			*iommu_dev;

	/* Low-level data specific to the table format */
	union {
		struct {
			u64	ttbr;
			struct {
				u32	ips:3;
				u32	tg:2;
				u32	sh:2;
				u32	orgn:2;
				u32	irgn:2;
				u32	tsz:6;
			}	tcr;
			u64	mair;
		} arm_lpae_s1_cfg;

		struct {
			u64	vttbr;
			struct {
				u32	ps:3;
				u32	tg:2;
				u32	sh:2;
				u32	orgn:2;
				u32	irgn:2;
				u32	sl:2;
				u32	tsz:6;
			}	vtcr;
		} arm_lpae_s2_cfg;

		struct {
			u32	ttbr;
			u32	tcr;
			u32	nmrr;
			u32	prrr;
		} arm_v7s_cfg;

		struct {
			u64	transtab;
			u64	memattr;
		} arm_mali_lpae_cfg;
	};
};

/**
 * struct io_pgtable_ops - Page table manipulation API for IOMMU drivers.
 *
 * @map:          Map a physically contiguous memory region.
 * @unmap:        Unmap a physically contiguous memory region.
 * @iova_to_phys: Translate iova to physical address.
 *
 * These functions map directly onto the iommu_ops member functions with
 * the same names.
 */
struct io_pgtable_ops {
	int (*map)(struct io_pgtable_ops *ops, unsigned long iova,
		   phys_addr_t paddr, size_t size, int prot);
	size_t (*unmap)(struct io_pgtable_ops *ops, unsigned long iova,
			size_t size, struct iommu_iotlb_gather *gather);
	phys_addr_t (*iova_to_phys)(struct io_pgtable_ops *ops,
				    unsigned long iova);
};

/**
 * alloc_io_pgtable_ops() - Allocate a page table allocator for use by an IOMMU.
 *
 * @fmt:    The page table format.
 * @cfg:    The page table configuration. This will be modified to represent
 *          the configuration actually provided by the allocator (e.g. the
 *          pgsize_bitmap may be restricted).
 * @cookie: An opaque token provided by the IOMMU driver and passed back to
 *          the callback routines in cfg->tlb.
 */
struct io_pgtable_ops *alloc_io_pgtable_ops(enum io_pgtable_fmt fmt,
					    struct io_pgtable_cfg *cfg,
					    void *cookie);

/**
 * free_io_pgtable_ops() - Free an io_pgtable_ops structure. The caller
 *                         *must* ensure that the page table is no longer
 *                         live, but the TLB can be dirty.
 *
 * @ops: The ops returned from alloc_io_pgtable_ops.
 */
void free_io_pgtable_ops(struct io_pgtable_ops *ops);


/*
 * Internal structures for page table allocator implementations.
 */

/**
 * struct io_pgtable - Internal structure describing a set of page tables.
 *
 * @fmt:    The page table format.
 * @cookie: An opaque token provided by the IOMMU driver and passed back to
 *          any callback routines.
 * @cfg:    A copy of the page table configuration.
 * @ops:    The page table operations in use for this set of page tables.
 */
struct io_pgtable {
	enum io_pgtable_fmt	fmt;
	void			*cookie;
	struct io_pgtable_cfg	cfg;
	struct io_pgtable_ops	ops;
};

#define io_pgtable_ops_to_pgtable(x) container_of((x), struct io_pgtable, ops)

static inline void io_pgtable_tlb_flush_all(struct io_pgtable *iop)
{
	iop->cfg.tlb->tlb_flush_all(iop->cookie);
}

static inline void
io_pgtable_tlb_flush_walk(struct io_pgtable *iop, unsigned long iova,
			  size_t size, size_t granule)
{
	iop->cfg.tlb->tlb_flush_walk(iova, size, granule, iop->cookie);
}

static inline void
io_pgtable_tlb_flush_leaf(struct io_pgtable *iop, unsigned long iova,
			  size_t size, size_t granule)
{
	iop->cfg.tlb->tlb_flush_leaf(iova, size, granule, iop->cookie);
}

static inline void
io_pgtable_tlb_add_page(struct io_pgtable *iop,
			struct iommu_iotlb_gather * gather, unsigned long iova,
			size_t granule)
{
	if (iop->cfg.tlb->tlb_add_page)
		iop->cfg.tlb->tlb_add_page(gather, iova, granule, iop->cookie);
}

/**
 * struct io_pgtable_init_fns - Alloc/free a set of page tables for a
 *                              particular format.
 *
 * @alloc: Allocate a set of page tables described by cfg.
 * @free:  Free the page tables associated with iop.
 */
struct io_pgtable_init_fns {
	struct io_pgtable *(*alloc)(struct io_pgtable_cfg *cfg, void *cookie);
	void (*free)(struct io_pgtable *iop);
};

extern struct io_pgtable_init_fns io_pgtable_arm_32_lpae_s1_init_fns;
extern struct io_pgtable_init_fns io_pgtable_arm_32_lpae_s2_init_fns;
extern struct io_pgtable_init_fns io_pgtable_arm_64_lpae_s1_init_fns;
extern struct io_pgtable_init_fns io_pgtable_arm_64_lpae_s2_init_fns;
extern struct io_pgtable_init_fns io_pgtable_arm_v7s_init_fns;
extern struct io_pgtable_init_fns io_pgtable_arm_mali_lpae_init_fns;

#endif /* __IO_PGTABLE_H */
