/*-
 * Copyright (c) 2010 Isilon Systems, Inc.
 * Copyright (c) 2010 iX Systems, Inc.
 * Copyright (c) 2010 Panasas, Inc.
 * Copyright (c) 2013-2018 Mellanox Technologies, Ltd.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice unmodified, this list of conditions, and the following
 *    disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 */
#ifndef	_LINUX_SCHED_H_
#define	_LINUX_SCHED_H_

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/proc.h>
#include <sys/sched.h>
#include <sys/sleepqueue.h>
#include <sys/time.h>

#include <linux/bitmap.h>
#include <linux/compat.h>
#include <linux/completion.h>
#include <linux/mm_types.h>
#include <linux/pid.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/time.h>

#include <asm/atomic.h>

#define	MAX_SCHEDULE_TIMEOUT	INT_MAX

#define	TASK_RUNNING		0x0000
#define	TASK_INTERRUPTIBLE	0x0001
#define	TASK_UNINTERRUPTIBLE	0x0002
#define	TASK_NORMAL		(TASK_INTERRUPTIBLE | TASK_UNINTERRUPTIBLE)
#define	TASK_WAKING		0x0100
#define	TASK_PARKED		0x0200

#define	TASK_COMM_LEN		(MAXCOMLEN + 1)

struct work_struct;
struct task_struct {
	struct thread *task_thread;
	struct mm_struct *mm;
	linux_task_fn_t *task_fn;
	void   *task_data;
	int	task_ret;
	atomic_t usage;
	atomic_t state;
	atomic_t kthread_flags;
	pid_t	pid;	/* BSD thread ID */
	const char    *comm;
	void   *bsd_ioctl_data;
	unsigned bsd_ioctl_len;
	struct completion parked;
	struct completion exited;
	TAILQ_ENTRY(task_struct) rcu_entry;
	int rcu_recurse;
	int bsd_interrupt_value;
	struct work_struct *work;	/* current work struct, if set */
};

#define	current	({ \
	struct thread *__td = curthread; \
	linux_set_current(__td); \
	((struct task_struct *)__td->td_lkpi_task); \
})

#define	task_pid_group_leader(task) (task)->task_thread->td_proc->p_pid
#define	task_pid(task)		((task)->pid)
#define	task_pid_nr(task)	((task)->pid)
#define	task_pid_vnr(task)	((task)->pid)
#define	get_pid(x)		(x)
#define	put_pid(x)		do { } while (0)
#define	current_euid()	(curthread->td_ucred->cr_uid)
#define	task_euid(task)	((task)->task_thread->td_ucred->cr_uid)

#define	get_task_state(task)		atomic_read(&(task)->state)
#define	set_task_state(task, x)		atomic_set(&(task)->state, (x))
#define	__set_task_state(task, x)	((task)->state.counter = (x))
#define	set_current_state(x)		set_task_state(current, x)
#define	__set_current_state(x)		__set_task_state(current, x)

static inline void
get_task_struct(struct task_struct *task)
{
	atomic_inc(&task->usage);
}

static inline void
put_task_struct(struct task_struct *task)
{
	if (atomic_dec_and_test(&task->usage))
		linux_free_current(task);
}

#define	cond_resched()	do { if (!cold) sched_relinquish(curthread); } while (0)

#define	yield()		kern_yield(PRI_UNCHANGED)
#define	sched_yield()	sched_relinquish(curthread)

#define	need_resched() (curthread->td_flags & TDF_NEEDRESCHED)

bool linux_signal_pending(struct task_struct *task);
bool linux_fatal_signal_pending(struct task_struct *task);
bool linux_signal_pending_state(long state, struct task_struct *task);
void linux_send_sig(int signo, struct task_struct *task);

#define	signal_pending(task)		linux_signal_pending(task)
#define	fatal_signal_pending(task)	linux_fatal_signal_pending(task)
#define	signal_pending_state(state, task)		\
	linux_signal_pending_state(state, task)
#define	send_sig(signo, task, priv) do {		\
	CTASSERT((priv) == 0);				\
	linux_send_sig(signo, task);			\
} while (0)

int linux_schedule_timeout(int timeout);

static inline void
linux_schedule_save_interrupt_value(struct task_struct *task, int value)
{
	task->bsd_interrupt_value = value;
}

bool linux_task_exiting(struct task_struct *task);

#define	current_exiting() \
	linux_task_exiting(current)

static inline int
linux_schedule_get_interrupt_value(struct task_struct *task)
{
	int value = task->bsd_interrupt_value;
	task->bsd_interrupt_value = 0;
	return (value);
}

#define	schedule()					\
	(void)linux_schedule_timeout(MAX_SCHEDULE_TIMEOUT)
#define	schedule_timeout(timeout)			\
	linux_schedule_timeout(timeout)
#define	schedule_timeout_killable(timeout)		\
	schedule_timeout_interruptible(timeout)
#define	schedule_timeout_interruptible(timeout) ({	\
	set_current_state(TASK_INTERRUPTIBLE);		\
	schedule_timeout(timeout);			\
})
#define	schedule_timeout_uninterruptible(timeout) ({	\
	set_current_state(TASK_UNINTERRUPTIBLE);	\
	schedule_timeout(timeout);			\
})

#define	io_schedule()			schedule()
#define	io_schedule_timeout(timeout)	schedule_timeout(timeout)

static inline uint64_t
local_clock(void)
{
	struct timespec ts;

	nanotime(&ts);
	return ((uint64_t)ts.tv_sec * NSEC_PER_SEC + ts.tv_nsec);
}

static inline const char *
get_task_comm(char *buf, struct task_struct *task)
{

	buf[0] = 0; /* buffer is too small */
	return (task->comm);
}

#endif	/* _LINUX_SCHED_H_ */
