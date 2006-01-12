/*
 * include/asm-xtensa/thread_info.h
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2001 - 2005 Tensilica Inc.
 */

#ifndef _XTENSA_THREAD_INFO_H
#define _XTENSA_THREAD_INFO_H

#ifdef __KERNEL__

#ifndef __ASSEMBLY__
# include <asm/processor.h>
#endif

/*
 * low level task data that entry.S needs immediate access to
 * - this struct should fit entirely inside of one cache line
 * - this struct shares the supervisor stack pages
 * - if the contents of this structure are changed, the assembly constants
 *   must also be changed
 */

#ifndef __ASSEMBLY__

struct thread_info {
	struct task_struct	*task;		/* main task structure */
	struct exec_domain	*exec_domain;	/* execution domain */
	unsigned long		flags;		/* low level flags */
	unsigned long		status;		/* thread-synchronous flags */
	__u32			cpu;		/* current CPU */
	__s32			preempt_count;	/* 0 => preemptable,< 0 => BUG*/

	mm_segment_t		addr_limit;	/* thread address space */
	struct restart_block    restart_block;


};

#else /* !__ASSEMBLY__ */

/* offsets into the thread_info struct for assembly code access */
#define TI_TASK		 0x00000000
#define TI_EXEC_DOMAIN	 0x00000004
#define TI_FLAGS	 0x00000008
#define TI_STATUS	 0x0000000C
#define TI_CPU		 0x00000010
#define TI_PRE_COUNT	 0x00000014
#define TI_ADDR_LIMIT	 0x00000018
#define TI_RESTART_BLOCK 0x000001C

#endif

#define PREEMPT_ACTIVE		0x10000000

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

/* how to get the thread information struct from C */
static inline struct thread_info *current_thread_info(void)
{
	struct thread_info *ti;
	 __asm__("extui %0,a1,0,13\n\t"
	         "xor %0, a1, %0" : "=&r" (ti) : );
	return ti;
}

/* thread information allocation */
#define alloc_thread_info(tsk) ((struct thread_info *) __get_free_pages(GFP_KERNEL,1))
#define free_thread_info(ti) free_pages((unsigned long) (ti), 1)

#else /* !__ASSEMBLY__ */

/* how to get the thread information struct from ASM */
#define GET_THREAD_INFO(reg,sp) \
	extui reg, sp, 0, 13; \
	xor   reg, sp, reg
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
#define TIF_MEMDIE		6
#define TIF_POLLING_NRFLAG	16	/* true if poll_idle() is polling TIF_NEED_RESCHED */

#define _TIF_SYSCALL_TRACE	(1<<TIF_SYSCALL_TRACE)
#define _TIF_NOTIFY_RESUME	(1<<TIF_NOTIFY_RESUME)
#define _TIF_SIGPENDING		(1<<TIF_SIGPENDING)
#define _TIF_NEED_RESCHED	(1<<TIF_NEED_RESCHED)
#define _TIF_SINGLESTEP		(1<<TIF_SINGLESTEP)
#define _TIF_IRET		(1<<TIF_IRET)
#define _TIF_POLLING_NRFLAG	(1<<TIF_POLLING_NRFLAG)

#define _TIF_WORK_MASK		0x0000FFFE	/* work to do on interrupt/exception return */
#define _TIF_ALLWORK_MASK	0x0000FFFF	/* work to do on any return to u-space */

/*
 * Thread-synchronous status.
 *
 * This is different from the flags in that nobody else
 * ever touches our thread-synchronous status, so we don't
 * have to worry about atomic accesses.
 */
#define TS_USEDFPU		0x0001	/* FPU was used by this task this quantum (SMP) */

#define THREAD_SIZE 8192	//(2*PAGE_SIZE)

#endif	/* __KERNEL__ */
#endif	/* _XTENSA_THREAD_INFO */
