// SPDX-License-Identifier: GPL-2.0-only
/* Page fragment allocator
 *
 * Page Fragment:
 *  An arbitrary-length arbitrary-offset area of memory which resides within a
 *  0 or higher order page.  Multiple fragments within that page are
 *  individually refcounted, in the page's reference counter.
 *
 * The page_frag functions provide a simple allocation framework for page
 * fragments.  This is used by the network stack and network device drivers to
 * provide a backing region of memory for use as either an sk_buff->head, or to
 * be used in the "frags" portion of skb_shared_info.
 */

#include <linux/build_bug.h>
#include <linux/export.h>
#include <linux/gfp_types.h>
#include <linux/init.h>
#include <linux/mm.h>
#include <linux/page_frag_cache.h>
#include "internal.h"

static unsigned long encoded_page_create(struct page *page, unsigned int order,
					 bool pfmemalloc)
{
	BUILD_BUG_ON(PAGE_FRAG_CACHE_MAX_ORDER > PAGE_FRAG_CACHE_ORDER_MASK);
	BUILD_BUG_ON(PAGE_FRAG_CACHE_PFMEMALLOC_BIT >= PAGE_SIZE);

	return (unsigned long)page_address(page) |
		(order & PAGE_FRAG_CACHE_ORDER_MASK) |
		((unsigned long)pfmemalloc * PAGE_FRAG_CACHE_PFMEMALLOC_BIT);
}

static unsigned long encoded_page_decode_order(unsigned long encoded_page)
{
	return encoded_page & PAGE_FRAG_CACHE_ORDER_MASK;
}

static void *encoded_page_decode_virt(unsigned long encoded_page)
{
	return (void *)(encoded_page & PAGE_MASK);
}

static struct page *encoded_page_decode_page(unsigned long encoded_page)
{
	return virt_to_page((void *)encoded_page);
}

static struct page *__page_frag_cache_refill(struct page_frag_cache *nc,
					     gfp_t gfp_mask)
{
	unsigned long order = PAGE_FRAG_CACHE_MAX_ORDER;
	struct page *page = NULL;
	gfp_t gfp = gfp_mask;

#if (PAGE_SIZE < PAGE_FRAG_CACHE_MAX_SIZE)
	gfp_mask = (gfp_mask & ~__GFP_DIRECT_RECLAIM) |  __GFP_COMP |
		   __GFP_NOWARN | __GFP_NORETRY | __GFP_NOMEMALLOC;
	page = __alloc_pages(gfp_mask, PAGE_FRAG_CACHE_MAX_ORDER,
			     numa_mem_id(), NULL);
#endif
	if (unlikely(!page)) {
		page = __alloc_pages(gfp, 0, numa_mem_id(), NULL);
		order = 0;
	}

	nc->encoded_page = page ?
		encoded_page_create(page, order, page_is_pfmemalloc(page)) : 0;

	return page;
}

void page_frag_cache_drain(struct page_frag_cache *nc)
{
	if (!nc->encoded_page)
		return;

	__page_frag_cache_drain(encoded_page_decode_page(nc->encoded_page),
				nc->pagecnt_bias);
	nc->encoded_page = 0;
}
EXPORT_SYMBOL(page_frag_cache_drain);

void __page_frag_cache_drain(struct page *page, unsigned int count)
{
	VM_BUG_ON_PAGE(page_ref_count(page) == 0, page);

	if (page_ref_sub_and_test(page, count))
		free_unref_page(page, compound_order(page));
}
EXPORT_SYMBOL(__page_frag_cache_drain);

void *__page_frag_alloc_align(struct page_frag_cache *nc,
			      unsigned int fragsz, gfp_t gfp_mask,
			      unsigned int align_mask)
{
	unsigned long encoded_page = nc->encoded_page;
	unsigned int size, offset;
	struct page *page;

	if (unlikely(!encoded_page)) {
refill:
		page = __page_frag_cache_refill(nc, gfp_mask);
		if (!page)
			return NULL;

		encoded_page = nc->encoded_page;

		/* Even if we own the page, we do not use atomic_set().
		 * This would break get_page_unless_zero() users.
		 */
		page_ref_add(page, PAGE_FRAG_CACHE_MAX_SIZE);

		/* reset page count bias and offset to start of new frag */
		nc->pagecnt_bias = PAGE_FRAG_CACHE_MAX_SIZE + 1;
		nc->offset = 0;
	}

	size = PAGE_SIZE << encoded_page_decode_order(encoded_page);
	offset = __ALIGN_KERNEL_MASK(nc->offset, ~align_mask);
	if (unlikely(offset + fragsz > size)) {
		if (unlikely(fragsz > PAGE_SIZE)) {
			/*
			 * The caller is trying to allocate a fragment
			 * with fragsz > PAGE_SIZE but the cache isn't big
			 * enough to satisfy the request, this may
			 * happen in low memory conditions.
			 * We don't release the cache page because
			 * it could make memory pressure worse
			 * so we simply return NULL here.
			 */
			return NULL;
		}

		page = encoded_page_decode_page(encoded_page);

		if (!page_ref_sub_and_test(page, nc->pagecnt_bias))
			goto refill;

		if (unlikely(encoded_page_decode_pfmemalloc(encoded_page))) {
			free_unref_page(page,
					encoded_page_decode_order(encoded_page));
			goto refill;
		}

		/* OK, page count is 0, we can safely set it */
		set_page_count(page, PAGE_FRAG_CACHE_MAX_SIZE + 1);

		/* reset page count bias and offset to start of new frag */
		nc->pagecnt_bias = PAGE_FRAG_CACHE_MAX_SIZE + 1;
		offset = 0;
	}

	nc->pagecnt_bias--;
	nc->offset = offset + fragsz;

	return encoded_page_decode_virt(encoded_page) + offset;
}
EXPORT_SYMBOL(__page_frag_alloc_align);

/*
 * Frees a page fragment allocated out of either a compound or order 0 page.
 */
void page_frag_free(void *addr)
{
	struct page *page = virt_to_head_page(addr);

	if (unlikely(put_page_testzero(page)))
		free_unref_page(page, compound_order(page));
}
EXPORT_SYMBOL(page_frag_free);
