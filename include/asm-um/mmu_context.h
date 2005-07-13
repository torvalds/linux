/* 
 * Copyright (C) 2002 Jeff Dike (jdike@karaya.com)
 * Licensed under the GPL
 */

#ifndef __UM_MMU_CONTEXT_H
#define __UM_MMU_CONTEXT_H

#include "linux/sched.h"
#include "linux/config.h"
#include "choose-mode.h"
#include "um_mmu.h"

#define get_mmu_context(task) do ; while(0)
#define activate_context(tsk) do ; while(0)

#define deactivate_mm(tsk,mm)	do { } while (0)

extern void force_flush_all(void);

static inline void activate_mm(struct mm_struct *old, struct mm_struct *new)
{
	if (old != new)
		force_flush_all();
}

static inline void switch_mm(struct mm_struct *prev, struct mm_struct *next, 
			     struct task_struct *tsk)
{
	unsigned cpu = smp_processor_id();

	if(prev != next){
		cpu_clear(cpu, prev->cpu_vm_mask);
		cpu_set(cpu, next->cpu_vm_mask);
		if(next != &init_mm)
			CHOOSE_MODE((void) 0, 
				    switch_mm_skas(&next->context.skas.id));
	}
}

static inline void enter_lazy_tlb(struct mm_struct *mm, 
				  struct task_struct *tsk)
{
}

extern int init_new_context_skas(struct task_struct *task, 
				 struct mm_struct *mm);

static inline int init_new_context_tt(struct task_struct *task, 
				      struct mm_struct *mm)
{
	return(0);
}

static inline int init_new_context(struct task_struct *task, 
				   struct mm_struct *mm)
{
	return(CHOOSE_MODE_PROC(init_new_context_tt, init_new_context_skas, 
				task, mm));
}

extern void destroy_context_skas(struct mm_struct *mm);

static inline void destroy_context(struct mm_struct *mm)
{
	CHOOSE_MODE((void) 0, destroy_context_skas(mm));
}

#endif

/*
 * Overrides for Emacs so that we follow Linus's tabbing style.
 * Emacs will notice this stuff at the end of the file and automatically
 * adjust the settings for this buffer only.  This must remain at the end
 * of the file.
 * ---------------------------------------------------------------------------
 * Local variables:
 * c-file-style: "linux"
 * End:
 */
