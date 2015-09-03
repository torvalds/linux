#ifndef _LINUX_IOMMU_COMMON_H
#define _LINUX_IOMMU_COMMON_H

#include <linux/spinlock_types.h>
#include <linux/device.h>
#include <asm/page.h>

#define IOMMU_POOL_HASHBITS     4
#define IOMMU_NR_POOLS          (1 << IOMMU_POOL_HASHBITS)

struct iommu_pool {
	unsigned long	start;
	unsigned long	end;
	unsigned long	hint;
	spinlock_t	lock;
};

struct iommu_map_table {
	unsigned long		table_map_base;
	unsigned long		table_shift;
	unsigned long		nr_pools;
	void			(*lazy_flush)(struct iommu_map_table *);
	unsigned long		poolsize;
	struct iommu_pool	pools[IOMMU_NR_POOLS];
	u32			flags;
#define	IOMMU_HAS_LARGE_POOL	0x00000001
#define	IOMMU_NO_SPAN_BOUND	0x00000002
#define	IOMMU_NEED_FLUSH	0x00000004
	struct iommu_pool	large_pool;
	unsigned long		*map;
};

extern void iommu_tbl_pool_init(struct iommu_map_table *iommu,
				unsigned long num_entries,
				u32 table_shift,
				void (*lazy_flush)(struct iommu_map_table *),
				bool large_pool, u32 npools,
				bool skip_span_boundary_check);

extern unsigned long iommu_tbl_range_alloc(struct device *dev,
					   struct iommu_map_table *iommu,
					   unsigned long npages,
					   unsigned long *handle,
					   unsigned long mask,
					   unsigned int align_order);

extern void iommu_tbl_range_free(struct iommu_map_table *iommu,
				 u64 dma_addr, unsigned long npages,
				 unsigned long entry);

#endif
