/*-
 * Copyright (c) 2010 Isilon Systems, Inc.
 * Copyright (c) 2010 iX Systems, Inc.
 * Copyright (c) 2010 Panasas, Inc.
 * Copyright (c) 2013, 2014 Mellanox Technologies, Ltd.
 * Copyright (c) 2017 Mark Johnston <markj@FreeBSD.org>
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

#ifndef _LINUX_WAIT_H_
#define	_LINUX_WAIT_H_

#include <linux/compiler.h>
#include <linux/list.h>
#include <linux/spinlock.h>

#include <asm/atomic.h>

#include <sys/param.h>
#include <sys/systm.h>

#define	SKIP_SLEEP() (SCHEDULER_STOPPED() || kdb_active)

#define	might_sleep()							\
	WITNESS_WARN(WARN_GIANTOK | WARN_SLEEPOK, NULL, "might_sleep()")

#define	might_sleep_if(cond) do { \
	if (cond) { might_sleep(); } \
} while (0)

struct wait_queue;
struct wait_queue_head;

#define	wait_queue_entry wait_queue

typedef struct wait_queue wait_queue_t;
typedef struct wait_queue_entry wait_queue_entry_t;
typedef struct wait_queue_head wait_queue_head_t;

typedef int wait_queue_func_t(wait_queue_t *, unsigned int, int, void *);

/*
 * Many API consumers directly reference these fields and those of
 * wait_queue_head.
 */
struct wait_queue {
	unsigned int flags;	/* always 0 */
	void *private;
	wait_queue_func_t *func;
	union {
		struct list_head task_list; /* < v4.13 */
		struct list_head entry; /* >= v4.13 */
	};
};

struct wait_queue_head {
	spinlock_t lock;
	union {
		struct list_head task_list; /* < v4.13 */
		struct list_head head; /* >= v4.13 */
	};
};

/*
 * This function is referenced by at least one DRM driver, so it may not be
 * renamed and furthermore must be the default wait queue callback.
 */
extern wait_queue_func_t autoremove_wake_function;
extern wait_queue_func_t default_wake_function;

#define	DEFINE_WAIT_FUNC(name, function)				\
	wait_queue_t name = {						\
		.private = current,					\
		.func = function,					\
		.task_list = LINUX_LIST_HEAD_INIT(name.task_list)	\
	}

#define	DEFINE_WAIT(name) \
	DEFINE_WAIT_FUNC(name, autoremove_wake_function)

#define	DECLARE_WAITQUEUE(name, task)					\
	wait_queue_t name = {						\
		.private = task,					\
		.task_list = LINUX_LIST_HEAD_INIT(name.task_list)	\
	}

#define	DECLARE_WAIT_QUEUE_HEAD(name)					\
	wait_queue_head_t name = {					\
		.task_list = LINUX_LIST_HEAD_INIT(name.task_list),	\
	};								\
	MTX_SYSINIT(name, &(name).lock.m, spin_lock_name("wqhead"), MTX_DEF)

#define	init_waitqueue_head(wqh) do {					\
	mtx_init(&(wqh)->lock.m, spin_lock_name("wqhead"),		\
	    NULL, MTX_DEF | MTX_NEW | MTX_NOWITNESS);			\
	INIT_LIST_HEAD(&(wqh)->task_list);				\
} while (0)

void linux_init_wait_entry(wait_queue_t *, int);
void linux_wake_up(wait_queue_head_t *, unsigned int, int, bool);

#define	init_wait_entry(wq, flags)					\
        linux_init_wait_entry(wq, flags)
#define	wake_up(wqh)							\
	linux_wake_up(wqh, TASK_NORMAL, 1, false)
#define	wake_up_all(wqh)						\
	linux_wake_up(wqh, TASK_NORMAL, 0, false)
#define	wake_up_locked(wqh)						\
	linux_wake_up(wqh, TASK_NORMAL, 1, true)
#define	wake_up_all_locked(wqh)						\
	linux_wake_up(wqh, TASK_NORMAL, 0, true)
#define	wake_up_interruptible(wqh)					\
	linux_wake_up(wqh, TASK_INTERRUPTIBLE, 1, false)
#define	wake_up_interruptible_all(wqh)					\
	linux_wake_up(wqh, TASK_INTERRUPTIBLE, 0, false)

int linux_wait_event_common(wait_queue_head_t *, wait_queue_t *, int,
    unsigned int, spinlock_t *);

/*
 * Returns -ERESTARTSYS for a signal, 0 if cond is false after timeout, 1 if
 * cond is true after timeout, remaining jiffies (> 0) if cond is true before
 * timeout.
 */
#define	__wait_event_common(wqh, cond, timeout, state, lock) ({	\
	DEFINE_WAIT(__wq);					\
	const int __timeout = ((int)(timeout)) < 1 ? 1 : (timeout);	\
	int __start = ticks;					\
	int __ret = 0;						\
								\
	for (;;) {						\
		linux_prepare_to_wait(&(wqh), &__wq, state);	\
		if (cond)					\
			break;					\
		__ret = linux_wait_event_common(&(wqh), &__wq,	\
		    __timeout, state, lock);			\
		if (__ret != 0)					\
			break;					\
	}							\
	linux_finish_wait(&(wqh), &__wq);			\
	if (__timeout != MAX_SCHEDULE_TIMEOUT) {		\
		if (__ret == -EWOULDBLOCK)			\
			__ret = !!(cond);			\
		else if (__ret != -ERESTARTSYS) {		\
			__ret = __timeout + __start - ticks;	\
			/* range check return value */		\
			if (__ret < 1)				\
				__ret = 1;			\
			else if (__ret > __timeout)		\
				__ret = __timeout;		\
		}						\
	}							\
	__ret;							\
})

