/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_GENERIC_HUGETLB_H
#define _ASM_GENERIC_HUGETLB_H

static inline pte_t mk_huge_pte(struct page *page, pgprot_t pgprot)
{
	return mk_pte(page, pgprot);
}

static inline unsigned long huge_pte_write(pte_t pte)
{
	return pte_write(pte);
}

static inline unsigned long huge_pte_dirty(pte_t pte)
{
	return pte_dirty(pte);
}

static inline pte_t huge_pte_mkwrite(pte_t pte)
{
	return pte_mkwrite(pte);
}

static inline pte_t huge_pte_mkdirty(pte_t pte)
{
	return pte_mkdirty(pte);
}

static inline pte_t huge_pte_modify(pte_t pte, pgprot_t newprot)
{
	return pte_modify(pte, newprot);
}

#ifndef __HAVE_ARCH_HUGE_PTE_CLEAR
static inline void huge_pte_clear(struct mm_struct *mm, unsigned long addr,
		    pte_t *ptep, unsigned long sz)
{
	pte_clear(mm, addr, ptep);
}
#endif

#ifndef __HAVE_ARCH_HUGETLB_FREE_PGD_RANGE
static inline void hugetlb_free_pgd_range(struct mmu_gather *tlb,
		unsigned long addr, unsigned long end,
		unsigned long floor, unsigned long ceiling)
{
	free_pgd_range(tlb, addr, end, floor, ceiling);
}
#endif

#ifndef __HAVE_ARCH_HUGE_SET_HUGE_PTE_AT
static inline void set_huge_pte_at(struct mm_struct *mm, unsigned long addr,
		pte_t *ptep, pte_t pte)
{
	set_pte_at(mm, addr, ptep, pte);
}
#endif

#endif /* _ASM_GENERIC_HUGETLB_H */
