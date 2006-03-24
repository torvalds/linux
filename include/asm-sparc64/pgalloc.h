/* $Id: pgalloc.h,v 1.30 2001/12/21 04:56:17 davem Exp $ */
#ifndef _SPARC64_PGALLOC_H
#define _SPARC64_PGALLOC_H

#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/slab.h>

#include <asm/spitfire.h>
#include <asm/cpudata.h>
#include <asm/cacheflush.h>
#include <asm/page.h>

/* Page table allocation/freeing. */
extern kmem_cache_t *pgtable_cache;

static inline pgd_t *pgd_alloc(struct mm_struct *mm)
{
	return kmem_cache_alloc(pgtable_cache, GFP_KERNEL);
}

static inline void pgd_free(pgd_t *pgd)
{
	kmem_cache_free(pgtable_cache, pgd);
}

#define pud_populate(MM, PUD, PMD)	pud_set(PUD, PMD)

static inline pmd_t *pmd_alloc_one(struct mm_struct *mm, unsigned long addr)
{
	return kmem_cache_alloc(pgtable_cache,
				GFP_KERNEL|__GFP_REPEAT);
}

static inline void pmd_free(pmd_t *pmd)
{
	kmem_cache_free(pgtable_cache, pmd);
}

static inline pte_t *pte_alloc_one_kernel(struct mm_struct *mm,
					  unsigned long address)
{
	return kmem_cache_alloc(pgtable_cache,
				GFP_KERNEL|__GFP_REPEAT);
}

static inline struct page *pte_alloc_one(struct mm_struct *mm,
					 unsigned long address)
{
	return virt_to_page(pte_alloc_one_kernel(mm, address));
}
		
static inline void pte_free_kernel(pte_t *pte)
{
	kmem_cache_free(pgtable_cache, pte);
}

static inline void pte_free(struct page *ptepage)
{
	pte_free_kernel(page_address(ptepage));
}


#define pmd_populate_kernel(MM, PMD, PTE)	pmd_set(PMD, PTE)
#define pmd_populate(MM,PMD,PTE_PAGE)		\
	pmd_populate_kernel(MM,PMD,page_address(PTE_PAGE))

#define check_pgt_cache()	do { } while (0)

#endif /* _SPARC64_PGALLOC_H */
