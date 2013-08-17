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
		if (pmd_none(*pmd)) {
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

		split_huge_page_pmd(walk->mm, pmd);
		if (pmd_none_or_trans_huge_or_clear_bad(pmd))
			goto again;
		err = walk_pte_range(pmd, addr, next, walk);
		if (err)
			break;
	} while (pmd++, addr = next, addr != end);

	return err;
}

static int walk_pud_range(pgd_t *pgd, unsigned long addr, unsigned long end,
			  struct mm_walk *walk)
{
	pud_t *pud;
	unsigned long next;
	int err = 0;

	pud = pud_offset(pgd, addr);
	do {
		next = pud_addr_end(addr, end);
		if (pud_none_or_clear_bad(pud)) {
			if (walk->pte_hole)
				err = walk->pte_hole(addr, next, walk);
			if (err)
				break;
			continue;
		}
		if (walk->pud_entry)
			err = walk->pud_entry(pud, addr, next, walk);
		if (!err && (walk->pmd_entry || walk->pte_entry))
			err = walk_pmd_range(pud, addr, next, walk);
		if (err)
			break;
	} while (pud++, addr = next, addr != end);

	return err;
}

#ifdef CONFIG_HUGETLB_PAGE
static unsigned long hugetlb_entry_end(struct hstate *h, unsigned long addr,
				       unsigned long end)
{
	unsigned long boundary = (addr & huge_page_mask(h)) + huge_page_size(h);
	return boundary < end ? boundary : end;
}

static int walk_hugetlb_range(struct vm_area_struct *vma,
			      unsigned long addr, unsigned long end,
			      struct mm_walk *walk)
{
	struct hstate *h = hstate_vma(vma);
	unsigned long next;
	unsigned long hmask = huge_page_mask(h);
	pte_t *pte;
	int err = 0;

	do {
		next = hugetlb_entry_end(h, addr, end);
		pte = huge_pte_offset(walk->mm, addr & hmask);
		if (pte && walk->hugetlb_entry)
			err = walk->hugetlb_entry(pte, hmask, addr, next, walk);
		if (err)
			return err;
	} while (addr = next, addr != end);

	return 0;
}

static struct vm_area_struct* hugetlb_vma(unsigned long addr, struct mm_walk *walk)
{
	struct vm_area_struct *vma;

	/* We don't need vma lookup at all. */
	if (!walk->hugetlb_entry)
		return NULL;

	VM_BUG_ON(!rwsem_is_locked(&walk->mm->mmap_sem));
	vma = find_vma(walk->mm, addr);
	if (vma && vma->vm_start <= addr && is_vm_hugetlb_page(vma))
		return vma;

	return NULL;
}

#else /* CONFIG_HUGETLB_PAGE */
static struct vm_area_struct* hugetlb_vma(unsigned long addr, struct mm_walk *walk)
{
	return NULL;
}

static int walk_hugetlb_range(struct vm_area_struct *vma,
			      unsigned long addr, unsigned long end,
			      struct mm_walk *walk)
{
	return 0;
}

#endif /* CONFIG_HUGETLB_PAGE */



/**
 * walk_page_range - walk a memory map's page tables with a callback
 * @mm: memory map to walk
 * @addr: starting address
 * @end: ending address
 * @walk: set of callbacks to invoke for each level of the tree
 *
 * Recursively walk the page table for the memory area in a VMA,
 * calling supplied callbacks. Callbacks are called in-order (first
 * PGD, first PUD, first PMD, first PTE, second PTE... second PMD,
 * etc.). If lower-level callbacks are omitted, walking depth is reduced.
 *
 * Each callback receives an entry pointer and the start and end of the
 * associated range, and a copy of the original mm_walk for access to
 * the ->private or ->mm fields.
 *
 * Usually no locks are taken, but splitting transparent huge page may
 * take page table lock. And the bottom level iterator will map PTE
 * directories from highmem if necessary.
 *
 * If any callback returns a non-zero value, the walk is aborted and
 * the return value is propagated back to the caller. Otherwise 0 is returned.
 *
 * walk->mm->mmap_sem must be held for at least read if walk->hugetlb_entry
 * is !NULL.
 */
int walk_page_range(unsigned long addr, unsigned long end,
		    struct mm_walk *walk)
{
	pgd_t *pgd;
	unsigned long next;
	int err = 0;

	if (addr >= end)
		return err;

	if (!walk->mm)
		return -EINVAL;

	pgd = pgd_offset(walk->mm, addr);
	do {
		struct vm_area_struct *vma;

		next = pgd_addr_end(addr, end);

		/*
		 * handle hugetlb vma individually because pagetable walk for
		 * the hugetlb page is dependent on the architecture and
		 * we can't handled it in the same manner as non-huge pages.
		 */
		vma = hugetlb_vma(addr, walk);
		if (vma) {
			if (vma->vm_end < next)
				next = vma->vm_end;
			/*
			 * Hugepage is very tightly coupled with vma, so
			 * walk through hugetlb entries within a given vma.
			 */
			err = walk_hugetlb_range(vma, addr, next, walk);
			if (err)
				break;
			pgd = pgd_offset(walk->mm, next);
			continue;
		}

		if (pgd_none_or_clear_bad(pgd)) {
			if (walk->pte_hole)
				err = walk->pte_hole(addr, next, walk);
			if (err)
				break;
			pgd++;
			continue;
		}
		if (walk->pgd_entry)
			err = walk->pgd_entry(pgd, addr, next, walk);
		if (!err &&
		    (walk->pud_entry || walk->pmd_entry || walk->pte_entry))
			err = walk_pud_range(pgd, addr, next, walk);
		if (err)
			break;
		pgd++;
	} while (addr = next, addr != end);

	return err;
}
