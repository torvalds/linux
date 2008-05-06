/* thread_info.h: x86_64 low-level thread information
 *
 * Copyright (C) 2002  David Howells (dhowells@redhat.com)
 * - Incorporating suggestions made by Linus Torvalds and Dave Miller
 */

#ifndef _ASM_THREAD_INFO_H
#define _ASM_THREAD_INFO_H

#ifdef __KERNEL__

#include <asm/page.h>
#include <asm/types.h>
#include <asm/pda.h>

/*
 * low level task data that entry.S needs immediate access to
 * - this struct should fit entirely inside of one cache line
 * - this struct shares the supervisor stack pages
 */
#ifndef __ASSEMBLY__
struct task_struct;
struct exec_domain;
#include <asm/processor.h>

struct thread_info {
	struct task_struct	*task;		/* main task structure */
	struct exec_domain	*exec_domain;	/* execution domain */
	__u32			flags;		/* low level flags */
	__u32			status;		/* thread synchronous flags */
	__u32			cpu;		/* current CPU */
	int 			preempt_count;	/* 0 => preemptable,
						   <0 => BUG */
	mm_segment_t		addr_limit;
	struct restart_block    restart_block;
#ifdef CONFIG_IA32_EMULATION
	void __user		*sysenter_return;
#endif
};
#endif

/*
 * macros/functions for gaining access to the thread information structure
 * preempt_count needs to be 1 initially, until the scheduler is functional.
 */
#ifndef __ASSEMBLY__
#define INIT_THREAD_INFO(tsk)			\
{						\
	.task	       = &tsk,			\
	.exec_domain   = &default_exec_domain,	\
	.flags	       = 0,			\
	.cpu	       = 0,			\
	.preempt_count = 1,			\
	.addr_limit     = KERNEL_DS,		\
	.restart_block = {			\
		.fn = do_no_restart_syscall,	\
	},					\
}

#define init_thread_info	(init_thread_union.thread_info)
#define init_stack		(init_thread_union.stack)

static inline struct thread_info *current_thread_info(void)
{
	struct thread_info *ti;
	ti = (void *)(read_pda(kernelstack) + PDA_STACKOFFSET - THREAD_SIZE);
	return ti;
}

/* do not use in interrupt context */
static inline struct thread_info *stack_thread_info(void)
{
	struct thread_info *ti;
	asm("andq %%rsp,%0; " : "=r" (ti) : "0" (~(THREAD_SIZE - 1)));
	return ti;
}

/* thread information allocation */
#ifdef CONFIG_DEBUG_STACK_USAGE
#define THREAD_FLAGS (GFP_KERNEL | __GFP_ZERO)
#else
#define THREAD_FLAGS GFP_KERNEL
#endif

#define alloc_thread_info(tsk)						\
	((struct thread_info *)__get_free_pages(THREAD_FLAGS, THREAD_ORDER))

#else /* !__ASSEMBLY__ */

/* how to get the thread information struct from ASM */
#define GET_THREAD_INFO(reg) \
	movq %gs:pda_kernelstack,reg ; \
	subq $(THREAD_SIZE-PDA_STACKOFFSET),reg

#endif

/*
 * thread information flags
 * - these are process state flags that various assembly files
 *   may need to access
 * - pending work-to-be-done flags are in LSW
 * - other flags in MSW
 * Warning: layout of LSW is hardcoded in entry.S
 */
#define TIF_SYSCALL_TRACE	0	/* syscall trace active */
#define TIF_SIGPENDING		2	/* signal pending */
#define TIF_NEED_RESCHED	3	/* rescheduling necessary */
#define TIF_SINGLESTEP		4	/* reenable singlestep on user return*/
#define TIF_IRET		5	/* force IRET */
#define TIF_SYSCALL_AUDIT	7	/* syscall auditing active */
#define TIF_SECCOMP		8	/* secure computing */
#define TIF_MCE_NOTIFY		10	/* notify userspace of an MCE */
#define TIF_HRTICK_RESCHED	11	/* reprogram hrtick timer */
/* 16 free */
#define TIF_IA32		17	/* 32bit process */
#define TIF_FORK		18	/* ret_from_fork */
#define TIF_ABI_PENDING		19
#define TIF_MEMDIE		20
#define TIF_DEBUG		21	/* uses debug registers */
#define TIF_IO_BITMAP		22	/* uses I/O bitmap */
#define TIF_FREEZE		23	/* is freezing for suspend */
#define TIF_FORCED_TF		24	/* true if TF in eflags artificially */
#define TIF_DEBUGCTLMSR		25	/* uses thread_struct.debugctlmsr */
#define TIF_DS_AREA_MSR		26      /* uses thread_struct.ds_area_msr */
#define TIF_BTS_TRACE_TS	27      /* record scheduling event timestamps */
#define TIF_NOTSC		28	/* TSC is not accessible in userland */

