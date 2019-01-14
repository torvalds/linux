/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_SWAIT_H
#define _LINUX_SWAIT_H

#include <linux/list.h>
#include <linux/stddef.h>
#include <linux/spinlock.h>
#include <linux/wait.h>
#include <asm/current.h>

/*
 * BROKEN wait-queues.
 *
 * These "simple" wait-queues are broken garbage, and should never be
 * used. The comments below claim that they are "similar" to regular
 * wait-queues, but the semantics are actually completely different, and
 * every single user we have ever had has been buggy (or pointless).
 *
 * A "swake_up_one()" only wakes up _one_ waiter, which is not at all what
 * "wake_up()" does, and has led to problems. In other cases, it has
 * been fine, because there's only ever one waiter (kvm), but in that
 * case gthe whole "simple" wait-queue is just pointless to begin with,
 * since there is no "queue". Use "wake_up_process()" with a direct
 * pointer instead.
 *
 * While these are very similar to regular wait queues (wait.h) the most
 * important difference is that the simple waitqueue allows for deterministic
 * behaviour -- IOW it has strictly bounded IRQ and lock hold times.
 *
 * Mainly, this is accomplished by two things. Firstly not allowing swake_up_all
 * from IRQ disabled, and dropping the lock upon every wakeup, giving a higher
 * priority task a chance to run.
 *
 * Secondly, we had to drop a fair number of features of the other waitqueue
 * code; notably:
 *
 *  - mixing INTERRUPTIBLE and UNINTERRUPTIBLE sleeps on the same waitqueue;
 *    all wakeups are TASK_NORMAL in order to avoid O(n) lookups for the right
 *    sleeper state.
 *
 *  - the !exclusive mode; because that leads to O(n) wakeups, everything is
 *    exclusive.
 *
 *  - custom wake callback functions; because you cannot give any guarantees
 *    about random code. This also allows swait to be used in RT, such that
 *    raw spinlock can be used for the swait queue head.
 *
 * As a side effect of these; the data structures are slimmer albeit more ad-hoc.
 * For all the above, note that simple wait queues should _only_ be used under
 * very specific realtime constraints -- it is best to stick with the regular
 * wait queues in most cases.
 */

struct task_struct;

struct swait_queue_head {
	raw_spinlock_t		lock;
	struct list_head	task_list;
};

struct swait_queue {
	struct task_struct	*task;
	struct list_head	task_list;
};

#define __SWAITQUEUE_INITIALIZER(name) {				\
	.task		= current,					\
	.task_list	= LIST_HEAD_INIT((name).task_list),		\
}

#define DECLARE_SWAITQUEUE(name)					\
	struct swait_queue name = __SWAITQUEUE_INITIALIZER(name)

#define __SWAIT_QUEUE_HEAD_INITIALIZER(name) {				\
	.lock		= __RAW_SPIN_LOCK_UNLOCKED(name.lock),		\
	.task_list	= LIST_HEAD_INIT((name).task_list),		\
}

#define DECLARE_SWAIT_QUEUE_HEAD(name)					\
	struct swait_queue_head name = __SWAIT_QUEUE_HEAD_INITIALIZER(name)

extern void __init_swait_queue_head(struct swait_queue_head *q, const char *name,
				    struct lock_class_key *key);

