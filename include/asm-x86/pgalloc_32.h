#ifndef _I386_PGALLOC_H
#define _I386_PGALLOC_H

static inline void pmd_populate_kernel(struct mm_struct *mm,
				       pmd_t *pmd, pte_t *pte)
{
	paravirt_alloc_pt(mm, __pa(pte) >> PAGE_SHIFT);
	set_pmd(pmd, __pmd(__pa(pte) | _PAGE_TABLE));
}

static inline void pmd_populate(struct mm_struct *mm, pmd_t *pmd, struct page *pte)
{
	unsigned long pfn = page_to_pfn(pte);

	paravirt_alloc_pt(mm, pfn);
	set_pmd(pmd, __pmd(((pteval_t)pfn << PAGE_SHIFT) | _PAGE_TABLE));
}
#define pmd_pgtable(pmd) pmd_page(pmd)

/*
 * Allocate and free page tables.
 */

static inline void pte_free_kernel(struct mm_struct *mm, pte_t *pte)
{
	free_page((unsigned long)pte);
}

static inline void pte_free(struct mm_struct *mm, pgtable_t pte)
{
	pgtable_page_dtor(pte);
	__free_page(pte);
}


extern void __pte_free_tlb(struct mmu_gather *tlb, struct page *pte);

#ifdef CONFIG_X86_PAE
/*
 * In the PAE case we free the pmds as part of the pgd.
 */
static inline pmd_t *pmd_alloc_one(struct mm_struct *mm, unsigned long addr)
{
	return (pmd_t *)get_zeroed_page(GFP_KERNEL|__GFP_REPEAT);
}

static inline void pmd_free(struct mm_struct *mm, pmd_t *pmd)
{
	BUG_ON((unsigned long)pmd & (PAGE_SIZE-1));
	free_page((unsigned long)pmd);
}

extern void __pmd_free_tlb(struct mmu_gather *tlb, pmd_t *pmd);

extern void pud_populate(struct mm_struct *mm, pud_t *pudp, pmd_t *pmd);

#endif	/* CONFIG_X86_PAE */

#endif /* _I386_PGALLOC_H */
