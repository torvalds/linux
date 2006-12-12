#ifndef __ASM_SH_PGALLOC_H
#define __ASM_SH_PGALLOC_H

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

/*
 * Allocate and free page tables.
 */
static inline pgd_t *pgd_alloc(struct mm_struct *mm)
{
	pgd_t *pgd = (pgd_t *)__get_free_page(GFP_KERNEL | __GFP_REPEAT);

	if (pgd) {
		memset(pgd, 0, USER_PTRS_PER_PGD * sizeof(pgd_t));
		memcpy(pgd + USER_PTRS_PER_PGD,
		       swapper_pg_dir + USER_PTRS_PER_PGD,
		       (PTRS_PER_PGD - USER_PTRS_PER_PGD) * sizeof(pgd_t));
	}

	return pgd;
}

static inline void pgd_free(pgd_t *pgd)
{
	free_page((unsigned long)pgd);
}

static inline pte_t *pte_alloc_one_kernel(struct mm_struct *mm,
					  unsigned long address)
{
	return (pte_t *)__get_free_page(GFP_KERNEL | __GFP_REPEAT | __GFP_ZERO);
}

static inline struct page *pte_alloc_one(struct mm_struct *mm,
					 unsigned long address)
{
	return alloc_page(GFP_KERNEL | __GFP_REPEAT | __GFP_ZERO);
}

static inline void pte_free_kernel(pte_t *pte)
{
	free_page((unsigned long)pte);
}

static inline void pte_free(struct page *pte)
{
	__free_page(pte);
}

#define __pte_free_tlb(tlb,pte) tlb_remove_page((tlb),(pte))

/*
 * allocating and freeing a pmd is trivial: the 1-entry pmd is
 * inside the pgd, so has no extra memory associated with it.
 */

#define pmd_free(x)			do { } while (0)
#define __pmd_free_tlb(tlb,x)		do { } while (0)
#define check_pgt_cache()		do { } while (0)

#endif /* __ASM_SH_PGALLOC_H */
