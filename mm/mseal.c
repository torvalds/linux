// SPDX-License-Identifier: GPL-2.0
/*
 *  Implement mseal() syscall.
 *
 *  Copyright (c) 2023,2024 Google, Inc.
 *
 *  Author: Jeff Xu <jeffxu@chromium.org>
 */

#include <linux/mempolicy.h>
#include <linux/mman.h>
#include <linux/mm.h>
#include <linux/mm_inline.h>
#include <linux/mmu_context.h>
#include <linux/syscalls.h>
#include <linux/sched.h>
#include "internal.h"

static inline void set_vma_sealed(struct vm_area_struct *vma)
{
	vm_flags_set(vma, VM_SEALED);
}

static bool is_madv_discard(int behavior)
{
	switch (behavior) {
	case MADV_FREE:
	case MADV_DONTNEED:
	case MADV_DONTNEED_LOCKED:
	case MADV_REMOVE:
	case MADV_DONTFORK:
	case MADV_WIPEONFORK:
		return true;
	}

	return false;
}

static bool is_ro_anon(struct vm_area_struct *vma)
{
	/* check anonymous mapping. */
	if (vma->vm_file || vma->vm_flags & VM_SHARED)
		return false;

	/*
	 * check for non-writable:
	 * PROT=RO or PKRU is not writeable.
	 */
	if (!(vma->vm_flags & VM_WRITE) ||
		!arch_vma_access_permitted(vma, true, false, false))
		return true;

	return false;
}

/*
 * Check if the vmas of a memory range are allowed to be modified.
 * the memory ranger can have a gap (unallocated memory).
 * return true, if it is allowed.
 */
bool can_modify_mm(struct mm_struct *mm, unsigned long start, unsigned long end)
{
	struct vm_area_struct *vma;

	VMA_ITERATOR(vmi, mm, start);

	/* going through each vma to check. */
	for_each_vma_range(vmi, vma, end) {
		if (unlikely(!can_modify_vma(vma)))
			return false;
	}

	/* Allow by default. */
	return true;
}

/*
 * Check if a vma is allowed to be modified by madvise.
 */
bool can_modify_vma_madv(struct vm_area_struct *vma, int behavior)
{
	if (!is_madv_discard(behavior))
		return true;

	if (unlikely(!can_modify_vma(vma) && is_ro_anon(vma)))
		return false;

	/* Allow by default. */
	return true;
}

static int mseal_fixup(struct vma_iterator *vmi, struct vm_area_struct *vma,
		struct vm_area_struct **prev, unsigned long start,
		unsigned long end, vm_flags_t newflags)
{
	int ret = 0;
	vm_flags_t oldflags = vma->vm_flags;

	if (newflags == oldflags)
		goto out;

	vma = vma_modify_flags(vmi, *prev, vma, start, end, newflags);
	if (IS_ERR(vma)) {
		ret = PTR_ERR(vma);
		goto out;
	}

	set_vma_sealed(vma);
out:
	*prev = vma;
	return ret;
}

/*
 * Check for do_mseal:
 * 1> start is part of a valid vma.
 * 2> end is part of a valid vma.
 * 3> No gap (unallocated address) between start and end.
 * 4> map is sealable.
 */
static int check_mm_seal(unsigned long start, unsigned long end)
{
	struct vm_area_struct *vma;
	unsigned long nstart = start;

	VMA_ITERATOR(vmi, current->mm, start);

	/* going through each vma to check. */
	for_each_vma_range(vmi, vma, end) {
		if (vma->vm_start > nstart)
			/* unallocated memory found. */
			return -ENOMEM;

		if (vma->vm_end >= end)
			return 0;

		nstart = vma->vm_end;
	}

	return -ENOMEM;
}

/*
 * Apply sealing.
 */
static int apply_mm_seal(unsigned long start, unsigned long end)
{
	unsigned long nstart;
	struct vm_area_struct *vma, *prev;

	VMA_ITERATOR(vmi, current->mm, start);

	vma = vma_iter_load(&vmi);
	/*
	 * Note: check_mm_seal should already checked ENOMEM case.
	 * so vma should not be null, same for the other ENOMEM cases.
	 */
	prev = vma_prev(&vmi);
	if (start > vma->vm_start)
		prev = vma;

	nstart = start;
	for_each_vma_range(vmi, vma, end) {
		int error;
		unsigned long tmp;
		vm_flags_t newflags;

		newflags = vma->vm_flags | VM_SEALED;
		tmp = vma->vm_end;
		if (tmp > end)
			tmp = end;
		error = mseal_fixup(&vmi, vma, &prev, nstart, tmp, newflags);
		if (error)
			return error;
		nstart = vma_iter_end(&vmi);
	}

	return 0;
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
 *   Address arange (start + len) overflow.
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
	size_t len;
	int ret = 0;
	unsigned long end;
	struct mm_struct *mm = current->mm;

	ret = can_do_mseal(flags);
	if (ret)
		return ret;

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

	if (mmap_write_lock_killable(mm))
		return -EINTR;

	/*
	 * First pass, this helps to avoid
	 * partial sealing in case of error in input address range,
	 * e.g. ENOMEM error.
	 */
	ret = check_mm_seal(start, end);
	if (ret)
		goto out;

	/*
	 * Second pass, this should success, unless there are errors
	 * from vma_modify_flags, e.g. merge/split error, or process
	 * reaching the max supported VMAs, however, those cases shall
	 * be rare.
	 */
	ret = apply_mm_seal(start, end);

out:
	mmap_write_unlock(current->mm);
	return ret;
}

SYSCALL_DEFINE3(mseal, unsigned long, start, size_t, len, unsigned long,
		flags)
{
	return do_mseal(start, len, flags);
}
