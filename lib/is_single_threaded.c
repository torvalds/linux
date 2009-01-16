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

/**
 * is_single_threaded - Determine if a thread group is single-threaded or not
 * @p: A task in the thread group in question
 *
 * This returns true if the thread group to which a task belongs is single
 * threaded, false if it is not.
 */
bool is_single_threaded(struct task_struct *p)
{
	struct task_struct *g, *t;
	struct mm_struct *mm = p->mm;

	if (atomic_read(&p->signal->count) != 1)
		goto no;

	if (atomic_read(&p->mm->mm_users) != 1) {
		read_lock(&tasklist_lock);
		do_each_thread(g, t) {
			if (t->mm == mm && t != p)
				goto no_unlock;
		} while_each_thread(g, t);
		read_unlock(&tasklist_lock);
	}

	return true;

no_unlock:
	read_unlock(&tasklist_lock);
no:
	return false;
}
