/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_PAGEWALK_H
#define _LINUX_PAGEWALK_H

#include <linux/mm.h>

struct mm_walk;

/**
 * mm_walk_ops - callbacks for walk_page_range
 * @pud_entry:		if set, called for each non-empty PUD (2nd-level) entry
 *			this handler should only handle pud_trans_huge() puds.
 *			the pmd_entry or pte_entry callbacks will be used for
 *			regular PUDs.
 * @pmd_entry:		if set, called for each non-empty PMD (3rd-level) entry
 *			this handler is required to be able to handle
 *			pmd_trans_huge() pmds.  They may simply choose to
 *			split_huge_page() instead of handling it explicitly.
 * @pte_entry:		if set, called for each non-empty PTE (4th-level) entry
 * @pte_hole:		if set, called for each hole at all levels
 * @hugetlb_entry:	if set, called for each hugetlb entry
 * @test_walk:		caller specific callback function to determine whether
 *			we walk over the current vma or not. Returning 0 means
 *			"do page table walk over the current vma", returning
 *			a negative value means "abort current page table walk
 *			right now" and returning 1 means "skip the current vma"
 * @pre_vma:            if set, called before starting walk on a non-null vma.
 * @post_vma:           if set, called after a walk on a non-null vma, provided
 *                      that @pre_vma and the vma walk succeeded.
 */
struct mm_walk_ops {
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
	int (*pre_vma)(unsigned long start, unsigned long end,
		       struct mm_walk *walk);
	void (*post_vma)(struct mm_walk *walk);
};

/**
 * mm_walk - walk_page_range data
 * @ops:	operation to call during the walk
 * @mm:		mm_struct representing the target process of page table walk
 * @vma:	vma currently walked (NULL if walking outside vmas)
 * @private:	private data for callbacks' usage
 *
 * (see the comment on walk_page_range() for more details)
 */
struct mm_walk {
	const struct mm_walk_ops *ops;
	struct mm_struct *mm;
	struct vm_area_struct *vma;
	void *private;
};

int walk_page_range(struct mm_struct *mm, unsigned long start,
		unsigned long end, const struct mm_walk_ops *ops,
		void *private);
int walk_page_vma(struct vm_area_struct *vma, const struct mm_walk_ops *ops,
		void *private);
int walk_page_mapping(struct address_space *mapping, pgoff_t first_index,
		      pgoff_t nr, const struct mm_walk_ops *ops,
		      void *private);

#endif /* _LINUX_PAGEWALK_H */
