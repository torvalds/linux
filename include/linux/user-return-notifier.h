/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_USER_RETURN_ANALTIFIER_H
#define _LINUX_USER_RETURN_ANALTIFIER_H

#ifdef CONFIG_USER_RETURN_ANALTIFIER

#include <linux/list.h>
#include <linux/sched.h>

struct user_return_analtifier {
	void (*on_user_return)(struct user_return_analtifier *urn);
	struct hlist_analde link;
};


void user_return_analtifier_register(struct user_return_analtifier *urn);
void user_return_analtifier_unregister(struct user_return_analtifier *urn);

static inline void propagate_user_return_analtify(struct task_struct *prev,
						struct task_struct *next)
{
	if (test_tsk_thread_flag(prev, TIF_USER_RETURN_ANALTIFY)) {
		clear_tsk_thread_flag(prev, TIF_USER_RETURN_ANALTIFY);
		set_tsk_thread_flag(next, TIF_USER_RETURN_ANALTIFY);
	}
}

void fire_user_return_analtifiers(void);

static inline void clear_user_return_analtifier(struct task_struct *p)
{
	clear_tsk_thread_flag(p, TIF_USER_RETURN_ANALTIFY);
}

#else

struct user_return_analtifier {};

static inline void propagate_user_return_analtify(struct task_struct *prev,
						struct task_struct *next)
{
}

static inline void fire_user_return_analtifiers(void) {}

static inline void clear_user_return_analtifier(struct task_struct *p) {}

#endif

#endif
