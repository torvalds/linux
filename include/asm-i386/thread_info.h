/* thread_info.h: i386 low-level thread information
 *
 * Copyright (C) 2002  David Howells (dhowells@redhat.com)
 * - Incorporating suggestions made by Linus Torvalds and Dave Miller
 */

#ifndef _ASM_THREAD_INFO_H
#define _ASM_THREAD_INFO_H

#ifdef __KERNEL__

#include <linux/compiler.h>
#include <asm/page.h>

#ifndef __ASSEMBLY__
#include <asm/processor.h>
#endif

/*
 * low level task data that entry.S needs immediate access to
 * - this struct should fit entirely inside of one cache line
 * - this struct shares the supervisor stack pages
 * - if the contents of this structure are changed, the assembly constants must also be changed
 */
#ifndef __ASSEMBLY__

struct thread_info {
	struct task_struct	*task;		/* main task structure */
	struct exec_domain	*exec_domain;	/* execution domain */
	unsigned long		flags;		/* low level flags */
	unsigned long		status;		/* thread-synchronous flags */
	__u32			cpu;		/* current CPU */
	int			preempt_count;	/* 0 => preemptable, <0 => BUG */


	mm_segment_t		addr_limit;	/* thread address space:
					 	   0-0xBFFFFFFF for user-thead
						   0-0xFFFFFFFF for kernel-thread
						*/
	void			*sysenter_return;
	struct restart_block    restart_block;

	unsigned long           previous_esp;   /* ESP of the previous stack in case
						   of nested (IRQ) stacks
						*/
	__u8			supervisor_stack[0];
};

#else /* !__ASSEMBLY__ */

#include <asm/asm-offsets.h>

#endif

#define PREEMPT_ACTIVE		0x10000000
#ifdef CONFIG_4KSTACKS
#define THREAD_SIZE            (4096)
#else
#define THREAD_SIZE		(8192)
#endif

#define STACK_WARN             (THREAD_SIZE/8)
/*
 * macros/functions for gaining access to the thread information structure
 *
 * preempt_count needs to be 1 initially, until the scheduler is functional.
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
	.restart_block = {			\
		.fn = do_no_restart_syscall,	\
	},					\
}

#define init_thread_info	(init_thread_union.thread_info)
#define init_stack		(init_thread_union.stack)


/* how to get the current stack pointer from C */
register unsigned long current_stack_pointer asm("esp") __attribute_used__;

/* how to get the thread information struct from C */
static inline struct thread_info *current_thread_info(void)
{
	return (struct thread_info *)(current_stack_pointer & ~(THREAD_SIZE - 1));
}

/* thread information allocation */
#ifdef CONFIG_DEBUG_STACK_USAGE
#define alloc_thread_info(tsk) ((struct thread_info *) \
	__get_free_pages(GFP_KERNEL| __GFP_ZERO, get_order(THREAD_SIZE)))
#else
#define alloc_thread_info(tsk) ((struct thread_info *) \
	__get_free_pages(GFP_KERNEL, get_order(THREAD_SIZE)))
#endif

#define free_thread_info(info)	free_pages((unsigned long)(info), get_order(THREAD_SIZE))

#else /* !__ASSEMBLY__ */

/* how to get the thread information struct from ASM */
#define GET_THREAD_INFO(reg) \
	movl $-THREAD_SIZE, reg; \
	andl %esp, reg

/* use this one if reg already contains %esp */
#define GET_THREAD_INFO_WITH_ESP(reg) \
	andl $-THREAD_SIZE, reg

#endif

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
#define TIF_SINGLESTEP		4	/* restore singlestep on return to user mode */
#define TIF_IRET		5	/* return with iret */
#define TIF_SYSCALL_EMU		6	/* syscall emulation active */
#define TIF_SYSCALL_AUDIT	7	/* syscall auditing active */
#define TIF_SECCOMP		8	/* secure computing */
#define TIF_RESTORE_SIGMASK	9	/* restore signal mask in do_signal() */
#define TIF_MEMDIE		16
#define TIF_DEBUG		17	/* uses debug registers */
#define TIF_IO_BITMAP		18	/* uses I/O bitmap */
#define TIF_FREEZE		19	/* is freezing for suspend */

#define _TIF_SYSCALL_TRACE	(1<<TIF_SYSCALL_TRACE)
#define _TIF_NOTIFY_RESUME	(1<<TIF_NOTIFY_RESUME)
#define _TIF_SIGPENDING		(1<<TIF_SIGPENDING)
#define _TIF_NEED_RESCHED	(1<<TIF_NEED_RESCHED)
#define _TIF_SINGLESTEP		(1<<TIF_SINGLESTEP)
#define _TIF_IRET		(1<<TIF_IRET)
#define _TIF_SYSCALL_EMU	(1<<TIF_SYSCALL_EMU)
#define _TIF_SYSCALL_AUDIT	(1<<TIF_SYSCALL_AUDIT)
#define _TIF_SECCOMP		(1<<TIF_SECCOMP)
#define _TIF_RESTORE_SIGMASK	(1<<TIF_RESTORE_SIGMASK)
#define _TIF_DEBUG		(1<<TIF_DEBUG)
#define _TIF_IO_BITMAP		(1<<TIF_IO_BITMAP)
#define _TIF_FREEZE		(1<<TIF_FREEZE)

/* work to do on interrupt/exception return */
#define _TIF_WORK_MASK \
  (0x0000FFFF & ~(_TIF_SYSCALL_TRACE | _TIF_SYSCALL_AUDIT | \
		  _TIF_SECCOMP | _TIF_SYSCALL_EMU))
/* work to do on any return to u-space */
#define _TIF_ALLWORK_MASK	(0x0000FFFF & ~_TIF_SECCOMP)

/* flags to check in __switch_to() */
#define _TIF_WORK_CTXSW (_TIF_DEBUG|_TIF_IO_BITMAP)

/*
 * Thread-synchronous status.
 *
 * This is different from the flags in that nobody else
 * ever touches our thread-synchronous status, so we don't
 * have to worry about atomic accesses.
 */
#define TS_USEDFPU		0x0001	/* FPU was used by this task this quantum (SMP) */
#define TS_POLLING		0x0002	/* True if in idle loop and not sleeping */

#define tsk_is_polling(t) (task_thread_info(t)->status & TS_POLLING)

#endif /* __KERNEL__ */

#endif /* _ASM_THREAD_INFO_H */
