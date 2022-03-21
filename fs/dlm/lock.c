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
#include <linux/types.h>
#include <linux/rbtree.h>
#include <linux/slab.h>
#include "dlm_internal.h"
#include <linux/dlm_device.h>
#include "memory.h"
#include "lowcomms.h"
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
				    struct dlm_message *ms);
static int receive_extralen(struct dlm_message *ms);
static void do_purge(struct dlm_ls *ls, int nodeid, int pid);
static void del_timeout(struct dlm_lkb *lkb);
static void toss_rsb(struct kref *kref);

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
	       lkb->lkb_flags, lkb->lkb_status, lkb->lkb_rqmode,
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

static inline void dlm_lock_recovery(struct dlm_ls *ls)
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
	return (lkb->lkb_sbflags & DLM_SBF_DEMOTED);
}

static inline int is_altmode(struct dlm_lkb *lkb)
{
	return (lkb->lkb_sbflags & DLM_SBF_ALTMODE);
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
	return (lkb->lkb_nodeid && !(lkb->lkb_flags & DLM_IFL_MSTCPY));
}

static inline int is_master_copy(struct dlm_lkb *lkb)
{
	return (lkb->lkb_flags & DLM_IFL_MSTCPY) ? 1 : 0;
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
	return lkb->lkb_flags & DLM_IFL_OVERLAP_UNLOCK;
}

static inline int is_overlap_cancel(struct dlm_lkb *lkb)
{
	return lkb->lkb_flags & DLM_IFL_OVERLAP_CANCEL;
}

static inline int is_overlap(struct dlm_lkb *lkb)
{
	return (lkb->lkb_flags & (DLM_IFL_OVERLAP_UNLOCK |
				  DLM_IFL_OVERLAP_CANCEL));
}

