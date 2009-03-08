/******************************************************************************
*******************************************************************************
**
**  Copyright (C) Sistina Software, Inc.  1997-2003  All rights reserved.
**  Copyright (C) 2004-2005 Red Hat, Inc.  All rights reserved.
**
**  This copyrighted material is made available to anyone wishing to use,
**  modify, copy, or redistribute it subject to the terms and conditions
**  of the GNU General Public License v.2.
**
*******************************************************************************
******************************************************************************/

#include "dlm_internal.h"
#include "lockspace.h"
#include "dir.h"
#include "config.h"
#include "ast.h"
#include "memory.h"
#include "rcom.h"
#include "lock.h"
#include "lowcomms.h"
#include "member.h"
#include "recover.h"


/*
 * Recovery waiting routines: these functions wait for a particular reply from
 * a remote node, or for the remote node to report a certain status.  They need
 * to abort if the lockspace is stopped indicating a node has failed (perhaps
 * the one being waited for).
 */

/*
 * Wait until given function returns non-zero or lockspace is stopped
 * (LS_RECOVERY_STOP set due to failure of a node in ls_nodes).  When another
 * function thinks it could have completed the waited-on task, they should wake
 * up ls_wait_general to get an immediate response rather than waiting for the
 * timer to detect the result.  A timer wakes us up periodically while waiting
 * to see if we should abort due to a node failure.  This should only be called
 * by the dlm_recoverd thread.
 */

static void dlm_wait_timer_fn(unsigned long data)
{
	struct dlm_ls *ls = (struct dlm_ls *) data;
	mod_timer(&ls->ls_timer, jiffies + (dlm_config.ci_recover_timer * HZ));
	wake_up(&ls->ls_wait_general);
}

int dlm_wait_function(struct dlm_ls *ls, int (*testfn) (struct dlm_ls *ls))
{
	int error = 0;

	init_timer(&ls->ls_timer);
	ls->ls_timer.function = dlm_wait_timer_fn;
	ls->ls_timer.data = (long) ls;
	ls->ls_timer.expires = jiffies + (dlm_config.ci_recover_timer * HZ);
	add_timer(&ls->ls_timer);

	wait_event(ls->ls_wait_general, testfn(ls) || dlm_recovery_stopped(ls));
	del_timer_sync(&ls->ls_timer);

	if (dlm_recovery_stopped(ls)) {
		log_debug(ls, "dlm_wait_function aborted");
		error = -EINTR;
	}
	return error;
}

/*
 * An efficient way for all nodes to wait for all others to have a certain
 * status.  The node with the lowest nodeid polls all the others for their
 * status (wait_status_all) and all the others poll the node with the low id
 * for its accumulated result (wait_status_low).  When all nodes have set
 * status flag X, then status flag X_ALL will be set on the low nodeid.
 */

uint32_t dlm_recover_status(struct dlm_ls *ls)
{
	uint32_t status;
	spin_lock(&ls->ls_recover_lock);
	status = ls->ls_recover_status;
	spin_unlock(&ls->ls_recover_lock);
	return status;
}

void dlm_set_recover_status(struct dlm_ls *ls, uint32_t status)
{
	spin_lock(&ls->ls_recover_lock);
	ls->ls_recover_status |= status;
	spin_unlock(&ls->ls_recover_lock);
}

static int wait_status_all(struct dlm_ls *ls, uint32_t wait_status)
{
	struct dlm_rcom *rc = ls->ls_recover_buf;
	struct dlm_member *memb;
	int error = 0, delay;

	list_for_each_entry(memb, &ls->ls_nodes, list) {
		delay = 0;
		for (;;) {
			if (dlm_recovery_stopped(ls)) {
				error = -EINTR;
				goto out;
			}

			error = dlm_rcom_status(ls, memb->nodeid);
			if (error)
				goto out;

			if (rc->rc_result & wait_status)
				break;
			if (delay < 1000)
				delay += 20;
			msleep(delay);
		}
	}
 out:
	return error;
}

