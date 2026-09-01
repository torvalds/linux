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
 * Seal VMAs in the specified input range to prevent an attacker replacing what
 * is mapped in the range with something else.
 *
 * Disallows:
 * - VMA unmapping, remapping or shrinking.
 * - Overwriting the VMA with another one via mmap(), mremap() or similar.
 * - Alteration of properties via mprotect()/pkey_mprotect().
 * - Destructive madvise() behaviours (like MADV_DONTNEED) on anonymous read-only
 *   ranges.
 *
 * Since unmapped ranges can be mapped at any time, the input range must span
 * mapped ranges only.
 *
 * The flags parameter is currently reserved.
 */
SYSCALL_DEFINE3(mseal, unsigned long, start, size_t, len, unsigned long, flags)
{
	size_t len_aligned;
	unsigned long end;

	/* Verify flags not set. */
	if (flags)
		return -EINVAL;

	start = untagged_addr(start);
	if (!PAGE_ALIGNED(start))
		return -EINVAL;

	len_aligned = PAGE_ALIGN(len);
	/* Check to see whether len was rounded up from small -ve to zero. */
	if (len && !len_aligned)
		return -EINVAL;

	end = start + len_aligned;
	if (end < start)
		return -EINVAL;

	if (end == start)
		return 0;

	return mseal_range(start, end);
}
