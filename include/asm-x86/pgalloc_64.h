#ifndef _X86_64_PGALLOC_H
#define _X86_64_PGALLOC_H

#include <asm/pda.h>

static inline void pud_populate(struct mm_struct *mm, pud_t *pud, pmd_t *pmd)
{
	set_pud(pud, __pud(_PAGE_TABLE | __pa(pmd)));
}

static inline void pgd_populate(struct mm_struct *mm, pgd_t *pgd, pud_t *pud)
{
	set_pgd(pgd, __pgd(_PAGE_TABLE | __pa(pud)));
}

static inline pud_t *pud_alloc_one(struct mm_struct *mm, unsigned long addr)
{
	return (pud_t *)get_zeroed_page(GFP_KERNEL|__GFP_REPEAT);
}

static inline void pud_free(struct mm_struct *mm, pud_t *pud)
{
	BUG_ON((unsigned long)pud & (PAGE_SIZE-1));
	free_page((unsigned long)pud);
}

extern void __pud_free_tlb(struct mmu_gather *tlb, pud_t *pud);

#endif /* _X86_64_PGALLOC_H */
