/* SPDX-License-Identifier: GPL-2.0 */

#ifndef _LINUX_PAGE_FRAG_CACHE_H
#define _LINUX_PAGE_FRAG_CACHE_H

#include <linux/bits.h>
#include <linux/log2.h>
#include <linux/mm_types_task.h>
#include <linux/types.h>

#if (PAGE_SIZE < PAGE_FRAG_CACHE_MAX_SIZE)
/* Use a full byte here to enable assembler optimization as the shift
 * operation is usually expecting a byte.
 */
#define PAGE_FRAG_CACHE_ORDER_MASK		GENMASK(7, 0)
#else
/* Compiler should be able to figure out we don't read things as any value
 * ANDed with 0 is 0.
 */
#define PAGE_FRAG_CACHE_ORDER_MASK		0
#endif

#define PAGE_FRAG_CACHE_PFMEMALLOC_BIT		(PAGE_FRAG_CACHE_ORDER_MASK + 1)

static inline bool encoded_page_decode_pfmemalloc(unsigned long encoded_page)
{
	return !!(encoded_page & PAGE_FRAG_CACHE_PFMEMALLOC_BIT);
}

static inline void page_frag_cache_init(struct page_frag_cache *nc)
{
	nc->encoded_page = 0;
}

static inline bool page_frag_cache_is_pfmemalloc(struct page_frag_cache *nc)
{
	return encoded_page_decode_pfmemalloc(nc->encoded_page);
}

void page_frag_cache_drain(struct page_frag_cache *nc);
void __page_frag_cache_drain(struct page *page, unsigned int count);
void *__page_frag_alloc_align(struct page_frag_cache *nc, unsigned int fragsz,
			      gfp_t gfp_mask, unsigned int align_mask);

static inline void *page_frag_alloc_align(struct page_frag_cache *nc,
					  unsigned int fragsz, gfp_t gfp_mask,
					  unsigned int align)
{
	WARN_ON_ONCE(!is_power_of_2(align));
	return __page_frag_alloc_align(nc, fragsz, gfp_mask, -align);
}

static inline void *page_frag_alloc(struct page_frag_cache *nc,
				    unsigned int fragsz, gfp_t gfp_mask)
{
	return __page_frag_alloc_align(nc, fragsz, gfp_mask, ~0u);
}

void page_frag_free(void *addr);

#endif