static int wait_status_low(struct dlm_ls *ls, uint32_t wait_status)
{
	struct dlm_rcom *rc = ls->ls_recover_buf;
	int error = 0, delay = 0, nodeid = ls->ls_low_nodeid;

	for (;;) {
		if (dlm_recovery_stopped(ls)) {
			error = -EINTR;
			goto out;
		}

		error = dlm_rcom_status(ls, nodeid);
		if (error)
			break;

		if (rc->rc_result & wait_status)
			break;
		if (delay < 1000)
			delay += 20;
		msleep(delay);
	}
 out:
	return error;
}

static int wait_status(struct dlm_ls *ls, uint32_t status)
{
	uint32_t status_all = status << 1;
	int error;

	if (ls->ls_low_nodeid == dlm_our_nodeid()) {
		error = wait_status_all(ls, status);
		if (!error)
			dlm_set_recover_status(ls, status_all);
	} else
		error = wait_status_low(ls, status_all);

	return error;
}

int dlm_recover_members_wait(struct dlm_ls *ls)
{
	return wait_status(ls, DLM_RS_NODES);
}

int dlm_recover_directory_wait(struct dlm_ls *ls)
{
	return wait_status(ls, DLM_RS_DIR);
}

int dlm_recover_locks_wait(struct dlm_ls *ls)
{
	return wait_status(ls, DLM_RS_LOCKS);
}

int dlm_recover_done_wait(struct dlm_ls *ls)
{
	return wait_status(ls, DLM_RS_DONE);
}

/*
 * The recover_list contains all the rsb's for which we've requested the new
 * master nodeid.  As replies are returned from the resource directories the
 * rsb's are removed from the list.  When the list is empty we're done.
 *
 * The recover_list is later similarly used for all rsb's for which we've sent
 * new lkb's and need to receive new corresponding lkid's.
 *
 * We use the address of the rsb struct as a simple local identifier for the
 * rsb so we can match an rcom reply with the rsb it was sent for.
 */

static int recover_list_empty(struct dlm_ls *ls)
{
	int empty;

	spin_lock(&ls->ls_recover_list_lock);
	empty = list_empty(&ls->ls_recover_list);
	spin_unlock(&ls->ls_recover_list_lock);

	return empty;
}

static void recover_list_add(struct dlm_rsb *r)
{
	struct dlm_ls *ls = r->res_ls;

	spin_lock(&ls->ls_recover_list_lock);
	if (list_empty(&r->res_recover_list)) {
		list_add_tail(&r->res_recover_list, &ls->ls_recover_list);
		ls->ls_recover_list_count++;
		dlm_hold_rsb(r);
	}
	spin_unlock(&ls->ls_recover_list_lock);
}

static void recover_list_del(struct dlm_rsb *r)
{
	struct dlm_ls *ls = r->res_ls;

	spin_lock(&ls->ls_recover_list_lock);
	list_del_init(&r->res_recover_list);
	ls->ls_recover_list_count--;
	spin_unlock(&ls->ls_recover_list_lock);

	dlm_put_rsb(r);
}

static struct dlm_rsb *recover_list_find(struct dlm_ls *ls, uint64_t id)
{
	struct dlm_rsb *r = NULL;

	spin_lock(&ls->ls_recover_list_lock);

	list_for_each_entry(r, &ls->ls_recover_list, res_recover_list) {
		if (id == (unsigned long) r)
			goto out;
	}
	r = NULL;
 out:
	spin_unlock(&ls->ls_recover_list_lock);
	return r;
}

static void recover_list_clear(struct dlm_ls *ls)
{
	struct dlm_rsb *r, *s;

	spin_lock(&ls->ls_recover_list_lock);
	list_for_each_entry_safe(r, s, &ls->ls_recover_list, res_recover_list) {
		list_del_init(&r->res_recover_list);
		r->res_recover_locks_count = 0;
		dlm_put_rsb(r);
		ls->ls_recover_list_count--;
	}

	if (ls->ls_recover_list_count != 0) {
		log_error(ls, "warning: recover_list_count %d",
			  ls->ls_recover_list_count);
		ls->ls_recover_list_count = 0;
	}
	spin_unlock(&ls->ls_recover_list_lock);
}


