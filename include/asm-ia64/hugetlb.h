#ifndef _ASM_IA64_HUGETLB_H
#define _ASM_IA64_HUGETLB_H

#include <asm/page.h>


void hugetlb_free_pgd_range(struct mmu_gather **tlb, unsigned long addr,
			    unsigned long end, unsigned long floor,
			    unsigned long ceiling);

int prepare_hugepage_range(unsigned long addr, unsigned long len);

static inline int is_hugepage_only_range(struct mm_struct *mm,
					 unsigned long addr,
					 unsigned long len)
{
	return (REGION_NUMBER(addr) == RGN_HPAGE ||
		REGION_NUMBER((addr)+(len)-1) == RGN_HPAGE);
}

static inline void hugetlb_prefault_arch_hook(struct mm_struct *mm)
{
}

static inline void set_huge_pte_at(struct mm_struct *mm, unsigned long addr,
				   pte_t *ptep, pte_t pte)
{
	set_pte_at(mm, addr, ptep, pte);
}

static inline pte_t huge_ptep_get_and_clear(struct mm_struct *mm,
					    unsigned long addr, pte_t *ptep)
{
	return ptep_get_and_clear(mm, addr, ptep);
}

#endif /* _ASM_IA64_HUGETLB_H */
