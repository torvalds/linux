#ifndef __ASM_GENERIC_MMU_CONTEXT_H
#define __ASM_GENERIC_MMU_CONTEXT_H

/*
 * Generic hooks for NOMMU architectures, which do not need to do
 * anything special here.
 */

#include <asm-generic/mm_hooks.h>

struct task_struct;
struct mm_struct;

static inline void enter_lazy_tlb(struct mm_struct *mm,
			struct task_struct *tsk)
{
}

static inline int init_new_context(struct task_struct *tsk,
			struct mm_struct *mm)
{
	return 0;
}

static inline void destroy_context(struct mm_struct *mm)
{
}

static inline void deactivate_mm(struct task_struct *task,
			struct mm_struct *mm)
{
}

static inline void switch_mm(struct mm_struct *prev,
			struct mm_struct *next,
			struct task_struct *tsk)
{
}

static inline void activate_mm(struct mm_struct *prev_mm,
			       struct mm_struct *next_mm)
{
}

static inline void arch_bprm_mm_init(struct mm_struct *mm,
			struct vm_area_struct *vma)
{
}

#endif /* __ASM_GENERIC_MMU_CONTEXT_H */
