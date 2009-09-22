/*
 * Initial dummy version just to illustrate KSM's interface to other files.
 */

#include <linux/errno.h>
#include <linux/mman.h>
#include <linux/ksm.h>

int ksm_madvise(struct vm_area_struct *vma, unsigned long start,
		unsigned long end, int advice, unsigned long *vm_flags)
{
	struct mm_struct *mm = vma->vm_mm;

	switch (advice) {
	case MADV_MERGEABLE:
		/*
		 * Be somewhat over-protective for now!
		 */
		if (*vm_flags & (VM_MERGEABLE | VM_SHARED  | VM_MAYSHARE   |
				 VM_PFNMAP    | VM_IO      | VM_DONTEXPAND |
				 VM_RESERVED  | VM_HUGETLB | VM_INSERTPAGE |
				 VM_MIXEDMAP  | VM_SAO))
			return 0;		/* just ignore the advice */

		if (!test_bit(MMF_VM_MERGEABLE, &mm->flags))
			if (__ksm_enter(mm) < 0)
				return -EAGAIN;

		*vm_flags |= VM_MERGEABLE;
		break;

	case MADV_UNMERGEABLE:
		if (!(*vm_flags & VM_MERGEABLE))
			return 0;		/* just ignore the advice */

		/* Unmerge any merged pages here */

		*vm_flags &= ~VM_MERGEABLE;
		break;
	}

	return 0;
}

int __ksm_enter(struct mm_struct *mm)
{
	/* Allocate a structure to track mm and link it into KSM's list */
	set_bit(MMF_VM_MERGEABLE, &mm->flags);
	return 0;
}

void __ksm_exit(struct mm_struct *mm)
{
	/* Unlink and free all KSM's structures which track this mm */
	clear_bit(MMF_VM_MERGEABLE, &mm->flags);
}
