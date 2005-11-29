/*
 *	linux/mm/madvise.c
 *
 * Copyright (C) 1999  Linus Torvalds
 * Copyright (C) 2002  Christoph Hellwig
 */

#include <linux/mman.h>
#include <linux/pagemap.h>
#include <linux/syscalls.h>
#include <linux/mempolicy.h>
#include <linux/hugetlb.h>

/*
 * We can potentially split a vm area into separate
 * areas, each area with its own behavior.
 */
static long madvise_behavior(struct vm_area_struct * vma,
		     struct vm_area_struct **prev,
		     unsigned long start, unsigned long end, int behavior)
{
	struct mm_struct * mm = vma->vm_mm;
	int error = 0;
	pgoff_t pgoff;
	int new_flags = vma->vm_flags & ~VM_READHINTMASK;

	switch (behavior) {
	case MADV_SEQUENTIAL:
		new_flags |= VM_SEQ_READ;
		break;
	case MADV_RANDOM:
		new_flags |= VM_RAND_READ;
		break;
	default:
		break;
	}

	if (new_flags == vma->vm_flags) {
		*prev = vma;
		goto out;
	}

	pgoff = vma->vm_pgoff + ((start - vma->vm_start) >> PAGE_SHIFT);
	*prev = vma_merge(mm, *prev, start, end, new_flags, vma->anon_vma,
				vma->vm_file, pgoff, vma_policy(vma));
	if (*prev) {
		vma = *prev;
		goto success;
	}

	*prev = vma;

	if (start != vma->vm_start) {
		error = split_vma(mm, vma, start, 1);
		if (error)
			goto out;
	}

	if (end != vma->vm_end) {
		error = split_vma(mm, vma, end, 0);
		if (error)
			goto out;
	}

success:
	/*
	 * vm_flags is protected by the mmap_sem held in write mode.
	 */
	vma->vm_flags = new_flags;

out:
	if (error == -ENOMEM)
		error = -EAGAIN;
	return error;
}

/*
 * Schedule all required I/O operations.  Do not wait for completion.
 */
static long madvise_willneed(struct vm_area_struct * vma,
			     struct vm_area_struct ** prev,
			     unsigned long start, unsigned long end)
{
	struct file *file = vma->vm_file;

	if (!file)
		return -EBADF;

	if (file->f_mapping->a_ops->get_xip_page) {
		/* no bad return value, but ignore advice */
		return 0;
	}

	*prev = vma;
	start = ((start - vma->vm_start) >> PAGE_SHIFT) + vma->vm_pgoff;
	if (end > vma->vm_end)
		end = vma->vm_end;
	end = ((end - vma->vm_start) >> PAGE_SHIFT) + vma->vm_pgoff;

	force_page_cache_readahead(file->f_mapping,
			file, start, max_sane_readahead(end - start));
	return 0;
}

/*
 * Application no longer needs these pages.  If the pages are dirty,
 * it's OK to just throw them away.  The app will be more careful about
 * data it wants to keep.  Be sure to free swap resources too.  The
 * zap_page_range call sets things up for refill_inactive to actually free
 * these pages later if no one else has touched them in the meantime,
 * although we could add these pages to a global reuse list for
 * refill_inactive to pick up before reclaiming other pages.
 *
 * NB: This interface discards data rather than pushes it out to swap,
 * as some implementations do.  This has performance implications for
 * applications like large transactional databases which want to discard
 * pages in anonymous maps after committing to backing store the data
 * that was kept in them.  There is no reason to write this data out to
 * the swap area if the application is discarding it.
 *
 * An interface that causes the system to free clean pages and flush
 * dirty pages is already available as msync(MS_INVALIDATE).
 */
static long madvise_dontneed(struct vm_area_struct * vma,
			     struct vm_area_struct ** prev,
			     unsigned long start, unsigned long end)
{
	*prev = vma;
	if (vma->vm_flags & (VM_LOCKED|VM_HUGETLB|VM_PFNMAP))
		return -EINVAL;

	if (unlikely(vma->vm_flags & VM_NONLINEAR)) {
		struct zap_details details = {
			.nonlinear_vma = vma,
			.last_index = ULONG_MAX,
		};
		zap_page_range(vma, start, end - start, &details);
	} else
		zap_page_range(vma, start, end - start, NULL);
	return 0;
}

