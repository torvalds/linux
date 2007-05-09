#ifndef _ASM_POWERPC_PGALLOC_64_H
#define _ASM_POWERPC_PGALLOC_64_H
/*
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/cpumask.h>
#include <linux/percpu.h>

extern struct kmem_cache *pgtable_cache[];

#define PGD_CACHE_NUM		0
#define PUD_CACHE_NUM		1
#define PMD_CACHE_NUM		1
#define HUGEPTE_CACHE_NUM	2
#define PTE_NONCACHE_NUM	3  /* from GFP rather than kmem_cache */

static inline pgd_t *pgd_alloc(struct mm_struct *mm)
{
	return kmem_cache_alloc(pgtable_cache[PGD_CACHE_NUM], GFP_KERNEL);
}

static inline void pgd_free(pgd_t *pgd)
{
	kmem_cache_free(pgtable_cache[PGD_CACHE_NUM], pgd);
}

#ifndef CONFIG_PPC_64K_PAGES

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

static inline void pud_populate(struct mm_struct *mm, pud_t *pud, pmd_t *pmd)
{
	pud_set(pud, (unsigned long)pmd);
}

#define pmd_populate(mm, pmd, pte_page) \
	pmd_populate_kernel(mm, pmd, page_address(pte_page))
#define pmd_populate_kernel(mm, pmd, pte) pmd_set(pmd, (unsigned long)(pte))


#else /* CONFIG_PPC_64K_PAGES */

#define pud_populate(mm, pud, pmd)	pud_set(pud, (unsigned long)pmd)

static inline void pmd_populate_kernel(struct mm_struct *mm, pmd_t *pmd,
				       pte_t *pte)
{
	pmd_set(pmd, (unsigned long)pte);
}

#define pmd_populate(mm, pmd, pte_page) \
	pmd_populate_kernel(mm, pmd, page_address(pte_page))

#endif /* CONFIG_PPC_64K_PAGES */

static inline pmd_t *pmd_alloc_one(struct mm_struct *mm, unsigned long addr)
{
	return kmem_cache_alloc(pgtable_cache[PMD_CACHE_NUM],
				GFP_KERNEL|__GFP_REPEAT);
}

static inline void pmd_free(pmd_t *pmd)
{
	kmem_cache_free(pgtable_cache[PMD_CACHE_NUM], pmd);
}

static inline pte_t *pte_alloc_one_kernel(struct mm_struct *mm,
					  unsigned long address)
{
        return (pte_t *)__get_free_page(GFP_KERNEL | __GFP_REPEAT | __GFP_ZERO);
}

static inline struct page *pte_alloc_one(struct mm_struct *mm,
					 unsigned long address)
{
	return virt_to_page(pte_alloc_one_kernel(mm, address));
}

static inline void pte_free_kernel(pte_t *pte)
{
	free_page((unsigned long)pte);
}

static inline void pte_free(struct page *ptepage)
{
	__free_page(ptepage);
}

#define PGF_CACHENUM_MASK	0x3

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

	if (cachenum == PTE_NONCACHE_NUM)
		free_page((unsigned long)p);
	else
		kmem_cache_free(pgtable_cache[cachenum], p);
}

extern void pgtable_free_tlb(struct mmu_gather *tlb, pgtable_free_t pgf);

#define __pte_free_tlb(tlb, ptepage)	\
	pgtable_free_tlb(tlb, pgtable_free_cache(page_address(ptepage), \
		PTE_NONCACHE_NUM, PTE_TABLE_SIZE-1))
#define __pmd_free_tlb(tlb, pmd) 	\
	pgtable_free_tlb(tlb, pgtable_free_cache(pmd, \
		PMD_CACHE_NUM, PMD_TABLE_SIZE-1))
#ifndef CONFIG_PPC_64K_PAGES
#define __pud_free_tlb(tlb, pud)	\
	pgtable_free_tlb(tlb, pgtable_free_cache(pud, \
		PUD_CACHE_NUM, PUD_TABLE_SIZE-1))
#endif /* CONFIG_PPC_64K_PAGES */

#define check_pgt_cache()	do { } while (0)

#endif /* _ASM_POWERPC_PGALLOC_64_H */
