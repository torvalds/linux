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
#include <linux/sched.h>
#include <linux/vmstat.h>

struct mmu_gather;

#ifdef CONFIG_KSM
int ksm_madvise(struct vm_area_struct *vma, unsigned long start,
		unsigned long end, int advice, unsigned long *vm_flags);
int __ksm_enter(struct mm_struct *mm);
void __ksm_exit(struct mm_struct *mm,
		struct mmu_gather **tlbp, unsigned long end);

static inline int ksm_fork(struct mm_struct *mm, struct mm_struct *oldmm)
{
	if (test_bit(MMF_VM_MERGEABLE, &oldmm->flags))
		return __ksm_enter(mm);
	return 0;
}

/*
 * For KSM to handle OOM without deadlock when it's breaking COW in a
 * likely victim of the OOM killer, exit_mmap() has to serialize with
 * ksm_exit() after freeing mm's pages but before freeing its page tables.
 * That leaves a window in which KSM might refault pages which have just
 * been finally unmapped: guard against that with ksm_test_exit(), and
 * use it after getting mmap_sem in ksm.c, to check if mm is exiting.
 */
static inline bool ksm_test_exit(struct mm_struct *mm)
{
	return atomic_read(&mm->mm_users) == 0;
}

static inline void ksm_exit(struct mm_struct *mm,
			    struct mmu_gather **tlbp, unsigned long end)
{
	if (test_bit(MMF_VM_MERGEABLE, &mm->flags))
		__ksm_exit(mm, tlbp, end);
}

/*
 * A KSM page is one of those write-protected "shared pages" or "merged pages"
 * which KSM maps into multiple mms, wherever identical anonymous page content
 * is found in VM_MERGEABLE vmas.  It's a PageAnon page, with NULL anon_vma.
 */
static inline int PageKsm(struct page *page)
{
	return ((unsigned long)page->mapping == PAGE_MAPPING_ANON);
}

/*
 * But we have to avoid the checking which page_add_anon_rmap() performs.
 */
static inline void page_add_ksm_rmap(struct page *page)
{
	if (atomic_inc_and_test(&page->_mapcount)) {
		page->mapping = (void *) PAGE_MAPPING_ANON;
		__inc_zone_page_state(page, NR_ANON_PAGES);
	}
}
#else  /* !CONFIG_KSM */

static inline int ksm_madvise(struct vm_area_struct *vma, unsigned long start,
		unsigned long end, int advice, unsigned long *vm_flags)
{
	return 0;
}

static inline int ksm_fork(struct mm_struct *mm, struct mm_struct *oldmm)
{
	return 0;
}

static inline bool ksm_test_exit(struct mm_struct *mm)
{
	return 0;
}

static inline void ksm_exit(struct mm_struct *mm,
			    struct mmu_gather **tlbp, unsigned long end)
{
}

static inline int PageKsm(struct page *page)
{
	return 0;
}

/* No stub required for page_add_ksm_rmap(page) */
#endif /* !CONFIG_KSM */

#endif
