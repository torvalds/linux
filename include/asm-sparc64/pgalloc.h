/* $Id: pgalloc.h,v 1.30 2001/12/21 04:56:17 davem Exp $ */
#ifndef _SPARC64_PGALLOC_H
#define _SPARC64_PGALLOC_H

#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/quicklist.h>

#include <asm/spitfire.h>
#include <asm/cpudata.h>
#include <asm/cacheflush.h>
#include <asm/page.h>

/* Page table allocation/freeing. */

static inline pgd_t *pgd_alloc(struct mm_struct *mm)
{
	return quicklist_alloc(0, GFP_KERNEL, NULL);
}

static inline void pgd_free(struct mm_struct *mm, pgd_t *pgd)
{
	quicklist_free(0, NULL, pgd);
}

#define pud_populate(MM, PUD, PMD)	pud_set(PUD, PMD)

static inline pmd_t *pmd_alloc_one(struct mm_struct *mm, unsigned long addr)
{
	return quicklist_alloc(0, GFP_KERNEL, NULL);
}

static inline void pmd_free(struct mm_struct *mm, pmd_t *pmd)
{
	quicklist_free(0, NULL, pmd);
}

static inline pte_t *pte_alloc_one_kernel(struct mm_struct *mm,
					  unsigned long address)
{
	return quicklist_alloc(0, GFP_KERNEL, NULL);
}

static inline struct page *pte_alloc_one(struct mm_struct *mm,
					 unsigned long address)
{
	void *pg = quicklist_alloc(0, GFP_KERNEL, NULL);
	return pg ? virt_to_page(pg) : NULL;
}
		
static inline void pte_free_kernel(struct mm_struct *mm, pte_t *pte)
{
	quicklist_free(0, NULL, pte);
}

static inline void pte_free(struct mm_struct *mm, struct page *ptepage)
{
	quicklist_free_page(0, NULL, ptepage);
}


#define pmd_populate_kernel(MM, PMD, PTE)	pmd_set(PMD, PTE)
#define pmd_populate(MM,PMD,PTE_PAGE)		\
	pmd_populate_kernel(MM,PMD,page_address(PTE_PAGE))

static inline void check_pgt_cache(void)
{
	quicklist_trim(0, NULL, 25, 16);
}

#endif /* _SPARC64_PGALLOC_H */
