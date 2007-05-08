#ifndef __ASM_SH_PGALLOC_H
#define __ASM_SH_PGALLOC_H

#include <linux/quicklist.h>
#include <asm/page.h>

#define QUICK_PGD 0	/* We preserve special mappings over free */
#define QUICK_PT 1	/* Other page table pages that are zero on free */

static inline void pmd_populate_kernel(struct mm_struct *mm, pmd_t *pmd,
				       pte_t *pte)
{
	set_pmd(pmd, __pmd((unsigned long)pte));
}

static inline void pmd_populate(struct mm_struct *mm, pmd_t *pmd,
				struct page *pte)
{
	set_pmd(pmd, __pmd((unsigned long)page_address(pte)));
}

static inline void pgd_ctor(void *x)
{
	pgd_t *pgd = x;

	memcpy(pgd + USER_PTRS_PER_PGD,
	       swapper_pg_dir + USER_PTRS_PER_PGD,
	       (PTRS_PER_PGD - USER_PTRS_PER_PGD) * sizeof(pgd_t));
}

/*
 * Allocate and free page tables.
 */
static inline pgd_t *pgd_alloc(struct mm_struct *mm)
{
	return quicklist_alloc(QUICK_PGD, GFP_KERNEL | __GFP_REPEAT, pgd_ctor);
}

static inline void pgd_free(pgd_t *pgd)
{
	quicklist_free(QUICK_PGD, NULL, pgd);
}

static inline pte_t *pte_alloc_one_kernel(struct mm_struct *mm,
					  unsigned long address)
{
	return quicklist_alloc(QUICK_PT, GFP_KERNEL | __GFP_REPEAT, NULL);
}

static inline struct page *pte_alloc_one(struct mm_struct *mm,
					 unsigned long address)
{
	void *pg = quicklist_alloc(QUICK_PT, GFP_KERNEL | __GFP_REPEAT, NULL);
	return pg ? virt_to_page(pg) : NULL;
}

static inline void pte_free_kernel(pte_t *pte)
{
	quicklist_free(QUICK_PT, NULL, pte);
}

static inline void pte_free(struct page *pte)
{
	quicklist_free_page(QUICK_PT, NULL, pte);
}

#define __pte_free_tlb(tlb,pte) tlb_remove_page((tlb),(pte))

/*
 * allocating and freeing a pmd is trivial: the 1-entry pmd is
 * inside the pgd, so has no extra memory associated with it.
 */

#define pmd_free(x)			do { } while (0)
#define __pmd_free_tlb(tlb,x)		do { } while (0)

static inline void check_pgt_cache(void)
{
	quicklist_trim(QUICK_PGD, NULL, 25, 16);
	quicklist_trim(QUICK_PT, NULL, 25, 16);
}

#endif /* __ASM_SH_PGALLOC_H */
