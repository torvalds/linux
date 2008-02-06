#ifndef _CRIS_PGALLOC_H
#define _CRIS_PGALLOC_H

#include <linux/threads.h>
#include <linux/mm.h>

#define pmd_populate_kernel(mm, pmd, pte) pmd_set(pmd, pte)
#define pmd_populate(mm, pmd, pte) pmd_set(pmd, page_address(pte))

/*
 * Allocate and free page tables.
 */

static inline pgd_t *pgd_alloc (struct mm_struct *mm)
{
	return (pgd_t *)get_zeroed_page(GFP_KERNEL);
}

static inline void pgd_free(struct mm_struct *mm, pgd_t *pgd)
{
	free_page((unsigned long)pgd);
}

static inline pte_t *pte_alloc_one_kernel(struct mm_struct *mm, unsigned long address)
{
  	pte_t *pte = (pte_t *)__get_free_page(GFP_KERNEL|__GFP_REPEAT|__GFP_ZERO);
 	return pte;
}

static inline struct page *pte_alloc_one(struct mm_struct *mm, unsigned long address)
{
	struct page *pte;
	pte = alloc_pages(GFP_KERNEL|__GFP_REPEAT|__GFP_ZERO, 0);
	return pte;
}

static inline void pte_free_kernel(struct mm_struct *mm, pte_t *pte)
{
	free_page((unsigned long)pte);
}

static inline void pte_free(struct mm_struct *mm, struct page *pte)
{
	__free_page(pte);
}


#define __pte_free_tlb(tlb,pte) tlb_remove_page((tlb),(pte))

#define check_pgt_cache()          do { } while (0)

#endif
