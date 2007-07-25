/*
 * File:         include/asm-blackfin/thread_info.h
 * Based on:     include/asm-m68knommu/thread_info.h
 * Author:       LG Soft India
 *               Copyright (C) 2004-2005 Analog Devices Inc.
 * Created:      Tue Sep 21 2004
 * Description:  Blackfin low-level thread information
 * Modified:
 * Bugs:         Enter bugs at http://blackfin.uclinux.org/
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; see the file COPYING.
 * If not, write to the Free Software Foundation,
 * 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#ifndef _ASM_THREAD_INFO_H
#define _ASM_THREAD_INFO_H

#include <asm/page.h>
#include <asm/entry.h>
#include <asm/l1layout.h>
#include <linux/compiler.h>

#ifdef __KERNEL__

/* Thread Align Mask to reach to the top of the stack
 * for any process
 */
#define ALIGN_PAGE_MASK         0xffffe000

/*
 * Size of kernel stack for each process. This must be a power of 2...
 */
#define THREAD_SIZE		8192	/* 2 pages */

#ifndef __ASSEMBLY__

typedef unsigned long mm_segment_t;

/*
 * low level task data.
 * If you change this, change the TI_* offsets below to match.
 */

struct thread_info {
	struct task_struct *task;	/* main task structure */
	struct exec_domain *exec_domain;	/* execution domain */
	unsigned long flags;	/* low level flags */
	int cpu;		/* cpu we're on */
	int preempt_count;	/* 0 => preemptable, <0 => BUG */
	mm_segment_t addr_limit;	/* address limit */
	struct restart_block restart_block;
	struct l1_scratch_task_info l1_task_info;
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
	.preempt_count  = 1,                    \
	.restart_block	= {			\
		.fn = do_no_restart_syscall,	\
	},					\
}
#define init_thread_info	(init_thread_union.thread_info)
#define init_stack		(init_thread_union.stack)

/* How to get the thread information struct from C */

static inline struct thread_info *current_thread_info(void)
    __attribute__ ((__const__));

/* Given a task stack pointer, you can find it's task structure
 * just by masking it to the 8K boundary.
 */
static inline struct thread_info *current_thread_info(void)
{
	struct thread_info *ti;
      __asm__("%0 = sp;": "=&d"(ti):
	);
	return (struct thread_info *)((long)ti & ~((long)THREAD_SIZE-1));
}

/* thread information allocation */
#define alloc_thread_info(tsk) ((struct thread_info *) \
				__get_free_pages(GFP_KERNEL, 1))
#define free_thread_info(ti)	free_pages((unsigned long) (ti), 1)
#endif				/* __ASSEMBLY__ */

/*
 * Offsets in thread_info structure, used in assembly code
 */
#define TI_TASK		0
#define TI_EXECDOMAIN	4
#define TI_FLAGS	8
#define TI_CPU		12
#define TI_PREEMPT	16

#define	PREEMPT_ACTIVE	0x4000000

/*
 * thread information flag bit numbers
 */
#define TIF_SYSCALL_TRACE	0	/* syscall trace active */
#define TIF_NOTIFY_RESUME	1	/* resumption notification requested */
#define TIF_SIGPENDING		2	/* signal pending */
#define TIF_NEED_RESCHED	3	/* rescheduling necessary */
#define TIF_POLLING_NRFLAG	4	/* true if poll_idle() is polling
					   TIF_NEED_RESCHED */
#define TIF_MEMDIE              5
#define TIF_RESTORE_SIGMASK	6	/* restore signal mask in do_signal() */
#define TIF_FREEZE              7       /* is freezing for suspend */

/* as above, but as bit values */
#define _TIF_SYSCALL_TRACE	(1<<TIF_SYSCALL_TRACE)
#define _TIF_NOTIFY_RESUME	(1<<TIF_NOTIFY_RESUME)
#define _TIF_SIGPENDING		(1<<TIF_SIGPENDING)
#define _TIF_NEED_RESCHED	(1<<TIF_NEED_RESCHED)
#define _TIF_POLLING_NRFLAG	(1<<TIF_POLLING_NRFLAG)
#define _TIF_RESTORE_SIGMASK	(1<<TIF_RESTORE_SIGMASK)
#define _TIF_FREEZE             (1<<TIF_FREEZE)

#define _TIF_WORK_MASK		0x0000FFFE	/* work to do on interrupt/exception return */

#endif				/* __KERNEL__ */

#endif				/* _ASM_THREAD_INFO_H */