#define _TIF_SYSCALL_TRACE	(1 << TIF_SYSCALL_TRACE)
#define _TIF_SIGPENDING		(1 << TIF_SIGPENDING)
#define _TIF_SINGLESTEP		(1 << TIF_SINGLESTEP)
#define _TIF_NEED_RESCHED	(1 << TIF_NEED_RESCHED)
#define _TIF_IRET		(1 << TIF_IRET)
#define _TIF_SYSCALL_AUDIT	(1 << TIF_SYSCALL_AUDIT)
#define _TIF_SECCOMP		(1 << TIF_SECCOMP)
#define _TIF_MCE_NOTIFY		(1 << TIF_MCE_NOTIFY)
#define _TIF_HRTICK_RESCHED	(1 << TIF_HRTICK_RESCHED)
#define _TIF_IA32		(1 << TIF_IA32)
#define _TIF_FORK		(1 << TIF_FORK)
#define _TIF_ABI_PENDING	(1 << TIF_ABI_PENDING)
#define _TIF_DEBUG		(1 << TIF_DEBUG)
#define _TIF_IO_BITMAP		(1 << TIF_IO_BITMAP)
#define _TIF_FREEZE		(1 << TIF_FREEZE)
#define _TIF_FORCED_TF		(1 << TIF_FORCED_TF)
#define _TIF_DEBUGCTLMSR	(1 << TIF_DEBUGCTLMSR)
#define _TIF_DS_AREA_MSR	(1 << TIF_DS_AREA_MSR)
#define _TIF_BTS_TRACE_TS	(1 << TIF_BTS_TRACE_TS)
#define _TIF_NOTSC		(1 << TIF_NOTSC)

/* work to do on interrupt/exception return */
#define _TIF_WORK_MASK							\
	(0x0000FFFF &							\
	 ~(_TIF_SYSCALL_TRACE|_TIF_SYSCALL_AUDIT|_TIF_SINGLESTEP|_TIF_SECCOMP))
/* work to do on any return to user space */
#define _TIF_ALLWORK_MASK (0x0000FFFF & ~_TIF_SECCOMP)

#define _TIF_DO_NOTIFY_MASK						\
	(_TIF_SIGPENDING|_TIF_SINGLESTEP|_TIF_MCE_NOTIFY|_TIF_HRTICK_RESCHED)

/* flags to check in __switch_to() */
#define _TIF_WORK_CTXSW							\
	(_TIF_IO_BITMAP|_TIF_DEBUGCTLMSR|_TIF_DS_AREA_MSR|_TIF_BTS_TRACE_TS|_TIF_NOTSC)
#define _TIF_WORK_CTXSW_PREV _TIF_WORK_CTXSW
#define _TIF_WORK_CTXSW_NEXT (_TIF_WORK_CTXSW|_TIF_DEBUG)

#define PREEMPT_ACTIVE     0x10000000

/*
 * Thread-synchronous status.
 *
 * This is different from the flags in that nobody else
 * ever touches our thread-synchronous status, so we don't
 * have to worry about atomic accesses.
 */
#define TS_USEDFPU		0x0001	/* FPU was used by this task
					   this quantum (SMP) */
#define TS_COMPAT		0x0002	/* 32bit syscall active */
#define TS_POLLING		0x0004	/* true if in idle loop
					   and not sleeping */
#define TS_RESTORE_SIGMASK	0x0008	/* restore signal mask in do_signal() */

#define tsk_is_polling(t) (task_thread_info(t)->status & TS_POLLING)

#ifndef __ASSEMBLY__
#define HAVE_SET_RESTORE_SIGMASK	1
static inline void set_restore_sigmask(void)
{
	struct thread_info *ti = current_thread_info();
	ti->status |= TS_RESTORE_SIGMASK;
	set_bit(TIF_SIGPENDING, &ti->flags);
}
#endif	/* !__ASSEMBLY__ */

#endif /* __KERNEL__ */

#endif /* _ASM_THREAD_INFO_H */
