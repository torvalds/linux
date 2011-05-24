/******************************************************************************
*******************************************************************************
**
**  Copyright (C) Sistina Software, Inc.  1997-2003  All rights reserved.
**  Copyright (C) 2004-2010 Red Hat, Inc.  All rights reserved.
**
**  This copyrighted material is made available to anyone wishing to use,
**  modify, copy, or redistribute it subject to the terms and conditions
**  of the GNU General Public License v.2.
**
*******************************************************************************
******************************************************************************/

#include "dlm_internal.h"
#include "lock.h"
#include "user.h"
#include "ast.h"

#define WAKE_ASTS  0

static uint64_t			ast_seq_count;
static struct list_head		ast_queue;
static spinlock_t		ast_queue_lock;
static struct task_struct *	astd_task;
static unsigned long		astd_wakeflags;
static struct mutex		astd_running;


static void dlm_dump_lkb_callbacks(struct dlm_lkb *lkb)
{
	int i;

	log_print("last_bast %x %llu flags %x mode %d sb %d %x",
		  lkb->lkb_id,
		  (unsigned long long)lkb->lkb_last_bast.seq,
		  lkb->lkb_last_bast.flags,
		  lkb->lkb_last_bast.mode,
		  lkb->lkb_last_bast.sb_status,
		  lkb->lkb_last_bast.sb_flags);

	log_print("last_cast %x %llu flags %x mode %d sb %d %x",
		  lkb->lkb_id,
		  (unsigned long long)lkb->lkb_last_cast.seq,
		  lkb->lkb_last_cast.flags,
		  lkb->lkb_last_cast.mode,
		  lkb->lkb_last_cast.sb_status,
		  lkb->lkb_last_cast.sb_flags);

	for (i = 0; i < DLM_CALLBACKS_SIZE; i++) {
		log_print("cb %x %llu flags %x mode %d sb %d %x",
			  lkb->lkb_id,
			  (unsigned long long)lkb->lkb_callbacks[i].seq,
			  lkb->lkb_callbacks[i].flags,
			  lkb->lkb_callbacks[i].mode,
			  lkb->lkb_callbacks[i].sb_status,
			  lkb->lkb_callbacks[i].sb_flags);
	}
}

void dlm_del_ast(struct dlm_lkb *lkb)
{
	spin_lock(&ast_queue_lock);
	if (!list_empty(&lkb->lkb_astqueue))
		list_del_init(&lkb->lkb_astqueue);
	spin_unlock(&ast_queue_lock);
}

int dlm_add_lkb_callback(struct dlm_lkb *lkb, uint32_t flags, int mode,
			 int status, uint32_t sbflags, uint64_t seq)
{
	struct dlm_ls *ls = lkb->lkb_resource->res_ls;
	uint64_t prev_seq;
	int prev_mode;
	int i;

	for (i = 0; i < DLM_CALLBACKS_SIZE; i++) {
		if (lkb->lkb_callbacks[i].seq)
			continue;

		/*
		 * Suppress some redundant basts here, do more on removal.
		 * Don't even add a bast if the callback just before it
		 * is a bast for the same mode or a more restrictive mode.
		 * (the addional > PR check is needed for PR/CW inversion)
		 */

		if ((i > 0) && (flags & DLM_CB_BAST) &&
		    (lkb->lkb_callbacks[i-1].flags & DLM_CB_BAST)) {

			prev_seq = lkb->lkb_callbacks[i-1].seq;
			prev_mode = lkb->lkb_callbacks[i-1].mode;

			if ((prev_mode == mode) ||
			    (prev_mode > mode && prev_mode > DLM_LOCK_PR)) {

				log_debug(ls, "skip %x add bast %llu mode %d "
					  "for bast %llu mode %d",
					  lkb->lkb_id,
					  (unsigned long long)seq,
					  mode,
					  (unsigned long long)prev_seq,
					  prev_mode);
				return 0;
			}
		}

		lkb->lkb_callbacks[i].seq = seq;
		lkb->lkb_callbacks[i].flags = flags;
		lkb->lkb_callbacks[i].mode = mode;
		lkb->lkb_callbacks[i].sb_status = status;
		lkb->lkb_callbacks[i].sb_flags = (sbflags & 0x000000FF);
		break;
	}

	if (i == DLM_CALLBACKS_SIZE) {
		log_error(ls, "no callbacks %x %llu flags %x mode %d sb %d %x",
			  lkb->lkb_id, (unsigned long long)seq,
			  flags, mode, status, sbflags);
		dlm_dump_lkb_callbacks(lkb);
		return -1;
	}

	return 0;
}

int dlm_rem_lkb_callback(struct dlm_ls *ls, struct dlm_lkb *lkb,
			 struct dlm_callback *cb, int *resid)
{
	int i;

	*resid = 0;

	if (!lkb->lkb_callbacks[0].seq)
		return -ENOENT;

	/* oldest undelivered cb is callbacks[0] */

	memcpy(cb, &lkb->lkb_callbacks[0], sizeof(struct dlm_callback));
	memset(&lkb->lkb_callbacks[0], 0, sizeof(struct dlm_callback));

	/* shift others down */

	for (i = 1; i < DLM_CALLBACKS_SIZE; i++) {
		if (!lkb->lkb_callbacks[i].seq)
			break;
		memcpy(&lkb->lkb_callbacks[i-1], &lkb->lkb_callbacks[i],
		       sizeof(struct dlm_callback));
		memset(&lkb->lkb_callbacks[i], 0, sizeof(struct dlm_callback));
		(*resid)++;
	}

	/* if cb is a bast, it should be skipped if the blocking mode is
	   compatible with the last granted mode */

