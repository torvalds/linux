#ifndef _PPC64_PGALLOC_H
#define _PPC64_PGALLOC_H

#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/cpumask.h>
#include <linux/percpu.h>

extern kmem_cache_t *pgtable_cache[];

#define PTE_CACHE_NUM	0
#define PMD_CACHE_NUM	1
#define PUD_CACHE_NUM	1
#define PGD_CACHE_NUM	0

/*
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

static inline pgd_t *pgd_alloc(struct mm_struct *mm)
{
	return kmem_cache_alloc(pgtable_cache[PGD_CACHE_NUM], GFP_KERNEL);
}

static inline void pgd_free(pgd_t *pgd)
{
	kmem_cache_free(pgtable_cache[PGD_CACHE_NUM], pgd);
}

#define pgd_populate(MM, PGD, PUD)	pgd_set(PGD, PUD)

static inline pud_t *pud_alloc_one(struct mm_struct *mm, unsigned long addr)
{
	return kmem_cache_alloc(pgtable_cache[PUD_CACHE_NUM],
				GFP_KERNEL|__GFP_REPEAT);
}

static inline void pud_free(pud_t *pud)
{
	kmem_cache_free(pgtable_cache[PUD_CACHE_NUM], pud);
}

#define pud_populate(MM, PUD, PMD)	pud_set(PUD, PMD)

static inline pmd_t *pmd_alloc_one(struct mm_struct *mm, unsigned long addr)
{
	return kmem_cache_alloc(pgtable_cache[PMD_CACHE_NUM],
				GFP_KERNEL|__GFP_REPEAT);
}

static inline void pmd_free(pmd_t *pmd)
{
	kmem_cache_free(pgtable_cache[PMD_CACHE_NUM], pmd);
}

#define pmd_populate_kernel(mm, pmd, pte) pmd_set(pmd, pte)
#define pmd_populate(mm, pmd, pte_page) \
	pmd_populate_kernel(mm, pmd, page_address(pte_page))

static inline pte_t *pte_alloc_one_kernel(struct mm_struct *mm, unsigned long address)
{
	return kmem_cache_alloc(pgtable_cache[PTE_CACHE_NUM],
				GFP_KERNEL|__GFP_REPEAT);
}

static inline struct page *pte_alloc_one(struct mm_struct *mm, unsigned long address)
{
	return virt_to_page(pte_alloc_one_kernel(mm, address));
}
		
static inline void pte_free_kernel(pte_t *pte)
{
	kmem_cache_free(pgtable_cache[PTE_CACHE_NUM], pte);
}

static inline void pte_free(struct page *ptepage)
{
	pte_free_kernel(page_address(ptepage));
}

#define PGF_CACHENUM_MASK	0xf

typedef struct pgtable_free {
	unsigned long val;
} pgtable_free_t;

static inline pgtable_free_t pgtable_free_cache(void *p, int cachenum,
						unsigned long mask)
{
	BUG_ON(cachenum > PGF_CACHENUM_MASK);

	return (pgtable_free_t){.val = ((unsigned long) p & ~mask) | cachenum};
}

static inline void pgtable_free(pgtable_free_t pgf)
{
	void *p = (void *)(pgf.val & ~PGF_CACHENUM_MASK);
	int cachenum = pgf.val & PGF_CACHENUM_MASK;

	kmem_cache_free(pgtable_cache[cachenum], p);
}

void pgtable_free_tlb(struct mmu_gather *tlb, pgtable_free_t pgf);

#define __pte_free_tlb(tlb, ptepage)	\
	pgtable_free_tlb(tlb, pgtable_free_cache(page_address(ptepage), \
		PTE_CACHE_NUM, PTE_TABLE_SIZE-1))
#define __pmd_free_tlb(tlb, pmd) 	\
	pgtable_free_tlb(tlb, pgtable_free_cache(pmd, \
		PMD_CACHE_NUM, PMD_TABLE_SIZE-1))
#define __pud_free_tlb(tlb, pmd)	\
	pgtable_free_tlb(tlb, pgtable_free_cache(pud, \
		PUD_CACHE_NUM, PUD_TABLE_SIZE-1))

#define check_pgt_cache()	do { } while (0)

#endif /* _PPC64_PGALLOC_H */
