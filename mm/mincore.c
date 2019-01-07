// SPDX-License-Identifier: GPL-2.0
/*
 *	linux/mm/mincore.c
 *
 * Copyright (C) 1994-2006  Linus Torvalds
 */

/*
 * The mincore() system call.
 */
#include <linux/pagemap.h>
#include <linux/gfp.h>
#include <linux/mm.h>
#include <linux/mman.h>
#include <linux/syscalls.h>
#include <linux/swap.h>
#include <linux/swapops.h>
#include <linux/shmem_fs.h>
#include <linux/hugetlb.h>

#include <linux/uaccess.h>
#include <asm/pgtable.h>

static int mincore_hugetlb(pte_t *pte, unsigned long hmask, unsigned long addr,
			unsigned long end, struct mm_walk *walk)
{
#ifdef CONFIG_HUGETLB_PAGE
	unsigned char present;
	unsigned char *vec = walk->private;

	/*
	 * Hugepages under user process are always in RAM and never
	 * swapped out, but theoretically it needs to be checked.
	 */
	present = pte && !huge_pte_none(huge_ptep_get(pte));
	for (; addr != end; vec++, addr += PAGE_SIZE)
		*vec = present;
	walk->private = vec;
#else
	BUG();
#endif
	return 0;
}

static int mincore_unmapped_range(unsigned long addr, unsigned long end,
				   struct mm_walk *walk)
{
	unsigned char *vec = walk->private;
	unsigned long nr = (end - addr) >> PAGE_SHIFT;

	memset(vec, 0, nr);
	walk->private += nr;
	return 0;
}

static int mincore_pte_range(pmd_t *pmd, unsigned long addr, unsigned long end,
			struct mm_walk *walk)
{
	spinlock_t *ptl;
	struct vm_area_struct *vma = walk->vma;
	pte_t *ptep;
	unsigned char *vec = walk->private;
	int nr = (end - addr) >> PAGE_SHIFT;

	ptl = pmd_trans_huge_lock(pmd, vma);
	if (ptl) {
		memset(vec, 1, nr);
		spin_unlock(ptl);
		goto out;
	}

	/* We'll consider a THP page under construction to be there */
	if (pmd_trans_unstable(pmd)) {
		memset(vec, 1, nr);
		goto out;
	}

	ptep = pte_offset_map_lock(walk->mm, pmd, addr, &ptl);
	for (; addr != end; ptep++, addr += PAGE_SIZE) {
		pte_t pte = *ptep;

		if (pte_none(pte))
			*vec = 0;
		else if (pte_present(pte))
			*vec = 1;
		else { /* pte is a swap entry */
			swp_entry_t entry = pte_to_swp_entry(pte);

			/*
			 * migration or hwpoison entries are always
			 * uptodate
			 */
			*vec = !!non_swap_entry(entry);
		}
		vec++;
	}
	pte_unmap_unlock(ptep - 1, ptl);
out:
	walk->private += nr;
	cond_resched();
	return 0;
}

/*
 * Do a chunk of "sys_mincore()". We've already checked
 * all the arguments, we hold the mmap semaphore: we should
 * just return the amount of info we're asked for.
 */
static long do_mincore(unsigned long addr, unsigned long pages, unsigned char *vec)
{
	struct vm_area_struct *vma;
	unsigned long end;
	int err;
	struct mm_walk mincore_walk = {
		.pmd_entry = mincore_pte_range,
		.pte_hole = mincore_unmapped_range,
		.hugetlb_entry = mincore_hugetlb,
		.private = vec,
	};

	vma = find_vma(current->mm, addr);
	if (!vma || addr < vma->vm_start)
		return -ENOMEM;
	mincore_walk.mm = vma->vm_mm;
	end = min(vma->vm_end, addr + (pages << PAGE_SHIFT));
	err = walk_page_range(addr, end, &mincore_walk);
	if (err < 0)
		return err;
	return (end - addr) >> PAGE_SHIFT;
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
 *  -EINVAL - addr is not a multiple of PAGE_SIZE
 *  -ENOMEM - Addresses in the range [addr, addr + len] are
 *		invalid for the address space of this process, or
 *		specify one or more pages which are not currently
 *		mapped
 *  -EAGAIN - A kernel resource was temporarily unavailable.
 */
SYSCALL_DEFINE3(mincore, unsigned long, start, size_t, len,
		unsigned char __user *, vec)
{
	long retval;
	unsigned long pages;
	unsigned char *tmp;

	/* Check the start address: needs to be page-aligned.. */
	if (start & ~PAGE_MASK)
		return -EINVAL;

	/* ..and we need to be passed a valid user-space range */
	if (!access_ok((void __user *) start, len))
		return -ENOMEM;

	/* This also avoids any overflows on PAGE_ALIGN */
	pages = len >> PAGE_SHIFT;
	pages += (offset_in_page(len)) != 0;

	if (!access_ok(vec, pages))
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
		retval = do_mincore(start, min(pages, PAGE_SIZE), tmp);
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
