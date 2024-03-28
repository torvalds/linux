// SPDX-License-Identifier: GPL-2.0-only
/******************************************************************************
*******************************************************************************
**
**  Copyright (C) Sistina Software, Inc.  1997-2003  All rights reserved.
**  Copyright (C) 2004-2010 Red Hat, Inc.  All rights reserved.
**
**
*******************************************************************************
******************************************************************************/

#include <trace/events/dlm.h>

#include "dlm_internal.h"
#include "lvb_table.h"
#include "memory.h"
#include "lock.h"
#include "user.h"
#include "ast.h"

void dlm_release_callback(struct kref *ref)
{
	struct dlm_callback *cb = container_of(ref, struct dlm_callback, ref);

	dlm_free_cb(cb);
}

void dlm_callback_set_last_ptr(struct dlm_callback **from,
			       struct dlm_callback *to)
{
	if (*from)
		kref_put(&(*from)->ref, dlm_release_callback);

	if (to)
		kref_get(&to->ref);

	*from = to;
}

int dlm_enqueue_lkb_callback(struct dlm_lkb *lkb, uint32_t flags, int mode,
			     int status, uint32_t sbflags)
{
	struct dlm_ls *ls = lkb->lkb_resource->res_ls;
	int rv = DLM_ENQUEUE_CALLBACK_SUCCESS;
	struct dlm_callback *cb;
	int copy_lvb = 0;
	int prev_mode;

	if (flags & DLM_CB_BAST) {
		/* if cb is a bast, it should be skipped if the blocking mode is
		 * compatible with the last granted mode
		 */
		if (lkb->lkb_last_cast) {
			if (dlm_modes_compat(mode, lkb->lkb_last_cast->mode)) {
				log_debug(ls, "skip %x bast mode %d for cast mode %d",
					  lkb->lkb_id, mode,
					  lkb->lkb_last_cast->mode);
				goto out;
			}
		}

		/*
		 * Suppress some redundant basts here, do more on removal.
		 * Don't even add a bast if the callback just before it
		 * is a bast for the same mode or a more restrictive mode.
		 * (the addional > PR check is needed for PR/CW inversion)
		 */
		if (lkb->lkb_last_cb && lkb->lkb_last_cb->flags & DLM_CB_BAST) {
			prev_mode = lkb->lkb_last_cb->mode;

			if ((prev_mode == mode) ||
			    (prev_mode > mode && prev_mode > DLM_LOCK_PR)) {
				log_debug(ls, "skip %x add bast mode %d for bast mode %d",
					  lkb->lkb_id, mode, prev_mode);
				goto out;
			}
		}
	} else if (flags & DLM_CB_CAST) {
		if (test_bit(DLM_DFL_USER_BIT, &lkb->lkb_dflags)) {
			if (lkb->lkb_last_cast)
				prev_mode = lkb->lkb_last_cb->mode;
			else
				prev_mode = -1;

			if (!status && lkb->lkb_lksb->sb_lvbptr &&
			    dlm_lvb_operations[prev_mode + 1][mode + 1])
				copy_lvb = 1;
		}
	}

	cb = dlm_allocate_cb();
	if (!cb) {
		rv = DLM_ENQUEUE_CALLBACK_FAILURE;
		goto out;
	}

	cb->flags = flags;
	cb->mode = mode;
	cb->sb_status = status;
	cb->sb_flags = (sbflags & 0x000000FF);
	cb->copy_lvb = copy_lvb;
	kref_init(&cb->ref);
	if (!test_and_set_bit(DLM_IFL_CB_PENDING_BIT, &lkb->lkb_iflags))
		rv = DLM_ENQUEUE_CALLBACK_NEED_SCHED;

	list_add_tail(&cb->list, &lkb->lkb_callbacks);

	if (flags & DLM_CB_CAST)
		dlm_callback_set_last_ptr(&lkb->lkb_last_cast, cb);

	dlm_callback_set_last_ptr(&lkb->lkb_last_cb, cb);

 out:
	return rv;
}

int dlm_dequeue_lkb_callback(struct dlm_lkb *lkb, struct dlm_callback **cb)
{
	/* oldest undelivered cb is callbacks first entry */
	*cb = list_first_entry_or_null(&lkb->lkb_callbacks,
				       struct dlm_callback, list);
	if (!*cb)
		return DLM_DEQUEUE_CALLBACK_EMPTY;

	/* remove it from callbacks so shift others down */
	list_del(&(*cb)->list);
	if (list_empty(&lkb->lkb_callbacks))
		return DLM_DEQUEUE_CALLBACK_LAST;

	return DLM_DEQUEUE_CALLBACK_SUCCESS;
}

