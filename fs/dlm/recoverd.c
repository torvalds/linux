// SPDX-License-Identifier: GPL-2.0-only
/******************************************************************************
*******************************************************************************
**
**  Copyright (C) Sistina Software, Inc.  1997-2003  All rights reserved.
**  Copyright (C) 2004-2011 Red Hat, Inc.  All rights reserved.
**
**
*******************************************************************************
******************************************************************************/

#include "dlm_internal.h"
#include "lockspace.h"
#include "member.h"
#include "dir.h"
#include "ast.h"
#include "recover.h"
#include "lowcomms.h"
#include "lock.h"
#include "requestqueue.h"
#include "recoverd.h"

static int dlm_create_masters_list(struct dlm_ls *ls)
{
	struct dlm_rsb *r;
	int error = 0;

	write_lock_bh(&ls->ls_masters_lock);
	if (!list_empty(&ls->ls_masters_list)) {
		log_error(ls, "root list not empty");
		error = -EINVAL;
		goto out;
	}

	read_lock_bh(&ls->ls_rsbtbl_lock);
	list_for_each_entry(r, &ls->ls_slow_active, res_slow_list) {
		if (r->res_nodeid)
			continue;

		list_add(&r->res_masters_list, &ls->ls_masters_list);
		dlm_hold_rsb(r);
	}
	read_unlock_bh(&ls->ls_rsbtbl_lock);
 out:
	write_unlock_bh(&ls->ls_masters_lock);
	return error;
}

static void dlm_release_masters_list(struct dlm_ls *ls)
{
	struct dlm_rsb *r, *safe;

	write_lock_bh(&ls->ls_masters_lock);
	list_for_each_entry_safe(r, safe, &ls->ls_masters_list, res_masters_list) {
		list_del_init(&r->res_masters_list);
		dlm_put_rsb(r);
	}
	write_unlock_bh(&ls->ls_masters_lock);
}

static void dlm_create_root_list(struct dlm_ls *ls, struct list_head *root_list)
{
	struct dlm_rsb *r;

	read_lock_bh(&ls->ls_rsbtbl_lock);
	list_for_each_entry(r, &ls->ls_slow_active, res_slow_list) {
		list_add(&r->res_root_list, root_list);
		dlm_hold_rsb(r);
	}

	WARN_ON_ONCE(!list_empty(&ls->ls_slow_inactive));
	read_unlock_bh(&ls->ls_rsbtbl_lock);
}

static void dlm_release_root_list(struct list_head *root_list)
{
	struct dlm_rsb *r, *safe;

	list_for_each_entry_safe(r, safe, root_list, res_root_list) {
		list_del_init(&r->res_root_list);
		dlm_put_rsb(r);
	}
}

/* If the start for which we're re-enabling locking (seq) has been superseded
   by a newer stop (ls_recover_seq), we need to leave locking disabled.

   We suspend dlm_recv threads here to avoid the race where dlm_recv a) sees
   locking stopped and b) adds a message to the requestqueue, but dlm_recoverd
   enables locking and clears the requestqueue between a and b. */

static int enable_locking(struct dlm_ls *ls, uint64_t seq)
{
	int error = -EINTR;

	write_lock_bh(&ls->ls_recv_active);

	spin_lock_bh(&ls->ls_recover_lock);
	if (ls->ls_recover_seq == seq) {
		set_bit(LSFL_RUNNING, &ls->ls_flags);
		/* Schedule next timer if recovery put something on inactive.
		 *
		 * The rsbs that was queued while recovery on toss hasn't
		 * started yet because LSFL_RUNNING was set everything
		 * else recovery hasn't started as well because ls_in_recovery
		 * is still hold. So we should not run into the case that
		 * resume_scan_timer() queues a timer that can occur in
		 * a no op.
		 */
		resume_scan_timer(ls);
		/* unblocks processes waiting to enter the dlm */
		up_write(&ls->ls_in_recovery);
		clear_bit(LSFL_RECOVER_LOCK, &ls->ls_flags);
		error = 0;
	}
	spin_unlock_bh(&ls->ls_recover_lock);

	write_unlock_bh(&ls->ls_recv_active);
	return error;
}

