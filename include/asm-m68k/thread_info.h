#ifndef _ASM_M68K_THREAD_INFO_H
#define _ASM_M68K_THREAD_INFO_H

#include <asm/types.h>
#include <asm/page.h>

struct thread_info {
	struct task_struct	*task;		/* main task structure */
	unsigned long		flags;
	struct exec_domain	*exec_domain;	/* execution domain */
	int			preempt_count;	/* 0 => preemptable, <0 => BUG */
	__u32 cpu; /* should always be 0 on m68k */
	struct restart_block    restart_block;
};

#define PREEMPT_ACTIVE		0x4000000

#define INIT_THREAD_INFO(tsk)			\
{						\
	.task		= &tsk,			\
	.exec_domain	= &default_exec_domain,	\
	.restart_block = {			\
		.fn = do_no_restart_syscall,	\
	},					\
}

/* THREAD_SIZE should be 8k, so handle differently for 4k and 8k machines */
#if PAGE_SHIFT == 13 /* 8k machines */
#define alloc_thread_info(tsk)   ((struct thread_info *)__get_free_pages(GFP_KERNEL,0))
#define free_thread_info(ti)  free_pages((unsigned long)(ti),0)
#else /* otherwise assume 4k pages */
#define alloc_thread_info(tsk)   ((struct thread_info *)__get_free_pages(GFP_KERNEL,1))
#define free_thread_info(ti)  free_pages((unsigned long)(ti),1)
#endif /* PAGE_SHIFT == 13 */

#define init_thread_info	(init_task.thread.info)
#define init_stack		(init_thread_union.stack)

#define task_thread_info(tsk)	(&(tsk)->thread.info)
#define task_stack_page(tsk)	((void *)(tsk)->thread_info)
#define current_thread_info()	task_thread_info(current)

#define __HAVE_THREAD_FUNCTIONS

#define setup_thread_stack(p, org) ({			\
	*(struct task_struct **)(p)->thread_info = (p);	\
	task_thread_info(p)->task = (p);		\
})

#define end_of_stack(p) ((unsigned long *)(p)->thread_info + 1)

/* entry.S relies on these definitions!
 * bits 0-7 are tested at every exception exit
 * bits 8-15 are also tested at syscall exit
 */
#define TIF_SIGPENDING		6	/* signal pending */
#define TIF_NEED_RESCHED	7	/* rescheduling necessary */
#define TIF_DELAYED_TRACE	14	/* single step a syscall */
#define TIF_SYSCALL_TRACE	15	/* syscall trace active */
#define TIF_MEMDIE		16

#endif	/* _ASM_M68K_THREAD_INFO_H */
