// SPDX-License-Identifier: GPL-2.0-only

/*
 * Functions explicitly implemented for exec functionality which however are
 * explicitly VMA-only logic.
 */

#include "vma_internal.h"
#include "vma.h"

/*
 * Relocate a VMA downwards by shift bytes. There cannot be any VMAs between
 * this VMA and its relocated range, which will now reside at [vma->vm_start -
 * shift, vma->vm_end - shift).
 *
 * This function is almost certainly NOT what you want for anything other than
 * early executable temporary stack relocation.
 */
int relocate_vma_down(struct vm_area_struct *vma, unsigned long shift)
{
	/*
	 * The process proceeds as follows:
	 *
	 * 1) Use shift to calculate the new vma endpoints.
	 * 2) Extend vma to cover both the old and new ranges.  This ensures the
	 *    arguments passed to subsequent functions are consistent.
	 * 3) Move vma's page tables to the new range.
	 * 4) Free up any cleared pgd range.
	 * 5) Shrink the vma to cover only the new range.
	 */

	struct mm_struct *mm = vma->vm_mm;
	unsigned long old_start = vma->vm_start;
	unsigned long old_end = vma->vm_end;
	unsigned long length = old_end - old_start;
	unsigned long new_start = old_start - shift;
	unsigned long new_end = old_end - shift;
	VMA_ITERATOR(vmi, mm, new_start);
	VMG_STATE(vmg, mm, &vmi, new_start, old_end, 0, vma->vm_pgoff);
	struct vm_area_struct *next;
	struct mmu_gather tlb;
	PAGETABLE_MOVE(pmc, vma, vma, old_start, new_start, length);

	BUG_ON(new_start > new_end);

	/*
	 * ensure there are no vmas between where we want to go
	 * and where we are
	 */
	if (vma != vma_next(&vmi))
		return -EFAULT;

	vma_iter_prev_range(&vmi);
	/*
	 * cover the whole range: [new_start, old_end)
	 */
	vmg.target = vma;
	if (vma_expand(&vmg))
		return -ENOMEM;

	/*
	 * move the page tables downwards, on failure we rely on
	 * process cleanup to remove whatever mess we made.
	 */
	pmc.for_stack = true;
	if (length != move_page_tables(&pmc))
		return -ENOMEM;

	tlb_gather_mmu(&tlb, mm);
	next = vma_next(&vmi);
	if (new_end > old_start) {
		/*
		 * when the old and new regions overlap clear from new_end.
		 */
		free_pgd_range(&tlb, new_end, old_end, new_end,
			next ? next->vm_start : USER_PGTABLES_CEILING);
	} else {
		/*
		 * otherwise, clean from old_start; this is done to not touch
		 * the address space in [new_end, old_start) some architectures
		 * have constraints on va-space that make this illegal (IA64) -
		 * for the others its just a little faster.
		 */
		free_pgd_range(&tlb, old_start, old_end, new_end,
			next ? next->vm_start : USER_PGTABLES_CEILING);
	}
	tlb_finish_mmu(&tlb);

	vma_prev(&vmi);
	/* Shrink the vma to just the new range */
	return vma_shrink(&vmi, vma, new_start, new_end, vma->vm_pgoff);
}

/*
 * Establish the stack VMA in an execve'd process, located temporarily at the
 * maximum stack address provided by the architecture.
 *
 * We later relocate this downwards in relocate_vma_down().
 *
 * This function is almost certainly NOT what you want for anything other than
 * early executable initialisation.
 *
 * On success, returns 0 and sets *vmap to the stack VMA and *top_mem_p to the
 * maximum addressable location in the stack (that is capable of storing a
 * system word of data).
 */
int create_init_stack_vma(struct mm_struct *mm, struct vm_area_struct **vmap,
			  unsigned long *top_mem_p)
{
	int err;
	struct vm_area_struct *vma = vm_area_alloc(mm);

	if (!vma)
		return -ENOMEM;

	vma_set_anonymous(vma);

	if (mmap_write_lock_killable(mm)) {
		err = -EINTR;
		goto err_free;
	}

	/*
	 * Need to be called with mmap write lock
	 * held, to avoid race with ksmd.
	 */
	err = ksm_execve(mm);
	if (err)
		goto err_ksm;

	/*
	 * Place the stack at the largest stack address the architecture
	 * supports. Later, we'll move this to an appropriate place. We don't
	 * use STACK_TOP because that can depend on attributes which aren't
	 * configured yet.
	 */
	BUILD_BUG_ON(VM_STACK_FLAGS & VM_STACK_INCOMPLETE_SETUP);
	vma->vm_end = STACK_TOP_MAX;
	vma->vm_start = vma->vm_end - PAGE_SIZE;
	vm_flags_init(vma, VM_SOFTDIRTY | VM_STACK_FLAGS | VM_STACK_INCOMPLETE_SETUP);
	vma->vm_page_prot = vm_get_page_prot(vma->vm_flags);

	err = insert_vm_struct(mm, vma);
	if (err)
		goto err;

	mm->stack_vm = mm->total_vm = 1;
	mmap_write_unlock(mm);
	*vmap = vma;
	*top_mem_p = vma->vm_end - sizeof(void *);
	return 0;

err:
	ksm_exit(mm);
err_ksm:
	mmap_write_unlock(mm);
err_free:
	*vmap = NULL;
	vm_area_free(vma);
	return err;
}
