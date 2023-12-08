/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_SCHED_WAKE_Q_H
#define _LINUX_SCHED_WAKE_Q_H

/*
 * Wake-queues are lists of tasks with a pending wakeup, whose
 * callers have already marked the task as woken internally,
 * and can thus carry on. A common use case is being able to
 * do the wakeups once the corresponding user lock as been
 * released.
 *
 * We hold reference to each task in the list across the wakeup,
 * thus guaranteeing that the memory is still valid by the time
 * the actual wakeups are performed in wake_up_q().
 *
 * One per task suffices, because there's never a need for a task to be
 * in two wake queues simultaneously; it is forbidden to abandon a task
 * in a wake queue (a call to wake_up_q() _must_ follow), so if a task is
 * already in a wake queue, the wakeup will happen soon and the second
 * waker can just skip it.
 *
 * The DEFINE_WAKE_Q macro declares and initializes the list head.
 * wake_up_q() does NOT reinitialize the list; it's expected to be
 * called near the end of a function. Otherwise, the list can be
 * re-initialized for later re-use by wake_q_init().
 *
 * NOTE that this can cause spurious wakeups. schedule() callers
 * must ensure the call is done inside a loop, confirming that the
 * wakeup condition has in fact occurred.
 *
 * NOTE that there is no guarantee the wakeup will happen any later than the
 * wake_q_add() location. Therefore task must be ready to be woken at the
 * location of the wake_q_add().
 */

#include <linux/sched.h>

struct wake_q_head {
	struct wake_q_node *first;
	struct wake_q_node **lastp;
	int count;
};

#define WAKE_Q_TAIL ((struct wake_q_node *) 0x01)

#define WAKE_Q_HEAD_INITIALIZER(name)				\
	{ WAKE_Q_TAIL, &name.first }

#define DEFINE_WAKE_Q(name)					\
	struct wake_q_head name = WAKE_Q_HEAD_INITIALIZER(name)

static inline void wake_q_init(struct wake_q_head *head)
{
	head->first = WAKE_Q_TAIL;
	head->lastp = &head->first;
	head->count = 0;
}

static inline bool wake_q_empty(struct wake_q_head *head)
{
	return head->first == WAKE_Q_TAIL;
}

extern void wake_q_add(struct wake_q_head *head, struct task_struct *task);
extern void wake_q_add_safe(struct wake_q_head *head, struct task_struct *task);
extern void wake_up_q(struct wake_q_head *head);

#endif /* _LINUX_SCHED_WAKE_Q_H */
