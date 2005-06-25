/*
 *  include/asm-s390/thread_info.h
 *
 *  S390 version
 *    Copyright (C) 2002 IBM Deutschland Entwicklung GmbH, IBM Corporation
 *    Author(s): Martin Schwidefsky (schwidefsky@de.ibm.com)
 */

#ifndef _ASM_THREAD_INFO_H
#define _ASM_THREAD_INFO_H

#ifdef __KERNEL__

/*
 * Size of kernel stack for each process
 */
#ifndef __s390x__
#ifndef __SMALL_STACK
#define THREAD_ORDER 1
#define ASYNC_ORDER  1
#else
#define THREAD_ORDER 0
#define ASYNC_ORDER  0
#endif
#else /* __s390x__ */
#ifndef __SMALL_STACK
#define THREAD_ORDER 2
#define ASYNC_ORDER  2
#else
#define THREAD_ORDER 1
#define ASYNC_ORDER  1
#endif
#endif /* __s390x__ */

#define THREAD_SIZE (PAGE_SIZE << THREAD_ORDER)
#define ASYNC_SIZE  (PAGE_SIZE << ASYNC_ORDER)

#ifndef __ASSEMBLY__
#include <asm/processor.h>
#include <asm/lowcore.h>

/*
 * low level task data that entry.S needs immediate access to
 * - this struct should fit entirely inside of one cache line
 * - this struct shares the supervisor stack pages
 * - if the contents of this structure are changed, the assembly constants must also be changed
 */
struct thread_info {
	struct task_struct	*task;		/* main task structure */
	struct exec_domain	*exec_domain;	/* execution domain */
	unsigned long		flags;		/* low level flags */
	unsigned int		cpu;		/* current CPU */
	int			preempt_count;	/* 0 => preemptable, <0 => BUG */
	struct restart_block	restart_block;
};

/*
 * macros/functions for gaining access to the thread information structure
 */
#define INIT_THREAD_INFO(tsk)			\
{						\
	.task		= &tsk,			\
	.exec_domain	= &default_exec_domain,	\
	.flags		= 0,			\
	.cpu		= 0,			\
	.restart_block	= {			\
		.fn = do_no_restart_syscall,	\
	},					\
}

#define init_thread_info	(init_thread_union.thread_info)
#define init_stack		(init_thread_union.stack)

/* how to get the thread information struct from C */
static inline struct thread_info *current_thread_info(void)
{
	return (struct thread_info *)((*(unsigned long *) __LC_KERNEL_STACK)-THREAD_SIZE);
}

/* thread information allocation */
#define alloc_thread_info(tsk) ((struct thread_info *) \
	__get_free_pages(GFP_KERNEL,THREAD_ORDER))
#define free_thread_info(ti) free_pages((unsigned long) (ti),THREAD_ORDER)
#define get_thread_info(ti) get_task_struct((ti)->task)
#define put_thread_info(ti) put_task_struct((ti)->task)

#endif

/*
 * thread information flags bit numbers
 */
#define TIF_SYSCALL_TRACE	0	/* syscall trace active */
#define TIF_NOTIFY_RESUME	1	/* resumption notification requested */
#define TIF_SIGPENDING		2	/* signal pending */
#define TIF_NEED_RESCHED	3	/* rescheduling necessary */
#define TIF_RESTART_SVC		4	/* restart svc with new svc number */
#define TIF_SYSCALL_AUDIT	5	/* syscall auditing active */
#define TIF_SINGLE_STEP		6	/* deliver sigtrap on return to user */
#define TIF_MCCK_PENDING	7	/* machine check handling is pending */
#define TIF_USEDFPU		16	/* FPU was used by this task this quantum (SMP) */
#define TIF_POLLING_NRFLAG	17	/* true if poll_idle() is polling 
					   TIF_NEED_RESCHED */
#define TIF_31BIT		18	/* 32bit process */ 
#define TIF_MEMDIE		19

#define _TIF_SYSCALL_TRACE	(1<<TIF_SYSCALL_TRACE)
#define _TIF_NOTIFY_RESUME	(1<<TIF_NOTIFY_RESUME)
#define _TIF_SIGPENDING		(1<<TIF_SIGPENDING)
#define _TIF_NEED_RESCHED	(1<<TIF_NEED_RESCHED)
#define _TIF_RESTART_SVC	(1<<TIF_RESTART_SVC)
#define _TIF_SYSCALL_AUDIT	(1<<TIF_SYSCALL_AUDIT)
#define _TIF_SINGLE_STEP	(1<<TIF_SINGLE_STEP)
#define _TIF_MCCK_PENDING	(1<<TIF_MCCK_PENDING)
#define _TIF_USEDFPU		(1<<TIF_USEDFPU)
#define _TIF_POLLING_NRFLAG	(1<<TIF_POLLING_NRFLAG)
#define _TIF_31BIT		(1<<TIF_31BIT)

#endif /* __KERNEL__ */

#define PREEMPT_ACTIVE		0x4000000

#endif /* _ASM_THREAD_INFO_H */
