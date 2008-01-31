/*
 * Copyright (C) Sistina Software, Inc.  1997-2003 All rights reserved.
 * Copyright (C) 2004-2005 Red Hat, Inc.  All rights reserved.
 *
 * This copyrighted material is made available to anyone wishing to use,
 * modify, copy, or redistribute it subject to the terms and conditions
 * of the GNU General Public License version 2.
 */

#include "lock_dlm.h"

/* A lock placed on this queue is re-submitted to DLM as soon as the lock_dlm
   thread gets to it. */

static void queue_submit(struct gdlm_lock *lp)
{
	struct gdlm_ls *ls = lp->ls;

	spin_lock(&ls->async_lock);
	list_add_tail(&lp->delay_list, &ls->submit);
	spin_unlock(&ls->async_lock);
	wake_up(&ls->thread_wait);
}

static void process_blocking(struct gdlm_lock *lp, int bast_mode)
{
	struct gdlm_ls *ls = lp->ls;
	unsigned int cb = 0;

	switch (gdlm_make_lmstate(bast_mode)) {
	case LM_ST_EXCLUSIVE:
		cb = LM_CB_NEED_E;
		break;
	case LM_ST_DEFERRED:
		cb = LM_CB_NEED_D;
		break;
	case LM_ST_SHARED:
		cb = LM_CB_NEED_S;
		break;
	default:
		gdlm_assert(0, "unknown bast mode %u", lp->bast_mode);
	}

	ls->fscb(ls->sdp, cb, &lp->lockname);
}

static void wake_up_ast(struct gdlm_lock *lp)
{
	clear_bit(LFL_AST_WAIT, &lp->flags);
	smp_mb__after_clear_bit();
	wake_up_bit(&lp->flags, LFL_AST_WAIT);
}

