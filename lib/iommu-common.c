/*
 * IOMMU mmap management and range allocation functions.
 * Based almost entirely upon the powerpc iommu allocator.
 */

#include <linux/export.h>
#include <linux/bitmap.h>
#include <linux/bug.h>
#include <linux/iommu-helper.h>
#include <linux/iommu-common.h>
#include <linux/dma-mapping.h>

#ifndef	DMA_ERROR_CODE
#define	DMA_ERROR_CODE (~(dma_addr_t)0x0)
#endif

#define IOMMU_LARGE_ALLOC	15

/*
 * Initialize iommu_pool entries for the iommu_table. `num_entries'
 * is the number of table entries. If `large_pool' is set to true,
 * the top 1/4 of the table will be set aside for pool allocations
 * of more than IOMMU_LARGE_ALLOC pages.
 */
extern void iommu_tbl_pool_init(struct iommu_table *iommu,
				unsigned long num_entries,
				u32 page_table_shift,
				const struct iommu_tbl_ops *iommu_tbl_ops,
				bool large_pool, u32 npools)
{
	unsigned int start, i;
	struct iommu_pool *p = &(iommu->large_pool);

	if (npools == 0)
		iommu->nr_pools = IOMMU_NR_POOLS;
	else
		iommu->nr_pools = npools;
	BUG_ON(npools > IOMMU_NR_POOLS);

	iommu->page_table_shift = page_table_shift;
	iommu->iommu_tbl_ops = iommu_tbl_ops;
	start = 0;
	if (large_pool)
		iommu->flags |= IOMMU_HAS_LARGE_POOL;

	if (!large_pool)
		iommu->poolsize = num_entries/iommu->nr_pools;
	else
		iommu->poolsize = (num_entries * 3 / 4)/iommu->nr_pools;
	for (i = 0; i < iommu->nr_pools; i++) {
		spin_lock_init(&(iommu->arena_pool[i].lock));
		iommu->arena_pool[i].start = start;
		iommu->arena_pool[i].hint = start;
		start += iommu->poolsize; /* start for next pool */
		iommu->arena_pool[i].end = start - 1;
	}
	if (!large_pool)
		return;
	/* initialize large_pool */
	spin_lock_init(&(p->lock));
	p->start = start;
	p->hint = p->start;
	p->end = num_entries;
}
EXPORT_SYMBOL(iommu_tbl_pool_init);

