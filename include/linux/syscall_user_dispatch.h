/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2020 Collabora Ltd.
 */
#ifndef _SYSCALL_USER_DISPATCH_H
#define _SYSCALL_USER_DISPATCH_H

#include <linux/thread_info.h>
#include <linux/syscall_user_dispatch_types.h>

#ifdef CONFIG_GENERIC_ENTRY

int set_syscall_user_dispatch(unsigned long mode, unsigned long offset,
			      unsigned long len, char __user *selector);

#define clear_syscall_work_syscall_user_dispatch(tsk) \
	clear_task_syscall_work(tsk, SYSCALL_USER_DISPATCH)

int syscall_user_dispatch_get_config(struct task_struct *task, unsigned long size,
				     void __user *data);

int syscall_user_dispatch_set_config(struct task_struct *task, unsigned long size,
				     void __user *data);

#else

static inline int set_syscall_user_dispatch(unsigned long mode, unsigned long offset,
					    unsigned long len, char __user *selector)
{
	return -EINVAL;
}

static inline void clear_syscall_work_syscall_user_dispatch(struct task_struct *tsk)
{
}

static inline int syscall_user_dispatch_get_config(struct task_struct *task,
						   unsigned long size, void __user *data)
{
	return -EINVAL;
}

static inline int syscall_user_dispatch_set_config(struct task_struct *task,
						   unsigned long size, void __user *data)
{
	return -EINVAL;
}

#endif /* CONFIG_GENERIC_ENTRY */

#endif /* _SYSCALL_USER_DISPATCH_H */
