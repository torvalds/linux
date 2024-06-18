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

static void dlm_callback_work(struct work_struct *work)
{
	struct dlm_callback *cb = container_of(work, struct dlm_callback, work);

	if (cb->flags & DLM_CB_BAST) {
		trace_dlm_bast(cb->ls_id, cb->lkb_id, cb->mode, cb->res_name,
			       cb->res_length);
		cb->bastfn(cb->astparam, cb->mode);
	} else if (cb->flags & DLM_CB_CAST) {
		trace_dlm_ast(cb->ls_id, cb->lkb_id, cb->sb_status,
			      cb->sb_flags, cb->res_name, cb->res_length);
		cb->lkb_lksb->sb_status = cb->sb_status;
		cb->lkb_lksb->sb_flags = cb->sb_flags;
		cb->astfn(cb->astparam);
	}

	dlm_free_cb(cb);
}

int dlm_queue_lkb_callback(struct dlm_lkb *lkb, uint32_t flags, int mode,
			   int status, uint32_t sbflags,
			   struct dlm_callback **cb)
{
	struct dlm_rsb *rsb = lkb->lkb_resource;
	int rv = DLM_ENQUEUE_CALLBACK_SUCCESS;
	struct dlm_ls *ls = rsb->res_ls;
	int copy_lvb = 0;
	int prev_mode;

	if (flags & DLM_CB_BAST) {
		/* if cb is a bast, it should be skipped if the blocking mode is
		 * compatible with the last granted mode
		 */
		if (lkb->lkb_last_cast_cb_mode != -1) {
			if (dlm_modes_compat(mode, lkb->lkb_last_cast_cb_mode)) {
				log_debug(ls, "skip %x bast mode %d for cast mode %d",
					  lkb->lkb_id, mode,
					  lkb->lkb_last_cast_cb_mode);
				goto out;
			}
		}

		/*
		 * Suppress some redundant basts here, do more on removal.
		 * Don't even add a bast if the callback just before it
		 * is a bast for the same mode or a more restrictive mode.
		 * (the addional > PR check is needed for PR/CW inversion)
		 */
		if (lkb->lkb_last_cb_mode != -1 &&
		    lkb->lkb_last_cb_flags & DLM_CB_BAST) {
			prev_mode = lkb->lkb_last_cb_mode;

			if ((prev_mode == mode) ||
			    (prev_mode > mode && prev_mode > DLM_LOCK_PR)) {
				log_debug(ls, "skip %x add bast mode %d for bast mode %d",
					  lkb->lkb_id, mode, prev_mode);
				goto out;
			}
		}

		lkb->lkb_last_bast_time = ktime_get();
		lkb->lkb_last_bast_cb_mode = mode;
	} else if (flags & DLM_CB_CAST) {
		if (test_bit(DLM_DFL_USER_BIT, &lkb->lkb_dflags)) {
			prev_mode = lkb->lkb_last_cast_cb_mode;

			if (!status && lkb->lkb_lksb->sb_lvbptr &&
			    dlm_lvb_operations[prev_mode + 1][mode + 1])
				copy_lvb = 1;
		}

		lkb->lkb_last_cast_cb_mode = mode;
		lkb->lkb_last_cast_time = ktime_get();
	}

	lkb->lkb_last_cb_mode = mode;
	lkb->lkb_last_cb_flags = flags;

	*cb = dlm_allocate_cb();
	if (!*cb) {
		rv = DLM_ENQUEUE_CALLBACK_FAILURE;
		goto out;
	}

	/* for tracing */
	(*cb)->lkb_id = lkb->lkb_id;
	(*cb)->ls_id = ls->ls_global_id;
	memcpy((*cb)->res_name, rsb->res_name, rsb->res_length);
	(*cb)->res_length = rsb->res_length;

	(*cb)->flags = flags;
	(*cb)->mode = mode;
	(*cb)->sb_status = status;
	(*cb)->sb_flags = (sbflags & 0x000000FF);
	(*cb)->copy_lvb = copy_lvb;
	(*cb)->lkb_lksb = lkb->lkb_lksb;

	rv = DLM_ENQUEUE_CALLBACK_NEED_SCHED;

out:
	return rv;
}

void dlm_add_cb(struct dlm_lkb *lkb, uint32_t flags, int mode, int status,
		  uint32_t sbflags)
{
	struct dlm_ls *ls = lkb->lkb_resource->res_ls;
	struct dlm_callback *cb;
	int rv;

	if (test_bit(DLM_DFL_USER_BIT, &lkb->lkb_dflags)) {
		dlm_user_add_ast(lkb, flags, mode, status, sbflags);
		return;
	}

	rv = dlm_queue_lkb_callback(lkb, flags, mode, status, sbflags,
				    &cb);
	switch (rv) {
	case DLM_ENQUEUE_CALLBACK_NEED_SCHED:
		cb->astfn = lkb->lkb_astfn;
		cb->bastfn = lkb->lkb_bastfn;
		cb->astparam = lkb->lkb_astparam;
		INIT_WORK(&cb->work, dlm_callback_work);

		spin_lock_bh(&ls->ls_cb_lock);
		if (test_bit(LSFL_CB_DELAY, &ls->ls_flags))
			list_add(&cb->list, &ls->ls_cb_delay);
		else
			queue_work(ls->ls_callback_wq, &cb->work);
		spin_unlock_bh(&ls->ls_cb_lock);
		break;
	case DLM_ENQUEUE_CALLBACK_SUCCESS:
		break;
	case DLM_ENQUEUE_CALLBACK_FAILURE:
		fallthrough;
	default:
		WARN_ON_ONCE(1);
		break;
	}
}

int dlm_callback_start(struct dlm_ls *ls)
{
	ls->ls_callback_wq = alloc_ordered_workqueue("dlm_callback",
						     WQ_HIGHPRI | WQ_MEM_RECLAIM);
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
		spin_lock_bh(&ls->ls_cb_lock);
		set_bit(LSFL_CB_DELAY, &ls->ls_flags);
		spin_unlock_bh(&ls->ls_cb_lock);

		flush_workqueue(ls->ls_callback_wq);
	}
}

#define MAX_CB_QUEUE 25

void dlm_callback_resume(struct dlm_ls *ls)
{
	struct dlm_callback *cb, *safe;
	int count = 0, sum = 0;
	bool empty;

	if (!ls->ls_callback_wq)
		return;

more:
	spin_lock_bh(&ls->ls_cb_lock);
	list_for_each_entry_safe(cb, safe, &ls->ls_cb_delay, list) {
		list_del(&cb->list);
		queue_work(ls->ls_callback_wq, &cb->work);
		count++;
		if (count == MAX_CB_QUEUE)
			break;
	}
	empty = list_empty(&ls->ls_cb_delay);
	if (empty)
		clear_bit(LSFL_CB_DELAY, &ls->ls_flags);
	spin_unlock_bh(&ls->ls_cb_lock);

	sum += count;
	if (!empty) {
		count = 0;
		cond_resched();
		goto more;
	}

	if (sum)
		log_rinfo(ls, "%s %d", __func__, sum);
}

