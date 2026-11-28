/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2022, Microsoft Corporation.
 *
 * Authors:
 *   Beau Belgrave <beaub@linux.microsoft.com>
 */

#ifndef _LINUX_USER_EVENTS_H
#define _LINUX_USER_EVENTS_H

#include <linux/list.h>
#include <linux/refcount.h>
#include <linux/mm_types.h>
#include <linux/workqueue.h>
#include <uapi/linux/user_events.h>

#ifdef CONFIG_USER_EVENTS
struct user_event_mm {
	struct list_head	mms_link;
	struct list_head	enablers;
	struct mm_struct	*mm;
	/* Used for one-shot lists, protected by event_mutex */
	struct user_event_mm	*next;
	refcount_t		refcnt;
	refcount_t		tasks;
	struct rcu_work		put_rwork;
};

int user_event_mm_dup(struct task_struct *t, struct user_event_mm *old_mm);

extern void user_event_mm_remove(struct task_struct *t);

static inline int user_events_fork(struct task_struct *t, u64 clone_flags)
{
	struct user_event_mm *old_mm;

	if (!t || !current->user_event_mm)
		return 0;

	old_mm = current->user_event_mm;

	if (clone_flags & CLONE_VM) {
		t->user_event_mm = old_mm;
		refcount_inc(&old_mm->tasks);
		return 0;
	}

	return user_event_mm_dup(t, old_mm);
}

static inline void user_events_execve(struct task_struct *t)
{
	if (!t || !t->user_event_mm)
		return;

	user_event_mm_remove(t);
}

static inline void user_events_exit(struct task_struct *t)
{
	if (!t || !t->user_event_mm)
		return;

	user_event_mm_remove(t);
}
#else
static inline int user_events_fork(struct task_struct *t, u64 clone_flags)
{
	return 0;
}

static inline void user_events_execve(struct task_struct *t)
{
}

static inline void user_events_exit(struct task_struct *t)
{
}
#endif /* CONFIG_USER_EVENTS */

#endif /* _LINUX_USER_EVENTS_H */
