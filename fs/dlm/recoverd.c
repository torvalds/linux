/******************************************************************************
*******************************************************************************
**
**  Copyright (C) Sistina Software, Inc.  1997-2003  All rights reserved.
**  Copyright (C) 2004-2011 Red Hat, Inc.  All rights reserved.
**
**  This copyrighted material is made available to anyone wishing to use,
**  modify, copy, or redistribute it subject to the terms and conditions
**  of the GNU General Public License v.2.
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


/* If the start for which we're re-enabling locking (seq) has been superseded
   by a newer stop (ls_recover_seq), we need to leave locking disabled.

   We suspend dlm_recv threads here to avoid the race where dlm_recv a) sees
   locking stopped and b) adds a message to the requestqueue, but dlm_recoverd
   enables locking and clears the requestqueue between a and b. */

static int enable_locking(struct dlm_ls *ls, uint64_t seq)
{
	int error = -EINTR;

	down_write(&ls->ls_recv_active);

	spin_lock(&ls->ls_recover_lock);
	if (ls->ls_recover_seq == seq) {
		set_bit(LSFL_RUNNING, &ls->ls_flags);
		/* unblocks processes waiting to enter the dlm */
		up_write(&ls->ls_in_recovery);
		error = 0;
	}
	spin_unlock(&ls->ls_recover_lock);

	up_write(&ls->ls_recv_active);
	return error;
}

