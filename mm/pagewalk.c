// SPDX-License-Identifier: GPL-2.0
#include <linux/mm.h>
#include <linux/highmem.h>
#include <linux/sched.h>
#include <linux/hugetlb.h>

static int walk_pte_range(pmd_t *pmd, unsigned long addr, unsigned long end,
			  struct mm_walk *walk)
{
	pte_t *pte;
	int err = 0;

	pte = pte_offset_map(pmd, addr);
	for (;;) {
		err = walk->pte_entry(pte, addr, addr + PAGE_SIZE, walk);
		if (err)
		       break;
		addr += PAGE_SIZE;
		if (addr == end)
			break;
		pte++;
	}

	pte_unmap(pte);
	return err;
}

static int walk_pmd_range(pud_t *pud, unsigned long addr, unsigned long end,
			  struct mm_walk *walk)
{
	pmd_t *pmd;
	unsigned long next;
	int err = 0;

	pmd = pmd_offset(pud, addr);
	do {
again:
		next = pmd_addr_end(addr, end);
		if (pmd_none(*pmd) || !walk->vma) {
			if (walk->pte_hole)
				err = walk->pte_hole(addr, next, walk);
			if (err)
				break;
			continue;
		}
		/*
		 * This implies that each ->pmd_entry() handler
		 * needs to know about pmd_trans_huge() pmds
		 */
		if (walk->pmd_entry)
			err = walk->pmd_entry(pmd, addr, next, walk);
		if (err)
			break;

		/*
		 * Check this here so we only break down trans_huge
		 * pages when we _need_ to
		 */
		if (!walk->pte_entry)
			continue;

		split_huge_pmd(walk->vma, pmd, addr);
		if (pmd_trans_unstable(pmd))
			goto again;
		err = walk_pte_range(pmd, addr, next, walk);
		if (err)
			break;
	} while (pmd++, addr = next, addr != end);

	return err;
}

static int walk_pud_range(p4d_t *p4d, unsigned long addr, unsigned long end,
			  struct mm_walk *walk)
{
	pud_t *pud;
	unsigned long next;
	int err = 0;

	pud = pud_offset(p4d, addr);
	do {
 again:
		next = pud_addr_end(addr, end);
		if (pud_none(*pud) || !walk->vma) {
			if (walk->pte_hole)
				err = walk->pte_hole(addr, next, walk);
			if (err)
				break;
			continue;
		}

		if (walk->pud_entry) {
			spinlock_t *ptl = pud_trans_huge_lock(pud, walk->vma);

			if (ptl) {
				err = walk->pud_entry(pud, addr, next, walk);
				spin_unlock(ptl);
				if (err)
					break;
				continue;
			}
		}

		split_huge_pud(walk->vma, pud, addr);
		if (pud_none(*pud))
			goto again;

		if (walk->pmd_entry || walk->pte_entry)
			err = walk_pmd_range(pud, addr, next, walk);
		if (err)
			break;
	} while (pud++, addr = next, addr != end);

	return err;
}

static int walk_p4d_range(pgd_t *pgd, unsigned long addr, unsigned long end,
			  struct mm_walk *walk)
{
	p4d_t *p4d;
	unsigned long next;
	int err = 0;

	p4d = p4d_offset(pgd, addr);
	do {
		next = p4d_addr_end(addr, end);
		if (p4d_none_or_clear_bad(p4d)) {
			if (walk->pte_hole)
				err = walk->pte_hole(addr, next, walk);
			if (err)
				break;
			continue;
		}
		if (walk->pmd_entry || walk->pte_entry)
			err = walk_pud_range(p4d, addr, next, walk);
		if (err)
			break;
	} while (p4d++, addr = next, addr != end);

	return err;
}

static int walk_pgd_range(unsigned long addr, unsigned long end,
			  struct mm_walk *walk)
{
	pgd_t *pgd;
	unsigned long next;
	int err = 0;

	pgd = pgd_offset(walk->mm, addr);
	do {
		next = pgd_addr_end(addr, end);
		if (pgd_none_or_clear_bad(pgd)) {
			if (walk->pte_hole)
				err = walk->pte_hole(addr, next, walk);
			if (err)
				break;
			continue;
		}
		if (walk->pmd_entry || walk->pte_entry)
			err = walk_p4d_range(pgd, addr, next, walk);
		if (err)
			break;
	} while (pgd++, addr = next, addr != end);

	return err;
}

#ifdef CONFIG_HUGETLB_PAGE
static unsigned long hugetlb_entry_end(struct hstate *h, unsigned long addr,
				       unsigned long end)
{
	unsigned long boundary = (addr & huge_page_mask(h)) + huge_page_size(h);
	return boundary < end ? boundary : end;
}

static int walk_hugetlb_range(unsigned long addr, unsigned long end,
			      struct mm_walk *walk)
{
	struct vm_area_struct *vma = walk->vma;
	struct hstate *h = hstate_vma(vma);
	unsigned long next;
	unsigned long hmask = huge_page_mask(h);
	unsigned long sz = huge_page_size(h);
	pte_t *pte;
	int err = 0;

