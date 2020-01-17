/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_USER_RETURN_NOTIFIER_H
#define _LINUX_USER_RETURN_NOTIFIER_H

#ifdef CONFIG_USER_RETURN_NOTIFIER

#include <linux/list.h>
#include <linux/sched.h>

struct user_return_yestifier {
	void (*on_user_return)(struct user_return_yestifier *urn);
	struct hlist_yesde link;
};


void user_return_yestifier_register(struct user_return_yestifier *urn);
void user_return_yestifier_unregister(struct user_return_yestifier *urn);

static inline void propagate_user_return_yestify(struct task_struct *prev,
						struct task_struct *next)
{
	if (test_tsk_thread_flag(prev, TIF_USER_RETURN_NOTIFY)) {
		clear_tsk_thread_flag(prev, TIF_USER_RETURN_NOTIFY);
		set_tsk_thread_flag(next, TIF_USER_RETURN_NOTIFY);
	}
}

void fire_user_return_yestifiers(void);

static inline void clear_user_return_yestifier(struct task_struct *p)
{
	clear_tsk_thread_flag(p, TIF_USER_RETURN_NOTIFY);
}

#else

struct user_return_yestifier {};

static inline void propagate_user_return_yestify(struct task_struct *prev,
						struct task_struct *next)
{
}

static inline void fire_user_return_yestifiers(void) {}

static inline void clear_user_return_yestifier(struct task_struct *p) {}

#endif

#endif
