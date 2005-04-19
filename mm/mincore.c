/*
 *	linux/mm/mincore.c
 *
 * Copyright (C) 1994-1999  Linus Torvalds
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

static long mincore_vma(struct vm_area_struct * vma,
	unsigned long start, unsigned long end, unsigned char __user * vec)
{
	long error, i, remaining;
	unsigned char * tmp;

	error = -ENOMEM;
	if (!vma->vm_file)
		return error;

	start = ((start - vma->vm_start) >> PAGE_SHIFT) + vma->vm_pgoff;
	if (end > vma->vm_end)
		end = vma->vm_end;
	end = ((end - vma->vm_start) >> PAGE_SHIFT) + vma->vm_pgoff;

	error = -EAGAIN;
	tmp = (unsigned char *) __get_free_page(GFP_KERNEL);
	if (!tmp)
		return error;

	/* (end - start) is # of pages, and also # of bytes in "vec */
	remaining = (end - start),

	error = 0;
	for (i = 0; remaining > 0; remaining -= PAGE_SIZE, i++) {
		int j = 0;
		long thispiece = (remaining < PAGE_SIZE) ?
						remaining : PAGE_SIZE;

		while (j < thispiece)
			tmp[j++] = mincore_page(vma, start++);

		if (copy_to_user(vec + PAGE_SIZE * i, tmp, thispiece)) {
			error = -EFAULT;
			break;
		}
	}

	free_page((unsigned long) tmp);
	return error;
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
	int index = 0;
	unsigned long end, limit;
	struct vm_area_struct * vma;
	size_t max;
	int unmapped_error = 0;
	long error;

	/* check the arguments */
 	if (start & ~PAGE_CACHE_MASK)
		goto einval;

	limit = TASK_SIZE;
	if (start >= limit)
		goto enomem;

	if (!len)
		return 0;

	max = limit - start;
	len = PAGE_CACHE_ALIGN(len);
	if (len > max || !len)
		goto enomem;

	end = start + len;

	/* check the output buffer whilst holding the lock */
	error = -EFAULT;
	down_read(&current->mm->mmap_sem);

	if (!access_ok(VERIFY_WRITE, vec, len >> PAGE_SHIFT))
		goto out;

	/*
	 * If the interval [start,end) covers some unmapped address
	 * ranges, just ignore them, but return -ENOMEM at the end.
	 */
	error = 0;

	vma = find_vma(current->mm, start);
	while (vma) {
		/* Here start < vma->vm_end. */
		if (start < vma->vm_start) {
			unmapped_error = -ENOMEM;
			start = vma->vm_start;
		}

		/* Here vma->vm_start <= start < vma->vm_end. */
		if (end <= vma->vm_end) {
			if (start < end) {
				error = mincore_vma(vma, start, end,
							&vec[index]);
				if (error)
					goto out;
			}
			error = unmapped_error;
			goto out;
		}

		/* Here vma->vm_start <= start < vma->vm_end < end. */
		error = mincore_vma(vma, start, vma->vm_end, &vec[index]);
		if (error)
			goto out;
		index += (vma->vm_end - start) >> PAGE_CACHE_SHIFT;
		start = vma->vm_end;
		vma = vma->vm_next;
	}

	/* we found a hole in the area queried if we arrive here */
	error = -ENOMEM;

out:
	up_read(&current->mm->mmap_sem);
	return error;

einval:
	return -EINVAL;
enomem:
	return -ENOMEM;
}