static void process_complete(struct gdlm_lock *lp)
{
	struct gdlm_ls *ls = lp->ls;
	struct lm_async_cb acb;
	s16 prev_mode = lp->cur;

	memset(&acb, 0, sizeof(acb));

	if (lp->lksb.sb_status == -DLM_ECANCEL) {
		log_info("complete dlm cancel %x,%llx flags %lx",
		 	 lp->lockname.ln_type,
			 (unsigned long long)lp->lockname.ln_number,
			 lp->flags);

		lp->req = lp->cur;
		acb.lc_ret |= LM_OUT_CANCELED;
		if (lp->cur == DLM_LOCK_IV)
			lp->lksb.sb_lkid = 0;
		goto out;
	}

	if (test_and_clear_bit(LFL_DLM_UNLOCK, &lp->flags)) {
		if (lp->lksb.sb_status != -DLM_EUNLOCK) {
			log_info("unlock sb_status %d %x,%llx flags %lx",
				 lp->lksb.sb_status, lp->lockname.ln_type,
				 (unsigned long long)lp->lockname.ln_number,
				 lp->flags);
			return;
		}

		lp->cur = DLM_LOCK_IV;
		lp->req = DLM_LOCK_IV;
		lp->lksb.sb_lkid = 0;

		if (test_and_clear_bit(LFL_UNLOCK_DELETE, &lp->flags)) {
			gdlm_delete_lp(lp);
			return;
		}
		goto out;
	}

	if (lp->lksb.sb_flags & DLM_SBF_VALNOTVALID)
		memset(lp->lksb.sb_lvbptr, 0, GDLM_LVB_SIZE);

	if (lp->lksb.sb_flags & DLM_SBF_ALTMODE) {
		if (lp->req == DLM_LOCK_PR)
			lp->req = DLM_LOCK_CW;
		else if (lp->req == DLM_LOCK_CW)
			lp->req = DLM_LOCK_PR;
	}

	/*
	 * A canceled lock request.  The lock was just taken off the delayed
	 * list and was never even submitted to dlm.
	 */

	if (test_and_clear_bit(LFL_CANCEL, &lp->flags)) {
		log_info("complete internal cancel %x,%llx",
		 	 lp->lockname.ln_type,
			 (unsigned long long)lp->lockname.ln_number);
		lp->req = lp->cur;
		acb.lc_ret |= LM_OUT_CANCELED;
		goto out;
	}

	/*
	 * An error occured.
	 */

	if (lp->lksb.sb_status) {
		/* a "normal" error */
		if ((lp->lksb.sb_status == -EAGAIN) &&
		    (lp->lkf & DLM_LKF_NOQUEUE)) {
			lp->req = lp->cur;
			if (lp->cur == DLM_LOCK_IV)
				lp->lksb.sb_lkid = 0;
			goto out;
		}

		/* this could only happen with cancels I think */
		log_info("ast sb_status %d %x,%llx flags %lx",
			 lp->lksb.sb_status, lp->lockname.ln_type,
			 (unsigned long long)lp->lockname.ln_number,
			 lp->flags);
		return;
	}

	/*
	 * This is an AST for an EX->EX conversion for sync_lvb from GFS.
	 */

	if (test_and_clear_bit(LFL_SYNC_LVB, &lp->flags)) {
		wake_up_ast(lp);
		return;
	}

	/*
	 * A lock has been demoted to NL because it initially completed during
	 * BLOCK_LOCKS.  Now it must be requested in the originally requested
	 * mode.
	 */

	if (test_and_clear_bit(LFL_REREQUEST, &lp->flags)) {
		gdlm_assert(lp->req == DLM_LOCK_NL, "%x,%llx",
			    lp->lockname.ln_type,
			    (unsigned long long)lp->lockname.ln_number);
		gdlm_assert(lp->prev_req > DLM_LOCK_NL, "%x,%llx",
			    lp->lockname.ln_type,
			    (unsigned long long)lp->lockname.ln_number);

		lp->cur = DLM_LOCK_NL;
		lp->req = lp->prev_req;
		lp->prev_req = DLM_LOCK_IV;
		lp->lkf &= ~DLM_LKF_CONVDEADLK;

		set_bit(LFL_NOCACHE, &lp->flags);

		if (test_bit(DFL_BLOCK_LOCKS, &ls->flags) &&
		    !test_bit(LFL_NOBLOCK, &lp->flags))
			gdlm_queue_delayed(lp);
		else
			queue_submit(lp);
		return;
	}

	/*
	 * A request is granted during dlm recovery.  It may be granted
	 * because the locks of a failed node were cleared.  In that case,
	 * there may be inconsistent data beneath this lock and we must wait
	 * for recovery to complete to use it.  When gfs recovery is done this
	 * granted lock will be converted to NL and then reacquired in this
	 * granted state.
	 */

	if (test_bit(DFL_BLOCK_LOCKS, &ls->flags) &&
	    !test_bit(LFL_NOBLOCK, &lp->flags) &&
	    lp->req != DLM_LOCK_NL) {

		lp->cur = lp->req;
		lp->prev_req = lp->req;
		lp->req = DLM_LOCK_NL;
		lp->lkf |= DLM_LKF_CONVERT;
		lp->lkf &= ~DLM_LKF_CONVDEADLK;

		log_debug("rereq %x,%llx id %x %d,%d",
			  lp->lockname.ln_type,
			  (unsigned long long)lp->lockname.ln_number,
			  lp->lksb.sb_lkid, lp->cur, lp->req);

		set_bit(LFL_REREQUEST, &lp->flags);
		queue_submit(lp);
		return;
	}

	/*
	 * DLM demoted the lock to NL before it was granted so GFS must be
	 * told it cannot cache data for this lock.
	 */

	if (lp->lksb.sb_flags & DLM_SBF_DEMOTED)
		set_bit(LFL_NOCACHE, &lp->flags);

out:
	/*
	 * This is an internal lock_dlm lock
	 */

	if (test_bit(LFL_INLOCK, &lp->flags)) {
		clear_bit(LFL_NOBLOCK, &lp->flags);
		lp->cur = lp->req;
		wake_up_ast(lp);
		return;
	}

	/*
	 * Normal completion of a lock request.  Tell GFS it now has the lock.
	 */

	clear_bit(LFL_NOBLOCK, &lp->flags);
	lp->cur = lp->req;

	acb.lc_name = lp->lockname;
	acb.lc_ret |= gdlm_make_lmstate(lp->cur);

	if (!test_and_clear_bit(LFL_NOCACHE, &lp->flags) &&
	    (lp->cur > DLM_LOCK_NL) && (prev_mode > DLM_LOCK_NL))
		acb.lc_ret |= LM_OUT_CACHEABLE;

	ls->fscb(ls->sdp, LM_CB_ASYNC, &acb);
}

