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

static void dlm_run_callback(uint32_t ls_id, uint32_t lkb_id, int8_t mode,
			     uint32_t flags, uint8_t sb_flags, int sb_status,
			     struct dlm_lksb *lksb,
			     void (*astfn)(void *astparam),
			     void (*bastfn)(void *astparam, int mode),
			     void *astparam, const char *res_name,
			     size_t res_length)
{
	if (flags & DLM_CB_BAST) {
		trace_dlm_bast(ls_id, lkb_id, mode, res_name, res_length);
		bastfn(astparam, mode);
	} else if (flags & DLM_CB_CAST) {
		trace_dlm_ast(ls_id, lkb_id, sb_status, sb_flags, res_name,
			      res_length);
		lksb->sb_status = sb_status;
		lksb->sb_flags = sb_flags;
		astfn(astparam);
	}
}

static void dlm_do_callback(struct dlm_callback *cb)
{
	dlm_run_callback(cb->ls_id, cb->lkb_id, cb->mode, cb->flags,
			 cb->sb_flags, cb->sb_status, cb->lkb_lksb,
			 cb->astfn, cb->bastfn, cb->astparam,
			 cb->res_name, cb->res_length);
	dlm_free_cb(cb);
}

static void dlm_callback_work(struct work_struct *work)
{
	struct dlm_callback *cb = container_of(work, struct dlm_callback, work);

	dlm_do_callback(cb);
}

bool dlm_may_skip_callback(struct dlm_lkb *lkb, uint32_t flags, int mode,
			   int status, uint32_t sbflags, int *copy_lvb)
{
	struct dlm_rsb *rsb = lkb->lkb_resource;
	struct dlm_ls *ls = rsb->res_ls;
	int prev_mode;

	if (copy_lvb)
		*copy_lvb = 0;

	if (flags & DLM_CB_BAST) {
		/* if cb is a bast, it should be skipped if the blocking mode is
		 * compatible with the last granted mode
		 */
		if (lkb->lkb_last_cast_cb_mode != -1) {
			if (dlm_modes_compat(mode, lkb->lkb_last_cast_cb_mode)) {
				log_debug(ls, "skip %x bast mode %d for cast mode %d",
					  lkb->lkb_id, mode,
					  lkb->lkb_last_cast_cb_mode);
				return true;
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
				return true;
			}
		}

		lkb->lkb_last_bast_time = ktime_get();
		lkb->lkb_last_bast_cb_mode = mode;
	} else if (flags & DLM_CB_CAST) {
		if (test_bit(DLM_DFL_USER_BIT, &lkb->lkb_dflags)) {
			prev_mode = lkb->lkb_last_cast_cb_mode;

			if (!status && lkb->lkb_lksb->sb_lvbptr &&
			    dlm_lvb_operations[prev_mode + 1][mode + 1]) {
				if (copy_lvb)
					*copy_lvb = 1;
			}
		}

		lkb->lkb_last_cast_cb_mode = mode;
		lkb->lkb_last_cast_time = ktime_get();
	}

	lkb->lkb_last_cb_mode = mode;
	lkb->lkb_last_cb_flags = flags;

	return false;
}

int dlm_get_cb(struct dlm_lkb *lkb, uint32_t flags, int mode,
	       int status, uint32_t sbflags,
	       struct dlm_callback **cb)
{
	struct dlm_rsb *rsb = lkb->lkb_resource;
	struct dlm_ls *ls = rsb->res_ls;

	*cb = dlm_allocate_cb();
	if (WARN_ON_ONCE(!*cb))
		return -ENOMEM;

	/* for tracing */
	(*cb)->lkb_id = lkb->lkb_id;
	(*cb)->ls_id = ls->ls_global_id;
	memcpy((*cb)->res_name, rsb->res_name, rsb->res_length);
	(*cb)->res_length = rsb->res_length;

	(*cb)->flags = flags;
	(*cb)->mode = mode;
	(*cb)->sb_status = status;
	(*cb)->sb_flags = (sbflags & 0x000000FF);
	(*cb)->lkb_lksb = lkb->lkb_lksb;

	return 0;
}

static int dlm_get_queue_cb(struct dlm_lkb *lkb, uint32_t flags, int mode,
			    int status, uint32_t sbflags,
			    struct dlm_callback **cb)
{
	int rv;

	rv = dlm_get_cb(lkb, flags, mode, status, sbflags, cb);
	if (rv)
		return rv;

	(*cb)->astfn = lkb->lkb_astfn;
	(*cb)->bastfn = lkb->lkb_bastfn;
	(*cb)->astparam = lkb->lkb_astparam;
	INIT_WORK(&(*cb)->work, dlm_callback_work);

	return 0;
}

void dlm_add_cb(struct dlm_lkb *lkb, uint32_t flags, int mode, int status,
		uint32_t sbflags)
{
	struct dlm_rsb *rsb = lkb->lkb_resource;
	struct dlm_ls *ls = rsb->res_ls;
	struct dlm_callback *cb;
	int rv;

	if (test_bit(DLM_DFL_USER_BIT, &lkb->lkb_dflags)) {
		dlm_user_add_ast(lkb, flags, mode, status, sbflags);
		return;
	}

	if (dlm_may_skip_callback(lkb, flags, mode, status, sbflags, NULL))
		return;

	spin_lock_bh(&ls->ls_cb_lock);
	if (test_bit(LSFL_CB_DELAY, &ls->ls_flags)) {
		rv = dlm_get_queue_cb(lkb, flags, mode, status, sbflags, &cb);
		if (!rv)
			list_add(&cb->list, &ls->ls_cb_delay);
	} else {
		if (test_bit(LSFL_SOFTIRQ, &ls->ls_flags)) {
			dlm_run_callback(ls->ls_global_id, lkb->lkb_id, mode, flags,
					 sbflags, status, lkb->lkb_lksb,
					 lkb->lkb_astfn, lkb->lkb_bastfn,
					 lkb->lkb_astparam, rsb->res_name,
					 rsb->res_length);
		} else {
			rv = dlm_get_queue_cb(lkb, flags, mode, status, sbflags, &cb);
			if (!rv)
				queue_work(ls->ls_callback_wq, &cb->work);
		}
	}
	spin_unlock_bh(&ls->ls_cb_lock);
}

int dlm_callback_start(struct dlm_ls *ls)
{
	if (!test_bit(LSFL_FS, &ls->ls_flags) ||
	    test_bit(LSFL_SOFTIRQ, &ls->ls_flags))
		return 0;

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
	if (!test_bit(LSFL_FS, &ls->ls_flags))
		return;

	spin_lock_bh(&ls->ls_cb_lock);
	set_bit(LSFL_CB_DELAY, &ls->ls_flags);
	spin_unlock_bh(&ls->ls_cb_lock);

	if (ls->ls_callback_wq)
		flush_workqueue(ls->ls_callback_wq);
}

#define MAX_CB_QUEUE 25

void dlm_callback_resume(struct dlm_ls *ls)
{
	struct dlm_callback *cb, *safe;
	int count = 0, sum = 0;
	bool empty;

	if (!test_bit(LSFL_FS, &ls->ls_flags))
		return;

more:
	spin_lock_bh(&ls->ls_cb_lock);
	list_for_each_entry_safe(cb, safe, &ls->ls_cb_delay, list) {
		list_del(&cb->list);
		if (test_bit(LSFL_SOFTIRQ, &ls->ls_flags))
			dlm_do_callback(cb);
		else
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