/* Master recovery: find new master node for rsb's that were
   mastered on nodes that have been removed.

   dlm_recover_masters
   recover_master
   dlm_send_rcom_lookup            ->  receive_rcom_lookup
                                       dlm_dir_lookup
   receive_rcom_lookup_reply       <-
   dlm_recover_master_reply
   set_new_master
   set_master_lkbs
   set_lock_master
*/

/*
 * Set the lock master for all LKBs in a lock queue
 * If we are the new master of the rsb, we may have received new
 * MSTCPY locks from other nodes already which we need to ignore
 * when setting the new nodeid.
 */

static void set_lock_master(struct list_head *queue, int nodeid)
{
	struct dlm_lkb *lkb;

	list_for_each_entry(lkb, queue, lkb_statequeue)
		if (!(lkb->lkb_flags & DLM_IFL_MSTCPY))
			lkb->lkb_nodeid = nodeid;
}

static void set_master_lkbs(struct dlm_rsb *r)
{
	set_lock_master(&r->res_grantqueue, r->res_nodeid);
	set_lock_master(&r->res_convertqueue, r->res_nodeid);
	set_lock_master(&r->res_waitqueue, r->res_nodeid);
}

/*
 * Propogate the new master nodeid to locks
 * The NEW_MASTER flag tells dlm_recover_locks() which rsb's to consider.
 * The NEW_MASTER2 flag tells recover_lvb() and set_locks_purged() which
 * rsb's to consider.
 */

static void set_new_master(struct dlm_rsb *r, int nodeid)
{
	lock_rsb(r);
	r->res_nodeid = nodeid;
	set_master_lkbs(r);
	rsb_set_flag(r, RSB_NEW_MASTER);
	rsb_set_flag(r, RSB_NEW_MASTER2);
	unlock_rsb(r);
}

/*
 * We do async lookups on rsb's that need new masters.  The rsb's
 * waiting for a lookup reply are kept on the recover_list.
 */

static int recover_master(struct dlm_rsb *r)
{
	struct dlm_ls *ls = r->res_ls;
	int error, dir_nodeid, ret_nodeid, our_nodeid = dlm_our_nodeid();

	dir_nodeid = dlm_dir_nodeid(r);

	if (dir_nodeid == our_nodeid) {
		error = dlm_dir_lookup(ls, our_nodeid, r->res_name,
				       r->res_length, &ret_nodeid);
		if (error)
			log_error(ls, "recover dir lookup error %d", error);

		if (ret_nodeid == our_nodeid)
			ret_nodeid = 0;
		set_new_master(r, ret_nodeid);
	} else {
		recover_list_add(r);
		error = dlm_send_rcom_lookup(r, dir_nodeid);
	}

	return error;
}

/*
 * When not using a directory, most resource names will hash to a new static
 * master nodeid and the resource will need to be remastered.
 */

static int recover_master_static(struct dlm_rsb *r)
{
	int master = dlm_dir_nodeid(r);

	if (master == dlm_our_nodeid())
		master = 0;

	if (r->res_nodeid != master) {
		if (is_master(r))
			dlm_purge_mstcpy_locks(r);
		set_new_master(r, master);
		return 1;
	}
	return 0;
}

/*
 * Go through local root resources and for each rsb which has a master which
 * has departed, get the new master nodeid from the directory.  The dir will
 * assign mastery to the first node to look up the new master.  That means
 * we'll discover in this lookup if we're the new master of any rsb's.
 *
 * We fire off all the dir lookup requests individually and asynchronously to
 * the correct dir node.
 */

int dlm_recover_masters(struct dlm_ls *ls)
{
	struct dlm_rsb *r;
	int error = 0, count = 0;

	log_debug(ls, "dlm_recover_masters");

	down_read(&ls->ls_root_sem);
	list_for_each_entry(r, &ls->ls_root_list, res_root_list) {
		if (dlm_recovery_stopped(ls)) {
			up_read(&ls->ls_root_sem);
			error = -EINTR;
			goto out;
		}

		if (dlm_no_directory(ls))
			count += recover_master_static(r);
		else if (!is_master(r) &&
			 (dlm_is_removed(ls, r->res_nodeid) ||
			  rsb_flag(r, RSB_NEW_MASTER))) {
			recover_master(r);
			count++;
		}

		schedule();
	}
	up_read(&ls->ls_root_sem);

	log_debug(ls, "dlm_recover_masters %d resources", count);

	error = dlm_wait_function(ls, &recover_list_empty);
 out:
	if (error)
		recover_list_clear(ls);
	return error;
}

