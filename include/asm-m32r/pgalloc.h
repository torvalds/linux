#ifndef _ASM_M32R_PGALLOC_H
#define _ASM_M32R_PGALLOC_H

#include <linux/mm.h>

#include <asm/io.h>

#define pmd_populate_kernel(mm, pmd, pte)	\
	set_pmd(pmd, __pmd(_PAGE_TABLE + __pa(pte)))

static __inline__ void pmd_populate(struct mm_struct *mm, pmd_t *pmd,
	struct page *pte)
{
	set_pmd(pmd, __pmd(_PAGE_TABLE + page_to_phys(pte)));
}

/*
 * Allocate and free page tables.
 */
static __inline__ pgd_t *pgd_alloc(struct mm_struct *mm)
{
	pgd_t *pgd = (pgd_t *)__get_free_page(GFP_KERNEL|__GFP_ZERO);

	return pgd;
}

static inline void pgd_free(struct mm_struct *mm, pgd_t *pgd)
{
	free_page((unsigned long)pgd);
}

static __inline__ pte_t *pte_alloc_one_kernel(struct mm_struct *mm,
	unsigned long address)
{
	pte_t *pte = (pte_t *)__get_free_page(GFP_KERNEL|__GFP_ZERO);

	return pte;
}

static __inline__ struct page *pte_alloc_one(struct mm_struct *mm,
	unsigned long address)
{
	struct page *pte = alloc_page(GFP_KERNEL|__GFP_ZERO);


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

#define __pte_free_tlb(tlb, pte)	pte_free((tlb)->mm, (pte))

/*
 * allocating and freeing a pmd is trivial: the 1-entry pmd is
 * inside the pgd, so has no extra memory associated with it.
 * (In the PAE case we free the pmds as part of the pgd.)
 */

#define pmd_alloc_one(mm, addr)		({ BUG(); ((pmd_t *)2); })
#define pmd_free(mm, x)			do { } while (0)
#define __pmd_free_tlb(tlb, x)		do { } while (0)
#define pgd_populate(mm, pmd, pte)	BUG()

#define check_pgt_cache()	do { } while (0)

#endif /* _ASM_M32R_PGALLOC_H */
