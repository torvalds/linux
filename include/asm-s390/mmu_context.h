/*
 *  include/asm-s390/mmu_context.h
 *
 *  S390 version
 *
 *  Derived from "include/asm-i386/mmu_context.h"
 */

#ifndef __S390_MMU_CONTEXT_H
#define __S390_MMU_CONTEXT_H

/*
 * get a new mmu context.. S390 don't know about contexts.
 */
#define init_new_context(tsk,mm)        0

#define destroy_context(mm)             do { } while (0)

static inline void enter_lazy_tlb(struct mm_struct *mm,
                                  struct task_struct *tsk)
{
}

static inline void switch_mm(struct mm_struct *prev, struct mm_struct *next,
                             struct task_struct *tsk)
{
        if (prev != next) {
#ifndef __s390x__
	        S390_lowcore.user_asce = (__pa(next->pgd)&PAGE_MASK) |
                      (_SEGMENT_TABLE|USER_STD_MASK);
                /* Load home space page table origin. */
                asm volatile("lctl  13,13,%0"
			     : : "m" (S390_lowcore.user_asce) );
#else /* __s390x__ */
                S390_lowcore.user_asce = (__pa(next->pgd) & PAGE_MASK) |
			(_REGION_TABLE|USER_STD_MASK);
		/* Load home space page table origin. */
		asm volatile("lctlg  13,13,%0"
			     : : "m" (S390_lowcore.user_asce) );
#endif /* __s390x__ */
        }
	cpu_set(smp_processor_id(), next->cpu_vm_mask);
}

#define deactivate_mm(tsk,mm)	do { } while (0)

extern inline void activate_mm(struct mm_struct *prev,
                               struct mm_struct *next)
{
        switch_mm(prev, next, current);
	set_fs(current->thread.mm_segment);
}

#endif
