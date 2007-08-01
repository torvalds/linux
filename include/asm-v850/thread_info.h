/*
 * include/asm-v850/thread_info.h -- v850 low-level thread information
 *
 *  Copyright (C) 2002  NEC Corporation
 *  Copyright (C) 2002  Miles Bader <miles@gnu.org>
 *  Copyright (C) 2002  David Howells (dhowells@redhat.com)
 *    - Incorporating suggestions made by Linus Torvalds and Dave Miller
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License.  See the file COPYING in the main directory of this
 * archive for more details.
 *
 * This file was derived from the PPC version, include/asm-ppc/thread_info.h
 * which was adapted from the i386 version by Paul Mackerras
 */

#ifndef __V850_THREAD_INFO_H__
#define __V850_THREAD_INFO_H__

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
	int			cpu;		/* cpu we're on */
	int			preempt_count;	/* 0 => preemptable,
						   <0 => BUG */
	struct restart_block	restart_block;
};

#define INIT_THREAD_INFO(tsk)						      \
{									      \
	.task =		&tsk,						      \
	.exec_domain =	&default_exec_domain,				      \
	.flags =	0,						      \
	.cpu =		0,						      \
	.preempt_count = 1,						      \
	.restart_block = {						      \
		.fn = do_no_restart_syscall,				      \
	},								      \
}

#define init_thread_info	(init_thread_union.thread_info)
#define init_stack		(init_thread_union.stack)

/*
 * macros/functions for gaining access to the thread information structure
 */

/* thread information allocation */
#define alloc_thread_info(tsk) ((struct thread_info *) \
				__get_free_pages(GFP_KERNEL, 1))
#define free_thread_info(ti)	free_pages((unsigned long) (ti), 1)

#endif /* __ASSEMBLY__ */


/*
 * Offsets in thread_info structure, used in assembly code
 */
#define TI_TASK		0
#define TI_EXECDOMAIN	4
#define TI_FLAGS	8
#define TI_CPU		12
#define TI_PREEMPT	16

#define PREEMPT_ACTIVE		0x4000000

/*
 * thread information flag bit numbers
 */
#define TIF_SYSCALL_TRACE	0	/* syscall trace active */
#define TIF_SIGPENDING		1	/* signal pending */
#define TIF_NEED_RESCHED	2	/* rescheduling necessary */
#define TIF_POLLING_NRFLAG	3	/* true if poll_idle() is polling
					   TIF_NEED_RESCHED */
#define TIF_MEMDIE		4

/* as above, but as bit values */
#define _TIF_SYSCALL_TRACE	(1<<TIF_SYSCALL_TRACE)
#define _TIF_SIGPENDING		(1<<TIF_SIGPENDING)
#define _TIF_NEED_RESCHED	(1<<TIF_NEED_RESCHED)
#define _TIF_POLLING_NRFLAG	(1<<TIF_POLLING_NRFLAG)


/* Size of kernel stack for each process.  */
#define THREAD_SIZE		0x2000

/* The alignment of kernel threads, with thread_info structures at their
   base.  Thus, a pointer for a task's task structure can be derived from
   its kernel stack pointer.  */
#define THREAD_ALIGNMENT	THREAD_SIZE
#define THREAD_MASK		(-THREAD_ALIGNMENT)


#ifdef __ASSEMBLY__

/* Put a pointer to the current thread_info structure into REG.  Note that
   this definition requires THREAD_MASK to be representable as a signed
   16-bit value.  */
#define GET_CURRENT_THREAD(reg)						\
        /* Use `addi' and then `and' instead of just `andi', because	\
	   `addi' sign-extends the immediate value, whereas `andi'	\
	   zero-extends it.  */						\
	addi	THREAD_MASK, r0, reg;					\
	and	sp, reg

#else

/* Return a pointer to the current thread_info structure.  */
static inline struct thread_info *current_thread_info (void)
{
	register unsigned long sp __asm__ ("sp");
	return (struct thread_info *)(sp & THREAD_MASK);
}

#endif /* __ASSEMBLY__ */


#endif /* __KERNEL__ */

#endif /* __V850_THREAD_INFO_H__ */
