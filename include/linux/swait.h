#ifndef _LINUX_SWAIT_H
#define _LINUX_SWAIT_H

#include <linux/list.h>
#include <linux/stddef.h>
#include <linux/spinlock.h>
#include <asm/current.h>

/*
 * Simple wait queues
 *
 * While these are very similar to the other/complex wait queues (wait.h) the
 * most important difference is that the simple waitqueue allows for
 * deterministic behaviour -- IOW it has strictly bounded IRQ and lock hold
 * times.
 *
 * In order to make this so, we had to drop a fair number of features of the
 * other waitqueue code; notably:
 *
 *  - mixing INTERRUPTIBLE and UNINTERRUPTIBLE sleeps on the same waitqueue;
 *    all wakeups are TASK_NORMAL in order to avoid O(n) lookups for the right
 *    sleeper state.
 *
 *  - the exclusive mode; because this requires preserving the list order
 *    and this is hard.
 *
 *  - custom wake functions; because you cannot give any guarantees about
 *    random code.
 *
 * As a side effect of this; the data structures are slimmer.
 *
 * One would recommend using this wait queue where possible.
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

static inline int swait_active(struct swait_queue_head *q)
{
	return !list_empty(&q->task_list);
}

extern void swake_up(struct swait_queue_head *q);
extern void swake_up_all(struct swait_queue_head *q);
extern void swake_up_locked(struct swait_queue_head *q);

extern void __prepare_to_swait(struct swait_queue_head *q, struct swait_queue *wait);
extern void prepare_to_swait(struct swait_queue_head *q, struct swait_queue *wait, int state);
extern long prepare_to_swait_event(struct swait_queue_head *q, struct swait_queue *wait, int state);

extern void __finish_swait(struct swait_queue_head *q, struct swait_queue *wait);
extern void finish_swait(struct swait_queue_head *q, struct swait_queue *wait);

/* as per ___wait_event() but for swait, therefore "exclusive == 0" */
#define ___swait_event(wq, condition, state, ret, cmd)			\
({									\
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
			break;						\
		}							\
									\
		cmd;							\
	}								\
	finish_swait(&wq, &__wait);					\
	__ret;								\
})

#define __swait_event(wq, condition)					\
	(void)___swait_event(wq, condition, TASK_UNINTERRUPTIBLE, 0,	\
			    schedule())

#define swait_event(wq, condition)					\
do {									\
	if (condition)							\
		break;							\
	__swait_event(wq, condition);					\
} while (0)

#define __swait_event_timeout(wq, condition, timeout)			\
	___swait_event(wq, ___wait_cond_timeout(condition),		\
		      TASK_UNINTERRUPTIBLE, timeout,			\
		      __ret = schedule_timeout(__ret))

#define swait_event_timeout(wq, condition, timeout)			\
({									\
	long __ret = timeout;						\
	if (!___wait_cond_timeout(condition))				\
		__ret = __swait_event_timeout(wq, condition, timeout);	\
	__ret;								\
})

#define __swait_event_interruptible(wq, condition)			\
	___swait_event(wq, condition, TASK_INTERRUPTIBLE, 0,		\
		      schedule())

#define swait_event_interruptible(wq, condition)			\
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

#define swait_event_interruptible_timeout(wq, condition, timeout)	\
({									\
	long __ret = timeout;						\
	if (!___wait_cond_timeout(condition))				\
		__ret = __swait_event_interruptible_timeout(wq,		\
						condition, timeout);	\
	__ret;								\
})

#endif /* _LINUX_SWAIT_H */