	if ((cb->flags & DLM_CB_BAST) && lkb->lkb_last_cast.seq) {
		if (dlm_modes_compat(cb->mode, lkb->lkb_last_cast.mode)) {
			cb->flags |= DLM_CB_SKIP;

			log_debug(ls, "skip %x bast %llu mode %d "
				  "for cast %llu mode %d",
				  lkb->lkb_id,
				  (unsigned long long)cb->seq,
				  cb->mode,
				  (unsigned long long)lkb->lkb_last_cast.seq,
				  lkb->lkb_last_cast.mode);
			return 0;
		}
	}

	if (cb->flags & DLM_CB_CAST) {
		memcpy(&lkb->lkb_last_cast, cb, sizeof(struct dlm_callback));
		lkb->lkb_last_cast_time = ktime_get();
	}

	if (cb->flags & DLM_CB_BAST) {
		memcpy(&lkb->lkb_last_bast, cb, sizeof(struct dlm_callback));
		lkb->lkb_last_bast_time = ktime_get();
	}

	return 0;
}

void dlm_add_ast(struct dlm_lkb *lkb, uint32_t flags, int mode, int status,
		 uint32_t sbflags)
{
	uint64_t seq;
	int rv;

	spin_lock(&ast_queue_lock);

	seq = ++ast_seq_count;

	if (lkb->lkb_flags & DLM_IFL_USER) {
		spin_unlock(&ast_queue_lock);
		dlm_user_add_ast(lkb, flags, mode, status, sbflags, seq);
		return;
	}

	rv = dlm_add_lkb_callback(lkb, flags, mode, status, sbflags, seq);
	if (rv < 0) {
		spin_unlock(&ast_queue_lock);
		return;
	}

	if (list_empty(&lkb->lkb_astqueue)) {
		kref_get(&lkb->lkb_ref);
		list_add_tail(&lkb->lkb_astqueue, &ast_queue);
	}
	spin_unlock(&ast_queue_lock);

	set_bit(WAKE_ASTS, &astd_wakeflags);
	wake_up_process(astd_task);
}

static void process_asts(void)
{
	struct dlm_ls *ls = NULL;
	struct dlm_rsb *r = NULL;
	struct dlm_lkb *lkb;
	void (*castfn) (void *astparam);
	void (*bastfn) (void *astparam, int mode);
	struct dlm_callback callbacks[DLM_CALLBACKS_SIZE];
	int i, rv, resid;

repeat:
	spin_lock(&ast_queue_lock);
	list_for_each_entry(lkb, &ast_queue, lkb_astqueue) {
		r = lkb->lkb_resource;
		ls = r->res_ls;

		if (dlm_locking_stopped(ls))
			continue;

		/* we remove from astqueue list and remove everything in
		   lkb_callbacks before releasing the spinlock so empty
		   lkb_astqueue is always consistent with empty lkb_callbacks */

		list_del_init(&lkb->lkb_astqueue);

		castfn = lkb->lkb_astfn;
		bastfn = lkb->lkb_bastfn;

		memset(&callbacks, 0, sizeof(callbacks));

		for (i = 0; i < DLM_CALLBACKS_SIZE; i++) {
			rv = dlm_rem_lkb_callback(ls, lkb, &callbacks[i], &resid);
			if (rv < 0)
				break;
		}
		spin_unlock(&ast_queue_lock);

		if (resid) {
			/* shouldn't happen, for loop should have removed all */
			log_error(ls, "callback resid %d lkb %x",
				  resid, lkb->lkb_id);
		}

		for (i = 0; i < DLM_CALLBACKS_SIZE; i++) {
			if (!callbacks[i].seq)
				break;
			if (callbacks[i].flags & DLM_CB_SKIP) {
				continue;
			} else if (callbacks[i].flags & DLM_CB_BAST) {
				bastfn(lkb->lkb_astparam, callbacks[i].mode);
			} else if (callbacks[i].flags & DLM_CB_CAST) {
				lkb->lkb_lksb->sb_status = callbacks[i].sb_status;
				lkb->lkb_lksb->sb_flags = callbacks[i].sb_flags;
				castfn(lkb->lkb_astparam);
			}
		}

		/* removes ref for ast_queue, may cause lkb to be freed */
		dlm_put_lkb(lkb);

		cond_resched();
		goto repeat;
	}
	spin_unlock(&ast_queue_lock);
}

static inline int no_asts(void)
{
	int ret;

	spin_lock(&ast_queue_lock);
	ret = list_empty(&ast_queue);
	spin_unlock(&ast_queue_lock);
	return ret;
}

static int dlm_astd(void *data)
{
	while (!kthread_should_stop()) {
		set_current_state(TASK_INTERRUPTIBLE);
		if (!test_bit(WAKE_ASTS, &astd_wakeflags))
			schedule();
		set_current_state(TASK_RUNNING);

		mutex_lock(&astd_running);
		if (test_and_clear_bit(WAKE_ASTS, &astd_wakeflags))
			process_asts();
		mutex_unlock(&astd_running);
	}
	return 0;
}

void dlm_astd_wake(void)
{
	if (!no_asts()) {
		set_bit(WAKE_ASTS, &astd_wakeflags);
		wake_up_process(astd_task);
	}
}

int dlm_astd_start(void)
{
	struct task_struct *p;
	int error = 0;

	INIT_LIST_HEAD(&ast_queue);
	spin_lock_init(&ast_queue_lock);
	mutex_init(&astd_running);

	p = kthread_run(dlm_astd, NULL, "dlm_astd");
	if (IS_ERR(p))
		error = PTR_ERR(p);
	else
		astd_task = p;
	return error;
}

void dlm_astd_stop(void)
{
	kthread_stop(astd_task);
}

void dlm_astd_suspend(void)
{
	mutex_lock(&astd_running);
}

void dlm_astd_resume(void)
{
	mutex_unlock(&astd_running);
}

