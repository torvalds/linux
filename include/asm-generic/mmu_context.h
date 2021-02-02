/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ASM_GENERIC_MMU_CONTEXT_H
#define __ASM_GENERIC_MMU_CONTEXT_H

/*
 * Generic hooks to implement no-op functionality.
 */

struct task_struct;
struct mm_struct;

/*
 * enter_lazy_tlb - Called when "tsk" is about to enter lazy TLB mode.
 *
 * @mm:  the currently active mm context which is becoming lazy
 * @tsk: task which is entering lazy tlb
 *
 * tsk->mm will be NULL
 */
#ifndef enter_lazy_tlb
static inline void enter_lazy_tlb(struct mm_struct *mm,
			struct task_struct *tsk)
{
}
#endif

/**
 * init_new_context - Initialize context of a new mm_struct.
 * @tsk: task struct for the mm
 * @mm:  the new mm struct
 * @return: 0 on success, -errno on failure
 */
#ifndef init_new_context
static inline int init_new_context(struct task_struct *tsk,
			struct mm_struct *mm)
{
	return 0;
}
#endif

/**
 * destroy_context - Undo init_new_context when the mm is going away
 * @mm: old mm struct
 */
#ifndef destroy_context
static inline void destroy_context(struct mm_struct *mm)
{
}
#endif

/**
 * activate_mm - called after exec switches the current task to a new mm, to switch to it
 * @prev_mm: previous mm of this task
 * @next_mm: new mm
 */
#ifndef activate_mm
static inline void activate_mm(struct mm_struct *prev_mm,
			       struct mm_struct *next_mm)
{
	switch_mm(prev_mm, next_mm, current);
}
#endif

/**
 * dectivate_mm - called when an mm is released after exit or exec switches away from it
 * @tsk: the task
 * @mm:  the old mm
 */
#ifndef deactivate_mm
static inline void deactivate_mm(struct task_struct *tsk,
			struct mm_struct *mm)
{
}
#endif

#endif /* __ASM_GENERIC_MMU_CONTEXT_H */
