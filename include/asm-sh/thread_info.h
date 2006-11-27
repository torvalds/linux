#ifndef __ASM_SH_THREAD_INFO_H
#define __ASM_SH_THREAD_INFO_H

/* SuperH version
 * Copyright (C) 2002  Niibe Yutaka
 *
 * The copyright of original i386 version is:
 *
 *  Copyright (C) 2002  David Howells (dhowells@redhat.com)
 *  - Incorporating suggestions made by Linus Torvalds and Dave Miller
 */
#ifdef __KERNEL__
#include <asm/page.h>

#ifndef __ASSEMBLY__
#include <asm/processor.h>

struct thread_info {
	struct task_struct	*task;		/* main task structure */
	struct exec_domain	*exec_domain;	/* execution domain */
	unsigned long		flags;		/* low level flags */
	__u32			cpu;
	int			preempt_count; /* 0 => preemptable, <0 => BUG */
	mm_segment_t		addr_limit;	/* thread address space */
	struct restart_block	restart_block;
	unsigned long		previous_sp;	/* sp of previous stack in case
						   of nested IRQ stacks */
	__u8			supervisor_stack[0];
};

#endif

#define PREEMPT_ACTIVE		0x10000000

#ifdef CONFIG_4KSTACKS
#define THREAD_SIZE		(PAGE_SIZE)
#else
#define THREAD_SIZE		(PAGE_SIZE * 2)
#endif
#define STACK_WARN		(THREAD_SIZE / 8)

/*
 * macros/functions for gaining access to the thread information structure
 */
#ifndef __ASSEMBLY__
#define INIT_THREAD_INFO(tsk)			\
{						\
	.task		= &tsk,			\
	.exec_domain	= &default_exec_domain,	\
	.flags		= 0,			\
	.cpu		= 0,			\
	.preempt_count	= 1,			\
	.addr_limit	= KERNEL_DS,		\
	.restart_block	= {			\
		.fn = do_no_restart_syscall,	\
	},					\
}

#define init_thread_info	(init_thread_union.thread_info)
#define init_stack		(init_thread_union.stack)

/* how to get the current stack pointer from C */
register unsigned long current_stack_pointer asm("r15") __attribute_used__;

/* how to get the thread information struct from C */
static inline struct thread_info *current_thread_info(void)
{
	struct thread_info *ti;
#ifdef CONFIG_CPU_HAS_SR_RB
	__asm__("stc	r7_bank, %0" : "=r" (ti));
#else
	unsigned long __dummy;

	__asm__ __volatile__ (
		"mov	r15, %0\n\t"
		"and	%1, %0\n\t"
		: "=&r" (ti), "=r" (__dummy)
		: "1" (~(THREAD_SIZE - 1))
		: "memory");
#endif

	return ti;
}

/* thread information allocation */
#ifdef CONFIG_DEBUG_STACK_USAGE
#define alloc_thread_info(ti)	kzalloc(THREAD_SIZE, GFP_KERNEL)
#else
#define alloc_thread_info(ti)	kmalloc(THREAD_SIZE, GFP_KERNEL)
#endif
#define free_thread_info(ti)	kfree(ti)

#endif /* __ASSEMBLY__ */

/*
 * thread information flags
 * - these are process state flags that various assembly files may need to access
 * - pending work-to-be-done flags are in LSW
 * - other flags in MSW
 */
#define TIF_SYSCALL_TRACE	0	/* syscall trace active */
#define TIF_NOTIFY_RESUME	1	/* resumption notification requested */
#define TIF_SIGPENDING		2	/* signal pending */
#define TIF_NEED_RESCHED	3	/* rescheduling necessary */
#define TIF_RESTORE_SIGMASK	4	/* restore signal mask in do_signal() */
#define TIF_USEDFPU		16	/* FPU was used by this task this quantum (SMP) */
#define TIF_POLLING_NRFLAG	17	/* true if poll_idle() is polling TIF_NEED_RESCHED */
#define TIF_MEMDIE		18

#define _TIF_SYSCALL_TRACE	(1<<TIF_SYSCALL_TRACE)
#define _TIF_NOTIFY_RESUME	(1<<TIF_NOTIFY_RESUME)
#define _TIF_SIGPENDING		(1<<TIF_SIGPENDING)
#define _TIF_NEED_RESCHED	(1<<TIF_NEED_RESCHED)
#define _TIF_RESTORE_SIGMASK	(1<<TIF_RESTORE_SIGMASK)
#define _TIF_USEDFPU		(1<<TIF_USEDFPU)
#define _TIF_POLLING_NRFLAG	(1<<TIF_POLLING_NRFLAG)

#define _TIF_WORK_MASK		0x000000FE	/* work to do on interrupt/exception return */
#define _TIF_ALLWORK_MASK	0x000000FF	/* work to do on any return to u-space */

#endif /* __KERNEL__ */

#endif /* __ASM_SH_THREAD_INFO_H */