#define init_swait_queue_head(q)				\
	do {							\
		static struct lock_class_key __key;		\
		__init_swait_queue_head((q), #q, &__key);	\
	} while (0)

#ifdef CONFIG_LOCKDEP
# define __SWAIT_QUEUE_HEAD_INIT_ONSTACK(name)			\
	({ init_swait_queue_head(&name); name; })
# define DECLARE_SWAIT_QUEUE_HEAD_ONSTACK(name)			\
	struct swait_queue_head name = __SWAIT_QUEUE_HEAD_INIT_ONSTACK(name)
#else
# define DECLARE_SWAIT_QUEUE_HEAD_ONSTACK(name)			\
	DECLARE_SWAIT_QUEUE_HEAD(name)
#endif

/**
 * swait_active -- locklessly test for waiters on the queue
 * @wq: the waitqueue to test for waiters
 *
 * returns true if the wait list is not empty
 *
 * NOTE: this function is lockless and requires care, incorrect usage _will_
 * lead to sporadic and non-obvious failure.
 *
 * NOTE2: this function has the same above implications as regular waitqueues.
 *
 * Use either while holding swait_queue_head::lock or when used for wakeups
 * with an extra smp_mb() like:
 *
 *      CPU0 - waker                    CPU1 - waiter
 *
 *                                      for (;;) {
 *      @cond = true;                     prepare_to_swait_exclusive(&wq_head, &wait, state);
 *      smp_mb();                         // smp_mb() from set_current_state()
 *      if (swait_active(wq_head))        if (@cond)
 *        wake_up(wq_head);                      break;
 *                                        schedule();
 *                                      }
 *                                      finish_swait(&wq_head, &wait);
 *
 * Because without the explicit smp_mb() it's possible for the
 * swait_active() load to get hoisted over the @cond store such that we'll
 * observe an empty wait list while the waiter might not observe @cond.
 * This, in turn, can trigger missing wakeups.
 *
 * Also note that this 'optimization' trades a spin_lock() for an smp_mb(),
 * which (when the lock is uncontended) are of roughly equal cost.
 */
static inline int swait_active(struct swait_queue_head *wq)
{
	return !list_empty(&wq->task_list);
}

/**
 * swq_has_sleeper - check if there are any waiting processes
 * @wq: the waitqueue to test for waiters
 *
 * Returns true if @wq has waiting processes
 *
 * Please refer to the comment for swait_active.
 */
static inline bool swq_has_sleeper(struct swait_queue_head *wq)
{
	/*
	 * We need to be sure we are in sync with the list_add()
	 * modifications to the wait queue (task_list).
	 *
	 * This memory barrier should be paired with one on the
	 * waiting side.
	 */
	smp_mb();
	return swait_active(wq);
}

extern void swake_up_one(struct swait_queue_head *q);
extern void swake_up_all(struct swait_queue_head *q);
extern void swake_up_locked(struct swait_queue_head *q);

extern void prepare_to_swait_exclusive(struct swait_queue_head *q, struct swait_queue *wait, int state);
extern long prepare_to_swait_event(struct swait_queue_head *q, struct swait_queue *wait, int state);

extern void __finish_swait(struct swait_queue_head *q, struct swait_queue *wait);
extern void finish_swait(struct swait_queue_head *q, struct swait_queue *wait);

/* as per ___wait_event() but for swait, therefore "exclusive == 1" */
#define ___swait_event(wq, condition, state, ret, cmd)			\
({									\
	__label__ __out;						\
	struct swait_queue __wait;					\
	long __ret = ret;						\
									\
	INIT_LIST_HEAD(&__wait.task_list);				\
	for (;;) {							\
		long __int = prepare_to_swait_event(&wq, &__wait, state);\
									\
		if (condition)						\
			break;						\
									\
		if (___wait_is_interruptible(state) && __int) {		\
			__ret = __int;					\
			goto __out;					\
		}							\
									\
		cmd;							\
	}								\
	finish_swait(&wq, &__wait);					\
__out:	__ret;								\
})

#define __swait_event(wq, condition)					\
	(void)___swait_event(wq, condition, TASK_UNINTERRUPTIBLE, 0,	\
			    schedule())

#define swait_event_exclusive(wq, condition)				\
do {									\
	if (condition)							\
		break;							\
	__swait_event(wq, condition);					\
} while (0)

#define __swait_event_timeout(wq, condition, timeout)			\
	___swait_event(wq, ___wait_cond_timeout(condition),		\
		      TASK_UNINTERRUPTIBLE, timeout,			\
		      __ret = schedule_timeout(__ret))

#define swait_event_timeout_exclusive(wq, condition, timeout)		\
({									\
	long __ret = timeout;						\
	if (!___wait_cond_timeout(condition))				\
		__ret = __swait_event_timeout(wq, condition, timeout);	\
	__ret;								\
})

#define __swait_event_interruptible(wq, condition)			\
	___swait_event(wq, condition, TASK_INTERRUPTIBLE, 0,		\
		      schedule())

#define swait_event_interruptible_exclusive(wq, condition)		\
({									\
	int __ret = 0;							\
	if (!(condition))						\
		__ret = __swait_event_interruptible(wq, condition);	\
	__ret;								\
})

#define __swait_event_interruptible_timeout(wq, condition, timeout)	\
	___swait_event(wq, ___wait_cond_timeout(condition),		\
		      TASK_INTERRUPTIBLE, timeout,			\
		      __ret = schedule_timeout(__ret))

#define swait_event_interruptible_timeout_exclusive(wq, condition, timeout)\
({									\
	long __ret = timeout;						\
	if (!___wait_cond_timeout(condition))				\
		__ret = __swait_event_interruptible_timeout(wq,		\
						condition, timeout);	\
	__ret;								\
})

#define __swait_event_idle(wq, condition)				\
	(void)___swait_event(wq, condition, TASK_IDLE, 0, schedule())

/**
 * swait_event_idle_exclusive - wait without system load contribution
 * @wq: the waitqueue to wait on
 * @condition: a C expression for the event to wait for
 *
 * The process is put to sleep (TASK_IDLE) until the @condition evaluates to
 * true. The @condition is checked each time the waitqueue @wq is woken up.
 *
 * This function is mostly used when a kthread or workqueue waits for some
 * condition and doesn't want to contribute to system load. Signals are
 * ignored.
 */
#define swait_event_idle_exclusive(wq, condition)			\
do {									\
	if (condition)							\
		break;							\
	__swait_event_idle(wq, condition);				\
} while (0)

#define __swait_event_idle_timeout(wq, condition, timeout)		\
	___swait_event(wq, ___wait_cond_timeout(condition),		\
		       TASK_IDLE, timeout,				\
		       __ret = schedule_timeout(__ret))

/**
 * swait_event_idle_timeout_exclusive - wait up to timeout without load contribution
 * @wq: the waitqueue to wait on
 * @condition: a C expression for the event to wait for
 * @timeout: timeout at which we'll give up in jiffies
 *
 * The process is put to sleep (TASK_IDLE) until the @condition evaluates to
 * true. The @condition is checked each time the waitqueue @wq is woken up.
 *
 * This function is mostly used when a kthread or workqueue waits for some
 * condition and doesn't want to contribute to system load. Signals are
 * ignored.
 *
 * Returns:
 * 0 if the @condition evaluated to %false after the @timeout elapsed,
 * 1 if the @condition evaluated to %true after the @timeout elapsed,
 * or the remaining jiffies (at least 1) if the @condition evaluated
 * to %true before the @timeout elapsed.
 */
#define swait_event_idle_timeout_exclusive(wq, condition, timeout)	\
({									\
	long __ret = timeout;						\
	if (!___wait_cond_timeout(condition))				\
		__ret = __swait_event_idle_timeout(wq,			\
						   condition, timeout);	\
	__ret;								\
})

#endif /* _LINUX_SWAIT_H */
