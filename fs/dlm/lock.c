// SPDX-License-Identifier: GPL-2.0-only
/******************************************************************************
*******************************************************************************
**
**  Copyright (C) 2005-2010 Red Hat, Inc.  All rights reserved.
**
**
*******************************************************************************
******************************************************************************/

/* Central locking logic has four stages:

   dlm_lock()
   dlm_unlock()

   request_lock(ls, lkb)
   convert_lock(ls, lkb)
   unlock_lock(ls, lkb)
   cancel_lock(ls, lkb)

   _request_lock(r, lkb)
   _convert_lock(r, lkb)
   _unlock_lock(r, lkb)
   _cancel_lock(r, lkb)

   do_request(r, lkb)
   do_convert(r, lkb)
   do_unlock(r, lkb)
   do_cancel(r, lkb)

   Stage 1 (lock, unlock) is mainly about checking input args and
   splitting into one of the four main operations:

       dlm_lock          = request_lock
       dlm_lock+CONVERT  = convert_lock
       dlm_unlock        = unlock_lock
       dlm_unlock+CANCEL = cancel_lock

   Stage 2, xxxx_lock(), just finds and locks the relevant rsb which is
   provided to the next stage.

   Stage 3, _xxxx_lock(), determines if the operation is local or remote.
   When remote, it calls send_xxxx(), when local it calls do_xxxx().

   Stage 4, do_xxxx(), is the guts of the operation.  It manipulates the
   given rsb and lkb and queues callbacks.

   For remote operations, send_xxxx() results in the corresponding do_xxxx()
   function being executed on the remote node.  The connecting send/receive
   calls on local (L) and remote (R) nodes:

   L: send_xxxx()              ->  R: receive_xxxx()
                                   R: do_xxxx()
   L: receive_xxxx_reply()     <-  R: send_xxxx_reply()
*/
#include <trace/events/dlm.h>

#include <linux/types.h>
#include <linux/rbtree.h>
#include <linux/slab.h>
#include "dlm_internal.h"
#include <linux/dlm_device.h>
#include "memory.h"
#include "midcomms.h"
#include "requestqueue.h"
#include "util.h"
#include "dir.h"
#include "member.h"
#include "lockspace.h"
#include "ast.h"
#include "lock.h"
#include "rcom.h"
#include "recover.h"
#include "lvb_table.h"
#include "user.h"
#include "config.h"

static int send_request(struct dlm_rsb *r, struct dlm_lkb *lkb);
static int send_convert(struct dlm_rsb *r, struct dlm_lkb *lkb);
static int send_unlock(struct dlm_rsb *r, struct dlm_lkb *lkb);
static int send_cancel(struct dlm_rsb *r, struct dlm_lkb *lkb);
static int send_grant(struct dlm_rsb *r, struct dlm_lkb *lkb);
static int send_bast(struct dlm_rsb *r, struct dlm_lkb *lkb, int mode);
static int send_lookup(struct dlm_rsb *r, struct dlm_lkb *lkb);
static int send_remove(struct dlm_rsb *r);
static int _request_lock(struct dlm_rsb *r, struct dlm_lkb *lkb);
static int _cancel_lock(struct dlm_rsb *r, struct dlm_lkb *lkb);
static void __receive_convert_reply(struct dlm_rsb *r, struct dlm_lkb *lkb,
				    const struct dlm_message *ms, bool local);
static int receive_extralen(const struct dlm_message *ms);
static void do_purge(struct dlm_ls *ls, int nodeid, int pid);
static void deactivate_rsb(struct kref *kref);

/*
 * Lock compatibilty matrix - thanks Steve
 * UN = Unlocked state. Not really a state, used as a flag
 * PD = Padding. Used to make the matrix a nice power of two in size
 * Other states are the same as the VMS DLM.
 * Usage: matrix[grmode+1][rqmode+1]  (although m[rq+1][gr+1] is the same)
 */

static const int __dlm_compat_matrix[8][8] = {
      /* UN NL CR CW PR PW EX PD */
        {1, 1, 1, 1, 1, 1, 1, 0},       /* UN */
        {1, 1, 1, 1, 1, 1, 1, 0},       /* NL */
        {1, 1, 1, 1, 1, 1, 0, 0},       /* CR */
        {1, 1, 1, 1, 0, 0, 0, 0},       /* CW */
        {1, 1, 1, 0, 1, 0, 0, 0},       /* PR */
        {1, 1, 1, 0, 0, 0, 0, 0},       /* PW */
        {1, 1, 0, 0, 0, 0, 0, 0},       /* EX */
        {0, 0, 0, 0, 0, 0, 0, 0}        /* PD */
};

/*
 * This defines the direction of transfer of LVB data.
 * Granted mode is the row; requested mode is the column.
 * Usage: matrix[grmode+1][rqmode+1]
 * 1 = LVB is returned to the caller
 * 0 = LVB is written to the resource
 * -1 = nothing happens to the LVB
 */

const int dlm_lvb_operations[8][8] = {
        /* UN   NL  CR  CW  PR  PW  EX  PD*/
        {  -1,  1,  1,  1,  1,  1,  1, -1 }, /* UN */
        {  -1,  1,  1,  1,  1,  1,  1,  0 }, /* NL */
        {  -1, -1,  1,  1,  1,  1,  1,  0 }, /* CR */
        {  -1, -1, -1,  1,  1,  1,  1,  0 }, /* CW */
        {  -1, -1, -1, -1,  1,  1,  1,  0 }, /* PR */
        {  -1,  0,  0,  0,  0,  0,  1,  0 }, /* PW */
        {  -1,  0,  0,  0,  0,  0,  0,  0 }, /* EX */
        {  -1,  0,  0,  0,  0,  0,  0,  0 }  /* PD */
};

#define modes_compat(gr, rq) \
	__dlm_compat_matrix[(gr)->lkb_grmode + 1][(rq)->lkb_rqmode + 1]

int dlm_modes_compat(int mode1, int mode2)
{
	return __dlm_compat_matrix[mode1 + 1][mode2 + 1];
}

/*
 * Compatibility matrix for conversions with QUECVT set.
 * Granted mode is the row; requested mode is the column.
 * Usage: matrix[grmode+1][rqmode+1]
 */

static const int __quecvt_compat_matrix[8][8] = {
      /* UN NL CR CW PR PW EX PD */
        {0, 0, 0, 0, 0, 0, 0, 0},       /* UN */
        {0, 0, 1, 1, 1, 1, 1, 0},       /* NL */
        {0, 0, 0, 1, 1, 1, 1, 0},       /* CR */
        {0, 0, 0, 0, 1, 1, 1, 0},       /* CW */
        {0, 0, 0, 1, 0, 1, 1, 0},       /* PR */
        {0, 0, 0, 0, 0, 0, 1, 0},       /* PW */
        {0, 0, 0, 0, 0, 0, 0, 0},       /* EX */
        {0, 0, 0, 0, 0, 0, 0, 0}        /* PD */
};

void dlm_print_lkb(struct dlm_lkb *lkb)
{
	printk(KERN_ERR "lkb: nodeid %d id %x remid %x exflags %x flags %x "
	       "sts %d rq %d gr %d wait_type %d wait_nodeid %d seq %llu\n",
	       lkb->lkb_nodeid, lkb->lkb_id, lkb->lkb_remid, lkb->lkb_exflags,
	       dlm_iflags_val(lkb), lkb->lkb_status, lkb->lkb_rqmode,
	       lkb->lkb_grmode, lkb->lkb_wait_type, lkb->lkb_wait_nodeid,
	       (unsigned long long)lkb->lkb_recover_seq);
}

static void dlm_print_rsb(struct dlm_rsb *r)
{
	printk(KERN_ERR "rsb: nodeid %d master %d dir %d flags %lx first %x "
	       "rlc %d name %s\n",
	       r->res_nodeid, r->res_master_nodeid, r->res_dir_nodeid,
	       r->res_flags, r->res_first_lkid, r->res_recover_locks_count,
	       r->res_name);
}

void dlm_dump_rsb(struct dlm_rsb *r)
{
	struct dlm_lkb *lkb;

	dlm_print_rsb(r);

	printk(KERN_ERR "rsb: root_list empty %d recover_list empty %d\n",
	       list_empty(&r->res_root_list), list_empty(&r->res_recover_list));
	printk(KERN_ERR "rsb lookup list\n");
	list_for_each_entry(lkb, &r->res_lookup, lkb_rsb_lookup)
		dlm_print_lkb(lkb);
	printk(KERN_ERR "rsb grant queue:\n");
	list_for_each_entry(lkb, &r->res_grantqueue, lkb_statequeue)
		dlm_print_lkb(lkb);
	printk(KERN_ERR "rsb convert queue:\n");
	list_for_each_entry(lkb, &r->res_convertqueue, lkb_statequeue)
		dlm_print_lkb(lkb);
	printk(KERN_ERR "rsb wait queue:\n");
	list_for_each_entry(lkb, &r->res_waitqueue, lkb_statequeue)
		dlm_print_lkb(lkb);
}

/* Threads cannot use the lockspace while it's being recovered */

void dlm_lock_recovery(struct dlm_ls *ls)
{
	down_read(&ls->ls_in_recovery);
}

void dlm_unlock_recovery(struct dlm_ls *ls)
{
	up_read(&ls->ls_in_recovery);
}

int dlm_lock_recovery_try(struct dlm_ls *ls)
{
	return down_read_trylock(&ls->ls_in_recovery);
}

static inline int can_be_queued(struct dlm_lkb *lkb)
{
	return !(lkb->lkb_exflags & DLM_LKF_NOQUEUE);
}

static inline int force_blocking_asts(struct dlm_lkb *lkb)
{
	return (lkb->lkb_exflags & DLM_LKF_NOQUEUEBAST);
}

static inline int is_demoted(struct dlm_lkb *lkb)
{
	return test_bit(DLM_SBF_DEMOTED_BIT, &lkb->lkb_sbflags);
}

static inline int is_altmode(struct dlm_lkb *lkb)
{
	return test_bit(DLM_SBF_ALTMODE_BIT, &lkb->lkb_sbflags);
}

static inline int is_granted(struct dlm_lkb *lkb)
{
	return (lkb->lkb_status == DLM_LKSTS_GRANTED);
}

static inline int is_remote(struct dlm_rsb *r)
{
	DLM_ASSERT(r->res_nodeid >= 0, dlm_print_rsb(r););
	return !!r->res_nodeid;
}

static inline int is_process_copy(struct dlm_lkb *lkb)
{
	return lkb->lkb_nodeid &&
	       !test_bit(DLM_IFL_MSTCPY_BIT, &lkb->lkb_iflags);
}

static inline int is_master_copy(struct dlm_lkb *lkb)
{
	return test_bit(DLM_IFL_MSTCPY_BIT, &lkb->lkb_iflags);
}

static inline int middle_conversion(struct dlm_lkb *lkb)
{
	if ((lkb->lkb_grmode==DLM_LOCK_PR && lkb->lkb_rqmode==DLM_LOCK_CW) ||
	    (lkb->lkb_rqmode==DLM_LOCK_PR && lkb->lkb_grmode==DLM_LOCK_CW))
		return 1;
	return 0;
}

static inline int down_conversion(struct dlm_lkb *lkb)
{
	return (!middle_conversion(lkb) && lkb->lkb_rqmode < lkb->lkb_grmode);
}

static inline int is_overlap_unlock(struct dlm_lkb *lkb)
{
	return test_bit(DLM_IFL_OVERLAP_UNLOCK_BIT, &lkb->lkb_iflags);
}

static inline int is_overlap_cancel(struct dlm_lkb *lkb)
{
	return test_bit(DLM_IFL_OVERLAP_CANCEL_BIT, &lkb->lkb_iflags);
}

static inline int is_overlap(struct dlm_lkb *lkb)
{
	return test_bit(DLM_IFL_OVERLAP_UNLOCK_BIT, &lkb->lkb_iflags) ||
	       test_bit(DLM_IFL_OVERLAP_CANCEL_BIT, &lkb->lkb_iflags);
}

static void queue_cast(struct dlm_rsb *r, struct dlm_lkb *lkb, int rv)
{
	if (is_master_copy(lkb))
		return;

	DLM_ASSERT(lkb->lkb_lksb, dlm_print_lkb(lkb););

	if (rv == -DLM_ECANCEL &&
	    test_and_clear_bit(DLM_IFL_DEADLOCK_CANCEL_BIT, &lkb->lkb_iflags))
		rv = -EDEADLK;

	dlm_add_cb(lkb, DLM_CB_CAST, lkb->lkb_grmode, rv, dlm_sbflags_val(lkb));
}

static inline void queue_cast_overlap(struct dlm_rsb *r, struct dlm_lkb *lkb)
{
	queue_cast(r, lkb,
		   is_overlap_unlock(lkb) ? -DLM_EUNLOCK : -DLM_ECANCEL);
}

static void queue_bast(struct dlm_rsb *r, struct dlm_lkb *lkb, int rqmode)
{
	if (is_master_copy(lkb)) {
		send_bast(r, lkb, rqmode);
	} else {
		dlm_add_cb(lkb, DLM_CB_BAST, rqmode, 0, 0);
	}
}

/*
 * Basic operations on rsb's and lkb's
 */

static inline unsigned long rsb_toss_jiffies(void)
{
	return jiffies + (READ_ONCE(dlm_config.ci_toss_secs) * HZ);
}

/* This is only called to add a reference when the code already holds
   a valid reference to the rsb, so there's no need for locking. */

static inline void hold_rsb(struct dlm_rsb *r)
{
	/* inactive rsbs are not ref counted */
	WARN_ON(rsb_flag(r, RSB_INACTIVE));
	kref_get(&r->res_ref);
}

void dlm_hold_rsb(struct dlm_rsb *r)
{
	hold_rsb(r);
}

/* TODO move this to lib/refcount.c */
static __must_check bool
dlm_refcount_dec_and_write_lock_bh(refcount_t *r, rwlock_t *lock)
__cond_acquires(lock)
{
	if (refcount_dec_not_one(r))
		return false;

	write_lock_bh(lock);
	if (!refcount_dec_and_test(r)) {
		write_unlock_bh(lock);
		return false;
	}

	return true;
}

/* TODO move this to include/linux/kref.h */
static inline int dlm_kref_put_write_lock_bh(struct kref *kref,
					     void (*release)(struct kref *kref),
					     rwlock_t *lock)
{
	if (dlm_refcount_dec_and_write_lock_bh(&kref->refcount, lock)) {
		release(kref);
		return 1;
	}

	return 0;
}

static void put_rsb(struct dlm_rsb *r)
{
	struct dlm_ls *ls = r->res_ls;
	int rv;

	rv = dlm_kref_put_write_lock_bh(&r->res_ref, deactivate_rsb,
					&ls->ls_rsbtbl_lock);
	if (rv)
		write_unlock_bh(&ls->ls_rsbtbl_lock);
}

void dlm_put_rsb(struct dlm_rsb *r)
{
	put_rsb(r);
}

/* connected with timer_delete_sync() in dlm_ls_stop() to stop
 * new timers when recovery is triggered and don't run them
 * again until a resume_scan_timer() tries it again.
 */
static void enable_scan_timer(struct dlm_ls *ls, unsigned long jiffies)
{
	if (!dlm_locking_stopped(ls))
		mod_timer(&ls->ls_scan_timer, jiffies);
}

/* This function tries to resume the timer callback if a rsb
 * is on the scan list and no timer is pending. It might that
 * the first entry is on currently executed as timer callback
 * but we don't care if a timer queued up again and does
 * nothing. Should be a rare case.
 */
void resume_scan_timer(struct dlm_ls *ls)
{
	struct dlm_rsb *r;

	spin_lock_bh(&ls->ls_scan_lock);
	r = list_first_entry_or_null(&ls->ls_scan_list, struct dlm_rsb,
				     res_scan_list);
	if (r && !timer_pending(&ls->ls_scan_timer))
		enable_scan_timer(ls, r->res_toss_time);
	spin_unlock_bh(&ls->ls_scan_lock);
}

/* ls_rsbtbl_lock must be held */

static void del_scan(struct dlm_ls *ls, struct dlm_rsb *r)
{
	struct dlm_rsb *first;

	/* active rsbs should never be on the scan list */
	WARN_ON(!rsb_flag(r, RSB_INACTIVE));

	spin_lock_bh(&ls->ls_scan_lock);
	r->res_toss_time = 0;

	/* if the rsb is not queued do nothing */
	if (list_empty(&r->res_scan_list))
		goto out;

	/* get the first element before delete */
	first = list_first_entry(&ls->ls_scan_list, struct dlm_rsb,
				 res_scan_list);
	list_del_init(&r->res_scan_list);
	/* check if the first element was the rsb we deleted */
	if (first == r) {
		/* try to get the new first element, if the list
		 * is empty now try to delete the timer, if we are
		 * too late we don't care.
		 *
		 * if the list isn't empty and a new first element got
		 * in place, set the new timer expire time.
		 */
		first = list_first_entry_or_null(&ls->ls_scan_list, struct dlm_rsb,
						 res_scan_list);
		if (!first)
			timer_delete(&ls->ls_scan_timer);
		else
			enable_scan_timer(ls, first->res_toss_time);
	}

out:
	spin_unlock_bh(&ls->ls_scan_lock);
}

static void add_scan(struct dlm_ls *ls, struct dlm_rsb *r)
{
	int our_nodeid = dlm_our_nodeid();
	struct dlm_rsb *first;

	/* A dir record for a remote master rsb should never be on the scan list. */
	WARN_ON(!dlm_no_directory(ls) &&
		(r->res_master_nodeid != our_nodeid) &&
		(dlm_dir_nodeid(r) == our_nodeid));

	/* An active rsb should never be on the scan list. */
	WARN_ON(!rsb_flag(r, RSB_INACTIVE));

	/* An rsb should not already be on the scan list. */
	WARN_ON(!list_empty(&r->res_scan_list));

	spin_lock_bh(&ls->ls_scan_lock);
	/* set the new rsb absolute expire time in the rsb */
	r->res_toss_time = rsb_toss_jiffies();
	if (list_empty(&ls->ls_scan_list)) {
		/* if the queue is empty add the element and it's
		 * our new expire time
		 */
		list_add_tail(&r->res_scan_list, &ls->ls_scan_list);
		enable_scan_timer(ls, r->res_toss_time);
	} else {
		/* try to get the maybe new first element and then add
		 * to this rsb with the oldest expire time to the end
		 * of the queue. If the list was empty before this
		 * rsb expire time is our next expiration if it wasn't
		 * the now new first elemet is our new expiration time
		 */
		first = list_first_entry_or_null(&ls->ls_scan_list, struct dlm_rsb,
						 res_scan_list);
		list_add_tail(&r->res_scan_list, &ls->ls_scan_list);
		if (!first)
			enable_scan_timer(ls, r->res_toss_time);
		else
			enable_scan_timer(ls, first->res_toss_time);
	}
	spin_unlock_bh(&ls->ls_scan_lock);
}

/* if we hit contention we do in 250 ms a retry to trylock.
 * if there is any other mod_timer in between we don't care
 * about that it expires earlier again this is only for the
 * unlikely case nothing happened in this time.
 */
#define DLM_TOSS_TIMER_RETRY	(jiffies + msecs_to_jiffies(250))

/* Called by lockspace scan_timer to free unused rsb's. */

void dlm_rsb_scan(struct timer_list *timer)
{
	struct dlm_ls *ls = from_timer(ls, timer, ls_scan_timer);
	int our_nodeid = dlm_our_nodeid();
	struct dlm_rsb *r;
	int rv;

	while (1) {
		/* interrupting point to leave iteration when
		 * recovery waits for timer_delete_sync(), recovery
		 * will take care to delete everything in scan list.
		 */
		if (dlm_locking_stopped(ls))
			break;

		rv = spin_trylock(&ls->ls_scan_lock);
		if (!rv) {
			/* rearm again try timer */
			enable_scan_timer(ls, DLM_TOSS_TIMER_RETRY);
			break;
		}

		r = list_first_entry_or_null(&ls->ls_scan_list, struct dlm_rsb,
					     res_scan_list);
		if (!r) {
			/* the next add_scan will enable the timer again */
			spin_unlock(&ls->ls_scan_lock);
			break;
		}

		/*
		 * If the first rsb is not yet expired, then stop because the
		 * list is sorted with nearest expiration first.
		 */
		if (time_before(jiffies, r->res_toss_time)) {
			/* rearm with the next rsb to expire in the future */
			enable_scan_timer(ls, r->res_toss_time);
			spin_unlock(&ls->ls_scan_lock);
			break;
		}

		/* in find_rsb_dir/nodir there is a reverse order of this
		 * lock, however this is only a trylock if we hit some
		 * possible contention we try it again.
		 */
		rv = write_trylock(&ls->ls_rsbtbl_lock);
		if (!rv) {
			spin_unlock(&ls->ls_scan_lock);
			/* rearm again try timer */
			enable_scan_timer(ls, DLM_TOSS_TIMER_RETRY);
			break;
		}

		list_del(&r->res_slow_list);
		rhashtable_remove_fast(&ls->ls_rsbtbl, &r->res_node,
				       dlm_rhash_rsb_params);
		rsb_clear_flag(r, RSB_HASHED);

		/* ls_rsbtbl_lock is not needed when calling send_remove() */
		write_unlock(&ls->ls_rsbtbl_lock);

		list_del_init(&r->res_scan_list);
		spin_unlock(&ls->ls_scan_lock);

		/* An rsb that is a dir record for a remote master rsb
		 * cannot be removed, and should not have a timer enabled.
		 */
		WARN_ON(!dlm_no_directory(ls) &&
			(r->res_master_nodeid != our_nodeid) &&
			(dlm_dir_nodeid(r) == our_nodeid));

		/* We're the master of this rsb but we're not
		 * the directory record, so we need to tell the
		 * dir node to remove the dir record
		 */
		if (!dlm_no_directory(ls) &&
		    (r->res_master_nodeid == our_nodeid) &&
		    (dlm_dir_nodeid(r) != our_nodeid))
			send_remove(r);

		free_inactive_rsb(r);
	}
}

/* If ls->ls_new_rsb is empty, return -EAGAIN, so the caller can
   unlock any spinlocks, go back and call pre_rsb_struct again.
   Otherwise, take an rsb off the list and return it. */

static int get_rsb_struct(struct dlm_ls *ls, const void *name, int len,
			  struct dlm_rsb **r_ret)
{
	struct dlm_rsb *r;

	r = dlm_allocate_rsb();
	if (!r)
		return -ENOMEM;

	r->res_ls = ls;
	r->res_length = len;
	memcpy(r->res_name, name, len);
	spin_lock_init(&r->res_lock);

	INIT_LIST_HEAD(&r->res_lookup);
	INIT_LIST_HEAD(&r->res_grantqueue);
	INIT_LIST_HEAD(&r->res_convertqueue);
	INIT_LIST_HEAD(&r->res_waitqueue);
	INIT_LIST_HEAD(&r->res_root_list);
	INIT_LIST_HEAD(&r->res_scan_list);
	INIT_LIST_HEAD(&r->res_recover_list);
	INIT_LIST_HEAD(&r->res_masters_list);

	*r_ret = r;
	return 0;
}

int dlm_search_rsb_tree(struct rhashtable *rhash, const void *name, int len,
			struct dlm_rsb **r_ret)
{
	char key[DLM_RESNAME_MAXLEN] = {};

	memcpy(key, name, len);
	*r_ret = rhashtable_lookup_fast(rhash, &key, dlm_rhash_rsb_params);
	if (*r_ret)
		return 0;

	return -EBADR;
}

static int rsb_insert(struct dlm_rsb *rsb, struct rhashtable *rhash)
{
	int rv;

	rv = rhashtable_insert_fast(rhash, &rsb->res_node,
				    dlm_rhash_rsb_params);
	if (!rv)
		rsb_set_flag(rsb, RSB_HASHED);

	return rv;
}

/*
 * Find rsb in rsbtbl and potentially create/add one
 *
 * Delaying the release of rsb's has a similar benefit to applications keeping
 * NL locks on an rsb, but without the guarantee that the cached master value
 * will still be valid when the rsb is reused.  Apps aren't always smart enough
 * to keep NL locks on an rsb that they may lock again shortly; this can lead
 * to excessive master lookups and removals if we don't delay the release.
 *
 * Searching for an rsb means looking through both the normal list and toss
 * list.  When found on the toss list the rsb is moved to the normal list with
 * ref count of 1; when found on normal list the ref count is incremented.
 *
 * rsb's on the keep list are being used locally and refcounted.
 * rsb's on the toss list are not being used locally, and are not refcounted.
 *
 * The toss list rsb's were either
 * - previously used locally but not any more (were on keep list, then
 *   moved to toss list when last refcount dropped)
 * - created and put on toss list as a directory record for a lookup
 *   (we are the dir node for the res, but are not using the res right now,
 *   but some other node is)
 *
 * The purpose of find_rsb() is to return a refcounted rsb for local use.
 * So, if the given rsb is on the toss list, it is moved to the keep list
 * before being returned.
 *
 * deactivate_rsb() happens when all local usage of the rsb is done, i.e. no
 * more refcounts exist, so the rsb is moved from the keep list to the
 * toss list.
 *
 * rsb's on both keep and toss lists are used for doing a name to master
 * lookups.  rsb's that are in use locally (and being refcounted) are on
 * the keep list, rsb's that are not in use locally (not refcounted) and
 * only exist for name/master lookups are on the toss list.
 *
 * rsb's on the toss list who's dir_nodeid is not local can have stale
 * name/master mappings.  So, remote requests on such rsb's can potentially
 * return with an error, which means the mapping is stale and needs to
 * be updated with a new lookup.  (The idea behind MASTER UNCERTAIN and
 * first_lkid is to keep only a single outstanding request on an rsb
 * while that rsb has a potentially stale master.)
 */