int dlm_recover_master_reply(struct dlm_ls *ls, struct dlm_rcom *rc)
{
	struct dlm_rsb *r;
	int nodeid;

	r = recover_list_find(ls, rc->rc_id);
	if (!r) {
		log_error(ls, "dlm_recover_master_reply no id %llx",
			  (unsigned long long)rc->rc_id);
		goto out;
	}

	nodeid = rc->rc_result;
	if (nodeid == dlm_our_nodeid())
		nodeid = 0;

	set_new_master(r, nodeid);
	recover_list_del(r);

	if (recover_list_empty(ls))
		wake_up(&ls->ls_wait_general);
 out:
	return 0;
}


/* Lock recovery: rebuild the process-copy locks we hold on a
   remastered rsb on the new rsb master.

   dlm_recover_locks
   recover_locks
   recover_locks_queue
   dlm_send_rcom_lock              ->  receive_rcom_lock
                                       dlm_recover_master_copy
   receive_rcom_lock_reply         <-
   dlm_recover_process_copy
*/


/*
 * keep a count of the number of lkb's we send to the new master; when we get
 * an equal number of replies then recovery for the rsb is done
 */

static int recover_locks_queue(struct dlm_rsb *r, struct list_head *head)
{
	struct dlm_lkb *lkb;
	int error = 0;

	list_for_each_entry(lkb, head, lkb_statequeue) {
	   	error = dlm_send_rcom_lock(r, lkb);
		if (error)
			break;
		r->res_recover_locks_count++;
	}

	return error;
}

static int recover_locks(struct dlm_rsb *r)
{
	int error = 0;

	lock_rsb(r);

	DLM_ASSERT(!r->res_recover_locks_count, dlm_dump_rsb(r););

	error = recover_locks_queue(r, &r->res_grantqueue);
	if (error)
		goto out;
	error = recover_locks_queue(r, &r->res_convertqueue);
	if (error)
		goto out;
	error = recover_locks_queue(r, &r->res_waitqueue);
	if (error)
		goto out;

	if (r->res_recover_locks_count)
		recover_list_add(r);
	else
		rsb_clear_flag(r, RSB_NEW_MASTER);
 out:
	unlock_rsb(r);
	return error;
}

int dlm_recover_locks(struct dlm_ls *ls)
{
	struct dlm_rsb *r;
	int error, count = 0;

	log_debug(ls, "dlm_recover_locks");

	down_read(&ls->ls_root_sem);
	list_for_each_entry(r, &ls->ls_root_list, res_root_list) {
		if (is_master(r)) {
			rsb_clear_flag(r, RSB_NEW_MASTER);
			continue;
		}

		if (!rsb_flag(r, RSB_NEW_MASTER))
			continue;

		if (dlm_recovery_stopped(ls)) {
			error = -EINTR;
			up_read(&ls->ls_root_sem);
			goto out;
		}

		error = recover_locks(r);
		if (error) {
			up_read(&ls->ls_root_sem);
			goto out;
		}

		count += r->res_recover_locks_count;
	}
	up_read(&ls->ls_root_sem);

	log_debug(ls, "dlm_recover_locks %d locks", count);

	error = dlm_wait_function(ls, &recover_list_empty);
 out:
	if (error)
		recover_list_clear(ls);
	else
		dlm_set_recover_status(ls, DLM_RS_LOCKS);
	return error;
}

void dlm_recovered_lock(struct dlm_rsb *r)
{
	DLM_ASSERT(rsb_flag(r, RSB_NEW_MASTER), dlm_dump_rsb(r););

	r->res_recover_locks_count--;
	if (!r->res_recover_locks_count) {
		rsb_clear_flag(r, RSB_NEW_MASTER);
		recover_list_del(r);
	}

	if (recover_list_empty(r->res_ls))
		wake_up(&r->res_ls->ls_wait_general);
}

