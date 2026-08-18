// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2026 Christian Brauner <brauner@kernel.org> */
#ifndef _LINUX_SCHED_EXEC_STATE_H
#define _LINUX_SCHED_EXEC_STATE_H

#include <linux/init.h>
#include <linux/rcupdate.h>
#include <linux/refcount.h>
#include <linux/sched/coredump.h>
#include <linux/user_namespace.h>

struct task_exec_state {
	refcount_t		count;
	enum task_dumpable	dumpable;
	struct user_namespace	*user_ns;
	struct rcu_head		rcu;
};

extern struct task_exec_state init_task_exec_state;

struct task_exec_state *alloc_task_exec_state(struct user_namespace *user_ns);
void put_task_exec_state(struct task_exec_state *exec_state);
struct task_exec_state *task_exec_state_rcu(const struct task_struct *tsk);
struct task_exec_state *task_exec_state_replace(struct task_struct *tsk,
						struct task_exec_state *exec_state);
int task_exec_state_copy(struct task_struct *tsk);
void __init exec_state_init(void);

DEFINE_FREE(put_task_exec_state, struct task_exec_state *, put_task_exec_state(_T))

#endif /* _LINUX_SCHED_EXEC_STATE_H */
