// SPDX-License-Identifier: GPL-2.0
/*
 *  Implement mseal() syscall.
 *
 *  Copyright (c) 2023,2024 Google, Inc.
 *
 *  Author: Jeff Xu <jeffxu@chromium.org>
 */

#include <linux/mempolicy.h>
#include <linux/minmax.h>
#include <linux/mman.h>
#include <linux/mm.h>
#include <linux/mm_inline.h>
#include <linux/syscalls.h>
#include <linux/sched.h>
#include "internal.h"

static bool range_contains_unmapped(unsigned long start, unsigned long end)
{
	VMA_ITERATOR(vmi, current->mm, start);
	unsigned long prev_end = start;
	struct vm_area_struct *vma;

	for_each_vma_range(vmi, vma, end) {
		if (vma->vm_start > prev_end)
			return true;

		prev_end = vma->vm_end;
	}

	return prev_end < end;
}

static int __mseal_range(unsigned long start, unsigned long end)
{
	VMA_ITERATOR(vmi, current->mm, start);
	struct vm_area_struct *vma, *prev;

	/* We know there are no gaps so this will be non-NULL. */
	vma = vma_iter_load(&vmi);
	prev = vma_prev(&vmi);
	if (start > vma->vm_start)
		prev = vma;

	for_each_vma_range(vmi, vma, end) {
		const unsigned long curr_start = max(vma->vm_start, start);
		const unsigned long curr_end = min(vma->vm_end, end);

		if (!vma_test(vma, VMA_SEALED_BIT)) {
			vma_flags_t vma_flags = vma->flags;

			vma_flags_set(&vma_flags, VMA_SEALED_BIT);

			vma = vma_modify_flags(&vmi, prev, vma, curr_start,
					       curr_end, &vma_flags);
			if (IS_ERR(vma))
				return PTR_ERR(vma);
			vma_start_write(vma);
			vma_set_flags(vma, VMA_SEALED_BIT);
		}

		prev = vma;
	}

	return 0;
}

static int mseal_range(unsigned long start, unsigned long end)
{
	int err;

	err = mmap_write_lock_killable(current->mm);
	if (err)
		return err;
	if (range_contains_unmapped(start, end))
		err = -ENOMEM;
	else
		err = __mseal_range(start, end);
	mmap_write_unlock(current->mm);
	return err;
}

/**
 * mseal_mmap_page_zero() - If the MMAP_PAGE_ZERO personality is set, mseal()
 * the page mapped at address zero.
 */
void mseal_mmap_page_zero(void)
{
	int err;

	if (WARN_ON_ONCE(!(current->personality & MMAP_PAGE_ZERO)))
		return;

	err = mseal_range(0, PAGE_SIZE);
	if (err)
		pr_warn_ratelimited("pid=%d, couldn't seal address 0, ret=%d.\n",
				    task_pid_nr(current), err);
}

/*
 * mseal(2) seals the VM's meta data from
 * selected syscalls.
 *
 * addr/len: VM address range.
 *
 *  The address range by addr/len must meet:
 *   start (addr) must be in a valid VMA.
 *   end (addr + len) must be in a valid VMA.
 *   no gap (unallocated memory) between start and end.
 *   start (addr) must be page aligned.
 *
 *  len: len will be page aligned implicitly.
 *
 *   Below VMA operations are blocked after sealing.
 *   1> Unmapping, moving to another location, and shrinking
 *	the size, via munmap() and mremap(), can leave an empty
 *	space, therefore can be replaced with a VMA with a new
 *	set of attributes.
 *   2> Moving or expanding a different vma into the current location,
 *	via mremap().
 *   3> Modifying a VMA via mmap(MAP_FIXED).
 *   4> Size expansion, via mremap(), does not appear to pose any
 *	specific risks to sealed VMAs. It is included anyway because
 *	the use case is unclear. In any case, users can rely on
 *	merging to expand a sealed VMA.
 *   5> mprotect and pkey_mprotect.
 *   6> Some destructive madvice() behavior (e.g. MADV_DONTNEED)
 *      for anonymous memory, when users don't have write permission to the
 *	memory. Those behaviors can alter region contents by discarding pages,
 *	effectively a memset(0) for anonymous memory.
 *
 *  flags: reserved.
 *
 * return values:
 *  zero: success.
 *  -EINVAL:
 *   invalid input flags.
 *   start address is not page aligned.
 *   Address range (start + len) overflow.
 *  -ENOMEM:
 *   addr is not a valid address (not allocated).
 *   end (start + len) is not a valid address.
 *   a gap (unallocated memory) between start and end.
 *  -EPERM:
 *  - In 32 bit architecture, sealing is not supported.
 * Note:
 *  user can call mseal(2) multiple times, adding a seal on an
 *  already sealed memory is a no-action (no error).
 *
 *  unseal() is not supported.
 */
static int do_mseal(unsigned long start, size_t len_in, unsigned long flags)
{
	unsigned long end;
	size_t len;

	/* Verify flags not set. */
	if (flags)
		return -EINVAL;

	start = untagged_addr(start);
	if (!PAGE_ALIGNED(start))
		return -EINVAL;

	len = PAGE_ALIGN(len_in);
	/* Check to see whether len was rounded up from small -ve to zero. */
	if (len_in && !len)
		return -EINVAL;

	end = start + len;
	if (end < start)
		return -EINVAL;

	if (end == start)
		return 0;

	return mseal_range(start, end);
}

SYSCALL_DEFINE3(mseal, unsigned long, start, size_t, len, unsigned long,
		flags)
{
	return do_mseal(start, len, flags);
}
