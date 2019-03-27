/*-
 * Copyright (c) 2016 Matthew Macy (mmacy@mattmacy.io)
 * Copyright (c) 2017 Hans Petter Selasky (hselasky@freebsd.org)
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

#include <sys/types.h>
#include <sys/systm.h>
#include <sys/malloc.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/proc.h>
#include <sys/sched.h>
#include <sys/smp.h>
#include <sys/queue.h>
#include <sys/taskqueue.h>
#include <sys/kdb.h>

#include <ck_epoch.h>

#include <linux/rcupdate.h>
#include <linux/srcu.h>
#include <linux/slab.h>
#include <linux/kernel.h>
#include <linux/compat.h>

/*
 * By defining CONFIG_NO_RCU_SKIP LinuxKPI RCU locks and asserts will
 * not be skipped during panic().
 */
#ifdef CONFIG_NO_RCU_SKIP
#define	RCU_SKIP(void) 0
#else
#define	RCU_SKIP(void)	unlikely(SCHEDULER_STOPPED() || kdb_active)
#endif

struct callback_head {
	STAILQ_ENTRY(callback_head) entry;
	rcu_callback_t func;
};

struct linux_epoch_head {
	STAILQ_HEAD(, callback_head) cb_head;
	struct mtx lock;
	struct task task;
} __aligned(CACHE_LINE_SIZE);

struct linux_epoch_record {
	ck_epoch_record_t epoch_record;
	TAILQ_HEAD(, task_struct) ts_head;
	int cpuid;
} __aligned(CACHE_LINE_SIZE);

/*
 * Verify that "struct rcu_head" is big enough to hold "struct
 * callback_head". This has been done to avoid having to add special
 * compile flags for including ck_epoch.h to all clients of the
 * LinuxKPI.
 */
CTASSERT(sizeof(struct rcu_head) == sizeof(struct callback_head));

/*
 * Verify that "epoch_record" is at beginning of "struct
 * linux_epoch_record":
 */
CTASSERT(offsetof(struct linux_epoch_record, epoch_record) == 0);

static ck_epoch_t linux_epoch;
static struct linux_epoch_head linux_epoch_head;
DPCPU_DEFINE_STATIC(struct linux_epoch_record, linux_epoch_record);

static void linux_rcu_cleaner_func(void *, int);

static void
linux_rcu_runtime_init(void *arg __unused)
{
	struct linux_epoch_head *head;
	int i;

	ck_epoch_init(&linux_epoch);

	head = &linux_epoch_head;

	mtx_init(&head->lock, "LRCU-HEAD", NULL, MTX_DEF);
	TASK_INIT(&head->task, 0, linux_rcu_cleaner_func, NULL);
	STAILQ_INIT(&head->cb_head);

	CPU_FOREACH(i) {
		struct linux_epoch_record *record;

		record = &DPCPU_ID_GET(i, linux_epoch_record);

		record->cpuid = i;
		ck_epoch_register(&linux_epoch, &record->epoch_record, NULL);
		TAILQ_INIT(&record->ts_head);
	}
}
SYSINIT(linux_rcu_runtime, SI_SUB_CPU, SI_ORDER_ANY, linux_rcu_runtime_init, NULL);

static void
linux_rcu_runtime_uninit(void *arg __unused)
{
	struct linux_epoch_head *head;

	head = &linux_epoch_head;

	/* destroy head lock */
	mtx_destroy(&head->lock);
}
SYSUNINIT(linux_rcu_runtime, SI_SUB_LOCK, SI_ORDER_SECOND, linux_rcu_runtime_uninit, NULL);

static void
linux_rcu_cleaner_func(void *context __unused, int pending __unused)
{
	struct linux_epoch_head *head;
	struct callback_head *rcu;
	STAILQ_HEAD(, callback_head) tmp_head;

	linux_set_current(curthread);

	head = &linux_epoch_head;

	/* move current callbacks into own queue */
	mtx_lock(&head->lock);
	STAILQ_INIT(&tmp_head);
	STAILQ_CONCAT(&tmp_head, &head->cb_head);
	mtx_unlock(&head->lock);

	/* synchronize */
	linux_synchronize_rcu();

	/* dispatch all callbacks, if any */
	while ((rcu = STAILQ_FIRST(&tmp_head)) != NULL) {
		uintptr_t offset;

		STAILQ_REMOVE_HEAD(&tmp_head, entry);

		offset = (uintptr_t)rcu->func;

		if (offset < LINUX_KFREE_RCU_OFFSET_MAX)
			kfree((char *)rcu - offset);
		else
			rcu->func((struct rcu_head *)rcu);
	}
}

void
linux_rcu_read_lock(void)
{
	struct linux_epoch_record *record;
	struct task_struct *ts;

	if (RCU_SKIP())
		return;

	/*
	 * Pin thread to current CPU so that the unlock code gets the
	 * same per-CPU epoch record:
	 */
	sched_pin();

	record = &DPCPU_GET(linux_epoch_record);
	ts = current;

	/*
	 * Use a critical section to prevent recursion inside
	 * ck_epoch_begin(). Else this function supports recursion.
	 */
	critical_enter();
	ck_epoch_begin(&record->epoch_record, NULL);
	ts->rcu_recurse++;
	if (ts->rcu_recurse == 1)
		TAILQ_INSERT_TAIL(&record->ts_head, ts, rcu_entry);
	critical_exit();
}