	do {
		next = hugetlb_entry_end(h, addr, end);
		pte = huge_pte_offset(walk->mm, addr & hmask, sz);

		if (pte)
			err = walk->hugetlb_entry(pte, hmask, addr, next, walk);
		else if (walk->pte_hole)
			err = walk->pte_hole(addr, next, walk);

		if (err)
			break;
	} while (addr = next, addr != end);

	return err;
}

#else /* CONFIG_HUGETLB_PAGE */
static int walk_hugetlb_range(unsigned long addr, unsigned long end,
			      struct mm_walk *walk)
{
	return 0;
}

#endif /* CONFIG_HUGETLB_PAGE */

/*
 * Decide whether we really walk over the current vma on [@start, @end)
 * or skip it via the returned value. Return 0 if we do walk over the
 * current vma, and return 1 if we skip the vma. Negative values means
 * error, where we abort the current walk.
 */
static int walk_page_test(unsigned long start, unsigned long end,
			struct mm_walk *walk)
{
	struct vm_area_struct *vma = walk->vma;

	if (walk->test_walk)
		return walk->test_walk(start, end, walk);

	/*
	 * vma(VM_PFNMAP) doesn't have any valid struct pages behind VM_PFNMAP
	 * range, so we don't walk over it as we do for normal vmas. However,
	 * Some callers are interested in handling hole range and they don't
	 * want to just ignore any single address range. Such users certainly
	 * define their ->pte_hole() callbacks, so let's delegate them to handle
	 * vma(VM_PFNMAP).
	 */
	if (vma->vm_flags & VM_PFNMAP) {
		int err = 1;
		if (walk->pte_hole)
			err = walk->pte_hole(start, end, walk);
		return err ? err : 1;
	}
	return 0;
}

static int __walk_page_range(unsigned long start, unsigned long end,
			struct mm_walk *walk)
{
	int err = 0;
	struct vm_area_struct *vma = walk->vma;

	if (vma && is_vm_hugetlb_page(vma)) {
		if (walk->hugetlb_entry)
			err = walk_hugetlb_range(start, end, walk);
	} else
		err = walk_pgd_range(start, end, walk);

	return err;
}

/**
 * walk_page_range - walk page table with caller specific callbacks
 *
 * Recursively walk the page table tree of the process represented by @walk->mm
 * within the virtual address range [@start, @end). During walking, we can do
 * some caller-specific works for each entry, by setting up pmd_entry(),
 * pte_entry(), and/or hugetlb_entry(). If you don't set up for some of these
 * callbacks, the associated entries/pages are just ignored.
 * The return values of these callbacks are commonly defined like below:
 *
 *  - 0  : succeeded to handle the current entry, and if you don't reach the
 *         end address yet, continue to walk.
 *  - >0 : succeeded to handle the current entry, and return to the caller
 *         with caller specific value.
 *  - <0 : failed to handle the current entry, and return to the caller
 *         with error code.
 *
 * Before starting to walk page table, some callers want to check whether
 * they really want to walk over the current vma, typically by checking
 * its vm_flags. walk_page_test() and @walk->test_walk() are used for this
 * purpose.
 *
 * struct mm_walk keeps current values of some common data like vma and pmd,
 * which are useful for the access from callbacks. If you want to pass some
 * caller-specific data to callbacks, @walk->private should be helpful.
 *
 * Locking:
 *   Callers of walk_page_range() and walk_page_vma() should hold
 *   @walk->mm->mmap_sem, because these function traverse vma list and/or
 *   access to vma's data.
 */
int walk_page_range(unsigned long start, unsigned long end,
		    struct mm_walk *walk)
{
	int err = 0;
	unsigned long next;
	struct vm_area_struct *vma;

	if (start >= end)
		return -EINVAL;

	if (!walk->mm)
		return -EINVAL;

	VM_BUG_ON_MM(!rwsem_is_locked(&walk->mm->mmap_sem), walk->mm);

	vma = find_vma(walk->mm, start);
	do {
		if (!vma) { /* after the last vma */
			walk->vma = NULL;
			next = end;
		} else if (start < vma->vm_start) { /* outside vma */
			walk->vma = NULL;
			next = min(end, vma->vm_start);
		} else { /* inside vma */
			walk->vma = vma;
			next = min(end, vma->vm_end);
			vma = vma->vm_next;

			err = walk_page_test(start, next, walk);
			if (err > 0) {
				/*
				 * positive return values are purely for
				 * controlling the pagewalk, so should never
				 * be passed to the callers.
				 */
				err = 0;
				continue;
			}
			if (err < 0)
				break;
		}
		if (walk->vma || walk->pte_hole)
			err = __walk_page_range(start, next, walk);
		if (err)
			break;
	} while (start = next, start < end);
	return err;
}

int walk_page_vma(struct vm_area_struct *vma, struct mm_walk *walk)
{
	int err;

	if (!walk->mm)
		return -EINVAL;

	VM_BUG_ON(!rwsem_is_locked(&walk->mm->mmap_sem));
	VM_BUG_ON(!vma);
	walk->vma = vma;
	err = walk_page_test(vma->vm_start, vma->vm_end, walk);
	if (err > 0)
		return 0;
	if (err < 0)
		return err;
	return __walk_page_range(vma->vm_start, vma->vm_end, walk);
}