/*
 * The lvb needs to be recovered on all master rsb's.  This includes setting
 * the VALNOTVALID flag if necessary, and determining the correct lvb contents
 * based on the lvb's of the locks held on the rsb.
 *
 * RSB_VALNOTVALID is set if there are only NL/CR locks on the rsb.  If it
 * was already set prior to recovery, it's not cleared, regardless of locks.
 *
 * The LVB contents are only considered for changing when this is a new master
 * of the rsb (NEW_MASTER2).  Then, the rsb's lvb is taken from any lkb with
 * mode > CR.  If no lkb's exist with mode above CR, the lvb contents are taken
 * from the lkb with the largest lvb sequence number.
 */

static void recover_lvb(struct dlm_rsb *r)
{
	struct dlm_lkb *lkb, *high_lkb = NULL;
	uint32_t high_seq = 0;
	int lock_lvb_exists = 0;
	int big_lock_exists = 0;
	int lvblen = r->res_ls->ls_lvblen;

	list_for_each_entry(lkb, &r->res_grantqueue, lkb_statequeue) {
		if (!(lkb->lkb_exflags & DLM_LKF_VALBLK))
			continue;

		lock_lvb_exists = 1;

		if (lkb->lkb_grmode > DLM_LOCK_CR) {
			big_lock_exists = 1;
			goto setflag;
		}

		if (((int)lkb->lkb_lvbseq - (int)high_seq) >= 0) {
			high_lkb = lkb;
			high_seq = lkb->lkb_lvbseq;
		}
	}

	list_for_each_entry(lkb, &r->res_convertqueue, lkb_statequeue) {
		if (!(lkb->lkb_exflags & DLM_LKF_VALBLK))
			continue;

		lock_lvb_exists = 1;

		if (lkb->lkb_grmode > DLM_LOCK_CR) {
			big_lock_exists = 1;
			goto setflag;
		}

		if (((int)lkb->lkb_lvbseq - (int)high_seq) >= 0) {
			high_lkb = lkb;
			high_seq = lkb->lkb_lvbseq;
		}
	}

 setflag:
	if (!lock_lvb_exists)
		goto out;

	if (!big_lock_exists)
		rsb_set_flag(r, RSB_VALNOTVALID);

	/* don't mess with the lvb unless we're the new master */
	if (!rsb_flag(r, RSB_NEW_MASTER2))
		goto out;

	if (!r->res_lvbptr) {
		r->res_lvbptr = dlm_allocate_lvb(r->res_ls);
		if (!r->res_lvbptr)
			goto out;
	}

	if (big_lock_exists) {
		r->res_lvbseq = lkb->lkb_lvbseq;
		memcpy(r->res_lvbptr, lkb->lkb_lvbptr, lvblen);
	} else if (high_lkb) {
		r->res_lvbseq = high_lkb->lkb_lvbseq;
		memcpy(r->res_lvbptr, high_lkb->lkb_lvbptr, lvblen);
	} else {
		r->res_lvbseq = 0;
		memset(r->res_lvbptr, 0, lvblen);
	}
 out:
	return;
}

/* All master rsb's flagged RECOVER_CONVERT need to be looked at.  The locks
   converting PR->CW or CW->PR need to have their lkb_grmode set. */

static void recover_conversion(struct dlm_rsb *r)
{
	struct dlm_lkb *lkb;
	int grmode = -1;

	list_for_each_entry(lkb, &r->res_grantqueue, lkb_statequeue) {
		if (lkb->lkb_grmode == DLM_LOCK_PR ||
		    lkb->lkb_grmode == DLM_LOCK_CW) {
			grmode = lkb->lkb_grmode;
			break;
		}
	}

	list_for_each_entry(lkb, &r->res_convertqueue, lkb_statequeue) {
		if (lkb->lkb_grmode != DLM_LOCK_IV)
			continue;
		if (grmode == -1)
			lkb->lkb_grmode = lkb->lkb_rqmode;
		else
			lkb->lkb_grmode = grmode;
	}
}

