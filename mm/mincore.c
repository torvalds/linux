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

#include <asm/uaccess.h>
#include <asm/pgtable.h>

/*
 * Later we can get more picky about what "in core" means precisely.
 * For now, simply check to see if the page is in the page cache,
 * and is up to date; i.e. that no page-in operation would be required
 * at this time if an application were to map and access this page.
 */
static unsigned char mincore_page(struct vm_area_struct * vma,
	unsigned long pgoff)
{
	unsigned char present = 0;
	struct address_space * as = vma->vm_file->f_mapping;
	struct page * page;

	page = find_get_page(as, pgoff);
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
	unsigned long i, nr, pgoff;
	struct vm_area_struct *vma = find_vma(current->mm, addr);

	/*
	 * find_vma() didn't find anything: the address
	 * is above everything we have mapped.
	 */
	if (!vma) {
		memset(vec, 0, pages);
		return pages;
	}

	/*
	 * find_vma() found something, but we might be
	 * below it: check for that.
	 */
	if (addr < vma->vm_start) {
		unsigned long gap = (vma->vm_start - addr) >> PAGE_SHIFT;
		if (gap > pages)
			gap = pages;
		memset(vec, 0, gap);
		return gap;
	}

	/*
	 * Ok, got it. But check whether it's a segment we support
	 * mincore() on. Right now, we don't do any anonymous mappings.
	 */
	if (!vma->vm_file)
		return -ENOMEM;

	/*
	 * Calculate how many pages there are left in the vma, and
	 * what the pgoff is for our address.
	 */
	nr = (vma->vm_end - addr) >> PAGE_SHIFT;
	if (nr > pages)
		nr = pages;

	pgoff = (addr - vma->vm_start) >> PAGE_SHIFT;
	pgoff += vma->vm_pgoff;

	/* And then we just fill the sucker in.. */
	for (i = 0 ; i < nr; i++, pgoff++)
		vec[i] = mincore_page(vma, pgoff);

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
		return -ENOMEM;

	retval = 0;
	while (pages) {
		/*
		 * Do at most PAGE_SIZE entries per iteration, due to
		 * the temporary buffer size.
		 */
		down_read(&current->mm->mmap_sem);
		retval = do_mincore(start, tmp, max(pages, PAGE_SIZE));
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
