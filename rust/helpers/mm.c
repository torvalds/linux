// SPDX-License-Identifier: GPL-2.0

#include <linux/mm.h>
#include <linux/sched/mm.h>

void rust_helper_mmgrab(struct mm_struct *mm)
{
	mmgrab(mm);
}

void rust_helper_mmdrop(struct mm_struct *mm)
{
	mmdrop(mm);
}

void rust_helper_mmget(struct mm_struct *mm)
{
	mmget(mm);
}

bool rust_helper_mmget_not_zero(struct mm_struct *mm)
{
	return mmget_not_zero(mm);
}

void rust_helper_mmap_read_lock(struct mm_struct *mm)
{
	mmap_read_lock(mm);
}

bool rust_helper_mmap_read_trylock(struct mm_struct *mm)
{
	return mmap_read_trylock(mm);
}

void rust_helper_mmap_read_unlock(struct mm_struct *mm)
{
	mmap_read_unlock(mm);
}

struct vm_area_struct *rust_helper_vma_lookup(struct mm_struct *mm,
					      unsigned long addr)
{
	return vma_lookup(mm, addr);
}

void rust_helper_vma_end_read(struct vm_area_struct *vma)
{
	vma_end_read(vma);
}
