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
#include <linux/pagewalk.h>
#include <linux/mman.h>
#include <linux/syscalls.h>
#include <linux/swap.h>
#include <linux/swapops.h>
#include <linux/shmem_fs.h>
#include <linux/hugetlb.h>
#include <linux/pgtable.h>

#include <linux/uaccess.h>
#include "swap.h"

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
	present = pte && !huge_pte_none_mostly(huge_ptep_get(pte));
	for (; addr != end; vec++, addr += PAGE_SIZE)
		*vec = present;
	walk->private = vec;
#else
	BUG();
#endif
	return 0;
}

/*
 * Later we can get more picky about what "in core" means precisely.
 * For now, simply check to see if the page is in the page cache,
 * and is up to date; i.e. that no page-in operation would be required
 * at this time if an application were to map and access this page.
 */
static unsigned char mincore_page(struct address_space *mapping, pgoff_t index)
{
	unsigned char present = 0;
	struct folio *folio;

	/*
	 * When tmpfs swaps out a page from a file, any process mapping that
	 * file will not get a swp_entry_t in its pte, but rather it is like
	 * any other file mapping (ie. marked !present and faulted in with
	 * tmpfs's .fault). So swapped out tmpfs mappings are tested here.
	 */
	folio = filemap_get_incore_folio(mapping, index);
	if (!IS_ERR(folio)) {
		present = folio_test_uptodate(folio);
		folio_put(folio);
	}

	return present;
}

static int __mincore_unmapped_range(unsigned long addr, unsigned long end,
				struct vm_area_struct *vma, unsigned char *vec)
{
	unsigned long nr = (end - addr) >> PAGE_SHIFT;
	int i;

	if (vma->vm_file) {
		pgoff_t pgoff;

		pgoff = linear_page_index(vma, addr);
		for (i = 0; i < nr; i++, pgoff++)
			vec[i] = mincore_page(vma->vm_file->f_mapping, pgoff);
	} else {
		for (i = 0; i < nr; i++)
			vec[i] = 0;
	}
	return nr;
}

static int mincore_unmapped_range(unsigned long addr, unsigned long end,
				   __always_unused int depth,
				   struct mm_walk *walk)
{
	walk->private += __mincore_unmapped_range(addr, end,
						  walk->vma, walk->private);
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

	ptep = pte_offset_map_lock(walk->mm, pmd, addr, &ptl);
	if (!ptep) {
		walk->action = ACTION_AGAIN;
		return 0;
	}
	for (; addr != end; ptep++, addr += PAGE_SIZE) {
		pte_t pte = ptep_get(ptep);

		/* We need to do cache lookup too for pte markers */
		if (pte_none_mostly(pte))
			__mincore_unmapped_range(addr, addr + PAGE_SIZE,
						 vma, vec);
		else if (pte_present(pte))
			*vec = 1;
		else { /* pte is a swap entry */
			swp_entry_t entry = pte_to_swp_entry(pte);

			if (non_swap_entry(entry)) {
				/*
				 * migration or hwpoison entries are always
				 * uptodate
				 */
				*vec = 1;
			} else {
#ifdef CONFIG_SWAP
				*vec = mincore_page(swap_address_space(entry),
						    swap_cache_index(entry));
#else
				WARN_ON(1);
				*vec = 1;
#endif
			}
		}
		vec++;
	}
	pte_unmap_unlock(ptep - 1, ptl);
out:
	walk->private += nr;
	cond_resched();
	return 0;
}

static inline bool can_do_mincore(struct vm_area_struct *vma)
{
	if (vma_is_anonymous(vma))
		return true;
	if (!vma->vm_file)
		return false;
	/*
	 * Reveal pagecache information only for non-anonymous mappings that
	 * correspond to the files the calling process could (if tried) open
	 * for writing; otherwise we'd be including shared non-exclusive
	 * mappings, which opens a side channel.
	 */
	return inode_owner_or_capable(&nop_mnt_idmap,
				      file_inode(vma->vm_file)) ||
	       file_permission(vma->vm_file, MAY_WRITE) == 0;
}

static const struct mm_walk_ops mincore_walk_ops = {
	.pmd_entry		= mincore_pte_range,
	.pte_hole		= mincore_unmapped_range,
	.hugetlb_entry		= mincore_hugetlb,
	.walk_lock		= PGWALK_RDLOCK,
};

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

	vma = vma_lookup(current->mm, addr);
	if (!vma)
		return -ENOMEM;
	end = min(vma->vm_end, addr + (pages << PAGE_SHIFT));
	if (!can_do_mincore(vma)) {
		unsigned long pages = DIV_ROUND_UP(end - addr, PAGE_SIZE);
		memset(vec, 1, pages);
		return pages;
	}
	err = walk_page_range(vma->vm_mm, addr, end, &mincore_walk_ops, vec);
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

	start = untagged_addr(start);

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
		mmap_read_lock(current->mm);
		retval = do_mincore(start, min(pages, PAGE_SIZE), tmp);
		mmap_read_unlock(current->mm);

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
