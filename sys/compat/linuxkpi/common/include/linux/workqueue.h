/*-
 * Copyright (c) 2010 Isilon Systems, Inc.
 * Copyright (c) 2010 iX Systems, Inc.
 * Copyright (c) 2010 Panasas, Inc.
 * Copyright (c) 2013-2017 Mellanox Technologies, Ltd.
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
#ifndef	_LINUX_WORKQUEUE_H_
#define	_LINUX_WORKQUEUE_H_

#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/timer.h>
#include <linux/slab.h>

#include <asm/atomic.h>

#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/taskqueue.h>
#include <sys/mutex.h>

#define	WORK_CPU_UNBOUND MAXCPU
#define	WQ_UNBOUND (1 << 0)
#define	WQ_HIGHPRI (1 << 1)

struct work_struct;
typedef void (*work_func_t)(struct work_struct *);

struct work_exec {
	TAILQ_ENTRY(work_exec) entry;
	struct work_struct *target;
};

struct workqueue_struct {
	struct taskqueue *taskqueue;
	struct mtx exec_mtx;
	TAILQ_HEAD(, work_exec) exec_head;
	atomic_t draining;
};

#define	WQ_EXEC_LOCK(wq) mtx_lock(&(wq)->exec_mtx)
#define	WQ_EXEC_UNLOCK(wq) mtx_unlock(&(wq)->exec_mtx)

struct work_struct {
	struct task work_task;
	struct workqueue_struct *work_queue;
	work_func_t func;
	atomic_t state;
};

#define	DECLARE_WORK(name, fn)						\
	struct work_struct name;					\
	static void name##_init(void *arg)				\
	{								\
		INIT_WORK(&name, fn);					\
	}								\
	SYSINIT(name, SI_SUB_LOCK, SI_ORDER_SECOND, name##_init, NULL)

struct delayed_work {
	struct work_struct work;
	struct {
		struct callout callout;
		struct mtx mtx;
		int	expires;
	} timer;
};

#define	DECLARE_DELAYED_WORK(name, fn)					\
	struct delayed_work name;					\
	static void name##_init(void *arg)				\
	{								\
		linux_init_delayed_work(&name, fn);			\
	}								\
	SYSINIT(name, SI_SUB_LOCK, SI_ORDER_SECOND, name##_init, NULL)

static inline struct delayed_work *
to_delayed_work(struct work_struct *work)
{
	return (container_of(work, struct delayed_work, work));
}

#define	INIT_WORK(work, fn)						\
do {									\
	(work)->func = (fn);						\
	(work)->work_queue = NULL;					\
	atomic_set(&(work)->state, 0);					\
	TASK_INIT(&(work)->work_task, 0, linux_work_fn, (work));	\
} while (0)

#define	INIT_WORK_ONSTACK(work, fn) \
	INIT_WORK(work, fn)

#define	INIT_DELAYED_WORK(dwork, fn) \
	linux_init_delayed_work(dwork, fn)

#define	INIT_DELAYED_WORK_ONSTACK(dwork, fn) \
	linux_init_delayed_work(dwork, fn)

#define	INIT_DEFERRABLE_WORK(dwork, fn) \
	INIT_DELAYED_WORK(dwork, fn)

#define	flush_scheduled_work() \
	taskqueue_drain_all(system_wq->taskqueue)

#define	queue_work(wq, work) \
	linux_queue_work_on(WORK_CPU_UNBOUND, wq, work)

#define	schedule_work(work) \
	linux_queue_work_on(WORK_CPU_UNBOUND, system_wq, work)

#define	queue_delayed_work(wq, dwork, delay) \
	linux_queue_delayed_work_on(WORK_CPU_UNBOUND, wq, dwork, delay)

#define	schedule_delayed_work_on(cpu, dwork, delay) \
	linux_queue_delayed_work_on(cpu, system_wq, dwork, delay)

#define	queue_work_on(cpu, wq, work) \
	linux_queue_work_on(cpu, wq, work)

#define	schedule_delayed_work(dwork, delay) \
	linux_queue_delayed_work_on(WORK_CPU_UNBOUND, system_wq, dwork, delay)

#define	queue_delayed_work_on(cpu, wq, dwork, delay) \
	linux_queue_delayed_work_on(cpu, wq, dwork, delay)

#define	create_singlethread_workqueue(name) \
	linux_create_workqueue_common(name, 1)

#define	create_workqueue(name) \
	linux_create_workqueue_common(name, mp_ncpus)

#define	alloc_ordered_workqueue(name, flags) \
	linux_create_workqueue_common(name, 1)

#define	alloc_workqueue(name, flags, max_active) \
	linux_create_workqueue_common(name, max_active)

#define	flush_workqueue(wq) \
	taskqueue_drain_all((wq)->taskqueue)

#define	drain_workqueue(wq) do {		\
	atomic_inc(&(wq)->draining);		\
	taskqueue_drain_all((wq)->taskqueue);	\
	atomic_dec(&(wq)->draining);		\
} while (0)

#define	mod_delayed_work(wq, dwork, delay) ({		\
	bool __retval;					\
	__retval = linux_cancel_delayed_work(dwork);	\
	linux_queue_delayed_work_on(WORK_CPU_UNBOUND,	\
	    wq, dwork, delay);				\
	__retval;					\
})

#define	delayed_work_pending(dwork) \
	linux_work_pending(&(dwork)->work)

#define	cancel_delayed_work(dwork) \
	linux_cancel_delayed_work(dwork)

#define	cancel_work_sync(work) \
	linux_cancel_work_sync(work)

#define	cancel_delayed_work_sync(dwork) \
	linux_cancel_delayed_work_sync(dwork)

#define	flush_work(work) \
	linux_flush_work(work)

#define	flush_delayed_work(dwork) \
	linux_flush_delayed_work(dwork)

#define	work_pending(work) \
	linux_work_pending(work)

#define	work_busy(work) \
	linux_work_busy(work)

#define	destroy_work_on_stack(work) \
	do { } while (0)

#define	destroy_delayed_work_on_stack(dwork) \
	do { } while (0)

#define	destroy_workqueue(wq) \
	linux_destroy_workqueue(wq)

#define	current_work() \
	linux_current_work()

/* prototypes */

extern struct workqueue_struct *system_wq;
extern struct workqueue_struct *system_long_wq;
extern struct workqueue_struct *system_unbound_wq;
extern struct workqueue_struct *system_highpri_wq;
extern struct workqueue_struct *system_power_efficient_wq;

extern void linux_init_delayed_work(struct delayed_work *, work_func_t);
extern void linux_work_fn(void *, int);
extern void linux_delayed_work_fn(void *, int);
extern struct workqueue_struct *linux_create_workqueue_common(const char *, int);
extern void linux_destroy_workqueue(struct workqueue_struct *);
extern bool linux_queue_work_on(int cpu, struct workqueue_struct *, struct work_struct *);
extern bool linux_queue_delayed_work_on(int cpu, struct workqueue_struct *,
    struct delayed_work *, unsigned delay);
extern bool linux_cancel_delayed_work(struct delayed_work *);
extern bool linux_cancel_work_sync(struct work_struct *);
extern bool linux_cancel_delayed_work_sync(struct delayed_work *);
extern bool linux_flush_work(struct work_struct *);
extern bool linux_flush_delayed_work(struct delayed_work *);
extern bool linux_work_pending(struct work_struct *);
extern bool linux_work_busy(struct work_struct *);
extern struct work_struct *linux_current_work(void);

#endif					/* _LINUX_WORKQUEUE_H_ */