static int find_rsb_dir(struct dlm_ls *ls, const void *name, int len,
			uint32_t hash, int dir_nodeid, int from_nodeid,
			unsigned int flags, struct dlm_rsb **r_ret)
{
	struct dlm_rsb *r = NULL;
	int our_nodeid = dlm_our_nodeid();
	int from_local = 0;
	int from_other = 0;
	int from_dir = 0;
	int create = 0;
	int error;

	if (flags & R_RECEIVE_REQUEST) {
		if (from_nodeid == dir_nodeid)
			from_dir = 1;
		else
			from_other = 1;
	} else if (flags & R_REQUEST) {
		from_local = 1;
	}

	/*
	 * flags & R_RECEIVE_RECOVER is from dlm_recover_master_copy, so
	 * from_nodeid has sent us a lock in dlm_recover_locks, believing
	 * we're the new master.  Our local recovery may not have set
	 * res_master_nodeid to our_nodeid yet, so allow either.  Don't
	 * create the rsb; dlm_recover_process_copy() will handle EBADR
	 * by resending.
	 *
	 * If someone sends us a request, we are the dir node, and we do
	 * not find the rsb anywhere, then recreate it.  This happens if
	 * someone sends us a request after we have removed/freed an rsb.
	 * (They sent a request instead of lookup because they are using
	 * an rsb taken from their scan list.)
	 */

	if (from_local || from_dir ||
	    (from_other && (dir_nodeid == our_nodeid))) {
		create = 1;
	}

 retry:
	error = dlm_search_rsb_tree(&ls->ls_rsbtbl, name, len, &r);
	if (error)
		goto do_new;

	/* check if the rsb is active under read lock - likely path */
	read_lock_bh(&ls->ls_rsbtbl_lock);
	if (!rsb_flag(r, RSB_HASHED)) {
		read_unlock_bh(&ls->ls_rsbtbl_lock);
		goto do_new;
	}
	
	/*
	 * rsb is active, so we can't check master_nodeid without lock_rsb.
	 */

	if (rsb_flag(r, RSB_INACTIVE)) {
		read_unlock_bh(&ls->ls_rsbtbl_lock);
		goto do_inactive;
	}

	kref_get(&r->res_ref);
	read_unlock_bh(&ls->ls_rsbtbl_lock);
	goto out;


 do_inactive:
	write_lock_bh(&ls->ls_rsbtbl_lock);

	/*
	 * The expectation here is that the rsb will have HASHED and
	 * INACTIVE flags set, and that the rsb can be moved from
	 * inactive back to active again.  However, between releasing
	 * the read lock and acquiring the write lock, this rsb could
	 * have been removed from rsbtbl, and had HASHED cleared, to
	 * be freed.  To deal with this case, we would normally need
	 * to repeat dlm_search_rsb_tree while holding the write lock,
	 * but rcu allows us to simply check the HASHED flag, because
	 * the rcu read lock means the rsb will not be freed yet.
	 * If the HASHED flag is not set, then the rsb is being freed,
	 * so we add a new rsb struct.  If the HASHED flag is set,
	 * and INACTIVE is not set, it means another thread has
	 * made the rsb active, as we're expecting to do here, and
	 * we just repeat the lookup (this will be very unlikely.)
	 */
	if (rsb_flag(r, RSB_HASHED)) {
		if (!rsb_flag(r, RSB_INACTIVE)) {
			write_unlock_bh(&ls->ls_rsbtbl_lock);
			goto retry;
		}
	} else {
		write_unlock_bh(&ls->ls_rsbtbl_lock);
		goto do_new;
	}

	/*
	 * rsb found inactive (master_nodeid may be out of date unless
	 * we are the dir_nodeid or were the master)  No other thread
	 * is using this rsb because it's inactive, so we can
	 * look at or update res_master_nodeid without lock_rsb.
	 */

	if ((r->res_master_nodeid != our_nodeid) && from_other) {
		/* our rsb was not master, and another node (not the dir node)
		   has sent us a request */
		log_debug(ls, "find_rsb inactive from_other %d master %d dir %d %s",
			  from_nodeid, r->res_master_nodeid, dir_nodeid,
			  r->res_name);
		write_unlock_bh(&ls->ls_rsbtbl_lock);
		error = -ENOTBLK;
		goto out;
	}

	if ((r->res_master_nodeid != our_nodeid) && from_dir) {
		/* don't think this should ever happen */
		log_error(ls, "find_rsb inactive from_dir %d master %d",
			  from_nodeid, r->res_master_nodeid);
		dlm_print_rsb(r);
		/* fix it and go on */
		r->res_master_nodeid = our_nodeid;
		r->res_nodeid = 0;
		rsb_clear_flag(r, RSB_MASTER_UNCERTAIN);
		r->res_first_lkid = 0;
	}

	if (from_local && (r->res_master_nodeid != our_nodeid)) {
		/* Because we have held no locks on this rsb,
		   res_master_nodeid could have become stale. */
		rsb_set_flag(r, RSB_MASTER_UNCERTAIN);
		r->res_first_lkid = 0;
	}

	/* A dir record will not be on the scan list. */
	if (r->res_dir_nodeid != our_nodeid)
		del_scan(ls, r);
	list_move(&r->res_slow_list, &ls->ls_slow_active);
	rsb_clear_flag(r, RSB_INACTIVE);
	kref_init(&r->res_ref); /* ref is now used in active state */
	write_unlock_bh(&ls->ls_rsbtbl_lock);

	goto out;


 do_new:
	/*
	 * rsb not found
	 */

	if (error == -EBADR && !create)
		goto out;

	error = get_rsb_struct(ls, name, len, &r);
	if (WARN_ON_ONCE(error))
		goto out;

	r->res_hash = hash;
	r->res_dir_nodeid = dir_nodeid;
	kref_init(&r->res_ref);

	if (from_dir) {
		/* want to see how often this happens */
		log_debug(ls, "find_rsb new from_dir %d recreate %s",
			  from_nodeid, r->res_name);
		r->res_master_nodeid = our_nodeid;
		r->res_nodeid = 0;
		goto out_add;
	}

	if (from_other && (dir_nodeid != our_nodeid)) {
		/* should never happen */
		log_error(ls, "find_rsb new from_other %d dir %d our %d %s",
			  from_nodeid, dir_nodeid, our_nodeid, r->res_name);
		dlm_free_rsb(r);
		r = NULL;
		error = -ENOTBLK;
		goto out;
	}

	if (from_other) {
		log_debug(ls, "find_rsb new from_other %d dir %d %s",
			  from_nodeid, dir_nodeid, r->res_name);
	}

	if (dir_nodeid == our_nodeid) {
		/* When we are the dir nodeid, we can set the master
		   node immediately */
		r->res_master_nodeid = our_nodeid;
		r->res_nodeid = 0;
	} else {
		/* set_master will send_lookup to dir_nodeid */
		r->res_master_nodeid = 0;
		r->res_nodeid = -1;
	}

 out_add:

	write_lock_bh(&ls->ls_rsbtbl_lock);
	error = rsb_insert(r, &ls->ls_rsbtbl);
	if (error == -EEXIST) {
		/* somebody else was faster and it seems the
		 * rsb exists now, we do a whole relookup
		 */
		write_unlock_bh(&ls->ls_rsbtbl_lock);
		dlm_free_rsb(r);
		goto retry;
	} else if (!error) {
		list_add(&r->res_slow_list, &ls->ls_slow_active);
	}
	write_unlock_bh(&ls->ls_rsbtbl_lock);
 out:
	*r_ret = r;
	return error;
}

/* During recovery, other nodes can send us new MSTCPY locks (from
   dlm_recover_locks) before we've made ourself master (in
   dlm_recover_masters). */

static int find_rsb_nodir(struct dlm_ls *ls, const void *name, int len,
			  uint32_t hash, int dir_nodeid, int from_nodeid,
			  unsigned int flags, struct dlm_rsb **r_ret)
{
	struct dlm_rsb *r = NULL;
	int our_nodeid = dlm_our_nodeid();
	int recover = (flags & R_RECEIVE_RECOVER);
	int error;

 retry:
	error = dlm_search_rsb_tree(&ls->ls_rsbtbl, name, len, &r);
	if (error)
		goto do_new;

	/* check if the rsb is in active state under read lock - likely path */
	read_lock_bh(&ls->ls_rsbtbl_lock);
	if (!rsb_flag(r, RSB_HASHED)) {
		read_unlock_bh(&ls->ls_rsbtbl_lock);
		goto do_new;
	}

	if (rsb_flag(r, RSB_INACTIVE)) {
		read_unlock_bh(&ls->ls_rsbtbl_lock);
		goto do_inactive;
	}

	/*
	 * rsb is active, so we can't check master_nodeid without lock_rsb.
	 */

	kref_get(&r->res_ref);
	read_unlock_bh(&ls->ls_rsbtbl_lock);

	goto out;


 do_inactive:
	write_lock_bh(&ls->ls_rsbtbl_lock);

	/* See comment in find_rsb_dir. */
	if (rsb_flag(r, RSB_HASHED)) {
		if (!rsb_flag(r, RSB_INACTIVE)) {
			write_unlock_bh(&ls->ls_rsbtbl_lock);
			goto retry;
		}
	} else {
		write_unlock_bh(&ls->ls_rsbtbl_lock);
		goto do_new;
	}


	/*
	 * rsb found inactive. No other thread is using this rsb because
	 * it's inactive, so we can look at or update res_master_nodeid
	 * without lock_rsb.
	 */

	if (!recover && (r->res_master_nodeid != our_nodeid) && from_nodeid) {
		/* our rsb is not master, and another node has sent us a
		   request; this should never happen */
		log_error(ls, "find_rsb inactive from_nodeid %d master %d dir %d",
			  from_nodeid, r->res_master_nodeid, dir_nodeid);
		dlm_print_rsb(r);
		write_unlock_bh(&ls->ls_rsbtbl_lock);
		error = -ENOTBLK;
		goto out;
	}

	if (!recover && (r->res_master_nodeid != our_nodeid) &&
	    (dir_nodeid == our_nodeid)) {
		/* our rsb is not master, and we are dir; may as well fix it;
		   this should never happen */
		log_error(ls, "find_rsb inactive our %d master %d dir %d",
			  our_nodeid, r->res_master_nodeid, dir_nodeid);
		dlm_print_rsb(r);
		r->res_master_nodeid = our_nodeid;
		r->res_nodeid = 0;
	}

	list_move(&r->res_slow_list, &ls->ls_slow_active);
	rsb_clear_flag(r, RSB_INACTIVE);
	kref_init(&r->res_ref);
	del_scan(ls, r);
	write_unlock_bh(&ls->ls_rsbtbl_lock);

	goto out;


 do_new:
	/*
	 * rsb not found
	 */

	error = get_rsb_struct(ls, name, len, &r);
	if (WARN_ON_ONCE(error))
		goto out;

	r->res_hash = hash;
	r->res_dir_nodeid = dir_nodeid;
	r->res_master_nodeid = dir_nodeid;
	r->res_nodeid = (dir_nodeid == our_nodeid) ? 0 : dir_nodeid;
	kref_init(&r->res_ref);

	write_lock_bh(&ls->ls_rsbtbl_lock);
	error = rsb_insert(r, &ls->ls_rsbtbl);
	if (error == -EEXIST) {
		/* somebody else was faster and it seems the
		 * rsb exists now, we do a whole relookup
		 */
		write_unlock_bh(&ls->ls_rsbtbl_lock);
		dlm_free_rsb(r);
		goto retry;
	} else if (!error) {
		list_add(&r->res_slow_list, &ls->ls_slow_active);
	}
	write_unlock_bh(&ls->ls_rsbtbl_lock);

 out:
	*r_ret = r;
	return error;
}

/*
 * rsb rcu usage
 *
 * While rcu read lock is held, the rsb cannot be freed,
 * which allows a lookup optimization.
 *
 * Two threads are accessing the same rsb concurrently,
 * the first (A) is trying to use the rsb, the second (B)
 * is trying to free the rsb.
 *
 * thread A                 thread B
 * (trying to use rsb)      (trying to free rsb)
 *
 * A1. rcu read lock
 * A2. rsbtbl read lock
 * A3. look up rsb in rsbtbl
 * A4. rsbtbl read unlock
 *                          B1. rsbtbl write lock
 *                          B2. look up rsb in rsbtbl
 *                          B3. remove rsb from rsbtbl
 *                          B4. clear rsb HASHED flag
 *                          B5. rsbtbl write unlock
 *                          B6. begin freeing rsb using rcu...
 *
 * (rsb is inactive, so try to make it active again)
 * A5. read rsb HASHED flag (safe because rsb is not freed yet)
 * A6. the rsb HASHED flag is not set, which it means the rsb
 *     is being removed from rsbtbl and freed, so don't use it.
 * A7. rcu read unlock
 *
 *                          B7. ...finish freeing rsb using rcu
 * A8. create a new rsb
 *
 * Without the rcu optimization, steps A5-8 would need to do
 * an extra rsbtbl lookup:
 * A5. rsbtbl write lock
 * A6. look up rsb in rsbtbl, not found
 * A7. rsbtbl write unlock
 * A8. create a new rsb
 */

static int find_rsb(struct dlm_ls *ls, const void *name, int len,
		    int from_nodeid, unsigned int flags,
		    struct dlm_rsb **r_ret)
{
	int dir_nodeid;
	uint32_t hash;
	int rv;

	if (len > DLM_RESNAME_MAXLEN)
		return -EINVAL;

	hash = jhash(name, len, 0);
	dir_nodeid = dlm_hash2nodeid(ls, hash);

	rcu_read_lock();
	if (dlm_no_directory(ls))
		rv = find_rsb_nodir(ls, name, len, hash, dir_nodeid,
				      from_nodeid, flags, r_ret);
	else
		rv = find_rsb_dir(ls, name, len, hash, dir_nodeid,
				    from_nodeid, flags, r_ret);
	rcu_read_unlock();
	return rv;
}

/* we have received a request and found that res_master_nodeid != our_nodeid,
   so we need to return an error or make ourself the master */

static int validate_master_nodeid(struct dlm_ls *ls, struct dlm_rsb *r,
				  int from_nodeid)
{
	if (dlm_no_directory(ls)) {
		log_error(ls, "find_rsb keep from_nodeid %d master %d dir %d",
			  from_nodeid, r->res_master_nodeid,
			  r->res_dir_nodeid);
		dlm_print_rsb(r);
		return -ENOTBLK;
	}

	if (from_nodeid != r->res_dir_nodeid) {
		/* our rsb is not master, and another node (not the dir node)
	   	   has sent us a request.  this is much more common when our
	   	   master_nodeid is zero, so limit debug to non-zero.  */

		if (r->res_master_nodeid) {
			log_debug(ls, "validate master from_other %d master %d "
				  "dir %d first %x %s", from_nodeid,
				  r->res_master_nodeid, r->res_dir_nodeid,
				  r->res_first_lkid, r->res_name);
		}
		return -ENOTBLK;
	} else {
		/* our rsb is not master, but the dir nodeid has sent us a
	   	   request; this could happen with master 0 / res_nodeid -1 */

		if (r->res_master_nodeid) {
			log_error(ls, "validate master from_dir %d master %d "
				  "first %x %s",
				  from_nodeid, r->res_master_nodeid,
				  r->res_first_lkid, r->res_name);
		}

		r->res_master_nodeid = dlm_our_nodeid();
		r->res_nodeid = 0;
		return 0;
	}
}

static void __dlm_master_lookup(struct dlm_ls *ls, struct dlm_rsb *r, int our_nodeid,
				int from_nodeid, bool is_inactive, unsigned int flags,
				int *r_nodeid, int *result)
{
	int fix_master = (flags & DLM_LU_RECOVER_MASTER);
	int from_master = (flags & DLM_LU_RECOVER_DIR);

	if (r->res_dir_nodeid != our_nodeid) {
		/* should not happen, but may as well fix it and carry on */
		log_error(ls, "%s res_dir %d our %d %s", __func__,
			  r->res_dir_nodeid, our_nodeid, r->res_name);
		r->res_dir_nodeid = our_nodeid;
	}

	if (fix_master && r->res_master_nodeid && dlm_is_removed(ls, r->res_master_nodeid)) {
		/* Recovery uses this function to set a new master when
		 * the previous master failed.  Setting NEW_MASTER will
		 * force dlm_recover_masters to call recover_master on this
		 * rsb even though the res_nodeid is no longer removed.
		 */

		r->res_master_nodeid = from_nodeid;
		r->res_nodeid = from_nodeid;
		rsb_set_flag(r, RSB_NEW_MASTER);

		if (is_inactive) {
			/* I don't think we should ever find it inactive. */
			log_error(ls, "%s fix_master inactive", __func__);
			dlm_dump_rsb(r);
		}
	}

	if (from_master && (r->res_master_nodeid != from_nodeid)) {
		/* this will happen if from_nodeid became master during
		 * a previous recovery cycle, and we aborted the previous
		 * cycle before recovering this master value
		 */

		log_limit(ls, "%s from_master %d master_nodeid %d res_nodeid %d first %x %s",
			  __func__, from_nodeid, r->res_master_nodeid,
			  r->res_nodeid, r->res_first_lkid, r->res_name);

		if (r->res_master_nodeid == our_nodeid) {
			log_error(ls, "from_master %d our_master", from_nodeid);
			dlm_dump_rsb(r);
			goto ret_assign;
		}

		r->res_master_nodeid = from_nodeid;
		r->res_nodeid = from_nodeid;
		rsb_set_flag(r, RSB_NEW_MASTER);
	}

	if (!r->res_master_nodeid) {
		/* this will happen if recovery happens while we're looking
		 * up the master for this rsb
		 */

		log_debug(ls, "%s master 0 to %d first %x %s", __func__,
			  from_nodeid, r->res_first_lkid, r->res_name);
		r->res_master_nodeid = from_nodeid;
		r->res_nodeid = from_nodeid;
	}

	if (!from_master && !fix_master &&
	    (r->res_master_nodeid == from_nodeid)) {
		/* this can happen when the master sends remove, the dir node
		 * finds the rsb on the active list and ignores the remove,
		 * and the former master sends a lookup
		 */

		log_limit(ls, "%s from master %d flags %x first %x %s",
			  __func__, from_nodeid, flags, r->res_first_lkid,
			  r->res_name);
	}

 ret_assign:
	*r_nodeid = r->res_master_nodeid;
	if (result)
		*result = DLM_LU_MATCH;
}

/*
 * We're the dir node for this res and another node wants to know the
 * master nodeid.  During normal operation (non recovery) this is only
 * called from receive_lookup(); master lookups when the local node is
 * the dir node are done by find_rsb().
 *
 * normal operation, we are the dir node for a resource
 * . _request_lock
 * . set_master
 * . send_lookup
 * . receive_lookup
 * . dlm_master_lookup flags 0
 *
 * recover directory, we are rebuilding dir for all resources
 * . dlm_recover_directory
 * . dlm_rcom_names
 *   remote node sends back the rsb names it is master of and we are dir of
 * . dlm_master_lookup RECOVER_DIR (fix_master 0, from_master 1)
 *   we either create new rsb setting remote node as master, or find existing
 *   rsb and set master to be the remote node.
 *
 * recover masters, we are finding the new master for resources
 * . dlm_recover_masters
 * . recover_master
 * . dlm_send_rcom_lookup
 * . receive_rcom_lookup
 * . dlm_master_lookup RECOVER_MASTER (fix_master 1, from_master 0)
 */

static int _dlm_master_lookup(struct dlm_ls *ls, int from_nodeid, const char *name,
			      int len, unsigned int flags, int *r_nodeid, int *result)
{
	struct dlm_rsb *r = NULL;
	uint32_t hash;
	int our_nodeid = dlm_our_nodeid();
	int dir_nodeid, error;

	if (len > DLM_RESNAME_MAXLEN)
		return -EINVAL;

	if (from_nodeid == our_nodeid) {
		log_error(ls, "dlm_master_lookup from our_nodeid %d flags %x",
			  our_nodeid, flags);
		return -EINVAL;
	}

	hash = jhash(name, len, 0);
	dir_nodeid = dlm_hash2nodeid(ls, hash);
	if (dir_nodeid != our_nodeid) {
		log_error(ls, "dlm_master_lookup from %d dir %d our %d h %x %d",
			  from_nodeid, dir_nodeid, our_nodeid, hash,
			  ls->ls_num_nodes);
		*r_nodeid = -1;
		return -EINVAL;
	}

 retry:
	error = dlm_search_rsb_tree(&ls->ls_rsbtbl, name, len, &r);
	if (error)
		goto not_found;

	/* check if the rsb is active under read lock - likely path */
	read_lock_bh(&ls->ls_rsbtbl_lock);
	if (!rsb_flag(r, RSB_HASHED)) {
		read_unlock_bh(&ls->ls_rsbtbl_lock);
		goto not_found;
	}

	if (rsb_flag(r, RSB_INACTIVE)) {
		read_unlock_bh(&ls->ls_rsbtbl_lock);
		goto do_inactive;
	}

	/* because the rsb is active, we need to lock_rsb before
	 * checking/changing re_master_nodeid
	 */

	hold_rsb(r);
	read_unlock_bh(&ls->ls_rsbtbl_lock);
	lock_rsb(r);

	__dlm_master_lookup(ls, r, our_nodeid, from_nodeid, false,
			    flags, r_nodeid, result);

	/* the rsb was active */
	unlock_rsb(r);
	put_rsb(r);

	return 0;

 do_inactive:
	/* unlikely path - check if still part of ls_rsbtbl */
	write_lock_bh(&ls->ls_rsbtbl_lock);

	/* see comment in find_rsb_dir */
	if (rsb_flag(r, RSB_HASHED)) {
		if (!rsb_flag(r, RSB_INACTIVE)) {
			write_unlock_bh(&ls->ls_rsbtbl_lock);
			/* something as changed, very unlikely but
			 * try again
			 */
			goto retry;
		}
	} else {
		write_unlock_bh(&ls->ls_rsbtbl_lock);
		goto not_found;
	}

	/* because the rsb is inactive, it's not refcounted and lock_rsb
	   is not used, but is protected by the rsbtbl lock */

	__dlm_master_lookup(ls, r, our_nodeid, from_nodeid, true, flags,
			    r_nodeid, result);

	/* A dir record rsb should never be on scan list. */
	/* Try to fix this with del_scan? */
	WARN_ON(!list_empty(&r->res_scan_list));

	write_unlock_bh(&ls->ls_rsbtbl_lock);

	return 0;

 not_found:
	error = get_rsb_struct(ls, name, len, &r);
	if (WARN_ON_ONCE(error))
		goto out;

	r->res_hash = hash;
	r->res_dir_nodeid = our_nodeid;
	r->res_master_nodeid = from_nodeid;
	r->res_nodeid = from_nodeid;
	rsb_set_flag(r, RSB_INACTIVE);

	write_lock_bh(&ls->ls_rsbtbl_lock);
	error = rsb_insert(r, &ls->ls_rsbtbl);
	if (error == -EEXIST) {
		/* somebody else was faster and it seems the
		 * rsb exists now, we do a whole relookup
		 */
		write_unlock_bh(&ls->ls_rsbtbl_lock);
		dlm_free_rsb(r);
		goto retry;
	} else if (error) {
		write_unlock_bh(&ls->ls_rsbtbl_lock);
		/* should never happen */
		dlm_free_rsb(r);
		goto retry;
	}

	list_add(&r->res_slow_list, &ls->ls_slow_inactive);
	write_unlock_bh(&ls->ls_rsbtbl_lock);

	if (result)
		*result = DLM_LU_ADD;
	*r_nodeid = from_nodeid;
 out:
	return error;
}

int dlm_master_lookup(struct dlm_ls *ls, int from_nodeid, const char *name,
		      int len, unsigned int flags, int *r_nodeid, int *result)
{
	int rv;
	rcu_read_lock();
	rv = _dlm_master_lookup(ls, from_nodeid, name, len, flags, r_nodeid, result);
	rcu_read_unlock();
	return rv;
}

static void dlm_dump_rsb_hash(struct dlm_ls *ls, uint32_t hash)
{
	struct dlm_rsb *r;

	read_lock_bh(&ls->ls_rsbtbl_lock);
	list_for_each_entry(r, &ls->ls_slow_active, res_slow_list) {
		if (r->res_hash == hash)
			dlm_dump_rsb(r);
	}
	read_unlock_bh(&ls->ls_rsbtbl_lock);
}

void dlm_dump_rsb_name(struct dlm_ls *ls, const char *name, int len)
{
	struct dlm_rsb *r = NULL;
	int error;

	rcu_read_lock();
	error = dlm_search_rsb_tree(&ls->ls_rsbtbl, name, len, &r);
	if (!error)
		goto out;

	dlm_dump_rsb(r);
 out:
	rcu_read_unlock();
}

static void deactivate_rsb(struct kref *kref)
{
	struct dlm_rsb *r = container_of(kref, struct dlm_rsb, res_ref);
	struct dlm_ls *ls = r->res_ls;
	int our_nodeid = dlm_our_nodeid();

	DLM_ASSERT(list_empty(&r->res_root_list), dlm_print_rsb(r););
	rsb_set_flag(r, RSB_INACTIVE);
	list_move(&r->res_slow_list, &ls->ls_slow_inactive);

	/*
	 * When the rsb becomes unused:
	 * - If it's not a dir record for a remote master rsb,
	 *   then it is put on the scan list to be freed.
	 * - If it's a dir record for a remote master rsb,
	 *   then it is kept in the inactive state until
	 *   receive_remove() from the master node.
	 */
	if (!dlm_no_directory(ls) &&
	    (r->res_master_nodeid != our_nodeid) &&
	    (dlm_dir_nodeid(r) != our_nodeid))
		add_scan(ls, r);

	if (r->res_lvbptr) {
		dlm_free_lvb(r->res_lvbptr);
		r->res_lvbptr = NULL;
	}
}

void free_inactive_rsb(struct dlm_rsb *r)
{
	WARN_ON_ONCE(!rsb_flag(r, RSB_INACTIVE));

	DLM_ASSERT(list_empty(&r->res_lookup), dlm_dump_rsb(r););
	DLM_ASSERT(list_empty(&r->res_grantqueue), dlm_dump_rsb(r););
	DLM_ASSERT(list_empty(&r->res_convertqueue), dlm_dump_rsb(r););
	DLM_ASSERT(list_empty(&r->res_waitqueue), dlm_dump_rsb(r););
	DLM_ASSERT(list_empty(&r->res_root_list), dlm_dump_rsb(r););
	DLM_ASSERT(list_empty(&r->res_scan_list), dlm_dump_rsb(r););
	DLM_ASSERT(list_empty(&r->res_recover_list), dlm_dump_rsb(r););
	DLM_ASSERT(list_empty(&r->res_masters_list), dlm_dump_rsb(r););

	dlm_free_rsb(r);
}

/* Attaching/detaching lkb's from rsb's is for rsb reference counting.
   The rsb must exist as long as any lkb's for it do. */

static void attach_lkb(struct dlm_rsb *r, struct dlm_lkb *lkb)
{
	hold_rsb(r);
	lkb->lkb_resource = r;
}

static void detach_lkb(struct dlm_lkb *lkb)
{
	if (lkb->lkb_resource) {
		put_rsb(lkb->lkb_resource);
		lkb->lkb_resource = NULL;
	}
}

static int _create_lkb(struct dlm_ls *ls, struct dlm_lkb **lkb_ret,
		       unsigned long start, unsigned long end)
{
	struct xa_limit limit;
	struct dlm_lkb *lkb;
	int rv;

	limit.max = end;
	limit.min = start;

	lkb = dlm_allocate_lkb();
	if (!lkb)
		return -ENOMEM;

	lkb->lkb_last_bast_cb_mode = DLM_LOCK_IV;
	lkb->lkb_last_cast_cb_mode = DLM_LOCK_IV;
	lkb->lkb_last_cb_mode = DLM_LOCK_IV;
	lkb->lkb_nodeid = -1;
	lkb->lkb_grmode = DLM_LOCK_IV;
	kref_init(&lkb->lkb_ref);
	INIT_LIST_HEAD(&lkb->lkb_ownqueue);
	INIT_LIST_HEAD(&lkb->lkb_rsb_lookup);

	write_lock_bh(&ls->ls_lkbxa_lock);
	rv = xa_alloc(&ls->ls_lkbxa, &lkb->lkb_id, lkb, limit, GFP_ATOMIC);
	write_unlock_bh(&ls->ls_lkbxa_lock);

	if (rv < 0) {
		log_error(ls, "create_lkb xa error %d", rv);
		dlm_free_lkb(lkb);
		return rv;
	}

	*lkb_ret = lkb;
	return 0;
}

