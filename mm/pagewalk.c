#include <linux/mm.h>
#include <linux/highmem.h>
#include <linux/sched.h>

static int walk_pte_range(pmd_t *pmd, unsigned long addr, unsigned long end,
			  const struct mm_walk *walk, void *private)
{
	pte_t *pte;
	int err = 0;

	pte = pte_offset_map(pmd, addr);
	do {
		err = walk->pte_entry(pte, addr, addr + PAGE_SIZE, private);
		if (err)
		       break;
	} while (pte++, addr += PAGE_SIZE, addr != end);

	pte_unmap(pte);
	return err;
}

static int walk_pmd_range(pud_t *pud, unsigned long addr, unsigned long end,
			  const struct mm_walk *walk, void *private)
{
	pmd_t *pmd;
	unsigned long next;
	int err = 0;

	pmd = pmd_offset(pud, addr);
	do {
		next = pmd_addr_end(addr, end);
		if (pmd_none_or_clear_bad(pmd)) {
			if (walk->pte_hole)
				err = walk->pte_hole(addr, next, private);
			if (err)
				break;
			continue;
		}
		if (walk->pmd_entry)
			err = walk->pmd_entry(pmd, addr, next, private);
		if (!err && walk->pte_entry)
			err = walk_pte_range(pmd, addr, next, walk, private);
		if (err)
			break;
	} while (pmd++, addr = next, addr != end);

	return err;
}

static int walk_pud_range(pgd_t *pgd, unsigned long addr, unsigned long end,
			  const struct mm_walk *walk, void *private)
{
	pud_t *pud;
	unsigned long next;
	int err = 0;

	pud = pud_offset(pgd, addr);
	do {
		next = pud_addr_end(addr, end);
		if (pud_none_or_clear_bad(pud)) {
			if (walk->pte_hole)
				err = walk->pte_hole(addr, next, private);
			if (err)
				break;
			continue;
		}
		if (walk->pud_entry)
			err = walk->pud_entry(pud, addr, next, private);
		if (!err && (walk->pmd_entry || walk->pte_entry))
			err = walk_pmd_range(pud, addr, next, walk, private);
		if (err)
			break;
	} while (pud++, addr = next, addr != end);

	return err;
}

/**
 * walk_page_range - walk a memory map's page tables with a callback
 * @mm: memory map to walk
 * @addr: starting address
 * @end: ending address
 * @walk: set of callbacks to invoke for each level of the tree
 * @private: private data passed to the callback function
 *
 * Recursively walk the page table for the memory area in a VMA,
 * calling supplied callbacks. Callbacks are called in-order (first
 * PGD, first PUD, first PMD, first PTE, second PTE... second PMD,
 * etc.). If lower-level callbacks are omitted, walking depth is reduced.
 *
 * Each callback receives an entry pointer, the start and end of the
 * associated range, and a caller-supplied private data pointer.
 *
 * No locks are taken, but the bottom level iterator will map PTE
 * directories from highmem if necessary.
 *
 * If any callback returns a non-zero value, the walk is aborted and
 * the return value is propagated back to the caller. Otherwise 0 is returned.
 */
int walk_page_range(const struct mm_struct *mm,
		    unsigned long addr, unsigned long end,
		    const struct mm_walk *walk, void *private)
{
	pgd_t *pgd;
	unsigned long next;
	int err = 0;

	if (addr >= end)
		return err;

	pgd = pgd_offset(mm, addr);
	do {
		next = pgd_addr_end(addr, end);
		if (pgd_none_or_clear_bad(pgd)) {
			if (walk->pte_hole)
				err = walk->pte_hole(addr, next, private);
			if (err)
				break;
			continue;
		}
		if (walk->pgd_entry)
			err = walk->pgd_entry(pgd, addr, next, private);
		if (!err &&
		    (walk->pud_entry || walk->pmd_entry || walk->pte_entry))
			err = walk_pud_range(pgd, addr, next, walk, private);
		if (err)
			break;
	} while (pgd++, addr = next, addr != end);

	return err;
}
