/*
 * linux/include/asm-xtensa/pgalloc.h
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Copyright (C) 2001-2005 Tensilica Inc.
 */

#ifndef _XTENSA_PGALLOC_H
#define _XTENSA_PGALLOC_H

#ifdef __KERNEL__

#include <linux/config.h>
#include <linux/threads.h>
#include <linux/highmem.h>
#include <asm/processor.h>
#include <asm/cacheflush.h>


/* Cache aliasing:
 *
 * If the cache size for one way is greater than the page size, we have to
 * deal with cache aliasing. The cache index is wider than the page size:
 *
 *      |cache |
 * |pgnum |page|	virtual address
 * |xxxxxX|zzzz|
 * |      |    |
 *   \  / |    |
 *  trans.|    |
 *   /  \ |    |
 * |yyyyyY|zzzz|	physical address
 *
 * When the page number is translated to the physical page address, the lowest
 * bit(s) (X) that are also part of the cache index are also translated (Y).
 * If this translation changes this bit (X), the cache index is also afected,
 * thus resulting in a different cache line than before.
 * The kernel does not provide a mechanism to ensure that the page color
 * (represented by this bit) remains the same when allocated or when pages
 * are remapped. When user pages are mapped into kernel space, the color of
 * the page might also change.
 *
 * We use the address space VMALLOC_END ... VMALLOC_END + DCACHE_WAY_SIZE * 2
 * to temporarily map a patch so we can match the color.
 */

#if (DCACHE_WAY_SIZE > PAGE_SIZE)
# define PAGE_COLOR_MASK	(PAGE_MASK & (DCACHE_WAY_SIZE-1))
# define PAGE_COLOR(a)		\
	(((unsigned long)(a)&PAGE_COLOR_MASK) >> PAGE_SHIFT)
# define PAGE_COLOR_EQ(a,b)	\
	((((unsigned long)(a) ^ (unsigned long)(b)) & PAGE_COLOR_MASK) == 0)
# define PAGE_COLOR_MAP0(v)	\
	(VMALLOC_END + ((unsigned long)(v) & PAGE_COLOR_MASK))
# define PAGE_COLOR_MAP1(v)	\
	(VMALLOC_END + ((unsigned long)(v) & PAGE_COLOR_MASK) + DCACHE_WAY_SIZE)
#endif

/*
 * Allocating and freeing a pmd is trivial: the 1-entry pmd is
 * inside the pgd, so has no extra memory associated with it.
 */

#define pgd_free(pgd)	free_page((unsigned long)(pgd))

#if (DCACHE_WAY_SIZE > PAGE_SIZE) && XCHAL_DCACHE_IS_WRITEBACK

static inline void
pmd_populate_kernel(struct mm_struct *mm, pmd_t *pmdp, pte_t *pte)
{
	pmd_val(*(pmdp)) = (unsigned long)(pte);
	__asm__ __volatile__ ("memw; dhwb %0, 0; dsync" :: "a" (pmdp));
}

static inline void
pmd_populate(struct mm_struct *mm, pmd_t *pmdp, struct page *page)
{
	pmd_val(*(pmdp)) = (unsigned long)page_to_virt(page);
	__asm__ __volatile__ ("memw; dhwb %0, 0; dsync" :: "a" (pmdp));
}



#else

# define pmd_populate_kernel(mm, pmdp, pte)				     \
	(pmd_val(*(pmdp)) = (unsigned long)(pte))
# define pmd_populate(mm, pmdp, page)					     \
	(pmd_val(*(pmdp)) = (unsigned long)page_to_virt(page))

#endif

static inline pgd_t*
pgd_alloc(struct mm_struct *mm)
{
	pgd_t *pgd;

	pgd = (pgd_t *)__get_free_pages(GFP_KERNEL|__GFP_ZERO, PGD_ORDER);

	if (likely(pgd != NULL))
		__flush_dcache_page((unsigned long)pgd);

	return pgd;
}

extern pte_t* pte_alloc_one_kernel(struct mm_struct* mm, unsigned long addr);
extern struct page* pte_alloc_one(struct mm_struct* mm, unsigned long addr);

#define pte_free_kernel(pte) free_page((unsigned long)pte)
#define pte_free(pte) __free_page(pte)

#endif /* __KERNEL__ */
#endif /* _XTENSA_PGALLOC_H */
