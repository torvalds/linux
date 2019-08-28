/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_PAGEWALK_H
#define _LINUX_PAGEWALK_H

#include <linux/mm.h>

/**
 * mm_walk - callbacks for walk_page_range
 * @pud_entry: if set, called for each non-empty PUD (2nd-level) entry
 *	       this handler should only handle pud_trans_huge() puds.
 *	       the pmd_entry or pte_entry callbacks will be used for
 *	       regular PUDs.
 * @pmd_entry: if set, called for each non-empty PMD (3rd-level) entry
 *	       this handler is required to be able to handle
 *	       pmd_trans_huge() pmds.  They may simply choose to
 *	       split_huge_page() instead of handling it explicitly.
 * @pte_entry: if set, called for each non-empty PTE (4th-level) entry
 * @pte_hole: if set, called for each hole at all levels
 * @hugetlb_entry: if set, called for each hugetlb entry
 * @test_walk: caller specific callback function to determine whether
 *             we walk over the current vma or not. Returning 0
 *             value means "do page table walk over the current vma,"
 *             and a negative one means "abort current page table walk
 *             right now." 1 means "skip the current vma."
 * @mm:        mm_struct representing the target process of page table walk
 * @vma:       vma currently walked (NULL if walking outside vmas)
 * @private:   private data for callbacks' usage
 *
 * (see the comment on walk_page_range() for more details)
 */
struct mm_walk {
	int (*pud_entry)(pud_t *pud, unsigned long addr,
			 unsigned long next, struct mm_walk *walk);
	int (*pmd_entry)(pmd_t *pmd, unsigned long addr,
			 unsigned long next, struct mm_walk *walk);
	int (*pte_entry)(pte_t *pte, unsigned long addr,
			 unsigned long next, struct mm_walk *walk);
	int (*pte_hole)(unsigned long addr, unsigned long next,
			struct mm_walk *walk);
	int (*hugetlb_entry)(pte_t *pte, unsigned long hmask,
			     unsigned long addr, unsigned long next,
			     struct mm_walk *walk);
	int (*test_walk)(unsigned long addr, unsigned long next,
			struct mm_walk *walk);
	struct mm_struct *mm;
	struct vm_area_struct *vma;
	void *private;
};

int walk_page_range(unsigned long addr, unsigned long end,
		struct mm_walk *walk);
int walk_page_vma(struct vm_area_struct *vma, struct mm_walk *walk);

#endif /* _LINUX_PAGEWALK_H */