#define	wait_event(wqh, cond) do {					\
	(void) __wait_event_common(wqh, cond, MAX_SCHEDULE_TIMEOUT,	\
	    TASK_UNINTERRUPTIBLE, NULL);				\
} while (0)

#define	wait_event_timeout(wqh, cond, timeout) ({			\
	__wait_event_common(wqh, cond, timeout, TASK_UNINTERRUPTIBLE,	\
	    NULL);							\
})

#define	wait_event_killable(wqh, cond) ({				\
	__wait_event_common(wqh, cond, MAX_SCHEDULE_TIMEOUT,		\
	    TASK_INTERRUPTIBLE, NULL);					\
})

#define	wait_event_interruptible(wqh, cond) ({				\
	__wait_event_common(wqh, cond, MAX_SCHEDULE_TIMEOUT,		\
	    TASK_INTERRUPTIBLE, NULL);					\
})

#define	wait_event_interruptible_timeout(wqh, cond, timeout) ({		\
	__wait_event_common(wqh, cond, timeout, TASK_INTERRUPTIBLE,	\
	    NULL);							\
})

/*
 * Wait queue is already locked.
 */
#define	wait_event_interruptible_locked(wqh, cond) ({			\
	int __ret;							\
									\
	spin_unlock(&(wqh).lock);					\
	__ret = __wait_event_common(wqh, cond, MAX_SCHEDULE_TIMEOUT,	\
	    TASK_INTERRUPTIBLE, NULL);					\
	spin_lock(&(wqh).lock);						\
	__ret;								\
})

/*
 * The passed spinlock is held when testing the condition.
 */
#define	wait_event_interruptible_lock_irq(wqh, cond, lock) ({		\
	__wait_event_common(wqh, cond, MAX_SCHEDULE_TIMEOUT,		\
	    TASK_INTERRUPTIBLE, &(lock));				\
})

/*
 * The passed spinlock is held when testing the condition.
 */
#define	wait_event_lock_irq(wqh, cond, lock) ({			\
	__wait_event_common(wqh, cond, MAX_SCHEDULE_TIMEOUT,	\
	    TASK_UNINTERRUPTIBLE, &(lock));			\
})

static inline void
__add_wait_queue(wait_queue_head_t *wqh, wait_queue_t *wq)
{
	list_add(&wq->task_list, &wqh->task_list);
}

static inline void
add_wait_queue(wait_queue_head_t *wqh, wait_queue_t *wq)
{

	spin_lock(&wqh->lock);
	__add_wait_queue(wqh, wq);
	spin_unlock(&wqh->lock);
}

static inline void
__add_wait_queue_tail(wait_queue_head_t *wqh, wait_queue_t *wq)
{
	list_add_tail(&wq->task_list, &wqh->task_list);
}

static inline void
__add_wait_queue_entry_tail(wait_queue_head_t *wqh, wait_queue_entry_t *wq)
{
        list_add_tail(&wq->entry, &wqh->head);
}

static inline void
__remove_wait_queue(wait_queue_head_t *wqh, wait_queue_t *wq)
{
	list_del(&wq->task_list);
}

static inline void
remove_wait_queue(wait_queue_head_t *wqh, wait_queue_t *wq)
{

	spin_lock(&wqh->lock);
	__remove_wait_queue(wqh, wq);
	spin_unlock(&wqh->lock);
}

bool linux_waitqueue_active(wait_queue_head_t *);

#define	waitqueue_active(wqh)		linux_waitqueue_active(wqh)

void linux_prepare_to_wait(wait_queue_head_t *, wait_queue_t *, int);
void linux_finish_wait(wait_queue_head_t *, wait_queue_t *);

#define	prepare_to_wait(wqh, wq, state)	linux_prepare_to_wait(wqh, wq, state)
#define	finish_wait(wqh, wq)		linux_finish_wait(wqh, wq)

void linux_wake_up_bit(void *, int);
int linux_wait_on_bit_timeout(unsigned long *, int, unsigned int, int);
void linux_wake_up_atomic_t(atomic_t *);
int linux_wait_on_atomic_t(atomic_t *, unsigned int);

#define	wake_up_bit(word, bit)		linux_wake_up_bit(word, bit)
#define	wait_on_bit(word, bit, state)					\
	linux_wait_on_bit_timeout(word, bit, state, MAX_SCHEDULE_TIMEOUT)
#define	wait_on_bit_timeout(word, bit, state, timeout)			\
	linux_wait_on_bit_timeout(word, bit, state, timeout)
#define	wake_up_atomic_t(a)		linux_wake_up_atomic_t(a)
/*
 * All existing callers have a cb that just schedule()s. To avoid adding
 * complexity, just emulate that internally. The prototype is different so that
 * callers must be manually modified; a cb that does something other than call
 * schedule() will require special treatment.
 */
#define	wait_on_atomic_t(a, state)	linux_wait_on_atomic_t(a, state)

struct task_struct;
bool linux_wake_up_state(struct task_struct *, unsigned int);

#define	wake_up_process(task)		linux_wake_up_state(task, TASK_NORMAL)
#define	wake_up_state(task, state)	linux_wake_up_state(task, state)

#endif /* _LINUX_WAIT_H_ */
