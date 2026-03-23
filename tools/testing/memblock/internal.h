/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef _MM_INTERNAL_H
#define _MM_INTERNAL_H

/*
 * Enable memblock_dbg() messages
 */
#ifdef MEMBLOCK_DEBUG
static int memblock_debug = 1;
#endif

#define pr_warn_ratelimited(fmt, ...)    printf(fmt, ##__VA_ARGS__)

#define K(x) ((x) << (PAGE_SHIFT-10))

bool mirrored_kernelcore = false;

struct page {};
static inline void *page_address(struct page *page)
{
	BUG();
	return page;
}

static inline struct page *virt_to_page(void *virt)
{
	BUG();
	return virt;
}

void memblock_free_pages(unsigned long pfn, unsigned int order)
{
}

static inline void accept_memory(phys_addr_t start, unsigned long size)
{
}

unsigned long free_reserved_area(void *start, void *end, int poison, const char *s);
void free_reserved_page(struct page *page);

static inline bool deferred_pages_enabled(void)
{
	return false;
}

#define for_each_valid_pfn(pfn, start_pfn, end_pfn)			 \
	for ((pfn) = (start_pfn); (pfn) < (end_pfn); (pfn)++)

static inline void *kasan_reset_tag(const void *addr)
{
	return (void *)addr;
}

static inline bool __is_kernel(unsigned long addr)
{
	return false;
}

#define for_each_valid_pfn(pfn, start_pfn, end_pfn)                     \
       for ((pfn) = (start_pfn); (pfn) < (end_pfn); (pfn)++)

static inline void init_deferred_page(unsigned long pfn, int nid)
{
}

#define __SetPageReserved(p)	((void)(p))

#endif