static int create_lkb(struct dlm_ls *ls, struct dlm_lkb **lkb_ret)
{
	return _create_lkb(ls, lkb_ret, 1, ULONG_MAX);
}

static int find_lkb(struct dlm_ls *ls, uint32_t lkid, struct dlm_lkb **lkb_ret)
{
	struct dlm_lkb *lkb;

	rcu_read_lock();
	lkb = xa_load(&ls->ls_lkbxa, lkid);
	if (lkb) {
		/* check if lkb is still part of lkbxa under lkbxa_lock as
		 * the lkb_ref is tight to the lkbxa data structure, see
		 * __put_lkb().
		 */
		read_lock_bh(&ls->ls_lkbxa_lock);
		if (kref_read(&lkb->lkb_ref))
			kref_get(&lkb->lkb_ref);
		else
			lkb = NULL;
		read_unlock_bh(&ls->ls_lkbxa_lock);
	}
	rcu_read_unlock();

	*lkb_ret = lkb;
	return lkb ? 0 : -ENOENT;
}

static void kill_lkb(struct kref *kref)
{
	struct dlm_lkb *lkb = container_of(kref, struct dlm_lkb, lkb_ref);

	/* All work is done after the return from kref_put() so we
	   can release the write_lock before the detach_lkb */

	DLM_ASSERT(!lkb->lkb_status, dlm_print_lkb(lkb););
}

/* __put_lkb() is used when an lkb may not have an rsb attached to
   it so we need to provide the lockspace explicitly */

static int __put_lkb(struct dlm_ls *ls, struct dlm_lkb *lkb)
{
	uint32_t lkid = lkb->lkb_id;
	int rv;

	rv = dlm_kref_put_write_lock_bh(&lkb->lkb_ref, kill_lkb,
					&ls->ls_lkbxa_lock);
	if (rv) {
		xa_erase(&ls->ls_lkbxa, lkid);
		write_unlock_bh(&ls->ls_lkbxa_lock);

		detach_lkb(lkb);

		/* for local/process lkbs, lvbptr points to caller's lksb */
		if (lkb->lkb_lvbptr && is_master_copy(lkb))
			dlm_free_lvb(lkb->lkb_lvbptr);
		dlm_free_lkb(lkb);
	}

	return rv;
}

int dlm_put_lkb(struct dlm_lkb *lkb)
{
	struct dlm_ls *ls;

	DLM_ASSERT(lkb->lkb_resource, dlm_print_lkb(lkb););
	DLM_ASSERT(lkb->lkb_resource->res_ls, dlm_print_lkb(lkb););

	ls = lkb->lkb_resource->res_ls;
	return __put_lkb(ls, lkb);
}

/* This is only called to add a reference when the code already holds
   a valid reference to the lkb, so there's no need for locking. */

static inline void hold_lkb(struct dlm_lkb *lkb)
{
	kref_get(&lkb->lkb_ref);
}

static void unhold_lkb_assert(struct kref *kref)
{
	struct dlm_lkb *lkb = container_of(kref, struct dlm_lkb, lkb_ref);

	DLM_ASSERT(false, dlm_print_lkb(lkb););
}

/* This is called when we need to remove a reference and are certain
   it's not the last ref.  e.g. del_lkb is always called between a
   find_lkb/put_lkb and is always the inverse of a previous add_lkb.
   put_lkb would work fine, but would involve unnecessary locking */

static inline void unhold_lkb(struct dlm_lkb *lkb)
{
	kref_put(&lkb->lkb_ref, unhold_lkb_assert);
}

static void lkb_add_ordered(struct list_head *new, struct list_head *head,
			    int mode)
{
	struct dlm_lkb *lkb = NULL, *iter;

	list_for_each_entry(iter, head, lkb_statequeue)
		if (iter->lkb_rqmode < mode) {
			lkb = iter;
			list_add_tail(new, &iter->lkb_statequeue);
			break;
		}

	if (!lkb)
		list_add_tail(new, head);
}

/* add/remove lkb to rsb's grant/convert/wait queue */

static void add_lkb(struct dlm_rsb *r, struct dlm_lkb *lkb, int status)
{
	kref_get(&lkb->lkb_ref);

	DLM_ASSERT(!lkb->lkb_status, dlm_print_lkb(lkb););

	lkb->lkb_timestamp = ktime_get();

	lkb->lkb_status = status;

	switch (status) {
	case DLM_LKSTS_WAITING:
		if (lkb->lkb_exflags & DLM_LKF_HEADQUE)
			list_add(&lkb->lkb_statequeue, &r->res_waitqueue);
		else
			list_add_tail(&lkb->lkb_statequeue, &r->res_waitqueue);
		break;
	case DLM_LKSTS_GRANTED:
		/* convention says granted locks kept in order of grmode */
		lkb_add_ordered(&lkb->lkb_statequeue, &r->res_grantqueue,
				lkb->lkb_grmode);
		break;
	case DLM_LKSTS_CONVERT:
		if (lkb->lkb_exflags & DLM_LKF_HEADQUE)
			list_add(&lkb->lkb_statequeue, &r->res_convertqueue);
		else
			list_add_tail(&lkb->lkb_statequeue,
				      &r->res_convertqueue);
		break;
	default:
		DLM_ASSERT(0, dlm_print_lkb(lkb); printk("sts=%d\n", status););
	}
}

static void del_lkb(struct dlm_rsb *r, struct dlm_lkb *lkb)
{
	lkb->lkb_status = 0;
	list_del(&lkb->lkb_statequeue);
	unhold_lkb(lkb);
}

static void move_lkb(struct dlm_rsb *r, struct dlm_lkb *lkb, int sts)
{
	del_lkb(r, lkb);
	add_lkb(r, lkb, sts);
}

static int msg_reply_type(int mstype)
{
	switch (mstype) {
	case DLM_MSG_REQUEST:
		return DLM_MSG_REQUEST_REPLY;
	case DLM_MSG_CONVERT:
		return DLM_MSG_CONVERT_REPLY;
	case DLM_MSG_UNLOCK:
		return DLM_MSG_UNLOCK_REPLY;
	case DLM_MSG_CANCEL:
		return DLM_MSG_CANCEL_REPLY;
	case DLM_MSG_LOOKUP:
		return DLM_MSG_LOOKUP_REPLY;
	}
	return -1;
}

/* add/remove lkb from global waiters list of lkb's waiting for
   a reply from a remote node */

static int add_to_waiters(struct dlm_lkb *lkb, int mstype, int to_nodeid)
{
	struct dlm_ls *ls = lkb->lkb_resource->res_ls;
	int error = 0;

	spin_lock_bh(&ls->ls_waiters_lock);

	if (is_overlap_unlock(lkb) ||
	    (is_overlap_cancel(lkb) && (mstype == DLM_MSG_CANCEL))) {
		error = -EINVAL;
		goto out;
	}

	if (lkb->lkb_wait_type || is_overlap_cancel(lkb)) {
		switch (mstype) {
		case DLM_MSG_UNLOCK:
			set_bit(DLM_IFL_OVERLAP_UNLOCK_BIT, &lkb->lkb_iflags);
			break;
		case DLM_MSG_CANCEL:
			set_bit(DLM_IFL_OVERLAP_CANCEL_BIT, &lkb->lkb_iflags);
			break;
		default:
			error = -EBUSY;
			goto out;
		}
		lkb->lkb_wait_count++;
		hold_lkb(lkb);

		log_debug(ls, "addwait %x cur %d overlap %d count %d f %x",
			  lkb->lkb_id, lkb->lkb_wait_type, mstype,
			  lkb->lkb_wait_count, dlm_iflags_val(lkb));
		goto out;
	}

	DLM_ASSERT(!lkb->lkb_wait_count,
		   dlm_print_lkb(lkb);
		   printk("wait_count %d\n", lkb->lkb_wait_count););

	lkb->lkb_wait_count++;
	lkb->lkb_wait_type = mstype;
	lkb->lkb_wait_nodeid = to_nodeid; /* for debugging */
	hold_lkb(lkb);
	list_add(&lkb->lkb_wait_reply, &ls->ls_waiters);
 out:
	if (error)
		log_error(ls, "addwait error %x %d flags %x %d %d %s",
			  lkb->lkb_id, error, dlm_iflags_val(lkb), mstype,
			  lkb->lkb_wait_type, lkb->lkb_resource->res_name);
	spin_unlock_bh(&ls->ls_waiters_lock);
	return error;
}

/* We clear the RESEND flag because we might be taking an lkb off the waiters
   list as part of process_requestqueue (e.g. a lookup that has an optimized
   request reply on the requestqueue) between dlm_recover_waiters_pre() which
   set RESEND and dlm_recover_waiters_post() */

static int _remove_from_waiters(struct dlm_lkb *lkb, int mstype,
				const struct dlm_message *ms)
{
	struct dlm_ls *ls = lkb->lkb_resource->res_ls;
	int overlap_done = 0;

	if (mstype == DLM_MSG_UNLOCK_REPLY &&
	    test_and_clear_bit(DLM_IFL_OVERLAP_UNLOCK_BIT, &lkb->lkb_iflags)) {
		log_debug(ls, "remwait %x unlock_reply overlap", lkb->lkb_id);
		overlap_done = 1;
		goto out_del;
	}

	if (mstype == DLM_MSG_CANCEL_REPLY &&
	    test_and_clear_bit(DLM_IFL_OVERLAP_CANCEL_BIT, &lkb->lkb_iflags)) {
		log_debug(ls, "remwait %x cancel_reply overlap", lkb->lkb_id);
		overlap_done = 1;
		goto out_del;
	}

	/* Cancel state was preemptively cleared by a successful convert,
	   see next comment, nothing to do. */

	if ((mstype == DLM_MSG_CANCEL_REPLY) &&
	    (lkb->lkb_wait_type != DLM_MSG_CANCEL)) {
		log_debug(ls, "remwait %x cancel_reply wait_type %d",
			  lkb->lkb_id, lkb->lkb_wait_type);
		return -1;
	}

	/* Remove for the convert reply, and premptively remove for the
	   cancel reply.  A convert has been granted while there's still
	   an outstanding cancel on it (the cancel is moot and the result
	   in the cancel reply should be 0).  We preempt the cancel reply
	   because the app gets the convert result and then can follow up
	   with another op, like convert.  This subsequent op would see the
	   lingering state of the cancel and fail with -EBUSY. */

	if ((mstype == DLM_MSG_CONVERT_REPLY) &&
	    (lkb->lkb_wait_type == DLM_MSG_CONVERT) && ms && !ms->m_result &&
	    test_and_clear_bit(DLM_IFL_OVERLAP_CANCEL_BIT, &lkb->lkb_iflags)) {
		log_debug(ls, "remwait %x convert_reply zap overlap_cancel",
			  lkb->lkb_id);
		lkb->lkb_wait_type = 0;
		lkb->lkb_wait_count--;
		unhold_lkb(lkb);
		goto out_del;
	}

	/* N.B. type of reply may not always correspond to type of original
	   msg due to lookup->request optimization, verify others? */

	if (lkb->lkb_wait_type) {
		lkb->lkb_wait_type = 0;
		goto out_del;
	}

	log_error(ls, "remwait error %x remote %d %x msg %d flags %x no wait",
		  lkb->lkb_id, ms ? le32_to_cpu(ms->m_header.h_nodeid) : 0,
		  lkb->lkb_remid, mstype, dlm_iflags_val(lkb));
	return -1;

 out_del:
	/* the force-unlock/cancel has completed and we haven't recvd a reply
	   to the op that was in progress prior to the unlock/cancel; we
	   give up on any reply to the earlier op.  FIXME: not sure when/how
	   this would happen */

	if (overlap_done && lkb->lkb_wait_type) {
		log_error(ls, "remwait error %x reply %d wait_type %d overlap",
			  lkb->lkb_id, mstype, lkb->lkb_wait_type);
		lkb->lkb_wait_count--;
		unhold_lkb(lkb);
		lkb->lkb_wait_type = 0;
	}

	DLM_ASSERT(lkb->lkb_wait_count, dlm_print_lkb(lkb););

	clear_bit(DLM_IFL_RESEND_BIT, &lkb->lkb_iflags);
	lkb->lkb_wait_count--;
	if (!lkb->lkb_wait_count)
		list_del_init(&lkb->lkb_wait_reply);
	unhold_lkb(lkb);
	return 0;
}

static int remove_from_waiters(struct dlm_lkb *lkb, int mstype)
{
	struct dlm_ls *ls = lkb->lkb_resource->res_ls;
	int error;

	spin_lock_bh(&ls->ls_waiters_lock);
	error = _remove_from_waiters(lkb, mstype, NULL);
	spin_unlock_bh(&ls->ls_waiters_lock);
	return error;
}

/* Handles situations where we might be processing a "fake" or "local" reply in
 * the recovery context which stops any locking activity. Only debugfs might
 * change the lockspace waiters but they will held the recovery lock to ensure
 * remove_from_waiters_ms() in local case will be the only user manipulating the
 * lockspace waiters in recovery context.
 */

static int remove_from_waiters_ms(struct dlm_lkb *lkb,
				  const struct dlm_message *ms, bool local)
{
	struct dlm_ls *ls = lkb->lkb_resource->res_ls;
	int error;

	if (!local)
		spin_lock_bh(&ls->ls_waiters_lock);
	else
		WARN_ON_ONCE(!rwsem_is_locked(&ls->ls_in_recovery) ||
			     !dlm_locking_stopped(ls));
	error = _remove_from_waiters(lkb, le32_to_cpu(ms->m_type), ms);
	if (!local)
		spin_unlock_bh(&ls->ls_waiters_lock);
	return error;
}

/* lkb is master or local copy */

static void set_lvb_lock(struct dlm_rsb *r, struct dlm_lkb *lkb)
{
	int b, len = r->res_ls->ls_lvblen;

	/* b=1 lvb returned to caller
	   b=0 lvb written to rsb or invalidated
	   b=-1 do nothing */

	b =  dlm_lvb_operations[lkb->lkb_grmode + 1][lkb->lkb_rqmode + 1];

	if (b == 1) {
		if (!lkb->lkb_lvbptr)
			return;

		if (!(lkb->lkb_exflags & DLM_LKF_VALBLK))
			return;

		if (!r->res_lvbptr)
			return;

		memcpy(lkb->lkb_lvbptr, r->res_lvbptr, len);
		lkb->lkb_lvbseq = r->res_lvbseq;

	} else if (b == 0) {
		if (lkb->lkb_exflags & DLM_LKF_IVVALBLK) {
			rsb_set_flag(r, RSB_VALNOTVALID);
			return;
		}

		if (!lkb->lkb_lvbptr)
			return;

		if (!(lkb->lkb_exflags & DLM_LKF_VALBLK))
			return;

		if (!r->res_lvbptr)
			r->res_lvbptr = dlm_allocate_lvb(r->res_ls);

		if (!r->res_lvbptr)
			return;

		memcpy(r->res_lvbptr, lkb->lkb_lvbptr, len);
		r->res_lvbseq++;
		lkb->lkb_lvbseq = r->res_lvbseq;
		rsb_clear_flag(r, RSB_VALNOTVALID);
	}

	if (rsb_flag(r, RSB_VALNOTVALID))
		set_bit(DLM_SBF_VALNOTVALID_BIT, &lkb->lkb_sbflags);
}

static void set_lvb_unlock(struct dlm_rsb *r, struct dlm_lkb *lkb)
{
	if (lkb->lkb_grmode < DLM_LOCK_PW)
		return;

	if (lkb->lkb_exflags & DLM_LKF_IVVALBLK) {
		rsb_set_flag(r, RSB_VALNOTVALID);
		return;
	}

	if (!lkb->lkb_lvbptr)
		return;

	if (!(lkb->lkb_exflags & DLM_LKF_VALBLK))
		return;

	if (!r->res_lvbptr)
		r->res_lvbptr = dlm_allocate_lvb(r->res_ls);

	if (!r->res_lvbptr)
		return;

	memcpy(r->res_lvbptr, lkb->lkb_lvbptr, r->res_ls->ls_lvblen);
	r->res_lvbseq++;
	rsb_clear_flag(r, RSB_VALNOTVALID);
}

/* lkb is process copy (pc) */

static void set_lvb_lock_pc(struct dlm_rsb *r, struct dlm_lkb *lkb,
			    const struct dlm_message *ms)
{
	int b;

	if (!lkb->lkb_lvbptr)
		return;

	if (!(lkb->lkb_exflags & DLM_LKF_VALBLK))
		return;

	b = dlm_lvb_operations[lkb->lkb_grmode + 1][lkb->lkb_rqmode + 1];
	if (b == 1) {
		int len = receive_extralen(ms);
		if (len > r->res_ls->ls_lvblen)
			len = r->res_ls->ls_lvblen;
		memcpy(lkb->lkb_lvbptr, ms->m_extra, len);
		lkb->lkb_lvbseq = le32_to_cpu(ms->m_lvbseq);
	}
}

/* Manipulate lkb's on rsb's convert/granted/waiting queues
   remove_lock -- used for unlock, removes lkb from granted
   revert_lock -- used for cancel, moves lkb from convert to granted
   grant_lock  -- used for request and convert, adds lkb to granted or
                  moves lkb from convert or waiting to granted

   Each of these is used for master or local copy lkb's.  There is
   also a _pc() variation used to make the corresponding change on
   a process copy (pc) lkb. */

static void _remove_lock(struct dlm_rsb *r, struct dlm_lkb *lkb)
{
	del_lkb(r, lkb);
	lkb->lkb_grmode = DLM_LOCK_IV;
	/* this unhold undoes the original ref from create_lkb()
	   so this leads to the lkb being freed */
	unhold_lkb(lkb);
}

static void remove_lock(struct dlm_rsb *r, struct dlm_lkb *lkb)
{
	set_lvb_unlock(r, lkb);
	_remove_lock(r, lkb);
}

static void remove_lock_pc(struct dlm_rsb *r, struct dlm_lkb *lkb)
{
	_remove_lock(r, lkb);
}

/* returns: 0 did nothing
	    1 moved lock to granted
	   -1 removed lock */

static int revert_lock(struct dlm_rsb *r, struct dlm_lkb *lkb)
{
	int rv = 0;

	lkb->lkb_rqmode = DLM_LOCK_IV;

	switch (lkb->lkb_status) {
	case DLM_LKSTS_GRANTED:
		break;
	case DLM_LKSTS_CONVERT:
		move_lkb(r, lkb, DLM_LKSTS_GRANTED);
		rv = 1;
		break;
	case DLM_LKSTS_WAITING:
		del_lkb(r, lkb);
		lkb->lkb_grmode = DLM_LOCK_IV;
		/* this unhold undoes the original ref from create_lkb()
		   so this leads to the lkb being freed */
		unhold_lkb(lkb);
		rv = -1;
		break;
	default:
		log_print("invalid status for revert %d", lkb->lkb_status);
	}
	return rv;
}

static int revert_lock_pc(struct dlm_rsb *r, struct dlm_lkb *lkb)
{
	return revert_lock(r, lkb);
}

static void _grant_lock(struct dlm_rsb *r, struct dlm_lkb *lkb)
{
	if (lkb->lkb_grmode != lkb->lkb_rqmode) {
		lkb->lkb_grmode = lkb->lkb_rqmode;
		if (lkb->lkb_status)
			move_lkb(r, lkb, DLM_LKSTS_GRANTED);
		else
			add_lkb(r, lkb, DLM_LKSTS_GRANTED);
	}

	lkb->lkb_rqmode = DLM_LOCK_IV;
	lkb->lkb_highbast = 0;
}

static void grant_lock(struct dlm_rsb *r, struct dlm_lkb *lkb)
{
	set_lvb_lock(r, lkb);
	_grant_lock(r, lkb);
}

static void grant_lock_pc(struct dlm_rsb *r, struct dlm_lkb *lkb,
			  const struct dlm_message *ms)
{
	set_lvb_lock_pc(r, lkb, ms);
	_grant_lock(r, lkb);
}

/* called by grant_pending_locks() which means an async grant message must
   be sent to the requesting node in addition to granting the lock if the
   lkb belongs to a remote node. */

static void grant_lock_pending(struct dlm_rsb *r, struct dlm_lkb *lkb)
{
	grant_lock(r, lkb);
	if (is_master_copy(lkb))
		send_grant(r, lkb);
	else
		queue_cast(r, lkb, 0);
}

/* The special CONVDEADLK, ALTPR and ALTCW flags allow the master to
   change the granted/requested modes.  We're munging things accordingly in
   the process copy.
   CONVDEADLK: our grmode may have been forced down to NL to resolve a
   conversion deadlock
   ALTPR/ALTCW: our rqmode may have been changed to PR or CW to become
   compatible with other granted locks */

static void munge_demoted(struct dlm_lkb *lkb)
{
	if (lkb->lkb_rqmode == DLM_LOCK_IV || lkb->lkb_grmode == DLM_LOCK_IV) {
		log_print("munge_demoted %x invalid modes gr %d rq %d",
			  lkb->lkb_id, lkb->lkb_grmode, lkb->lkb_rqmode);
		return;
	}

	lkb->lkb_grmode = DLM_LOCK_NL;
}

static void munge_altmode(struct dlm_lkb *lkb, const struct dlm_message *ms)
{
	if (ms->m_type != cpu_to_le32(DLM_MSG_REQUEST_REPLY) &&
	    ms->m_type != cpu_to_le32(DLM_MSG_GRANT)) {
		log_print("munge_altmode %x invalid reply type %d",
			  lkb->lkb_id, le32_to_cpu(ms->m_type));
		return;
	}

	if (lkb->lkb_exflags & DLM_LKF_ALTPR)
		lkb->lkb_rqmode = DLM_LOCK_PR;
	else if (lkb->lkb_exflags & DLM_LKF_ALTCW)
		lkb->lkb_rqmode = DLM_LOCK_CW;
	else {
		log_print("munge_altmode invalid exflags %x", lkb->lkb_exflags);
		dlm_print_lkb(lkb);
	}
}

static inline int first_in_list(struct dlm_lkb *lkb, struct list_head *head)
{
	struct dlm_lkb *first = list_entry(head->next, struct dlm_lkb,
					   lkb_statequeue);
	if (lkb->lkb_id == first->lkb_id)
		return 1;

	return 0;
}

/* Check if the given lkb conflicts with another lkb on the queue. */

static int queue_conflict(struct list_head *head, struct dlm_lkb *lkb)
{
	struct dlm_lkb *this;

	list_for_each_entry(this, head, lkb_statequeue) {
		if (this == lkb)
			continue;
		if (!modes_compat(this, lkb))
			return 1;
	}
	return 0;
}

/*
 * "A conversion deadlock arises with a pair of lock requests in the converting
 * queue for one resource.  The granted mode of each lock blocks the requested
 * mode of the other lock."
 *
 * Part 2: if the granted mode of lkb is preventing an earlier lkb in the
 * convert queue from being granted, then deadlk/demote lkb.
 *
 * Example:
 * Granted Queue: empty
 * Convert Queue: NL->EX (first lock)
 *                PR->EX (second lock)
 *
 * The first lock can't be granted because of the granted mode of the second
 * lock and the second lock can't be granted because it's not first in the
 * list.  We either cancel lkb's conversion (PR->EX) and return EDEADLK, or we
 * demote the granted mode of lkb (from PR to NL) if it has the CONVDEADLK
 * flag set and return DEMOTED in the lksb flags.
 *
 * Originally, this function detected conv-deadlk in a more limited scope:
 * - if !modes_compat(lkb1, lkb2) && !modes_compat(lkb2, lkb1), or
 * - if lkb1 was the first entry in the queue (not just earlier), and was
 *   blocked by the granted mode of lkb2, and there was nothing on the
 *   granted queue preventing lkb1 from being granted immediately, i.e.
 *   lkb2 was the only thing preventing lkb1 from being granted.
 *
 * That second condition meant we'd only say there was conv-deadlk if
 * resolving it (by demotion) would lead to the first lock on the convert
 * queue being granted right away.  It allowed conversion deadlocks to exist
 * between locks on the convert queue while they couldn't be granted anyway.
 *
 * Now, we detect and take action on conversion deadlocks immediately when
 * they're created, even if they may not be immediately consequential.  If
 * lkb1 exists anywhere in the convert queue and lkb2 comes in with a granted
 * mode that would prevent lkb1's conversion from being granted, we do a
 * deadlk/demote on lkb2 right away and don't let it onto the convert queue.
 * I think this means that the lkb_is_ahead condition below should always
 * be zero, i.e. there will never be conv-deadlk between two locks that are
 * both already on the convert queue.
 */

static int conversion_deadlock_detect(struct dlm_rsb *r, struct dlm_lkb *lkb2)
{
	struct dlm_lkb *lkb1;
	int lkb_is_ahead = 0;

	list_for_each_entry(lkb1, &r->res_convertqueue, lkb_statequeue) {
		if (lkb1 == lkb2) {
			lkb_is_ahead = 1;
			continue;
		}

		if (!lkb_is_ahead) {
			if (!modes_compat(lkb2, lkb1))
				return 1;
		} else {
			if (!modes_compat(lkb2, lkb1) &&
			    !modes_compat(lkb1, lkb2))
				return 1;
		}
	}
	return 0;
}

/*
 * Return 1 if the lock can be granted, 0 otherwise.
 * Also detect and resolve conversion deadlocks.
 *
 * lkb is the lock to be granted
 *
 * now is 1 if the function is being called in the context of the
 * immediate request, it is 0 if called later, after the lock has been
 * queued.
 *
 * recover is 1 if dlm_recover_grant() is trying to grant conversions
 * after recovery.
 *
 * References are from chapter 6 of "VAXcluster Principles" by Roy Davis
 */