void
linux_rcu_read_unlock(void)
{
	struct linux_epoch_record *record;
	struct task_struct *ts;

	if (RCU_SKIP())
		return;

	record = &DPCPU_GET(linux_epoch_record);
	ts = current;

	/*
	 * Use a critical section to prevent recursion inside
	 * ck_epoch_end(). Else this function supports recursion.
	 */
	critical_enter();
	ck_epoch_end(&record->epoch_record, NULL);
	ts->rcu_recurse--;
	if (ts->rcu_recurse == 0)
		TAILQ_REMOVE(&record->ts_head, ts, rcu_entry);
	critical_exit();

	sched_unpin();
}

static void
linux_synchronize_rcu_cb(ck_epoch_t *epoch __unused, ck_epoch_record_t *epoch_record, void *arg __unused)
{
	struct linux_epoch_record *record =
	    container_of(epoch_record, struct linux_epoch_record, epoch_record);
	struct thread *td = curthread;
	struct task_struct *ts;

	/* check if blocked on the current CPU */
	if (record->cpuid == PCPU_GET(cpuid)) {
		bool is_sleeping = 0;
		u_char prio = 0;

		/*
		 * Find the lowest priority or sleeping thread which
		 * is blocking synchronization on this CPU core. All
		 * the threads in the queue are CPU-pinned and cannot
		 * go anywhere while the current thread is locked.
		 */
		TAILQ_FOREACH(ts, &record->ts_head, rcu_entry) {
			if (ts->task_thread->td_priority > prio)
				prio = ts->task_thread->td_priority;
			is_sleeping |= (ts->task_thread->td_inhibitors != 0);
		}

		if (is_sleeping) {
			thread_unlock(td);
			pause("W", 1);
			thread_lock(td);
		} else {
			/* set new thread priority */
			sched_prio(td, prio);
			/* task switch */
			mi_switch(SW_VOL | SWT_RELINQUISH, NULL);

			/*
			 * Release the thread lock while yielding to
			 * allow other threads to acquire the lock
			 * pointed to by TDQ_LOCKPTR(td). Else a
			 * deadlock like situation might happen.
			 */
			thread_unlock(td);
			thread_lock(td);
		}
	} else {
		/*
		 * To avoid spinning move execution to the other CPU
		 * which is blocking synchronization. Set highest
		 * thread priority so that code gets run. The thread
		 * priority will be restored later.
		 */
		sched_prio(td, 0);
		sched_bind(td, record->cpuid);
	}
}

void
linux_synchronize_rcu(void)
{
	struct thread *td;
	int was_bound;
	int old_cpu;
	int old_pinned;
	u_char old_prio;

	if (RCU_SKIP())
		return;

	WITNESS_WARN(WARN_GIANTOK | WARN_SLEEPOK, NULL,
	    "linux_synchronize_rcu() can sleep");

	td = curthread;

	/*
	 * Synchronizing RCU might change the CPU core this function
	 * is running on. Save current values:
	 */
	thread_lock(td);

	DROP_GIANT();

	old_cpu = PCPU_GET(cpuid);
	old_pinned = td->td_pinned;
	old_prio = td->td_priority;
	was_bound = sched_is_bound(td);
	sched_unbind(td);
	td->td_pinned = 0;
	sched_bind(td, old_cpu);

	ck_epoch_synchronize_wait(&linux_epoch,
	    &linux_synchronize_rcu_cb, NULL);

	/* restore CPU binding, if any */
	if (was_bound != 0) {
		sched_bind(td, old_cpu);
	} else {
		/* get thread back to initial CPU, if any */
		if (old_pinned != 0)
			sched_bind(td, old_cpu);
		sched_unbind(td);
	}
	/* restore pinned after bind */
	td->td_pinned = old_pinned;

	/* restore thread priority */
	sched_prio(td, old_prio);
	thread_unlock(td);

	PICKUP_GIANT();
}

void
linux_rcu_barrier(void)
{
	struct linux_epoch_head *head;

	linux_synchronize_rcu();

	head = &linux_epoch_head;

	/* wait for callbacks to complete */
	taskqueue_drain(taskqueue_fast, &head->task);
}

void
linux_call_rcu(struct rcu_head *context, rcu_callback_t func)
{
	struct callback_head *rcu = (struct callback_head *)context;
	struct linux_epoch_head *head = &linux_epoch_head;

	mtx_lock(&head->lock);
	rcu->func = func;
	STAILQ_INSERT_TAIL(&head->cb_head, rcu, entry);
	taskqueue_enqueue(taskqueue_fast, &head->task);
	mtx_unlock(&head->lock);
}

int
init_srcu_struct(struct srcu_struct *srcu)
{
	return (0);
}

void
cleanup_srcu_struct(struct srcu_struct *srcu)
{
}

int
srcu_read_lock(struct srcu_struct *srcu)
{
	linux_rcu_read_lock();
	return (0);
}

void
srcu_read_unlock(struct srcu_struct *srcu, int key __unused)
{
	linux_rcu_read_unlock();
}

void
synchronize_srcu(struct srcu_struct *srcu)
{
	linux_synchronize_rcu();
}

void
srcu_barrier(struct srcu_struct *srcu)
{
	linux_rcu_barrier();
}
