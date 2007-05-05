/*
 *  include/asm-s390/mmu_context.h
 *
 *  S390 version
 *
 *  Derived from "include/asm-i386/mmu_context.h"
 */

#ifndef __S390_MMU_CONTEXT_H
#define __S390_MMU_CONTEXT_H

#include <asm/pgalloc.h>
#include <asm-generic/mm_hooks.h>

/*
 * get a new mmu context.. S390 don't know about contexts.
 */
#define init_new_context(tsk,mm)        0

#define destroy_context(mm)             do { } while (0)

#ifndef __s390x__
#define LCTL_OPCODE "lctl"
#define PGTABLE_BITS (_SEGMENT_TABLE|USER_STD_MASK)
#else
#define LCTL_OPCODE "lctlg"
#define PGTABLE_BITS (_REGION_TABLE|USER_STD_MASK)
#endif

static inline void enter_lazy_tlb(struct mm_struct *mm,
                                  struct task_struct *tsk)
{
}

static inline void switch_mm(struct mm_struct *prev, struct mm_struct *next,
			     struct task_struct *tsk)
{
	pgd_t *shadow_pgd = get_shadow_pgd(next->pgd);

	if (prev != next) {
		S390_lowcore.user_asce = (__pa(next->pgd) & PAGE_MASK) |
					 PGTABLE_BITS;
		if (shadow_pgd) {
			/* Load primary/secondary space page table origin. */
			S390_lowcore.user_exec_asce =
				(__pa(shadow_pgd) & PAGE_MASK) | PGTABLE_BITS;
			asm volatile(LCTL_OPCODE" 1,1,%0\n"
				     LCTL_OPCODE" 7,7,%1"
				     : : "m" (S390_lowcore.user_exec_asce),
					 "m" (S390_lowcore.user_asce) );
		} else if (switch_amode) {
			/* Load primary space page table origin. */
			asm volatile(LCTL_OPCODE" 1,1,%0"
				     : : "m" (S390_lowcore.user_asce) );
		} else
			/* Load home space page table origin. */
			asm volatile(LCTL_OPCODE" 13,13,%0"
				     : : "m" (S390_lowcore.user_asce) );
	}
	cpu_set(smp_processor_id(), next->cpu_vm_mask);
}

#define deactivate_mm(tsk,mm)	do { } while (0)

static inline void activate_mm(struct mm_struct *prev,
                               struct mm_struct *next)
{
        switch_mm(prev, next, current);
	set_fs(current->thread.mm_segment);
}

#endif /* __S390_MMU_CONTEXT_H */
