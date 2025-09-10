// SPDX-License-Identifier: GPL-2.0

#include <linux/kernel.h>
#include <linux/sched/task.h>

void rust_helper_might_resched(void)
{
	might_resched();
}

struct task_struct *rust_helper_get_current(void)
{
	return current;
}

void rust_helper_get_task_struct(struct task_struct *t)
{
	get_task_struct(t);
}

void rust_helper_put_task_struct(struct task_struct *t)
{
	put_task_struct(t);
}

kuid_t rust_helper_task_uid(struct task_struct *task)
{
	return task_uid(task);
}

kuid_t rust_helper_task_euid(struct task_struct *task)
{
	return task_euid(task);
}

#ifndef CONFIG_USER_NS
uid_t rust_helper_from_kuid(struct user_namespace *to, kuid_t uid)
{
	return from_kuid(to, uid);
}
#endif /* CONFIG_USER_NS */

bool rust_helper_uid_eq(kuid_t left, kuid_t right)
{
	return uid_eq(left, right);
}

kuid_t rust_helper_current_euid(void)
{
	return current_euid();
}

struct user_namespace *rust_helper_current_user_ns(void)
{
	return current_user_ns();
}

pid_t rust_helper_task_tgid_nr_ns(struct task_struct *tsk,
				  struct pid_namespace *ns)
{
	return task_tgid_nr_ns(tsk, ns);
}
