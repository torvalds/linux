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

#ifndef __HAVE_ARCH_HUGE_PTEP_GET_AND_CLEAR
static inline pte_t huge_ptep_get_and_clear(struct mm_struct *mm,
		unsigned long addr, pte_t *ptep)
{
	return ptep_get_and_clear(mm, addr, ptep);
}
#endif

#ifndef __HAVE_ARCH_HUGE_PTEP_CLEAR_FLUSH
static inline void huge_ptep_clear_flush(struct vm_area_struct *vma,
		unsigned long addr, pte_t *ptep)
{
	ptep_clear_flush(vma, addr, ptep);
}
#endif

#ifndef __HAVE_ARCH_HUGE_PTE_NONE
static inline int huge_pte_none(pte_t pte)
{
	return pte_none(pte);
}
#endif

#ifndef __HAVE_ARCH_HUGE_PTE_WRPROTECT
static inline pte_t huge_pte_wrprotect(pte_t pte)
{
	return pte_wrprotect(pte);
}
#endif

#ifndef __HAVE_ARCH_PREPARE_HUGEPAGE_RANGE
static inline int prepare_hugepage_range(struct file *file,
		unsigned long addr, unsigned long len)
{
	struct hstate *h = hstate_file(file);

	if (len & ~huge_page_mask(h))
		return -EINVAL;
	if (addr & ~huge_page_mask(h))
		return -EINVAL;

	return 0;
}
#endif

#ifndef __HAVE_ARCH_HUGE_PTEP_SET_WRPROTECT
static inline void huge_ptep_set_wrprotect(struct mm_struct *mm,
		unsigned long addr, pte_t *ptep)
{
	ptep_set_wrprotect(mm, addr, ptep);
}
#endif

#ifndef __HAVE_ARCH_HUGE_PTEP_SET_ACCESS_FLAGS
static inline int huge_ptep_set_access_flags(struct vm_area_struct *vma,
		unsigned long addr, pte_t *ptep,
		pte_t pte, int dirty)
{
	return ptep_set_access_flags(vma, addr, ptep, pte, dirty);
}
#endif

#ifndef __HAVE_ARCH_HUGE_PTEP_GET
static inline pte_t huge_ptep_get(pte_t *ptep)
{
	return *ptep;
}
#endif

#ifndef __HAVE_ARCH_GIGANTIC_PAGE_RUNTIME_SUPPORTED
static inline bool gigantic_page_runtime_supported(void)
{
	return IS_ENABLED(CONFIG_ARCH_HAS_GIGANTIC_PAGE);
}
#endif /* __HAVE_ARCH_GIGANTIC_PAGE_RUNTIME_SUPPORTED */

#endif /* _ASM_GENERIC_HUGETLB_H */
