/*
 * Copyright (C) Sistina Software, Inc.  1997-2003 All rights reserved.
 * Copyright (C) 2004-2005 Red Hat, Inc.  All rights reserved.
 *
 * This copyrighted material is made available to anyone wishing to use,
 * modify, copy, or redistribute it subject to the terms and conditions
 * of the GNU General Public License version 2.
 */

#include "lock_dlm.h"

static inline int no_work(struct gdlm_ls *ls)
{
	int ret;

	spin_lock(&ls->async_lock);
	ret = list_empty(&ls->submit);
	spin_unlock(&ls->async_lock);

	return ret;
}

static int gdlm_thread(void *data)
{
	struct gdlm_ls *ls = (struct gdlm_ls *) data;
	struct gdlm_lock *lp = NULL;

	while (!kthread_should_stop()) {
		wait_event_interruptible(ls->thread_wait,
				!no_work(ls) || kthread_should_stop());

		spin_lock(&ls->async_lock);

		if (!list_empty(&ls->submit)) {
			lp = list_entry(ls->submit.next, struct gdlm_lock,
					delay_list);
			list_del_init(&lp->delay_list);
			spin_unlock(&ls->async_lock);
			gdlm_do_lock(lp);
			spin_lock(&ls->async_lock);
		}
		spin_unlock(&ls->async_lock);
	}

	return 0;
}

int gdlm_init_threads(struct gdlm_ls *ls)
{
	struct task_struct *p;
	int error;

	p = kthread_run(gdlm_thread, ls, "lock_dlm");
	error = IS_ERR(p);
	if (error) {
		log_error("can't start lock_dlm thread %d", error);
		return error;
	}
	ls->thread = p;

	return 0;
}

void gdlm_release_threads(struct gdlm_ls *ls)
{
	kthread_stop(ls->thread);
}