void dlm_add_cb(struct dlm_lkb *lkb, uint32_t flags, int mode, int status,
		uint32_t sbflags)
{
	struct dlm_ls *ls = lkb->lkb_resource->res_ls;
	int rv;

	if (test_bit(DLM_DFL_USER_BIT, &lkb->lkb_dflags)) {
		dlm_user_add_ast(lkb, flags, mode, status, sbflags);
		return;
	}

	spin_lock(&lkb->lkb_cb_lock);
	rv = dlm_enqueue_lkb_callback(lkb, flags, mode, status, sbflags);
	switch (rv) {
	case DLM_ENQUEUE_CALLBACK_NEED_SCHED:
		kref_get(&lkb->lkb_ref);

		spin_lock(&ls->ls_cb_lock);
		if (test_bit(LSFL_CB_DELAY, &ls->ls_flags)) {
			list_add(&lkb->lkb_cb_list, &ls->ls_cb_delay);
		} else {
			queue_work(ls->ls_callback_wq, &lkb->lkb_cb_work);
		}
		spin_unlock(&ls->ls_cb_lock);
		break;
	case DLM_ENQUEUE_CALLBACK_FAILURE:
		WARN_ON_ONCE(1);
		break;
	case DLM_ENQUEUE_CALLBACK_SUCCESS:
		break;
	default:
		WARN_ON_ONCE(1);
		break;
	}
	spin_unlock(&lkb->lkb_cb_lock);
}

void dlm_callback_work(struct work_struct *work)
{
	struct dlm_lkb *lkb = container_of(work, struct dlm_lkb, lkb_cb_work);
	struct dlm_ls *ls = lkb->lkb_resource->res_ls;
	void (*castfn) (void *astparam);
	void (*bastfn) (void *astparam, int mode);
	struct dlm_callback *cb;
	int rv;

	spin_lock(&lkb->lkb_cb_lock);
	rv = dlm_dequeue_lkb_callback(lkb, &cb);
	if (WARN_ON_ONCE(rv == DLM_DEQUEUE_CALLBACK_EMPTY)) {
		clear_bit(DLM_IFL_CB_PENDING_BIT, &lkb->lkb_iflags);
		spin_unlock(&lkb->lkb_cb_lock);
		goto out;
	}
	spin_unlock(&lkb->lkb_cb_lock);

	for (;;) {
		castfn = lkb->lkb_astfn;
		bastfn = lkb->lkb_bastfn;

		if (cb->flags & DLM_CB_BAST) {
			trace_dlm_bast(ls, lkb, cb->mode);
			lkb->lkb_last_bast_time = ktime_get();
			lkb->lkb_last_bast_mode = cb->mode;
			bastfn(lkb->lkb_astparam, cb->mode);
		} else if (cb->flags & DLM_CB_CAST) {
			lkb->lkb_lksb->sb_status = cb->sb_status;
			lkb->lkb_lksb->sb_flags = cb->sb_flags;
			trace_dlm_ast(ls, lkb);
			lkb->lkb_last_cast_time = ktime_get();
			castfn(lkb->lkb_astparam);
		}

		kref_put(&cb->ref, dlm_release_callback);

		spin_lock(&lkb->lkb_cb_lock);
		rv = dlm_dequeue_lkb_callback(lkb, &cb);
		if (rv == DLM_DEQUEUE_CALLBACK_EMPTY) {
			clear_bit(DLM_IFL_CB_PENDING_BIT, &lkb->lkb_iflags);
			spin_unlock(&lkb->lkb_cb_lock);
			break;
		}
		spin_unlock(&lkb->lkb_cb_lock);
	}

out:
	/* undo kref_get from dlm_add_callback, may cause lkb to be freed */
	dlm_put_lkb(lkb);
}

int dlm_callback_start(struct dlm_ls *ls)
{
	ls->ls_callback_wq = alloc_workqueue("dlm_callback",
					     WQ_HIGHPRI | WQ_MEM_RECLAIM, 0);
	if (!ls->ls_callback_wq) {
		log_print("can't start dlm_callback workqueue");
		return -ENOMEM;
	}
	return 0;
}

void dlm_callback_stop(struct dlm_ls *ls)
{
	if (ls->ls_callback_wq)
		destroy_workqueue(ls->ls_callback_wq);
}

void dlm_callback_suspend(struct dlm_ls *ls)
{
	if (ls->ls_callback_wq) {
		spin_lock(&ls->ls_cb_lock);
		set_bit(LSFL_CB_DELAY, &ls->ls_flags);
		spin_unlock(&ls->ls_cb_lock);

		flush_workqueue(ls->ls_callback_wq);
	}
}

#define MAX_CB_QUEUE 25

void dlm_callback_resume(struct dlm_ls *ls)
{
	struct dlm_lkb *lkb, *safe;
	int count = 0, sum = 0;
	bool empty;

	if (!ls->ls_callback_wq)
		return;

more:
	spin_lock(&ls->ls_cb_lock);
	list_for_each_entry_safe(lkb, safe, &ls->ls_cb_delay, lkb_cb_list) {
		list_del_init(&lkb->lkb_cb_list);
		queue_work(ls->ls_callback_wq, &lkb->lkb_cb_work);
		count++;
		if (count == MAX_CB_QUEUE)
			break;
	}
	empty = list_empty(&ls->ls_cb_delay);
	if (empty)
		clear_bit(LSFL_CB_DELAY, &ls->ls_flags);
	spin_unlock(&ls->ls_cb_lock);

	sum += count;
	if (!empty) {
		count = 0;
		cond_resched();
		goto more;
	}

	if (sum)
		log_rinfo(ls, "%s %d", __func__, sum);
}