static int ls_recover(struct dlm_ls *ls, struct dlm_recover *rv)
{
	LIST_HEAD(root_list);
	unsigned long start;
	int error, neg = 0;

	log_rinfo(ls, "dlm_recover %llu", (unsigned long long)rv->seq);

	mutex_lock(&ls->ls_recoverd_active);

	dlm_callback_suspend(ls);

	dlm_clear_inactive(ls);

	/*
	 * This list of root rsb's will be the basis of most of the recovery
	 * routines.
	 */

	dlm_create_root_list(ls, &root_list);

	/*
	 * Add or remove nodes from the lockspace's ls_nodes list.
	 *
	 * Due to the fact that we must report all membership changes to lsops
	 * or midcomms layer, it is not permitted to abort ls_recover() until
	 * this is done.
	 */

	error = dlm_recover_members(ls, rv, &neg);
	if (error) {
		log_rinfo(ls, "dlm_recover_members error %d", error);
		goto fail;
	}

	dlm_recover_dir_nodeid(ls, &root_list);

	/* Create a snapshot of all active rsbs were we are the master of.
	 * During the barrier between dlm_recover_members_wait() and
	 * dlm_recover_directory() other nodes can dump their necessary
	 * directory dlm_rsb (r->res_dir_nodeid == nodeid) in rcom
	 * communication dlm_copy_master_names() handling.
	 *
	 * TODO We should create a per lockspace list that contains rsbs
	 * that we are the master of. Instead of creating this list while
	 * recovery we keep track of those rsbs while locking handling and
	 * recovery can use it when necessary.
	 */
	error = dlm_create_masters_list(ls);
	if (error) {
		log_rinfo(ls, "dlm_create_masters_list error %d", error);
		goto fail_root_list;
	}

	ls->ls_recover_locks_in = 0;

	dlm_set_recover_status(ls, DLM_RS_NODES);

	error = dlm_recover_members_wait(ls, rv->seq);
	if (error) {
		log_rinfo(ls, "dlm_recover_members_wait error %d", error);
		dlm_release_masters_list(ls);
		goto fail_root_list;
	}

	start = jiffies;

	/*
	 * Rebuild our own share of the directory by collecting from all other
	 * nodes their master rsb names that hash to us.
	 */

	error = dlm_recover_directory(ls, rv->seq);
	if (error) {
		log_rinfo(ls, "dlm_recover_directory error %d", error);
		dlm_release_masters_list(ls);
		goto fail_root_list;
	}

	dlm_set_recover_status(ls, DLM_RS_DIR);

	error = dlm_recover_directory_wait(ls, rv->seq);
	if (error) {
		log_rinfo(ls, "dlm_recover_directory_wait error %d", error);
		dlm_release_masters_list(ls);
		goto fail_root_list;
	}

	dlm_release_masters_list(ls);

	/*
	 * We may have outstanding operations that are waiting for a reply from
	 * a failed node.  Mark these to be resent after recovery.  Unlock and
	 * cancel ops can just be completed.
	 */

	dlm_recover_waiters_pre(ls);

	if (dlm_recovery_stopped(ls)) {
		error = -EINTR;
		goto fail_root_list;
	}

	if (neg || dlm_no_directory(ls)) {
		/*
		 * Clear lkb's for departed nodes.
		 */

		dlm_recover_purge(ls, &root_list);

		/*
		 * Get new master nodeid's for rsb's that were mastered on
		 * departed nodes.
		 */

		error = dlm_recover_masters(ls, rv->seq, &root_list);
		if (error) {
			log_rinfo(ls, "dlm_recover_masters error %d", error);
			goto fail_root_list;
		}

		/*
		 * Send our locks on remastered rsb's to the new masters.
		 */

		error = dlm_recover_locks(ls, rv->seq, &root_list);
		if (error) {
			log_rinfo(ls, "dlm_recover_locks error %d", error);
			goto fail_root_list;
		}

		dlm_set_recover_status(ls, DLM_RS_LOCKS);

		error = dlm_recover_locks_wait(ls, rv->seq);
		if (error) {
			log_rinfo(ls, "dlm_recover_locks_wait error %d", error);
			goto fail_root_list;
		}

		log_rinfo(ls, "dlm_recover_locks %u in",
			  ls->ls_recover_locks_in);

		/*
		 * Finalize state in master rsb's now that all locks can be
		 * checked.  This includes conversion resolution and lvb
		 * settings.
		 */

		dlm_recover_rsbs(ls, &root_list);
	} else {
		/*
		 * Other lockspace members may be going through the "neg" steps
		 * while also adding us to the lockspace, in which case they'll
		 * be doing the recover_locks (RS_LOCKS) barrier.
		 */
		dlm_set_recover_status(ls, DLM_RS_LOCKS);

		error = dlm_recover_locks_wait(ls, rv->seq);
		if (error) {
			log_rinfo(ls, "dlm_recover_locks_wait error %d", error);
			goto fail_root_list;
		}
	}

	dlm_release_root_list(&root_list);

	/*
	 * Purge directory-related requests that are saved in requestqueue.
	 * All dir requests from before recovery are invalid now due to the dir
	 * rebuild and will be resent by the requesting nodes.
	 */

	dlm_purge_requestqueue(ls);

	dlm_set_recover_status(ls, DLM_RS_DONE);

	error = dlm_recover_done_wait(ls, rv->seq);
	if (error) {
		log_rinfo(ls, "dlm_recover_done_wait error %d", error);
		goto fail;
	}

	dlm_clear_members_gone(ls);

	dlm_callback_resume(ls);

	error = enable_locking(ls, rv->seq);
	if (error) {
		log_rinfo(ls, "enable_locking error %d", error);
		goto fail;
	}

	error = dlm_process_requestqueue(ls);
	if (error) {
		log_rinfo(ls, "dlm_process_requestqueue error %d", error);
		goto fail;
	}

	error = dlm_recover_waiters_post(ls);
	if (error) {
		log_rinfo(ls, "dlm_recover_waiters_post error %d", error);
		goto fail;
	}

	dlm_recover_grant(ls);

	log_rinfo(ls, "dlm_recover %llu generation %u done: %u ms",
		  (unsigned long long)rv->seq, ls->ls_generation,
		  jiffies_to_msecs(jiffies - start));
	mutex_unlock(&ls->ls_recoverd_active);

	return 0;

 fail_root_list:
	dlm_release_root_list(&root_list);
 fail:
	mutex_unlock(&ls->ls_recoverd_active);

	return error;
}

