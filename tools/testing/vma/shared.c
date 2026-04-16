// SPDX-License-Identifier: GPL-2.0-or-later

#include "shared.h"


bool fail_prealloc;
unsigned long mmap_min_addr = CONFIG_DEFAULT_MMAP_MIN_ADDR;
unsigned long dac_mmap_min_addr = CONFIG_DEFAULT_MMAP_MIN_ADDR;
unsigned long stack_guard_gap = 256UL<<PAGE_SHIFT;

const struct vm_operations_struct vma_dummy_vm_ops;
struct anon_vma dummy_anon_vma;
struct task_struct __current;

struct vm_area_struct *alloc_vma(struct mm_struct *mm,
		unsigned long start, unsigned long end,
		pgoff_t pgoff, vm_flags_t vm_flags)
{
	struct vm_area_struct *vma = vm_area_alloc(mm);

	if (vma == NULL)
		return NULL;

	vma->vm_start = start;
	vma->vm_end = end;
	vma->vm_pgoff = pgoff;
	vm_flags_reset(vma, vm_flags);
	vma_assert_detached(vma);

	return vma;
}

void detach_free_vma(struct vm_area_struct *vma)
{
	vma_mark_detached(vma);
	vm_area_free(vma);
}

struct vm_area_struct *alloc_and_link_vma(struct mm_struct *mm,
		unsigned long start, unsigned long end,
		pgoff_t pgoff, vm_flags_t vm_flags)
{
	struct vm_area_struct *vma = alloc_vma(mm, start, end, pgoff, vm_flags);

	if (vma == NULL)
		return NULL;

	if (attach_vma(mm, vma)) {
		detach_free_vma(vma);
		return NULL;
	}

	/*
	 * Reset this counter which we use to track whether writes have
	 * begun. Linking to the tree will have caused this to be incremented,
	 * which means we will get a false positive otherwise.
	 */
	vma->vm_lock_seq = UINT_MAX;

	return vma;
}

void reset_dummy_anon_vma(void)
{
	dummy_anon_vma.was_cloned = false;
	dummy_anon_vma.was_unlinked = false;
}

int cleanup_mm(struct mm_struct *mm, struct vma_iterator *vmi)
{
	struct vm_area_struct *vma;
	int count = 0;

	fail_prealloc = false;
	reset_dummy_anon_vma();

	vma_iter_set(vmi, 0);
	for_each_vma(*vmi, vma) {
		detach_free_vma(vma);
		count++;
	}

	mtree_destroy(&mm->mm_mt);
	mm->map_count = 0;
	return count;
}

bool vma_write_started(struct vm_area_struct *vma)
{
	int seq = vma->vm_lock_seq;

	/* We reset after each check. */
	vma->vm_lock_seq = UINT_MAX;

	/* The vma_start_write() stub simply increments this value. */
	return seq > -1;
}

void __vma_set_dummy_anon_vma(struct vm_area_struct *vma,
		struct anon_vma_chain *avc, struct anon_vma *anon_vma)
{
	vma->anon_vma = anon_vma;
	INIT_LIST_HEAD(&vma->anon_vma_chain);
	list_add(&avc->same_vma, &vma->anon_vma_chain);
	avc->anon_vma = vma->anon_vma;
}

void vma_set_dummy_anon_vma(struct vm_area_struct *vma,
		struct anon_vma_chain *avc)
{
	__vma_set_dummy_anon_vma(vma, avc, &dummy_anon_vma);
}

struct task_struct *get_current(void)
{
	return &__current;
}

unsigned long rlimit(unsigned int limit)
{
	return (unsigned long)-1;
}

void vma_set_range(struct vm_area_struct *vma,
		   unsigned long start, unsigned long end,
		   pgoff_t pgoff)
{
	vma->vm_start = start;
	vma->vm_end = end;
	vma->vm_pgoff = pgoff;
}
