/* 
 * Copyright (C) 2002 - 2007 Jeff Dike (jdike@{addtoit,linux.intel}.com)
 * Licensed under the GPL
 */

#ifndef __UM_MMU_CONTEXT_H
#define __UM_MMU_CONTEXT_H

#include <asm-generic/mm_hooks.h>

#include "linux/sched.h"
#include "um_mmu.h"

#define get_mmu_context(task) do ; while(0)
#define activate_context(tsk) do ; while(0)

#define deactivate_mm(tsk,mm)	do { } while (0)

extern void force_flush_all(void);

static inline void activate_mm(struct mm_struct *old, struct mm_struct *new)
{
	/*
	 * This is called by fs/exec.c and fs/aio.c. In the first case, for an
	 * exec, we don't need to do anything as we're called from userspace
	 * and thus going to use a new host PID. In the second, we're called
	 * from a kernel thread, and thus need to go doing the mmap's on the
	 * host. Since they're very expensive, we want to avoid that as far as
	 * possible.
	 */
	if (old != new && (current->flags & PF_BORROWED_MM))
		__switch_mm(&new->context.id);
}

static inline void switch_mm(struct mm_struct *prev, struct mm_struct *next, 
			     struct task_struct *tsk)
{
	unsigned cpu = smp_processor_id();

	if(prev != next){
		cpu_clear(cpu, prev->cpu_vm_mask);
		cpu_set(cpu, next->cpu_vm_mask);
		if(next != &init_mm)
			__switch_mm(&next->context.id);
	}
}

static inline void enter_lazy_tlb(struct mm_struct *mm, 
				  struct task_struct *tsk)
{
}

extern int init_new_context(struct task_struct *task, struct mm_struct *mm);

extern void destroy_context(struct mm_struct *mm);

#endif
