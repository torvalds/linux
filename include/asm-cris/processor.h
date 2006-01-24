/*
 * include/asm-cris/processor.h
 *
 * Copyright (C) 2000, 2001 Axis Communications AB
 *
 * Authors:         Bjorn Wesen        Initial version
 *
 */

#ifndef __ASM_CRIS_PROCESSOR_H
#define __ASM_CRIS_PROCESSOR_H

#include <linux/config.h>
#include <asm/system.h>
#include <asm/page.h>
#include <asm/ptrace.h>
#include <asm/arch/processor.h>

struct task_struct;

/* This decides where the kernel will search for a free chunk of vm
 * space during mmap's.
 */
#define TASK_UNMAPPED_BASE      (PAGE_ALIGN(TASK_SIZE / 3))

/* THREAD_SIZE is the size of the task_struct/kernel_stack combo.
 * normally, the stack is found by doing something like p + THREAD_SIZE
 * in CRIS, a page is 8192 bytes, which seems like a sane size
 */

#define THREAD_SIZE       PAGE_SIZE
#define KERNEL_STACK_SIZE PAGE_SIZE

/*
 * At user->kernel entry, the pt_regs struct is stacked on the top of the kernel-stack.
 * This macro allows us to find those regs for a task.
 * Notice that subsequent pt_regs stackings, like recursive interrupts occurring while
 * we're in the kernel, won't affect this - only the first user->kernel transition
 * registers are reached by this.
 */

#define user_regs(thread_info) (((struct pt_regs *)((unsigned long)(thread_info) + THREAD_SIZE)) - 1)

/*
 * Dito but for the currently running task
 */

#define task_pt_regs(task) user_regs(task_thread_info(task))
#define current_regs() task_pt_regs(current)

static inline void prepare_to_copy(struct task_struct *tsk)
{
}

extern int kernel_thread(int (*fn)(void *), void * arg, unsigned long flags);

unsigned long get_wchan(struct task_struct *p);

#define KSTK_ESP(tsk)   ((tsk) == current ? rdusp() : (tsk)->thread.usp)

extern unsigned long thread_saved_pc(struct task_struct *tsk);

/* Free all resources held by a thread. */
static inline void release_thread(struct task_struct *dead_task)
{
        /* Nothing needs to be done.  */
}

#define init_stack      (init_thread_union.stack)

#define cpu_relax()     barrier()

#endif /* __ASM_CRIS_PROCESSOR_H */