unsigned long iommu_tbl_range_alloc(struct device *dev,
				struct iommu_table *iommu,
				unsigned long npages,
				unsigned long *handle,
				unsigned int pool_hash)
{
	unsigned long n, end, start, limit, boundary_size;
	struct iommu_pool *arena;
	int pass = 0;
	unsigned int pool_nr;
	unsigned int npools = iommu->nr_pools;
	unsigned long flags;
	bool large_pool = ((iommu->flags & IOMMU_HAS_LARGE_POOL) != 0);
	bool largealloc = (large_pool && npages > IOMMU_LARGE_ALLOC);
	unsigned long shift;

	/* Sanity check */
	if (unlikely(npages == 0)) {
		printk_ratelimited("npages == 0\n");
		return DMA_ERROR_CODE;
	}

	if (largealloc) {
		arena = &(iommu->large_pool);
		spin_lock_irqsave(&arena->lock, flags);
		pool_nr = 0; /* to keep compiler happy */
	} else {
		/* pick out pool_nr */
		pool_nr =  pool_hash & (npools - 1);
		arena = &(iommu->arena_pool[pool_nr]);

		/* find first available unlocked pool */
		while (!spin_trylock_irqsave(&(arena->lock), flags)) {
			pool_nr = (pool_nr + 1) & (iommu->nr_pools - 1);
			arena = &(iommu->arena_pool[pool_nr]);
		}
	}

 again:
	if (pass == 0 && handle && *handle &&
	    (*handle >= arena->start) && (*handle < arena->end))
		start = *handle;
	else
		start = arena->hint;

	limit = arena->end;

	/* The case below can happen if we have a small segment appended
	 * to a large, or when the previous alloc was at the very end of
	 * the available space. If so, go back to the beginning and flush.
	 */
	if (start >= limit) {
		start = arena->start;
		if (iommu->iommu_tbl_ops->reset != NULL)
			iommu->iommu_tbl_ops->reset(iommu);
	}

	if (dev)
		boundary_size = ALIGN(dma_get_seg_boundary(dev) + 1,
				      1 << iommu->page_table_shift);
	else
		boundary_size = ALIGN(1ULL << 32, 1 << iommu->page_table_shift);

	shift = iommu->page_table_map_base >> iommu->page_table_shift;
	boundary_size = boundary_size >> iommu->page_table_shift;
	/*
	 * if the iommu has a non-trivial cookie <-> index mapping, we set
	 * things up so that iommu_is_span_boundary() merely checks if the
	 * (index + npages) < num_tsb_entries
	 */
	if (iommu->iommu_tbl_ops->cookie_to_index != NULL) {
		shift = 0;
		boundary_size = iommu->poolsize * iommu->nr_pools;
	}
	n = iommu_area_alloc(iommu->map, limit, start, npages, shift,
			     boundary_size, 0);
	if (n == -1) {
		if (likely(pass == 0)) {
			/* First failure, rescan from the beginning.  */
			arena->hint = arena->start;
			if (iommu->iommu_tbl_ops->reset != NULL)
				iommu->iommu_tbl_ops->reset(iommu);
			pass++;
			goto again;
		} else if (!largealloc && pass <= iommu->nr_pools) {
			spin_unlock(&(arena->lock));
			pool_nr = (pool_nr + 1) & (iommu->nr_pools - 1);
			arena = &(iommu->arena_pool[pool_nr]);
			while (!spin_trylock(&(arena->lock))) {
				pool_nr = (pool_nr + 1) & (iommu->nr_pools - 1);
				arena = &(iommu->arena_pool[pool_nr]);
			}
			arena->hint = arena->start;
			pass++;
			goto again;
		} else {
			/* give up */
			spin_unlock_irqrestore(&(arena->lock), flags);
			return DMA_ERROR_CODE;
		}
	}

	end = n + npages;

	arena->hint = end;

	/* Update handle for SG allocations */
	if (handle)
		*handle = end;
	spin_unlock_irqrestore(&(arena->lock), flags);

	return n;
}
EXPORT_SYMBOL(iommu_tbl_range_alloc);

static struct iommu_pool *get_pool(struct iommu_table *tbl,
				   unsigned long entry)
{
	struct iommu_pool *p;
	unsigned long largepool_start = tbl->large_pool.start;
	bool large_pool = ((tbl->flags & IOMMU_HAS_LARGE_POOL) != 0);

	/* The large pool is the last pool at the top of the table */
	if (large_pool && entry >= largepool_start) {
		p = &tbl->large_pool;
	} else {
		unsigned int pool_nr = entry / tbl->poolsize;

		BUG_ON(pool_nr >= tbl->nr_pools);
		p = &tbl->arena_pool[pool_nr];
	}
	return p;
}

void iommu_tbl_range_free(struct iommu_table *iommu, u64 dma_addr,
			  unsigned long npages, bool do_demap, void *demap_arg)
{
	unsigned long entry;
	struct iommu_pool *pool;
	unsigned long flags;
	unsigned long shift = iommu->page_table_shift;

	if (iommu->iommu_tbl_ops->cookie_to_index != NULL) {
		entry = (*iommu->iommu_tbl_ops->cookie_to_index)(dma_addr,
								 demap_arg);
	} else {
		entry = (dma_addr - iommu->page_table_map_base) >> shift;
	}
	pool = get_pool(iommu, entry);

	spin_lock_irqsave(&(pool->lock), flags);
	if (do_demap && iommu->iommu_tbl_ops->demap != NULL)
		(*iommu->iommu_tbl_ops->demap)(demap_arg, entry, npages);

	bitmap_clear(iommu->map, entry, npages);
	spin_unlock_irqrestore(&(pool->lock), flags);
}
EXPORT_SYMBOL(iommu_tbl_range_free);
