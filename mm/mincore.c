/*
 *	linux/mm/mincore.c
 *
 * Copyright (C) 1994-2006  Linus Torvalds
 */

/*
 * The mincore() system call.
 */
#include <linux/slab.h>
#include <linux/pagemap.h>
#include <linux/mm.h>
#include <linux/mman.h>
#include <linux/syscalls.h>
#include <linux/swap.h>
#include <linux/swapops.h>

#include <asm/uaccess.h>
#include <asm/pgtable.h>

/*
 * Later we can get more picky about what "in core" means precisely.
 * For now, simply check to see if the page is in the page cache,
 * and is up to date; i.e. that no page-in operation would be required
 * at this time if an application were to map and access this page.
 */
static unsigned char mincore_page(struct address_space *mapping, pgoff_t pgoff)
{
	unsigned char present = 0;
	struct page *page;

	/*
	 * When tmpfs swaps out a page from a file, any process mapping that
	 * file will not get a swp_entry_t in its pte, but rather it is like
	 * any other file mapping (ie. marked !present and faulted in with
	 * tmpfs's .nopage). So swapped out tmpfs mappings are tested here.
	 *
	 * However when tmpfs moves the page from pagecache and into swapcache,
	 * it is still in core, but the find_get_page below won't find it.
	 * No big deal, but make a note of it.
	 */
	page = find_get_page(mapping, pgoff);
	if (page) {
		present = PageUptodate(page);
		page_cache_release(page);
	}

	return present;
}

/*
 * Do a chunk of "sys_mincore()". We've already checked
 * all the arguments, we hold the mmap semaphore: we should
 * just return the amount of info we're asked for.
 */
static long do_mincore(unsigned long addr, unsigned char *vec, unsigned long pages)
{
	pgd_t *pgd;
	pud_t *pud;
	pmd_t *pmd;
	pte_t *ptep;
	spinlock_t *ptl;
	unsigned long nr;
	int i;
	pgoff_t pgoff;
	struct vm_area_struct *vma = find_vma(current->mm, addr);

	/*
	 * find_vma() didn't find anything above us, or we're
	 * in an unmapped hole in the address space: ENOMEM.
	 */
	if (!vma || addr < vma->vm_start)
		return -ENOMEM;

	/*
	 * Calculate how many pages there are left in the last level of the
	 * PTE array for our address.
	 */
	nr = PTRS_PER_PTE - ((addr >> PAGE_SHIFT) & (PTRS_PER_PTE-1));

	/*
	 * Don't overrun this vma
	 */
	nr = min(nr, (vma->vm_end - addr) >> PAGE_SHIFT);

	/*
	 * Don't return more than the caller asked for
	 */
	nr = min(nr, pages);

	pgd = pgd_offset(vma->vm_mm, addr);
	if (pgd_none_or_clear_bad(pgd))
		goto none_mapped;
	pud = pud_offset(pgd, addr);
	if (pud_none_or_clear_bad(pud))
		goto none_mapped;
	pmd = pmd_offset(pud, addr);
	if (pmd_none_or_clear_bad(pmd))
		goto none_mapped;

	ptep = pte_offset_map_lock(vma->vm_mm, pmd, addr, &ptl);
	for (i = 0; i < nr; i++, ptep++, addr += PAGE_SIZE) {
		unsigned char present;
		pte_t pte = *ptep;

		if (pte_present(pte)) {
			present = 1;

		} else if (pte_none(pte)) {
			if (vma->vm_file) {
				pgoff = linear_page_index(vma, addr);
				present = mincore_page(vma->vm_file->f_mapping,
							pgoff);
			} else
				present = 0;

		} else if (pte_file(pte)) {
			pgoff = pte_to_pgoff(pte);
			present = mincore_page(vma->vm_file->f_mapping, pgoff);

		} else { /* pte is a swap entry */
			swp_entry_t entry = pte_to_swp_entry(pte);
			if (is_migration_entry(entry)) {
				/* migration entries are always uptodate */
				present = 1;
			} else {
#ifdef CONFIG_SWAP
				pgoff = entry.val;
				present = mincore_page(&swapper_space, pgoff);
#else
				WARN_ON(1);
				present = 1;
#endif
			}
		}

		vec[i] = present;
	}
	pte_unmap_unlock(ptep-1, ptl);

	return nr;

none_mapped:
	if (vma->vm_file) {
		pgoff = linear_page_index(vma, addr);
		for (i = 0; i < nr; i++, pgoff++)
			vec[i] = mincore_page(vma->vm_file->f_mapping, pgoff);
	} else {
		for (i = 0; i < nr; i++)
			vec[i] = 0;
	}

	return nr;
}

/*
 * The mincore(2) system call.
 *
 * mincore() returns the memory residency status of the pages in the
 * current process's address space specified by [addr, addr + len).
 * The status is returned in a vector of bytes.  The least significant
 * bit of each byte is 1 if the referenced page is in memory, otherwise
 * it is zero.
 *
 * Because the status of a page can change after mincore() checks it
 * but before it returns to the application, the returned vector may
 * contain stale information.  Only locked pages are guaranteed to
 * remain in memory.
 *
 * return values:
 *  zero    - success
 *  -EFAULT - vec points to an illegal address
 *  -EINVAL - addr is not a multiple of PAGE_CACHE_SIZE
 *  -ENOMEM - Addresses in the range [addr, addr + len] are
 *		invalid for the address space of this process, or
 *		specify one or more pages which are not currently
 *		mapped
 *  -EAGAIN - A kernel resource was temporarily unavailable.
 */
asmlinkage long sys_mincore(unsigned long start, size_t len,
	unsigned char __user * vec)
{
	long retval;
	unsigned long pages;
	unsigned char *tmp;

	/* Check the start address: needs to be page-aligned.. */
 	if (start & ~PAGE_CACHE_MASK)
		return -EINVAL;

	/* ..and we need to be passed a valid user-space range */
	if (!access_ok(VERIFY_READ, (void __user *) start, len))
		return -ENOMEM;

	/* This also avoids any overflows on PAGE_CACHE_ALIGN */
	pages = len >> PAGE_SHIFT;
	pages += (len & ~PAGE_MASK) != 0;

	if (!access_ok(VERIFY_WRITE, vec, pages))
		return -EFAULT;

	tmp = (void *) __get_free_page(GFP_USER);
	if (!tmp)
		return -EAGAIN;

	retval = 0;
	while (pages) {
		/*
		 * Do at most PAGE_SIZE entries per iteration, due to
		 * the temporary buffer size.
		 */
		down_read(&current->mm->mmap_sem);
		retval = do_mincore(start, tmp, min(pages, PAGE_SIZE));
		up_read(&current->mm->mmap_sem);

		if (retval <= 0)
			break;
		if (copy_to_user(vec, tmp, retval)) {
			retval = -EFAULT;
			break;
		}
		pages -= retval;
		vec += retval;
		start += retval << PAGE_SHIFT;
		retval = 0;
	}
	free_page((unsigned long) tmp);
	return retval;
}
