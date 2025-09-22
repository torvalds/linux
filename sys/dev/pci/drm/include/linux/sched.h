/*	$OpenBSD: sched.h,v 1.5 2023/03/21 09:44:35 jsg Exp $	*/
/*
 * Copyright (c) 2013, 2014, 2015 Mark Kettenis
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#ifndef _LINUX_SCHED_H
#define _LINUX_SCHED_H

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/stdint.h>
#include <sys/mutex.h>
#include <linux/hrtimer.h>
#include <linux/sem.h>

#define TASK_NORMAL		1
#define TASK_UNINTERRUPTIBLE	0
#define TASK_INTERRUPTIBLE	PCATCH
#define TASK_RUNNING		-1

#define MAX_SCHEDULE_TIMEOUT	(INT32_MAX)

#define TASK_COMM_LEN		(MAXCOMLEN + 1)

#define cond_resched()		sched_pause(yield)
#define drm_need_resched() \
    (curcpu()->ci_schedstate.spc_schedflags & SPCF_SHOULDYIELD)

static inline int
cond_resched_lock(struct mutex *mtxp)
{
	if (drm_need_resched() == 0)
		return 0;

	mtx_leave(mtxp);
	cond_resched();
	mtx_enter(mtxp);

	return 1;
}

void set_current_state(int);
void __set_current_state(int);
void schedule(void);
long schedule_timeout(long);
long schedule_timeout_uninterruptible(long);

#define io_schedule_timeout(x)	schedule_timeout(x)

struct proc;
int wake_up_process(struct proc *p);

#endif
