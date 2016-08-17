/*****************************************************************************\
 *  Copyright (C) 2007-2010 Lawrence Livermore National Security, LLC.
 *  Copyright (C) 2007 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Brian Behlendorf <behlendorf1@llnl.gov>.
 *  UCRL-CODE-235197
 *
 *  This file is part of the SPL, Solaris Porting Layer.
 *  For details, see <http://zfsonlinux.org/>.
 *
 *  The SPL is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the
 *  Free Software Foundation; either version 2 of the License, or (at your
 *  option) any later version.
 *
 *  The SPL is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 *  for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with the SPL.  If not, see <http://www.gnu.org/licenses/>.
\*****************************************************************************/

#ifndef _SPL_THREAD_H
#define	_SPL_THREAD_H

#include <linux/module.h>
#include <linux/mm.h>
#include <linux/spinlock.h>
#include <linux/kthread.h>
#include <sys/types.h>
#include <sys/sysmacros.h>
#include <sys/tsd.h>

/*
 * Thread interfaces
 */
#define	TP_MAGIC			0x53535353

#define	TS_SLEEP			TASK_INTERRUPTIBLE
#define	TS_RUN				TASK_RUNNING
#define	TS_ZOMB				EXIT_ZOMBIE
#define	TS_STOPPED			TASK_STOPPED

typedef void (*thread_func_t)(void *);

/* BEGIN CSTYLED */
#define	thread_create(stk, stksize, func, arg, len, pp, state, pri)	\
	__thread_create(stk, stksize, (thread_func_t)func,		\
	#func, arg, len, pp, state, pri)
/* END CSTYLED */

#define	thread_exit()			__thread_exit()
#define	thread_join(t)			VERIFY(0)
#define	curthread			current
#define	getcomm()			current->comm
#define	getpid()			current->pid

extern kthread_t *__thread_create(caddr_t stk, size_t  stksize,
    thread_func_t func, const char *name, void *args, size_t len, proc_t *pp,
    int state, pri_t pri);
extern void __thread_exit(void);
extern struct task_struct *spl_kthread_create(int (*func)(void *),
			void *data, const char namefmt[], ...);

#endif  /* _SPL_THREAD_H */