static long
madvise_vma(struct vm_area_struct *vma, struct vm_area_struct **prev,
		unsigned long start, unsigned long end, int behavior)
{
	long error;

	switch (behavior) {
	case MADV_NORMAL:
	case MADV_SEQUENTIAL:
	case MADV_RANDOM:
		error = madvise_behavior(vma, prev, start, end, behavior);
		break;

	case MADV_WILLNEED:
		error = madvise_willneed(vma, prev, start, end);
		break;

	case MADV_DONTNEED:
		error = madvise_dontneed(vma, prev, start, end);
		break;

	default:
		error = -EINVAL;
		break;
	}
	return error;
}

/*
 * The madvise(2) system call.
 *
 * Applications can use madvise() to advise the kernel how it should
 * handle paging I/O in this VM area.  The idea is to help the kernel
 * use appropriate read-ahead and caching techniques.  The information
 * provided is advisory only, and can be safely disregarded by the
 * kernel without affecting the correct operation of the application.
 *
 * behavior values:
 *  MADV_NORMAL - the default behavior is to read clusters.  This
 *		results in some read-ahead and read-behind.
 *  MADV_RANDOM - the system should read the minimum amount of data
 *		on any access, since it is unlikely that the appli-
 *		cation will need more than what it asks for.
 *  MADV_SEQUENTIAL - pages in the given range will probably be accessed
 *		once, so they can be aggressively read ahead, and
 *		can be freed soon after they are accessed.
 *  MADV_WILLNEED - the application is notifying the system to read
 *		some pages ahead.
 *  MADV_DONTNEED - the application is finished with the given range,
 *		so the kernel can free resources associated with it.
 *
 * return values:
 *  zero    - success
 *  -EINVAL - start + len < 0, start is not page-aligned,
 *		"behavior" is not a valid value, or application
 *		is attempting to release locked or shared pages.
 *  -ENOMEM - addresses in the specified range are not currently
 *		mapped, or are outside the AS of the process.
 *  -EIO    - an I/O error occurred while paging in data.
 *  -EBADF  - map exists, but area maps something that isn't a file.
 *  -EAGAIN - a kernel resource was temporarily unavailable.
 */
asmlinkage long sys_madvise(unsigned long start, size_t len_in, int behavior)
{
	unsigned long end, tmp;
	struct vm_area_struct * vma, *prev;
	int unmapped_error = 0;
	int error = -EINVAL;
	size_t len;

	down_write(&current->mm->mmap_sem);

	if (start & ~PAGE_MASK)
		goto out;
	len = (len_in + ~PAGE_MASK) & PAGE_MASK;

	/* Check to see whether len was rounded up from small -ve to zero */
	if (len_in && !len)
		goto out;

	end = start + len;
	if (end < start)
		goto out;

	error = 0;
	if (end == start)
		goto out;

	/*
	 * If the interval [start,end) covers some unmapped address
	 * ranges, just ignore them, but return -ENOMEM at the end.
	 * - different from the way of handling in mlock etc.
	 */
	vma = find_vma_prev(current->mm, start, &prev);
	if (vma && start > vma->vm_start)
		prev = vma;

	for (;;) {
		/* Still start < end. */
		error = -ENOMEM;
		if (!vma)
			goto out;

		/* Here start < (end|vma->vm_end). */
		if (start < vma->vm_start) {
			unmapped_error = -ENOMEM;
			start = vma->vm_start;
			if (start >= end)
				goto out;
		}

		/* Here vma->vm_start <= start < (end|vma->vm_end) */
		tmp = vma->vm_end;
		if (end < tmp)
			tmp = end;

		/* Here vma->vm_start <= start < tmp <= (end|vma->vm_end). */
		error = madvise_vma(vma, &prev, start, tmp, behavior);
		if (error)
			goto out;
		start = tmp;
		if (start < prev->vm_end)
			start = prev->vm_end;
		error = unmapped_error;
		if (start >= end)
			goto out;
		vma = prev->vm_next;
	}
out:
	up_write(&current->mm->mmap_sem);
	return error;
}
