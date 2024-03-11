/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __LINUX_KSM_H
#define __LINUX_KSM_H
/*
 * Memory merging support.
 *
 * This code enables dynamic sharing of identical pages found in different
 * memory areas, even if they are not shared by fork().
 */

#include <linux/bitops.h>
#include <linux/mm.h>
#include <linux/pagemap.h>
#include <linux/rmap.h>
#include <linux/sched.h>
#include <linux/sched/coredump.h>

#ifdef CONFIG_KSM
int ksm_madvise(struct vm_area_struct *vma, unsigned long start,
		unsigned long end, int advice, unsigned long *vm_flags);

void ksm_add_vma(struct vm_area_struct *vma);
int ksm_enable_merge_any(struct mm_struct *mm);
int ksm_disable_merge_any(struct mm_struct *mm);
int ksm_disable(struct mm_struct *mm);

int __ksm_enter(struct mm_struct *mm);
void __ksm_exit(struct mm_struct *mm);
/*
 * To identify zeropages that were mapped by KSM, we reuse the dirty bit
 * in the PTE. If the PTE is dirty, the zeropage was mapped by KSM when
 * deduplicating memory.
 */
#define is_ksm_zero_pte(pte)	(is_zero_pfn(pte_pfn(pte)) && pte_dirty(pte))

extern unsigned long ksm_zero_pages;

static inline void ksm_might_unmap_zero_page(struct mm_struct *mm, pte_t pte)
{
	if (is_ksm_zero_pte(pte)) {
		ksm_zero_pages--;
		mm->ksm_zero_pages--;
	}
}

static inline int ksm_fork(struct mm_struct *mm, struct mm_struct *oldmm)
{
	int ret;

	if (test_bit(MMF_VM_MERGEABLE, &oldmm->flags)) {
		ret = __ksm_enter(mm);
		if (ret)
			return ret;
	}

	if (test_bit(MMF_VM_MERGE_ANY, &oldmm->flags))
		set_bit(MMF_VM_MERGE_ANY, &mm->flags);

	return 0;
}

static inline void ksm_exit(struct mm_struct *mm)
{
	if (test_bit(MMF_VM_MERGEABLE, &mm->flags))
		__ksm_exit(mm);
}

/*
 * When do_swap_page() first faults in from swap what used to be a KSM page,
 * no problem, it will be assigned to this vma's anon_vma; but thereafter,
 * it might be faulted into a different anon_vma (or perhaps to a different
 * offset in the same anon_vma).  do_swap_page() cannot do all the locking
 * needed to reconstitute a cross-anon_vma KSM page: for now it has to make
 * a copy, and leave remerging the pages to a later pass of ksmd.
 *
 * We'd like to make this conditional on vma->vm_flags & VM_MERGEABLE,
 * but what if the vma was unmerged while the page was swapped out?
 */
struct folio *ksm_might_need_to_copy(struct folio *folio,
			struct vm_area_struct *vma, unsigned long addr);

void rmap_walk_ksm(struct folio *folio, struct rmap_walk_control *rwc);
void folio_migrate_ksm(struct folio *newfolio, struct folio *folio);

#ifdef CONFIG_MEMORY_FAILURE
void collect_procs_ksm(struct page *page, struct list_head *to_kill,
		       int force_early);
#endif

#ifdef CONFIG_PROC_FS
long ksm_process_profit(struct mm_struct *);
#endif /* CONFIG_PROC_FS */

#else  /* !CONFIG_KSM */

static inline void ksm_add_vma(struct vm_area_struct *vma)
{
}

static inline int ksm_disable(struct mm_struct *mm)
{
	return 0;
}

static inline int ksm_fork(struct mm_struct *mm, struct mm_struct *oldmm)
{
	return 0;
}

static inline void ksm_exit(struct mm_struct *mm)
{
}

static inline void ksm_might_unmap_zero_page(struct mm_struct *mm, pte_t pte)
{
}

#ifdef CONFIG_MEMORY_FAILURE
static inline void collect_procs_ksm(struct page *page,
				     struct list_head *to_kill, int force_early)
{
}
#endif

#ifdef CONFIG_MMU
static inline int ksm_madvise(struct vm_area_struct *vma, unsigned long start,
		unsigned long end, int advice, unsigned long *vm_flags)
{
	return 0;
}

static inline struct folio *ksm_might_need_to_copy(struct folio *folio,
			struct vm_area_struct *vma, unsigned long addr)
{
	return folio;
}

static inline void rmap_walk_ksm(struct folio *folio,
			struct rmap_walk_control *rwc)
{
}

static inline void folio_migrate_ksm(struct folio *newfolio, struct folio *old)
{
}
#endif /* CONFIG_MMU */
#endif /* !CONFIG_KSM */

#endif /* __LINUX_KSM_H */
