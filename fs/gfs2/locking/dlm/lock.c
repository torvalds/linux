/*
 * Copyright (C) Sistina Software, Inc.  1997-2003 All rights reserved.
 * Copyright (C) 2004-2005 Red Hat, Inc.  All rights reserved.
 *
 * This copyrighted material is made available to anyone wishing to use,
 * modify, copy, or redistribute it subject to the terms and conditions
 * of the GNU General Public License version 2.
 */

#include "lock_dlm.h"

static char junk_lvb[GDLM_LVB_SIZE];


/* convert dlm lock-mode to gfs lock-state */

static s16 gdlm_make_lmstate(s16 dlmmode)
{
	switch (dlmmode) {
	case DLM_LOCK_IV:
	case DLM_LOCK_NL:
		return LM_ST_UNLOCKED;
	case DLM_LOCK_EX:
		return LM_ST_EXCLUSIVE;
	case DLM_LOCK_CW:
		return LM_ST_DEFERRED;
	case DLM_LOCK_PR:
		return LM_ST_SHARED;
	}
	gdlm_assert(0, "unknown DLM mode %d", dlmmode);
	return -1;
}

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

static void wake_up_ast(struct gdlm_lock *lp)
{
	clear_bit(LFL_AST_WAIT, &lp->flags);
	smp_mb__after_clear_bit();
	wake_up_bit(&lp->flags, LFL_AST_WAIT);
}

static void gdlm_delete_lp(struct gdlm_lock *lp)
{
	struct gdlm_ls *ls = lp->ls;

	spin_lock(&ls->async_lock);
	if (!list_empty(&lp->delay_list))
		list_del_init(&lp->delay_list);
	ls->all_locks_count--;
	spin_unlock(&ls->async_lock);

	kfree(lp);
}

static void gdlm_queue_delayed(struct gdlm_lock *lp)
{
	struct gdlm_ls *ls = lp->ls;

	spin_lock(&ls->async_lock);
	list_add_tail(&lp->delay_list, &ls->delayed);
	spin_unlock(&ls->async_lock);
}

static void process_complete(struct gdlm_lock *lp)
{
	struct gdlm_ls *ls = lp->ls;
	struct lm_async_cb acb;

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

	ls->fscb(ls->sdp, LM_CB_ASYNC, &acb);
}

static void gdlm_ast(void *astarg)
{
	struct gdlm_lock *lp = astarg;
	clear_bit(LFL_ACTIVE, &lp->flags);
	process_complete(lp);
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
		gdlm_assert(0, "unknown bast mode %u", bast_mode);
	}

	ls->fscb(ls->sdp, cb, &lp->lockname);
}


static void gdlm_bast(void *astarg, int mode)
{
	struct gdlm_lock *lp = astarg;

	if (!mode) {
		printk(KERN_INFO "lock_dlm: bast mode zero %x,%llx\n",
			lp->lockname.ln_type,
			(unsigned long long)lp->lockname.ln_number);
		return;
	}

	process_blocking(lp, mode);
}

/* convert gfs lock-state to dlm lock-mode */

static s16 make_mode(s16 lmstate)
{
	switch (lmstate) {
	case LM_ST_UNLOCKED:
		return DLM_LOCK_NL;
	case LM_ST_EXCLUSIVE:
		return DLM_LOCK_EX;
	case LM_ST_DEFERRED:
		return DLM_LOCK_CW;
	case LM_ST_SHARED:
		return DLM_LOCK_PR;
	}
	gdlm_assert(0, "unknown LM state %d", lmstate);
	return -1;
}


/* verify agreement with GFS on the current lock state, NB: DLM_LOCK_NL and
   DLM_LOCK_IV are both considered LM_ST_UNLOCKED by GFS. */

static void check_cur_state(struct gdlm_lock *lp, unsigned int cur_state)
{
	s16 cur = make_mode(cur_state);
	if (lp->cur != DLM_LOCK_IV)
		gdlm_assert(lp->cur == cur, "%d, %d", lp->cur, cur);
}