static int _can_be_granted(struct dlm_rsb *r, struct dlm_lkb *lkb, int now,
			   int recover)
{
	int8_t conv = (lkb->lkb_grmode != DLM_LOCK_IV);

	/*
	 * 6-10: Version 5.4 introduced an option to address the phenomenon of
	 * a new request for a NL mode lock being blocked.
	 *
	 * 6-11: If the optional EXPEDITE flag is used with the new NL mode
	 * request, then it would be granted.  In essence, the use of this flag
	 * tells the Lock Manager to expedite theis request by not considering
	 * what may be in the CONVERTING or WAITING queues...  As of this
	 * writing, the EXPEDITE flag can be used only with new requests for NL
	 * mode locks.  This flag is not valid for conversion requests.
	 *
	 * A shortcut.  Earlier checks return an error if EXPEDITE is used in a
	 * conversion or used with a non-NL requested mode.  We also know an
	 * EXPEDITE request is always granted immediately, so now must always
	 * be 1.  The full condition to grant an expedite request: (now &&
	 * !conv && lkb->rqmode == DLM_LOCK_NL && (flags & EXPEDITE)) can
	 * therefore be shortened to just checking the flag.
	 */

	if (lkb->lkb_exflags & DLM_LKF_EXPEDITE)
		return 1;

	/*
	 * A shortcut. Without this, !queue_conflict(grantqueue, lkb) would be
	 * added to the remaining conditions.
	 */

	if (queue_conflict(&r->res_grantqueue, lkb))
		return 0;

	/*
	 * 6-3: By default, a conversion request is immediately granted if the
	 * requested mode is compatible with the modes of all other granted
	 * locks
	 */

	if (queue_conflict(&r->res_convertqueue, lkb))
		return 0;

	/*
	 * The RECOVER_GRANT flag means dlm_recover_grant() is granting
	 * locks for a recovered rsb, on which lkb's have been rebuilt.
	 * The lkb's may have been rebuilt on the queues in a different
	 * order than they were in on the previous master.  So, granting
	 * queued conversions in order after recovery doesn't make sense
	 * since the order hasn't been preserved anyway.  The new order
	 * could also have created a new "in place" conversion deadlock.
	 * (e.g. old, failed master held granted EX, with PR->EX, NL->EX.
	 * After recovery, there would be no granted locks, and possibly
	 * NL->EX, PR->EX, an in-place conversion deadlock.)  So, after
	 * recovery, grant conversions without considering order.
	 */

	if (conv && recover)
		return 1;

	/*
	 * 6-5: But the default algorithm for deciding whether to grant or
	 * queue conversion requests does not by itself guarantee that such
	 * requests are serviced on a "first come first serve" basis.  This, in
	 * turn, can lead to a phenomenon known as "indefinate postponement".
	 *
	 * 6-7: This issue is dealt with by using the optional QUECVT flag with
	 * the system service employed to request a lock conversion.  This flag
	 * forces certain conversion requests to be queued, even if they are
	 * compatible with the granted modes of other locks on the same
	 * resource.  Thus, the use of this flag results in conversion requests
	 * being ordered on a "first come first servce" basis.
	 *
	 * DCT: This condition is all about new conversions being able to occur
	 * "in place" while the lock remains on the granted queue (assuming
	 * nothing else conflicts.)  IOW if QUECVT isn't set, a conversion
	 * doesn't _have_ to go onto the convert queue where it's processed in
	 * order.  The "now" variable is necessary to distinguish converts
	 * being received and processed for the first time now, because once a
	 * convert is moved to the conversion queue the condition below applies
	 * requiring fifo granting.
	 */

	if (now && conv && !(lkb->lkb_exflags & DLM_LKF_QUECVT))
		return 1;

	/*
	 * Even if the convert is compat with all granted locks,
	 * QUECVT forces it behind other locks on the convert queue.
	 */

	if (now && conv && (lkb->lkb_exflags & DLM_LKF_QUECVT)) {
		if (list_empty(&r->res_convertqueue))
			return 1;
		else
			return 0;
	}

	/*
	 * The NOORDER flag is set to avoid the standard vms rules on grant
	 * order.
	 */

	if (lkb->lkb_exflags & DLM_LKF_NOORDER)
		return 1;

	/*
	 * 6-3: Once in that queue [CONVERTING], a conversion request cannot be
	 * granted until all other conversion requests ahead of it are granted
	 * and/or canceled.
	 */

	if (!now && conv && first_in_list(lkb, &r->res_convertqueue))
		return 1;

	/*
	 * 6-4: By default, a new request is immediately granted only if all
	 * three of the following conditions are satisfied when the request is
	 * issued:
	 * - The queue of ungranted conversion requests for the resource is
	 *   empty.
	 * - The queue of ungranted new requests for the resource is empty.
	 * - The mode of the new request is compatible with the most
	 *   restrictive mode of all granted locks on the resource.
	 */

	if (now && !conv && list_empty(&r->res_convertqueue) &&
	    list_empty(&r->res_waitqueue))
		return 1;

	/*
	 * 6-4: Once a lock request is in the queue of ungranted new requests,
	 * it cannot be granted until the queue of ungranted conversion
	 * requests is empty, all ungranted new requests ahead of it are
	 * granted and/or canceled, and it is compatible with the granted mode
	 * of the most restrictive lock granted on the resource.
	 */

	if (!now && !conv && list_empty(&r->res_convertqueue) &&
	    first_in_list(lkb, &r->res_waitqueue))
		return 1;

	return 0;
}

static int can_be_granted(struct dlm_rsb *r, struct dlm_lkb *lkb, int now,
			  int recover, int *err)
{
	int rv;
	int8_t alt = 0, rqmode = lkb->lkb_rqmode;
	int8_t is_convert = (lkb->lkb_grmode != DLM_LOCK_IV);

	if (err)
		*err = 0;

	rv = _can_be_granted(r, lkb, now, recover);
	if (rv)
		goto out;

	/*
	 * The CONVDEADLK flag is non-standard and tells the dlm to resolve
	 * conversion deadlocks by demoting grmode to NL, otherwise the dlm
	 * cancels one of the locks.
	 */

	if (is_convert && can_be_queued(lkb) &&
	    conversion_deadlock_detect(r, lkb)) {
		if (lkb->lkb_exflags & DLM_LKF_CONVDEADLK) {
			lkb->lkb_grmode = DLM_LOCK_NL;
			set_bit(DLM_SBF_DEMOTED_BIT, &lkb->lkb_sbflags);
		} else if (err) {
			*err = -EDEADLK;
		} else {
			log_print("can_be_granted deadlock %x now %d",
				  lkb->lkb_id, now);
			dlm_dump_rsb(r);
		}
		goto out;
	}

	/*
	 * The ALTPR and ALTCW flags are non-standard and tell the dlm to try
	 * to grant a request in a mode other than the normal rqmode.  It's a
	 * simple way to provide a big optimization to applications that can
	 * use them.
	 */

	if (rqmode != DLM_LOCK_PR && (lkb->lkb_exflags & DLM_LKF_ALTPR))
		alt = DLM_LOCK_PR;
	else if (rqmode != DLM_LOCK_CW && (lkb->lkb_exflags & DLM_LKF_ALTCW))
		alt = DLM_LOCK_CW;

	if (alt) {
		lkb->lkb_rqmode = alt;
		rv = _can_be_granted(r, lkb, now, 0);
		if (rv)
			set_bit(DLM_SBF_ALTMODE_BIT, &lkb->lkb_sbflags);
		else
			lkb->lkb_rqmode = rqmode;
	}
 out:
	return rv;
}

/* Returns the highest requested mode of all blocked conversions; sets
   cw if there's a blocked conversion to DLM_LOCK_CW. */

static int grant_pending_convert(struct dlm_rsb *r, int high, int *cw,
				 unsigned int *count)
{
	struct dlm_lkb *lkb, *s;
	int recover = rsb_flag(r, RSB_RECOVER_GRANT);
	int hi, demoted, quit, grant_restart, demote_restart;
	int deadlk;

	quit = 0;
 restart:
	grant_restart = 0;
	demote_restart = 0;
	hi = DLM_LOCK_IV;

	list_for_each_entry_safe(lkb, s, &r->res_convertqueue, lkb_statequeue) {
		demoted = is_demoted(lkb);
		deadlk = 0;

		if (can_be_granted(r, lkb, 0, recover, &deadlk)) {
			grant_lock_pending(r, lkb);
			grant_restart = 1;
			if (count)
				(*count)++;
			continue;
		}

		if (!demoted && is_demoted(lkb)) {
			log_print("WARN: pending demoted %x node %d %s",
				  lkb->lkb_id, lkb->lkb_nodeid, r->res_name);
			demote_restart = 1;
			continue;
		}

		if (deadlk) {
			/*
			 * If DLM_LKB_NODLKWT flag is set and conversion
			 * deadlock is detected, we request blocking AST and
			 * down (or cancel) conversion.
			 */
			if (lkb->lkb_exflags & DLM_LKF_NODLCKWT) {
				if (lkb->lkb_highbast < lkb->lkb_rqmode) {
					queue_bast(r, lkb, lkb->lkb_rqmode);
					lkb->lkb_highbast = lkb->lkb_rqmode;
				}
			} else {
				log_print("WARN: pending deadlock %x node %d %s",
					  lkb->lkb_id, lkb->lkb_nodeid,
					  r->res_name);
				dlm_dump_rsb(r);
			}
			continue;
		}

		hi = max_t(int, lkb->lkb_rqmode, hi);

		if (cw && lkb->lkb_rqmode == DLM_LOCK_CW)
			*cw = 1;
	}

	if (grant_restart)
		goto restart;
	if (demote_restart && !quit) {
		quit = 1;
		goto restart;
	}

	return max_t(int, high, hi);
}

static int grant_pending_wait(struct dlm_rsb *r, int high, int *cw,
			      unsigned int *count)
{
	struct dlm_lkb *lkb, *s;

	list_for_each_entry_safe(lkb, s, &r->res_waitqueue, lkb_statequeue) {
		if (can_be_granted(r, lkb, 0, 0, NULL)) {
			grant_lock_pending(r, lkb);
			if (count)
				(*count)++;
		} else {
			high = max_t(int, lkb->lkb_rqmode, high);
			if (lkb->lkb_rqmode == DLM_LOCK_CW)
				*cw = 1;
		}
	}

	return high;
}

/* cw of 1 means there's a lock with a rqmode of DLM_LOCK_CW that's blocked
   on either the convert or waiting queue.
   high is the largest rqmode of all locks blocked on the convert or
   waiting queue. */

static int lock_requires_bast(struct dlm_lkb *gr, int high, int cw)
{
	if (gr->lkb_grmode == DLM_LOCK_PR && cw) {
		if (gr->lkb_highbast < DLM_LOCK_EX)
			return 1;
		return 0;
	}

	if (gr->lkb_highbast < high &&
	    !__dlm_compat_matrix[gr->lkb_grmode+1][high+1])
		return 1;
	return 0;
}

static void grant_pending_locks(struct dlm_rsb *r, unsigned int *count)
{
	struct dlm_lkb *lkb, *s;
	int high = DLM_LOCK_IV;
	int cw = 0;

	if (!is_master(r)) {
		log_print("grant_pending_locks r nodeid %d", r->res_nodeid);
		dlm_dump_rsb(r);
		return;
	}

	high = grant_pending_convert(r, high, &cw, count);
	high = grant_pending_wait(r, high, &cw, count);

	if (high == DLM_LOCK_IV)
		return;

	/*
	 * If there are locks left on the wait/convert queue then send blocking
	 * ASTs to granted locks based on the largest requested mode (high)
	 * found above.
	 */

	list_for_each_entry_safe(lkb, s, &r->res_grantqueue, lkb_statequeue) {
		if (lkb->lkb_bastfn && lock_requires_bast(lkb, high, cw)) {
			if (cw && high == DLM_LOCK_PR &&
			    lkb->lkb_grmode == DLM_LOCK_PR)
				queue_bast(r, lkb, DLM_LOCK_CW);
			else
				queue_bast(r, lkb, high);
			lkb->lkb_highbast = high;
		}
	}
}

static int modes_require_bast(struct dlm_lkb *gr, struct dlm_lkb *rq)
{
	if ((gr->lkb_grmode == DLM_LOCK_PR && rq->lkb_rqmode == DLM_LOCK_CW) ||
	    (gr->lkb_grmode == DLM_LOCK_CW && rq->lkb_rqmode == DLM_LOCK_PR)) {
		if (gr->lkb_highbast < DLM_LOCK_EX)
			return 1;
		return 0;
	}

	if (gr->lkb_highbast < rq->lkb_rqmode && !modes_compat(gr, rq))
		return 1;
	return 0;
}

static void send_bast_queue(struct dlm_rsb *r, struct list_head *head,
			    struct dlm_lkb *lkb)
{
	struct dlm_lkb *gr;

	list_for_each_entry(gr, head, lkb_statequeue) {
		/* skip self when sending basts to convertqueue */
		if (gr == lkb)
			continue;
		if (gr->lkb_bastfn && modes_require_bast(gr, lkb)) {
			queue_bast(r, gr, lkb->lkb_rqmode);
			gr->lkb_highbast = lkb->lkb_rqmode;
		}
	}
}

static void send_blocking_asts(struct dlm_rsb *r, struct dlm_lkb *lkb)
{
	send_bast_queue(r, &r->res_grantqueue, lkb);
}

static void send_blocking_asts_all(struct dlm_rsb *r, struct dlm_lkb *lkb)
{
	send_bast_queue(r, &r->res_grantqueue, lkb);
	send_bast_queue(r, &r->res_convertqueue, lkb);
}

/* set_master(r, lkb) -- set the master nodeid of a resource

   The purpose of this function is to set the nodeid field in the given
   lkb using the nodeid field in the given rsb.  If the rsb's nodeid is
   known, it can just be copied to the lkb and the function will return
   0.  If the rsb's nodeid is _not_ known, it needs to be looked up
   before it can be copied to the lkb.

   When the rsb nodeid is being looked up remotely, the initial lkb
   causing the lookup is kept on the ls_waiters list waiting for the
   lookup reply.  Other lkb's waiting for the same rsb lookup are kept
   on the rsb's res_lookup list until the master is verified.

   Return values:
   0: nodeid is set in rsb/lkb and the caller should go ahead and use it
   1: the rsb master is not available and the lkb has been placed on
      a wait queue
*/

static int set_master(struct dlm_rsb *r, struct dlm_lkb *lkb)
{
	int our_nodeid = dlm_our_nodeid();

	if (rsb_flag(r, RSB_MASTER_UNCERTAIN)) {
		rsb_clear_flag(r, RSB_MASTER_UNCERTAIN);
		r->res_first_lkid = lkb->lkb_id;
		lkb->lkb_nodeid = r->res_nodeid;
		return 0;
	}

	if (r->res_first_lkid && r->res_first_lkid != lkb->lkb_id) {
		list_add_tail(&lkb->lkb_rsb_lookup, &r->res_lookup);
		return 1;
	}

	if (r->res_master_nodeid == our_nodeid) {
		lkb->lkb_nodeid = 0;
		return 0;
	}

	if (r->res_master_nodeid) {
		lkb->lkb_nodeid = r->res_master_nodeid;
		return 0;
	}

	if (dlm_dir_nodeid(r) == our_nodeid) {
		/* This is a somewhat unusual case; find_rsb will usually
		   have set res_master_nodeid when dir nodeid is local, but
		   there are cases where we become the dir node after we've
		   past find_rsb and go through _request_lock again.
		   confirm_master() or process_lookup_list() needs to be
		   called after this. */
		log_debug(r->res_ls, "set_master %x self master %d dir %d %s",
			  lkb->lkb_id, r->res_master_nodeid, r->res_dir_nodeid,
			  r->res_name);
		r->res_master_nodeid = our_nodeid;
		r->res_nodeid = 0;
		lkb->lkb_nodeid = 0;
		return 0;
	}

	r->res_first_lkid = lkb->lkb_id;
	send_lookup(r, lkb);
	return 1;
}

static void process_lookup_list(struct dlm_rsb *r)
{
	struct dlm_lkb *lkb, *safe;

	list_for_each_entry_safe(lkb, safe, &r->res_lookup, lkb_rsb_lookup) {
		list_del_init(&lkb->lkb_rsb_lookup);
		_request_lock(r, lkb);
	}
}

/* confirm_master -- confirm (or deny) an rsb's master nodeid */

static void confirm_master(struct dlm_rsb *r, int error)
{
	struct dlm_lkb *lkb;

	if (!r->res_first_lkid)
		return;

	switch (error) {
	case 0:
	case -EINPROGRESS:
		r->res_first_lkid = 0;
		process_lookup_list(r);
		break;

	case -EAGAIN:
	case -EBADR:
	case -ENOTBLK:
		/* the remote request failed and won't be retried (it was
		   a NOQUEUE, or has been canceled/unlocked); make a waiting
		   lkb the first_lkid */

		r->res_first_lkid = 0;

		if (!list_empty(&r->res_lookup)) {
			lkb = list_entry(r->res_lookup.next, struct dlm_lkb,
					 lkb_rsb_lookup);
			list_del_init(&lkb->lkb_rsb_lookup);
			r->res_first_lkid = lkb->lkb_id;
			_request_lock(r, lkb);
		}
		break;

	default:
		log_error(r->res_ls, "confirm_master unknown error %d", error);
	}
}

static int set_lock_args(int mode, struct dlm_lksb *lksb, uint32_t flags,
			 int namelen, void (*ast)(void *astparam),
			 void *astparam,
			 void (*bast)(void *astparam, int mode),
			 struct dlm_args *args)
{
	int rv = -EINVAL;

	/* check for invalid arg usage */

	if (mode < 0 || mode > DLM_LOCK_EX)
		goto out;

	if (!(flags & DLM_LKF_CONVERT) && (namelen > DLM_RESNAME_MAXLEN))
		goto out;

	if (flags & DLM_LKF_CANCEL)
		goto out;

	if (flags & DLM_LKF_QUECVT && !(flags & DLM_LKF_CONVERT))
		goto out;

	if (flags & DLM_LKF_CONVDEADLK && !(flags & DLM_LKF_CONVERT))
		goto out;

	if (flags & DLM_LKF_CONVDEADLK && flags & DLM_LKF_NOQUEUE)
		goto out;

	if (flags & DLM_LKF_EXPEDITE && flags & DLM_LKF_CONVERT)
		goto out;

	if (flags & DLM_LKF_EXPEDITE && flags & DLM_LKF_QUECVT)
		goto out;

	if (flags & DLM_LKF_EXPEDITE && flags & DLM_LKF_NOQUEUE)
		goto out;

	if (flags & DLM_LKF_EXPEDITE && mode != DLM_LOCK_NL)
		goto out;

	if (!ast || !lksb)
		goto out;

	if (flags & DLM_LKF_VALBLK && !lksb->sb_lvbptr)
		goto out;

	if (flags & DLM_LKF_CONVERT && !lksb->sb_lkid)
		goto out;

	/* these args will be copied to the lkb in validate_lock_args,
	   it cannot be done now because when converting locks, fields in
	   an active lkb cannot be modified before locking the rsb */

	args->flags = flags;
	args->astfn = ast;
	args->astparam = astparam;
	args->bastfn = bast;
	args->mode = mode;
	args->lksb = lksb;
	rv = 0;
 out:
	return rv;
}

static int set_unlock_args(uint32_t flags, void *astarg, struct dlm_args *args)
{
	if (flags & ~(DLM_LKF_CANCEL | DLM_LKF_VALBLK | DLM_LKF_IVVALBLK |
 		      DLM_LKF_FORCEUNLOCK))
		return -EINVAL;

	if (flags & DLM_LKF_CANCEL && flags & DLM_LKF_FORCEUNLOCK)
		return -EINVAL;

	args->flags = flags;
	args->astparam = astarg;
	return 0;
}

static int validate_lock_args(struct dlm_ls *ls, struct dlm_lkb *lkb,
			      struct dlm_args *args)
{
	int rv = -EBUSY;

	if (args->flags & DLM_LKF_CONVERT) {
		if (lkb->lkb_status != DLM_LKSTS_GRANTED)
			goto out;

		/* lock not allowed if there's any op in progress */
		if (lkb->lkb_wait_type || lkb->lkb_wait_count)
			goto out;

		if (is_overlap(lkb))
			goto out;

		rv = -EINVAL;
		if (test_bit(DLM_IFL_MSTCPY_BIT, &lkb->lkb_iflags))
			goto out;

		if (args->flags & DLM_LKF_QUECVT &&
		    !__quecvt_compat_matrix[lkb->lkb_grmode+1][args->mode+1])
			goto out;
	}

	lkb->lkb_exflags = args->flags;
	dlm_set_sbflags_val(lkb, 0);
	lkb->lkb_astfn = args->astfn;
	lkb->lkb_astparam = args->astparam;
	lkb->lkb_bastfn = args->bastfn;
	lkb->lkb_rqmode = args->mode;
	lkb->lkb_lksb = args->lksb;
	lkb->lkb_lvbptr = args->lksb->sb_lvbptr;
	lkb->lkb_ownpid = (int) current->pid;
	rv = 0;
 out:
	switch (rv) {
	case 0:
		break;
	case -EINVAL:
		/* annoy the user because dlm usage is wrong */
		WARN_ON(1);
		log_error(ls, "%s %d %x %x %x %d %d %s", __func__,
			  rv, lkb->lkb_id, dlm_iflags_val(lkb), args->flags,
			  lkb->lkb_status, lkb->lkb_wait_type,
			  lkb->lkb_resource->res_name);
		break;
	default:
		log_debug(ls, "%s %d %x %x %x %d %d %s", __func__,
			  rv, lkb->lkb_id, dlm_iflags_val(lkb), args->flags,
			  lkb->lkb_status, lkb->lkb_wait_type,
			  lkb->lkb_resource->res_name);
		break;
	}

	return rv;
}

/* when dlm_unlock() sees -EBUSY with CANCEL/FORCEUNLOCK it returns 0
   for success */

/* note: it's valid for lkb_nodeid/res_nodeid to be -1 when we get here
   because there may be a lookup in progress and it's valid to do
   cancel/unlockf on it */

static int validate_unlock_args(struct dlm_lkb *lkb, struct dlm_args *args)
{
	struct dlm_ls *ls = lkb->lkb_resource->res_ls;
	int rv = -EBUSY;

	/* normal unlock not allowed if there's any op in progress */
	if (!(args->flags & (DLM_LKF_CANCEL | DLM_LKF_FORCEUNLOCK)) &&
	    (lkb->lkb_wait_type || lkb->lkb_wait_count))
		goto out;

	/* an lkb may be waiting for an rsb lookup to complete where the
	   lookup was initiated by another lock */

	if (!list_empty(&lkb->lkb_rsb_lookup)) {
		if (args->flags & (DLM_LKF_CANCEL | DLM_LKF_FORCEUNLOCK)) {
			log_debug(ls, "unlock on rsb_lookup %x", lkb->lkb_id);
			list_del_init(&lkb->lkb_rsb_lookup);
			queue_cast(lkb->lkb_resource, lkb,
				   args->flags & DLM_LKF_CANCEL ?
				   -DLM_ECANCEL : -DLM_EUNLOCK);
			unhold_lkb(lkb); /* undoes create_lkb() */
		}
		/* caller changes -EBUSY to 0 for CANCEL and FORCEUNLOCK */
		goto out;
	}

	rv = -EINVAL;
	if (test_bit(DLM_IFL_MSTCPY_BIT, &lkb->lkb_iflags)) {
		log_error(ls, "unlock on MSTCPY %x", lkb->lkb_id);
		dlm_print_lkb(lkb);
		goto out;
	}

	/* an lkb may still exist even though the lock is EOL'ed due to a
	 * cancel, unlock or failed noqueue request; an app can't use these
	 * locks; return same error as if the lkid had not been found at all
	 */

	if (test_bit(DLM_IFL_ENDOFLIFE_BIT, &lkb->lkb_iflags)) {
		log_debug(ls, "unlock on ENDOFLIFE %x", lkb->lkb_id);
		rv = -ENOENT;
		goto out;
	}

	/* cancel not allowed with another cancel/unlock in progress */

	if (args->flags & DLM_LKF_CANCEL) {
		if (lkb->lkb_exflags & DLM_LKF_CANCEL)
			goto out;

		if (is_overlap(lkb))
			goto out;

		if (test_bit(DLM_IFL_RESEND_BIT, &lkb->lkb_iflags)) {
			set_bit(DLM_IFL_OVERLAP_CANCEL_BIT, &lkb->lkb_iflags);
			rv = -EBUSY;
			goto out;
		}

		/* there's nothing to cancel */
		if (lkb->lkb_status == DLM_LKSTS_GRANTED &&
		    !lkb->lkb_wait_type) {
			rv = -EBUSY;
			goto out;
		}

		switch (lkb->lkb_wait_type) {
		case DLM_MSG_LOOKUP:
		case DLM_MSG_REQUEST:
			set_bit(DLM_IFL_OVERLAP_CANCEL_BIT, &lkb->lkb_iflags);
			rv = -EBUSY;
			goto out;
		case DLM_MSG_UNLOCK:
		case DLM_MSG_CANCEL:
			goto out;
		}
		/* add_to_waiters() will set OVERLAP_CANCEL */
		goto out_ok;
	}

	/* do we need to allow a force-unlock if there's a normal unlock
	   already in progress?  in what conditions could the normal unlock
	   fail such that we'd want to send a force-unlock to be sure? */

	if (args->flags & DLM_LKF_FORCEUNLOCK) {
		if (lkb->lkb_exflags & DLM_LKF_FORCEUNLOCK)
			goto out;

		if (is_overlap_unlock(lkb))
			goto out;

		if (test_bit(DLM_IFL_RESEND_BIT, &lkb->lkb_iflags)) {
			set_bit(DLM_IFL_OVERLAP_UNLOCK_BIT, &lkb->lkb_iflags);
			rv = -EBUSY;
			goto out;
		}

		switch (lkb->lkb_wait_type) {
		case DLM_MSG_LOOKUP:
		case DLM_MSG_REQUEST:
			set_bit(DLM_IFL_OVERLAP_UNLOCK_BIT, &lkb->lkb_iflags);
			rv = -EBUSY;
			goto out;
		case DLM_MSG_UNLOCK:
			goto out;
		}
		/* add_to_waiters() will set OVERLAP_UNLOCK */
	}

 out_ok:
	/* an overlapping op shouldn't blow away exflags from other op */
	lkb->lkb_exflags |= args->flags;
	dlm_set_sbflags_val(lkb, 0);
	lkb->lkb_astparam = args->astparam;
	rv = 0;
 out:
	switch (rv) {
	case 0:
		break;
	case -EINVAL:
		/* annoy the user because dlm usage is wrong */
		WARN_ON(1);
		log_error(ls, "%s %d %x %x %x %x %d %s", __func__, rv,
			  lkb->lkb_id, dlm_iflags_val(lkb), lkb->lkb_exflags,
			  args->flags, lkb->lkb_wait_type,
			  lkb->lkb_resource->res_name);
		break;
	default:
		log_debug(ls, "%s %d %x %x %x %x %d %s", __func__, rv,
			  lkb->lkb_id, dlm_iflags_val(lkb), lkb->lkb_exflags,
			  args->flags, lkb->lkb_wait_type,
			  lkb->lkb_resource->res_name);
		break;
	}

	return rv;
}

/*
 * Four stage 4 varieties:
 * do_request(), do_convert(), do_unlock(), do_cancel()
 * These are called on the master node for the given lock and
 * from the central locking logic.
 */

static int do_request(struct dlm_rsb *r, struct dlm_lkb *lkb)
{
	int error = 0;

	if (can_be_granted(r, lkb, 1, 0, NULL)) {
		grant_lock(r, lkb);
		queue_cast(r, lkb, 0);
		goto out;
	}

	if (can_be_queued(lkb)) {
		error = -EINPROGRESS;
		add_lkb(r, lkb, DLM_LKSTS_WAITING);
		goto out;
	}

	error = -EAGAIN;
	queue_cast(r, lkb, -EAGAIN);
 out:
	return error;
}

