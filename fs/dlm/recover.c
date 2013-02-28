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
 * timeout.  This uses a timeout so it can check periodically if the wait
 * should abort due to node failure (which doesn't cause a wake_up).
 * This should only be called by the dlm_recoverd thread.
 */

int dlm_wait_function(struct dlm_ls *ls, int (*testfn) (struct dlm_ls *ls))
{
	int error = 0;
	int rv;

	while (1) {
		rv = wait_event_timeout(ls->ls_wait_general,
					testfn(ls) || dlm_recovery_stopped(ls),
					dlm_config.ci_recover_timer * HZ);
		if (rv)
			break;
	}

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

static void _set_recover_status(struct dlm_ls *ls, uint32_t status)
{
	ls->ls_recover_status |= status;
}

void dlm_set_recover_status(struct dlm_ls *ls, uint32_t status)
{
	spin_lock(&ls->ls_recover_lock);
	_set_recover_status(ls, status);
	spin_unlock(&ls->ls_recover_lock);
}

static int wait_status_all(struct dlm_ls *ls, uint32_t wait_status,
			   int save_slots)
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

			error = dlm_rcom_status(ls, memb->nodeid, 0);
			if (error)
				goto out;

			if (save_slots)
				dlm_slot_save(ls, rc, memb);

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

static int wait_status_low(struct dlm_ls *ls, uint32_t wait_status,
			   uint32_t status_flags)
{
	struct dlm_rcom *rc = ls->ls_recover_buf;
	int error = 0, delay = 0, nodeid = ls->ls_low_nodeid;

	for (;;) {
		if (dlm_recovery_stopped(ls)) {
			error = -EINTR;
			goto out;
		}

		error = dlm_rcom_status(ls, nodeid, status_flags);
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
		error = wait_status_all(ls, status, 0);
		if (!error)
			dlm_set_recover_status(ls, status_all);
	} else
		error = wait_status_low(ls, status_all, 0);

	return error;
}

int dlm_recover_members_wait(struct dlm_ls *ls)
{
	struct dlm_member *memb;
	struct dlm_slot *slots;
	int num_slots, slots_size;
	int error, rv;
	uint32_t gen;

	list_for_each_entry(memb, &ls->ls_nodes, list) {
		memb->slot = -1;
		memb->generation = 0;
	}

	if (ls->ls_low_nodeid == dlm_our_nodeid()) {
		error = wait_status_all(ls, DLM_RS_NODES, 1);
		if (error)
			goto out;

		/* slots array is sparse, slots_size may be > num_slots */

		rv = dlm_slots_assign(ls, &num_slots, &slots_size, &slots, &gen);
		if (!rv) {
			spin_lock(&ls->ls_recover_lock);
			_set_recover_status(ls, DLM_RS_NODES_ALL);
			ls->ls_num_slots = num_slots;
			ls->ls_slots_size = slots_size;
			ls->ls_slots = slots;
			ls->ls_generation = gen;
			spin_unlock(&ls->ls_recover_lock);
		} else {
			dlm_set_recover_status(ls, DLM_RS_NODES_ALL);
		}
	} else {
		error = wait_status_low(ls, DLM_RS_NODES_ALL, DLM_RSF_NEED_SLOTS);
		if (error)
			goto out;

		dlm_slots_copy_in(ls);
	}
 out:
	return error;
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

static int recover_idr_empty(struct dlm_ls *ls)
{
	int empty = 1;

	spin_lock(&ls->ls_recover_idr_lock);
	if (ls->ls_recover_list_count)
		empty = 0;
	spin_unlock(&ls->ls_recover_idr_lock);

	return empty;
}

static int recover_idr_add(struct dlm_rsb *r)
{
	struct dlm_ls *ls = r->res_ls;
	int rv, id;

	rv = idr_pre_get(&ls->ls_recover_idr, GFP_NOFS);
	if (!rv)
		return -ENOMEM;

	spin_lock(&ls->ls_recover_idr_lock);
	if (r->res_id) {
		spin_unlock(&ls->ls_recover_idr_lock);
		return -1;
	}
	rv = idr_get_new_above(&ls->ls_recover_idr, r, 1, &id);
	if (rv) {
		spin_unlock(&ls->ls_recover_idr_lock);
		return rv;
	}
	r->res_id = id;
	ls->ls_recover_list_count++;
	dlm_hold_rsb(r);
	spin_unlock(&ls->ls_recover_idr_lock);
	return 0;
}

static void recover_idr_del(struct dlm_rsb *r)
{
	struct dlm_ls *ls = r->res_ls;

	spin_lock(&ls->ls_recover_idr_lock);
	idr_remove(&ls->ls_recover_idr, r->res_id);
	r->res_id = 0;
	ls->ls_recover_list_count--;
	spin_unlock(&ls->ls_recover_idr_lock);

	dlm_put_rsb(r);
}

static struct dlm_rsb *recover_idr_find(struct dlm_ls *ls, uint64_t id)
{
	struct dlm_rsb *r;

	spin_lock(&ls->ls_recover_idr_lock);
	r = idr_find(&ls->ls_recover_idr, (int)id);
	spin_unlock(&ls->ls_recover_idr_lock);
	return r;
}

static void recover_idr_clear(struct dlm_ls *ls)
{
	struct dlm_rsb *r;
	int id;

	spin_lock(&ls->ls_recover_idr_lock);

	idr_for_each_entry(&ls->ls_recover_idr, r, id) {
		r->res_id = 0;
		r->res_recover_locks_count = 0;
		ls->ls_recover_list_count--;

		dlm_put_rsb(r);
	}
	idr_remove_all(&ls->ls_recover_idr);

	if (ls->ls_recover_list_count != 0) {
		log_error(ls, "warning: recover_list_count %d",
			  ls->ls_recover_list_count);
		ls->ls_recover_list_count = 0;
	}
	spin_unlock(&ls->ls_recover_idr_lock);
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

	list_for_each_entry(lkb, queue, lkb_statequeue) {
		if (!(lkb->lkb_flags & DLM_IFL_MSTCPY)) {
			lkb->lkb_nodeid = nodeid;
			lkb->lkb_remid = 0;
		}
	}
}

static void set_master_lkbs(struct dlm_rsb *r)
{
	set_lock_master(&r->res_grantqueue, r->res_nodeid);
	set_lock_master(&r->res_convertqueue, r->res_nodeid);
	set_lock_master(&r->res_waitqueue, r->res_nodeid);
}

/*
 * Propagate the new master nodeid to locks
 * The NEW_MASTER flag tells dlm_recover_locks() which rsb's to consider.
 * The NEW_MASTER2 flag tells recover_lvb() and recover_grant() which
 * rsb's to consider.
 */

static void set_new_master(struct dlm_rsb *r)
{
	set_master_lkbs(r);
	rsb_set_flag(r, RSB_NEW_MASTER);
	rsb_set_flag(r, RSB_NEW_MASTER2);
}

/*
 * We do async lookups on rsb's that need new masters.  The rsb's
 * waiting for a lookup reply are kept on the recover_list.
 *
 * Another node recovering the master may have sent us a rcom lookup,
 * and our dlm_master_lookup() set it as the new master, along with
 * NEW_MASTER so that we'll recover it here (this implies dir_nodeid
 * equals our_nodeid below).
 */

static int recover_master(struct dlm_rsb *r, unsigned int *count)
{
	struct dlm_ls *ls = r->res_ls;
	int our_nodeid, dir_nodeid;
	int is_removed = 0;
	int error;

	if (is_master(r))
		return 0;

	is_removed = dlm_is_removed(ls, r->res_nodeid);

	if (!is_removed && !rsb_flag(r, RSB_NEW_MASTER))
		return 0;

	our_nodeid = dlm_our_nodeid();
	dir_nodeid = dlm_dir_nodeid(r);

	if (dir_nodeid == our_nodeid) {
		if (is_removed) {
			r->res_master_nodeid = our_nodeid;
			r->res_nodeid = 0;
		}

		/* set master of lkbs to ourself when is_removed, or to
		   another new master which we set along with NEW_MASTER
		   in dlm_master_lookup */
		set_new_master(r);
		error = 0;
	} else {
		recover_idr_add(r);
		error = dlm_send_rcom_lookup(r, dir_nodeid);
	}

	(*count)++;
	return error;
}

/*
 * All MSTCPY locks are purged and rebuilt, even if the master stayed the same.
 * This is necessary because recovery can be started, aborted and restarted,
 * causing the master nodeid to briefly change during the aborted recovery, and
 * change back to the original value in the second recovery.  The MSTCPY locks
 * may or may not have been purged during the aborted recovery.  Another node
 * with an outstanding request in waiters list and a request reply saved in the
 * requestqueue, cannot know whether it should ignore the reply and resend the
 * request, or accept the reply and complete the request.  It must do the
 * former if the remote node purged MSTCPY locks, and it must do the later if
 * the remote node did not.  This is solved by always purging MSTCPY locks, in
 * which case, the request reply would always be ignored and the request
 * resent.
 */

static int recover_master_static(struct dlm_rsb *r, unsigned int *count)
{
	int dir_nodeid = dlm_dir_nodeid(r);
	int new_master = dir_nodeid;

	if (dir_nodeid == dlm_our_nodeid())
		new_master = 0;

	dlm_purge_mstcpy_locks(r);
	r->res_master_nodeid = dir_nodeid;
	r->res_nodeid = new_master;
	set_new_master(r);
	(*count)++;
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
	unsigned int total = 0;
	unsigned int count = 0;
	int nodir = dlm_no_directory(ls);
	int error;

	log_debug(ls, "dlm_recover_masters");

	down_read(&ls->ls_root_sem);
	list_for_each_entry(r, &ls->ls_root_list, res_root_list) {
		if (dlm_recovery_stopped(ls)) {
			up_read(&ls->ls_root_sem);
			error = -EINTR;
			goto out;
		}

		lock_rsb(r);
		if (nodir)
			error = recover_master_static(r, &count);
		else
			error = recover_master(r, &count);
		unlock_rsb(r);
		cond_resched();
		total++;

		if (error) {
			up_read(&ls->ls_root_sem);
			goto out;
		}
	}
	up_read(&ls->ls_root_sem);

	log_debug(ls, "dlm_recover_masters %u of %u", count, total);

	error = dlm_wait_function(ls, &recover_idr_empty);
 out:
	if (error)
		recover_idr_clear(ls);
	return error;
}

int dlm_recover_master_reply(struct dlm_ls *ls, struct dlm_rcom *rc)
{
	struct dlm_rsb *r;
	int ret_nodeid, new_master;

	r = recover_idr_find(ls, rc->rc_id);
	if (!r) {
		log_error(ls, "dlm_recover_master_reply no id %llx",
			  (unsigned long long)rc->rc_id);
		goto out;
	}

	ret_nodeid = rc->rc_result;

	if (ret_nodeid == dlm_our_nodeid())
		new_master = 0;
	else
		new_master = ret_nodeid;

	lock_rsb(r);
	r->res_master_nodeid = ret_nodeid;
	r->res_nodeid = new_master;
	set_new_master(r);
	unlock_rsb(r);
	recover_idr_del(r);

	if (recover_idr_empty(ls))
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

	log_debug(ls, "dlm_recover_locks %d out", count);

	error = dlm_wait_function(ls, &recover_list_empty);
 out:
	if (error)
		recover_list_clear(ls);
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
 * RSB_VALNOTVALID is set in two cases:
 *
 * 1. we are master, but not new, and we purged an EX/PW lock held by a
 * failed node (in dlm_recover_purge which set RSB_RECOVER_LVB_INVAL)
 *
 * 2. we are a new master, and there are only NL/CR locks left.
 * (We could probably improve this by only invaliding in this way when
 * the previous master left uncleanly.  VMS docs mention that.)
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

	if (!rsb_flag(r, RSB_NEW_MASTER2) &&
	    rsb_flag(r, RSB_RECOVER_LVB_INVAL)) {
		/* case 1 above */
		rsb_set_flag(r, RSB_VALNOTVALID);
		return;
	}

	if (!rsb_flag(r, RSB_NEW_MASTER2))
		return;

	/* we are the new master, so figure out if VALNOTVALID should
	   be set, and set the rsb lvb from the best lkb available. */

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

	/* lvb is invalidated if only NL/CR locks remain */
	if (!big_lock_exists)
		rsb_set_flag(r, RSB_VALNOTVALID);

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
	struct dlm_ls *ls = r->res_ls;
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
		if (grmode == -1) {
			log_debug(ls, "recover_conversion %x set gr to rq %d",
				  lkb->lkb_id, lkb->lkb_rqmode);
			lkb->lkb_grmode = lkb->lkb_rqmode;
		} else {
			log_debug(ls, "recover_conversion %x set gr %d",
				  lkb->lkb_id, grmode);
			lkb->lkb_grmode = grmode;
		}
	}
}

/* We've become the new master for this rsb and waiting/converting locks may
   need to be granted in dlm_recover_grant() due to locks that may have
   existed from a removed node. */

static void recover_grant(struct dlm_rsb *r)
{
	if (!list_empty(&r->res_waitqueue) || !list_empty(&r->res_convertqueue))
		rsb_set_flag(r, RSB_RECOVER_GRANT);
}

void dlm_recover_rsbs(struct dlm_ls *ls)
{
	struct dlm_rsb *r;
	unsigned int count = 0;

	down_read(&ls->ls_root_sem);
	list_for_each_entry(r, &ls->ls_root_list, res_root_list) {
		lock_rsb(r);
		if (is_master(r)) {
			if (rsb_flag(r, RSB_RECOVER_CONVERT))
				recover_conversion(r);

			/* recover lvb before granting locks so the updated
			   lvb/VALNOTVALID is presented in the completion */
			recover_lvb(r);

			if (rsb_flag(r, RSB_NEW_MASTER2))
				recover_grant(r);
			count++;
		} else {
			rsb_clear_flag(r, RSB_VALNOTVALID);
		}
		rsb_clear_flag(r, RSB_RECOVER_CONVERT);
		rsb_clear_flag(r, RSB_RECOVER_LVB_INVAL);
		rsb_clear_flag(r, RSB_NEW_MASTER2);
		unlock_rsb(r);
	}
	up_read(&ls->ls_root_sem);

	if (count)
		log_debug(ls, "dlm_recover_rsbs %d done", count);
}

/* Create a single list of all root rsb's to be used during recovery */

int dlm_create_root_list(struct dlm_ls *ls)
{
	struct rb_node *n;
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
		for (n = rb_first(&ls->ls_rsbtbl[i].keep); n; n = rb_next(n)) {
			r = rb_entry(n, struct dlm_rsb, res_hashnode);
			list_add(&r->res_root_list, &ls->ls_root_list);
			dlm_hold_rsb(r);
		}

		if (!RB_EMPTY_ROOT(&ls->ls_rsbtbl[i].toss))
			log_error(ls, "dlm_create_root_list toss not empty");
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

void dlm_clear_toss(struct dlm_ls *ls)
{
	struct rb_node *n, *next;
	struct dlm_rsb *r;
	unsigned int count = 0;
	int i;

	for (i = 0; i < ls->ls_rsbtbl_size; i++) {
		spin_lock(&ls->ls_rsbtbl[i].lock);
		for (n = rb_first(&ls->ls_rsbtbl[i].toss); n; n = next) {
			next = rb_next(n);
			r = rb_entry(n, struct dlm_rsb, res_hashnode);
			rb_erase(n, &ls->ls_rsbtbl[i].toss);
			dlm_free_rsb(r);
			count++;
		}
		spin_unlock(&ls->ls_rsbtbl[i].lock);
	}

	if (count)
		log_debug(ls, "dlm_clear_toss %u done", count);
}

