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

struct iommu_table;

struct iommu_tbl_ops {
	unsigned long	(*cookie_to_index)(u64, void *);
	void		(*demap)(void *, unsigned long, unsigned long);
	void		(*reset)(struct iommu_table *);
};

struct iommu_table {
	unsigned long		page_table_map_base;
	unsigned long		page_table_shift;
	unsigned long		nr_pools;
	const struct iommu_tbl_ops  *iommu_tbl_ops;
	unsigned long		poolsize;
	struct iommu_pool	arena_pool[IOMMU_NR_POOLS];
	u32			flags;
#define	IOMMU_HAS_LARGE_POOL	0x00000001
	struct iommu_pool	large_pool;
	unsigned long		*map;
};

extern void iommu_tbl_pool_init(struct iommu_table *iommu,
				unsigned long num_entries,
				u32 page_table_shift,
				const struct iommu_tbl_ops *iommu_tbl_ops,
				bool large_pool, u32 npools);

extern unsigned long iommu_tbl_range_alloc(struct device *dev,
					   struct iommu_table *iommu,
					   unsigned long npages,
					   unsigned long *handle,
					   unsigned int pool_hash);

extern void iommu_tbl_range_free(struct iommu_table *iommu,
				 u64 dma_addr, unsigned long npages,
				 bool do_demap, void *demap_arg);

#endif