static void queue_cast(struct dlm_rsb *r, struct dlm_lkb *lkb, int rv)
{
	if (is_master_copy(lkb))
		return;

	del_timeout(lkb);

	DLM_ASSERT(lkb->lkb_lksb, dlm_print_lkb(lkb););

	/* if the operation was a cancel, then return -DLM_ECANCEL, if a
	   timeout caused the cancel then return -ETIMEDOUT */
	if (rv == -DLM_ECANCEL && (lkb->lkb_flags & DLM_IFL_TIMEOUT_CANCEL)) {
		lkb->lkb_flags &= ~DLM_IFL_TIMEOUT_CANCEL;
		rv = -ETIMEDOUT;
	}

	if (rv == -DLM_ECANCEL && (lkb->lkb_flags & DLM_IFL_DEADLOCK_CANCEL)) {
		lkb->lkb_flags &= ~DLM_IFL_DEADLOCK_CANCEL;
		rv = -EDEADLK;
	}

	dlm_add_cb(lkb, DLM_CB_CAST, lkb->lkb_grmode, rv, lkb->lkb_sbflags);
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

/* This is only called to add a reference when the code already holds
   a valid reference to the rsb, so there's no need for locking. */

static inline void hold_rsb(struct dlm_rsb *r)
{
	kref_get(&r->res_ref);
}

void dlm_hold_rsb(struct dlm_rsb *r)
{
	hold_rsb(r);
}

/* When all references to the rsb are gone it's transferred to
   the tossed list for later disposal. */

static void put_rsb(struct dlm_rsb *r)
{
	struct dlm_ls *ls = r->res_ls;
	uint32_t bucket = r->res_bucket;

	spin_lock(&ls->ls_rsbtbl[bucket].lock);
	kref_put(&r->res_ref, toss_rsb);
	spin_unlock(&ls->ls_rsbtbl[bucket].lock);
}

void dlm_put_rsb(struct dlm_rsb *r)
{
	put_rsb(r);
}

static int pre_rsb_struct(struct dlm_ls *ls)
{
	struct dlm_rsb *r1, *r2;
	int count = 0;

	spin_lock(&ls->ls_new_rsb_spin);
	if (ls->ls_new_rsb_count > dlm_config.ci_new_rsb_count / 2) {
		spin_unlock(&ls->ls_new_rsb_spin);
		return 0;
	}
	spin_unlock(&ls->ls_new_rsb_spin);

	r1 = dlm_allocate_rsb(ls);
	r2 = dlm_allocate_rsb(ls);

	spin_lock(&ls->ls_new_rsb_spin);
	if (r1) {
		list_add(&r1->res_hashchain, &ls->ls_new_rsb);
		ls->ls_new_rsb_count++;
	}
	if (r2) {
		list_add(&r2->res_hashchain, &ls->ls_new_rsb);
		ls->ls_new_rsb_count++;
	}
	count = ls->ls_new_rsb_count;
	spin_unlock(&ls->ls_new_rsb_spin);

	if (!count)
		return -ENOMEM;
	return 0;
}

/* If ls->ls_new_rsb is empty, return -EAGAIN, so the caller can
   unlock any spinlocks, go back and call pre_rsb_struct again.
   Otherwise, take an rsb off the list and return it. */

static int get_rsb_struct(struct dlm_ls *ls, char *name, int len,
			  struct dlm_rsb **r_ret)
{
	struct dlm_rsb *r;
	int count;

	spin_lock(&ls->ls_new_rsb_spin);
	if (list_empty(&ls->ls_new_rsb)) {
		count = ls->ls_new_rsb_count;
		spin_unlock(&ls->ls_new_rsb_spin);
		log_debug(ls, "find_rsb retry %d %d %s",
			  count, dlm_config.ci_new_rsb_count, name);
		return -EAGAIN;
	}

	r = list_first_entry(&ls->ls_new_rsb, struct dlm_rsb, res_hashchain);
	list_del(&r->res_hashchain);
	/* Convert the empty list_head to a NULL rb_node for tree usage: */
	memset(&r->res_hashnode, 0, sizeof(struct rb_node));
	ls->ls_new_rsb_count--;
	spin_unlock(&ls->ls_new_rsb_spin);

	r->res_ls = ls;
	r->res_length = len;
	memcpy(r->res_name, name, len);
	mutex_init(&r->res_mutex);

	INIT_LIST_HEAD(&r->res_lookup);
	INIT_LIST_HEAD(&r->res_grantqueue);
	INIT_LIST_HEAD(&r->res_convertqueue);
	INIT_LIST_HEAD(&r->res_waitqueue);
	INIT_LIST_HEAD(&r->res_root_list);
	INIT_LIST_HEAD(&r->res_recover_list);

	*r_ret = r;
	return 0;
}

static int rsb_cmp(struct dlm_rsb *r, const char *name, int nlen)
{
	char maxname[DLM_RESNAME_MAXLEN];

	memset(maxname, 0, DLM_RESNAME_MAXLEN);
	memcpy(maxname, name, nlen);
	return memcmp(r->res_name, maxname, DLM_RESNAME_MAXLEN);
}

int dlm_search_rsb_tree(struct rb_root *tree, char *name, int len,
			struct dlm_rsb **r_ret)
{
	struct rb_node *node = tree->rb_node;
	struct dlm_rsb *r;
	int rc;

	while (node) {
		r = rb_entry(node, struct dlm_rsb, res_hashnode);
		rc = rsb_cmp(r, name, len);
		if (rc < 0)
			node = node->rb_left;
		else if (rc > 0)
			node = node->rb_right;
		else
			goto found;
	}
	*r_ret = NULL;
	return -EBADR;

 found:
	*r_ret = r;
	return 0;
}

static int rsb_insert(struct dlm_rsb *rsb, struct rb_root *tree)
{
	struct rb_node **newn = &tree->rb_node;
	struct rb_node *parent = NULL;
	int rc;

	while (*newn) {
		struct dlm_rsb *cur = rb_entry(*newn, struct dlm_rsb,
					       res_hashnode);

		parent = *newn;
		rc = rsb_cmp(cur, rsb->res_name, rsb->res_length);
		if (rc < 0)
			newn = &parent->rb_left;
		else if (rc > 0)
			newn = &parent->rb_right;
		else {
			log_print("rsb_insert match");
			dlm_dump_rsb(rsb);
			dlm_dump_rsb(cur);
			return -EEXIST;
		}
	}

	rb_link_node(&rsb->res_hashnode, parent, newn);
	rb_insert_color(&rsb->res_hashnode, tree);
	return 0;
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
 * toss_rsb() happens when all local usage of the rsb is done, i.e. no
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

static int find_rsb_dir(struct dlm_ls *ls, char *name, int len,
			uint32_t hash, uint32_t b,
			int dir_nodeid, int from_nodeid,
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
	 * someone sends us a request after we have removed/freed an rsb
	 * from our toss list.  (They sent a request instead of lookup
	 * because they are using an rsb from their toss list.)
	 */

	if (from_local || from_dir ||
	    (from_other && (dir_nodeid == our_nodeid))) {
		create = 1;
	}

 retry:
	if (create) {
		error = pre_rsb_struct(ls);
		if (error < 0)
			goto out;
	}

	spin_lock(&ls->ls_rsbtbl[b].lock);

	error = dlm_search_rsb_tree(&ls->ls_rsbtbl[b].keep, name, len, &r);
	if (error)
		goto do_toss;
	
	/*
	 * rsb is active, so we can't check master_nodeid without lock_rsb.
	 */

	kref_get(&r->res_ref);
	error = 0;
	goto out_unlock;


 do_toss:
	error = dlm_search_rsb_tree(&ls->ls_rsbtbl[b].toss, name, len, &r);
	if (error)
		goto do_new;

	/*
	 * rsb found inactive (master_nodeid may be out of date unless
	 * we are the dir_nodeid or were the master)  No other thread
	 * is using this rsb because it's on the toss list, so we can
	 * look at or update res_master_nodeid without lock_rsb.
	 */

	if ((r->res_master_nodeid != our_nodeid) && from_other) {
		/* our rsb was not master, and another node (not the dir node)
		   has sent us a request */
		log_debug(ls, "find_rsb toss from_other %d master %d dir %d %s",
			  from_nodeid, r->res_master_nodeid, dir_nodeid,
			  r->res_name);
		error = -ENOTBLK;
		goto out_unlock;
	}

	if ((r->res_master_nodeid != our_nodeid) && from_dir) {
		/* don't think this should ever happen */
		log_error(ls, "find_rsb toss from_dir %d master %d",
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

	rb_erase(&r->res_hashnode, &ls->ls_rsbtbl[b].toss);
	error = rsb_insert(r, &ls->ls_rsbtbl[b].keep);
	goto out_unlock;


 do_new:
	/*
	 * rsb not found
	 */

	if (error == -EBADR && !create)
		goto out_unlock;

	error = get_rsb_struct(ls, name, len, &r);
	if (error == -EAGAIN) {
		spin_unlock(&ls->ls_rsbtbl[b].lock);
		goto retry;
	}
	if (error)
		goto out_unlock;

	r->res_hash = hash;
	r->res_bucket = b;
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
		goto out_unlock;
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
	error = rsb_insert(r, &ls->ls_rsbtbl[b].keep);
 out_unlock:
	spin_unlock(&ls->ls_rsbtbl[b].lock);
 out:
	*r_ret = r;
	return error;
}

/* During recovery, other nodes can send us new MSTCPY locks (from
   dlm_recover_locks) before we've made ourself master (in
   dlm_recover_masters). */

static int find_rsb_nodir(struct dlm_ls *ls, char *name, int len,
			  uint32_t hash, uint32_t b,
			  int dir_nodeid, int from_nodeid,
			  unsigned int flags, struct dlm_rsb **r_ret)
{
	struct dlm_rsb *r = NULL;
	int our_nodeid = dlm_our_nodeid();
	int recover = (flags & R_RECEIVE_RECOVER);
	int error;

 retry:
	error = pre_rsb_struct(ls);
	if (error < 0)
		goto out;

	spin_lock(&ls->ls_rsbtbl[b].lock);

	error = dlm_search_rsb_tree(&ls->ls_rsbtbl[b].keep, name, len, &r);
	if (error)
		goto do_toss;

	/*
	 * rsb is active, so we can't check master_nodeid without lock_rsb.
	 */

	kref_get(&r->res_ref);
	goto out_unlock;


 do_toss:
	error = dlm_search_rsb_tree(&ls->ls_rsbtbl[b].toss, name, len, &r);
	if (error)
		goto do_new;

	/*
	 * rsb found inactive. No other thread is using this rsb because
	 * it's on the toss list, so we can look at or update
	 * res_master_nodeid without lock_rsb.
	 */

	if (!recover && (r->res_master_nodeid != our_nodeid) && from_nodeid) {
		/* our rsb is not master, and another node has sent us a
		   request; this should never happen */
		log_error(ls, "find_rsb toss from_nodeid %d master %d dir %d",
			  from_nodeid, r->res_master_nodeid, dir_nodeid);
		dlm_print_rsb(r);
		error = -ENOTBLK;
		goto out_unlock;
	}

	if (!recover && (r->res_master_nodeid != our_nodeid) &&
	    (dir_nodeid == our_nodeid)) {
		/* our rsb is not master, and we are dir; may as well fix it;
		   this should never happen */
		log_error(ls, "find_rsb toss our %d master %d dir %d",
			  our_nodeid, r->res_master_nodeid, dir_nodeid);
		dlm_print_rsb(r);
		r->res_master_nodeid = our_nodeid;
		r->res_nodeid = 0;
	}

	rb_erase(&r->res_hashnode, &ls->ls_rsbtbl[b].toss);
	error = rsb_insert(r, &ls->ls_rsbtbl[b].keep);
	goto out_unlock;


 do_new:
	/*
	 * rsb not found
	 */

	error = get_rsb_struct(ls, name, len, &r);
	if (error == -EAGAIN) {
		spin_unlock(&ls->ls_rsbtbl[b].lock);
		goto retry;
	}
	if (error)
		goto out_unlock;

	r->res_hash = hash;
	r->res_bucket = b;
	r->res_dir_nodeid = dir_nodeid;
	r->res_master_nodeid = dir_nodeid;
	r->res_nodeid = (dir_nodeid == our_nodeid) ? 0 : dir_nodeid;
	kref_init(&r->res_ref);

	error = rsb_insert(r, &ls->ls_rsbtbl[b].keep);
 out_unlock:
	spin_unlock(&ls->ls_rsbtbl[b].lock);
 out:
	*r_ret = r;
	return error;
}

static int find_rsb(struct dlm_ls *ls, char *name, int len, int from_nodeid,
		    unsigned int flags, struct dlm_rsb **r_ret)
{
	uint32_t hash, b;
	int dir_nodeid;

	if (len > DLM_RESNAME_MAXLEN)
		return -EINVAL;

	hash = jhash(name, len, 0);
	b = hash & (ls->ls_rsbtbl_size - 1);

	dir_nodeid = dlm_hash2nodeid(ls, hash);

	if (dlm_no_directory(ls))
		return find_rsb_nodir(ls, name, len, hash, b, dir_nodeid,
				      from_nodeid, flags, r_ret);
	else
		return find_rsb_dir(ls, name, len, hash, b, dir_nodeid,
				      from_nodeid, flags, r_ret);
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

int dlm_master_lookup(struct dlm_ls *ls, int from_nodeid, char *name, int len,
		      unsigned int flags, int *r_nodeid, int *result)
{
	struct dlm_rsb *r = NULL;
	uint32_t hash, b;
	int from_master = (flags & DLM_LU_RECOVER_DIR);
	int fix_master = (flags & DLM_LU_RECOVER_MASTER);
	int our_nodeid = dlm_our_nodeid();
	int dir_nodeid, error, toss_list = 0;

	if (len > DLM_RESNAME_MAXLEN)
		return -EINVAL;

	if (from_nodeid == our_nodeid) {
		log_error(ls, "dlm_master_lookup from our_nodeid %d flags %x",
			  our_nodeid, flags);
		return -EINVAL;
	}

	hash = jhash(name, len, 0);
	b = hash & (ls->ls_rsbtbl_size - 1);

	dir_nodeid = dlm_hash2nodeid(ls, hash);
	if (dir_nodeid != our_nodeid) {
		log_error(ls, "dlm_master_lookup from %d dir %d our %d h %x %d",
			  from_nodeid, dir_nodeid, our_nodeid, hash,
			  ls->ls_num_nodes);
		*r_nodeid = -1;
		return -EINVAL;
	}

 retry:
	error = pre_rsb_struct(ls);
	if (error < 0)
		return error;

	spin_lock(&ls->ls_rsbtbl[b].lock);
	error = dlm_search_rsb_tree(&ls->ls_rsbtbl[b].keep, name, len, &r);
	if (!error) {
		/* because the rsb is active, we need to lock_rsb before
		   checking/changing re_master_nodeid */

		hold_rsb(r);
		spin_unlock(&ls->ls_rsbtbl[b].lock);
		lock_rsb(r);
		goto found;
	}

	error = dlm_search_rsb_tree(&ls->ls_rsbtbl[b].toss, name, len, &r);
	if (error)
		goto not_found;

	/* because the rsb is inactive (on toss list), it's not refcounted
	   and lock_rsb is not used, but is protected by the rsbtbl lock */

	toss_list = 1;
 found:
	if (r->res_dir_nodeid != our_nodeid) {
		/* should not happen, but may as well fix it and carry on */
		log_error(ls, "dlm_master_lookup res_dir %d our %d %s",
			  r->res_dir_nodeid, our_nodeid, r->res_name);
		r->res_dir_nodeid = our_nodeid;
	}

	if (fix_master && dlm_is_removed(ls, r->res_master_nodeid)) {
		/* Recovery uses this function to set a new master when
		   the previous master failed.  Setting NEW_MASTER will
		   force dlm_recover_masters to call recover_master on this
		   rsb even though the res_nodeid is no longer removed. */

		r->res_master_nodeid = from_nodeid;
		r->res_nodeid = from_nodeid;
		rsb_set_flag(r, RSB_NEW_MASTER);

		if (toss_list) {
			/* I don't think we should ever find it on toss list. */
			log_error(ls, "dlm_master_lookup fix_master on toss");
			dlm_dump_rsb(r);
		}
	}

	if (from_master && (r->res_master_nodeid != from_nodeid)) {
		/* this will happen if from_nodeid became master during
		   a previous recovery cycle, and we aborted the previous
		   cycle before recovering this master value */

		log_limit(ls, "dlm_master_lookup from_master %d "
			  "master_nodeid %d res_nodeid %d first %x %s",
			  from_nodeid, r->res_master_nodeid, r->res_nodeid,
			  r->res_first_lkid, r->res_name);

		if (r->res_master_nodeid == our_nodeid) {
			log_error(ls, "from_master %d our_master", from_nodeid);
			dlm_dump_rsb(r);
			goto out_found;
		}

		r->res_master_nodeid = from_nodeid;
		r->res_nodeid = from_nodeid;
		rsb_set_flag(r, RSB_NEW_MASTER);
	}

	if (!r->res_master_nodeid) {
		/* this will happen if recovery happens while we're looking
		   up the master for this rsb */

		log_debug(ls, "dlm_master_lookup master 0 to %d first %x %s",
			  from_nodeid, r->res_first_lkid, r->res_name);
		r->res_master_nodeid = from_nodeid;
		r->res_nodeid = from_nodeid;
	}

	if (!from_master && !fix_master &&
	    (r->res_master_nodeid == from_nodeid)) {
		/* this can happen when the master sends remove, the dir node
		   finds the rsb on the keep list and ignores the remove,
		   and the former master sends a lookup */

		log_limit(ls, "dlm_master_lookup from master %d flags %x "
			  "first %x %s", from_nodeid, flags,
			  r->res_first_lkid, r->res_name);
	}

 out_found:
	*r_nodeid = r->res_master_nodeid;
	if (result)
		*result = DLM_LU_MATCH;

	if (toss_list) {
		r->res_toss_time = jiffies;
		/* the rsb was inactive (on toss list) */
		spin_unlock(&ls->ls_rsbtbl[b].lock);
	} else {
		/* the rsb was active */
		unlock_rsb(r);
		put_rsb(r);
	}
	return 0;

 not_found:
	error = get_rsb_struct(ls, name, len, &r);
	if (error == -EAGAIN) {
		spin_unlock(&ls->ls_rsbtbl[b].lock);
		goto retry;
	}
	if (error)
		goto out_unlock;

	r->res_hash = hash;
	r->res_bucket = b;
	r->res_dir_nodeid = our_nodeid;
	r->res_master_nodeid = from_nodeid;
	r->res_nodeid = from_nodeid;
	kref_init(&r->res_ref);
	r->res_toss_time = jiffies;

	error = rsb_insert(r, &ls->ls_rsbtbl[b].toss);
	if (error) {
		/* should never happen */
		dlm_free_rsb(r);
		spin_unlock(&ls->ls_rsbtbl[b].lock);
		goto retry;
	}

	if (result)
		*result = DLM_LU_ADD;
	*r_nodeid = from_nodeid;
	error = 0;
 out_unlock:
	spin_unlock(&ls->ls_rsbtbl[b].lock);
	return error;
}

static void dlm_dump_rsb_hash(struct dlm_ls *ls, uint32_t hash)
{
	struct rb_node *n;
	struct dlm_rsb *r;
	int i;

	for (i = 0; i < ls->ls_rsbtbl_size; i++) {
		spin_lock(&ls->ls_rsbtbl[i].lock);
		for (n = rb_first(&ls->ls_rsbtbl[i].keep); n; n = rb_next(n)) {
			r = rb_entry(n, struct dlm_rsb, res_hashnode);
			if (r->res_hash == hash)
				dlm_dump_rsb(r);
		}
		spin_unlock(&ls->ls_rsbtbl[i].lock);
	}
}

void dlm_dump_rsb_name(struct dlm_ls *ls, char *name, int len)
{
	struct dlm_rsb *r = NULL;
	uint32_t hash, b;
	int error;

	hash = jhash(name, len, 0);
	b = hash & (ls->ls_rsbtbl_size - 1);

	spin_lock(&ls->ls_rsbtbl[b].lock);
	error = dlm_search_rsb_tree(&ls->ls_rsbtbl[b].keep, name, len, &r);
	if (!error)
		goto out_dump;

	error = dlm_search_rsb_tree(&ls->ls_rsbtbl[b].toss, name, len, &r);
	if (error)
		goto out;
 out_dump:
	dlm_dump_rsb(r);
 out:
	spin_unlock(&ls->ls_rsbtbl[b].lock);
}

static void toss_rsb(struct kref *kref)
{
	struct dlm_rsb *r = container_of(kref, struct dlm_rsb, res_ref);
	struct dlm_ls *ls = r->res_ls;

	DLM_ASSERT(list_empty(&r->res_root_list), dlm_print_rsb(r););
	kref_init(&r->res_ref);
	rb_erase(&r->res_hashnode, &ls->ls_rsbtbl[r->res_bucket].keep);
	rsb_insert(r, &ls->ls_rsbtbl[r->res_bucket].toss);
	r->res_toss_time = jiffies;
	ls->ls_rsbtbl[r->res_bucket].flags |= DLM_RTF_SHRINK;
	if (r->res_lvbptr) {
		dlm_free_lvb(r->res_lvbptr);
		r->res_lvbptr = NULL;
	}
}

/* See comment for unhold_lkb */

static void unhold_rsb(struct dlm_rsb *r)
{
	int rv;
	rv = kref_put(&r->res_ref, toss_rsb);
	DLM_ASSERT(!rv, dlm_dump_rsb(r););
}

static void kill_rsb(struct kref *kref)
{
	struct dlm_rsb *r = container_of(kref, struct dlm_rsb, res_ref);

	/* All work is done after the return from kref_put() so we
	   can release the write_lock before the remove and free. */

	DLM_ASSERT(list_empty(&r->res_lookup), dlm_dump_rsb(r););
	DLM_ASSERT(list_empty(&r->res_grantqueue), dlm_dump_rsb(r););
	DLM_ASSERT(list_empty(&r->res_convertqueue), dlm_dump_rsb(r););
	DLM_ASSERT(list_empty(&r->res_waitqueue), dlm_dump_rsb(r););
	DLM_ASSERT(list_empty(&r->res_root_list), dlm_dump_rsb(r););
	DLM_ASSERT(list_empty(&r->res_recover_list), dlm_dump_rsb(r););
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

static int create_lkb(struct dlm_ls *ls, struct dlm_lkb **lkb_ret)
{
	struct dlm_lkb *lkb;
	int rv;

	lkb = dlm_allocate_lkb(ls);
	if (!lkb)
		return -ENOMEM;

	lkb->lkb_nodeid = -1;
	lkb->lkb_grmode = DLM_LOCK_IV;
	kref_init(&lkb->lkb_ref);
	INIT_LIST_HEAD(&lkb->lkb_ownqueue);
	INIT_LIST_HEAD(&lkb->lkb_rsb_lookup);
	INIT_LIST_HEAD(&lkb->lkb_time_list);
	INIT_LIST_HEAD(&lkb->lkb_cb_list);
	mutex_init(&lkb->lkb_cb_mutex);
	INIT_WORK(&lkb->lkb_cb_work, dlm_callback_work);

	idr_preload(GFP_NOFS);
	spin_lock(&ls->ls_lkbidr_spin);
	rv = idr_alloc(&ls->ls_lkbidr, lkb, 1, 0, GFP_NOWAIT);
	if (rv >= 0)
		lkb->lkb_id = rv;
	spin_unlock(&ls->ls_lkbidr_spin);
	idr_preload_end();

	if (rv < 0) {
		log_error(ls, "create_lkb idr error %d", rv);
		dlm_free_lkb(lkb);
		return rv;
	}

	*lkb_ret = lkb;
	return 0;
}

static int find_lkb(struct dlm_ls *ls, uint32_t lkid, struct dlm_lkb **lkb_ret)
{
	struct dlm_lkb *lkb;

	spin_lock(&ls->ls_lkbidr_spin);
	lkb = idr_find(&ls->ls_lkbidr, lkid);
	if (lkb)
		kref_get(&lkb->lkb_ref);
	spin_unlock(&ls->ls_lkbidr_spin);

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

	spin_lock(&ls->ls_lkbidr_spin);
	if (kref_put(&lkb->lkb_ref, kill_lkb)) {
		idr_remove(&ls->ls_lkbidr, lkid);
		spin_unlock(&ls->ls_lkbidr_spin);

		detach_lkb(lkb);

		/* for local/process lkbs, lvbptr points to caller's lksb */
		if (lkb->lkb_lvbptr && is_master_copy(lkb))
			dlm_free_lvb(lkb->lkb_lvbptr);
		dlm_free_lkb(lkb);
		return 1;
	} else {
		spin_unlock(&ls->ls_lkbidr_spin);
		return 0;
	}
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

/* This is called when we need to remove a reference and are certain
   it's not the last ref.  e.g. del_lkb is always called between a
   find_lkb/put_lkb and is always the inverse of a previous add_lkb.
   put_lkb would work fine, but would involve unnecessary locking */

static inline void unhold_lkb(struct dlm_lkb *lkb)
{
	int rv;
	rv = kref_put(&lkb->lkb_ref, kill_lkb);
	DLM_ASSERT(!rv, dlm_print_lkb(lkb););
}

static void lkb_add_ordered(struct list_head *new, struct list_head *head,
			    int mode)
{
	struct dlm_lkb *lkb = NULL;

	list_for_each_entry(lkb, head, lkb_statequeue)
		if (lkb->lkb_rqmode < mode)
			break;

	__list_add(new, lkb->lkb_statequeue.prev, &lkb->lkb_statequeue);
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
	hold_lkb(lkb);
	del_lkb(r, lkb);
	add_lkb(r, lkb, sts);
	unhold_lkb(lkb);
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

static int nodeid_warned(int nodeid, int num_nodes, int *warned)
{
	int i;

	for (i = 0; i < num_nodes; i++) {
		if (!warned[i]) {
			warned[i] = nodeid;
			return 0;
		}
		if (warned[i] == nodeid)
			return 1;
	}
	return 0;
}

void dlm_scan_waiters(struct dlm_ls *ls)
{
	struct dlm_lkb *lkb;
	s64 us;
	s64 debug_maxus = 0;
	u32 debug_scanned = 0;
	u32 debug_expired = 0;
	int num_nodes = 0;
	int *warned = NULL;

	if (!dlm_config.ci_waitwarn_us)
		return;

	mutex_lock(&ls->ls_waiters_mutex);

	list_for_each_entry(lkb, &ls->ls_waiters, lkb_wait_reply) {
		if (!lkb->lkb_wait_time)
			continue;

		debug_scanned++;

		us = ktime_to_us(ktime_sub(ktime_get(), lkb->lkb_wait_time));

		if (us < dlm_config.ci_waitwarn_us)
			continue;

		lkb->lkb_wait_time = 0;

		debug_expired++;
		if (us > debug_maxus)
			debug_maxus = us;

		if (!num_nodes) {
			num_nodes = ls->ls_num_nodes;
			warned = kcalloc(num_nodes, sizeof(int), GFP_KERNEL);
		}
		if (!warned)
			continue;
		if (nodeid_warned(lkb->lkb_wait_nodeid, num_nodes, warned))
			continue;

		log_error(ls, "waitwarn %x %lld %d us check connection to "
			  "node %d", lkb->lkb_id, (long long)us,
			  dlm_config.ci_waitwarn_us, lkb->lkb_wait_nodeid);
	}
	mutex_unlock(&ls->ls_waiters_mutex);
	kfree(warned);

	if (debug_expired)
		log_debug(ls, "scan_waiters %u warn %u over %d us max %lld us",
			  debug_scanned, debug_expired,
			  dlm_config.ci_waitwarn_us, (long long)debug_maxus);
}

/* add/remove lkb from global waiters list of lkb's waiting for
   a reply from a remote node */

static int add_to_waiters(struct dlm_lkb *lkb, int mstype, int to_nodeid)
{
	struct dlm_ls *ls = lkb->lkb_resource->res_ls;
	int error = 0;

	mutex_lock(&ls->ls_waiters_mutex);

	if (is_overlap_unlock(lkb) ||
	    (is_overlap_cancel(lkb) && (mstype == DLM_MSG_CANCEL))) {
		error = -EINVAL;
		goto out;
	}

	if (lkb->lkb_wait_type || is_overlap_cancel(lkb)) {
		switch (mstype) {
		case DLM_MSG_UNLOCK:
			lkb->lkb_flags |= DLM_IFL_OVERLAP_UNLOCK;
			break;
		case DLM_MSG_CANCEL:
			lkb->lkb_flags |= DLM_IFL_OVERLAP_CANCEL;
			break;
		default:
			error = -EBUSY;
			goto out;
		}
		lkb->lkb_wait_count++;
		hold_lkb(lkb);

		log_debug(ls, "addwait %x cur %d overlap %d count %d f %x",
			  lkb->lkb_id, lkb->lkb_wait_type, mstype,
			  lkb->lkb_wait_count, lkb->lkb_flags);
		goto out;
	}

	DLM_ASSERT(!lkb->lkb_wait_count,
		   dlm_print_lkb(lkb);
		   printk("wait_count %d\n", lkb->lkb_wait_count););

	lkb->lkb_wait_count++;
	lkb->lkb_wait_type = mstype;
	lkb->lkb_wait_time = ktime_get();
	lkb->lkb_wait_nodeid = to_nodeid; /* for debugging */
	hold_lkb(lkb);
	list_add(&lkb->lkb_wait_reply, &ls->ls_waiters);
 out:
	if (error)
		log_error(ls, "addwait error %x %d flags %x %d %d %s",
			  lkb->lkb_id, error, lkb->lkb_flags, mstype,
			  lkb->lkb_wait_type, lkb->lkb_resource->res_name);
	mutex_unlock(&ls->ls_waiters_mutex);
	return error;
}

/* We clear the RESEND flag because we might be taking an lkb off the waiters
   list as part of process_requestqueue (e.g. a lookup that has an optimized
   request reply on the requestqueue) between dlm_recover_waiters_pre() which
   set RESEND and dlm_recover_waiters_post() */

static int _remove_from_waiters(struct dlm_lkb *lkb, int mstype,
				struct dlm_message *ms)
{
	struct dlm_ls *ls = lkb->lkb_resource->res_ls;
	int overlap_done = 0;

	if (is_overlap_unlock(lkb) && (mstype == DLM_MSG_UNLOCK_REPLY)) {
		log_debug(ls, "remwait %x unlock_reply overlap", lkb->lkb_id);
		lkb->lkb_flags &= ~DLM_IFL_OVERLAP_UNLOCK;
		overlap_done = 1;
		goto out_del;
	}

	if (is_overlap_cancel(lkb) && (mstype == DLM_MSG_CANCEL_REPLY)) {
		log_debug(ls, "remwait %x cancel_reply overlap", lkb->lkb_id);
		lkb->lkb_flags &= ~DLM_IFL_OVERLAP_CANCEL;
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
	    (lkb->lkb_wait_type == DLM_MSG_CONVERT) &&
	    is_overlap_cancel(lkb) && ms && !ms->m_result) {
		log_debug(ls, "remwait %x convert_reply zap overlap_cancel",
			  lkb->lkb_id);
		lkb->lkb_wait_type = 0;
		lkb->lkb_flags &= ~DLM_IFL_OVERLAP_CANCEL;
		lkb->lkb_wait_count--;
		goto out_del;
	}

	/* N.B. type of reply may not always correspond to type of original
	   msg due to lookup->request optimization, verify others? */

	if (lkb->lkb_wait_type) {
		lkb->lkb_wait_type = 0;
		goto out_del;
	}

	log_error(ls, "remwait error %x remote %d %x msg %d flags %x no wait",
		  lkb->lkb_id, ms ? ms->m_header.h_nodeid : 0, lkb->lkb_remid,
		  mstype, lkb->lkb_flags);
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
		lkb->lkb_wait_type = 0;
	}

	DLM_ASSERT(lkb->lkb_wait_count, dlm_print_lkb(lkb););

	lkb->lkb_flags &= ~DLM_IFL_RESEND;
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

	mutex_lock(&ls->ls_waiters_mutex);
	error = _remove_from_waiters(lkb, mstype, NULL);
	mutex_unlock(&ls->ls_waiters_mutex);
	return error;
}

/* Handles situations where we might be processing a "fake" or "stub" reply in
   which we can't try to take waiters_mutex again. */

static int remove_from_waiters_ms(struct dlm_lkb *lkb, struct dlm_message *ms)
{
	struct dlm_ls *ls = lkb->lkb_resource->res_ls;
	int error;

	if (ms->m_flags != DLM_IFL_STUB_MS)
		mutex_lock(&ls->ls_waiters_mutex);
	error = _remove_from_waiters(lkb, ms->m_type, ms);
	if (ms->m_flags != DLM_IFL_STUB_MS)
		mutex_unlock(&ls->ls_waiters_mutex);
	return error;
}

/* If there's an rsb for the same resource being removed, ensure
   that the remove message is sent before the new lookup message.
   It should be rare to need a delay here, but if not, then it may
   be worthwhile to add a proper wait mechanism rather than a delay. */

static void wait_pending_remove(struct dlm_rsb *r)
{
	struct dlm_ls *ls = r->res_ls;
 restart:
	spin_lock(&ls->ls_remove_spin);
	if (ls->ls_remove_len &&
	    !rsb_cmp(r, ls->ls_remove_name, ls->ls_remove_len)) {
		log_debug(ls, "delay lookup for remove dir %d %s",
		  	  r->res_dir_nodeid, r->res_name);
		spin_unlock(&ls->ls_remove_spin);
		msleep(1);
		goto restart;
	}
	spin_unlock(&ls->ls_remove_spin);
}

/*
 * ls_remove_spin protects ls_remove_name and ls_remove_len which are
 * read by other threads in wait_pending_remove.  ls_remove_names
 * and ls_remove_lens are only used by the scan thread, so they do
 * not need protection.
 */

static void shrink_bucket(struct dlm_ls *ls, int b)
{
	struct rb_node *n, *next;
	struct dlm_rsb *r;
	char *name;
	int our_nodeid = dlm_our_nodeid();
	int remote_count = 0;
	int need_shrink = 0;
	int i, len, rv;

	memset(&ls->ls_remove_lens, 0, sizeof(int) * DLM_REMOVE_NAMES_MAX);

	spin_lock(&ls->ls_rsbtbl[b].lock);

	if (!(ls->ls_rsbtbl[b].flags & DLM_RTF_SHRINK)) {
		spin_unlock(&ls->ls_rsbtbl[b].lock);
		return;
	}

	for (n = rb_first(&ls->ls_rsbtbl[b].toss); n; n = next) {
		next = rb_next(n);
		r = rb_entry(n, struct dlm_rsb, res_hashnode);

		/* If we're the directory record for this rsb, and
		   we're not the master of it, then we need to wait
		   for the master node to send us a dir remove for
		   before removing the dir record. */

		if (!dlm_no_directory(ls) &&
		    (r->res_master_nodeid != our_nodeid) &&
		    (dlm_dir_nodeid(r) == our_nodeid)) {
			continue;
		}

		need_shrink = 1;

		if (!time_after_eq(jiffies, r->res_toss_time +
				   dlm_config.ci_toss_secs * HZ)) {
			continue;
		}

		if (!dlm_no_directory(ls) &&
		    (r->res_master_nodeid == our_nodeid) &&
		    (dlm_dir_nodeid(r) != our_nodeid)) {

			/* We're the master of this rsb but we're not
			   the directory record, so we need to tell the
			   dir node to remove the dir record. */

			ls->ls_remove_lens[remote_count] = r->res_length;
			memcpy(ls->ls_remove_names[remote_count], r->res_name,
			       DLM_RESNAME_MAXLEN);
			remote_count++;

			if (remote_count >= DLM_REMOVE_NAMES_MAX)
				break;
			continue;
		}

		if (!kref_put(&r->res_ref, kill_rsb)) {
			log_error(ls, "tossed rsb in use %s", r->res_name);
			continue;
		}

		rb_erase(&r->res_hashnode, &ls->ls_rsbtbl[b].toss);
		dlm_free_rsb(r);
	}

	if (need_shrink)
		ls->ls_rsbtbl[b].flags |= DLM_RTF_SHRINK;
	else
		ls->ls_rsbtbl[b].flags &= ~DLM_RTF_SHRINK;
	spin_unlock(&ls->ls_rsbtbl[b].lock);

	/*
	 * While searching for rsb's to free, we found some that require
	 * remote removal.  We leave them in place and find them again here
	 * so there is a very small gap between removing them from the toss
	 * list and sending the removal.  Keeping this gap small is
	 * important to keep us (the master node) from being out of sync
	 * with the remote dir node for very long.
	 *
	 * From the time the rsb is removed from toss until just after
	 * send_remove, the rsb name is saved in ls_remove_name.  A new
	 * lookup checks this to ensure that a new lookup message for the
	 * same resource name is not sent just before the remove message.
	 */

	for (i = 0; i < remote_count; i++) {
		name = ls->ls_remove_names[i];
		len = ls->ls_remove_lens[i];

		spin_lock(&ls->ls_rsbtbl[b].lock);
		rv = dlm_search_rsb_tree(&ls->ls_rsbtbl[b].toss, name, len, &r);
		if (rv) {
			spin_unlock(&ls->ls_rsbtbl[b].lock);
			log_debug(ls, "remove_name not toss %s", name);
			continue;
		}

		if (r->res_master_nodeid != our_nodeid) {
			spin_unlock(&ls->ls_rsbtbl[b].lock);
			log_debug(ls, "remove_name master %d dir %d our %d %s",
				  r->res_master_nodeid, r->res_dir_nodeid,
				  our_nodeid, name);
			continue;
		}

		if (r->res_dir_nodeid == our_nodeid) {
			/* should never happen */
			spin_unlock(&ls->ls_rsbtbl[b].lock);
			log_error(ls, "remove_name dir %d master %d our %d %s",
				  r->res_dir_nodeid, r->res_master_nodeid,
				  our_nodeid, name);
			continue;
		}

		if (!time_after_eq(jiffies, r->res_toss_time +
				   dlm_config.ci_toss_secs * HZ)) {
			spin_unlock(&ls->ls_rsbtbl[b].lock);
			log_debug(ls, "remove_name toss_time %lu now %lu %s",
				  r->res_toss_time, jiffies, name);
			continue;
		}

		if (!kref_put(&r->res_ref, kill_rsb)) {
			spin_unlock(&ls->ls_rsbtbl[b].lock);
			log_error(ls, "remove_name in use %s", name);
			continue;
		}

		rb_erase(&r->res_hashnode, &ls->ls_rsbtbl[b].toss);

		/* block lookup of same name until we've sent remove */
		spin_lock(&ls->ls_remove_spin);
		ls->ls_remove_len = len;
		memcpy(ls->ls_remove_name, name, DLM_RESNAME_MAXLEN);
		spin_unlock(&ls->ls_remove_spin);
		spin_unlock(&ls->ls_rsbtbl[b].lock);

		send_remove(r);

		/* allow lookup of name again */
		spin_lock(&ls->ls_remove_spin);
		ls->ls_remove_len = 0;
		memset(ls->ls_remove_name, 0, DLM_RESNAME_MAXLEN);
		spin_unlock(&ls->ls_remove_spin);

		dlm_free_rsb(r);
	}
}

void dlm_scan_rsbs(struct dlm_ls *ls)
{
	int i;

	for (i = 0; i < ls->ls_rsbtbl_size; i++) {
		shrink_bucket(ls, i);
		if (dlm_locking_stopped(ls))
			break;
		cond_resched();
	}
}

static void add_timeout(struct dlm_lkb *lkb)
{
	struct dlm_ls *ls = lkb->lkb_resource->res_ls;

	if (is_master_copy(lkb))
		return;

	if (test_bit(LSFL_TIMEWARN, &ls->ls_flags) &&
	    !(lkb->lkb_exflags & DLM_LKF_NODLCKWT)) {
		lkb->lkb_flags |= DLM_IFL_WATCH_TIMEWARN;
		goto add_it;
	}
	if (lkb->lkb_exflags & DLM_LKF_TIMEOUT)
		goto add_it;
	return;

 add_it:
	DLM_ASSERT(list_empty(&lkb->lkb_time_list), dlm_print_lkb(lkb););
	mutex_lock(&ls->ls_timeout_mutex);
	hold_lkb(lkb);
	list_add_tail(&lkb->lkb_time_list, &ls->ls_timeout);
	mutex_unlock(&ls->ls_timeout_mutex);
}

static void del_timeout(struct dlm_lkb *lkb)
{
	struct dlm_ls *ls = lkb->lkb_resource->res_ls;

	mutex_lock(&ls->ls_timeout_mutex);
	if (!list_empty(&lkb->lkb_time_list)) {
		list_del_init(&lkb->lkb_time_list);
		unhold_lkb(lkb);
	}
	mutex_unlock(&ls->ls_timeout_mutex);
}

/* FIXME: is it safe to look at lkb_exflags, lkb_flags, lkb_timestamp, and
   lkb_lksb_timeout without lock_rsb?  Note: we can't lock timeout_mutex
   and then lock rsb because of lock ordering in add_timeout.  We may need
   to specify some special timeout-related bits in the lkb that are just to
   be accessed under the timeout_mutex. */

void dlm_scan_timeout(struct dlm_ls *ls)
{
	struct dlm_rsb *r;
	struct dlm_lkb *lkb;
	int do_cancel, do_warn;
	s64 wait_us;

	for (;;) {
		if (dlm_locking_stopped(ls))
			break;

		do_cancel = 0;
		do_warn = 0;
		mutex_lock(&ls->ls_timeout_mutex);
		list_for_each_entry(lkb, &ls->ls_timeout, lkb_time_list) {

			wait_us = ktime_to_us(ktime_sub(ktime_get(),
					      		lkb->lkb_timestamp));

			if ((lkb->lkb_exflags & DLM_LKF_TIMEOUT) &&
			    wait_us >= (lkb->lkb_timeout_cs * 10000))
				do_cancel = 1;

			if ((lkb->lkb_flags & DLM_IFL_WATCH_TIMEWARN) &&
			    wait_us >= dlm_config.ci_timewarn_cs * 10000)
				do_warn = 1;

			if (!do_cancel && !do_warn)
				continue;
			hold_lkb(lkb);
			break;
		}
		mutex_unlock(&ls->ls_timeout_mutex);

		if (!do_cancel && !do_warn)
			break;

		r = lkb->lkb_resource;
		hold_rsb(r);
		lock_rsb(r);

		if (do_warn) {
			/* clear flag so we only warn once */
			lkb->lkb_flags &= ~DLM_IFL_WATCH_TIMEWARN;
			if (!(lkb->lkb_exflags & DLM_LKF_TIMEOUT))
				del_timeout(lkb);
			dlm_timeout_warn(lkb);
		}

		if (do_cancel) {
			log_debug(ls, "timeout cancel %x node %d %s",
				  lkb->lkb_id, lkb->lkb_nodeid, r->res_name);
			lkb->lkb_flags &= ~DLM_IFL_WATCH_TIMEWARN;
			lkb->lkb_flags |= DLM_IFL_TIMEOUT_CANCEL;
			del_timeout(lkb);
			_cancel_lock(r, lkb);
		}

		unlock_rsb(r);
		unhold_rsb(r);
		dlm_put_lkb(lkb);
	}
}

/* This is only called by dlm_recoverd, and we rely on dlm_ls_stop() stopping
   dlm_recoverd before checking/setting ls_recover_begin. */

void dlm_adjust_timeouts(struct dlm_ls *ls)
{
	struct dlm_lkb *lkb;
	u64 adj_us = jiffies_to_usecs(jiffies - ls->ls_recover_begin);

	ls->ls_recover_begin = 0;
	mutex_lock(&ls->ls_timeout_mutex);
	list_for_each_entry(lkb, &ls->ls_timeout, lkb_time_list)
		lkb->lkb_timestamp = ktime_add_us(lkb->lkb_timestamp, adj_us);
	mutex_unlock(&ls->ls_timeout_mutex);

	if (!dlm_config.ci_waitwarn_us)
		return;

	mutex_lock(&ls->ls_waiters_mutex);
	list_for_each_entry(lkb, &ls->ls_waiters, lkb_wait_reply) {
		if (ktime_to_us(lkb->lkb_wait_time))
			lkb->lkb_wait_time = ktime_get();
	}
	mutex_unlock(&ls->ls_waiters_mutex);
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
		lkb->lkb_sbflags |= DLM_SBF_VALNOTVALID;
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
			    struct dlm_message *ms)
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
		lkb->lkb_lvbseq = ms->m_lvbseq;
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
			  struct dlm_message *ms)
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

static void munge_altmode(struct dlm_lkb *lkb, struct dlm_message *ms)
{
	if (ms->m_type != DLM_MSG_REQUEST_REPLY &&
	    ms->m_type != DLM_MSG_GRANT) {
		log_print("munge_altmode %x invalid reply type %d",
			  lkb->lkb_id, ms->m_type);
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
			lkb->lkb_sbflags |= DLM_SBF_DEMOTED;
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
			lkb->lkb_sbflags |= DLM_SBF_ALTMODE;
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

	wait_pending_remove(r);

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
		schedule();
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
			 int namelen, unsigned long timeout_cs,
			 void (*ast) (void *astparam),
			 void *astparam,
			 void (*bast) (void *astparam, int mode),
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
	args->timeout = timeout_cs;
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
	int rv = -EINVAL;

	if (args->flags & DLM_LKF_CONVERT) {
		if (lkb->lkb_flags & DLM_IFL_MSTCPY)
			goto out;

		if (args->flags & DLM_LKF_QUECVT &&
		    !__quecvt_compat_matrix[lkb->lkb_grmode+1][args->mode+1])
			goto out;

		rv = -EBUSY;
		if (lkb->lkb_status != DLM_LKSTS_GRANTED)
			goto out;

		if (lkb->lkb_wait_type)
			goto out;

		if (is_overlap(lkb))
			goto out;
	}

	lkb->lkb_exflags = args->flags;
	lkb->lkb_sbflags = 0;
	lkb->lkb_astfn = args->astfn;
	lkb->lkb_astparam = args->astparam;
	lkb->lkb_bastfn = args->bastfn;
	lkb->lkb_rqmode = args->mode;
	lkb->lkb_lksb = args->lksb;
	lkb->lkb_lvbptr = args->lksb->sb_lvbptr;
	lkb->lkb_ownpid = (int) current->pid;
	lkb->lkb_timeout_cs = args->timeout;
	rv = 0;
 out:
	if (rv)
		log_debug(ls, "validate_lock_args %d %x %x %x %d %d %s",
			  rv, lkb->lkb_id, lkb->lkb_flags, args->flags,
			  lkb->lkb_status, lkb->lkb_wait_type,
			  lkb->lkb_resource->res_name);
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
	int rv = -EINVAL;

	if (lkb->lkb_flags & DLM_IFL_MSTCPY) {
		log_error(ls, "unlock on MSTCPY %x", lkb->lkb_id);
		dlm_print_lkb(lkb);
		goto out;
	}

	/* an lkb may still exist even though the lock is EOL'ed due to a
	   cancel, unlock or failed noqueue request; an app can't use these
	   locks; return same error as if the lkid had not been found at all */

	if (lkb->lkb_flags & DLM_IFL_ENDOFLIFE) {
		log_debug(ls, "unlock on ENDOFLIFE %x", lkb->lkb_id);
		rv = -ENOENT;
		goto out;
	}

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
		rv = -EBUSY;
		goto out;
	}

	/* cancel not allowed with another cancel/unlock in progress */

	if (args->flags & DLM_LKF_CANCEL) {
		if (lkb->lkb_exflags & DLM_LKF_CANCEL)
			goto out;

		if (is_overlap(lkb))
			goto out;

		/* don't let scand try to do a cancel */
		del_timeout(lkb);

		if (lkb->lkb_flags & DLM_IFL_RESEND) {
			lkb->lkb_flags |= DLM_IFL_OVERLAP_CANCEL;
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
			lkb->lkb_flags |= DLM_IFL_OVERLAP_CANCEL;
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

		/* don't let scand try to do a cancel */
		del_timeout(lkb);

		if (lkb->lkb_flags & DLM_IFL_RESEND) {
			lkb->lkb_flags |= DLM_IFL_OVERLAP_UNLOCK;
			rv = -EBUSY;
			goto out;
		}

		switch (lkb->lkb_wait_type) {
		case DLM_MSG_LOOKUP:
		case DLM_MSG_REQUEST:
			lkb->lkb_flags |= DLM_IFL_OVERLAP_UNLOCK;
			rv = -EBUSY;
			goto out;
		case DLM_MSG_UNLOCK:
			goto out;
		}
		/* add_to_waiters() will set OVERLAP_UNLOCK */
		goto out_ok;
	}

	/* normal unlock not allowed if there's any op in progress */
	rv = -EBUSY;
	if (lkb->lkb_wait_type || lkb->lkb_wait_count)
		goto out;

 out_ok:
	/* an overlapping op shouldn't blow away exflags from other op */
	lkb->lkb_exflags |= args->flags;
	lkb->lkb_sbflags = 0;
	lkb->lkb_astparam = args->astparam;
	rv = 0;
 out:
	if (rv)
		log_debug(ls, "validate_unlock_args %d %x %x %x %x %d %s", rv,
			  lkb->lkb_id, lkb->lkb_flags, lkb->lkb_exflags,
			  args->flags, lkb->lkb_wait_type,
			  lkb->lkb_resource->res_name);
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
		add_timeout(lkb);
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
		add_timeout(lkb);
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

static int request_lock(struct dlm_ls *ls, struct dlm_lkb *lkb, char *name,
			int len, struct dlm_args *args)
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
	     void *name,
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

	error = set_lock_args(mode, lksb, flags, namelen, 0, ast,
			      astarg, bast, &args);
	if (error)
		goto out_put;

	if (convert)
		error = convert_lock(ls, lkb, &args);
	else
		error = request_lock(ls, lkb, name, namelen, &args);

	if (error == -EINPROGRESS)
		error = 0;
 out_put:
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
	   pass into lowcomms_commit and a message buffer (mb) that we
	   write our data into */

	mh = dlm_lowcomms_get_buffer(to_nodeid, mb_len, GFP_NOFS, &mb);
	if (!mh)
		return -ENOBUFS;

	memset(mb, 0, mb_len);

	ms = (struct dlm_message *) mb;

	ms->m_header.h_version = (DLM_HEADER_MAJOR | DLM_HEADER_MINOR);
	ms->m_header.h_lockspace = ls->ls_global_id;
	ms->m_header.h_nodeid = dlm_our_nodeid();
	ms->m_header.h_length = mb_len;
	ms->m_header.h_cmd = DLM_MSG;

	ms->m_type = mstype;

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
		if (lkb && lkb->lkb_lvbptr)
			mb_len += r->res_ls->ls_lvblen;
		break;
	}

	return _create_message(r->res_ls, mb_len, to_nodeid, mstype,
			       ms_ret, mh_ret);
}

/* further lowcomms enhancements or alternate implementations may make
   the return value from this function useful at some point */

static int send_message(struct dlm_mhandle *mh, struct dlm_message *ms)
{
	dlm_message_out(ms);
	dlm_lowcomms_commit_buffer(mh);
	return 0;
}

static void send_args(struct dlm_rsb *r, struct dlm_lkb *lkb,
		      struct dlm_message *ms)
{
	ms->m_nodeid   = lkb->lkb_nodeid;
	ms->m_pid      = lkb->lkb_ownpid;
	ms->m_lkid     = lkb->lkb_id;
	ms->m_remid    = lkb->lkb_remid;
	ms->m_exflags  = lkb->lkb_exflags;
	ms->m_sbflags  = lkb->lkb_sbflags;
	ms->m_flags    = lkb->lkb_flags;
	ms->m_lvbseq   = lkb->lkb_lvbseq;
	ms->m_status   = lkb->lkb_status;
	ms->m_grmode   = lkb->lkb_grmode;
	ms->m_rqmode   = lkb->lkb_rqmode;
	ms->m_hash     = r->res_hash;

	/* m_result and m_bastmode are set from function args,
	   not from lkb fields */

	if (lkb->lkb_bastfn)
		ms->m_asts |= DLM_CB_BAST;
	if (lkb->lkb_astfn)
		ms->m_asts |= DLM_CB_CAST;

	/* compare with switch in create_message; send_remove() doesn't
	   use send_args() */

	switch (ms->m_type) {
	case DLM_MSG_REQUEST:
	case DLM_MSG_LOOKUP:
		memcpy(ms->m_extra, r->res_name, r->res_length);
		break;
	case DLM_MSG_CONVERT:
	case DLM_MSG_UNLOCK:
	case DLM_MSG_REQUEST_REPLY:
	case DLM_MSG_CONVERT_REPLY:
	case DLM_MSG_GRANT:
		if (!lkb->lkb_lvbptr)
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

	error = send_message(mh, ms);
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
		r->res_ls->ls_stub_ms.m_flags = DLM_IFL_STUB_MS;
		r->res_ls->ls_stub_ms.m_type = DLM_MSG_CONVERT_REPLY;
		r->res_ls->ls_stub_ms.m_result = 0;
		__receive_convert_reply(r, lkb, &r->res_ls->ls_stub_ms);
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

	error = send_message(mh, ms);
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

	ms->m_bastmode = mode;

	error = send_message(mh, ms);
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

	error = send_message(mh, ms);
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
	ms->m_hash = r->res_hash;

	error = send_message(mh, ms);
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

	ms->m_result = rv;

	error = send_message(mh, ms);
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

static int send_lookup_reply(struct dlm_ls *ls, struct dlm_message *ms_in,
			     int ret_nodeid, int rv)
{
	struct dlm_rsb *r = &ls->ls_stub_rsb;
	struct dlm_message *ms;
	struct dlm_mhandle *mh;
	int error, nodeid = ms_in->m_header.h_nodeid;

	error = create_message(r, NULL, nodeid, DLM_MSG_LOOKUP_REPLY, &ms, &mh);
	if (error)
		goto out;

	ms->m_lkid = ms_in->m_lkid;
	ms->m_result = rv;
	ms->m_nodeid = ret_nodeid;

	error = send_message(mh, ms);
 out:
	return error;
}

/* which args we save from a received message depends heavily on the type
   of message, unlike the send side where we can safely send everything about
   the lkb for any type of message */

static void receive_flags(struct dlm_lkb *lkb, struct dlm_message *ms)
{
	lkb->lkb_exflags = ms->m_exflags;
	lkb->lkb_sbflags = ms->m_sbflags;
	lkb->lkb_flags = (lkb->lkb_flags & 0xFFFF0000) |
		         (ms->m_flags & 0x0000FFFF);
}

static void receive_flags_reply(struct dlm_lkb *lkb, struct dlm_message *ms)
{
	if (ms->m_flags == DLM_IFL_STUB_MS)
		return;

	lkb->lkb_sbflags = ms->m_sbflags;
	lkb->lkb_flags = (lkb->lkb_flags & 0xFFFF0000) |
		         (ms->m_flags & 0x0000FFFF);
}

static int receive_extralen(struct dlm_message *ms)
{
	return (ms->m_header.h_length - sizeof(struct dlm_message));
}

static int receive_lvb(struct dlm_ls *ls, struct dlm_lkb *lkb,
		       struct dlm_message *ms)
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
				struct dlm_message *ms)
{
	lkb->lkb_nodeid = ms->m_header.h_nodeid;
	lkb->lkb_ownpid = ms->m_pid;
	lkb->lkb_remid = ms->m_lkid;
	lkb->lkb_grmode = DLM_LOCK_IV;
	lkb->lkb_rqmode = ms->m_rqmode;

	lkb->lkb_bastfn = (ms->m_asts & DLM_CB_BAST) ? &fake_bastfn : NULL;
	lkb->lkb_astfn = (ms->m_asts & DLM_CB_CAST) ? &fake_astfn : NULL;

	if (lkb->lkb_exflags & DLM_LKF_VALBLK) {
		/* lkb was just created so there won't be an lvb yet */
		lkb->lkb_lvbptr = dlm_allocate_lvb(ls);
		if (!lkb->lkb_lvbptr)
			return -ENOMEM;
	}

	return 0;
}

static int receive_convert_args(struct dlm_ls *ls, struct dlm_lkb *lkb,
				struct dlm_message *ms)
{
	if (lkb->lkb_status != DLM_LKSTS_GRANTED)
		return -EBUSY;

	if (receive_lvb(ls, lkb, ms))
		return -ENOMEM;

	lkb->lkb_rqmode = ms->m_rqmode;
	lkb->lkb_lvbseq = ms->m_lvbseq;

	return 0;
}

static int receive_unlock_args(struct dlm_ls *ls, struct dlm_lkb *lkb,
			       struct dlm_message *ms)
{
	if (receive_lvb(ls, lkb, ms))
		return -ENOMEM;
	return 0;
}

/* We fill in the stub-lkb fields with the info that send_xxxx_reply()
   uses to send a reply and that the remote end uses to process the reply. */

static void setup_stub_lkb(struct dlm_ls *ls, struct dlm_message *ms)
{
	struct dlm_lkb *lkb = &ls->ls_stub_lkb;
	lkb->lkb_nodeid = ms->m_header.h_nodeid;
	lkb->lkb_remid = ms->m_lkid;
}

/* This is called after the rsb is locked so that we can safely inspect
   fields in the lkb. */

static int validate_message(struct dlm_lkb *lkb, struct dlm_message *ms)
{
	int from = ms->m_header.h_nodeid;
	int error = 0;

	/* currently mixing of user/kernel locks are not supported */
	if (ms->m_flags & DLM_IFL_USER && ~lkb->lkb_flags & DLM_IFL_USER) {
		log_error(lkb->lkb_resource->res_ls,
			  "got user dlm message for a kernel lock");
		error = -EINVAL;
		goto out;
	}

	switch (ms->m_type) {
	case DLM_MSG_CONVERT:
	case DLM_MSG_UNLOCK:
	case DLM_MSG_CANCEL:
		if (!is_master_copy(lkb) || lkb->lkb_nodeid != from)
			error = -EINVAL;
		break;

	case DLM_MSG_CONVERT_REPLY:
	case DLM_MSG_UNLOCK_REPLY:
	case DLM_MSG_CANCEL_REPLY:
	case DLM_MSG_GRANT:
	case DLM_MSG_BAST:
		if (!is_process_copy(lkb) || lkb->lkb_nodeid != from)
			error = -EINVAL;
		break;

	case DLM_MSG_REQUEST_REPLY:
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
			  ms->m_type, from, lkb->lkb_id, lkb->lkb_remid,
			  lkb->lkb_flags, lkb->lkb_nodeid);
	return error;
}

static void send_repeat_remove(struct dlm_ls *ls, char *ms_name, int len)
{
	char name[DLM_RESNAME_MAXLEN + 1];
	struct dlm_message *ms;
	struct dlm_mhandle *mh;
	struct dlm_rsb *r;
	uint32_t hash, b;
	int rv, dir_nodeid;

	memset(name, 0, sizeof(name));
	memcpy(name, ms_name, len);

	hash = jhash(name, len, 0);
	b = hash & (ls->ls_rsbtbl_size - 1);

	dir_nodeid = dlm_hash2nodeid(ls, hash);

	log_error(ls, "send_repeat_remove dir %d %s", dir_nodeid, name);

	spin_lock(&ls->ls_rsbtbl[b].lock);
	rv = dlm_search_rsb_tree(&ls->ls_rsbtbl[b].keep, name, len, &r);
	if (!rv) {
		spin_unlock(&ls->ls_rsbtbl[b].lock);
		log_error(ls, "repeat_remove on keep %s", name);
		return;
	}

	rv = dlm_search_rsb_tree(&ls->ls_rsbtbl[b].toss, name, len, &r);
	if (!rv) {
		spin_unlock(&ls->ls_rsbtbl[b].lock);
		log_error(ls, "repeat_remove on toss %s", name);
		return;
	}

	/* use ls->remove_name2 to avoid conflict with shrink? */

	spin_lock(&ls->ls_remove_spin);
	ls->ls_remove_len = len;
	memcpy(ls->ls_remove_name, name, DLM_RESNAME_MAXLEN);
	spin_unlock(&ls->ls_remove_spin);
	spin_unlock(&ls->ls_rsbtbl[b].lock);

	rv = _create_message(ls, sizeof(struct dlm_message) + len,
			     dir_nodeid, DLM_MSG_REMOVE, &ms, &mh);
	if (rv)
		return;

	memcpy(ms->m_extra, name, len);
	ms->m_hash = hash;

	send_message(mh, ms);

	spin_lock(&ls->ls_remove_spin);
	ls->ls_remove_len = 0;
	memset(ls->ls_remove_name, 0, DLM_RESNAME_MAXLEN);
	spin_unlock(&ls->ls_remove_spin);
}

static int receive_request(struct dlm_ls *ls, struct dlm_message *ms)
{
	struct dlm_lkb *lkb;
	struct dlm_rsb *r;
	int from_nodeid;
	int error, namelen = 0;

	from_nodeid = ms->m_header.h_nodeid;

	error = create_lkb(ls, &lkb);
	if (error)
		goto fail;

	receive_flags(lkb, ms);
	lkb->lkb_flags |= DLM_IFL_MSTCPY;
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

	/* We could repeatedly return -EBADR here if our send_remove() is
	   delayed in being sent/arriving/being processed on the dir node.
	   Another node would repeatedly lookup up the master, and the dir
	   node would continue returning our nodeid until our send_remove
	   took effect.

	   We send another remove message in case our previous send_remove
	   was lost/ignored/missed somehow. */

	if (error != -ENOTBLK) {
		log_limit(ls, "receive_request %x from %d %d",
			  ms->m_lkid, from_nodeid, error);
	}

	if (namelen && error == -EBADR) {
		send_repeat_remove(ls, ms->m_extra, namelen);
		msleep(1000);
	}

	setup_stub_lkb(ls, ms);
	send_request_reply(&ls->ls_stub_rsb, &ls->ls_stub_lkb, error);
	return error;
}

static int receive_convert(struct dlm_ls *ls, struct dlm_message *ms)
{
	struct dlm_lkb *lkb;
	struct dlm_rsb *r;
	int error, reply = 1;

	error = find_lkb(ls, ms->m_remid, &lkb);
	if (error)
		goto fail;

	if (lkb->lkb_remid != ms->m_lkid) {
		log_error(ls, "receive_convert %x remid %x recover_seq %llu "
			  "remote %d %x", lkb->lkb_id, lkb->lkb_remid,
			  (unsigned long long)lkb->lkb_recover_seq,
			  ms->m_header.h_nodeid, ms->m_lkid);
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
	setup_stub_lkb(ls, ms);
	send_convert_reply(&ls->ls_stub_rsb, &ls->ls_stub_lkb, error);
	return error;
}

static int receive_unlock(struct dlm_ls *ls, struct dlm_message *ms)
{
	struct dlm_lkb *lkb;
	struct dlm_rsb *r;
	int error;

	error = find_lkb(ls, ms->m_remid, &lkb);
	if (error)
		goto fail;

	if (lkb->lkb_remid != ms->m_lkid) {
		log_error(ls, "receive_unlock %x remid %x remote %d %x",
			  lkb->lkb_id, lkb->lkb_remid,
			  ms->m_header.h_nodeid, ms->m_lkid);
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
	setup_stub_lkb(ls, ms);
	send_unlock_reply(&ls->ls_stub_rsb, &ls->ls_stub_lkb, error);
	return error;
}

static int receive_cancel(struct dlm_ls *ls, struct dlm_message *ms)
{
	struct dlm_lkb *lkb;
	struct dlm_rsb *r;
	int error;

	error = find_lkb(ls, ms->m_remid, &lkb);
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
	setup_stub_lkb(ls, ms);
	send_cancel_reply(&ls->ls_stub_rsb, &ls->ls_stub_lkb, error);
	return error;
}

static int receive_grant(struct dlm_ls *ls, struct dlm_message *ms)
{
	struct dlm_lkb *lkb;
	struct dlm_rsb *r;
	int error;

	error = find_lkb(ls, ms->m_remid, &lkb);
	if (error)
		return error;

	r = lkb->lkb_resource;

	hold_rsb(r);
	lock_rsb(r);

	error = validate_message(lkb, ms);
	if (error)
		goto out;

	receive_flags_reply(lkb, ms);
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

static int receive_bast(struct dlm_ls *ls, struct dlm_message *ms)
{
	struct dlm_lkb *lkb;
	struct dlm_rsb *r;
	int error;

	error = find_lkb(ls, ms->m_remid, &lkb);
	if (error)
		return error;

	r = lkb->lkb_resource;

	hold_rsb(r);
	lock_rsb(r);

	error = validate_message(lkb, ms);
	if (error)
		goto out;

	queue_bast(r, lkb, ms->m_bastmode);
	lkb->lkb_highbast = ms->m_bastmode;
 out:
	unlock_rsb(r);
	put_rsb(r);
	dlm_put_lkb(lkb);
	return 0;
}

static void receive_lookup(struct dlm_ls *ls, struct dlm_message *ms)
{
	int len, error, ret_nodeid, from_nodeid, our_nodeid;

	from_nodeid = ms->m_header.h_nodeid;
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

static void receive_remove(struct dlm_ls *ls, struct dlm_message *ms)
{
	char name[DLM_RESNAME_MAXLEN+1];
	struct dlm_rsb *r;
	uint32_t hash, b;
	int rv, len, dir_nodeid, from_nodeid;

	from_nodeid = ms->m_header.h_nodeid;

	len = receive_extralen(ms);

	if (len > DLM_RESNAME_MAXLEN) {
		log_error(ls, "receive_remove from %d bad len %d",
			  from_nodeid, len);
		return;
	}

	dir_nodeid = dlm_hash2nodeid(ls, ms->m_hash);
	if (dir_nodeid != dlm_our_nodeid()) {
		log_error(ls, "receive_remove from %d bad nodeid %d",
			  from_nodeid, dir_nodeid);
		return;
	}

	/* Look for name on rsbtbl.toss, if it's there, kill it.
	   If it's on rsbtbl.keep, it's being used, and we should ignore this
	   message.  This is an expected race between the dir node sending a
	   request to the master node at the same time as the master node sends
	   a remove to the dir node.  The resolution to that race is for the
	   dir node to ignore the remove message, and the master node to
	   recreate the master rsb when it gets a request from the dir node for
	   an rsb it doesn't have. */

	memset(name, 0, sizeof(name));
	memcpy(name, ms->m_extra, len);

	hash = jhash(name, len, 0);
	b = hash & (ls->ls_rsbtbl_size - 1);

	spin_lock(&ls->ls_rsbtbl[b].lock);

	rv = dlm_search_rsb_tree(&ls->ls_rsbtbl[b].toss, name, len, &r);
	if (rv) {
		/* verify the rsb is on keep list per comment above */
		rv = dlm_search_rsb_tree(&ls->ls_rsbtbl[b].keep, name, len, &r);
		if (rv) {
			/* should not happen */
			log_error(ls, "receive_remove from %d not found %s",
				  from_nodeid, name);
			spin_unlock(&ls->ls_rsbtbl[b].lock);
			return;
		}
		if (r->res_master_nodeid != from_nodeid) {
			/* should not happen */
			log_error(ls, "receive_remove keep from %d master %d",
				  from_nodeid, r->res_master_nodeid);
			dlm_print_rsb(r);
			spin_unlock(&ls->ls_rsbtbl[b].lock);
			return;
		}

		log_debug(ls, "receive_remove from %d master %d first %x %s",
			  from_nodeid, r->res_master_nodeid, r->res_first_lkid,
			  name);
		spin_unlock(&ls->ls_rsbtbl[b].lock);
		return;
	}

	if (r->res_master_nodeid != from_nodeid) {
		log_error(ls, "receive_remove toss from %d master %d",
			  from_nodeid, r->res_master_nodeid);
		dlm_print_rsb(r);
		spin_unlock(&ls->ls_rsbtbl[b].lock);
		return;
	}

	if (kref_put(&r->res_ref, kill_rsb)) {
		rb_erase(&r->res_hashnode, &ls->ls_rsbtbl[b].toss);
		spin_unlock(&ls->ls_rsbtbl[b].lock);
		dlm_free_rsb(r);
	} else {
		log_error(ls, "receive_remove from %d rsb ref error",
			  from_nodeid);
		dlm_print_rsb(r);
		spin_unlock(&ls->ls_rsbtbl[b].lock);
	}
}

static void receive_purge(struct dlm_ls *ls, struct dlm_message *ms)
{
	do_purge(ls, ms->m_nodeid, ms->m_pid);
}

static int receive_request_reply(struct dlm_ls *ls, struct dlm_message *ms)
{
	struct dlm_lkb *lkb;
	struct dlm_rsb *r;
	int error, mstype, result;
	int from_nodeid = ms->m_header.h_nodeid;

	error = find_lkb(ls, ms->m_remid, &lkb);
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
			  lkb->lkb_id, from_nodeid, ms->m_lkid, ms->m_result);
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
	result = ms->m_result;

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
		receive_flags_reply(lkb, ms);
		lkb->lkb_remid = ms->m_lkid;
		if (is_altmode(lkb))
			munge_altmode(lkb, ms);
		if (result) {
			add_lkb(r, lkb, DLM_LKSTS_WAITING);
			add_timeout(lkb);
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

	if (is_overlap_unlock(lkb) && (result == 0 || result == -EINPROGRESS)) {
		log_debug(ls, "receive_request_reply %x result %d unlock",
			  lkb->lkb_id, result);
		lkb->lkb_flags &= ~DLM_IFL_OVERLAP_UNLOCK;
		lkb->lkb_flags &= ~DLM_IFL_OVERLAP_CANCEL;
		send_unlock(r, lkb);
	} else if (is_overlap_cancel(lkb) && (result == -EINPROGRESS)) {
		log_debug(ls, "receive_request_reply %x cancel", lkb->lkb_id);
		lkb->lkb_flags &= ~DLM_IFL_OVERLAP_UNLOCK;
		lkb->lkb_flags &= ~DLM_IFL_OVERLAP_CANCEL;
		send_cancel(r, lkb);
	} else {
		lkb->lkb_flags &= ~DLM_IFL_OVERLAP_CANCEL;
		lkb->lkb_flags &= ~DLM_IFL_OVERLAP_UNLOCK;
	}
 out:
	unlock_rsb(r);
	put_rsb(r);
	dlm_put_lkb(lkb);
	return 0;
}

static void __receive_convert_reply(struct dlm_rsb *r, struct dlm_lkb *lkb,
				    struct dlm_message *ms)
{
	/* this is the value returned from do_convert() on the master */
	switch (ms->m_result) {
	case -EAGAIN:
		/* convert would block (be queued) on remote master */
		queue_cast(r, lkb, -EAGAIN);
		break;

	case -EDEADLK:
		receive_flags_reply(lkb, ms);
		revert_lock_pc(r, lkb);
		queue_cast(r, lkb, -EDEADLK);
		break;

	case -EINPROGRESS:
		/* convert was queued on remote master */
		receive_flags_reply(lkb, ms);
		if (is_demoted(lkb))
			munge_demoted(lkb);
		del_lkb(r, lkb);
		add_lkb(r, lkb, DLM_LKSTS_CONVERT);
		add_timeout(lkb);
		break;

	case 0:
		/* convert was granted on remote master */
		receive_flags_reply(lkb, ms);
		if (is_demoted(lkb))
			munge_demoted(lkb);
		grant_lock_pc(r, lkb, ms);
		queue_cast(r, lkb, 0);
		break;

	default:
		log_error(r->res_ls, "receive_convert_reply %x remote %d %x %d",
			  lkb->lkb_id, ms->m_header.h_nodeid, ms->m_lkid,
			  ms->m_result);
		dlm_print_rsb(r);
		dlm_print_lkb(lkb);
	}
}

static void _receive_convert_reply(struct dlm_lkb *lkb, struct dlm_message *ms)
{
	struct dlm_rsb *r = lkb->lkb_resource;
	int error;

	hold_rsb(r);
	lock_rsb(r);

	error = validate_message(lkb, ms);
	if (error)
		goto out;

	/* stub reply can happen with waiters_mutex held */
	error = remove_from_waiters_ms(lkb, ms);
	if (error)
		goto out;

	__receive_convert_reply(r, lkb, ms);
 out:
	unlock_rsb(r);
	put_rsb(r);
}

static int receive_convert_reply(struct dlm_ls *ls, struct dlm_message *ms)
{
	struct dlm_lkb *lkb;
	int error;

	error = find_lkb(ls, ms->m_remid, &lkb);
	if (error)
		return error;

	_receive_convert_reply(lkb, ms);
	dlm_put_lkb(lkb);
	return 0;
}

static void _receive_unlock_reply(struct dlm_lkb *lkb, struct dlm_message *ms)
{
	struct dlm_rsb *r = lkb->lkb_resource;
	int error;

	hold_rsb(r);
	lock_rsb(r);

	error = validate_message(lkb, ms);
	if (error)
		goto out;

	/* stub reply can happen with waiters_mutex held */
	error = remove_from_waiters_ms(lkb, ms);
	if (error)
		goto out;

	/* this is the value returned from do_unlock() on the master */

	switch (ms->m_result) {
	case -DLM_EUNLOCK:
		receive_flags_reply(lkb, ms);
		remove_lock_pc(r, lkb);
		queue_cast(r, lkb, -DLM_EUNLOCK);
		break;
	case -ENOENT:
		break;
	default:
		log_error(r->res_ls, "receive_unlock_reply %x error %d",
			  lkb->lkb_id, ms->m_result);
	}
 out:
	unlock_rsb(r);
	put_rsb(r);
}

static int receive_unlock_reply(struct dlm_ls *ls, struct dlm_message *ms)
{
	struct dlm_lkb *lkb;
	int error;

	error = find_lkb(ls, ms->m_remid, &lkb);
	if (error)
		return error;

	_receive_unlock_reply(lkb, ms);
	dlm_put_lkb(lkb);
	return 0;
}

static void _receive_cancel_reply(struct dlm_lkb *lkb, struct dlm_message *ms)
{
	struct dlm_rsb *r = lkb->lkb_resource;
	int error;

	hold_rsb(r);
	lock_rsb(r);

	error = validate_message(lkb, ms);
	if (error)
		goto out;

	/* stub reply can happen with waiters_mutex held */
	error = remove_from_waiters_ms(lkb, ms);
	if (error)
		goto out;

	/* this is the value returned from do_cancel() on the master */

	switch (ms->m_result) {
	case -DLM_ECANCEL:
		receive_flags_reply(lkb, ms);
		revert_lock_pc(r, lkb);
		queue_cast(r, lkb, -DLM_ECANCEL);
		break;
	case 0:
		break;
	default:
		log_error(r->res_ls, "receive_cancel_reply %x error %d",
			  lkb->lkb_id, ms->m_result);
	}
 out:
	unlock_rsb(r);
	put_rsb(r);
}

static int receive_cancel_reply(struct dlm_ls *ls, struct dlm_message *ms)
{
	struct dlm_lkb *lkb;
	int error;

	error = find_lkb(ls, ms->m_remid, &lkb);
	if (error)
		return error;

	_receive_cancel_reply(lkb, ms);
	dlm_put_lkb(lkb);
	return 0;
}

static void receive_lookup_reply(struct dlm_ls *ls, struct dlm_message *ms)
{
	struct dlm_lkb *lkb;
	struct dlm_rsb *r;
	int error, ret_nodeid;
	int do_lookup_list = 0;

	error = find_lkb(ls, ms->m_lkid, &lkb);
	if (error) {
		log_error(ls, "receive_lookup_reply no lkid %x", ms->m_lkid);
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

	ret_nodeid = ms->m_nodeid;

	/* We sometimes receive a request from the dir node for this
	   rsb before we've received the dir node's loookup_reply for it.
	   The request from the dir node implies we're the master, so we set
	   ourself as master in receive_request_reply, and verify here that
	   we are indeed the master. */

	if (r->res_master_nodeid && (r->res_master_nodeid != ret_nodeid)) {
		/* This should never happen */
		log_error(ls, "receive_lookup_reply %x from %d ret %d "
			  "master %d dir %d our %d first %x %s",
			  lkb->lkb_id, ms->m_header.h_nodeid, ret_nodeid,
			  r->res_master_nodeid, r->res_dir_nodeid,
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
			  lkb->lkb_id, ms->m_header.h_nodeid);
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
			  lkb->lkb_id, lkb->lkb_flags);
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

static void _receive_message(struct dlm_ls *ls, struct dlm_message *ms,
			     uint32_t saved_seq)
{
	int error = 0, noent = 0;

	if (!dlm_is_member(ls, ms->m_header.h_nodeid)) {
		log_limit(ls, "receive %d from non-member %d %x %x %d",
			  ms->m_type, ms->m_header.h_nodeid, ms->m_lkid,
			  ms->m_remid, ms->m_result);
		return;
	}

	switch (ms->m_type) {

	/* messages sent to a master node */

	case DLM_MSG_REQUEST:
		error = receive_request(ls, ms);
		break;

	case DLM_MSG_CONVERT:
		error = receive_convert(ls, ms);
		break;

	case DLM_MSG_UNLOCK:
		error = receive_unlock(ls, ms);
		break;

	case DLM_MSG_CANCEL:
		noent = 1;
		error = receive_cancel(ls, ms);
		break;

	/* messages sent from a master node (replies to above) */

	case DLM_MSG_REQUEST_REPLY:
		error = receive_request_reply(ls, ms);
		break;

	case DLM_MSG_CONVERT_REPLY:
		error = receive_convert_reply(ls, ms);
		break;

	case DLM_MSG_UNLOCK_REPLY:
		error = receive_unlock_reply(ls, ms);
		break;

	case DLM_MSG_CANCEL_REPLY:
		error = receive_cancel_reply(ls, ms);
		break;

	/* messages sent from a master node (only two types of async msg) */

	case DLM_MSG_GRANT:
		noent = 1;
		error = receive_grant(ls, ms);
		break;

	case DLM_MSG_BAST:
		noent = 1;
		error = receive_bast(ls, ms);
		break;

	/* messages sent to a dir node */

	case DLM_MSG_LOOKUP:
		receive_lookup(ls, ms);
		break;

	case DLM_MSG_REMOVE:
		receive_remove(ls, ms);
		break;

	/* messages sent from a dir node (remove has no reply) */

	case DLM_MSG_LOOKUP_REPLY:
		receive_lookup_reply(ls, ms);
		break;

	/* other messages */

	case DLM_MSG_PURGE:
		receive_purge(ls, ms);
		break;

	default:
		log_error(ls, "unknown message type %d", ms->m_type);
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
			  ms->m_type, ms->m_remid, ms->m_header.h_nodeid,
			  ms->m_lkid, saved_seq);
	} else if (error == -ENOENT) {
		log_error(ls, "receive %d no %x remote %d %x saved_seq %u",
			  ms->m_type, ms->m_remid, ms->m_header.h_nodeid,
			  ms->m_lkid, saved_seq);

		if (ms->m_type == DLM_MSG_CONVERT)
			dlm_dump_rsb_hash(ls, ms->m_hash);
	}

	if (error == -EINVAL) {
		log_error(ls, "receive %d inval from %d lkid %x remid %x "
			  "saved_seq %u",
			  ms->m_type, ms->m_header.h_nodeid,
			  ms->m_lkid, ms->m_remid, saved_seq);
	}
}

/* If the lockspace is in recovery mode (locking stopped), then normal
   messages are saved on the requestqueue for processing after recovery is
   done.  When not in recovery mode, we wait for dlm_recoverd to drain saved
   messages off the requestqueue before we process new ones. This occurs right
   after recovery completes when we transition from saving all messages on
   requestqueue, to processing all the saved messages, to processing new
   messages as they arrive. */

static void dlm_receive_message(struct dlm_ls *ls, struct dlm_message *ms,
				int nodeid)
{
	if (dlm_locking_stopped(ls)) {
		/* If we were a member of this lockspace, left, and rejoined,
		   other nodes may still be sending us messages from the
		   lockspace generation before we left. */
		if (!ls->ls_generation) {
			log_limit(ls, "receive %d from %d ignore old gen",
				  ms->m_type, nodeid);
			return;
		}

		dlm_add_requestqueue(ls, nodeid, ms);
	} else {
		dlm_wait_requestqueue(ls);
		_receive_message(ls, ms, 0);
	}
}

/* This is called by dlm_recoverd to process messages that were saved on
   the requestqueue. */

void dlm_receive_message_saved(struct dlm_ls *ls, struct dlm_message *ms,
			       uint32_t saved_seq)
{
	_receive_message(ls, ms, saved_seq);
}

/* This is called by the midcomms layer when something is received for
   the lockspace.  It could be either a MSG (normal message sent as part of
   standard locking activity) or an RCOM (recovery message sent as part of
   lockspace recovery). */

void dlm_receive_buffer(union dlm_packet *p, int nodeid)
{
	struct dlm_header *hd = &p->header;
	struct dlm_ls *ls;
	int type = 0;

	switch (hd->h_cmd) {
	case DLM_MSG:
		dlm_message_in(&p->message);
		type = p->message.m_type;
		break;
	case DLM_RCOM:
		dlm_rcom_in(&p->rcom);
		type = p->rcom.rc_type;
		break;
	default:
		log_print("invalid h_cmd %d from %u", hd->h_cmd, nodeid);
		return;
	}

	if (hd->h_nodeid != nodeid) {
		log_print("invalid h_nodeid %d from %d lockspace %x",
			  hd->h_nodeid, nodeid, hd->h_lockspace);
		return;
	}

	ls = dlm_find_lockspace_global(hd->h_lockspace);
	if (!ls) {
		if (dlm_config.ci_log_debug) {
			printk_ratelimited(KERN_DEBUG "dlm: invalid lockspace "
				"%u from %d cmd %d type %d\n",
				hd->h_lockspace, nodeid, hd->h_cmd, type);
		}

		if (hd->h_cmd == DLM_RCOM && type == DLM_RCOM_STATUS)
			dlm_send_ls_not_ready(nodeid, &p->rcom);
		return;
	}

	/* this rwsem allows dlm_ls_stop() to wait for all dlm_recv threads to
	   be inactive (in this ls) before transitioning to recovery mode */

	down_read(&ls->ls_recv_active);
	if (hd->h_cmd == DLM_MSG)
		dlm_receive_message(ls, &p->message, nodeid);
	else
		dlm_receive_rcom(ls, &p->rcom, nodeid);
	up_read(&ls->ls_recv_active);

	dlm_put_lockspace(ls);
}

static void recover_convert_waiter(struct dlm_ls *ls, struct dlm_lkb *lkb,
				   struct dlm_message *ms_stub)
{
	if (middle_conversion(lkb)) {
		hold_lkb(lkb);
		memset(ms_stub, 0, sizeof(struct dlm_message));
		ms_stub->m_flags = DLM_IFL_STUB_MS;
		ms_stub->m_type = DLM_MSG_CONVERT_REPLY;
		ms_stub->m_result = -EINPROGRESS;
		ms_stub->m_header.h_nodeid = lkb->lkb_nodeid;
		_receive_convert_reply(lkb, ms_stub);

		/* Same special case as in receive_rcom_lock_args() */
		lkb->lkb_grmode = DLM_LOCK_IV;
		rsb_set_flag(lkb->lkb_resource, RSB_RECOVER_CONVERT);
		unhold_lkb(lkb);

	} else if (lkb->lkb_rqmode >= lkb->lkb_grmode) {
		lkb->lkb_flags |= DLM_IFL_RESEND;
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
	struct dlm_message *ms_stub;
	int wait_type, stub_unlock_result, stub_cancel_result;
	int dir_nodeid;

	ms_stub = kmalloc(sizeof(*ms_stub), GFP_KERNEL);
	if (!ms_stub)
		return;

	mutex_lock(&ls->ls_waiters_mutex);

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
			lkb->lkb_flags |= DLM_IFL_RESEND;
			continue;
		}

		if (!waiter_needs_recovery(ls, lkb, dir_nodeid))
			continue;

		wait_type = lkb->lkb_wait_type;
		stub_unlock_result = -DLM_EUNLOCK;
		stub_cancel_result = -DLM_ECANCEL;

		/* Main reply may have been received leaving a zero wait_type,
		   but a reply for the overlapping op may not have been
		   received.  In that case we need to fake the appropriate
		   reply for the overlap op. */

		if (!wait_type) {
			if (is_overlap_cancel(lkb)) {
				wait_type = DLM_MSG_CANCEL;
				if (lkb->lkb_grmode == DLM_LOCK_IV)
					stub_cancel_result = 0;
			}
			if (is_overlap_unlock(lkb)) {
				wait_type = DLM_MSG_UNLOCK;
				if (lkb->lkb_grmode == DLM_LOCK_IV)
					stub_unlock_result = -ENOENT;
			}

			log_debug(ls, "rwpre overlap %x %x %d %d %d",
				  lkb->lkb_id, lkb->lkb_flags, wait_type,
				  stub_cancel_result, stub_unlock_result);
		}

		switch (wait_type) {

		case DLM_MSG_REQUEST:
			lkb->lkb_flags |= DLM_IFL_RESEND;
			break;

		case DLM_MSG_CONVERT:
			recover_convert_waiter(ls, lkb, ms_stub);
			break;

		case DLM_MSG_UNLOCK:
			hold_lkb(lkb);
			memset(ms_stub, 0, sizeof(struct dlm_message));
			ms_stub->m_flags = DLM_IFL_STUB_MS;
			ms_stub->m_type = DLM_MSG_UNLOCK_REPLY;
			ms_stub->m_result = stub_unlock_result;
			ms_stub->m_header.h_nodeid = lkb->lkb_nodeid;
			_receive_unlock_reply(lkb, ms_stub);
			dlm_put_lkb(lkb);
			break;

		case DLM_MSG_CANCEL:
			hold_lkb(lkb);
			memset(ms_stub, 0, sizeof(struct dlm_message));
			ms_stub->m_flags = DLM_IFL_STUB_MS;
			ms_stub->m_type = DLM_MSG_CANCEL_REPLY;
			ms_stub->m_result = stub_cancel_result;
			ms_stub->m_header.h_nodeid = lkb->lkb_nodeid;
			_receive_cancel_reply(lkb, ms_stub);
			dlm_put_lkb(lkb);
			break;

		default:
			log_error(ls, "invalid lkb wait_type %d %d",
				  lkb->lkb_wait_type, wait_type);
		}
		schedule();
	}
	mutex_unlock(&ls->ls_waiters_mutex);
	kfree(ms_stub);
}

static struct dlm_lkb *find_resend_waiter(struct dlm_ls *ls)
{
	struct dlm_lkb *lkb;
	int found = 0;

	mutex_lock(&ls->ls_waiters_mutex);
	list_for_each_entry(lkb, &ls->ls_waiters, lkb_wait_reply) {
		if (lkb->lkb_flags & DLM_IFL_RESEND) {
			hold_lkb(lkb);
			found = 1;
			break;
		}
	}
	mutex_unlock(&ls->ls_waiters_mutex);

	if (!found)
		lkb = NULL;
	return lkb;
}

/* Deal with lookups and lkb's marked RESEND from _pre.  We may now be the
   master or dir-node for r.  Processing the lkb may result in it being placed
   back on waiters. */

/* We do this after normal locking has been enabled and any saved messages
   (in requestqueue) have been processed.  We should be confident that at
   this point we won't get or process a reply to any of these waiting
   operations.  But, new ops may be coming in on the rsbs/locks here from
   userspace or remotely. */

/* there may have been an overlap unlock/cancel prior to recovery or after
   recovery.  if before, the lkb may still have a pos wait_count; if after, the
   overlap flag would just have been set and nothing new sent.  we can be
   confident here than any replies to either the initial op or overlap ops
   prior to recovery have been received. */

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

		lkb = find_resend_waiter(ls);
		if (!lkb)
			break;

		r = lkb->lkb_resource;
		hold_rsb(r);
		lock_rsb(r);

		mstype = lkb->lkb_wait_type;
		oc = is_overlap_cancel(lkb);
		ou = is_overlap_unlock(lkb);
		err = 0;

		log_debug(ls, "waiter %x remote %x msg %d r_nodeid %d "
			  "lkb_nodeid %d wait_nodeid %d dir_nodeid %d "
			  "overlap %d %d", lkb->lkb_id, lkb->lkb_remid, mstype,
			  r->res_nodeid, lkb->lkb_nodeid, lkb->lkb_wait_nodeid,
			  dlm_dir_nodeid(r), oc, ou);

		/* At this point we assume that we won't get a reply to any
		   previous op or overlap op on this lock.  First, do a big
		   remove_from_waiters() for all previous ops. */

		lkb->lkb_flags &= ~DLM_IFL_RESEND;
		lkb->lkb_flags &= ~DLM_IFL_OVERLAP_UNLOCK;
		lkb->lkb_flags &= ~DLM_IFL_OVERLAP_CANCEL;
		lkb->lkb_wait_type = 0;
		lkb->lkb_wait_count = 0;
		mutex_lock(&ls->ls_waiters_mutex);
		list_del_init(&lkb->lkb_wait_reply);
		mutex_unlock(&ls->ls_waiters_mutex);
		unhold_lkb(lkb); /* for waiters list */

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
				if (is_master(r))
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

void dlm_recover_purge(struct dlm_ls *ls)
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

	down_write(&ls->ls_root_sem);
	list_for_each_entry(r, &ls->ls_root_list, res_root_list) {
		hold_rsb(r);
		lock_rsb(r);
		if (is_master(r)) {
			purge_dead_list(ls, r, &r->res_grantqueue,
					nodeid_gone, &lkb_count);
			purge_dead_list(ls, r, &r->res_convertqueue,
					nodeid_gone, &lkb_count);
			purge_dead_list(ls, r, &r->res_waitqueue,
					nodeid_gone, &lkb_count);
		}
		unlock_rsb(r);
		unhold_rsb(r);
		cond_resched();
	}
	up_write(&ls->ls_root_sem);

	if (lkb_count)
		log_rinfo(ls, "dlm_recover_purge %u locks for %u nodes",
			  lkb_count, nodes_count);
}

static struct dlm_rsb *find_grant_rsb(struct dlm_ls *ls, int bucket)
{
	struct rb_node *n;
	struct dlm_rsb *r;

	spin_lock(&ls->ls_rsbtbl[bucket].lock);
	for (n = rb_first(&ls->ls_rsbtbl[bucket].keep); n; n = rb_next(n)) {
		r = rb_entry(n, struct dlm_rsb, res_hashnode);

		if (!rsb_flag(r, RSB_RECOVER_GRANT))
			continue;
		if (!is_master(r)) {
			rsb_clear_flag(r, RSB_RECOVER_GRANT);
			continue;
		}
		hold_rsb(r);
		spin_unlock(&ls->ls_rsbtbl[bucket].lock);
		return r;
	}
	spin_unlock(&ls->ls_rsbtbl[bucket].lock);
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
	int bucket = 0;
	unsigned int count = 0;
	unsigned int rsb_count = 0;
	unsigned int lkb_count = 0;

	while (1) {
		r = find_grant_rsb(ls, bucket);
		if (!r) {
			if (bucket == ls->ls_rsbtbl_size - 1)
				break;
			bucket++;
			continue;
		}
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
				  struct dlm_rsb *r, struct dlm_rcom *rc)
{
	struct rcom_lock *rl = (struct rcom_lock *) rc->rc_buf;

	lkb->lkb_nodeid = rc->rc_header.h_nodeid;
	lkb->lkb_ownpid = le32_to_cpu(rl->rl_ownpid);
	lkb->lkb_remid = le32_to_cpu(rl->rl_lkid);
	lkb->lkb_exflags = le32_to_cpu(rl->rl_exflags);
	lkb->lkb_flags = le32_to_cpu(rl->rl_flags) & 0x0000FFFF;
	lkb->lkb_flags |= DLM_IFL_MSTCPY;
	lkb->lkb_lvbseq = le32_to_cpu(rl->rl_lvbseq);
	lkb->lkb_rqmode = rl->rl_rqmode;
	lkb->lkb_grmode = rl->rl_grmode;
	/* don't set lkb_status because add_lkb wants to itself */

	lkb->lkb_bastfn = (rl->rl_asts & DLM_CB_BAST) ? &fake_bastfn : NULL;
	lkb->lkb_astfn = (rl->rl_asts & DLM_CB_CAST) ? &fake_astfn : NULL;

	if (lkb->lkb_exflags & DLM_LKF_VALBLK) {
		int lvblen = rc->rc_header.h_length - sizeof(struct dlm_rcom) -
			 sizeof(struct rcom_lock);
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
int dlm_recover_master_copy(struct dlm_ls *ls, struct dlm_rcom *rc)
{
	struct rcom_lock *rl = (struct rcom_lock *) rc->rc_buf;
	struct dlm_rsb *r;
	struct dlm_lkb *lkb;
	uint32_t remid = 0;
	int from_nodeid = rc->rc_header.h_nodeid;
	int error;

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
	error = 0;
	ls->ls_recover_locks_in++;

	if (!list_empty(&r->res_waitqueue) || !list_empty(&r->res_convertqueue))
		rsb_set_flag(r, RSB_RECOVER_GRANT);

 out_remid:
	/* this is the new value returned to the lock holder for
	   saving in its process-copy lkb */
	rl->rl_remid = cpu_to_le32(lkb->lkb_id);

	lkb->lkb_recover_seq = ls->ls_recover_seq;

 out_unlock:
	unlock_rsb(r);
	put_rsb(r);
 out:
	if (error && error != -EEXIST)
		log_rinfo(ls, "dlm_recover_master_copy remote %d %x error %d",
			  from_nodeid, remid, error);
	rl->rl_result = cpu_to_le32(error);
	return error;
}

/* needs at least dlm_rcom + rcom_lock */
int dlm_recover_process_copy(struct dlm_ls *ls, struct dlm_rcom *rc)
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
			  lkid, rc->rc_header.h_nodeid, remid, result);
		return error;
	}

	r = lkb->lkb_resource;
	hold_rsb(r);
	lock_rsb(r);

	if (!is_process_copy(lkb)) {
		log_error(ls, "dlm_recover_process_copy bad %x remote %d %x %d",
			  lkid, rc->rc_header.h_nodeid, remid, result);
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
			  lkid, rc->rc_header.h_nodeid, remid, result);
	
		dlm_send_rcom_lock(r, lkb);
		goto out;
	case -EEXIST:
	case 0:
		lkb->lkb_remid = remid;
		break;
	default:
		log_error(ls, "dlm_recover_process_copy %x remote %d %x %d unk",
			  lkid, rc->rc_header.h_nodeid, remid, result);
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
		     int mode, uint32_t flags, void *name, unsigned int namelen,
		     unsigned long timeout_cs)
{
	struct dlm_lkb *lkb;
	struct dlm_args args;
	int error;

	dlm_lock_recovery(ls);

	error = create_lkb(ls, &lkb);
	if (error) {
		kfree(ua);
		goto out;
	}

	if (flags & DLM_LKF_VALBLK) {
		ua->lksb.sb_lvbptr = kzalloc(DLM_USER_LVB_LEN, GFP_NOFS);
		if (!ua->lksb.sb_lvbptr) {
			kfree(ua);
			__put_lkb(ls, lkb);
			error = -ENOMEM;
			goto out;
		}
	}
	error = set_lock_args(mode, &ua->lksb, flags, namelen, timeout_cs,
			      fake_astfn, ua, fake_bastfn, &args);
	if (error) {
		kfree(ua->lksb.sb_lvbptr);
		ua->lksb.sb_lvbptr = NULL;
		kfree(ua);
		__put_lkb(ls, lkb);
		goto out;
	}

	/* After ua is attached to lkb it will be freed by dlm_free_lkb().
	   When DLM_IFL_USER is set, the dlm knows that this is a userspace
	   lock and that lkb_astparam is the dlm_user_args structure. */
	lkb->lkb_flags |= DLM_IFL_USER;
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
		__put_lkb(ls, lkb);
		goto out;
	}

	/* add this new lkb to the per-process list of locks */
	spin_lock(&ua->proc->locks_spin);
	hold_lkb(lkb);
	list_add_tail(&lkb->lkb_ownqueue, &ua->proc->locks);
	spin_unlock(&ua->proc->locks_spin);
 out:
	dlm_unlock_recovery(ls);
	return error;
}

int dlm_user_convert(struct dlm_ls *ls, struct dlm_user_args *ua_tmp,
		     int mode, uint32_t flags, uint32_t lkid, char *lvb_in,
		     unsigned long timeout_cs)
{
	struct dlm_lkb *lkb;
	struct dlm_args args;
	struct dlm_user_args *ua;
	int error;

	dlm_lock_recovery(ls);

	error = find_lkb(ls, lkid, &lkb);
	if (error)
		goto out;

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

	error = set_lock_args(mode, &ua->lksb, flags, 0, timeout_cs,
			      fake_astfn, ua, fake_bastfn, &args);
	if (error)
		goto out_put;

	error = convert_lock(ls, lkb, &args);

	if (error == -EINPROGRESS || error == -EAGAIN || error == -EDEADLK)
		error = 0;
 out_put:
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
		     unsigned long timeout_cs, uint32_t *lkid)
{
	struct dlm_lkb *lkb;
	struct dlm_user_args *ua;
	int found_other_mode = 0;
	int found = 0;
	int rv = 0;

	mutex_lock(&ls->ls_orphans_mutex);
	list_for_each_entry(lkb, &ls->ls_orphans, lkb_ownqueue) {
		if (lkb->lkb_resource->res_length != namelen)
			continue;
		if (memcmp(lkb->lkb_resource->res_name, name, namelen))
			continue;
		if (lkb->lkb_grmode != mode) {
			found_other_mode = 1;
			continue;
		}

		found = 1;
		list_del_init(&lkb->lkb_ownqueue);
		lkb->lkb_flags &= ~DLM_IFL_ORPHAN;
		*lkid = lkb->lkb_id;
		break;
	}
	mutex_unlock(&ls->ls_orphans_mutex);

	if (!found && found_other_mode) {
		rv = -EAGAIN;
		goto out;
	}

	if (!found) {
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

	spin_lock(&ua->proc->locks_spin);
	list_add_tail(&lkb->lkb_ownqueue, &ua->proc->locks);
	spin_unlock(&ua->proc->locks_spin);
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

	spin_lock(&ua->proc->locks_spin);
	/* dlm_user_add_cb() may have already taken lkb off the proc list */
	if (!list_empty(&lkb->lkb_ownqueue))
		list_move(&lkb->lkb_ownqueue, &ua->proc->unlocking);
	spin_unlock(&ua->proc->locks_spin);
 out_put:
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
	lkb->lkb_flags |= DLM_IFL_DEADLOCK_CANCEL;

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
	mutex_lock(&ls->ls_orphans_mutex);
	list_add_tail(&lkb->lkb_ownqueue, &ls->ls_orphans);
	mutex_unlock(&ls->ls_orphans_mutex);

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

	mutex_lock(&ls->ls_clear_proc_locks);
	if (list_empty(&proc->locks))
		goto out;

	lkb = list_entry(proc->locks.next, struct dlm_lkb, lkb_ownqueue);
	list_del_init(&lkb->lkb_ownqueue);

	if (lkb->lkb_exflags & DLM_LKF_PERSISTENT)
		lkb->lkb_flags |= DLM_IFL_ORPHAN;
	else
		lkb->lkb_flags |= DLM_IFL_DEAD;
 out:
	mutex_unlock(&ls->ls_clear_proc_locks);
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
	struct dlm_lkb *lkb, *safe;

	dlm_lock_recovery(ls);

	while (1) {
		lkb = del_proc_lock(ls, proc);
		if (!lkb)
			break;
		del_timeout(lkb);
		if (lkb->lkb_exflags & DLM_LKF_PERSISTENT)
			orphan_proc_lock(ls, lkb);
		else
			unlock_proc_lock(ls, lkb);

		/* this removes the reference for the proc->locks list
		   added by dlm_user_request, it may result in the lkb
		   being freed */

		dlm_put_lkb(lkb);
	}

	mutex_lock(&ls->ls_clear_proc_locks);

	/* in-progress unlocks */
	list_for_each_entry_safe(lkb, safe, &proc->unlocking, lkb_ownqueue) {
		list_del_init(&lkb->lkb_ownqueue);
		lkb->lkb_flags |= DLM_IFL_DEAD;
		dlm_put_lkb(lkb);
	}

	list_for_each_entry_safe(lkb, safe, &proc->asts, lkb_cb_list) {
		memset(&lkb->lkb_callbacks, 0,
		       sizeof(struct dlm_callback) * DLM_CALLBACKS_SIZE);
		list_del_init(&lkb->lkb_cb_list);
		dlm_put_lkb(lkb);
	}

	mutex_unlock(&ls->ls_clear_proc_locks);
	dlm_unlock_recovery(ls);
}

static void purge_proc_locks(struct dlm_ls *ls, struct dlm_user_proc *proc)
{
	struct dlm_lkb *lkb, *safe;

	while (1) {
		lkb = NULL;
		spin_lock(&proc->locks_spin);
		if (!list_empty(&proc->locks)) {
			lkb = list_entry(proc->locks.next, struct dlm_lkb,
					 lkb_ownqueue);
			list_del_init(&lkb->lkb_ownqueue);
		}
		spin_unlock(&proc->locks_spin);

		if (!lkb)
			break;

		lkb->lkb_flags |= DLM_IFL_DEAD;
		unlock_proc_lock(ls, lkb);
		dlm_put_lkb(lkb); /* ref from proc->locks list */
	}

	spin_lock(&proc->locks_spin);
	list_for_each_entry_safe(lkb, safe, &proc->unlocking, lkb_ownqueue) {
		list_del_init(&lkb->lkb_ownqueue);
		lkb->lkb_flags |= DLM_IFL_DEAD;
		dlm_put_lkb(lkb);
	}
	spin_unlock(&proc->locks_spin);

	spin_lock(&proc->asts_spin);
	list_for_each_entry_safe(lkb, safe, &proc->asts, lkb_cb_list) {
		memset(&lkb->lkb_callbacks, 0,
		       sizeof(struct dlm_callback) * DLM_CALLBACKS_SIZE);
		list_del_init(&lkb->lkb_cb_list);
		dlm_put_lkb(lkb);
	}
	spin_unlock(&proc->asts_spin);
}

/* pid of 0 means purge all orphans */

static void do_purge(struct dlm_ls *ls, int nodeid, int pid)
{
	struct dlm_lkb *lkb, *safe;

	mutex_lock(&ls->ls_orphans_mutex);
	list_for_each_entry_safe(lkb, safe, &ls->ls_orphans, lkb_ownqueue) {
		if (pid && lkb->lkb_ownpid != pid)
			continue;
		unlock_proc_lock(ls, lkb);
		list_del_init(&lkb->lkb_ownqueue);
		dlm_put_lkb(lkb);
	}
	mutex_unlock(&ls->ls_orphans_mutex);
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
	ms->m_nodeid = nodeid;
	ms->m_pid = pid;

	return send_message(mh, ms);
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