/* We've become the new master for this rsb and waiting/converting locks may
   need to be granted in dlm_grant_after_purge() due to locks that may have
   existed from a removed node. */

static void set_locks_purged(struct dlm_rsb *r)
{
	if (!list_empty(&r->res_waitqueue) || !list_empty(&r->res_convertqueue))
		rsb_set_flag(r, RSB_LOCKS_PURGED);
}

void dlm_recover_rsbs(struct dlm_ls *ls)
{
	struct dlm_rsb *r;
	int count = 0;

	log_debug(ls, "dlm_recover_rsbs");

	down_read(&ls->ls_root_sem);
	list_for_each_entry(r, &ls->ls_root_list, res_root_list) {
		lock_rsb(r);
		if (is_master(r)) {
			if (rsb_flag(r, RSB_RECOVER_CONVERT))
				recover_conversion(r);
			if (rsb_flag(r, RSB_NEW_MASTER2))
				set_locks_purged(r);
			recover_lvb(r);
			count++;
		}
		rsb_clear_flag(r, RSB_RECOVER_CONVERT);
		rsb_clear_flag(r, RSB_NEW_MASTER2);
		unlock_rsb(r);
	}
	up_read(&ls->ls_root_sem);

	log_debug(ls, "dlm_recover_rsbs %d rsbs", count);
}

/* Create a single list of all root rsb's to be used during recovery */

int dlm_create_root_list(struct dlm_ls *ls)
{
	struct dlm_rsb *r;
	int i, error = 0;

	down_write(&ls->ls_root_sem);
	if (!list_empty(&ls->ls_root_list)) {
		log_error(ls, "root list not empty");
		error = -EINVAL;
		goto out;
	}

	for (i = 0; i < ls->ls_rsbtbl_size; i++) {
		spin_lock(&ls->ls_rsbtbl[i].lock);
		list_for_each_entry(r, &ls->ls_rsbtbl[i].list, res_hashchain) {
			list_add(&r->res_root_list, &ls->ls_root_list);
			dlm_hold_rsb(r);
		}

		/* If we're using a directory, add tossed rsbs to the root
		   list; they'll have entries created in the new directory,
		   but no other recovery steps should do anything with them. */

		if (dlm_no_directory(ls)) {
			spin_unlock(&ls->ls_rsbtbl[i].lock);
			continue;
		}

		list_for_each_entry(r, &ls->ls_rsbtbl[i].toss, res_hashchain) {
			list_add(&r->res_root_list, &ls->ls_root_list);
			dlm_hold_rsb(r);
		}
		spin_unlock(&ls->ls_rsbtbl[i].lock);
	}
 out:
	up_write(&ls->ls_root_sem);
	return error;
}

void dlm_release_root_list(struct dlm_ls *ls)
{
	struct dlm_rsb *r, *safe;

	down_write(&ls->ls_root_sem);
	list_for_each_entry_safe(r, safe, &ls->ls_root_list, res_root_list) {
		list_del_init(&r->res_root_list);
		dlm_put_rsb(r);
	}
	up_write(&ls->ls_root_sem);
}

/* If not using a directory, clear the entire toss list, there's no benefit to
   caching the master value since it's fixed.  If we are using a dir, keep the
   rsb's we're the master of.  Recovery will add them to the root list and from
   there they'll be entered in the rebuilt directory. */

void dlm_clear_toss_list(struct dlm_ls *ls)
{
	struct dlm_rsb *r, *safe;
	int i;

	for (i = 0; i < ls->ls_rsbtbl_size; i++) {
		spin_lock(&ls->ls_rsbtbl[i].lock);
		list_for_each_entry_safe(r, safe, &ls->ls_rsbtbl[i].toss,
					 res_hashchain) {
			if (dlm_no_directory(ls) || !is_master(r)) {
				list_del(&r->res_hashchain);
				dlm_free_rsb(r);
			}
		}
		spin_unlock(&ls->ls_rsbtbl[i].lock);
	}
}