static int ls_recover(struct dlm_ls *ls, struct dlm_recover *rv)
{
	unsigned long start;
	int error, neg = 0;

	log_debug(ls, "dlm_recover %llu", (unsigned long long)rv->seq);

	mutex_lock(&ls->ls_recoverd_active);

	dlm_callback_suspend(ls);

	/*
	 * Free non-master tossed rsb's.  Master rsb's are kept on toss
	 * list and put on root list to be included in resdir recovery.
	 */

	dlm_clear_toss_list(ls);

	/*
	 * This list of root rsb's will be the basis of most of the recovery
	 * routines.
	 */

	dlm_create_root_list(ls);

	/*
	 * Add or remove nodes from the lockspace's ls_nodes list.
	 */

	error = dlm_recover_members(ls, rv, &neg);
	if (error) {
		log_debug(ls, "dlm_recover_members error %d", error);
		goto fail;
	}

	ls->ls_recover_locks_in = 0;

	dlm_set_recover_status(ls, DLM_RS_NODES);

	error = dlm_recover_members_wait(ls);
	if (error) {
		log_debug(ls, "dlm_recover_members_wait error %d", error);
		goto fail;
	}

	start = jiffies;

	/*
	 * Rebuild our own share of the directory by collecting from all other
	 * nodes their master rsb names that hash to us.
	 */

	error = dlm_recover_directory(ls);
	if (error) {
		log_debug(ls, "dlm_recover_directory error %d", error);
		goto fail;
	}

	dlm_set_recover_status(ls, DLM_RS_DIR);

	error = dlm_recover_directory_wait(ls);
	if (error) {
		log_debug(ls, "dlm_recover_directory_wait error %d", error);
		goto fail;
	}

	/*
	 * We may have outstanding operations that are waiting for a reply from
	 * a failed node.  Mark these to be resent after recovery.  Unlock and
	 * cancel ops can just be completed.
	 */

	dlm_recover_waiters_pre(ls);

	error = dlm_recovery_stopped(ls);
	if (error)
		goto fail;

	if (neg || dlm_no_directory(ls)) {
		/*
		 * Clear lkb's for departed nodes.
		 */

		dlm_recover_purge(ls);

		/*
		 * Get new master nodeid's for rsb's that were mastered on
		 * departed nodes.
		 */

		error = dlm_recover_masters(ls);
		if (error) {
			log_debug(ls, "dlm_recover_masters error %d", error);
			goto fail;
		}

		/*
		 * Send our locks on remastered rsb's to the new masters.
		 */

		error = dlm_recover_locks(ls);
		if (error) {
			log_debug(ls, "dlm_recover_locks error %d", error);
			goto fail;
		}

		dlm_set_recover_status(ls, DLM_RS_LOCKS);

		error = dlm_recover_locks_wait(ls);
		if (error) {
			log_debug(ls, "dlm_recover_locks_wait error %d", error);
			goto fail;
		}

		log_debug(ls, "dlm_recover_locks %u in",
			  ls->ls_recover_locks_in);

		/*
		 * Finalize state in master rsb's now that all locks can be
		 * checked.  This includes conversion resolution and lvb
		 * settings.
		 */

		dlm_recover_rsbs(ls);
	} else {
		/*
		 * Other lockspace members may be going through the "neg" steps
		 * while also adding us to the lockspace, in which case they'll
		 * be doing the recover_locks (RS_LOCKS) barrier.
		 */
		dlm_set_recover_status(ls, DLM_RS_LOCKS);

		error = dlm_recover_locks_wait(ls);
		if (error) {
			log_debug(ls, "dlm_recover_locks_wait error %d", error);
			goto fail;
		}
	}

	dlm_release_root_list(ls);

	/*
	 * Purge directory-related requests that are saved in requestqueue.
	 * All dir requests from before recovery are invalid now due to the dir
	 * rebuild and will be resent by the requesting nodes.
	 */

	dlm_purge_requestqueue(ls);

	dlm_set_recover_status(ls, DLM_RS_DONE);

	error = dlm_recover_done_wait(ls);
	if (error) {
		log_debug(ls, "dlm_recover_done_wait error %d", error);
		goto fail;
	}

	dlm_clear_members_gone(ls);

	dlm_adjust_timeouts(ls);

	dlm_callback_resume(ls);

	error = enable_locking(ls, rv->seq);
	if (error) {
		log_debug(ls, "enable_locking error %d", error);
		goto fail;
	}

	error = dlm_process_requestqueue(ls);
	if (error) {
		log_debug(ls, "dlm_process_requestqueue error %d", error);
		goto fail;
	}

	error = dlm_recover_waiters_post(ls);
	if (error) {
		log_debug(ls, "dlm_recover_waiters_post error %d", error);
		goto fail;
	}

	dlm_recover_grant(ls);

	log_debug(ls, "dlm_recover %llu generation %u done: %u ms",
		  (unsigned long long)rv->seq, ls->ls_generation,
		  jiffies_to_msecs(jiffies - start));
	mutex_unlock(&ls->ls_recoverd_active);

	dlm_lsop_recover_done(ls);
	return 0;

 fail:
	dlm_release_root_list(ls);
	log_debug(ls, "dlm_recover %llu error %d",
		  (unsigned long long)rv->seq, error);
	mutex_unlock(&ls->ls_recoverd_active);
	return error;
}

/* The dlm_ls_start() that created the rv we take here may already have been
   stopped via dlm_ls_stop(); in that case we need to leave the RECOVERY_STOP
   flag set. */

static void do_ls_recovery(struct dlm_ls *ls)
{
	struct dlm_recover *rv = NULL;

	spin_lock(&ls->ls_recover_lock);
	rv = ls->ls_recover_args;
	ls->ls_recover_args = NULL;
	if (rv && ls->ls_recover_seq == rv->seq)
		clear_bit(LSFL_RECOVERY_STOP, &ls->ls_flags);
	spin_unlock(&ls->ls_recover_lock);

	if (rv) {
		ls_recover(ls, rv);
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

	while (!kthread_should_stop()) {
		set_current_state(TASK_INTERRUPTIBLE);
		if (!test_bit(LSFL_WORK, &ls->ls_flags))
			schedule();
		set_current_state(TASK_RUNNING);

		if (test_and_clear_bit(LSFL_WORK, &ls->ls_flags))
			do_ls_recovery(ls);
	}

	dlm_put_lockspace(ls);
	return 0;
}

void dlm_recoverd_kick(struct dlm_ls *ls)
{
	set_bit(LSFL_WORK, &ls->ls_flags);
	wake_up_process(ls->ls_recoverd_task);
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

