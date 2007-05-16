#ifndef __ASM_SH64_THREAD_INFO_H
#define __ASM_SH64_THREAD_INFO_H

/*
 * SuperH 5 version
 * Copyright (C) 2003  Paul Mundt
 */

#ifdef __KERNEL__

#ifndef __ASSEMBLY__
#include <asm/registers.h>

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
	/* Put the 4 32-bit fields together to make asm offsetting easier. */
	int			preempt_count;	/* 0 => preemptable, <0 => BUG */
	__u16			cpu;

	mm_segment_t		addr_limit;
	struct restart_block	restart_block;

	__u8			supervisor_stack[0];
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
	.preempt_count	= 1,			\
	.addr_limit     = KERNEL_DS,            \
	.restart_block	= {			\
		.fn = do_no_restart_syscall,	\
	},					\
}

#define init_thread_info	(init_thread_union.thread_info)
#define init_stack		(init_thread_union.stack)

/* how to get the thread information struct from C */
static inline struct thread_info *current_thread_info(void)
{
	struct thread_info *ti;

	__asm__ __volatile__ ("getcon " __KCR0 ", %0\n\t" : "=r" (ti));

	return ti;
}

/* thread information allocation */



#define alloc_thread_info(ti) ((struct thread_info *) __get_free_pages(GFP_KERNEL,1))
#define free_thread_info(ti) free_pages((unsigned long) (ti), 1)

#endif /* __ASSEMBLY__ */

#define THREAD_SIZE  8192

#define PREEMPT_ACTIVE		0x10000000

/* thread information flags */
#define TIF_SYSCALL_TRACE	0	/* syscall trace active */
#define TIF_SIGPENDING		2	/* signal pending */
#define TIF_NEED_RESCHED	3	/* rescheduling necessary */
#define TIF_MEMDIE		4
#define TIF_RESTORE_SIGMASK	5	/* Restore signal mask in do_signal */

#define _TIF_SYSCALL_TRACE	(1 << TIF_SYSCALL_TRACE)
#define _TIF_SIGPENDING		(1 << TIF_SIGPENDING)
#define _TIF_NEED_RESCHED	(1 << TIF_NEED_RESCHED)
#define _TIF_MEMDIE		(1 << TIF_MEMDIE)
#define _TIF_RESTORE_SIGMASK	(1 << TIF_RESTORE_SIGMASK)

#endif /* __KERNEL__ */

#endif /* __ASM_SH64_THREAD_INFO_H */
