/*
 *  linux/include/asm-arm/mmu_context.h
 *
 *  Copyright (C) 1996 Russell King.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 *  Changelog:
 *   27-06-1996	RMK	Created
 */
#ifndef __ASM_ARM_MMU_CONTEXT_H
#define __ASM_ARM_MMU_CONTEXT_H

#include <linux/compiler.h>
#include <asm/cacheflush.h>
#include <asm/proc-fns.h>

#if __LINUX_ARM_ARCH__ >= 6

/*
 * On ARMv6, we have the following structure in the Context ID:
 *
 * 31                         7          0
 * +-------------------------+-----------+
 * |      process ID         |   ASID    |
 * +-------------------------+-----------+
 * |              context ID             |
 * +-------------------------------------+
 *
 * The ASID is used to tag entries in the CPU caches and TLBs.
 * The context ID is used by debuggers and trace logic, and
 * should be unique within all running processes.
 */
#define ASID_BITS	8
#define ASID_MASK	((~0) << ASID_BITS)

extern unsigned int cpu_last_asid;

void __init_new_context(struct task_struct *tsk, struct mm_struct *mm);
void __new_context(struct mm_struct *mm);

static inline void check_context(struct mm_struct *mm)
{
	if (unlikely((mm->context.id ^ cpu_last_asid) >> ASID_BITS))
		__new_context(mm);
}

#define init_new_context(tsk,mm)	(__init_new_context(tsk,mm),0)

#else

#define check_context(mm)		do { } while (0)
#define init_new_context(tsk,mm)	0

#endif

#define destroy_context(mm)		do { } while(0)

/*
 * This is called when "tsk" is about to enter lazy TLB mode.
 *
 * mm:  describes the currently active mm context
 * tsk: task which is entering lazy tlb
 * cpu: cpu number which is entering lazy tlb
 *
 * tsk->mm will be NULL
 */
static inline void
enter_lazy_tlb(struct mm_struct *mm, struct task_struct *tsk)
{
}

/*
 * This is the actual mm switch as far as the scheduler
 * is concerned.  No registers are touched.  We avoid
 * calling the CPU specific function when the mm hasn't
 * actually changed.
 */
static inline void
switch_mm(struct mm_struct *prev, struct mm_struct *next,
	  struct task_struct *tsk)
{
	unsigned int cpu = smp_processor_id();

	if (prev != next) {
		cpu_set(cpu, next->cpu_vm_mask);
		check_context(next);
		cpu_switch_mm(next->pgd, next);
		if (cache_is_vivt())
			cpu_clear(cpu, prev->cpu_vm_mask);
	}
}

#define deactivate_mm(tsk,mm)	do { } while (0)
#define activate_mm(prev,next)	switch_mm(prev, next, NULL)

#endif