static inline unsigned int make_flags(struct gdlm_lock *lp,
				      unsigned int gfs_flags,
				      s16 cur, s16 req)
{
	unsigned int lkf = 0;

	if (gfs_flags & LM_FLAG_TRY)
		lkf |= DLM_LKF_NOQUEUE;

	if (gfs_flags & LM_FLAG_TRY_1CB) {
		lkf |= DLM_LKF_NOQUEUE;
		lkf |= DLM_LKF_NOQUEUEBAST;
	}

	if (gfs_flags & LM_FLAG_PRIORITY) {
		lkf |= DLM_LKF_NOORDER;
		lkf |= DLM_LKF_HEADQUE;
	}

	if (gfs_flags & LM_FLAG_ANY) {
		if (req == DLM_LOCK_PR)
			lkf |= DLM_LKF_ALTCW;
		else if (req == DLM_LOCK_CW)
			lkf |= DLM_LKF_ALTPR;
	}

	if (lp->lksb.sb_lkid != 0) {
		lkf |= DLM_LKF_CONVERT;
	}

	if (lp->lvb)
		lkf |= DLM_LKF_VALBLK;

	return lkf;
}

/* make_strname - convert GFS lock numbers to a string */

static inline void make_strname(const struct lm_lockname *lockname,
				struct gdlm_strname *str)
{
	sprintf(str->name, "%8x%16llx", lockname->ln_type,
		(unsigned long long)lockname->ln_number);
	str->namelen = GDLM_STRNAME_BYTES;
}

static int gdlm_create_lp(struct gdlm_ls *ls, struct lm_lockname *name,
			  struct gdlm_lock **lpp)
{
	struct gdlm_lock *lp;

	lp = kzalloc(sizeof(struct gdlm_lock), GFP_NOFS);
	if (!lp)
		return -ENOMEM;

	lp->lockname = *name;
	make_strname(name, &lp->strname);
	lp->ls = ls;
	lp->cur = DLM_LOCK_IV;
	INIT_LIST_HEAD(&lp->delay_list);

	spin_lock(&ls->async_lock);
	ls->all_locks_count++;
	spin_unlock(&ls->async_lock);

	*lpp = lp;
	return 0;
}

int gdlm_get_lock(void *lockspace, struct lm_lockname *name,
		  void **lockp)
{
	struct gdlm_lock *lp;
	int error;

	error = gdlm_create_lp(lockspace, name, &lp);

	*lockp = lp;
	return error;
}

void gdlm_put_lock(void *lock)
{
	gdlm_delete_lp(lock);
}

unsigned int gdlm_do_lock(struct gdlm_lock *lp)
{
	struct gdlm_ls *ls = lp->ls;
	int error, bast = 1;

	/*
	 * When recovery is in progress, delay lock requests for submission
	 * once recovery is done.  Requests for recovery (NOEXP) and unlocks
	 * can pass.
	 */

	if (test_bit(DFL_BLOCK_LOCKS, &ls->flags) &&
	    !test_bit(LFL_NOBLOCK, &lp->flags) && lp->req != DLM_LOCK_NL) {
		gdlm_queue_delayed(lp);
		return LM_OUT_ASYNC;
	}

	/*
	 * Submit the actual lock request.
	 */

	if (test_bit(LFL_NOBAST, &lp->flags))
		bast = 0;

	set_bit(LFL_ACTIVE, &lp->flags);

	log_debug("lk %x,%llx id %x %d,%d %x", lp->lockname.ln_type,
		  (unsigned long long)lp->lockname.ln_number, lp->lksb.sb_lkid,
		  lp->cur, lp->req, lp->lkf);

	error = dlm_lock(ls->dlm_lockspace, lp->req, &lp->lksb, lp->lkf,
			 lp->strname.name, lp->strname.namelen, 0, gdlm_ast,
			 lp, bast ? gdlm_bast : NULL);

	if ((error == -EAGAIN) && (lp->lkf & DLM_LKF_NOQUEUE)) {
		lp->lksb.sb_status = -EAGAIN;
		gdlm_ast(lp);
		error = 0;
	}

	if (error) {
		log_error("%s: gdlm_lock %x,%llx err=%d cur=%d req=%d lkf=%x "
			  "flags=%lx", ls->fsname, lp->lockname.ln_type,
			  (unsigned long long)lp->lockname.ln_number, error,
			  lp->cur, lp->req, lp->lkf, lp->flags);
		return LM_OUT_ERROR;
	}
	return LM_OUT_ASYNC;
}

