/* thread_info.h: PPC low-level thread information
 * adapted from the i386 version by Paul Mackerras
 *
 * Copyright (C) 2002  David Howells (dhowells@redhat.com)
 * - Incorporating suggestions made by Linus Torvalds and Dave Miller
 */

#ifndef _ASM_THREAD_INFO_H
#define _ASM_THREAD_INFO_H

#ifdef __KERNEL__
#ifndef __ASSEMBLY__
/*
 * low level task data.
 * If you change this, change the TI_* offsets below to match.
 */
struct thread_info {
	struct task_struct	*task;		/* main task structure */
	struct exec_domain	*exec_domain;	/* execution domain */
	unsigned long		flags;		/* low level flags */
	unsigned long		local_flags;	/* non-racy flags */
	int			cpu;		/* cpu we're on */
	int			preempt_count;
	struct restart_block	restart_block;
};

#define INIT_THREAD_INFO(tsk)			\
{						\
	.task =		&tsk,			\
	.exec_domain =	&default_exec_domain,	\
	.flags =	0,			\
	.local_flags =  0,			\
	.cpu =		0,			\
	.preempt_count = 1,			\
	.restart_block = {			\
		.fn = do_no_restart_syscall,	\
	},					\
}

#define init_thread_info	(init_thread_union.thread_info)
#define init_stack		(init_thread_union.stack)

/*
 * macros/functions for gaining access to the thread information structure
 */

/* how to get the thread information struct from C */
static inline struct thread_info *current_thread_info(void)
{
	struct thread_info *ti;
	__asm__("rlwinm %0,1,0,0,18" : "=r"(ti));
	return ti;
}

/* thread information allocation */
#define alloc_thread_info(tsk) ((struct thread_info *) \
				__get_free_pages(GFP_KERNEL, 1))
#define free_thread_info(ti)	free_pages((unsigned long) (ti), 1)
#define get_thread_info(ti)	get_task_struct((ti)->task)
#define put_thread_info(ti)	put_task_struct((ti)->task)
#endif /* __ASSEMBLY__ */

/*
 * Size of kernel stack for each process.
 */
#define THREAD_SIZE		8192	/* 2 pages */

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
#define TIF_MEMDIE		5
/* as above, but as bit values */
#define _TIF_SYSCALL_TRACE	(1<<TIF_SYSCALL_TRACE)
#define _TIF_NOTIFY_RESUME	(1<<TIF_NOTIFY_RESUME)
#define _TIF_SIGPENDING		(1<<TIF_SIGPENDING)
#define _TIF_NEED_RESCHED	(1<<TIF_NEED_RESCHED)
#define _TIF_POLLING_NRFLAG	(1<<TIF_POLLING_NRFLAG)

/*
 * Non racy (local) flags bit numbers
 */
#define TIFL_FORCE_NOERROR	0	/* don't return error from current
					   syscall even if result < 0 */

/* as above, but as bit values */
#define _TIFL_FORCE_NOERROR	(1<<TIFL_FORCE_NOERROR)


#endif /* __KERNEL__ */

#endif /* _ASM_THREAD_INFO_H */
