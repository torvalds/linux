// SPDX-License-Identifier: GPL-2.0

#include <linux/kernel.h>
#include <linux/sched/task.h>

__rust_helper void rust_helper_might_resched(void)
{
	might_resched();
}

__rust_helper struct task_struct *rust_helper_get_current(void)
{
	return current;
}

__rust_helper void rust_helper_get_task_struct(struct task_struct *t)
{
	get_task_struct(t);
}

__rust_helper void rust_helper_put_task_struct(struct task_struct *t)
{
	put_task_struct(t);
}

__rust_helper kuid_t rust_helper_task_uid(struct task_struct *task)
{
	return task_uid(task);
}

__rust_helper kuid_t rust_helper_task_euid(struct task_struct *task)
{
	return task_euid(task);
}

#ifndef CONFIG_USER_NS
__rust_helper uid_t rust_helper_from_kuid(struct user_namespace *to, kuid_t uid)
{
	return from_kuid(to, uid);
}
#endif /* CONFIG_USER_NS */

__rust_helper bool rust_helper_uid_eq(kuid_t left, kuid_t right)
{
	return uid_eq(left, right);
}

__rust_helper kuid_t rust_helper_current_euid(void)
{
	return current_euid();
}

__rust_helper struct user_namespace *rust_helper_current_user_ns(void)
{
	return current_user_ns();
}

__rust_helper pid_t rust_helper_task_tgid_nr_ns(struct task_struct *tsk,
						struct pid_namespace *ns)
{
	return task_tgid_nr_ns(tsk, ns);
}