/* The dlm_ls_start() that created the rv we take here may already have been
   stopped via dlm_ls_stop(); in that case we need to leave the RECOVERY_STOP
   flag set. */

static void do_ls_recovery(struct dlm_ls *ls)
{
	struct dlm_recover *rv = NULL;
	int error;

	spin_lock_bh(&ls->ls_recover_lock);
	rv = ls->ls_recover_args;
	ls->ls_recover_args = NULL;
	if (rv && ls->ls_recover_seq == rv->seq)
		clear_bit(LSFL_RECOVER_STOP, &ls->ls_flags);
	spin_unlock_bh(&ls->ls_recover_lock);

	if (rv) {
		error = ls_recover(ls, rv);
		switch (error) {
		case 0:
			ls->ls_recovery_result = 0;
			complete(&ls->ls_recovery_done);

			dlm_lsop_recover_done(ls);
			break;
		case -EINTR:
			/* if recovery was interrupted -EINTR we wait for the next
			 * ls_recover() iteration until it hopefully succeeds.
			 */
			log_rinfo(ls, "%s %llu interrupted and should be queued to run again",
				  __func__, (unsigned long long)rv->seq);
			break;
		default:
			log_rinfo(ls, "%s %llu error %d", __func__,
				  (unsigned long long)rv->seq, error);

			/* let new_lockspace() get aware of critical error */
			ls->ls_recovery_result = error;
			complete(&ls->ls_recovery_done);
			break;
		}

		kfree(rv->nodes);
		kfree(rv);
	}
}

static int dlm_recoverd(void *arg)
{
	struct dlm_ls *ls;

	ls = dlm_find_lockspace_local(arg);
	if (!ls) {
		log_print("dlm_recoverd: no lockspace %p", arg);
		return -1;
	}

	down_write(&ls->ls_in_recovery);
	set_bit(LSFL_RECOVER_LOCK, &ls->ls_flags);
	wake_up(&ls->ls_recover_lock_wait);

	while (1) {
		/*
		 * We call kthread_should_stop() after set_current_state().
		 * This is because it works correctly if kthread_stop() is
		 * called just before set_current_state().
		 */
		set_current_state(TASK_INTERRUPTIBLE);
		if (kthread_should_stop()) {
			set_current_state(TASK_RUNNING);
			break;
		}
		if (!test_bit(LSFL_RECOVER_WORK, &ls->ls_flags) &&
		    !test_bit(LSFL_RECOVER_DOWN, &ls->ls_flags)) {
			if (kthread_should_stop())
				break;
			schedule();
		}
		set_current_state(TASK_RUNNING);

		if (test_and_clear_bit(LSFL_RECOVER_DOWN, &ls->ls_flags)) {
			down_write(&ls->ls_in_recovery);
			set_bit(LSFL_RECOVER_LOCK, &ls->ls_flags);
			wake_up(&ls->ls_recover_lock_wait);
		}

		if (test_and_clear_bit(LSFL_RECOVER_WORK, &ls->ls_flags))
			do_ls_recovery(ls);
	}

	if (test_bit(LSFL_RECOVER_LOCK, &ls->ls_flags))
		up_write(&ls->ls_in_recovery);

	dlm_put_lockspace(ls);
	return 0;
}

int dlm_recoverd_start(struct dlm_ls *ls)
{
	struct task_struct *p;
	int error = 0;

	p = kthread_run(dlm_recoverd, ls, "dlm_recoverd");
	if (IS_ERR(p))
		error = PTR_ERR(p);
	else
                ls->ls_recoverd_task = p;
	return error;
}

void dlm_recoverd_stop(struct dlm_ls *ls)
{
	kthread_stop(ls->ls_recoverd_task);
}

void dlm_recoverd_suspend(struct dlm_ls *ls)
{
	wake_up(&ls->ls_wait_general);
	mutex_lock(&ls->ls_recoverd_active);
}

void dlm_recoverd_resume(struct dlm_ls *ls)
{
	mutex_unlock(&ls->ls_recoverd_active);
}

