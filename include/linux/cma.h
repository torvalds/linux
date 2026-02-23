/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __CMA_H__
#define __CMA_H__

#include <linux/init.h>
#include <linux/types.h>
#include <linux/numa.h>

#ifdef CONFIG_CMA_AREAS
#define MAX_CMA_AREAS	CONFIG_CMA_AREAS
#endif

#define CMA_MAX_NAME 64

/*
 *  the buddy -- especially pageblock merging and alloc_contig_range()
 * -- can deal with only some pageblocks of a higher-order page being
 *  MIGRATE_CMA, we can use pageblock_nr_pages.
 */
#define CMA_MIN_ALIGNMENT_PAGES pageblock_nr_pages
#define CMA_MIN_ALIGNMENT_BYTES (PAGE_SIZE * CMA_MIN_ALIGNMENT_PAGES)

struct cma;

extern unsigned long totalcma_pages;
extern phys_addr_t cma_get_base(const struct cma *cma);
extern unsigned long cma_get_size(const struct cma *cma);
extern const char *cma_get_name(const struct cma *cma);

extern int __init cma_declare_contiguous_nid(phys_addr_t base,
			phys_addr_t size, phys_addr_t limit,
			phys_addr_t alignment, unsigned int order_per_bit,
			bool fixed, const char *name, struct cma **res_cma,
			int nid);
static inline int __init cma_declare_contiguous(phys_addr_t base,
			phys_addr_t size, phys_addr_t limit,
			phys_addr_t alignment, unsigned int order_per_bit,
			bool fixed, const char *name, struct cma **res_cma)
{
	return cma_declare_contiguous_nid(base, size, limit, alignment,
			order_per_bit, fixed, name, res_cma, NUMA_NO_NODE);
}
extern int __init cma_declare_contiguous_multi(phys_addr_t size,
			phys_addr_t align, unsigned int order_per_bit,
			const char *name, struct cma **res_cma, int nid);
extern int cma_init_reserved_mem(phys_addr_t base, phys_addr_t size,
					unsigned int order_per_bit,
					const char *name,
					struct cma **res_cma);
extern struct page *cma_alloc(struct cma *cma, unsigned long count, unsigned int align,
			      bool no_warn);
extern bool cma_release(struct cma *cma, const struct page *pages, unsigned long count);

struct page *cma_alloc_frozen(struct cma *cma, unsigned long count,
		unsigned int align, bool no_warn);
struct page *cma_alloc_frozen_compound(struct cma *cma, unsigned int order);
bool cma_release_frozen(struct cma *cma, const struct page *pages,
		unsigned long count);

extern int cma_for_each_area(int (*it)(struct cma *cma, void *data), void *data);
extern bool cma_intersects(struct cma *cma, unsigned long start, unsigned long end);

extern void cma_reserve_pages_on_error(struct cma *cma);

#ifdef CONFIG_DMA_CMA
extern bool cma_skip_dt_default_reserved_mem(void);
#else
static inline bool cma_skip_dt_default_reserved_mem(void)
{
	return false;
}
#endif

#endif
