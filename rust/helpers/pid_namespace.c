// SPDX-License-Identifier: GPL-2.0

#include <linux/pid_namespace.h>
#include <linux/cleanup.h>

struct pid_namespace *rust_helper_get_pid_ns(struct pid_namespace *ns)
{
	return get_pid_ns(ns);
}

void rust_helper_put_pid_ns(struct pid_namespace *ns)
{
	put_pid_ns(ns);
}

/* Get a reference on a task's pid namespace. */
struct pid_namespace *rust_helper_task_get_pid_ns(struct task_struct *task)
{
	struct pid_namespace *pid_ns;

	guard(rcu)();
	pid_ns = task_active_pid_ns(task);
	if (pid_ns)
		get_pid_ns(pid_ns);
	return pid_ns;
}