static void do_request_effects(struct dlm_rsb *r, struct dlm_lkb *lkb,
			       int error)
{
	switch (error) {
	case -EAGAIN:
		if (force_blocking_asts(lkb))
			send_blocking_asts_all(r, lkb);
		break;
	case -EINPROGRESS:
		send_blocking_asts(r, lkb);
		break;
	}
}

static int do_convert(struct dlm_rsb *r, struct dlm_lkb *lkb)
{
	int error = 0;
	int deadlk = 0;

	/* changing an existing lock may allow others to be granted */

	if (can_be_granted(r, lkb, 1, 0, &deadlk)) {
		grant_lock(r, lkb);
		queue_cast(r, lkb, 0);
		goto out;
	}

	/* can_be_granted() detected that this lock would block in a conversion
	   deadlock, so we leave it on the granted queue and return EDEADLK in
	   the ast for the convert. */

	if (deadlk && !(lkb->lkb_exflags & DLM_LKF_NODLCKWT)) {
		/* it's left on the granted queue */
		revert_lock(r, lkb);
		queue_cast(r, lkb, -EDEADLK);
		error = -EDEADLK;
		goto out;
	}

	/* is_demoted() means the can_be_granted() above set the grmode
	   to NL, and left us on the granted queue.  This auto-demotion
	   (due to CONVDEADLK) might mean other locks, and/or this lock, are
	   now grantable.  We have to try to grant other converting locks
	   before we try again to grant this one. */

	if (is_demoted(lkb)) {
		grant_pending_convert(r, DLM_LOCK_IV, NULL, NULL);
		if (_can_be_granted(r, lkb, 1, 0)) {
			grant_lock(r, lkb);
			queue_cast(r, lkb, 0);
			goto out;
		}
		/* else fall through and move to convert queue */
	}

	if (can_be_queued(lkb)) {
		error = -EINPROGRESS;
		del_lkb(r, lkb);
		add_lkb(r, lkb, DLM_LKSTS_CONVERT);
		goto out;
	}

	error = -EAGAIN;
	queue_cast(r, lkb, -EAGAIN);
 out:
	return error;
}

static void do_convert_effects(struct dlm_rsb *r, struct dlm_lkb *lkb,
			       int error)
{
	switch (error) {
	case 0:
		grant_pending_locks(r, NULL);
		/* grant_pending_locks also sends basts */
		break;
	case -EAGAIN:
		if (force_blocking_asts(lkb))
			send_blocking_asts_all(r, lkb);
		break;
	case -EINPROGRESS:
		send_blocking_asts(r, lkb);
		break;
	}
}

static int do_unlock(struct dlm_rsb *r, struct dlm_lkb *lkb)
{
	remove_lock(r, lkb);
	queue_cast(r, lkb, -DLM_EUNLOCK);
	return -DLM_EUNLOCK;
}

static void do_unlock_effects(struct dlm_rsb *r, struct dlm_lkb *lkb,
			      int error)
{
	grant_pending_locks(r, NULL);
}

/* returns: 0 did nothing, -DLM_ECANCEL canceled lock */

static int do_cancel(struct dlm_rsb *r, struct dlm_lkb *lkb)
{
	int error;

	error = revert_lock(r, lkb);
	if (error) {
		queue_cast(r, lkb, -DLM_ECANCEL);
		return -DLM_ECANCEL;
	}
	return 0;
}

static void do_cancel_effects(struct dlm_rsb *r, struct dlm_lkb *lkb,
			      int error)
{
	if (error)
		grant_pending_locks(r, NULL);
}

/*
 * Four stage 3 varieties:
 * _request_lock(), _convert_lock(), _unlock_lock(), _cancel_lock()
 */

/* add a new lkb to a possibly new rsb, called by requesting process */

static int _request_lock(struct dlm_rsb *r, struct dlm_lkb *lkb)
{
	int error;

	/* set_master: sets lkb nodeid from r */

	error = set_master(r, lkb);
	if (error < 0)
		goto out;
	if (error) {
		error = 0;
		goto out;
	}

	if (is_remote(r)) {
		/* receive_request() calls do_request() on remote node */
		error = send_request(r, lkb);
	} else {
		error = do_request(r, lkb);
		/* for remote locks the request_reply is sent
		   between do_request and do_request_effects */
		do_request_effects(r, lkb, error);
	}
 out:
	return error;
}

/* change some property of an existing lkb, e.g. mode */

static int _convert_lock(struct dlm_rsb *r, struct dlm_lkb *lkb)
{
	int error;

	if (is_remote(r)) {
		/* receive_convert() calls do_convert() on remote node */
		error = send_convert(r, lkb);
	} else {
		error = do_convert(r, lkb);
		/* for remote locks the convert_reply is sent
		   between do_convert and do_convert_effects */
		do_convert_effects(r, lkb, error);
	}

	return error;
}

/* remove an existing lkb from the granted queue */

static int _unlock_lock(struct dlm_rsb *r, struct dlm_lkb *lkb)
{
	int error;

	if (is_remote(r)) {
		/* receive_unlock() calls do_unlock() on remote node */
		error = send_unlock(r, lkb);
	} else {
		error = do_unlock(r, lkb);
		/* for remote locks the unlock_reply is sent
		   between do_unlock and do_unlock_effects */
		do_unlock_effects(r, lkb, error);
	}

	return error;
}

/* remove an existing lkb from the convert or wait queue */

static int _cancel_lock(struct dlm_rsb *r, struct dlm_lkb *lkb)
{
	int error;

	if (is_remote(r)) {
		/* receive_cancel() calls do_cancel() on remote node */
		error = send_cancel(r, lkb);
	} else {
		error = do_cancel(r, lkb);
		/* for remote locks the cancel_reply is sent
		   between do_cancel and do_cancel_effects */
		do_cancel_effects(r, lkb, error);
	}

	return error;
}

/*
 * Four stage 2 varieties:
 * request_lock(), convert_lock(), unlock_lock(), cancel_lock()
 */

static int request_lock(struct dlm_ls *ls, struct dlm_lkb *lkb,
			const void *name, int len,
			struct dlm_args *args)
{
	struct dlm_rsb *r;
	int error;

	error = validate_lock_args(ls, lkb, args);
	if (error)
		return error;

	error = find_rsb(ls, name, len, 0, R_REQUEST, &r);
	if (error)
		return error;

	lock_rsb(r);

	attach_lkb(r, lkb);
	lkb->lkb_lksb->sb_lkid = lkb->lkb_id;

	error = _request_lock(r, lkb);

	unlock_rsb(r);
	put_rsb(r);
	return error;
}

static int convert_lock(struct dlm_ls *ls, struct dlm_lkb *lkb,
			struct dlm_args *args)
{
	struct dlm_rsb *r;
	int error;

	r = lkb->lkb_resource;

	hold_rsb(r);
	lock_rsb(r);

	error = validate_lock_args(ls, lkb, args);
	if (error)
		goto out;

	error = _convert_lock(r, lkb);
 out:
	unlock_rsb(r);
	put_rsb(r);
	return error;
}

static int unlock_lock(struct dlm_ls *ls, struct dlm_lkb *lkb,
		       struct dlm_args *args)
{
	struct dlm_rsb *r;
	int error;

	r = lkb->lkb_resource;

	hold_rsb(r);
	lock_rsb(r);

	error = validate_unlock_args(lkb, args);
	if (error)
		goto out;

	error = _unlock_lock(r, lkb);
 out:
	unlock_rsb(r);
	put_rsb(r);
	return error;
}

static int cancel_lock(struct dlm_ls *ls, struct dlm_lkb *lkb,
		       struct dlm_args *args)
{
	struct dlm_rsb *r;
	int error;

	r = lkb->lkb_resource;

	hold_rsb(r);
	lock_rsb(r);

	error = validate_unlock_args(lkb, args);
	if (error)
		goto out;

	error = _cancel_lock(r, lkb);
 out:
	unlock_rsb(r);
	put_rsb(r);
	return error;
}

/*
 * Two stage 1 varieties:  dlm_lock() and dlm_unlock()
 */

int dlm_lock(dlm_lockspace_t *lockspace,
	     int mode,
	     struct dlm_lksb *lksb,
	     uint32_t flags,
	     const void *name,
	     unsigned int namelen,
	     uint32_t parent_lkid,
	     void (*ast) (void *astarg),
	     void *astarg,
	     void (*bast) (void *astarg, int mode))
{
	struct dlm_ls *ls;
	struct dlm_lkb *lkb;
	struct dlm_args args;
	int error, convert = flags & DLM_LKF_CONVERT;

	ls = dlm_find_lockspace_local(lockspace);
	if (!ls)
		return -EINVAL;

	dlm_lock_recovery(ls);

	if (convert)
		error = find_lkb(ls, lksb->sb_lkid, &lkb);
	else
		error = create_lkb(ls, &lkb);

	if (error)
		goto out;

	trace_dlm_lock_start(ls, lkb, name, namelen, mode, flags);

	error = set_lock_args(mode, lksb, flags, namelen, ast, astarg, bast,
			      &args);
	if (error)
		goto out_put;

	if (convert)
		error = convert_lock(ls, lkb, &args);
	else
		error = request_lock(ls, lkb, name, namelen, &args);

	if (error == -EINPROGRESS)
		error = 0;
 out_put:
	trace_dlm_lock_end(ls, lkb, name, namelen, mode, flags, error, true);

	if (convert || error)
		__put_lkb(ls, lkb);
	if (error == -EAGAIN || error == -EDEADLK)
		error = 0;
 out:
	dlm_unlock_recovery(ls);
	dlm_put_lockspace(ls);
	return error;
}

int dlm_unlock(dlm_lockspace_t *lockspace,
	       uint32_t lkid,
	       uint32_t flags,
	       struct dlm_lksb *lksb,
	       void *astarg)
{
	struct dlm_ls *ls;
	struct dlm_lkb *lkb;
	struct dlm_args args;
	int error;

	ls = dlm_find_lockspace_local(lockspace);
	if (!ls)
		return -EINVAL;

	dlm_lock_recovery(ls);

	error = find_lkb(ls, lkid, &lkb);
	if (error)
		goto out;

	trace_dlm_unlock_start(ls, lkb, flags);

	error = set_unlock_args(flags, astarg, &args);
	if (error)
		goto out_put;

	if (flags & DLM_LKF_CANCEL)
		error = cancel_lock(ls, lkb, &args);
	else
		error = unlock_lock(ls, lkb, &args);

	if (error == -DLM_EUNLOCK || error == -DLM_ECANCEL)
		error = 0;
	if (error == -EBUSY && (flags & (DLM_LKF_CANCEL | DLM_LKF_FORCEUNLOCK)))
		error = 0;
 out_put:
	trace_dlm_unlock_end(ls, lkb, flags, error);

	dlm_put_lkb(lkb);
 out:
	dlm_unlock_recovery(ls);
	dlm_put_lockspace(ls);
	return error;
}

/*
 * send/receive routines for remote operations and replies
 *
 * send_args
 * send_common
 * send_request			receive_request
 * send_convert			receive_convert
 * send_unlock			receive_unlock
 * send_cancel			receive_cancel
 * send_grant			receive_grant
 * send_bast			receive_bast
 * send_lookup			receive_lookup
 * send_remove			receive_remove
 *
 * 				send_common_reply
 * receive_request_reply	send_request_reply
 * receive_convert_reply	send_convert_reply
 * receive_unlock_reply		send_unlock_reply
 * receive_cancel_reply		send_cancel_reply
 * receive_lookup_reply		send_lookup_reply
 */

static int _create_message(struct dlm_ls *ls, int mb_len,
			   int to_nodeid, int mstype,
			   struct dlm_message **ms_ret,
			   struct dlm_mhandle **mh_ret)
{
	struct dlm_message *ms;
	struct dlm_mhandle *mh;
	char *mb;

	/* get_buffer gives us a message handle (mh) that we need to
	   pass into midcomms_commit and a message buffer (mb) that we
	   write our data into */

	mh = dlm_midcomms_get_mhandle(to_nodeid, mb_len, &mb);
	if (!mh)
		return -ENOBUFS;

	ms = (struct dlm_message *) mb;

	ms->m_header.h_version = cpu_to_le32(DLM_HEADER_MAJOR | DLM_HEADER_MINOR);
	ms->m_header.u.h_lockspace = cpu_to_le32(ls->ls_global_id);
	ms->m_header.h_nodeid = cpu_to_le32(dlm_our_nodeid());
	ms->m_header.h_length = cpu_to_le16(mb_len);
	ms->m_header.h_cmd = DLM_MSG;

	ms->m_type = cpu_to_le32(mstype);

	*mh_ret = mh;
	*ms_ret = ms;
	return 0;
}

static int create_message(struct dlm_rsb *r, struct dlm_lkb *lkb,
			  int to_nodeid, int mstype,
			  struct dlm_message **ms_ret,
			  struct dlm_mhandle **mh_ret)
{
	int mb_len = sizeof(struct dlm_message);

	switch (mstype) {
	case DLM_MSG_REQUEST:
	case DLM_MSG_LOOKUP:
	case DLM_MSG_REMOVE:
		mb_len += r->res_length;
		break;
	case DLM_MSG_CONVERT:
	case DLM_MSG_UNLOCK:
	case DLM_MSG_REQUEST_REPLY:
	case DLM_MSG_CONVERT_REPLY:
	case DLM_MSG_GRANT:
		if (lkb && lkb->lkb_lvbptr && (lkb->lkb_exflags & DLM_LKF_VALBLK))
			mb_len += r->res_ls->ls_lvblen;
		break;
	}

	return _create_message(r->res_ls, mb_len, to_nodeid, mstype,
			       ms_ret, mh_ret);
}

/* further lowcomms enhancements or alternate implementations may make
   the return value from this function useful at some point */

static int send_message(struct dlm_mhandle *mh, struct dlm_message *ms,
			const void *name, int namelen)
{
	dlm_midcomms_commit_mhandle(mh, name, namelen);
	return 0;
}

static void send_args(struct dlm_rsb *r, struct dlm_lkb *lkb,
		      struct dlm_message *ms)
{
	ms->m_nodeid   = cpu_to_le32(lkb->lkb_nodeid);
	ms->m_pid      = cpu_to_le32(lkb->lkb_ownpid);
	ms->m_lkid     = cpu_to_le32(lkb->lkb_id);
	ms->m_remid    = cpu_to_le32(lkb->lkb_remid);
	ms->m_exflags  = cpu_to_le32(lkb->lkb_exflags);
	ms->m_sbflags  = cpu_to_le32(dlm_sbflags_val(lkb));
	ms->m_flags    = cpu_to_le32(dlm_dflags_val(lkb));
	ms->m_lvbseq   = cpu_to_le32(lkb->lkb_lvbseq);
	ms->m_status   = cpu_to_le32(lkb->lkb_status);
	ms->m_grmode   = cpu_to_le32(lkb->lkb_grmode);
	ms->m_rqmode   = cpu_to_le32(lkb->lkb_rqmode);
	ms->m_hash     = cpu_to_le32(r->res_hash);

	/* m_result and m_bastmode are set from function args,
	   not from lkb fields */

	if (lkb->lkb_bastfn)
		ms->m_asts |= cpu_to_le32(DLM_CB_BAST);
	if (lkb->lkb_astfn)
		ms->m_asts |= cpu_to_le32(DLM_CB_CAST);

	/* compare with switch in create_message; send_remove() doesn't
	   use send_args() */

	switch (ms->m_type) {
	case cpu_to_le32(DLM_MSG_REQUEST):
	case cpu_to_le32(DLM_MSG_LOOKUP):
		memcpy(ms->m_extra, r->res_name, r->res_length);
		break;
	case cpu_to_le32(DLM_MSG_CONVERT):
	case cpu_to_le32(DLM_MSG_UNLOCK):
	case cpu_to_le32(DLM_MSG_REQUEST_REPLY):
	case cpu_to_le32(DLM_MSG_CONVERT_REPLY):
	case cpu_to_le32(DLM_MSG_GRANT):
		if (!lkb->lkb_lvbptr || !(lkb->lkb_exflags & DLM_LKF_VALBLK))
			break;
		memcpy(ms->m_extra, lkb->lkb_lvbptr, r->res_ls->ls_lvblen);
		break;
	}
}

static int send_common(struct dlm_rsb *r, struct dlm_lkb *lkb, int mstype)
{
	struct dlm_message *ms;
	struct dlm_mhandle *mh;
	int to_nodeid, error;

	to_nodeid = r->res_nodeid;

	error = add_to_waiters(lkb, mstype, to_nodeid);
	if (error)
		return error;

	error = create_message(r, lkb, to_nodeid, mstype, &ms, &mh);
	if (error)
		goto fail;

	send_args(r, lkb, ms);

	error = send_message(mh, ms, r->res_name, r->res_length);
	if (error)
		goto fail;
	return 0;

 fail:
	remove_from_waiters(lkb, msg_reply_type(mstype));
	return error;
}

static int send_request(struct dlm_rsb *r, struct dlm_lkb *lkb)
{
	return send_common(r, lkb, DLM_MSG_REQUEST);
}

static int send_convert(struct dlm_rsb *r, struct dlm_lkb *lkb)
{
	int error;

	error = send_common(r, lkb, DLM_MSG_CONVERT);

	/* down conversions go without a reply from the master */
	if (!error && down_conversion(lkb)) {
		remove_from_waiters(lkb, DLM_MSG_CONVERT_REPLY);
		r->res_ls->ls_local_ms.m_type = cpu_to_le32(DLM_MSG_CONVERT_REPLY);
		r->res_ls->ls_local_ms.m_result = 0;
		__receive_convert_reply(r, lkb, &r->res_ls->ls_local_ms, true);
	}

	return error;
}

/* FIXME: if this lkb is the only lock we hold on the rsb, then set
   MASTER_UNCERTAIN to force the next request on the rsb to confirm
   that the master is still correct. */

static int send_unlock(struct dlm_rsb *r, struct dlm_lkb *lkb)
{
	return send_common(r, lkb, DLM_MSG_UNLOCK);
}

static int send_cancel(struct dlm_rsb *r, struct dlm_lkb *lkb)
{
	return send_common(r, lkb, DLM_MSG_CANCEL);
}

static int send_grant(struct dlm_rsb *r, struct dlm_lkb *lkb)
{
	struct dlm_message *ms;
	struct dlm_mhandle *mh;
	int to_nodeid, error;

	to_nodeid = lkb->lkb_nodeid;

	error = create_message(r, lkb, to_nodeid, DLM_MSG_GRANT, &ms, &mh);
	if (error)
		goto out;

	send_args(r, lkb, ms);

	ms->m_result = 0;

	error = send_message(mh, ms, r->res_name, r->res_length);
 out:
	return error;
}

static int send_bast(struct dlm_rsb *r, struct dlm_lkb *lkb, int mode)
{
	struct dlm_message *ms;
	struct dlm_mhandle *mh;
	int to_nodeid, error;

	to_nodeid = lkb->lkb_nodeid;

	error = create_message(r, NULL, to_nodeid, DLM_MSG_BAST, &ms, &mh);
	if (error)
		goto out;

	send_args(r, lkb, ms);

	ms->m_bastmode = cpu_to_le32(mode);

	error = send_message(mh, ms, r->res_name, r->res_length);
 out:
	return error;
}

static int send_lookup(struct dlm_rsb *r, struct dlm_lkb *lkb)
{
	struct dlm_message *ms;
	struct dlm_mhandle *mh;
	int to_nodeid, error;

	to_nodeid = dlm_dir_nodeid(r);

	error = add_to_waiters(lkb, DLM_MSG_LOOKUP, to_nodeid);
	if (error)
		return error;

	error = create_message(r, NULL, to_nodeid, DLM_MSG_LOOKUP, &ms, &mh);
	if (error)
		goto fail;

	send_args(r, lkb, ms);

	error = send_message(mh, ms, r->res_name, r->res_length);
	if (error)
		goto fail;
	return 0;

 fail:
	remove_from_waiters(lkb, DLM_MSG_LOOKUP_REPLY);
	return error;
}

static int send_remove(struct dlm_rsb *r)
{
	struct dlm_message *ms;
	struct dlm_mhandle *mh;
	int to_nodeid, error;

	to_nodeid = dlm_dir_nodeid(r);

	error = create_message(r, NULL, to_nodeid, DLM_MSG_REMOVE, &ms, &mh);
	if (error)
		goto out;

	memcpy(ms->m_extra, r->res_name, r->res_length);
	ms->m_hash = cpu_to_le32(r->res_hash);

	error = send_message(mh, ms, r->res_name, r->res_length);
 out:
	return error;
}

static int send_common_reply(struct dlm_rsb *r, struct dlm_lkb *lkb,
			     int mstype, int rv)
{
	struct dlm_message *ms;
	struct dlm_mhandle *mh;
	int to_nodeid, error;

	to_nodeid = lkb->lkb_nodeid;

	error = create_message(r, lkb, to_nodeid, mstype, &ms, &mh);
	if (error)
		goto out;

	send_args(r, lkb, ms);

	ms->m_result = cpu_to_le32(to_dlm_errno(rv));

	error = send_message(mh, ms, r->res_name, r->res_length);
 out:
	return error;
}

static int send_request_reply(struct dlm_rsb *r, struct dlm_lkb *lkb, int rv)
{
	return send_common_reply(r, lkb, DLM_MSG_REQUEST_REPLY, rv);
}

static int send_convert_reply(struct dlm_rsb *r, struct dlm_lkb *lkb, int rv)
{
	return send_common_reply(r, lkb, DLM_MSG_CONVERT_REPLY, rv);
}

static int send_unlock_reply(struct dlm_rsb *r, struct dlm_lkb *lkb, int rv)
{
	return send_common_reply(r, lkb, DLM_MSG_UNLOCK_REPLY, rv);
}

static int send_cancel_reply(struct dlm_rsb *r, struct dlm_lkb *lkb, int rv)
{
	return send_common_reply(r, lkb, DLM_MSG_CANCEL_REPLY, rv);
}

static int send_lookup_reply(struct dlm_ls *ls,
			     const struct dlm_message *ms_in, int ret_nodeid,
			     int rv)
{
	struct dlm_rsb *r = &ls->ls_local_rsb;
	struct dlm_message *ms;
	struct dlm_mhandle *mh;
	int error, nodeid = le32_to_cpu(ms_in->m_header.h_nodeid);

	error = create_message(r, NULL, nodeid, DLM_MSG_LOOKUP_REPLY, &ms, &mh);
	if (error)
		goto out;

	ms->m_lkid = ms_in->m_lkid;
	ms->m_result = cpu_to_le32(to_dlm_errno(rv));
	ms->m_nodeid = cpu_to_le32(ret_nodeid);

	error = send_message(mh, ms, ms_in->m_extra, receive_extralen(ms_in));
 out:
	return error;
}

/* which args we save from a received message depends heavily on the type
   of message, unlike the send side where we can safely send everything about
   the lkb for any type of message */

static void receive_flags(struct dlm_lkb *lkb, const struct dlm_message *ms)
{
	lkb->lkb_exflags = le32_to_cpu(ms->m_exflags);
	dlm_set_sbflags_val(lkb, le32_to_cpu(ms->m_sbflags));
	dlm_set_dflags_val(lkb, le32_to_cpu(ms->m_flags));
}

static void receive_flags_reply(struct dlm_lkb *lkb,
				const struct dlm_message *ms,
				bool local)
{
	if (local)
		return;

	dlm_set_sbflags_val(lkb, le32_to_cpu(ms->m_sbflags));
	dlm_set_dflags_val(lkb, le32_to_cpu(ms->m_flags));
}

static int receive_extralen(const struct dlm_message *ms)
{
	return (le16_to_cpu(ms->m_header.h_length) -
		sizeof(struct dlm_message));
}

static int receive_lvb(struct dlm_ls *ls, struct dlm_lkb *lkb,
		       const struct dlm_message *ms)
{
	int len;

	if (lkb->lkb_exflags & DLM_LKF_VALBLK) {
		if (!lkb->lkb_lvbptr)
			lkb->lkb_lvbptr = dlm_allocate_lvb(ls);
		if (!lkb->lkb_lvbptr)
			return -ENOMEM;
		len = receive_extralen(ms);
		if (len > ls->ls_lvblen)
			len = ls->ls_lvblen;
		memcpy(lkb->lkb_lvbptr, ms->m_extra, len);
	}
	return 0;
}

static void fake_bastfn(void *astparam, int mode)
{
	log_print("fake_bastfn should not be called");
}

static void fake_astfn(void *astparam)
{
	log_print("fake_astfn should not be called");
}

static int receive_request_args(struct dlm_ls *ls, struct dlm_lkb *lkb,
				const struct dlm_message *ms)
{
	lkb->lkb_nodeid = le32_to_cpu(ms->m_header.h_nodeid);
	lkb->lkb_ownpid = le32_to_cpu(ms->m_pid);
	lkb->lkb_remid = le32_to_cpu(ms->m_lkid);
	lkb->lkb_grmode = DLM_LOCK_IV;
	lkb->lkb_rqmode = le32_to_cpu(ms->m_rqmode);

	lkb->lkb_bastfn = (ms->m_asts & cpu_to_le32(DLM_CB_BAST)) ? &fake_bastfn : NULL;
	lkb->lkb_astfn = (ms->m_asts & cpu_to_le32(DLM_CB_CAST)) ? &fake_astfn : NULL;

	if (lkb->lkb_exflags & DLM_LKF_VALBLK) {
		/* lkb was just created so there won't be an lvb yet */
		lkb->lkb_lvbptr = dlm_allocate_lvb(ls);
		if (!lkb->lkb_lvbptr)
			return -ENOMEM;
	}

	return 0;
}

static int receive_convert_args(struct dlm_ls *ls, struct dlm_lkb *lkb,
				const struct dlm_message *ms)
{
	if (lkb->lkb_status != DLM_LKSTS_GRANTED)
		return -EBUSY;

	if (receive_lvb(ls, lkb, ms))
		return -ENOMEM;

	lkb->lkb_rqmode = le32_to_cpu(ms->m_rqmode);
	lkb->lkb_lvbseq = le32_to_cpu(ms->m_lvbseq);

	return 0;
}

static int receive_unlock_args(struct dlm_ls *ls, struct dlm_lkb *lkb,
			       const struct dlm_message *ms)
{
	if (receive_lvb(ls, lkb, ms))
		return -ENOMEM;
	return 0;
}

/* We fill in the local-lkb fields with the info that send_xxxx_reply()
   uses to send a reply and that the remote end uses to process the reply. */

static void setup_local_lkb(struct dlm_ls *ls, const struct dlm_message *ms)
{
	struct dlm_lkb *lkb = &ls->ls_local_lkb;
	lkb->lkb_nodeid = le32_to_cpu(ms->m_header.h_nodeid);
	lkb->lkb_remid = le32_to_cpu(ms->m_lkid);
}

/* This is called after the rsb is locked so that we can safely inspect
   fields in the lkb. */