static unsigned int gdlm_do_unlock(struct gdlm_lock *lp)
{
	struct gdlm_ls *ls = lp->ls;
	unsigned int lkf = 0;
	int error;

	set_bit(LFL_DLM_UNLOCK, &lp->flags);
	set_bit(LFL_ACTIVE, &lp->flags);

	if (lp->lvb)
		lkf = DLM_LKF_VALBLK;

	log_debug("un %x,%llx %x %d %x", lp->lockname.ln_type,
		  (unsigned long long)lp->lockname.ln_number,
		  lp->lksb.sb_lkid, lp->cur, lkf);

	error = dlm_unlock(ls->dlm_lockspace, lp->lksb.sb_lkid, lkf, NULL, lp);

	if (error) {
		log_error("%s: gdlm_unlock %x,%llx err=%d cur=%d req=%d lkf=%x "
			  "flags=%lx", ls->fsname, lp->lockname.ln_type,
			  (unsigned long long)lp->lockname.ln_number, error,
			  lp->cur, lp->req, lp->lkf, lp->flags);
		return LM_OUT_ERROR;
	}
	return LM_OUT_ASYNC;
}

unsigned int gdlm_lock(void *lock, unsigned int cur_state,
		       unsigned int req_state, unsigned int flags)
{
	struct gdlm_lock *lp = lock;

	if (req_state == LM_ST_UNLOCKED)
		return gdlm_unlock(lock, cur_state);

	if (req_state == LM_ST_UNLOCKED)
		return gdlm_unlock(lock, cur_state);

	clear_bit(LFL_DLM_CANCEL, &lp->flags);
	if (flags & LM_FLAG_NOEXP)
		set_bit(LFL_NOBLOCK, &lp->flags);

	check_cur_state(lp, cur_state);
	lp->req = make_mode(req_state);
	lp->lkf = make_flags(lp, flags, lp->cur, lp->req);

	return gdlm_do_lock(lp);
}

unsigned int gdlm_unlock(void *lock, unsigned int cur_state)
{
	struct gdlm_lock *lp = lock;

	clear_bit(LFL_DLM_CANCEL, &lp->flags);
	if (lp->cur == DLM_LOCK_IV)
		return 0;
	return gdlm_do_unlock(lp);
}

void gdlm_cancel(void *lock)
{
	struct gdlm_lock *lp = lock;
	struct gdlm_ls *ls = lp->ls;
	int error, delay_list = 0;

	if (test_bit(LFL_DLM_CANCEL, &lp->flags))
		return;

	log_info("gdlm_cancel %x,%llx flags %lx", lp->lockname.ln_type,
		 (unsigned long long)lp->lockname.ln_number, lp->flags);

	spin_lock(&ls->async_lock);
	if (!list_empty(&lp->delay_list)) {
		list_del_init(&lp->delay_list);
		delay_list = 1;
	}
	spin_unlock(&ls->async_lock);

	if (delay_list) {
		set_bit(LFL_CANCEL, &lp->flags);
		set_bit(LFL_ACTIVE, &lp->flags);
		gdlm_ast(lp);
		return;
	}

	if (!test_bit(LFL_ACTIVE, &lp->flags) ||
	    test_bit(LFL_DLM_UNLOCK, &lp->flags)) {
		log_info("gdlm_cancel skip %x,%llx flags %lx",
		 	 lp->lockname.ln_type,
			 (unsigned long long)lp->lockname.ln_number, lp->flags);
		return;
	}

	/* the lock is blocked in the dlm */

	set_bit(LFL_DLM_CANCEL, &lp->flags);
	set_bit(LFL_ACTIVE, &lp->flags);

	error = dlm_unlock(ls->dlm_lockspace, lp->lksb.sb_lkid, DLM_LKF_CANCEL,
			   NULL, lp);

	log_info("gdlm_cancel rv %d %x,%llx flags %lx", error,
		 lp->lockname.ln_type,
		 (unsigned long long)lp->lockname.ln_number, lp->flags);

	if (error == -EBUSY)
		clear_bit(LFL_DLM_CANCEL, &lp->flags);
}

