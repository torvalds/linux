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

#define __SetPageReserved(p)	((void)(p))

#endif
