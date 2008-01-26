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

static inline int init_new_context(struct task_struct *tsk,
				   struct mm_struct *mm)
{
	mm->context = _ASCE_TABLE_LENGTH | _ASCE_USER_BITS;
#ifdef CONFIG_64BIT
	mm->context |= _ASCE_TYPE_REGION3;
#endif
	return 0;
}

#define destroy_context(mm)             do { } while (0)

#ifndef __s390x__
#define LCTL_OPCODE "lctl"
#else
#define LCTL_OPCODE "lctlg"
#endif

static inline void update_mm(struct mm_struct *mm, struct task_struct *tsk)
{
	S390_lowcore.user_asce = mm->context | __pa(mm->pgd);
	if (switch_amode) {
		/* Load primary space page table origin. */
		pgd_t *shadow_pgd = get_shadow_table(mm->pgd) ? : mm->pgd;
		S390_lowcore.user_exec_asce = mm->context | __pa(shadow_pgd);
		asm volatile(LCTL_OPCODE" 1,1,%0\n"
			     : : "m" (S390_lowcore.user_exec_asce) );
	} else
		/* Load home space page table origin. */
		asm volatile(LCTL_OPCODE" 13,13,%0"
			     : : "m" (S390_lowcore.user_asce) );
}

static inline void switch_mm(struct mm_struct *prev, struct mm_struct *next,
			     struct task_struct *tsk)
{
	if (unlikely(prev == next))
		return;
	cpu_set(smp_processor_id(), next->cpu_vm_mask);
	update_mm(next, tsk);
}

#define enter_lazy_tlb(mm,tsk)	do { } while (0)
#define deactivate_mm(tsk,mm)	do { } while (0)

static inline void activate_mm(struct mm_struct *prev,
                               struct mm_struct *next)
{
        switch_mm(prev, next, current);
	set_fs(current->thread.mm_segment);
}

#endif /* __S390_MMU_CONTEXT_H */