static int gdlm_add_lvb(struct gdlm_lock *lp)
{
	char *lvb;

	lvb = kzalloc(GDLM_LVB_SIZE, GFP_NOFS);
	if (!lvb)
		return -ENOMEM;

	lp->lksb.sb_lvbptr = lvb;
	lp->lvb = lvb;
	return 0;
}

static void gdlm_del_lvb(struct gdlm_lock *lp)
{
	kfree(lp->lvb);
	lp->lvb = NULL;
	lp->lksb.sb_lvbptr = NULL;
}

static int gdlm_ast_wait(void *word)
{
	schedule();
	return 0;
}

/* This can do a synchronous dlm request (requiring a lock_dlm thread to get
   the completion) because gfs won't call hold_lvb() during a callback (from
   the context of a lock_dlm thread). */

static int hold_null_lock(struct gdlm_lock *lp)
{
	struct gdlm_lock *lpn = NULL;
	int error;

	if (lp->hold_null) {
		printk(KERN_INFO "lock_dlm: lvb already held\n");
		return 0;
	}

	error = gdlm_create_lp(lp->ls, &lp->lockname, &lpn);
	if (error)
		goto out;

	lpn->lksb.sb_lvbptr = junk_lvb;
	lpn->lvb = junk_lvb;

	lpn->req = DLM_LOCK_NL;
	lpn->lkf = DLM_LKF_VALBLK | DLM_LKF_EXPEDITE;
	set_bit(LFL_NOBAST, &lpn->flags);
	set_bit(LFL_INLOCK, &lpn->flags);
	set_bit(LFL_AST_WAIT, &lpn->flags);

	gdlm_do_lock(lpn);
	wait_on_bit(&lpn->flags, LFL_AST_WAIT, gdlm_ast_wait, TASK_UNINTERRUPTIBLE);
	error = lpn->lksb.sb_status;
	if (error) {
		printk(KERN_INFO "lock_dlm: hold_null_lock dlm error %d\n",
		       error);
		gdlm_delete_lp(lpn);
		lpn = NULL;
	}
out:
	lp->hold_null = lpn;
	return error;
}

/* This cannot do a synchronous dlm request (requiring a lock_dlm thread to get
   the completion) because gfs may call unhold_lvb() during a callback (from
   the context of a lock_dlm thread) which could cause a deadlock since the
   other lock_dlm thread could be engaged in recovery. */

static void unhold_null_lock(struct gdlm_lock *lp)
{
	struct gdlm_lock *lpn = lp->hold_null;

	gdlm_assert(lpn, "%x,%llx", lp->lockname.ln_type,
		    (unsigned long long)lp->lockname.ln_number);
	lpn->lksb.sb_lvbptr = NULL;
	lpn->lvb = NULL;
	set_bit(LFL_UNLOCK_DELETE, &lpn->flags);
	gdlm_do_unlock(lpn);
	lp->hold_null = NULL;
}

/* Acquire a NL lock because gfs requires the value block to remain
   intact on the resource while the lvb is "held" even if it's holding no locks
   on the resource. */

int gdlm_hold_lvb(void *lock, char **lvbp)
{
	struct gdlm_lock *lp = lock;
	int error;

	error = gdlm_add_lvb(lp);
	if (error)
		return error;

	*lvbp = lp->lvb;

	error = hold_null_lock(lp);
	if (error)
		gdlm_del_lvb(lp);

	return error;
}

void gdlm_unhold_lvb(void *lock, char *lvb)
{
	struct gdlm_lock *lp = lock;

	unhold_null_lock(lp);
	gdlm_del_lvb(lp);
}

void gdlm_submit_delayed(struct gdlm_ls *ls)
{
	struct gdlm_lock *lp, *safe;

	spin_lock(&ls->async_lock);
	list_for_each_entry_safe(lp, safe, &ls->delayed, delay_list) {
		list_del_init(&lp->delay_list);
		list_add_tail(&lp->delay_list, &ls->submit);
	}
	spin_unlock(&ls->async_lock);
	wake_up(&ls->thread_wait);
}

