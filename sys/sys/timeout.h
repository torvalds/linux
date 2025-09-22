/*	$OpenBSD: timeout.h,v 1.51 2025/05/23 23:56:14 dlg Exp $	*/
/*
 * Copyright (c) 2000-2001 Artur Grabowski <art@openbsd.org>
 * All rights reserved. 
 *
 * Redistribution and use in source and binary forms, with or without 
 * modification, are permitted provided that the following conditions 
 * are met: 
 *
 * 1. Redistributions of source code must retain the above copyright 
 *    notice, this list of conditions and the following disclaimer. 
 * 2. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission. 
 *
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES,
 * INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY
 * AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
 * THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL  DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE. 
 */

#ifndef _SYS_TIMEOUT_H_
#define _SYS_TIMEOUT_H_

#include <sys/time.h>

struct circq {
	struct circq *next;		/* next element */
	struct circq *prev;		/* previous element */
};

struct timeout {
	struct circq to_list;			/* timeout queue, don't move */
	struct timespec to_abstime;		/* absolute time to run at */
	void (*to_func)(void *);		/* function to call */
	void *to_arg;				/* function argument */
#if 1 /* NKCOV > 0 */
	struct process *to_process;		/* kcov identifier */
#endif
	int to_time;				/* ticks on event */
	int to_flags;				/* misc flags */
	int to_kclock;				/* abstime's kernel clock */
};

/*
 * flags in the to_flags field.
 */
#define TIMEOUT_PROC		0x01	/* needs a process context */
#define TIMEOUT_ONQUEUE		0x02	/* on any timeout queue */
#define TIMEOUT_INITIALIZED	0x04	/* initialized */
#define TIMEOUT_TRIGGERED	0x08	/* running or ran */
#define TIMEOUT_MPSAFE		0x10	/* run without kernel lock */

struct timeoutstat {
	uint64_t tos_added;		/* timeout_add*(9) calls */
	uint64_t tos_cancelled;		/* dequeued during timeout_del*(9) */
	uint64_t tos_deleted;		/* timeout_del*(9) calls */
	uint64_t tos_late;		/* run after deadline */
	uint64_t tos_pending;		/* number currently ONQUEUE */
	uint64_t tos_readded;		/* timeout_add*(9) + already ONQUEUE */
	uint64_t tos_rescheduled;	/* bucketed + already SCHEDULED */
	uint64_t tos_run_softclock;	/* run from softclock() */
	uint64_t tos_run_thread;	/* run from softclock_thread() */
	uint64_t tos_scheduled;		/* bucketed during softclock() */
	uint64_t tos_softclocks;	/* softclock() calls */
	uint64_t tos_thread_wakeups;	/* wakeups in softclock_thread() */
};

#ifdef _KERNEL
int timeout_sysctl(void *, size_t *, void *, size_t);

/*
 * special macros
 *
 * timeout_pending(to) - is this timeout already scheduled to run?
 * timeout_initialized(to) - is this timeout initialized?
 */
#define timeout_pending(to) ((to)->to_flags & TIMEOUT_ONQUEUE)
#define timeout_initialized(to) ((to)->to_flags & TIMEOUT_INITIALIZED)
#define timeout_triggered(to) ((to)->to_flags & TIMEOUT_TRIGGERED)

#define KCLOCK_NONE	(-1)		/* dummy clock for sanity checks */
#define KCLOCK_UPTIME	0		/* uptime clock; time since boot */
#define KCLOCK_MAX	1

#define TIMEOUT_INITIALIZER_FLAGS(_fn, _arg, _kclock, _flags) {		\
	.to_list = { NULL, NULL },					\
	.to_abstime = { .tv_sec = 0, .tv_nsec = 0 },			\
	.to_func = (_fn),						\
	.to_arg = (_arg),						\
	.to_time = 0,							\
	.to_flags = (_flags) | TIMEOUT_INITIALIZED,			\
	.to_kclock = (_kclock)						\
}

#define TIMEOUT_INITIALIZER(_f, _a)					\
    TIMEOUT_INITIALIZER_FLAGS((_f), (_a), KCLOCK_NONE, 0)

void timeout_set(struct timeout *, void (*)(void *), void *);
void timeout_set_flags(struct timeout *, void (*)(void *), void *, int, int);
void timeout_set_proc(struct timeout *, void (*)(void *), void *);

int timeout_add(struct timeout *, int);
int timeout_add_sec(struct timeout *, int);
int timeout_add_msec(struct timeout *, uint64_t);
int timeout_add_usec(struct timeout *, uint64_t);
int timeout_add_nsec(struct timeout *, uint64_t);

int timeout_abs_ts(struct timeout *, const struct timespec *);

int timeout_del(struct timeout *);
int timeout_del_barrier(struct timeout *);
void timeout_barrier(struct timeout *);

void timeout_adjust_ticks(int);
void timeout_hardclock_update(void);
void timeout_startup(void);

#endif /* _KERNEL */

#endif	/* _SYS_TIMEOUT_H_ */
