/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __LINUX_SWIOTLB_H
#define __LINUX_SWIOTLB_H

#include <linux/dma-direction.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/limits.h>

struct device;
struct page;
struct scatterlist;

enum swiotlb_force {
	SWIOTLB_NORMAL,		/* Default - depending on HW DMA mask etc. */
	SWIOTLB_FORCE,		/* swiotlb=force */
	SWIOTLB_NO_FORCE,	/* swiotlb=noforce */
};

/*
 * Maximum allowable number of contiguous slabs to map,
 * must be a power of 2.  What is the appropriate value ?
 * The complexity of {map,unmap}_single is linearly dependent on this value.
 */
#define IO_TLB_SEGSIZE	128

/*
 * log of the size of each IO TLB slab.  The number of slabs is command line
 * controllable.
 */
#define IO_TLB_SHIFT 11
#define IO_TLB_SIZE (1 << IO_TLB_SHIFT)

extern void swiotlb_init(int verbose);
int swiotlb_init_with_tbl(char *tlb, unsigned long nslabs, int verbose);
extern unsigned long swiotlb_nr_tbl(void);
unsigned long swiotlb_size_or_default(void);
extern int swiotlb_late_init_with_tbl(char *tlb, unsigned long nslabs);
extern int swiotlb_late_init_with_default_size(size_t default_size);
extern void __init swiotlb_update_mem_attributes(void);

/*
 * Enumeration for sync targets
 */
enum dma_sync_target {
	SYNC_FOR_CPU = 0,
	SYNC_FOR_DEVICE = 1,
};

phys_addr_t swiotlb_tbl_map_single(struct device *hwdev, phys_addr_t phys,
		size_t mapping_size, size_t alloc_size,
		enum dma_data_direction dir, unsigned long attrs);

extern void swiotlb_tbl_unmap_single(struct device *hwdev,
				     phys_addr_t tlb_addr,
				     size_t mapping_size,
				     size_t alloc_size,
				     enum dma_data_direction dir,
				     unsigned long attrs);

extern void swiotlb_tbl_sync_single(struct device *hwdev,
				    phys_addr_t tlb_addr,
				    size_t size, enum dma_data_direction dir,
				    enum dma_sync_target target);

dma_addr_t swiotlb_map(struct device *dev, phys_addr_t phys,
		size_t size, enum dma_data_direction dir, unsigned long attrs);

#ifdef CONFIG_SWIOTLB
extern enum swiotlb_force swiotlb_force;
extern phys_addr_t io_tlb_start, io_tlb_end;

static inline bool is_swiotlb_buffer(phys_addr_t paddr)
{
	return paddr >= io_tlb_start && paddr < io_tlb_end;
}

void __init swiotlb_exit(void);
unsigned int swiotlb_max_segment(void);
size_t swiotlb_max_mapping_size(struct device *dev);
bool is_swiotlb_active(void);
#else
#define swiotlb_force SWIOTLB_NO_FORCE
static inline bool is_swiotlb_buffer(phys_addr_t paddr)
{
	return false;
}
static inline void swiotlb_exit(void)
{
}
static inline unsigned int swiotlb_max_segment(void)
{
	return 0;
}
static inline size_t swiotlb_max_mapping_size(struct device *dev)
{
	return SIZE_MAX;
}

static inline bool is_swiotlb_active(void)
{
	return false;
}
#endif /* CONFIG_SWIOTLB */

extern void swiotlb_print_info(void);
extern void swiotlb_set_max_segment(unsigned int);

#endif /* __LINUX_SWIOTLB_H */