static int validate_message(struct dlm_lkb *lkb, const struct dlm_message *ms)
{
	int from = le32_to_cpu(ms->m_header.h_nodeid);
	int error = 0;

	/* currently mixing of user/kernel locks are not supported */
	if (ms->m_flags & cpu_to_le32(BIT(DLM_DFL_USER_BIT)) &&
	    !test_bit(DLM_DFL_USER_BIT, &lkb->lkb_dflags)) {
		log_error(lkb->lkb_resource->res_ls,
			  "got user dlm message for a kernel lock");
		error = -EINVAL;
		goto out;
	}

	switch (ms->m_type) {
	case cpu_to_le32(DLM_MSG_CONVERT):
	case cpu_to_le32(DLM_MSG_UNLOCK):
	case cpu_to_le32(DLM_MSG_CANCEL):
		if (!is_master_copy(lkb) || lkb->lkb_nodeid != from)
			error = -EINVAL;
		break;

	case cpu_to_le32(DLM_MSG_CONVERT_REPLY):
	case cpu_to_le32(DLM_MSG_UNLOCK_REPLY):
	case cpu_to_le32(DLM_MSG_CANCEL_REPLY):
	case cpu_to_le32(DLM_MSG_GRANT):
	case cpu_to_le32(DLM_MSG_BAST):
		if (!is_process_copy(lkb) || lkb->lkb_nodeid != from)
			error = -EINVAL;
		break;

	case cpu_to_le32(DLM_MSG_REQUEST_REPLY):
		if (!is_process_copy(lkb))
			error = -EINVAL;
		else if (lkb->lkb_nodeid != -1 && lkb->lkb_nodeid != from)
			error = -EINVAL;
		break;

	default:
		error = -EINVAL;
	}

out:
	if (error)
		log_error(lkb->lkb_resource->res_ls,
			  "ignore invalid message %d from %d %x %x %x %d",
			  le32_to_cpu(ms->m_type), from, lkb->lkb_id,
			  lkb->lkb_remid, dlm_iflags_val(lkb),
			  lkb->lkb_nodeid);
	return error;
}

static int receive_request(struct dlm_ls *ls, const struct dlm_message *ms)
{
	struct dlm_lkb *lkb;
	struct dlm_rsb *r;
	int from_nodeid;
	int error, namelen = 0;

	from_nodeid = le32_to_cpu(ms->m_header.h_nodeid);

	error = create_lkb(ls, &lkb);
	if (error)
		goto fail;

	receive_flags(lkb, ms);
	set_bit(DLM_IFL_MSTCPY_BIT, &lkb->lkb_iflags);
	error = receive_request_args(ls, lkb, ms);
	if (error) {
		__put_lkb(ls, lkb);
		goto fail;
	}

	/* The dir node is the authority on whether we are the master
	   for this rsb or not, so if the master sends us a request, we should
	   recreate the rsb if we've destroyed it.   This race happens when we
	   send a remove message to the dir node at the same time that the dir
	   node sends us a request for the rsb. */

	namelen = receive_extralen(ms);

	error = find_rsb(ls, ms->m_extra, namelen, from_nodeid,
			 R_RECEIVE_REQUEST, &r);
	if (error) {
		__put_lkb(ls, lkb);
		goto fail;
	}

	lock_rsb(r);

	if (r->res_master_nodeid != dlm_our_nodeid()) {
		error = validate_master_nodeid(ls, r, from_nodeid);
		if (error) {
			unlock_rsb(r);
			put_rsb(r);
			__put_lkb(ls, lkb);
			goto fail;
		}
	}

	attach_lkb(r, lkb);
	error = do_request(r, lkb);
	send_request_reply(r, lkb, error);
	do_request_effects(r, lkb, error);

	unlock_rsb(r);
	put_rsb(r);

	if (error == -EINPROGRESS)
		error = 0;
	if (error)
		dlm_put_lkb(lkb);
	return 0;

 fail:
	/* TODO: instead of returning ENOTBLK, add the lkb to res_lookup
	   and do this receive_request again from process_lookup_list once
	   we get the lookup reply.  This would avoid a many repeated
	   ENOTBLK request failures when the lookup reply designating us
	   as master is delayed. */

	if (error != -ENOTBLK) {
		log_limit(ls, "receive_request %x from %d %d",
			  le32_to_cpu(ms->m_lkid), from_nodeid, error);
	}

	setup_local_lkb(ls, ms);
	send_request_reply(&ls->ls_local_rsb, &ls->ls_local_lkb, error);
	return error;
}

static int receive_convert(struct dlm_ls *ls, const struct dlm_message *ms)
{
	struct dlm_lkb *lkb;
	struct dlm_rsb *r;
	int error, reply = 1;

	error = find_lkb(ls, le32_to_cpu(ms->m_remid), &lkb);
	if (error)
		goto fail;

	if (lkb->lkb_remid != le32_to_cpu(ms->m_lkid)) {
		log_error(ls, "receive_convert %x remid %x recover_seq %llu "
			  "remote %d %x", lkb->lkb_id, lkb->lkb_remid,
			  (unsigned long long)lkb->lkb_recover_seq,
			  le32_to_cpu(ms->m_header.h_nodeid),
			  le32_to_cpu(ms->m_lkid));
		error = -ENOENT;
		dlm_put_lkb(lkb);
		goto fail;
	}

	r = lkb->lkb_resource;

	hold_rsb(r);
	lock_rsb(r);

	error = validate_message(lkb, ms);
	if (error)
		goto out;

	receive_flags(lkb, ms);

	error = receive_convert_args(ls, lkb, ms);
	if (error) {
		send_convert_reply(r, lkb, error);
		goto out;
	}

	reply = !down_conversion(lkb);

	error = do_convert(r, lkb);
	if (reply)
		send_convert_reply(r, lkb, error);
	do_convert_effects(r, lkb, error);
 out:
	unlock_rsb(r);
	put_rsb(r);
	dlm_put_lkb(lkb);
	return 0;

 fail:
	setup_local_lkb(ls, ms);
	send_convert_reply(&ls->ls_local_rsb, &ls->ls_local_lkb, error);
	return error;
}

static int receive_unlock(struct dlm_ls *ls, const struct dlm_message *ms)
{
	struct dlm_lkb *lkb;
	struct dlm_rsb *r;
	int error;

	error = find_lkb(ls, le32_to_cpu(ms->m_remid), &lkb);
	if (error)
		goto fail;

	if (lkb->lkb_remid != le32_to_cpu(ms->m_lkid)) {
		log_error(ls, "receive_unlock %x remid %x remote %d %x",
			  lkb->lkb_id, lkb->lkb_remid,
			  le32_to_cpu(ms->m_header.h_nodeid),
			  le32_to_cpu(ms->m_lkid));
		error = -ENOENT;
		dlm_put_lkb(lkb);
		goto fail;
	}

	r = lkb->lkb_resource;

	hold_rsb(r);
	lock_rsb(r);

	error = validate_message(lkb, ms);
	if (error)
		goto out;

	receive_flags(lkb, ms);

	error = receive_unlock_args(ls, lkb, ms);
	if (error) {
		send_unlock_reply(r, lkb, error);
		goto out;
	}

	error = do_unlock(r, lkb);
	send_unlock_reply(r, lkb, error);
	do_unlock_effects(r, lkb, error);
 out:
	unlock_rsb(r);
	put_rsb(r);
	dlm_put_lkb(lkb);
	return 0;

 fail:
	setup_local_lkb(ls, ms);
	send_unlock_reply(&ls->ls_local_rsb, &ls->ls_local_lkb, error);
	return error;
}

static int receive_cancel(struct dlm_ls *ls, const struct dlm_message *ms)
{
	struct dlm_lkb *lkb;
	struct dlm_rsb *r;
	int error;

	error = find_lkb(ls, le32_to_cpu(ms->m_remid), &lkb);
	if (error)
		goto fail;

	receive_flags(lkb, ms);

	r = lkb->lkb_resource;

	hold_rsb(r);
	lock_rsb(r);

	error = validate_message(lkb, ms);
	if (error)
		goto out;

	error = do_cancel(r, lkb);
	send_cancel_reply(r, lkb, error);
	do_cancel_effects(r, lkb, error);
 out:
	unlock_rsb(r);
	put_rsb(r);
	dlm_put_lkb(lkb);
	return 0;

 fail:
	setup_local_lkb(ls, ms);
	send_cancel_reply(&ls->ls_local_rsb, &ls->ls_local_lkb, error);
	return error;
}

static int receive_grant(struct dlm_ls *ls, const struct dlm_message *ms)
{
	struct dlm_lkb *lkb;
	struct dlm_rsb *r;
	int error;

	error = find_lkb(ls, le32_to_cpu(ms->m_remid), &lkb);
	if (error)
		return error;

	r = lkb->lkb_resource;

	hold_rsb(r);
	lock_rsb(r);

	error = validate_message(lkb, ms);
	if (error)
		goto out;

	receive_flags_reply(lkb, ms, false);
	if (is_altmode(lkb))
		munge_altmode(lkb, ms);
	grant_lock_pc(r, lkb, ms);
	queue_cast(r, lkb, 0);
 out:
	unlock_rsb(r);
	put_rsb(r);
	dlm_put_lkb(lkb);
	return 0;
}

static int receive_bast(struct dlm_ls *ls, const struct dlm_message *ms)
{
	struct dlm_lkb *lkb;
	struct dlm_rsb *r;
	int error;

	error = find_lkb(ls, le32_to_cpu(ms->m_remid), &lkb);
	if (error)
		return error;

	r = lkb->lkb_resource;

	hold_rsb(r);
	lock_rsb(r);

	error = validate_message(lkb, ms);
	if (error)
		goto out;

	queue_bast(r, lkb, le32_to_cpu(ms->m_bastmode));
	lkb->lkb_highbast = le32_to_cpu(ms->m_bastmode);
 out:
	unlock_rsb(r);
	put_rsb(r);
	dlm_put_lkb(lkb);
	return 0;
}

static void receive_lookup(struct dlm_ls *ls, const struct dlm_message *ms)
{
	int len, error, ret_nodeid, from_nodeid, our_nodeid;

	from_nodeid = le32_to_cpu(ms->m_header.h_nodeid);
	our_nodeid = dlm_our_nodeid();

	len = receive_extralen(ms);

	error = dlm_master_lookup(ls, from_nodeid, ms->m_extra, len, 0,
				  &ret_nodeid, NULL);

	/* Optimization: we're master so treat lookup as a request */
	if (!error && ret_nodeid == our_nodeid) {
		receive_request(ls, ms);
		return;
	}
	send_lookup_reply(ls, ms, ret_nodeid, error);
}

static void receive_remove(struct dlm_ls *ls, const struct dlm_message *ms)
{
	char name[DLM_RESNAME_MAXLEN+1];
	struct dlm_rsb *r;
	int rv, len, dir_nodeid, from_nodeid;

	from_nodeid = le32_to_cpu(ms->m_header.h_nodeid);

	len = receive_extralen(ms);

	if (len > DLM_RESNAME_MAXLEN) {
		log_error(ls, "receive_remove from %d bad len %d",
			  from_nodeid, len);
		return;
	}

	dir_nodeid = dlm_hash2nodeid(ls, le32_to_cpu(ms->m_hash));
	if (dir_nodeid != dlm_our_nodeid()) {
		log_error(ls, "receive_remove from %d bad nodeid %d",
			  from_nodeid, dir_nodeid);
		return;
	}

	/*
	 * Look for inactive rsb, if it's there, free it.
	 * If the rsb is active, it's being used, and we should ignore this
	 * message.  This is an expected race between the dir node sending a
	 * request to the master node at the same time as the master node sends
	 * a remove to the dir node.  The resolution to that race is for the
	 * dir node to ignore the remove message, and the master node to
	 * recreate the master rsb when it gets a request from the dir node for
	 * an rsb it doesn't have.
	 */

	memset(name, 0, sizeof(name));
	memcpy(name, ms->m_extra, len);

	rcu_read_lock();
	rv = dlm_search_rsb_tree(&ls->ls_rsbtbl, name, len, &r);
	if (rv) {
		rcu_read_unlock();
		/* should not happen */
		log_error(ls, "%s from %d not found %s", __func__,
			  from_nodeid, name);
		return;
	}

	write_lock_bh(&ls->ls_rsbtbl_lock);
	if (!rsb_flag(r, RSB_HASHED)) {
		rcu_read_unlock();
		write_unlock_bh(&ls->ls_rsbtbl_lock);
		/* should not happen */
		log_error(ls, "%s from %d got removed during removal %s",
			  __func__, from_nodeid, name);
		return;
	}
	/* at this stage the rsb can only being freed here */
	rcu_read_unlock();

	if (!rsb_flag(r, RSB_INACTIVE)) {
		if (r->res_master_nodeid != from_nodeid) {
			/* should not happen */
			log_error(ls, "receive_remove on active rsb from %d master %d",
				  from_nodeid, r->res_master_nodeid);
			dlm_print_rsb(r);
			write_unlock_bh(&ls->ls_rsbtbl_lock);
			return;
		}

		/* Ignore the remove message, see race comment above. */

		log_debug(ls, "receive_remove from %d master %d first %x %s",
			  from_nodeid, r->res_master_nodeid, r->res_first_lkid,
			  name);
		write_unlock_bh(&ls->ls_rsbtbl_lock);
		return;
	}

	if (r->res_master_nodeid != from_nodeid) {
		log_error(ls, "receive_remove inactive from %d master %d",
			  from_nodeid, r->res_master_nodeid);
		dlm_print_rsb(r);
		write_unlock_bh(&ls->ls_rsbtbl_lock);
		return;
	}

	list_del(&r->res_slow_list);
	rhashtable_remove_fast(&ls->ls_rsbtbl, &r->res_node,
			       dlm_rhash_rsb_params);
	rsb_clear_flag(r, RSB_HASHED);
	write_unlock_bh(&ls->ls_rsbtbl_lock);

	free_inactive_rsb(r);
}

static void receive_purge(struct dlm_ls *ls, const struct dlm_message *ms)
{
	do_purge(ls, le32_to_cpu(ms->m_nodeid), le32_to_cpu(ms->m_pid));
}

static int receive_request_reply(struct dlm_ls *ls,
				 const struct dlm_message *ms)
{
	struct dlm_lkb *lkb;
	struct dlm_rsb *r;
	int error, mstype, result;
	int from_nodeid = le32_to_cpu(ms->m_header.h_nodeid);

	error = find_lkb(ls, le32_to_cpu(ms->m_remid), &lkb);
	if (error)
		return error;

	r = lkb->lkb_resource;
	hold_rsb(r);
	lock_rsb(r);

	error = validate_message(lkb, ms);
	if (error)
		goto out;

	mstype = lkb->lkb_wait_type;
	error = remove_from_waiters(lkb, DLM_MSG_REQUEST_REPLY);
	if (error) {
		log_error(ls, "receive_request_reply %x remote %d %x result %d",
			  lkb->lkb_id, from_nodeid, le32_to_cpu(ms->m_lkid),
			  from_dlm_errno(le32_to_cpu(ms->m_result)));
		dlm_dump_rsb(r);
		goto out;
	}

	/* Optimization: the dir node was also the master, so it took our
	   lookup as a request and sent request reply instead of lookup reply */
	if (mstype == DLM_MSG_LOOKUP) {
		r->res_master_nodeid = from_nodeid;
		r->res_nodeid = from_nodeid;
		lkb->lkb_nodeid = from_nodeid;
	}

	/* this is the value returned from do_request() on the master */
	result = from_dlm_errno(le32_to_cpu(ms->m_result));

	switch (result) {
	case -EAGAIN:
		/* request would block (be queued) on remote master */
		queue_cast(r, lkb, -EAGAIN);
		confirm_master(r, -EAGAIN);
		unhold_lkb(lkb); /* undoes create_lkb() */
		break;

	case -EINPROGRESS:
	case 0:
		/* request was queued or granted on remote master */
		receive_flags_reply(lkb, ms, false);
		lkb->lkb_remid = le32_to_cpu(ms->m_lkid);
		if (is_altmode(lkb))
			munge_altmode(lkb, ms);
		if (result) {
			add_lkb(r, lkb, DLM_LKSTS_WAITING);
		} else {
			grant_lock_pc(r, lkb, ms);
			queue_cast(r, lkb, 0);
		}
		confirm_master(r, result);
		break;

	case -EBADR:
	case -ENOTBLK:
		/* find_rsb failed to find rsb or rsb wasn't master */
		log_limit(ls, "receive_request_reply %x from %d %d "
			  "master %d dir %d first %x %s", lkb->lkb_id,
			  from_nodeid, result, r->res_master_nodeid,
			  r->res_dir_nodeid, r->res_first_lkid, r->res_name);

		if (r->res_dir_nodeid != dlm_our_nodeid() &&
		    r->res_master_nodeid != dlm_our_nodeid()) {
			/* cause _request_lock->set_master->send_lookup */
			r->res_master_nodeid = 0;
			r->res_nodeid = -1;
			lkb->lkb_nodeid = -1;
		}

		if (is_overlap(lkb)) {
			/* we'll ignore error in cancel/unlock reply */
			queue_cast_overlap(r, lkb);
			confirm_master(r, result);
			unhold_lkb(lkb); /* undoes create_lkb() */
		} else {
			_request_lock(r, lkb);

			if (r->res_master_nodeid == dlm_our_nodeid())
				confirm_master(r, 0);
		}
		break;

	default:
		log_error(ls, "receive_request_reply %x error %d",
			  lkb->lkb_id, result);
	}

	if ((result == 0 || result == -EINPROGRESS) &&
	    test_and_clear_bit(DLM_IFL_OVERLAP_UNLOCK_BIT, &lkb->lkb_iflags)) {
		log_debug(ls, "receive_request_reply %x result %d unlock",
			  lkb->lkb_id, result);
		clear_bit(DLM_IFL_OVERLAP_CANCEL_BIT, &lkb->lkb_iflags);
		send_unlock(r, lkb);
	} else if ((result == -EINPROGRESS) &&
		   test_and_clear_bit(DLM_IFL_OVERLAP_CANCEL_BIT,
				      &lkb->lkb_iflags)) {
		log_debug(ls, "receive_request_reply %x cancel", lkb->lkb_id);
		clear_bit(DLM_IFL_OVERLAP_UNLOCK_BIT, &lkb->lkb_iflags);
		send_cancel(r, lkb);
	} else {
		clear_bit(DLM_IFL_OVERLAP_CANCEL_BIT, &lkb->lkb_iflags);
		clear_bit(DLM_IFL_OVERLAP_UNLOCK_BIT, &lkb->lkb_iflags);
	}
 out:
	unlock_rsb(r);
	put_rsb(r);
	dlm_put_lkb(lkb);
	return 0;
}

static void __receive_convert_reply(struct dlm_rsb *r, struct dlm_lkb *lkb,
				    const struct dlm_message *ms, bool local)
{
	/* this is the value returned from do_convert() on the master */
	switch (from_dlm_errno(le32_to_cpu(ms->m_result))) {
	case -EAGAIN:
		/* convert would block (be queued) on remote master */
		queue_cast(r, lkb, -EAGAIN);
		break;

	case -EDEADLK:
		receive_flags_reply(lkb, ms, local);
		revert_lock_pc(r, lkb);
		queue_cast(r, lkb, -EDEADLK);
		break;

	case -EINPROGRESS:
		/* convert was queued on remote master */
		receive_flags_reply(lkb, ms, local);
		if (is_demoted(lkb))
			munge_demoted(lkb);
		del_lkb(r, lkb);
		add_lkb(r, lkb, DLM_LKSTS_CONVERT);
		break;

	case 0:
		/* convert was granted on remote master */
		receive_flags_reply(lkb, ms, local);
		if (is_demoted(lkb))
			munge_demoted(lkb);
		grant_lock_pc(r, lkb, ms);
		queue_cast(r, lkb, 0);
		break;

	default:
		log_error(r->res_ls, "receive_convert_reply %x remote %d %x %d",
			  lkb->lkb_id, le32_to_cpu(ms->m_header.h_nodeid),
			  le32_to_cpu(ms->m_lkid),
			  from_dlm_errno(le32_to_cpu(ms->m_result)));
		dlm_print_rsb(r);
		dlm_print_lkb(lkb);
	}
}

static void _receive_convert_reply(struct dlm_lkb *lkb,
				   const struct dlm_message *ms, bool local)
{
	struct dlm_rsb *r = lkb->lkb_resource;
	int error;

	hold_rsb(r);
	lock_rsb(r);

	error = validate_message(lkb, ms);
	if (error)
		goto out;

	error = remove_from_waiters_ms(lkb, ms, local);
	if (error)
		goto out;

	__receive_convert_reply(r, lkb, ms, local);
 out:
	unlock_rsb(r);
	put_rsb(r);
}

static int receive_convert_reply(struct dlm_ls *ls,
				 const struct dlm_message *ms)
{
	struct dlm_lkb *lkb;
	int error;

	error = find_lkb(ls, le32_to_cpu(ms->m_remid), &lkb);
	if (error)
		return error;

	_receive_convert_reply(lkb, ms, false);
	dlm_put_lkb(lkb);
	return 0;
}

static void _receive_unlock_reply(struct dlm_lkb *lkb,
				  const struct dlm_message *ms, bool local)
{
	struct dlm_rsb *r = lkb->lkb_resource;
	int error;

	hold_rsb(r);
	lock_rsb(r);

	error = validate_message(lkb, ms);
	if (error)
		goto out;

	error = remove_from_waiters_ms(lkb, ms, local);
	if (error)
		goto out;

	/* this is the value returned from do_unlock() on the master */

	switch (from_dlm_errno(le32_to_cpu(ms->m_result))) {
	case -DLM_EUNLOCK:
		receive_flags_reply(lkb, ms, local);
		remove_lock_pc(r, lkb);
		queue_cast(r, lkb, -DLM_EUNLOCK);
		break;
	case -ENOENT:
		break;
	default:
		log_error(r->res_ls, "receive_unlock_reply %x error %d",
			  lkb->lkb_id, from_dlm_errno(le32_to_cpu(ms->m_result)));
	}
 out:
	unlock_rsb(r);
	put_rsb(r);
}

static int receive_unlock_reply(struct dlm_ls *ls,
				const struct dlm_message *ms)
{
	struct dlm_lkb *lkb;
	int error;

	error = find_lkb(ls, le32_to_cpu(ms->m_remid), &lkb);
	if (error)
		return error;

	_receive_unlock_reply(lkb, ms, false);
	dlm_put_lkb(lkb);
	return 0;
}

static void _receive_cancel_reply(struct dlm_lkb *lkb,
				  const struct dlm_message *ms, bool local)
{
	struct dlm_rsb *r = lkb->lkb_resource;
	int error;

	hold_rsb(r);
	lock_rsb(r);

	error = validate_message(lkb, ms);
	if (error)
		goto out;

	error = remove_from_waiters_ms(lkb, ms, local);
	if (error)
		goto out;

	/* this is the value returned from do_cancel() on the master */

	switch (from_dlm_errno(le32_to_cpu(ms->m_result))) {
	case -DLM_ECANCEL:
		receive_flags_reply(lkb, ms, local);
		revert_lock_pc(r, lkb);
		queue_cast(r, lkb, -DLM_ECANCEL);
		break;
	case 0:
		break;
	default:
		log_error(r->res_ls, "receive_cancel_reply %x error %d",
			  lkb->lkb_id,
			  from_dlm_errno(le32_to_cpu(ms->m_result)));
	}
 out:
	unlock_rsb(r);
	put_rsb(r);
}

static int receive_cancel_reply(struct dlm_ls *ls,
				const struct dlm_message *ms)
{
	struct dlm_lkb *lkb;
	int error;

	error = find_lkb(ls, le32_to_cpu(ms->m_remid), &lkb);
	if (error)
		return error;

	_receive_cancel_reply(lkb, ms, false);
	dlm_put_lkb(lkb);
	return 0;
}

static void receive_lookup_reply(struct dlm_ls *ls,
				 const struct dlm_message *ms)
{
	struct dlm_lkb *lkb;
	struct dlm_rsb *r;
	int error, ret_nodeid;
	int do_lookup_list = 0;

	error = find_lkb(ls, le32_to_cpu(ms->m_lkid), &lkb);
	if (error) {
		log_error(ls, "%s no lkid %x", __func__,
			  le32_to_cpu(ms->m_lkid));
		return;
	}

	/* ms->m_result is the value returned by dlm_master_lookup on dir node
	   FIXME: will a non-zero error ever be returned? */

	r = lkb->lkb_resource;
	hold_rsb(r);
	lock_rsb(r);

	error = remove_from_waiters(lkb, DLM_MSG_LOOKUP_REPLY);
	if (error)
		goto out;

	ret_nodeid = le32_to_cpu(ms->m_nodeid);

	/* We sometimes receive a request from the dir node for this
	   rsb before we've received the dir node's loookup_reply for it.
	   The request from the dir node implies we're the master, so we set
	   ourself as master in receive_request_reply, and verify here that
	   we are indeed the master. */

	if (r->res_master_nodeid && (r->res_master_nodeid != ret_nodeid)) {
		/* This should never happen */
		log_error(ls, "receive_lookup_reply %x from %d ret %d "
			  "master %d dir %d our %d first %x %s",
			  lkb->lkb_id, le32_to_cpu(ms->m_header.h_nodeid),
			  ret_nodeid, r->res_master_nodeid, r->res_dir_nodeid,
			  dlm_our_nodeid(), r->res_first_lkid, r->res_name);
	}

	if (ret_nodeid == dlm_our_nodeid()) {
		r->res_master_nodeid = ret_nodeid;
		r->res_nodeid = 0;
		do_lookup_list = 1;
		r->res_first_lkid = 0;
	} else if (ret_nodeid == -1) {
		/* the remote node doesn't believe it's the dir node */
		log_error(ls, "receive_lookup_reply %x from %d bad ret_nodeid",
			  lkb->lkb_id, le32_to_cpu(ms->m_header.h_nodeid));
		r->res_master_nodeid = 0;
		r->res_nodeid = -1;
		lkb->lkb_nodeid = -1;
	} else {
		/* set_master() will set lkb_nodeid from r */
		r->res_master_nodeid = ret_nodeid;
		r->res_nodeid = ret_nodeid;
	}

	if (is_overlap(lkb)) {
		log_debug(ls, "receive_lookup_reply %x unlock %x",
			  lkb->lkb_id, dlm_iflags_val(lkb));
		queue_cast_overlap(r, lkb);
		unhold_lkb(lkb); /* undoes create_lkb() */
		goto out_list;
	}

	_request_lock(r, lkb);

 out_list:
	if (do_lookup_list)
		process_lookup_list(r);
 out:
	unlock_rsb(r);
	put_rsb(r);
	dlm_put_lkb(lkb);
}

