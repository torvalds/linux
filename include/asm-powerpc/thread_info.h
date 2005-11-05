/* thread_info.h: PowerPC low-level thread information
 * adapted from the i386 version by Paul Mackerras
 *
 * Copyright (C) 2002  David Howells (dhowells@redhat.com)
 * - Incorporating suggestions made by Linus Torvalds and Dave Miller
 */

#ifndef _ASM_POWERPC_THREAD_INFO_H
#define _ASM_POWERPC_THREAD_INFO_H

#ifdef __KERNEL__

/* We have 8k stacks on ppc32 and 16k on ppc64 */

#ifdef CONFIG_PPC64
#define THREAD_SHIFT		14
#else
#define THREAD_SHIFT		13
#endif

#define THREAD_SIZE		(1 << THREAD_SHIFT)

#ifndef __ASSEMBLY__
#include <linux/config.h>
#include <linux/cache.h>
#include <asm/processor.h>
#include <asm/page.h>
#include <linux/stringify.h>

/*
 * low level task data.
 */
struct thread_info {
	struct task_struct *task;		/* main task structure */
	struct exec_domain *exec_domain;	/* execution domain */
	int		cpu;			/* cpu we're on */
	int		preempt_count;		/* 0 => preemptable,
						   <0 => BUG */
	struct restart_block restart_block;
	/* set by force_successful_syscall_return */
	unsigned char	syscall_noerror;
	/* low level flags - has atomic operations done on it */
	unsigned long	flags ____cacheline_aligned_in_smp;
};

/*
 * macros/functions for gaining access to the thread information structure
 *
 * preempt_count needs to be 1 initially, until the scheduler is functional.
 */
#define INIT_THREAD_INFO(tsk)			\
{						\
	.task =		&tsk,			\
	.exec_domain =	&default_exec_domain,	\
	.cpu =		0,			\
	.preempt_count = 1,			\
	.restart_block = {			\
		.fn = do_no_restart_syscall,	\
	},					\
	.flags =	0,			\
}

#define init_thread_info	(init_thread_union.thread_info)
#define init_stack		(init_thread_union.stack)

/* thread information allocation */

#ifdef CONFIG_DEBUG_STACK_USAGE
#define THREAD_INFO_GFP		GFP_KERNEL | __GFP_ZERO
#else
#define THREAD_INFO_GFP		GFP_KERNEL
#endif

#if THREAD_SHIFT >= PAGE_SHIFT

#define THREAD_ORDER	(THREAD_SHIFT - PAGE_SHIFT)

#define alloc_thread_info(tsk)	\
	((struct thread_info *)__get_free_pages(THREAD_INFO_GFP, THREAD_ORDER))
#define free_thread_info(ti)	free_pages((unsigned long)ti, THREAD_ORDER)

#else /* THREAD_SHIFT < PAGE_SHIFT */

#define alloc_thread_info(tsk)	kmalloc(THREAD_SIZE, THREAD_INFO_GFP)
#define free_thread_info(ti)	kfree(ti)

#endif /* THREAD_SHIFT < PAGE_SHIFT */

#define get_thread_info(ti)	get_task_struct((ti)->task)
#define put_thread_info(ti)	put_task_struct((ti)->task)

/* how to get the thread information struct from C */
static inline struct thread_info *current_thread_info(void)
{
	register unsigned long sp asm("r1");

	/* gcc4, at least, is smart enough to turn this into a single
	 * rlwinm for ppc32 and clrrdi for ppc64 */
	return (struct thread_info *)(sp & ~(THREAD_SIZE-1));
}

#endif /* __ASSEMBLY__ */

#define PREEMPT_ACTIVE		0x10000000

/*
 * thread information flag bit numbers
 */
#define TIF_SYSCALL_TRACE	0	/* syscall trace active */
#define TIF_NOTIFY_RESUME	1	/* resumption notification requested */
#define TIF_SIGPENDING		2	/* signal pending */
#define TIF_NEED_RESCHED	3	/* rescheduling necessary */
#define TIF_POLLING_NRFLAG	4	/* true if poll_idle() is polling
					   TIF_NEED_RESCHED */
#define TIF_32BIT		5	/* 32 bit binary */
/* #define SPARE		6 */
#define TIF_ABI_PENDING		7	/* 32/64 bit switch needed */
#define TIF_SYSCALL_AUDIT	8	/* syscall auditing active */
#define TIF_SINGLESTEP		9	/* singlestepping active */
#define TIF_MEMDIE		10
#define TIF_SECCOMP		11	/* secure computing */

/* as above, but as bit values */
#define _TIF_SYSCALL_TRACE	(1<<TIF_SYSCALL_TRACE)
#define _TIF_NOTIFY_RESUME	(1<<TIF_NOTIFY_RESUME)
#define _TIF_SIGPENDING		(1<<TIF_SIGPENDING)
#define _TIF_NEED_RESCHED	(1<<TIF_NEED_RESCHED)
#define _TIF_POLLING_NRFLAG	(1<<TIF_POLLING_NRFLAG)
#define _TIF_32BIT		(1<<TIF_32BIT)
/* #define _SPARE		(1<<SPARE) */
#define _TIF_ABI_PENDING	(1<<TIF_ABI_PENDING)
#define _TIF_SYSCALL_AUDIT	(1<<TIF_SYSCALL_AUDIT)
#define _TIF_SINGLESTEP		(1<<TIF_SINGLESTEP)
#define _TIF_SECCOMP		(1<<TIF_SECCOMP)
#define _TIF_SYSCALL_T_OR_A	(_TIF_SYSCALL_TRACE|_TIF_SYSCALL_AUDIT|_TIF_SECCOMP)

#define _TIF_USER_WORK_MASK	(_TIF_NOTIFY_RESUME | _TIF_SIGPENDING | \
				 _TIF_NEED_RESCHED)

#endif /* __KERNEL__ */

#endif /* _ASM_POWERPC_THREAD_INFO_H */