static inline int no_work(struct gdlm_ls *ls, int blocking)
{
	int ret;

	spin_lock(&ls->async_lock);
	ret = list_empty(&ls->complete) && list_empty(&ls->submit);
	if (ret && blocking)
		ret = list_empty(&ls->blocking);
	spin_unlock(&ls->async_lock);

	return ret;
}

static inline int check_drop(struct gdlm_ls *ls)
{
	if (!ls->drop_locks_count)
		return 0;

	if (time_after(jiffies, ls->drop_time + ls->drop_locks_period * HZ)) {
		ls->drop_time = jiffies;
		if (ls->all_locks_count >= ls->drop_locks_count)
			return 1;
	}
	return 0;
}

static int gdlm_thread(void *data, int blist)
{
	struct gdlm_ls *ls = (struct gdlm_ls *) data;
	struct gdlm_lock *lp = NULL;
	uint8_t complete, blocking, submit, drop;

	/* Only thread1 is allowed to do blocking callbacks since gfs
	   may wait for a completion callback within a blocking cb. */

	while (!kthread_should_stop()) {
		wait_event_interruptible(ls->thread_wait,
				!no_work(ls, blist) || kthread_should_stop());

		complete = blocking = submit = drop = 0;

		spin_lock(&ls->async_lock);

		if (blist && !list_empty(&ls->blocking)) {
			lp = list_entry(ls->blocking.next, struct gdlm_lock,
					blist);
			list_del_init(&lp->blist);
			blocking = lp->bast_mode;
			lp->bast_mode = 0;
		} else if (!list_empty(&ls->complete)) {
			lp = list_entry(ls->complete.next, struct gdlm_lock,
					clist);
			list_del_init(&lp->clist);
			complete = 1;
		} else if (!list_empty(&ls->submit)) {
			lp = list_entry(ls->submit.next, struct gdlm_lock,
					delay_list);
			list_del_init(&lp->delay_list);
			submit = 1;
		}

		drop = check_drop(ls);
		spin_unlock(&ls->async_lock);

		if (complete)
			process_complete(lp);

		else if (blocking)
			process_blocking(lp, blocking);

		else if (submit)
			gdlm_do_lock(lp);

		if (drop)
			ls->fscb(ls->sdp, LM_CB_DROPLOCKS, NULL);

		schedule();
	}

	return 0;
}

static int gdlm_thread1(void *data)
{
	return gdlm_thread(data, 1);
}

static int gdlm_thread2(void *data)
{
	return gdlm_thread(data, 0);
}

int gdlm_init_threads(struct gdlm_ls *ls)
{
	struct task_struct *p;
	int error;

	p = kthread_run(gdlm_thread1, ls, "lock_dlm1");
	error = IS_ERR(p);
	if (error) {
		log_error("can't start lock_dlm1 thread %d", error);
		return error;
	}
	ls->thread1 = p;

	p = kthread_run(gdlm_thread2, ls, "lock_dlm2");
	error = IS_ERR(p);
	if (error) {
		log_error("can't start lock_dlm2 thread %d", error);
		kthread_stop(ls->thread1);
		return error;
	}
	ls->thread2 = p;

	return 0;
}

void gdlm_release_threads(struct gdlm_ls *ls)
{
	kthread_stop(ls->thread1);
	kthread_stop(ls->thread2);
}