static void _receive_message(struct dlm_ls *ls, const struct dlm_message *ms,
			     uint32_t saved_seq)
{
	int error = 0, noent = 0;

	if (WARN_ON_ONCE(!dlm_is_member(ls, le32_to_cpu(ms->m_header.h_nodeid)))) {
		log_limit(ls, "receive %d from non-member %d %x %x %d",
			  le32_to_cpu(ms->m_type),
			  le32_to_cpu(ms->m_header.h_nodeid),
			  le32_to_cpu(ms->m_lkid), le32_to_cpu(ms->m_remid),
			  from_dlm_errno(le32_to_cpu(ms->m_result)));
		return;
	}

	switch (ms->m_type) {

	/* messages sent to a master node */

	case cpu_to_le32(DLM_MSG_REQUEST):
		error = receive_request(ls, ms);
		break;

	case cpu_to_le32(DLM_MSG_CONVERT):
		error = receive_convert(ls, ms);
		break;

	case cpu_to_le32(DLM_MSG_UNLOCK):
		error = receive_unlock(ls, ms);
		break;

	case cpu_to_le32(DLM_MSG_CANCEL):
		noent = 1;
		error = receive_cancel(ls, ms);
		break;

	/* messages sent from a master node (replies to above) */

	case cpu_to_le32(DLM_MSG_REQUEST_REPLY):
		error = receive_request_reply(ls, ms);
		break;

	case cpu_to_le32(DLM_MSG_CONVERT_REPLY):
		error = receive_convert_reply(ls, ms);
		break;

	case cpu_to_le32(DLM_MSG_UNLOCK_REPLY):
		error = receive_unlock_reply(ls, ms);
		break;

	case cpu_to_le32(DLM_MSG_CANCEL_REPLY):
		error = receive_cancel_reply(ls, ms);
		break;

	/* messages sent from a master node (only two types of async msg) */

	case cpu_to_le32(DLM_MSG_GRANT):
		noent = 1;
		error = receive_grant(ls, ms);
		break;

	case cpu_to_le32(DLM_MSG_BAST):
		noent = 1;
		error = receive_bast(ls, ms);
		break;

	/* messages sent to a dir node */

	case cpu_to_le32(DLM_MSG_LOOKUP):
		receive_lookup(ls, ms);
		break;

	case cpu_to_le32(DLM_MSG_REMOVE):
		receive_remove(ls, ms);
		break;

	/* messages sent from a dir node (remove has no reply) */

	case cpu_to_le32(DLM_MSG_LOOKUP_REPLY):
		receive_lookup_reply(ls, ms);
		break;

	/* other messages */

	case cpu_to_le32(DLM_MSG_PURGE):
		receive_purge(ls, ms);
		break;

	default:
		log_error(ls, "unknown message type %d",
			  le32_to_cpu(ms->m_type));
	}

	/*
	 * When checking for ENOENT, we're checking the result of
	 * find_lkb(m_remid):
	 *
	 * The lock id referenced in the message wasn't found.  This may
	 * happen in normal usage for the async messages and cancel, so
	 * only use log_debug for them.
	 *
	 * Some errors are expected and normal.
	 */

	if (error == -ENOENT && noent) {
		log_debug(ls, "receive %d no %x remote %d %x saved_seq %u",
			  le32_to_cpu(ms->m_type), le32_to_cpu(ms->m_remid),
			  le32_to_cpu(ms->m_header.h_nodeid),
			  le32_to_cpu(ms->m_lkid), saved_seq);
	} else if (error == -ENOENT) {
		log_error(ls, "receive %d no %x remote %d %x saved_seq %u",
			  le32_to_cpu(ms->m_type), le32_to_cpu(ms->m_remid),
			  le32_to_cpu(ms->m_header.h_nodeid),
			  le32_to_cpu(ms->m_lkid), saved_seq);

		if (ms->m_type == cpu_to_le32(DLM_MSG_CONVERT))
			dlm_dump_rsb_hash(ls, le32_to_cpu(ms->m_hash));
	}

	if (error == -EINVAL) {
		log_error(ls, "receive %d inval from %d lkid %x remid %x "
			  "saved_seq %u",
			  le32_to_cpu(ms->m_type),
			  le32_to_cpu(ms->m_header.h_nodeid),
			  le32_to_cpu(ms->m_lkid), le32_to_cpu(ms->m_remid),
			  saved_seq);
	}
}

/* If the lockspace is in recovery mode (locking stopped), then normal
   messages are saved on the requestqueue for processing after recovery is
   done.  When not in recovery mode, we wait for dlm_recoverd to drain saved
   messages off the requestqueue before we process new ones. This occurs right
   after recovery completes when we transition from saving all messages on
   requestqueue, to processing all the saved messages, to processing new
   messages as they arrive. */

static void dlm_receive_message(struct dlm_ls *ls, const struct dlm_message *ms,
				int nodeid)
{
try_again:
	read_lock_bh(&ls->ls_requestqueue_lock);
	if (test_bit(LSFL_RECV_MSG_BLOCKED, &ls->ls_flags)) {
		/* If we were a member of this lockspace, left, and rejoined,
		   other nodes may still be sending us messages from the
		   lockspace generation before we left. */
		if (WARN_ON_ONCE(!ls->ls_generation)) {
			read_unlock_bh(&ls->ls_requestqueue_lock);
			log_limit(ls, "receive %d from %d ignore old gen",
				  le32_to_cpu(ms->m_type), nodeid);
			return;
		}

		read_unlock_bh(&ls->ls_requestqueue_lock);
		write_lock_bh(&ls->ls_requestqueue_lock);
		/* recheck because we hold writelock now */
		if (!test_bit(LSFL_RECV_MSG_BLOCKED, &ls->ls_flags)) {
			write_unlock_bh(&ls->ls_requestqueue_lock);
			goto try_again;
		}

		dlm_add_requestqueue(ls, nodeid, ms);
		write_unlock_bh(&ls->ls_requestqueue_lock);
	} else {
		_receive_message(ls, ms, 0);
		read_unlock_bh(&ls->ls_requestqueue_lock);
	}
}

/* This is called by dlm_recoverd to process messages that were saved on
   the requestqueue. */

void dlm_receive_message_saved(struct dlm_ls *ls, const struct dlm_message *ms,
			       uint32_t saved_seq)
{
	_receive_message(ls, ms, saved_seq);
}

/* This is called by the midcomms layer when something is received for
   the lockspace.  It could be either a MSG (normal message sent as part of
   standard locking activity) or an RCOM (recovery message sent as part of
   lockspace recovery). */

void dlm_receive_buffer(const union dlm_packet *p, int nodeid)
{
	const struct dlm_header *hd = &p->header;
	struct dlm_ls *ls;
	int type = 0;

	switch (hd->h_cmd) {
	case DLM_MSG:
		type = le32_to_cpu(p->message.m_type);
		break;
	case DLM_RCOM:
		type = le32_to_cpu(p->rcom.rc_type);
		break;
	default:
		log_print("invalid h_cmd %d from %u", hd->h_cmd, nodeid);
		return;
	}

	if (le32_to_cpu(hd->h_nodeid) != nodeid) {
		log_print("invalid h_nodeid %d from %d lockspace %x",
			  le32_to_cpu(hd->h_nodeid), nodeid,
			  le32_to_cpu(hd->u.h_lockspace));
		return;
	}

	ls = dlm_find_lockspace_global(le32_to_cpu(hd->u.h_lockspace));
	if (!ls) {
		if (dlm_config.ci_log_debug) {
			printk_ratelimited(KERN_DEBUG "dlm: invalid lockspace "
				"%u from %d cmd %d type %d\n",
				le32_to_cpu(hd->u.h_lockspace), nodeid,
				hd->h_cmd, type);
		}

		if (hd->h_cmd == DLM_RCOM && type == DLM_RCOM_STATUS)
			dlm_send_ls_not_ready(nodeid, &p->rcom);
		return;
	}

	/* this rwsem allows dlm_ls_stop() to wait for all dlm_recv threads to
	   be inactive (in this ls) before transitioning to recovery mode */

	read_lock_bh(&ls->ls_recv_active);
	if (hd->h_cmd == DLM_MSG)
		dlm_receive_message(ls, &p->message, nodeid);
	else if (hd->h_cmd == DLM_RCOM)
		dlm_receive_rcom(ls, &p->rcom, nodeid);
	else
		log_error(ls, "invalid h_cmd %d from %d lockspace %x",
			  hd->h_cmd, nodeid, le32_to_cpu(hd->u.h_lockspace));
	read_unlock_bh(&ls->ls_recv_active);

	dlm_put_lockspace(ls);
}

static void recover_convert_waiter(struct dlm_ls *ls, struct dlm_lkb *lkb,
				   struct dlm_message *ms_local)
{
	if (middle_conversion(lkb)) {
		hold_lkb(lkb);
		memset(ms_local, 0, sizeof(struct dlm_message));
		ms_local->m_type = cpu_to_le32(DLM_MSG_CONVERT_REPLY);
		ms_local->m_result = cpu_to_le32(to_dlm_errno(-EINPROGRESS));
		ms_local->m_header.h_nodeid = cpu_to_le32(lkb->lkb_nodeid);
		_receive_convert_reply(lkb, ms_local, true);

		/* Same special case as in receive_rcom_lock_args() */
		lkb->lkb_grmode = DLM_LOCK_IV;
		rsb_set_flag(lkb->lkb_resource, RSB_RECOVER_CONVERT);
		unhold_lkb(lkb);

	} else if (lkb->lkb_rqmode >= lkb->lkb_grmode) {
		set_bit(DLM_IFL_RESEND_BIT, &lkb->lkb_iflags);
	}

	/* lkb->lkb_rqmode < lkb->lkb_grmode shouldn't happen since down
	   conversions are async; there's no reply from the remote master */
}

/* A waiting lkb needs recovery if the master node has failed, or
   the master node is changing (only when no directory is used) */

static int waiter_needs_recovery(struct dlm_ls *ls, struct dlm_lkb *lkb,
				 int dir_nodeid)
{
	if (dlm_no_directory(ls))
		return 1;

	if (dlm_is_removed(ls, lkb->lkb_wait_nodeid))
		return 1;

	return 0;
}

/* Recovery for locks that are waiting for replies from nodes that are now
   gone.  We can just complete unlocks and cancels by faking a reply from the
   dead node.  Requests and up-conversions we flag to be resent after
   recovery.  Down-conversions can just be completed with a fake reply like
   unlocks.  Conversions between PR and CW need special attention. */

void dlm_recover_waiters_pre(struct dlm_ls *ls)
{
	struct dlm_lkb *lkb, *safe;
	struct dlm_message *ms_local;
	int wait_type, local_unlock_result, local_cancel_result;
	int dir_nodeid;

	ms_local = kmalloc(sizeof(*ms_local), GFP_KERNEL);
	if (!ms_local)
		return;

	list_for_each_entry_safe(lkb, safe, &ls->ls_waiters, lkb_wait_reply) {

		dir_nodeid = dlm_dir_nodeid(lkb->lkb_resource);

		/* exclude debug messages about unlocks because there can be so
		   many and they aren't very interesting */

		if (lkb->lkb_wait_type != DLM_MSG_UNLOCK) {
			log_debug(ls, "waiter %x remote %x msg %d r_nodeid %d "
				  "lkb_nodeid %d wait_nodeid %d dir_nodeid %d",
				  lkb->lkb_id,
				  lkb->lkb_remid,
				  lkb->lkb_wait_type,
				  lkb->lkb_resource->res_nodeid,
				  lkb->lkb_nodeid,
				  lkb->lkb_wait_nodeid,
				  dir_nodeid);
		}

		/* all outstanding lookups, regardless of destination  will be
		   resent after recovery is done */

		if (lkb->lkb_wait_type == DLM_MSG_LOOKUP) {
			set_bit(DLM_IFL_RESEND_BIT, &lkb->lkb_iflags);
			continue;
		}

		if (!waiter_needs_recovery(ls, lkb, dir_nodeid))
			continue;

		wait_type = lkb->lkb_wait_type;
		local_unlock_result = -DLM_EUNLOCK;
		local_cancel_result = -DLM_ECANCEL;

		/* Main reply may have been received leaving a zero wait_type,
		   but a reply for the overlapping op may not have been
		   received.  In that case we need to fake the appropriate
		   reply for the overlap op. */

		if (!wait_type) {
			if (is_overlap_cancel(lkb)) {
				wait_type = DLM_MSG_CANCEL;
				if (lkb->lkb_grmode == DLM_LOCK_IV)
					local_cancel_result = 0;
			}
			if (is_overlap_unlock(lkb)) {
				wait_type = DLM_MSG_UNLOCK;
				if (lkb->lkb_grmode == DLM_LOCK_IV)
					local_unlock_result = -ENOENT;
			}

			log_debug(ls, "rwpre overlap %x %x %d %d %d",
				  lkb->lkb_id, dlm_iflags_val(lkb), wait_type,
				  local_cancel_result, local_unlock_result);
		}

		switch (wait_type) {

		case DLM_MSG_REQUEST:
			set_bit(DLM_IFL_RESEND_BIT, &lkb->lkb_iflags);
			break;

		case DLM_MSG_CONVERT:
			recover_convert_waiter(ls, lkb, ms_local);
			break;

		case DLM_MSG_UNLOCK:
			hold_lkb(lkb);
			memset(ms_local, 0, sizeof(struct dlm_message));
			ms_local->m_type = cpu_to_le32(DLM_MSG_UNLOCK_REPLY);
			ms_local->m_result = cpu_to_le32(to_dlm_errno(local_unlock_result));
			ms_local->m_header.h_nodeid = cpu_to_le32(lkb->lkb_nodeid);
			_receive_unlock_reply(lkb, ms_local, true);
			dlm_put_lkb(lkb);
			break;

		case DLM_MSG_CANCEL:
			hold_lkb(lkb);
			memset(ms_local, 0, sizeof(struct dlm_message));
			ms_local->m_type = cpu_to_le32(DLM_MSG_CANCEL_REPLY);
			ms_local->m_result = cpu_to_le32(to_dlm_errno(local_cancel_result));
			ms_local->m_header.h_nodeid = cpu_to_le32(lkb->lkb_nodeid);
			_receive_cancel_reply(lkb, ms_local, true);
			dlm_put_lkb(lkb);
			break;

		default:
			log_error(ls, "invalid lkb wait_type %d %d",
				  lkb->lkb_wait_type, wait_type);
		}
		schedule();
	}
	kfree(ms_local);
}

static struct dlm_lkb *find_resend_waiter(struct dlm_ls *ls)
{
	struct dlm_lkb *lkb = NULL, *iter;

	spin_lock_bh(&ls->ls_waiters_lock);
	list_for_each_entry(iter, &ls->ls_waiters, lkb_wait_reply) {
		if (test_bit(DLM_IFL_RESEND_BIT, &iter->lkb_iflags)) {
			hold_lkb(iter);
			lkb = iter;
			break;
		}
	}
	spin_unlock_bh(&ls->ls_waiters_lock);

	return lkb;
}

/*
 * Forced state reset for locks that were in the middle of remote operations
 * when recovery happened (i.e. lkbs that were on the waiters list, waiting
 * for a reply from a remote operation.)  The lkbs remaining on the waiters
 * list need to be reevaluated; some may need resending to a different node
 * than previously, and some may now need local handling rather than remote.
 *
 * First, the lkb state for the voided remote operation is forcibly reset,
 * equivalent to what remove_from_waiters() would normally do:
 * . lkb removed from ls_waiters list
 * . lkb wait_type cleared
 * . lkb waiters_count cleared
 * . lkb ref count decremented for each waiters_count (almost always 1,
 *   but possibly 2 in case of cancel/unlock overlapping, which means
 *   two remote replies were being expected for the lkb.)
 *
 * Second, the lkb is reprocessed like an original operation would be,
 * by passing it to _request_lock or _convert_lock, which will either
 * process the lkb operation locally, or send it to a remote node again
 * and put the lkb back onto the waiters list.
 *
 * When reprocessing the lkb, we may find that it's flagged for an overlapping
 * force-unlock or cancel, either from before recovery began, or after recovery
 * finished.  If this is the case, the unlock/cancel is done directly, and the
 * original operation is not initiated again (no _request_lock/_convert_lock.)
 */

int dlm_recover_waiters_post(struct dlm_ls *ls)
{
	struct dlm_lkb *lkb;
	struct dlm_rsb *r;
	int error = 0, mstype, err, oc, ou;

	while (1) {
		if (dlm_locking_stopped(ls)) {
			log_debug(ls, "recover_waiters_post aborted");
			error = -EINTR;
			break;
		}

		/* 
		 * Find an lkb from the waiters list that's been affected by
		 * recovery node changes, and needs to be reprocessed.  Does
		 * hold_lkb(), adding a refcount.
		 */
		lkb = find_resend_waiter(ls);
		if (!lkb)
			break;

		r = lkb->lkb_resource;
		hold_rsb(r);
		lock_rsb(r);

		/*
		 * If the lkb has been flagged for a force unlock or cancel,
		 * then the reprocessing below will be replaced by just doing
		 * the unlock/cancel directly.
		 */
		mstype = lkb->lkb_wait_type;
		oc = test_and_clear_bit(DLM_IFL_OVERLAP_CANCEL_BIT,
					&lkb->lkb_iflags);
		ou = test_and_clear_bit(DLM_IFL_OVERLAP_UNLOCK_BIT,
					&lkb->lkb_iflags);
		err = 0;

		log_debug(ls, "waiter %x remote %x msg %d r_nodeid %d "
			  "lkb_nodeid %d wait_nodeid %d dir_nodeid %d "
			  "overlap %d %d", lkb->lkb_id, lkb->lkb_remid, mstype,
			  r->res_nodeid, lkb->lkb_nodeid, lkb->lkb_wait_nodeid,
			  dlm_dir_nodeid(r), oc, ou);

		/*
		 * No reply to the pre-recovery operation will now be received,
		 * so a forced equivalent of remove_from_waiters() is needed to
		 * reset the waiters state that was in place before recovery.
		 */

		clear_bit(DLM_IFL_RESEND_BIT, &lkb->lkb_iflags);

		/* Forcibly clear wait_type */
		lkb->lkb_wait_type = 0;

		/*
		 * Forcibly reset wait_count and associated refcount.  The
		 * wait_count will almost always be 1, but in case of an
		 * overlapping unlock/cancel it could be 2: see where
		 * add_to_waiters() finds the lkb is already on the waiters
		 * list and does lkb_wait_count++; hold_lkb().
		 */
		while (lkb->lkb_wait_count) {
			lkb->lkb_wait_count--;
			unhold_lkb(lkb);
		}

		/* Forcibly remove from waiters list */
		spin_lock_bh(&ls->ls_waiters_lock);
		list_del_init(&lkb->lkb_wait_reply);
		spin_unlock_bh(&ls->ls_waiters_lock);

		/*
		 * The lkb is now clear of all prior waiters state and can be
		 * processed locally, or sent to remote node again, or directly
		 * cancelled/unlocked.
		 */

		if (oc || ou) {
			/* do an unlock or cancel instead of resending */
			switch (mstype) {
			case DLM_MSG_LOOKUP:
			case DLM_MSG_REQUEST:
				queue_cast(r, lkb, ou ? -DLM_EUNLOCK :
							-DLM_ECANCEL);
				unhold_lkb(lkb); /* undoes create_lkb() */
				break;
			case DLM_MSG_CONVERT:
				if (oc) {
					queue_cast(r, lkb, -DLM_ECANCEL);
				} else {
					lkb->lkb_exflags |= DLM_LKF_FORCEUNLOCK;
					_unlock_lock(r, lkb);
				}
				break;
			default:
				err = 1;
			}
		} else {
			switch (mstype) {
			case DLM_MSG_LOOKUP:
			case DLM_MSG_REQUEST:
				_request_lock(r, lkb);
				if (r->res_nodeid != -1 && is_master(r))
					confirm_master(r, 0);
				break;
			case DLM_MSG_CONVERT:
				_convert_lock(r, lkb);
				break;
			default:
				err = 1;
			}
		}

		if (err) {
			log_error(ls, "waiter %x msg %d r_nodeid %d "
				  "dir_nodeid %d overlap %d %d",
				  lkb->lkb_id, mstype, r->res_nodeid,
				  dlm_dir_nodeid(r), oc, ou);
		}
		unlock_rsb(r);
		put_rsb(r);
		dlm_put_lkb(lkb);
	}

	return error;
}

static void purge_mstcpy_list(struct dlm_ls *ls, struct dlm_rsb *r,
			      struct list_head *list)
{
	struct dlm_lkb *lkb, *safe;

	list_for_each_entry_safe(lkb, safe, list, lkb_statequeue) {
		if (!is_master_copy(lkb))
			continue;

		/* don't purge lkbs we've added in recover_master_copy for
		   the current recovery seq */

		if (lkb->lkb_recover_seq == ls->ls_recover_seq)
			continue;

		del_lkb(r, lkb);

		/* this put should free the lkb */
		if (!dlm_put_lkb(lkb))
			log_error(ls, "purged mstcpy lkb not released");
	}
}

void dlm_purge_mstcpy_locks(struct dlm_rsb *r)
{
	struct dlm_ls *ls = r->res_ls;

	purge_mstcpy_list(ls, r, &r->res_grantqueue);
	purge_mstcpy_list(ls, r, &r->res_convertqueue);
	purge_mstcpy_list(ls, r, &r->res_waitqueue);
}

static void purge_dead_list(struct dlm_ls *ls, struct dlm_rsb *r,
			    struct list_head *list,
			    int nodeid_gone, unsigned int *count)
{
	struct dlm_lkb *lkb, *safe;

	list_for_each_entry_safe(lkb, safe, list, lkb_statequeue) {
		if (!is_master_copy(lkb))
			continue;

		if ((lkb->lkb_nodeid == nodeid_gone) ||
		    dlm_is_removed(ls, lkb->lkb_nodeid)) {

			/* tell recover_lvb to invalidate the lvb
			   because a node holding EX/PW failed */
			if ((lkb->lkb_exflags & DLM_LKF_VALBLK) &&
			    (lkb->lkb_grmode >= DLM_LOCK_PW)) {
				rsb_set_flag(r, RSB_RECOVER_LVB_INVAL);
			}

			del_lkb(r, lkb);

			/* this put should free the lkb */
			if (!dlm_put_lkb(lkb))
				log_error(ls, "purged dead lkb not released");

			rsb_set_flag(r, RSB_RECOVER_GRANT);

			(*count)++;
		}
	}
}

/* Get rid of locks held by nodes that are gone. */

void dlm_recover_purge(struct dlm_ls *ls, const struct list_head *root_list)
{
	struct dlm_rsb *r;
	struct dlm_member *memb;
	int nodes_count = 0;
	int nodeid_gone = 0;
	unsigned int lkb_count = 0;

	/* cache one removed nodeid to optimize the common
	   case of a single node removed */

	list_for_each_entry(memb, &ls->ls_nodes_gone, list) {
		nodes_count++;
		nodeid_gone = memb->nodeid;
	}

	if (!nodes_count)
		return;

	list_for_each_entry(r, root_list, res_root_list) {
		lock_rsb(r);
		if (r->res_nodeid != -1 && is_master(r)) {
			purge_dead_list(ls, r, &r->res_grantqueue,
					nodeid_gone, &lkb_count);
			purge_dead_list(ls, r, &r->res_convertqueue,
					nodeid_gone, &lkb_count);
			purge_dead_list(ls, r, &r->res_waitqueue,
					nodeid_gone, &lkb_count);
		}
		unlock_rsb(r);

		cond_resched();
	}

	if (lkb_count)
		log_rinfo(ls, "dlm_recover_purge %u locks for %u nodes",
			  lkb_count, nodes_count);
}

static struct dlm_rsb *find_grant_rsb(struct dlm_ls *ls)
{
	struct dlm_rsb *r;

	read_lock_bh(&ls->ls_rsbtbl_lock);
	list_for_each_entry(r, &ls->ls_slow_active, res_slow_list) {
		if (!rsb_flag(r, RSB_RECOVER_GRANT))
			continue;
		if (!is_master(r)) {
			rsb_clear_flag(r, RSB_RECOVER_GRANT);
			continue;
		}
		hold_rsb(r);
		read_unlock_bh(&ls->ls_rsbtbl_lock);
		return r;
	}
	read_unlock_bh(&ls->ls_rsbtbl_lock);
	return NULL;
}

/*
 * Attempt to grant locks on resources that we are the master of.
 * Locks may have become grantable during recovery because locks
 * from departed nodes have been purged (or not rebuilt), allowing
 * previously blocked locks to now be granted.  The subset of rsb's
 * we are interested in are those with lkb's on either the convert or
 * waiting queues.
 *
 * Simplest would be to go through each master rsb and check for non-empty
 * convert or waiting queues, and attempt to grant on those rsbs.
 * Checking the queues requires lock_rsb, though, for which we'd need
 * to release the rsbtbl lock.  This would make iterating through all
 * rsb's very inefficient.  So, we rely on earlier recovery routines
 * to set RECOVER_GRANT on any rsb's that we should attempt to grant
 * locks for.
 */

void dlm_recover_grant(struct dlm_ls *ls)
{
	struct dlm_rsb *r;
	unsigned int count = 0;
	unsigned int rsb_count = 0;
	unsigned int lkb_count = 0;

	while (1) {
		r = find_grant_rsb(ls);
		if (!r)
			break;

		rsb_count++;
		count = 0;
		lock_rsb(r);
		/* the RECOVER_GRANT flag is checked in the grant path */
		grant_pending_locks(r, &count);
		rsb_clear_flag(r, RSB_RECOVER_GRANT);
		lkb_count += count;
		confirm_master(r, 0);
		unlock_rsb(r);
		put_rsb(r);
		cond_resched();
	}

	if (lkb_count)
		log_rinfo(ls, "dlm_recover_grant %u locks on %u resources",
			  lkb_count, rsb_count);
}

static struct dlm_lkb *search_remid_list(struct list_head *head, int nodeid,
					 uint32_t remid)
{
	struct dlm_lkb *lkb;

	list_for_each_entry(lkb, head, lkb_statequeue) {
		if (lkb->lkb_nodeid == nodeid && lkb->lkb_remid == remid)
			return lkb;
	}
	return NULL;
}

static struct dlm_lkb *search_remid(struct dlm_rsb *r, int nodeid,
				    uint32_t remid)
{
	struct dlm_lkb *lkb;

	lkb = search_remid_list(&r->res_grantqueue, nodeid, remid);
	if (lkb)
		return lkb;
	lkb = search_remid_list(&r->res_convertqueue, nodeid, remid);
	if (lkb)
		return lkb;
	lkb = search_remid_list(&r->res_waitqueue, nodeid, remid);
	if (lkb)
		return lkb;
	return NULL;
}

/* needs at least dlm_rcom + rcom_lock */
static int receive_rcom_lock_args(struct dlm_ls *ls, struct dlm_lkb *lkb,
				  struct dlm_rsb *r, const struct dlm_rcom *rc)
{
	struct rcom_lock *rl = (struct rcom_lock *) rc->rc_buf;

	lkb->lkb_nodeid = le32_to_cpu(rc->rc_header.h_nodeid);
	lkb->lkb_ownpid = le32_to_cpu(rl->rl_ownpid);
	lkb->lkb_remid = le32_to_cpu(rl->rl_lkid);
	lkb->lkb_exflags = le32_to_cpu(rl->rl_exflags);
	dlm_set_dflags_val(lkb, le32_to_cpu(rl->rl_flags));
	set_bit(DLM_IFL_MSTCPY_BIT, &lkb->lkb_iflags);
	lkb->lkb_lvbseq = le32_to_cpu(rl->rl_lvbseq);
	lkb->lkb_rqmode = rl->rl_rqmode;
	lkb->lkb_grmode = rl->rl_grmode;
	/* don't set lkb_status because add_lkb wants to itself */

