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

struct stable_node;
struct mem_cgroup;

#ifdef CONFIG_KSM
int ksm_madvise(struct vm_area_struct *vma, unsigned long start,
		unsigned long end, int advice, unsigned long *vm_flags);
int __ksm_enter(struct mm_struct *mm);
void __ksm_exit(struct mm_struct *mm);

static inline int ksm_fork(struct mm_struct *mm, struct mm_struct *oldmm)
{
	if (test_bit(MMF_VM_MERGEABLE, &oldmm->flags))
		return __ksm_enter(mm);
	return 0;
}

static inline void ksm_exit(struct mm_struct *mm)
{
	if (test_bit(MMF_VM_MERGEABLE, &mm->flags))
		__ksm_exit(mm);
}

static inline struct stable_node *page_stable_node(struct page *page)
{
	return PageKsm(page) ? page_rmapping(page) : NULL;
}

static inline void set_page_stable_node(struct page *page,
					struct stable_node *stable_node)
{
	page->mapping = (void *)stable_node +
				(PAGE_MAPPING_ANON | PAGE_MAPPING_KSM);
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
struct page *ksm_might_need_to_copy(struct page *page,
			struct vm_area_struct *vma, unsigned long address);

int rmap_walk_ksm(struct page *page, struct rmap_walk_control *rwc);
void ksm_migrate_page(struct page *newpage, struct page *oldpage);

#else  /* !CONFIG_KSM */

static inline int ksm_fork(struct mm_struct *mm, struct mm_struct *oldmm)
{
	return 0;
}

static inline void ksm_exit(struct mm_struct *mm)
{
}

#ifdef CONFIG_MMU
static inline int ksm_madvise(struct vm_area_struct *vma, unsigned long start,
		unsigned long end, int advice, unsigned long *vm_flags)
{
	return 0;
}

static inline struct page *ksm_might_need_to_copy(struct page *page,
			struct vm_area_struct *vma, unsigned long address)
{
	return page;
}

static inline int page_referenced_ksm(struct page *page,
			struct mem_cgroup *memcg, unsigned long *vm_flags)
{
	return 0;
}

static inline int rmap_walk_ksm(struct page *page,
			struct rmap_walk_control *rwc)
{
	return 0;
}

static inline void ksm_migrate_page(struct page *newpage, struct page *oldpage)
{
}
#endif /* CONFIG_MMU */
#endif /* !CONFIG_KSM */

#endif /* __LINUX_KSM_H */
