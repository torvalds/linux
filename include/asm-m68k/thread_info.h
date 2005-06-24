#ifndef _ASM_M68K_THREAD_INFO_H
#define _ASM_M68K_THREAD_INFO_H

#include <asm/types.h>
#include <asm/processor.h>
#include <asm/page.h>

struct thread_info {
	struct task_struct	*task;		/* main task structure */
	struct exec_domain	*exec_domain;	/* execution domain */
	int			preempt_count;	/* 0 => preemptable, <0 => BUG */
	__u32 cpu; /* should always be 0 on m68k */
	struct restart_block    restart_block;

	__u8			supervisor_stack[0];
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

//#define init_thread_info	(init_task.thread.info)
#define init_stack		(init_thread_union.stack)

#define current_thread_info()	(current->thread_info)


#define __HAVE_THREAD_FUNCTIONS

#define TIF_SYSCALL_TRACE	0	/* syscall trace active */
#define TIF_DELAYED_TRACE	1	/* single step a syscall */
#define TIF_NOTIFY_RESUME	2	/* resumption notification requested */
#define TIF_SIGPENDING		3	/* signal pending */
#define TIF_NEED_RESCHED	4	/* rescheduling necessary */
#define TIF_MEMDIE		5

extern int thread_flag_fixme(void);

/*
 * flag set/clear/test wrappers
 * - pass TIF_xxxx constants to these functions
 */

#define __set_tsk_thread_flag(tsk, flag, val) ({	\
	switch (flag) {					\
	case TIF_SIGPENDING:				\
		tsk->thread.work.sigpending = val;	\
		break;					\
	case TIF_NEED_RESCHED:				\
		tsk->thread.work.need_resched = val;	\
		break;					\
	case TIF_SYSCALL_TRACE:				\
		tsk->thread.work.syscall_trace = val;	\
		break;					\
	case TIF_MEMDIE:				\
		tsk->thread.work.memdie = val;		\
		break;					\
	default:					\
		thread_flag_fixme();			\
	}						\
})

#define __get_tsk_thread_flag(tsk, flag) ({		\
	int ___res;					\
	switch (flag) {					\
	case TIF_SIGPENDING:				\
		___res = tsk->thread.work.sigpending;	\
		break;					\
	case TIF_NEED_RESCHED:				\
		___res = tsk->thread.work.need_resched;	\
		break;					\
	case TIF_SYSCALL_TRACE:				\
		___res = tsk->thread.work.syscall_trace;\
		break;					\
	case TIF_MEMDIE:				\
		___res = tsk->thread.work.memdie;\
		break;					\
	default:					\
		___res = thread_flag_fixme();		\
	}						\
	___res;						\
})

#define __get_set_tsk_thread_flag(tsk, flag, val) ({	\
	int __res = __get_tsk_thread_flag(tsk, flag);	\
	__set_tsk_thread_flag(tsk, flag, val);		\
	__res;						\
})

#define set_tsk_thread_flag(tsk, flag) __set_tsk_thread_flag(tsk, flag, ~0)
#define clear_tsk_thread_flag(tsk, flag) __set_tsk_thread_flag(tsk, flag, 0)
#define test_and_set_tsk_thread_flag(tsk, flag) __get_set_tsk_thread_flag(tsk, flag, ~0)
#define test_tsk_thread_flag(tsk, flag) __get_tsk_thread_flag(tsk, flag)

#define set_thread_flag(flag) set_tsk_thread_flag(current, flag)
#define clear_thread_flag(flag) clear_tsk_thread_flag(current, flag)
#define test_thread_flag(flag) test_tsk_thread_flag(current, flag)

#define set_need_resched() set_thread_flag(TIF_NEED_RESCHED)
#define clear_need_resched() clear_thread_flag(TIF_NEED_RESCHED)

#endif	/* _ASM_M68K_THREAD_INFO_H */