	lkb->lkb_bastfn = (rl->rl_asts & DLM_CB_BAST) ? &fake_bastfn : NULL;
	lkb->lkb_astfn = (rl->rl_asts & DLM_CB_CAST) ? &fake_astfn : NULL;

	if (lkb->lkb_exflags & DLM_LKF_VALBLK) {
		int lvblen = le16_to_cpu(rc->rc_header.h_length) -
			sizeof(struct dlm_rcom) - sizeof(struct rcom_lock);
		if (lvblen > ls->ls_lvblen)
			return -EINVAL;
		lkb->lkb_lvbptr = dlm_allocate_lvb(ls);
		if (!lkb->lkb_lvbptr)
			return -ENOMEM;
		memcpy(lkb->lkb_lvbptr, rl->rl_lvb, lvblen);
	}

	/* Conversions between PR and CW (middle modes) need special handling.
	   The real granted mode of these converting locks cannot be determined
	   until all locks have been rebuilt on the rsb (recover_conversion) */

	if (rl->rl_wait_type == cpu_to_le16(DLM_MSG_CONVERT) &&
	    middle_conversion(lkb)) {
		rl->rl_status = DLM_LKSTS_CONVERT;
		lkb->lkb_grmode = DLM_LOCK_IV;
		rsb_set_flag(r, RSB_RECOVER_CONVERT);
	}

	return 0;
}

/* This lkb may have been recovered in a previous aborted recovery so we need
   to check if the rsb already has an lkb with the given remote nodeid/lkid.
   If so we just send back a standard reply.  If not, we create a new lkb with
   the given values and send back our lkid.  We send back our lkid by sending
   back the rcom_lock struct we got but with the remid field filled in. */

/* needs at least dlm_rcom + rcom_lock */
int dlm_recover_master_copy(struct dlm_ls *ls, const struct dlm_rcom *rc,
			    __le32 *rl_remid, __le32 *rl_result)
{
	struct rcom_lock *rl = (struct rcom_lock *) rc->rc_buf;
	struct dlm_rsb *r;
	struct dlm_lkb *lkb;
	uint32_t remid = 0;
	int from_nodeid = le32_to_cpu(rc->rc_header.h_nodeid);
	int error;

	/* init rl_remid with rcom lock rl_remid */
	*rl_remid = rl->rl_remid;

	if (rl->rl_parent_lkid) {
		error = -EOPNOTSUPP;
		goto out;
	}

	remid = le32_to_cpu(rl->rl_lkid);

	/* In general we expect the rsb returned to be R_MASTER, but we don't
	   have to require it.  Recovery of masters on one node can overlap
	   recovery of locks on another node, so one node can send us MSTCPY
	   locks before we've made ourselves master of this rsb.  We can still
	   add new MSTCPY locks that we receive here without any harm; when
	   we make ourselves master, dlm_recover_masters() won't touch the
	   MSTCPY locks we've received early. */

	error = find_rsb(ls, rl->rl_name, le16_to_cpu(rl->rl_namelen),
			 from_nodeid, R_RECEIVE_RECOVER, &r);
	if (error)
		goto out;

	lock_rsb(r);

	if (dlm_no_directory(ls) && (dlm_dir_nodeid(r) != dlm_our_nodeid())) {
		log_error(ls, "dlm_recover_master_copy remote %d %x not dir",
			  from_nodeid, remid);
		error = -EBADR;
		goto out_unlock;
	}

	lkb = search_remid(r, from_nodeid, remid);
	if (lkb) {
		error = -EEXIST;
		goto out_remid;
	}

	error = create_lkb(ls, &lkb);
	if (error)
		goto out_unlock;

	error = receive_rcom_lock_args(ls, lkb, r, rc);
	if (error) {
		__put_lkb(ls, lkb);
		goto out_unlock;
	}

	attach_lkb(r, lkb);
	add_lkb(r, lkb, rl->rl_status);
	ls->ls_recover_locks_in++;

	if (!list_empty(&r->res_waitqueue) || !list_empty(&r->res_convertqueue))
		rsb_set_flag(r, RSB_RECOVER_GRANT);

 out_remid:
	/* this is the new value returned to the lock holder for
	   saving in its process-copy lkb */
	*rl_remid = cpu_to_le32(lkb->lkb_id);

	lkb->lkb_recover_seq = ls->ls_recover_seq;

 out_unlock:
	unlock_rsb(r);
	put_rsb(r);
 out:
	if (error && error != -EEXIST)
		log_rinfo(ls, "dlm_recover_master_copy remote %d %x error %d",
			  from_nodeid, remid, error);
	*rl_result = cpu_to_le32(error);
	return error;
}

/* needs at least dlm_rcom + rcom_lock */
int dlm_recover_process_copy(struct dlm_ls *ls, const struct dlm_rcom *rc,
			     uint64_t seq)
{
	struct rcom_lock *rl = (struct rcom_lock *) rc->rc_buf;
	struct dlm_rsb *r;
	struct dlm_lkb *lkb;
	uint32_t lkid, remid;
	int error, result;

	lkid = le32_to_cpu(rl->rl_lkid);
	remid = le32_to_cpu(rl->rl_remid);
	result = le32_to_cpu(rl->rl_result);

	error = find_lkb(ls, lkid, &lkb);
	if (error) {
		log_error(ls, "dlm_recover_process_copy no %x remote %d %x %d",
			  lkid, le32_to_cpu(rc->rc_header.h_nodeid), remid,
			  result);
		return error;
	}

	r = lkb->lkb_resource;
	hold_rsb(r);
	lock_rsb(r);

	if (!is_process_copy(lkb)) {
		log_error(ls, "dlm_recover_process_copy bad %x remote %d %x %d",
			  lkid, le32_to_cpu(rc->rc_header.h_nodeid), remid,
			  result);
		dlm_dump_rsb(r);
		unlock_rsb(r);
		put_rsb(r);
		dlm_put_lkb(lkb);
		return -EINVAL;
	}

	switch (result) {
	case -EBADR:
		/* There's a chance the new master received our lock before
		   dlm_recover_master_reply(), this wouldn't happen if we did
		   a barrier between recover_masters and recover_locks. */

		log_debug(ls, "dlm_recover_process_copy %x remote %d %x %d",
			  lkid, le32_to_cpu(rc->rc_header.h_nodeid), remid,
			  result);
	
		dlm_send_rcom_lock(r, lkb, seq);
		goto out;
	case -EEXIST:
	case 0:
		lkb->lkb_remid = remid;
		break;
	default:
		log_error(ls, "dlm_recover_process_copy %x remote %d %x %d unk",
			  lkid, le32_to_cpu(rc->rc_header.h_nodeid), remid,
			  result);
	}

	/* an ack for dlm_recover_locks() which waits for replies from
	   all the locks it sends to new masters */
	dlm_recovered_lock(r);
 out:
	unlock_rsb(r);
	put_rsb(r);
	dlm_put_lkb(lkb);

	return 0;
}

int dlm_user_request(struct dlm_ls *ls, struct dlm_user_args *ua,
		     int mode, uint32_t flags, void *name, unsigned int namelen)
{
	struct dlm_lkb *lkb;
	struct dlm_args args;
	bool do_put = true;
	int error;

	dlm_lock_recovery(ls);

	error = create_lkb(ls, &lkb);
	if (error) {
		kfree(ua);
		goto out;
	}

	trace_dlm_lock_start(ls, lkb, name, namelen, mode, flags);

	if (flags & DLM_LKF_VALBLK) {
		ua->lksb.sb_lvbptr = kzalloc(DLM_USER_LVB_LEN, GFP_NOFS);
		if (!ua->lksb.sb_lvbptr) {
			kfree(ua);
			error = -ENOMEM;
			goto out_put;
		}
	}
	error = set_lock_args(mode, &ua->lksb, flags, namelen, fake_astfn, ua,
			      fake_bastfn, &args);
	if (error) {
		kfree(ua->lksb.sb_lvbptr);
		ua->lksb.sb_lvbptr = NULL;
		kfree(ua);
		goto out_put;
	}

	/* After ua is attached to lkb it will be freed by dlm_free_lkb().
	   When DLM_DFL_USER_BIT is set, the dlm knows that this is a userspace
	   lock and that lkb_astparam is the dlm_user_args structure. */
	set_bit(DLM_DFL_USER_BIT, &lkb->lkb_dflags);
	error = request_lock(ls, lkb, name, namelen, &args);

	switch (error) {
	case 0:
		break;
	case -EINPROGRESS:
		error = 0;
		break;
	case -EAGAIN:
		error = 0;
		fallthrough;
	default:
		goto out_put;
	}

	/* add this new lkb to the per-process list of locks */
	spin_lock_bh(&ua->proc->locks_spin);
	hold_lkb(lkb);
	list_add_tail(&lkb->lkb_ownqueue, &ua->proc->locks);
	spin_unlock_bh(&ua->proc->locks_spin);
	do_put = false;
 out_put:
	trace_dlm_lock_end(ls, lkb, name, namelen, mode, flags, error, false);
	if (do_put)
		__put_lkb(ls, lkb);
 out:
	dlm_unlock_recovery(ls);
	return error;
}

int dlm_user_convert(struct dlm_ls *ls, struct dlm_user_args *ua_tmp,
		     int mode, uint32_t flags, uint32_t lkid, char *lvb_in)
{
	struct dlm_lkb *lkb;
	struct dlm_args args;
	struct dlm_user_args *ua;
	int error;

	dlm_lock_recovery(ls);

	error = find_lkb(ls, lkid, &lkb);
	if (error)
		goto out;

	trace_dlm_lock_start(ls, lkb, NULL, 0, mode, flags);

	/* user can change the params on its lock when it converts it, or
	   add an lvb that didn't exist before */

	ua = lkb->lkb_ua;

	if (flags & DLM_LKF_VALBLK && !ua->lksb.sb_lvbptr) {
		ua->lksb.sb_lvbptr = kzalloc(DLM_USER_LVB_LEN, GFP_NOFS);
		if (!ua->lksb.sb_lvbptr) {
			error = -ENOMEM;
			goto out_put;
		}
	}
	if (lvb_in && ua->lksb.sb_lvbptr)
		memcpy(ua->lksb.sb_lvbptr, lvb_in, DLM_USER_LVB_LEN);

	ua->xid = ua_tmp->xid;
	ua->castparam = ua_tmp->castparam;
	ua->castaddr = ua_tmp->castaddr;
	ua->bastparam = ua_tmp->bastparam;
	ua->bastaddr = ua_tmp->bastaddr;
	ua->user_lksb = ua_tmp->user_lksb;

	error = set_lock_args(mode, &ua->lksb, flags, 0, fake_astfn, ua,
			      fake_bastfn, &args);
	if (error)
		goto out_put;

	error = convert_lock(ls, lkb, &args);

	if (error == -EINPROGRESS || error == -EAGAIN || error == -EDEADLK)
		error = 0;
 out_put:
	trace_dlm_lock_end(ls, lkb, NULL, 0, mode, flags, error, false);
	dlm_put_lkb(lkb);
 out:
	dlm_unlock_recovery(ls);
	kfree(ua_tmp);
	return error;
}

/*
 * The caller asks for an orphan lock on a given resource with a given mode.
 * If a matching lock exists, it's moved to the owner's list of locks and
 * the lkid is returned.
 */

int dlm_user_adopt_orphan(struct dlm_ls *ls, struct dlm_user_args *ua_tmp,
		     int mode, uint32_t flags, void *name, unsigned int namelen,
		     uint32_t *lkid)
{
	struct dlm_lkb *lkb = NULL, *iter;
	struct dlm_user_args *ua;
	int found_other_mode = 0;
	int rv = 0;

	spin_lock_bh(&ls->ls_orphans_lock);
	list_for_each_entry(iter, &ls->ls_orphans, lkb_ownqueue) {
		if (iter->lkb_resource->res_length != namelen)
			continue;
		if (memcmp(iter->lkb_resource->res_name, name, namelen))
			continue;
		if (iter->lkb_grmode != mode) {
			found_other_mode = 1;
			continue;
		}

		lkb = iter;
		list_del_init(&iter->lkb_ownqueue);
		clear_bit(DLM_DFL_ORPHAN_BIT, &iter->lkb_dflags);
		*lkid = iter->lkb_id;
		break;
	}
	spin_unlock_bh(&ls->ls_orphans_lock);

	if (!lkb && found_other_mode) {
		rv = -EAGAIN;
		goto out;
	}

	if (!lkb) {
		rv = -ENOENT;
		goto out;
	}

	lkb->lkb_exflags = flags;
	lkb->lkb_ownpid = (int) current->pid;

	ua = lkb->lkb_ua;

	ua->proc = ua_tmp->proc;
	ua->xid = ua_tmp->xid;
	ua->castparam = ua_tmp->castparam;
	ua->castaddr = ua_tmp->castaddr;
	ua->bastparam = ua_tmp->bastparam;
	ua->bastaddr = ua_tmp->bastaddr;
	ua->user_lksb = ua_tmp->user_lksb;

	/*
	 * The lkb reference from the ls_orphans list was not
	 * removed above, and is now considered the reference
	 * for the proc locks list.
	 */

	spin_lock_bh(&ua->proc->locks_spin);
	list_add_tail(&lkb->lkb_ownqueue, &ua->proc->locks);
	spin_unlock_bh(&ua->proc->locks_spin);
 out:
	kfree(ua_tmp);
	return rv;
}

int dlm_user_unlock(struct dlm_ls *ls, struct dlm_user_args *ua_tmp,
		    uint32_t flags, uint32_t lkid, char *lvb_in)
{
	struct dlm_lkb *lkb;
	struct dlm_args args;
	struct dlm_user_args *ua;
	int error;

	dlm_lock_recovery(ls);

	error = find_lkb(ls, lkid, &lkb);
	if (error)
		goto out;

	trace_dlm_unlock_start(ls, lkb, flags);

	ua = lkb->lkb_ua;

	if (lvb_in && ua->lksb.sb_lvbptr)
		memcpy(ua->lksb.sb_lvbptr, lvb_in, DLM_USER_LVB_LEN);
	if (ua_tmp->castparam)
		ua->castparam = ua_tmp->castparam;
	ua->user_lksb = ua_tmp->user_lksb;

	error = set_unlock_args(flags, ua, &args);
	if (error)
		goto out_put;

	error = unlock_lock(ls, lkb, &args);

	if (error == -DLM_EUNLOCK)
		error = 0;
	/* from validate_unlock_args() */
	if (error == -EBUSY && (flags & DLM_LKF_FORCEUNLOCK))
		error = 0;
	if (error)
		goto out_put;

	spin_lock_bh(&ua->proc->locks_spin);
	/* dlm_user_add_cb() may have already taken lkb off the proc list */
	if (!list_empty(&lkb->lkb_ownqueue))
		list_move(&lkb->lkb_ownqueue, &ua->proc->unlocking);
	spin_unlock_bh(&ua->proc->locks_spin);
 out_put:
	trace_dlm_unlock_end(ls, lkb, flags, error);
	dlm_put_lkb(lkb);
 out:
	dlm_unlock_recovery(ls);
	kfree(ua_tmp);
	return error;
}

int dlm_user_cancel(struct dlm_ls *ls, struct dlm_user_args *ua_tmp,
		    uint32_t flags, uint32_t lkid)
{
	struct dlm_lkb *lkb;
	struct dlm_args args;
	struct dlm_user_args *ua;
	int error;

	dlm_lock_recovery(ls);

	error = find_lkb(ls, lkid, &lkb);
	if (error)
		goto out;

	trace_dlm_unlock_start(ls, lkb, flags);

	ua = lkb->lkb_ua;
	if (ua_tmp->castparam)
		ua->castparam = ua_tmp->castparam;
	ua->user_lksb = ua_tmp->user_lksb;

	error = set_unlock_args(flags, ua, &args);
	if (error)
		goto out_put;

	error = cancel_lock(ls, lkb, &args);

	if (error == -DLM_ECANCEL)
		error = 0;
	/* from validate_unlock_args() */
	if (error == -EBUSY)
		error = 0;
 out_put:
	trace_dlm_unlock_end(ls, lkb, flags, error);
	dlm_put_lkb(lkb);
 out:
	dlm_unlock_recovery(ls);
	kfree(ua_tmp);
	return error;
}

int dlm_user_deadlock(struct dlm_ls *ls, uint32_t flags, uint32_t lkid)
{
	struct dlm_lkb *lkb;
	struct dlm_args args;
	struct dlm_user_args *ua;
	struct dlm_rsb *r;
	int error;

	dlm_lock_recovery(ls);

	error = find_lkb(ls, lkid, &lkb);
	if (error)
		goto out;

	trace_dlm_unlock_start(ls, lkb, flags);

	ua = lkb->lkb_ua;

	error = set_unlock_args(flags, ua, &args);
	if (error)
		goto out_put;

	/* same as cancel_lock(), but set DEADLOCK_CANCEL after lock_rsb */

	r = lkb->lkb_resource;
	hold_rsb(r);
	lock_rsb(r);

	error = validate_unlock_args(lkb, &args);
	if (error)
		goto out_r;
	set_bit(DLM_IFL_DEADLOCK_CANCEL_BIT, &lkb->lkb_iflags);

	error = _cancel_lock(r, lkb);
 out_r:
	unlock_rsb(r);
	put_rsb(r);

	if (error == -DLM_ECANCEL)
		error = 0;
	/* from validate_unlock_args() */
	if (error == -EBUSY)
		error = 0;
 out_put:
	trace_dlm_unlock_end(ls, lkb, flags, error);
	dlm_put_lkb(lkb);
 out:
	dlm_unlock_recovery(ls);
	return error;
}

/* lkb's that are removed from the waiters list by revert are just left on the
   orphans list with the granted orphan locks, to be freed by purge */

static int orphan_proc_lock(struct dlm_ls *ls, struct dlm_lkb *lkb)
{
	struct dlm_args args;
	int error;

	hold_lkb(lkb); /* reference for the ls_orphans list */
	spin_lock_bh(&ls->ls_orphans_lock);
	list_add_tail(&lkb->lkb_ownqueue, &ls->ls_orphans);
	spin_unlock_bh(&ls->ls_orphans_lock);

	set_unlock_args(0, lkb->lkb_ua, &args);

	error = cancel_lock(ls, lkb, &args);
	if (error == -DLM_ECANCEL)
		error = 0;
	return error;
}

/* The FORCEUNLOCK flag allows the unlock to go ahead even if the lkb isn't
   granted.  Regardless of what rsb queue the lock is on, it's removed and
   freed.  The IVVALBLK flag causes the lvb on the resource to be invalidated
   if our lock is PW/EX (it's ignored if our granted mode is smaller.) */

static int unlock_proc_lock(struct dlm_ls *ls, struct dlm_lkb *lkb)
{
	struct dlm_args args;
	int error;

	set_unlock_args(DLM_LKF_FORCEUNLOCK | DLM_LKF_IVVALBLK,
			lkb->lkb_ua, &args);

	error = unlock_lock(ls, lkb, &args);
	if (error == -DLM_EUNLOCK)
		error = 0;
	return error;
}

/* We have to release clear_proc_locks mutex before calling unlock_proc_lock()
   (which does lock_rsb) due to deadlock with receiving a message that does
   lock_rsb followed by dlm_user_add_cb() */

static struct dlm_lkb *del_proc_lock(struct dlm_ls *ls,
				     struct dlm_user_proc *proc)
{
	struct dlm_lkb *lkb = NULL;

	spin_lock_bh(&ls->ls_clear_proc_locks);
	if (list_empty(&proc->locks))
		goto out;

	lkb = list_entry(proc->locks.next, struct dlm_lkb, lkb_ownqueue);
	list_del_init(&lkb->lkb_ownqueue);

	if (lkb->lkb_exflags & DLM_LKF_PERSISTENT)
		set_bit(DLM_DFL_ORPHAN_BIT, &lkb->lkb_dflags);
	else
		set_bit(DLM_IFL_DEAD_BIT, &lkb->lkb_iflags);
 out:
	spin_unlock_bh(&ls->ls_clear_proc_locks);
	return lkb;
}

/* The ls_clear_proc_locks mutex protects against dlm_user_add_cb() which
   1) references lkb->ua which we free here and 2) adds lkbs to proc->asts,
   which we clear here. */

/* proc CLOSING flag is set so no more device_reads should look at proc->asts
   list, and no more device_writes should add lkb's to proc->locks list; so we
   shouldn't need to take asts_spin or locks_spin here.  this assumes that
   device reads/writes/closes are serialized -- FIXME: we may need to serialize
   them ourself. */

void dlm_clear_proc_locks(struct dlm_ls *ls, struct dlm_user_proc *proc)
{
	struct dlm_callback *cb, *cb_safe;
	struct dlm_lkb *lkb, *safe;

	dlm_lock_recovery(ls);

	while (1) {
		lkb = del_proc_lock(ls, proc);
		if (!lkb)
			break;
		if (lkb->lkb_exflags & DLM_LKF_PERSISTENT)
			orphan_proc_lock(ls, lkb);
		else
			unlock_proc_lock(ls, lkb);

		/* this removes the reference for the proc->locks list
		   added by dlm_user_request, it may result in the lkb
		   being freed */

		dlm_put_lkb(lkb);
	}

	spin_lock_bh(&ls->ls_clear_proc_locks);

	/* in-progress unlocks */
	list_for_each_entry_safe(lkb, safe, &proc->unlocking, lkb_ownqueue) {
		list_del_init(&lkb->lkb_ownqueue);
		set_bit(DLM_IFL_DEAD_BIT, &lkb->lkb_iflags);
		dlm_put_lkb(lkb);
	}

	list_for_each_entry_safe(cb, cb_safe, &proc->asts, list) {
		list_del(&cb->list);
		dlm_free_cb(cb);
	}

	spin_unlock_bh(&ls->ls_clear_proc_locks);
	dlm_unlock_recovery(ls);
}

static void purge_proc_locks(struct dlm_ls *ls, struct dlm_user_proc *proc)
{
	struct dlm_callback *cb, *cb_safe;
	struct dlm_lkb *lkb, *safe;

	while (1) {
		lkb = NULL;
		spin_lock_bh(&proc->locks_spin);
		if (!list_empty(&proc->locks)) {
			lkb = list_entry(proc->locks.next, struct dlm_lkb,
					 lkb_ownqueue);
			list_del_init(&lkb->lkb_ownqueue);
		}
		spin_unlock_bh(&proc->locks_spin);

		if (!lkb)
			break;

		set_bit(DLM_IFL_DEAD_BIT, &lkb->lkb_iflags);
		unlock_proc_lock(ls, lkb);
		dlm_put_lkb(lkb); /* ref from proc->locks list */
	}

	spin_lock_bh(&proc->locks_spin);
	list_for_each_entry_safe(lkb, safe, &proc->unlocking, lkb_ownqueue) {
		list_del_init(&lkb->lkb_ownqueue);
		set_bit(DLM_IFL_DEAD_BIT, &lkb->lkb_iflags);
		dlm_put_lkb(lkb);
	}
	spin_unlock_bh(&proc->locks_spin);

	spin_lock_bh(&proc->asts_spin);
	list_for_each_entry_safe(cb, cb_safe, &proc->asts, list) {
		list_del(&cb->list);
		dlm_free_cb(cb);
	}
	spin_unlock_bh(&proc->asts_spin);
}

/* pid of 0 means purge all orphans */

static void do_purge(struct dlm_ls *ls, int nodeid, int pid)
{
	struct dlm_lkb *lkb, *safe;

	spin_lock_bh(&ls->ls_orphans_lock);
	list_for_each_entry_safe(lkb, safe, &ls->ls_orphans, lkb_ownqueue) {
		if (pid && lkb->lkb_ownpid != pid)
			continue;
		unlock_proc_lock(ls, lkb);
		list_del_init(&lkb->lkb_ownqueue);
		dlm_put_lkb(lkb);
	}
	spin_unlock_bh(&ls->ls_orphans_lock);
}

static int send_purge(struct dlm_ls *ls, int nodeid, int pid)
{
	struct dlm_message *ms;
	struct dlm_mhandle *mh;
	int error;

	error = _create_message(ls, sizeof(struct dlm_message), nodeid,
				DLM_MSG_PURGE, &ms, &mh);
	if (error)
		return error;
	ms->m_nodeid = cpu_to_le32(nodeid);
	ms->m_pid = cpu_to_le32(pid);

	return send_message(mh, ms, NULL, 0);
}

int dlm_user_purge(struct dlm_ls *ls, struct dlm_user_proc *proc,
		   int nodeid, int pid)
{
	int error = 0;

	if (nodeid && (nodeid != dlm_our_nodeid())) {
		error = send_purge(ls, nodeid, pid);
	} else {
		dlm_lock_recovery(ls);
		if (pid == current->pid)
			purge_proc_locks(ls, proc);
		else
			do_purge(ls, nodeid, pid);
		dlm_unlock_recovery(ls);
	}
	return error;
}

/* debug functionality */
int dlm_debug_add_lkb(struct dlm_ls *ls, uint32_t lkb_id, char *name, int len,
		      int lkb_nodeid, unsigned int lkb_dflags, int lkb_status)
{
	struct dlm_lksb *lksb;
	struct dlm_lkb *lkb;
	struct dlm_rsb *r;
	int error;

	/* we currently can't set a valid user lock */
	if (lkb_dflags & BIT(DLM_DFL_USER_BIT))
		return -EOPNOTSUPP;

	lksb = kzalloc(sizeof(*lksb), GFP_NOFS);
	if (!lksb)
		return -ENOMEM;

	error = _create_lkb(ls, &lkb, lkb_id, lkb_id + 1);
	if (error) {
		kfree(lksb);
		return error;
	}

	dlm_set_dflags_val(lkb, lkb_dflags);
	lkb->lkb_nodeid = lkb_nodeid;
	lkb->lkb_lksb = lksb;
	/* user specific pointer, just don't have it NULL for kernel locks */
	if (~lkb_dflags & BIT(DLM_DFL_USER_BIT))
		lkb->lkb_astparam = (void *)0xDEADBEEF;

	error = find_rsb(ls, name, len, 0, R_REQUEST, &r);
	if (error) {
		kfree(lksb);
		__put_lkb(ls, lkb);
		return error;
	}

	lock_rsb(r);
	attach_lkb(r, lkb);
	add_lkb(r, lkb, lkb_status);
	unlock_rsb(r);
	put_rsb(r);

	return 0;
}

int dlm_debug_add_lkb_to_waiters(struct dlm_ls *ls, uint32_t lkb_id,
				 int mstype, int to_nodeid)
{
	struct dlm_lkb *lkb;
	int error;

	error = find_lkb(ls, lkb_id, &lkb);
	if (error)
		return error;

	error = add_to_waiters(lkb, mstype, to_nodeid);
	dlm_put_lkb(lkb);
	return error;
}

