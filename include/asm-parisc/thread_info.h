#ifndef _ASM_PARISC_THREAD_INFO_H
#define _ASM_PARISC_THREAD_INFO_H

#ifdef __KERNEL__

#ifndef __ASSEMBLY__
#include <asm/processor.h>

struct thread_info {
	struct task_struct *task;	/* main task structure */
	struct exec_domain *exec_domain;/* execution domain */
	unsigned long flags;		/* thread_info flags (see TIF_*) */
	mm_segment_t addr_limit;	/* user-level address space limit */
	__u32 cpu;			/* current CPU */
	int preempt_count;		/* 0=premptable, <0=BUG; will also serve as bh-counter */
	struct restart_block restart_block;
};

#define INIT_THREAD_INFO(tsk)			\
{						\
	.task		= &tsk,			\
	.exec_domain	= &default_exec_domain,	\
	.flags		= 0,			\
	.cpu		= 0,			\
	.addr_limit	= KERNEL_DS,		\
	.preempt_count	= 1,			\
  	.restart_block	= {			\
		.fn = do_no_restart_syscall	\
	}					\
}

#define init_thread_info        (init_thread_union.thread_info)
#define init_stack              (init_thread_union.stack)

/* thread information allocation */

#define THREAD_ORDER            2
/* Be sure to hunt all references to this down when you change the size of
 * the kernel stack */
#define THREAD_SIZE             (PAGE_SIZE << THREAD_ORDER)
#define THREAD_SHIFT            (PAGE_SHIFT + THREAD_ORDER)

#define alloc_thread_info(tsk) ((struct thread_info *) \
			__get_free_pages(GFP_KERNEL, THREAD_ORDER))
#define free_thread_info(ti)    free_pages((unsigned long) (ti), THREAD_ORDER)

/* how to get the thread information struct from C */
#define current_thread_info()	((struct thread_info *)mfctl(30))

#endif /* !__ASSEMBLY */

#define PREEMPT_ACTIVE_BIT	28
#define PREEMPT_ACTIVE		(1 << PREEMPT_ACTIVE_BIT)

/*
 * thread information flags
 */
#define TIF_SYSCALL_TRACE	0	/* syscall trace active */
#define TIF_SIGPENDING		1	/* signal pending */
#define TIF_NEED_RESCHED	2	/* rescheduling necessary */
#define TIF_POLLING_NRFLAG	3	/* true if poll_idle() is polling TIF_NEED_RESCHED */
#define TIF_32BIT               4       /* 32 bit binary */
#define TIF_MEMDIE		5
#define TIF_RESTORE_SIGMASK	6	/* restore saved signal mask */

#define _TIF_SYSCALL_TRACE	(1 << TIF_SYSCALL_TRACE)
#define _TIF_SIGPENDING		(1 << TIF_SIGPENDING)
#define _TIF_NEED_RESCHED	(1 << TIF_NEED_RESCHED)
#define _TIF_POLLING_NRFLAG	(1 << TIF_POLLING_NRFLAG)
#define _TIF_32BIT		(1 << TIF_32BIT)
#define _TIF_RESTORE_SIGMASK	(1 << TIF_RESTORE_SIGMASK)

#define _TIF_USER_WORK_MASK     (_TIF_SIGPENDING | \
                                 _TIF_NEED_RESCHED | _TIF_RESTORE_SIGMASK)

#endif /* __KERNEL__ */

#endif /* _ASM_PARISC_THREAD_INFO_H */
