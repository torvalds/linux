/*-
 * Copyright (c) 2017 Hans Petter Selasky
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
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <linux/compat.h>
#include <linux/kthread.h>
#include <linux/sched.h>
#include <linux/wait.h>

#include <sys/bus.h>
#include <sys/interrupt.h>
#include <sys/priority.h>

enum {
	KTHREAD_SHOULD_STOP_MASK = (1 << 0),
	KTHREAD_SHOULD_PARK_MASK = (1 << 1),
	KTHREAD_IS_PARKED_MASK = (1 << 2),
};

bool
linux_kthread_should_stop_task(struct task_struct *task)
{

	return (atomic_read(&task->kthread_flags) & KTHREAD_SHOULD_STOP_MASK);
}

bool
linux_kthread_should_stop(void)
{

	return (atomic_read(&current->kthread_flags) & KTHREAD_SHOULD_STOP_MASK);
}

int
linux_kthread_stop(struct task_struct *task)
{
	int retval;

	/*
	 * Assume task is still alive else caller should not call
	 * kthread_stop():
	 */
	atomic_or(KTHREAD_SHOULD_STOP_MASK, &task->kthread_flags);
	kthread_unpark(task);
	wake_up_process(task);
	wait_for_completion(&task->exited);

	/*
	 * Get return code and free task structure:
	 */
	retval = task->task_ret;
	put_task_struct(task);

	return (retval);
}

int
linux_kthread_park(struct task_struct *task)
{

	atomic_or(KTHREAD_SHOULD_PARK_MASK, &task->kthread_flags);
	wake_up_process(task);
	wait_for_completion(&task->parked);
	return (0);
}

void
linux_kthread_parkme(void)
{
	struct task_struct *task;

	task = current;
	set_task_state(task, TASK_PARKED | TASK_UNINTERRUPTIBLE);
	while (linux_kthread_should_park()) {
		while ((atomic_fetch_or(KTHREAD_IS_PARKED_MASK,
		    &task->kthread_flags) & KTHREAD_IS_PARKED_MASK) == 0)
			complete(&task->parked);
		schedule();
		set_task_state(task, TASK_PARKED | TASK_UNINTERRUPTIBLE);
	}
	atomic_andnot(KTHREAD_IS_PARKED_MASK, &task->kthread_flags);
	set_task_state(task, TASK_RUNNING);
}

bool
linux_kthread_should_park(void)
{
	struct task_struct *task;

	task = current;
	return (atomic_read(&task->kthread_flags) & KTHREAD_SHOULD_PARK_MASK);
}

void
linux_kthread_unpark(struct task_struct *task)
{

	atomic_andnot(KTHREAD_SHOULD_PARK_MASK, &task->kthread_flags);
	if ((atomic_fetch_andnot(KTHREAD_IS_PARKED_MASK, &task->kthread_flags) &
	    KTHREAD_IS_PARKED_MASK) != 0)
		wake_up_state(task, TASK_PARKED);
}

struct task_struct *
linux_kthread_setup_and_run(struct thread *td, linux_task_fn_t *task_fn, void *arg)
{
	struct task_struct *task;

	linux_set_current(td);

	task = td->td_lkpi_task;
	task->task_fn = task_fn;
	task->task_data = arg;

	thread_lock(td);
	/* make sure the scheduler priority is raised */
	sched_prio(td, PI_SWI(SWI_NET));
	/* put thread into run-queue */
	sched_add(td, SRQ_BORING);
	thread_unlock(td);

	return (task);
}

void
linux_kthread_fn(void *arg __unused)
{
	struct task_struct *task = current;

	if (linux_kthread_should_stop_task(task) == 0)
		task->task_ret = task->task_fn(task->task_data);

	if (linux_kthread_should_stop_task(task) != 0) {
		struct thread *td = curthread;

		/* let kthread_stop() free data */
		td->td_lkpi_task = NULL;

		/* wakeup kthread_stop() */
		complete(&task->exited);
	}
	kthread_exit();
}
