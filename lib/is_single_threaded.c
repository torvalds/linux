/* Function to determine if a thread group is single threaded or not
 *
 * Copyright (C) 2008 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 * - Derived from security/selinux/hooks.c
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public Licence
 * as published by the Free Software Foundation; either version
 * 2 of the Licence, or (at your option) any later version.
 */

#include <linux/sched.h>

/*
 * Returns true if the task does not share ->mm with another thread/process.
 */
bool is_single_threaded(struct task_struct *task)
{
	struct mm_struct *mm = task->mm;
	struct task_struct *p, *t;
	bool ret;

	might_sleep();

	if (atomic_read(&task->signal->live) != 1)
		return false;

	if (atomic_read(&mm->mm_users) == 1)
		return true;

	ret = false;
	down_write(&mm->mmap_sem);
	rcu_read_lock();
	for_each_process(p) {
		if (unlikely(p->flags & PF_KTHREAD))
			continue;
		if (unlikely(p == task->group_leader))
			continue;

		t = p;
		do {
			if (unlikely(t->mm == mm))
				goto found;
			if (likely(t->mm))
				break;
		} while_each_thread(p, t);
	}
	ret = true;
found:
	rcu_read_unlock();
	up_write(&mm->mmap_sem);

	return ret;
}
