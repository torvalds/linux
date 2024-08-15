/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2020 Collabora Ltd.
 */
#ifndef _SYSCALL_USER_DISPATCH_H
#define _SYSCALL_USER_DISPATCH_H

#include <linux/thread_info.h>

#ifdef CONFIG_GENERIC_ENTRY

struct syscall_user_dispatch {
	char __user	*selector;
	unsigned long	offset;
	unsigned long	len;
	bool		on_dispatch;
};

int set_syscall_user_dispatch(unsigned long mode, unsigned long offset,
			      unsigned long len, char __user *selector);

#define clear_syscall_work_syscall_user_dispatch(tsk) \
	clear_task_syscall_work(tsk, SYSCALL_USER_DISPATCH)

#else
struct syscall_user_dispatch {};

static inline int set_syscall_user_dispatch(unsigned long mode, unsigned long offset,
					    unsigned long len, char __user *selector)
{
	return -EINVAL;
}

static inline void clear_syscall_work_syscall_user_dispatch(struct task_struct *tsk)
{
}

#endif /* CONFIG_GENERIC_ENTRY */

#endif /* _SYSCALL_USER_DISPATCH_H */
