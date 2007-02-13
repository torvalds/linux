/******************************************************************************
*******************************************************************************
**
**  Copyright (C) 2005 Red Hat, Inc.  All rights reserved.
**
**  This copyrighted material is made available to anyone wishing to use,
**  modify, copy, or redistribute it subject to the terms and conditions
**  of the GNU General Public License v.2.
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
static void __receive_convert_reply(struct dlm_rsb *r, struct dlm_lkb *lkb,
				    struct dlm_message *ms);
static int receive_extralen(struct dlm_message *ms);

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
	printk(KERN_ERR "lkb: nodeid %d id %x remid %x exflags %x flags %x\n"
	       "     status %d rqmode %d grmode %d wait_type %d ast_type %d\n",
	       lkb->lkb_nodeid, lkb->lkb_id, lkb->lkb_remid, lkb->lkb_exflags,
	       lkb->lkb_flags, lkb->lkb_status, lkb->lkb_rqmode,
	       lkb->lkb_grmode, lkb->lkb_wait_type, lkb->lkb_ast_type);
}

void dlm_print_rsb(struct dlm_rsb *r)
{
	printk(KERN_ERR "rsb: nodeid %d flags %lx first %x rlc %d name %s\n",
	       r->res_nodeid, r->res_flags, r->res_first_lkid,
	       r->res_recover_locks_count, r->res_name);
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

static inline void lock_recovery(struct dlm_ls *ls)
{
	down_read(&ls->ls_in_recovery);
}

static inline void unlock_recovery(struct dlm_ls *ls)
{
	up_read(&ls->ls_in_recovery);
}

static inline int lock_recovery_try(struct dlm_ls *ls)
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
	if (lkb->lkb_flags & DLM_IFL_MSTCPY)
		DLM_ASSERT(lkb->lkb_nodeid, dlm_print_lkb(lkb););
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

static void queue_cast(struct dlm_rsb *r, struct dlm_lkb *lkb, int rv)
{
	if (is_master_copy(lkb))
		return;

	DLM_ASSERT(lkb->lkb_lksb, dlm_print_lkb(lkb););

	lkb->lkb_lksb->sb_status = rv;
	lkb->lkb_lksb->sb_flags = lkb->lkb_sbflags;

	dlm_add_ast(lkb, AST_COMP);
}

static void queue_bast(struct dlm_rsb *r, struct dlm_lkb *lkb, int rqmode)
{
	if (is_master_copy(lkb))
		send_bast(r, lkb, rqmode);
	else {
		lkb->lkb_bastmode = rqmode;
		dlm_add_ast(lkb, AST_BAST);
	}
}

/*
 * Basic operations on rsb's and lkb's
 */

static struct dlm_rsb *create_rsb(struct dlm_ls *ls, char *name, int len)
{
	struct dlm_rsb *r;

	r = allocate_rsb(ls, len);
	if (!r)
		return NULL;

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

	return r;
}

static int search_rsb_list(struct list_head *head, char *name, int len,
			   unsigned int flags, struct dlm_rsb **r_ret)
{
	struct dlm_rsb *r;
	int error = 0;

	list_for_each_entry(r, head, res_hashchain) {
		if (len == r->res_length && !memcmp(name, r->res_name, len))
			goto found;
	}
	return -EBADR;

 found:
	if (r->res_nodeid && (flags & R_MASTER))
		error = -ENOTBLK;
	*r_ret = r;
	return error;
}

static int _search_rsb(struct dlm_ls *ls, char *name, int len, int b,
		       unsigned int flags, struct dlm_rsb **r_ret)
{
	struct dlm_rsb *r;
	int error;

	error = search_rsb_list(&ls->ls_rsbtbl[b].list, name, len, flags, &r);
	if (!error) {
		kref_get(&r->res_ref);
		goto out;
	}
	error = search_rsb_list(&ls->ls_rsbtbl[b].toss, name, len, flags, &r);
	if (error)
		goto out;

	list_move(&r->res_hashchain, &ls->ls_rsbtbl[b].list);

	if (dlm_no_directory(ls))
		goto out;

	if (r->res_nodeid == -1) {
		rsb_clear_flag(r, RSB_MASTER_UNCERTAIN);
		r->res_first_lkid = 0;
	} else if (r->res_nodeid > 0) {
		rsb_set_flag(r, RSB_MASTER_UNCERTAIN);
		r->res_first_lkid = 0;
	} else {
		DLM_ASSERT(r->res_nodeid == 0, dlm_print_rsb(r););
		DLM_ASSERT(!rsb_flag(r, RSB_MASTER_UNCERTAIN),);
	}
 out:
	*r_ret = r;
	return error;
}

static int search_rsb(struct dlm_ls *ls, char *name, int len, int b,
		      unsigned int flags, struct dlm_rsb **r_ret)
{
	int error;
	write_lock(&ls->ls_rsbtbl[b].lock);
	error = _search_rsb(ls, name, len, b, flags, r_ret);
	write_unlock(&ls->ls_rsbtbl[b].lock);
	return error;
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
 */

static int find_rsb(struct dlm_ls *ls, char *name, int namelen,
		    unsigned int flags, struct dlm_rsb **r_ret)
{
	struct dlm_rsb *r, *tmp;
	uint32_t hash, bucket;
	int error = 0;

	if (dlm_no_directory(ls))
		flags |= R_CREATE;

	hash = jhash(name, namelen, 0);
	bucket = hash & (ls->ls_rsbtbl_size - 1);

	error = search_rsb(ls, name, namelen, bucket, flags, &r);
	if (!error)
		goto out;

	if (error == -EBADR && !(flags & R_CREATE))
		goto out;

	/* the rsb was found but wasn't a master copy */
	if (error == -ENOTBLK)
		goto out;

	error = -ENOMEM;
	r = create_rsb(ls, name, namelen);
	if (!r)
		goto out;

	r->res_hash = hash;
	r->res_bucket = bucket;
	r->res_nodeid = -1;
	kref_init(&r->res_ref);

	/* With no directory, the master can be set immediately */
	if (dlm_no_directory(ls)) {
		int nodeid = dlm_dir_nodeid(r);
		if (nodeid == dlm_our_nodeid())
			nodeid = 0;
		r->res_nodeid = nodeid;
	}

	write_lock(&ls->ls_rsbtbl[bucket].lock);
	error = _search_rsb(ls, name, namelen, bucket, 0, &tmp);
	if (!error) {
		write_unlock(&ls->ls_rsbtbl[bucket].lock);
		free_rsb(r);
		r = tmp;
		goto out;
	}
	list_add(&r->res_hashchain, &ls->ls_rsbtbl[bucket].list);
	write_unlock(&ls->ls_rsbtbl[bucket].lock);
	error = 0;
 out:
	*r_ret = r;
	return error;
}

int dlm_find_rsb(struct dlm_ls *ls, char *name, int namelen,
		 unsigned int flags, struct dlm_rsb **r_ret)
{
	return find_rsb(ls, name, namelen, flags, r_ret);
}

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

static void toss_rsb(struct kref *kref)
{
	struct dlm_rsb *r = container_of(kref, struct dlm_rsb, res_ref);
	struct dlm_ls *ls = r->res_ls;

	DLM_ASSERT(list_empty(&r->res_root_list), dlm_print_rsb(r););
	kref_init(&r->res_ref);
	list_move(&r->res_hashchain, &ls->ls_rsbtbl[r->res_bucket].toss);
	r->res_toss_time = jiffies;
	if (r->res_lvbptr) {
		free_lvb(r->res_lvbptr);
		r->res_lvbptr = NULL;
	}
}

/* When all references to the rsb are gone it's transfered to
   the tossed list for later disposal. */

static void put_rsb(struct dlm_rsb *r)
{
	struct dlm_ls *ls = r->res_ls;
	uint32_t bucket = r->res_bucket;

	write_lock(&ls->ls_rsbtbl[bucket].lock);
	kref_put(&r->res_ref, toss_rsb);
	write_unlock(&ls->ls_rsbtbl[bucket].lock);
}

void dlm_put_rsb(struct dlm_rsb *r)
{
	put_rsb(r);
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
	struct dlm_lkb *lkb, *tmp;
	uint32_t lkid = 0;
	uint16_t bucket;

	lkb = allocate_lkb(ls);
	if (!lkb)
		return -ENOMEM;

	lkb->lkb_nodeid = -1;
	lkb->lkb_grmode = DLM_LOCK_IV;
	kref_init(&lkb->lkb_ref);
	INIT_LIST_HEAD(&lkb->lkb_ownqueue);

	get_random_bytes(&bucket, sizeof(bucket));
	bucket &= (ls->ls_lkbtbl_size - 1);

	write_lock(&ls->ls_lkbtbl[bucket].lock);

	/* counter can roll over so we must verify lkid is not in use */

	while (lkid == 0) {
		lkid = bucket | (ls->ls_lkbtbl[bucket].counter++ << 16);

		list_for_each_entry(tmp, &ls->ls_lkbtbl[bucket].list,
				    lkb_idtbl_list) {
			if (tmp->lkb_id != lkid)
				continue;
			lkid = 0;
			break;
		}
	}

	lkb->lkb_id = lkid;
	list_add(&lkb->lkb_idtbl_list, &ls->ls_lkbtbl[bucket].list);
	write_unlock(&ls->ls_lkbtbl[bucket].lock);

	*lkb_ret = lkb;
	return 0;
}

static struct dlm_lkb *__find_lkb(struct dlm_ls *ls, uint32_t lkid)
{
	uint16_t bucket = lkid & 0xFFFF;
	struct dlm_lkb *lkb;

	list_for_each_entry(lkb, &ls->ls_lkbtbl[bucket].list, lkb_idtbl_list) {
		if (lkb->lkb_id == lkid)
			return lkb;
	}
	return NULL;
}

static int find_lkb(struct dlm_ls *ls, uint32_t lkid, struct dlm_lkb **lkb_ret)
{
	struct dlm_lkb *lkb;
	uint16_t bucket = lkid & 0xFFFF;

	if (bucket >= ls->ls_lkbtbl_size)
		return -EBADSLT;

	read_lock(&ls->ls_lkbtbl[bucket].lock);
	lkb = __find_lkb(ls, lkid);
	if (lkb)
		kref_get(&lkb->lkb_ref);
	read_unlock(&ls->ls_lkbtbl[bucket].lock);

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
	uint16_t bucket = lkb->lkb_id & 0xFFFF;

	write_lock(&ls->ls_lkbtbl[bucket].lock);
	if (kref_put(&lkb->lkb_ref, kill_lkb)) {
		list_del(&lkb->lkb_idtbl_list);
		write_unlock(&ls->ls_lkbtbl[bucket].lock);

		detach_lkb(lkb);

		/* for local/process lkbs, lvbptr points to caller's lksb */
		if (lkb->lkb_lvbptr && is_master_copy(lkb))
			free_lvb(lkb->lkb_lvbptr);
		free_lkb(lkb);
		return 1;
	} else {
		write_unlock(&ls->ls_lkbtbl[bucket].lock);
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

	if (!lkb)
		list_add_tail(new, head);
	else
		__list_add(new, lkb->lkb_statequeue.prev, &lkb->lkb_statequeue);
}

/* add/remove lkb to rsb's grant/convert/wait queue */

static void add_lkb(struct dlm_rsb *r, struct dlm_lkb *lkb, int status)
{
	kref_get(&lkb->lkb_ref);

	DLM_ASSERT(!lkb->lkb_status, dlm_print_lkb(lkb););

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

/* add/remove lkb from global waiters list of lkb's waiting for
   a reply from a remote node */

static void add_to_waiters(struct dlm_lkb *lkb, int mstype)
{
	struct dlm_ls *ls = lkb->lkb_resource->res_ls;

	mutex_lock(&ls->ls_waiters_mutex);
	if (lkb->lkb_wait_type) {
		log_print("add_to_waiters error %d", lkb->lkb_wait_type);
		goto out;
	}
	lkb->lkb_wait_type = mstype;
	kref_get(&lkb->lkb_ref);
	list_add(&lkb->lkb_wait_reply, &ls->ls_waiters);
 out:
	mutex_unlock(&ls->ls_waiters_mutex);
}

/* We clear the RESEND flag because we might be taking an lkb off the waiters
   list as part of process_requestqueue (e.g. a lookup that has an optimized
   request reply on the requestqueue) between dlm_recover_waiters_pre() which
   set RESEND and dlm_recover_waiters_post() */

static int _remove_from_waiters(struct dlm_lkb *lkb)
{
	int error = 0;

	if (!lkb->lkb_wait_type) {
		log_print("remove_from_waiters error");
		error = -EINVAL;
		goto out;
	}
	lkb->lkb_wait_type = 0;
	lkb->lkb_flags &= ~DLM_IFL_RESEND;
	list_del(&lkb->lkb_wait_reply);
	unhold_lkb(lkb);
 out:
	return error;
}

static int remove_from_waiters(struct dlm_lkb *lkb)
{
	struct dlm_ls *ls = lkb->lkb_resource->res_ls;
	int error;

	mutex_lock(&ls->ls_waiters_mutex);
	error = _remove_from_waiters(lkb);
	mutex_unlock(&ls->ls_waiters_mutex);
	return error;
}

static void dir_remove(struct dlm_rsb *r)
{
	int to_nodeid;

	if (dlm_no_directory(r->res_ls))
		return;

	to_nodeid = dlm_dir_nodeid(r);
	if (to_nodeid != dlm_our_nodeid())
		send_remove(r);
	else
		dlm_dir_remove_entry(r->res_ls, to_nodeid,
				     r->res_name, r->res_length);
}

/* FIXME: shouldn't this be able to exit as soon as one non-due rsb is
   found since they are in order of newest to oldest? */

static int shrink_bucket(struct dlm_ls *ls, int b)
{
	struct dlm_rsb *r;
	int count = 0, found;

	for (;;) {
		found = 0;
		write_lock(&ls->ls_rsbtbl[b].lock);
		list_for_each_entry_reverse(r, &ls->ls_rsbtbl[b].toss,
					    res_hashchain) {
			if (!time_after_eq(jiffies, r->res_toss_time +
					   dlm_config.ci_toss_secs * HZ))
				continue;
			found = 1;
			break;
		}

		if (!found) {
			write_unlock(&ls->ls_rsbtbl[b].lock);
			break;
		}

		if (kref_put(&r->res_ref, kill_rsb)) {
			list_del(&r->res_hashchain);
			write_unlock(&ls->ls_rsbtbl[b].lock);

			if (is_master(r))
				dir_remove(r);
			free_rsb(r);
			count++;
		} else {
			write_unlock(&ls->ls_rsbtbl[b].lock);
			log_error(ls, "tossed rsb in use %s", r->res_name);
		}
	}

	return count;
}

void dlm_scan_rsbs(struct dlm_ls *ls)
{
	int i;

	if (dlm_locking_stopped(ls))
		return;

	for (i = 0; i < ls->ls_rsbtbl_size; i++) {
		shrink_bucket(ls, i);
		cond_resched();
	}
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
			r->res_lvbptr = allocate_lvb(r->res_ls);

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
		r->res_lvbptr = allocate_lvb(r->res_ls);

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

static void revert_lock(struct dlm_rsb *r, struct dlm_lkb *lkb)
{
	lkb->lkb_rqmode = DLM_LOCK_IV;

	switch (lkb->lkb_status) {
	case DLM_LKSTS_GRANTED:
		break;
	case DLM_LKSTS_CONVERT:
		move_lkb(r, lkb, DLM_LKSTS_GRANTED);
		break;
	case DLM_LKSTS_WAITING:
		del_lkb(r, lkb);
		lkb->lkb_grmode = DLM_LOCK_IV;
		/* this unhold undoes the original ref from create_lkb()
		   so this leads to the lkb being freed */
		unhold_lkb(lkb);
		break;
	default:
		log_print("invalid status for revert %d", lkb->lkb_status);
	}
}

static void revert_lock_pc(struct dlm_rsb *r, struct dlm_lkb *lkb)
{
	revert_lock(r, lkb);
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
}

static void grant_lock(struct dlm_rsb *r, struct dlm_lkb *lkb)
{
	set_lvb_lock(r, lkb);
	_grant_lock(r, lkb);
	lkb->lkb_highbast = 0;
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
 * Part 2: if the granted mode of lkb is preventing the first lkb in the
 * convert queue from being granted, then demote lkb (set grmode to NL).
 * This second form requires that we check for conv-deadlk even when
 * now == 0 in _can_be_granted().
 *
 * Example:
 * Granted Queue: empty
 * Convert Queue: NL->EX (first lock)
 *                PR->EX (second lock)
 *
 * The first lock can't be granted because of the granted mode of the second
 * lock and the second lock can't be granted because it's not first in the
 * list.  We demote the granted mode of the second lock (the lkb passed to this
 * function).
 *
 * After the resolution, the "grant pending" function needs to go back and try
 * to grant locks on the convert queue again since the first lock can now be
 * granted.
 */

static int conversion_deadlock_detect(struct dlm_rsb *rsb, struct dlm_lkb *lkb)
{
	struct dlm_lkb *this, *first = NULL, *self = NULL;

	list_for_each_entry(this, &rsb->res_convertqueue, lkb_statequeue) {
		if (!first)
			first = this;
		if (this == lkb) {
			self = lkb;
			continue;
		}

		if (!modes_compat(this, lkb) && !modes_compat(lkb, this))
			return 1;
	}

	/* if lkb is on the convert queue and is preventing the first
	   from being granted, then there's deadlock and we demote lkb.
	   multiple converting locks may need to do this before the first
	   converting lock can be granted. */

	if (self && self != first) {
		if (!modes_compat(lkb, first) &&
		    !queue_conflict(&rsb->res_grantqueue, first))
			return 1;
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
 * References are from chapter 6 of "VAXcluster Principles" by Roy Davis
 */

static int _can_be_granted(struct dlm_rsb *r, struct dlm_lkb *lkb, int now)
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
		goto out;

	/*
	 * 6-3: By default, a conversion request is immediately granted if the
	 * requested mode is compatible with the modes of all other granted
	 * locks
	 */

	if (queue_conflict(&r->res_convertqueue, lkb))
		goto out;

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

 out:
	/*
	 * The following, enabled by CONVDEADLK, departs from VMS.
	 */

	if (conv && (lkb->lkb_exflags & DLM_LKF_CONVDEADLK) &&
	    conversion_deadlock_detect(r, lkb)) {
		lkb->lkb_grmode = DLM_LOCK_NL;
		lkb->lkb_sbflags |= DLM_SBF_DEMOTED;
	}

	return 0;
}

/*
 * The ALTPR and ALTCW flags aren't traditional lock manager flags, but are a
 * simple way to provide a big optimization to applications that can use them.
 */

static int can_be_granted(struct dlm_rsb *r, struct dlm_lkb *lkb, int now)
{
	uint32_t flags = lkb->lkb_exflags;
	int rv;
	int8_t alt = 0, rqmode = lkb->lkb_rqmode;

	rv = _can_be_granted(r, lkb, now);
	if (rv)
		goto out;

	if (lkb->lkb_sbflags & DLM_SBF_DEMOTED)
		goto out;

	if (rqmode != DLM_LOCK_PR && flags & DLM_LKF_ALTPR)
		alt = DLM_LOCK_PR;
	else if (rqmode != DLM_LOCK_CW && flags & DLM_LKF_ALTCW)
		alt = DLM_LOCK_CW;

	if (alt) {
		lkb->lkb_rqmode = alt;
		rv = _can_be_granted(r, lkb, now);
		if (rv)
			lkb->lkb_sbflags |= DLM_SBF_ALTMODE;
		else
			lkb->lkb_rqmode = rqmode;
	}
 out:
	return rv;
}

static int grant_pending_convert(struct dlm_rsb *r, int high)
{
	struct dlm_lkb *lkb, *s;
	int hi, demoted, quit, grant_restart, demote_restart;

	quit = 0;
 restart:
	grant_restart = 0;
	demote_restart = 0;
	hi = DLM_LOCK_IV;

	list_for_each_entry_safe(lkb, s, &r->res_convertqueue, lkb_statequeue) {
		demoted = is_demoted(lkb);
		if (can_be_granted(r, lkb, 0)) {
			grant_lock_pending(r, lkb);
			grant_restart = 1;
		} else {
			hi = max_t(int, lkb->lkb_rqmode, hi);
			if (!demoted && is_demoted(lkb))
				demote_restart = 1;
		}
	}

	if (grant_restart)
		goto restart;
	if (demote_restart && !quit) {
		quit = 1;
		goto restart;
	}

	return max_t(int, high, hi);
}

static int grant_pending_wait(struct dlm_rsb *r, int high)
{
	struct dlm_lkb *lkb, *s;

	list_for_each_entry_safe(lkb, s, &r->res_waitqueue, lkb_statequeue) {
		if (can_be_granted(r, lkb, 0))
			grant_lock_pending(r, lkb);
                else
			high = max_t(int, lkb->lkb_rqmode, high);
	}

	return high;
}

static void grant_pending_locks(struct dlm_rsb *r)
{
	struct dlm_lkb *lkb, *s;
	int high = DLM_LOCK_IV;

	DLM_ASSERT(is_master(r), dlm_dump_rsb(r););

	high = grant_pending_convert(r, high);
	high = grant_pending_wait(r, high);

	if (high == DLM_LOCK_IV)
		return;

	/*
	 * If there are locks left on the wait/convert queue then send blocking
	 * ASTs to granted locks based on the largest requested mode (high)
	 * found above. FIXME: highbast < high comparison not valid for PR/CW.
	 */

	list_for_each_entry_safe(lkb, s, &r->res_grantqueue, lkb_statequeue) {
		if (lkb->lkb_bastaddr && (lkb->lkb_highbast < high) &&
		    !__dlm_compat_matrix[lkb->lkb_grmode+1][high+1]) {
			queue_bast(r, lkb, high);
			lkb->lkb_highbast = high;
		}
	}
}

static void send_bast_queue(struct dlm_rsb *r, struct list_head *head,
			    struct dlm_lkb *lkb)
{
	struct dlm_lkb *gr;

	list_for_each_entry(gr, head, lkb_statequeue) {
		if (gr->lkb_bastaddr &&
		    gr->lkb_highbast < lkb->lkb_rqmode &&
		    !modes_compat(gr, lkb)) {
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
	struct dlm_ls *ls = r->res_ls;
	int error, dir_nodeid, ret_nodeid, our_nodeid = dlm_our_nodeid();

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

	if (r->res_nodeid == 0) {
		lkb->lkb_nodeid = 0;
		return 0;
	}

	if (r->res_nodeid > 0) {
		lkb->lkb_nodeid = r->res_nodeid;
		return 0;
	}

	DLM_ASSERT(r->res_nodeid == -1, dlm_dump_rsb(r););

	dir_nodeid = dlm_dir_nodeid(r);

	if (dir_nodeid != our_nodeid) {
		r->res_first_lkid = lkb->lkb_id;
		send_lookup(r, lkb);
		return 1;
	}

	for (;;) {
		/* It's possible for dlm_scand to remove an old rsb for
		   this same resource from the toss list, us to create
		   a new one, look up the master locally, and find it
		   already exists just before dlm_scand does the
		   dir_remove() on the previous rsb. */

		error = dlm_dir_lookup(ls, our_nodeid, r->res_name,
				       r->res_length, &ret_nodeid);
		if (!error)
			break;
		log_debug(ls, "dir_lookup error %d %s", error, r->res_name);
		schedule();
	}

	if (ret_nodeid == our_nodeid) {
		r->res_first_lkid = 0;
		r->res_nodeid = 0;
		lkb->lkb_nodeid = 0;
	} else {
		r->res_first_lkid = lkb->lkb_id;
		r->res_nodeid = ret_nodeid;
		lkb->lkb_nodeid = ret_nodeid;
	}
	return 0;
}

static void process_lookup_list(struct dlm_rsb *r)
{
	struct dlm_lkb *lkb, *safe;

	list_for_each_entry_safe(lkb, safe, &r->res_lookup, lkb_rsb_lookup) {
		list_del(&lkb->lkb_rsb_lookup);
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
		/* the remote master didn't queue our NOQUEUE request;
		   make a waiting lkb the first_lkid */

		r->res_first_lkid = 0;

		if (!list_empty(&r->res_lookup)) {
			lkb = list_entry(r->res_lookup.next, struct dlm_lkb,
					 lkb_rsb_lookup);
			list_del(&lkb->lkb_rsb_lookup);
			r->res_first_lkid = lkb->lkb_id;
			_request_lock(r, lkb);
		} else
			r->res_nodeid = -1;
		break;

	default:
		log_error(r->res_ls, "confirm_master unknown error %d", error);
	}
}

static int set_lock_args(int mode, struct dlm_lksb *lksb, uint32_t flags,
			 int namelen, uint32_t parent_lkid, void *ast,
			 void *astarg, void *bast, struct dlm_args *args)
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

	/* parent/child locks not yet supported */
	if (parent_lkid)
		goto out;

	if (flags & DLM_LKF_CONVERT && !lksb->sb_lkid)
		goto out;

	/* these args will be copied to the lkb in validate_lock_args,
	   it cannot be done now because when converting locks, fields in
	   an active lkb cannot be modified before locking the rsb */

	args->flags = flags;
	args->astaddr = ast;
	args->astparam = (long) astarg;
	args->bastaddr = bast;
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

	args->flags = flags;
	args->astparam = (long) astarg;
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
	}

	lkb->lkb_exflags = args->flags;
	lkb->lkb_sbflags = 0;
	lkb->lkb_astaddr = args->astaddr;
	lkb->lkb_astparam = args->astparam;
	lkb->lkb_bastaddr = args->bastaddr;
	lkb->lkb_rqmode = args->mode;
	lkb->lkb_lksb = args->lksb;
	lkb->lkb_lvbptr = args->lksb->sb_lvbptr;
	lkb->lkb_ownpid = (int) current->pid;
	rv = 0;
 out:
	return rv;
}

static int validate_unlock_args(struct dlm_lkb *lkb, struct dlm_args *args)
{
	int rv = -EINVAL;

	if (lkb->lkb_flags & DLM_IFL_MSTCPY)
		goto out;

	if (args->flags & DLM_LKF_FORCEUNLOCK)
		goto out_ok;

	if (args->flags & DLM_LKF_CANCEL &&
	    lkb->lkb_status == DLM_LKSTS_GRANTED)
		goto out;

	if (!(args->flags & DLM_LKF_CANCEL) &&
	    lkb->lkb_status != DLM_LKSTS_GRANTED)
		goto out;

	rv = -EBUSY;
	if (lkb->lkb_wait_type)
		goto out;

 out_ok:
	lkb->lkb_exflags = args->flags;
	lkb->lkb_sbflags = 0;
	lkb->lkb_astparam = args->astparam;

	rv = 0;
 out:
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

	if (can_be_granted(r, lkb, 1)) {
		grant_lock(r, lkb);
		queue_cast(r, lkb, 0);
		goto out;
	}

	if (can_be_queued(lkb)) {
		error = -EINPROGRESS;
		add_lkb(r, lkb, DLM_LKSTS_WAITING);
		send_blocking_asts(r, lkb);
		goto out;
	}

	error = -EAGAIN;
	if (force_blocking_asts(lkb))
		send_blocking_asts_all(r, lkb);
	queue_cast(r, lkb, -EAGAIN);

 out:
	return error;
}

static int do_convert(struct dlm_rsb *r, struct dlm_lkb *lkb)
{
	int error = 0;

	/* changing an existing lock may allow others to be granted */

	if (can_be_granted(r, lkb, 1)) {
		grant_lock(r, lkb);
		queue_cast(r, lkb, 0);
		grant_pending_locks(r);
		goto out;
	}

	if (can_be_queued(lkb)) {
		if (is_demoted(lkb))
			grant_pending_locks(r);
		error = -EINPROGRESS;
		del_lkb(r, lkb);
		add_lkb(r, lkb, DLM_LKSTS_CONVERT);
		send_blocking_asts(r, lkb);
		goto out;
	}

	error = -EAGAIN;
	if (force_blocking_asts(lkb))
		send_blocking_asts_all(r, lkb);
	queue_cast(r, lkb, -EAGAIN);

 out:
	return error;
}

static int do_unlock(struct dlm_rsb *r, struct dlm_lkb *lkb)
{
	remove_lock(r, lkb);
	queue_cast(r, lkb, -DLM_EUNLOCK);
	grant_pending_locks(r);
	return -DLM_EUNLOCK;
}

/* FIXME: if revert_lock() finds that the lkb is granted, we should
   skip the queue_cast(ECANCEL).  It indicates that the request/convert
   completed (and queued a normal ast) just before the cancel; we don't
   want to clobber the sb_result for the normal ast with ECANCEL. */
 
static int do_cancel(struct dlm_rsb *r, struct dlm_lkb *lkb)
{
	revert_lock(r, lkb);
	queue_cast(r, lkb, -DLM_ECANCEL);
	grant_pending_locks(r);
	return -DLM_ECANCEL;
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

	if (is_remote(r))
		/* receive_request() calls do_request() on remote node */
		error = send_request(r, lkb);
	else
		error = do_request(r, lkb);
 out:
	return error;
}

/* change some property of an existing lkb, e.g. mode */

static int _convert_lock(struct dlm_rsb *r, struct dlm_lkb *lkb)
{
	int error;

	if (is_remote(r))
		/* receive_convert() calls do_convert() on remote node */
		error = send_convert(r, lkb);
	else
		error = do_convert(r, lkb);

	return error;
}

/* remove an existing lkb from the granted queue */

static int _unlock_lock(struct dlm_rsb *r, struct dlm_lkb *lkb)
{
	int error;

	if (is_remote(r))
		/* receive_unlock() calls do_unlock() on remote node */
		error = send_unlock(r, lkb);
	else
		error = do_unlock(r, lkb);

	return error;
}

/* remove an existing lkb from the convert or wait queue */

static int _cancel_lock(struct dlm_rsb *r, struct dlm_lkb *lkb)
{
	int error;

	if (is_remote(r))
		/* receive_cancel() calls do_cancel() on remote node */
		error = send_cancel(r, lkb);
	else
		error = do_cancel(r, lkb);

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
		goto out;

	error = find_rsb(ls, name, len, R_CREATE, &r);
	if (error)
		goto out;

	lock_rsb(r);

	attach_lkb(r, lkb);
	lkb->lkb_lksb->sb_lkid = lkb->lkb_id;

	error = _request_lock(r, lkb);

	unlock_rsb(r);
	put_rsb(r);

 out:
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

	lock_recovery(ls);

	if (convert)
		error = find_lkb(ls, lksb->sb_lkid, &lkb);
	else
		error = create_lkb(ls, &lkb);

	if (error)
		goto out;

	error = set_lock_args(mode, lksb, flags, namelen, parent_lkid, ast,
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
	if (error == -EAGAIN)
		error = 0;
 out:
	unlock_recovery(ls);
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

	lock_recovery(ls);

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
 out_put:
	dlm_put_lkb(lkb);
 out:
	unlock_recovery(ls);
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

static int create_message(struct dlm_rsb *r, struct dlm_lkb *lkb,
			  int to_nodeid, int mstype,
			  struct dlm_message **ms_ret,
			  struct dlm_mhandle **mh_ret)
{
	struct dlm_message *ms;
	struct dlm_mhandle *mh;
	char *mb;
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

	/* get_buffer gives us a message handle (mh) that we need to
	   pass into lowcomms_commit and a message buffer (mb) that we
	   write our data into */

	mh = dlm_lowcomms_get_buffer(to_nodeid, mb_len, GFP_KERNEL, &mb);
	if (!mh)
		return -ENOBUFS;

	memset(mb, 0, mb_len);

	ms = (struct dlm_message *) mb;

	ms->m_header.h_version = (DLM_HEADER_MAJOR | DLM_HEADER_MINOR);
	ms->m_header.h_lockspace = r->res_ls->ls_global_id;
	ms->m_header.h_nodeid = dlm_our_nodeid();
	ms->m_header.h_length = mb_len;
	ms->m_header.h_cmd = DLM_MSG;

	ms->m_type = mstype;

	*mh_ret = mh;
	*ms_ret = ms;
	return 0;
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

	if (lkb->lkb_bastaddr)
		ms->m_asts |= AST_BAST;
	if (lkb->lkb_astaddr)
		ms->m_asts |= AST_COMP;

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

	add_to_waiters(lkb, mstype);

	to_nodeid = r->res_nodeid;

	error = create_message(r, lkb, to_nodeid, mstype, &ms, &mh);
	if (error)
		goto fail;

	send_args(r, lkb, ms);

	error = send_message(mh, ms);
	if (error)
		goto fail;
	return 0;

 fail:
	remove_from_waiters(lkb);
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
		remove_from_waiters(lkb);
		r->res_ls->ls_stub_ms.m_result = 0;
		r->res_ls->ls_stub_ms.m_flags = lkb->lkb_flags;
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

	add_to_waiters(lkb, DLM_MSG_LOOKUP);

	to_nodeid = dlm_dir_nodeid(r);

	error = create_message(r, NULL, to_nodeid, DLM_MSG_LOOKUP, &ms, &mh);
	if (error)
		goto fail;

	send_args(r, lkb, ms);

	error = send_message(mh, ms);
	if (error)
		goto fail;
	return 0;

 fail:
	remove_from_waiters(lkb);
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
			lkb->lkb_lvbptr = allocate_lvb(ls);
		if (!lkb->lkb_lvbptr)
			return -ENOMEM;
		len = receive_extralen(ms);
		memcpy(lkb->lkb_lvbptr, ms->m_extra, len);
	}
	return 0;
}

static int receive_request_args(struct dlm_ls *ls, struct dlm_lkb *lkb,
				struct dlm_message *ms)
{
	lkb->lkb_nodeid = ms->m_header.h_nodeid;
	lkb->lkb_ownpid = ms->m_pid;
	lkb->lkb_remid = ms->m_lkid;
	lkb->lkb_grmode = DLM_LOCK_IV;
	lkb->lkb_rqmode = ms->m_rqmode;
	lkb->lkb_bastaddr = (void *) (long) (ms->m_asts & AST_BAST);
	lkb->lkb_astaddr = (void *) (long) (ms->m_asts & AST_COMP);

	DLM_ASSERT(is_master_copy(lkb), dlm_print_lkb(lkb););

	if (lkb->lkb_exflags & DLM_LKF_VALBLK) {
		/* lkb was just created so there won't be an lvb yet */
		lkb->lkb_lvbptr = allocate_lvb(ls);
		if (!lkb->lkb_lvbptr)
			return -ENOMEM;
	}

	return 0;
}

static int receive_convert_args(struct dlm_ls *ls, struct dlm_lkb *lkb,
				struct dlm_message *ms)
{
	if (lkb->lkb_nodeid != ms->m_header.h_nodeid) {
		log_error(ls, "convert_args nodeid %d %d lkid %x %x",
			  lkb->lkb_nodeid, ms->m_header.h_nodeid,
			  lkb->lkb_id, lkb->lkb_remid);
		return -EINVAL;
	}

	if (!is_master_copy(lkb))
		return -EINVAL;

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
	if (!is_master_copy(lkb))
		return -EINVAL;
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

static void receive_request(struct dlm_ls *ls, struct dlm_message *ms)
{
	struct dlm_lkb *lkb;
	struct dlm_rsb *r;
	int error, namelen;

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

	namelen = receive_extralen(ms);

	error = find_rsb(ls, ms->m_extra, namelen, R_MASTER, &r);
	if (error) {
		__put_lkb(ls, lkb);
		goto fail;
	}

	lock_rsb(r);

	attach_lkb(r, lkb);
	error = do_request(r, lkb);
	send_request_reply(r, lkb, error);

	unlock_rsb(r);
	put_rsb(r);

	if (error == -EINPROGRESS)
		error = 0;
	if (error)
		dlm_put_lkb(lkb);
	return;

 fail:
	setup_stub_lkb(ls, ms);
	send_request_reply(&ls->ls_stub_rsb, &ls->ls_stub_lkb, error);
}

static void receive_convert(struct dlm_ls *ls, struct dlm_message *ms)
{
	struct dlm_lkb *lkb;
	struct dlm_rsb *r;
	int error, reply = 1;

	error = find_lkb(ls, ms->m_remid, &lkb);
	if (error)
		goto fail;

	r = lkb->lkb_resource;

	hold_rsb(r);
	lock_rsb(r);

	receive_flags(lkb, ms);
	error = receive_convert_args(ls, lkb, ms);
	if (error)
		goto out;
	reply = !down_conversion(lkb);

	error = do_convert(r, lkb);
 out:
	if (reply)
		send_convert_reply(r, lkb, error);

	unlock_rsb(r);
	put_rsb(r);
	dlm_put_lkb(lkb);
	return;

 fail:
	setup_stub_lkb(ls, ms);
	send_convert_reply(&ls->ls_stub_rsb, &ls->ls_stub_lkb, error);
}

static void receive_unlock(struct dlm_ls *ls, struct dlm_message *ms)
{
	struct dlm_lkb *lkb;
	struct dlm_rsb *r;
	int error;

	error = find_lkb(ls, ms->m_remid, &lkb);
	if (error)
		goto fail;

	r = lkb->lkb_resource;

	hold_rsb(r);
	lock_rsb(r);

	receive_flags(lkb, ms);
	error = receive_unlock_args(ls, lkb, ms);
	if (error)
		goto out;

	error = do_unlock(r, lkb);
 out:
	send_unlock_reply(r, lkb, error);

	unlock_rsb(r);
	put_rsb(r);
	dlm_put_lkb(lkb);
	return;

 fail:
	setup_stub_lkb(ls, ms);
	send_unlock_reply(&ls->ls_stub_rsb, &ls->ls_stub_lkb, error);
}

static void receive_cancel(struct dlm_ls *ls, struct dlm_message *ms)
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

	error = do_cancel(r, lkb);
	send_cancel_reply(r, lkb, error);

	unlock_rsb(r);
	put_rsb(r);
	dlm_put_lkb(lkb);
	return;

 fail:
	setup_stub_lkb(ls, ms);
	send_cancel_reply(&ls->ls_stub_rsb, &ls->ls_stub_lkb, error);
}

static void receive_grant(struct dlm_ls *ls, struct dlm_message *ms)
{
	struct dlm_lkb *lkb;
	struct dlm_rsb *r;
	int error;

	error = find_lkb(ls, ms->m_remid, &lkb);
	if (error) {
		log_error(ls, "receive_grant no lkb");
		return;
	}
	DLM_ASSERT(is_process_copy(lkb), dlm_print_lkb(lkb););

	r = lkb->lkb_resource;

	hold_rsb(r);
	lock_rsb(r);

	receive_flags_reply(lkb, ms);
	grant_lock_pc(r, lkb, ms);
	queue_cast(r, lkb, 0);

	unlock_rsb(r);
	put_rsb(r);
	dlm_put_lkb(lkb);
}

static void receive_bast(struct dlm_ls *ls, struct dlm_message *ms)
{
	struct dlm_lkb *lkb;
	struct dlm_rsb *r;
	int error;

	error = find_lkb(ls, ms->m_remid, &lkb);
	if (error) {
		log_error(ls, "receive_bast no lkb");
		return;
	}
	DLM_ASSERT(is_process_copy(lkb), dlm_print_lkb(lkb););

	r = lkb->lkb_resource;

	hold_rsb(r);
	lock_rsb(r);

	queue_bast(r, lkb, ms->m_bastmode);

	unlock_rsb(r);
	put_rsb(r);
	dlm_put_lkb(lkb);
}

static void receive_lookup(struct dlm_ls *ls, struct dlm_message *ms)
{
	int len, error, ret_nodeid, dir_nodeid, from_nodeid, our_nodeid;

	from_nodeid = ms->m_header.h_nodeid;
	our_nodeid = dlm_our_nodeid();

	len = receive_extralen(ms);

	dir_nodeid = dlm_hash2nodeid(ls, ms->m_hash);
	if (dir_nodeid != our_nodeid) {
		log_error(ls, "lookup dir_nodeid %d from %d",
			  dir_nodeid, from_nodeid);
		error = -EINVAL;
		ret_nodeid = -1;
		goto out;
	}

	error = dlm_dir_lookup(ls, from_nodeid, ms->m_extra, len, &ret_nodeid);

	/* Optimization: we're master so treat lookup as a request */
	if (!error && ret_nodeid == our_nodeid) {
		receive_request(ls, ms);
		return;
	}
 out:
	send_lookup_reply(ls, ms, ret_nodeid, error);
}

static void receive_remove(struct dlm_ls *ls, struct dlm_message *ms)
{
	int len, dir_nodeid, from_nodeid;

	from_nodeid = ms->m_header.h_nodeid;

	len = receive_extralen(ms);

	dir_nodeid = dlm_hash2nodeid(ls, ms->m_hash);
	if (dir_nodeid != dlm_our_nodeid()) {
		log_error(ls, "remove dir entry dir_nodeid %d from %d",
			  dir_nodeid, from_nodeid);
		return;
	}

	dlm_dir_remove_entry(ls, from_nodeid, ms->m_extra, len);
}

static void receive_request_reply(struct dlm_ls *ls, struct dlm_message *ms)
{
	struct dlm_lkb *lkb;
	struct dlm_rsb *r;
	int error, mstype;

	error = find_lkb(ls, ms->m_remid, &lkb);
	if (error) {
		log_error(ls, "receive_request_reply no lkb");
		return;
	}
	DLM_ASSERT(is_process_copy(lkb), dlm_print_lkb(lkb););

	mstype = lkb->lkb_wait_type;
	error = remove_from_waiters(lkb);
	if (error) {
		log_error(ls, "receive_request_reply not on waiters");
		goto out;
	}

	/* this is the value returned from do_request() on the master */
	error = ms->m_result;

	r = lkb->lkb_resource;
	hold_rsb(r);
	lock_rsb(r);

	/* Optimization: the dir node was also the master, so it took our
	   lookup as a request and sent request reply instead of lookup reply */
	if (mstype == DLM_MSG_LOOKUP) {
		r->res_nodeid = ms->m_header.h_nodeid;
		lkb->lkb_nodeid = r->res_nodeid;
	}

	switch (error) {
	case -EAGAIN:
		/* request would block (be queued) on remote master;
		   the unhold undoes the original ref from create_lkb()
		   so it leads to the lkb being freed */
		queue_cast(r, lkb, -EAGAIN);
		confirm_master(r, -EAGAIN);
		unhold_lkb(lkb);
		break;

	case -EINPROGRESS:
	case 0:
		/* request was queued or granted on remote master */
		receive_flags_reply(lkb, ms);
		lkb->lkb_remid = ms->m_lkid;
		if (error)
			add_lkb(r, lkb, DLM_LKSTS_WAITING);
		else {
			grant_lock_pc(r, lkb, ms);
			queue_cast(r, lkb, 0);
		}
		confirm_master(r, error);
		break;

	case -EBADR:
	case -ENOTBLK:
		/* find_rsb failed to find rsb or rsb wasn't master */
		r->res_nodeid = -1;
		lkb->lkb_nodeid = -1;
		_request_lock(r, lkb);
		break;

	default:
		log_error(ls, "receive_request_reply error %d", error);
	}

	unlock_rsb(r);
	put_rsb(r);
 out:
	dlm_put_lkb(lkb);
}

static void __receive_convert_reply(struct dlm_rsb *r, struct dlm_lkb *lkb,
				    struct dlm_message *ms)
{
	int error = ms->m_result;

	/* this is the value returned from do_convert() on the master */

	switch (error) {
	case -EAGAIN:
		/* convert would block (be queued) on remote master */
		queue_cast(r, lkb, -EAGAIN);
		break;

	case -EINPROGRESS:
		/* convert was queued on remote master */
		del_lkb(r, lkb);
		add_lkb(r, lkb, DLM_LKSTS_CONVERT);
		break;

	case 0:
		/* convert was granted on remote master */
		receive_flags_reply(lkb, ms);
		grant_lock_pc(r, lkb, ms);
		queue_cast(r, lkb, 0);
		break;

	default:
		log_error(r->res_ls, "receive_convert_reply error %d", error);
	}
}

static void _receive_convert_reply(struct dlm_lkb *lkb, struct dlm_message *ms)
{
	struct dlm_rsb *r = lkb->lkb_resource;

	hold_rsb(r);
	lock_rsb(r);

	__receive_convert_reply(r, lkb, ms);

	unlock_rsb(r);
	put_rsb(r);
}

static void receive_convert_reply(struct dlm_ls *ls, struct dlm_message *ms)
{
	struct dlm_lkb *lkb;
	int error;

	error = find_lkb(ls, ms->m_remid, &lkb);
	if (error) {
		log_error(ls, "receive_convert_reply no lkb");
		return;
	}
	DLM_ASSERT(is_process_copy(lkb), dlm_print_lkb(lkb););

	error = remove_from_waiters(lkb);
	if (error) {
		log_error(ls, "receive_convert_reply not on waiters");
		goto out;
	}

	_receive_convert_reply(lkb, ms);
 out:
	dlm_put_lkb(lkb);
}

static void _receive_unlock_reply(struct dlm_lkb *lkb, struct dlm_message *ms)
{
	struct dlm_rsb *r = lkb->lkb_resource;
	int error = ms->m_result;

	hold_rsb(r);
	lock_rsb(r);

	/* this is the value returned from do_unlock() on the master */

	switch (error) {
	case -DLM_EUNLOCK:
		receive_flags_reply(lkb, ms);
		remove_lock_pc(r, lkb);
		queue_cast(r, lkb, -DLM_EUNLOCK);
		break;
	default:
		log_error(r->res_ls, "receive_unlock_reply error %d", error);
	}

	unlock_rsb(r);
	put_rsb(r);
}

static void receive_unlock_reply(struct dlm_ls *ls, struct dlm_message *ms)
{
	struct dlm_lkb *lkb;
	int error;

	error = find_lkb(ls, ms->m_remid, &lkb);
	if (error) {
		log_error(ls, "receive_unlock_reply no lkb");
		return;
	}
	DLM_ASSERT(is_process_copy(lkb), dlm_print_lkb(lkb););

	error = remove_from_waiters(lkb);
	if (error) {
		log_error(ls, "receive_unlock_reply not on waiters");
		goto out;
	}

	_receive_unlock_reply(lkb, ms);
 out:
	dlm_put_lkb(lkb);
}

static void _receive_cancel_reply(struct dlm_lkb *lkb, struct dlm_message *ms)
{
	struct dlm_rsb *r = lkb->lkb_resource;
	int error = ms->m_result;

	hold_rsb(r);
	lock_rsb(r);

	/* this is the value returned from do_cancel() on the master */

	switch (error) {
	case -DLM_ECANCEL:
		receive_flags_reply(lkb, ms);
		revert_lock_pc(r, lkb);
		queue_cast(r, lkb, -DLM_ECANCEL);
		break;
	default:
		log_error(r->res_ls, "receive_cancel_reply error %d", error);
	}

	unlock_rsb(r);
	put_rsb(r);
}

static void receive_cancel_reply(struct dlm_ls *ls, struct dlm_message *ms)
{
	struct dlm_lkb *lkb;
	int error;

	error = find_lkb(ls, ms->m_remid, &lkb);
	if (error) {
		log_error(ls, "receive_cancel_reply no lkb");
		return;
	}
	DLM_ASSERT(is_process_copy(lkb), dlm_print_lkb(lkb););

	error = remove_from_waiters(lkb);
	if (error) {
		log_error(ls, "receive_cancel_reply not on waiters");
		goto out;
	}

	_receive_cancel_reply(lkb, ms);
 out:
	dlm_put_lkb(lkb);
}

static void receive_lookup_reply(struct dlm_ls *ls, struct dlm_message *ms)
{
	struct dlm_lkb *lkb;
	struct dlm_rsb *r;
	int error, ret_nodeid;

	error = find_lkb(ls, ms->m_lkid, &lkb);
	if (error) {
		log_error(ls, "receive_lookup_reply no lkb");
		return;
	}

	error = remove_from_waiters(lkb);
	if (error) {
		log_error(ls, "receive_lookup_reply not on waiters");
		goto out;
	}

	/* this is the value returned by dlm_dir_lookup on dir node
	   FIXME: will a non-zero error ever be returned? */
	error = ms->m_result;

	r = lkb->lkb_resource;
	hold_rsb(r);
	lock_rsb(r);

	ret_nodeid = ms->m_nodeid;
	if (ret_nodeid == dlm_our_nodeid()) {
		r->res_nodeid = 0;
		ret_nodeid = 0;
		r->res_first_lkid = 0;
	} else {
		/* set_master() will copy res_nodeid to lkb_nodeid */
		r->res_nodeid = ret_nodeid;
	}

	_request_lock(r, lkb);

	if (!ret_nodeid)
		process_lookup_list(r);

	unlock_rsb(r);
	put_rsb(r);
 out:
	dlm_put_lkb(lkb);
}

int dlm_receive_message(struct dlm_header *hd, int nodeid, int recovery)
{
	struct dlm_message *ms = (struct dlm_message *) hd;
	struct dlm_ls *ls;
	int error = 0;

	if (!recovery)
		dlm_message_in(ms);

	ls = dlm_find_lockspace_global(hd->h_lockspace);
	if (!ls) {
		log_print("drop message %d from %d for unknown lockspace %d",
			  ms->m_type, nodeid, hd->h_lockspace);
		return -EINVAL;
	}

	/* recovery may have just ended leaving a bunch of backed-up requests
	   in the requestqueue; wait while dlm_recoverd clears them */

	if (!recovery)
		dlm_wait_requestqueue(ls);

	/* recovery may have just started while there were a bunch of
	   in-flight requests -- save them in requestqueue to be processed
	   after recovery.  we can't let dlm_recvd block on the recovery
	   lock.  if dlm_recoverd is calling this function to clear the
	   requestqueue, it needs to be interrupted (-EINTR) if another
	   recovery operation is starting. */

	while (1) {
		if (dlm_locking_stopped(ls)) {
			if (recovery) {
				error = -EINTR;
				goto out;
			}
			error = dlm_add_requestqueue(ls, nodeid, hd);
			if (error == -EAGAIN)
				continue;
			else {
				error = -EINTR;
				goto out;
			}
		}

		if (lock_recovery_try(ls))
			break;
		schedule();
	}

	switch (ms->m_type) {

	/* messages sent to a master node */

	case DLM_MSG_REQUEST:
		receive_request(ls, ms);
		break;

	case DLM_MSG_CONVERT:
		receive_convert(ls, ms);
		break;

	case DLM_MSG_UNLOCK:
		receive_unlock(ls, ms);
		break;

	case DLM_MSG_CANCEL:
		receive_cancel(ls, ms);
		break;

	/* messages sent from a master node (replies to above) */

	case DLM_MSG_REQUEST_REPLY:
		receive_request_reply(ls, ms);
		break;

	case DLM_MSG_CONVERT_REPLY:
		receive_convert_reply(ls, ms);
		break;

	case DLM_MSG_UNLOCK_REPLY:
		receive_unlock_reply(ls, ms);
		break;

	case DLM_MSG_CANCEL_REPLY:
		receive_cancel_reply(ls, ms);
		break;

	/* messages sent from a master node (only two types of async msg) */

	case DLM_MSG_GRANT:
		receive_grant(ls, ms);
		break;

	case DLM_MSG_BAST:
		receive_bast(ls, ms);
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

	default:
		log_error(ls, "unknown message type %d", ms->m_type);
	}

	unlock_recovery(ls);
 out:
	dlm_put_lockspace(ls);
	dlm_astd_wake();
	return error;
}


/*
 * Recovery related
 */

static void recover_convert_waiter(struct dlm_ls *ls, struct dlm_lkb *lkb)
{
	if (middle_conversion(lkb)) {
		hold_lkb(lkb);
		ls->ls_stub_ms.m_result = -EINPROGRESS;
		ls->ls_stub_ms.m_flags = lkb->lkb_flags;
		_remove_from_waiters(lkb);
		_receive_convert_reply(lkb, &ls->ls_stub_ms);

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

static int waiter_needs_recovery(struct dlm_ls *ls, struct dlm_lkb *lkb)
{
	if (dlm_is_removed(ls, lkb->lkb_nodeid))
		return 1;

	if (!dlm_no_directory(ls))
		return 0;

	if (dlm_dir_nodeid(lkb->lkb_resource) != lkb->lkb_nodeid)
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

	mutex_lock(&ls->ls_waiters_mutex);

	list_for_each_entry_safe(lkb, safe, &ls->ls_waiters, lkb_wait_reply) {
		log_debug(ls, "pre recover waiter lkid %x type %d flags %x",
			  lkb->lkb_id, lkb->lkb_wait_type, lkb->lkb_flags);

		/* all outstanding lookups, regardless of destination  will be
		   resent after recovery is done */

		if (lkb->lkb_wait_type == DLM_MSG_LOOKUP) {
			lkb->lkb_flags |= DLM_IFL_RESEND;
			continue;
		}

		if (!waiter_needs_recovery(ls, lkb))
			continue;

		switch (lkb->lkb_wait_type) {

		case DLM_MSG_REQUEST:
			lkb->lkb_flags |= DLM_IFL_RESEND;
			break;

		case DLM_MSG_CONVERT:
			recover_convert_waiter(ls, lkb);
			break;

		case DLM_MSG_UNLOCK:
			hold_lkb(lkb);
			ls->ls_stub_ms.m_result = -DLM_EUNLOCK;
			ls->ls_stub_ms.m_flags = lkb->lkb_flags;
			_remove_from_waiters(lkb);
			_receive_unlock_reply(lkb, &ls->ls_stub_ms);
			dlm_put_lkb(lkb);
			break;

		case DLM_MSG_CANCEL:
			hold_lkb(lkb);
			ls->ls_stub_ms.m_result = -DLM_ECANCEL;
			ls->ls_stub_ms.m_flags = lkb->lkb_flags;
			_remove_from_waiters(lkb);
			_receive_cancel_reply(lkb, &ls->ls_stub_ms);
			dlm_put_lkb(lkb);
			break;

		default:
			log_error(ls, "invalid lkb wait_type %d",
				  lkb->lkb_wait_type);
		}
		schedule();
	}
	mutex_unlock(&ls->ls_waiters_mutex);
}

static int remove_resend_waiter(struct dlm_ls *ls, struct dlm_lkb **lkb_ret)
{
	struct dlm_lkb *lkb;
	int rv = 0;

	mutex_lock(&ls->ls_waiters_mutex);
	list_for_each_entry(lkb, &ls->ls_waiters, lkb_wait_reply) {
		if (lkb->lkb_flags & DLM_IFL_RESEND) {
			rv = lkb->lkb_wait_type;
			_remove_from_waiters(lkb);
			lkb->lkb_flags &= ~DLM_IFL_RESEND;
			break;
		}
	}
	mutex_unlock(&ls->ls_waiters_mutex);

	if (!rv)
		lkb = NULL;
	*lkb_ret = lkb;
	return rv;
}

/* Deal with lookups and lkb's marked RESEND from _pre.  We may now be the
   master or dir-node for r.  Processing the lkb may result in it being placed
   back on waiters. */

int dlm_recover_waiters_post(struct dlm_ls *ls)
{
	struct dlm_lkb *lkb;
	struct dlm_rsb *r;
	int error = 0, mstype;

	while (1) {
		if (dlm_locking_stopped(ls)) {
			log_debug(ls, "recover_waiters_post aborted");
			error = -EINTR;
			break;
		}

		mstype = remove_resend_waiter(ls, &lkb);
		if (!mstype)
			break;

		r = lkb->lkb_resource;

		log_debug(ls, "recover_waiters_post %x type %d flags %x %s",
			  lkb->lkb_id, mstype, lkb->lkb_flags, r->res_name);

		switch (mstype) {

		case DLM_MSG_LOOKUP:
			hold_rsb(r);
			lock_rsb(r);
			_request_lock(r, lkb);
			if (is_master(r))
				confirm_master(r, 0);
			unlock_rsb(r);
			put_rsb(r);
			break;

		case DLM_MSG_REQUEST:
			hold_rsb(r);
			lock_rsb(r);
			_request_lock(r, lkb);
			if (is_master(r))
				confirm_master(r, 0);
			unlock_rsb(r);
			put_rsb(r);
			break;

		case DLM_MSG_CONVERT:
			hold_rsb(r);
			lock_rsb(r);
			_convert_lock(r, lkb);
			unlock_rsb(r);
			put_rsb(r);
			break;

		default:
			log_error(ls, "recover_waiters_post type %d", mstype);
		}
	}

	return error;
}

static void purge_queue(struct dlm_rsb *r, struct list_head *queue,
			int (*test)(struct dlm_ls *ls, struct dlm_lkb *lkb))
{
	struct dlm_ls *ls = r->res_ls;
	struct dlm_lkb *lkb, *safe;

	list_for_each_entry_safe(lkb, safe, queue, lkb_statequeue) {
		if (test(ls, lkb)) {
			rsb_set_flag(r, RSB_LOCKS_PURGED);
			del_lkb(r, lkb);
			/* this put should free the lkb */
			if (!dlm_put_lkb(lkb))
				log_error(ls, "purged lkb not released");
		}
	}
}

static int purge_dead_test(struct dlm_ls *ls, struct dlm_lkb *lkb)
{
	return (is_master_copy(lkb) && dlm_is_removed(ls, lkb->lkb_nodeid));
}

static int purge_mstcpy_test(struct dlm_ls *ls, struct dlm_lkb *lkb)
{
	return is_master_copy(lkb);
}

static void purge_dead_locks(struct dlm_rsb *r)
{
	purge_queue(r, &r->res_grantqueue, &purge_dead_test);
	purge_queue(r, &r->res_convertqueue, &purge_dead_test);
	purge_queue(r, &r->res_waitqueue, &purge_dead_test);
}

void dlm_purge_mstcpy_locks(struct dlm_rsb *r)
{
	purge_queue(r, &r->res_grantqueue, &purge_mstcpy_test);
	purge_queue(r, &r->res_convertqueue, &purge_mstcpy_test);
	purge_queue(r, &r->res_waitqueue, &purge_mstcpy_test);
}

/* Get rid of locks held by nodes that are gone. */

int dlm_purge_locks(struct dlm_ls *ls)
{
	struct dlm_rsb *r;

	log_debug(ls, "dlm_purge_locks");

	down_write(&ls->ls_root_sem);
	list_for_each_entry(r, &ls->ls_root_list, res_root_list) {
		hold_rsb(r);
		lock_rsb(r);
		if (is_master(r))
			purge_dead_locks(r);
		unlock_rsb(r);
		unhold_rsb(r);

		schedule();
	}
	up_write(&ls->ls_root_sem);

	return 0;
}

static struct dlm_rsb *find_purged_rsb(struct dlm_ls *ls, int bucket)
{
	struct dlm_rsb *r, *r_ret = NULL;

	read_lock(&ls->ls_rsbtbl[bucket].lock);
	list_for_each_entry(r, &ls->ls_rsbtbl[bucket].list, res_hashchain) {
		if (!rsb_flag(r, RSB_LOCKS_PURGED))
			continue;
		hold_rsb(r);
		rsb_clear_flag(r, RSB_LOCKS_PURGED);
		r_ret = r;
		break;
	}
	read_unlock(&ls->ls_rsbtbl[bucket].lock);
	return r_ret;
}

void dlm_grant_after_purge(struct dlm_ls *ls)
{
	struct dlm_rsb *r;
	int bucket = 0;

	while (1) {
		r = find_purged_rsb(ls, bucket);
		if (!r) {
			if (bucket == ls->ls_rsbtbl_size - 1)
				break;
			bucket++;
			continue;
		}
		lock_rsb(r);
		if (is_master(r)) {
			grant_pending_locks(r);
			confirm_master(r, 0);
		}
		unlock_rsb(r);
		put_rsb(r);
		schedule();
	}
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

static int receive_rcom_lock_args(struct dlm_ls *ls, struct dlm_lkb *lkb,
				  struct dlm_rsb *r, struct dlm_rcom *rc)
{
	struct rcom_lock *rl = (struct rcom_lock *) rc->rc_buf;
	int lvblen;

	lkb->lkb_nodeid = rc->rc_header.h_nodeid;
	lkb->lkb_ownpid = rl->rl_ownpid;
	lkb->lkb_remid = rl->rl_lkid;
	lkb->lkb_exflags = rl->rl_exflags;
	lkb->lkb_flags = rl->rl_flags & 0x0000FFFF;
	lkb->lkb_flags |= DLM_IFL_MSTCPY;
	lkb->lkb_lvbseq = rl->rl_lvbseq;
	lkb->lkb_rqmode = rl->rl_rqmode;
	lkb->lkb_grmode = rl->rl_grmode;
	/* don't set lkb_status because add_lkb wants to itself */

	lkb->lkb_bastaddr = (void *) (long) (rl->rl_asts & AST_BAST);
	lkb->lkb_astaddr = (void *) (long) (rl->rl_asts & AST_COMP);

	if (lkb->lkb_exflags & DLM_LKF_VALBLK) {
		lkb->lkb_lvbptr = allocate_lvb(ls);
		if (!lkb->lkb_lvbptr)
			return -ENOMEM;
		lvblen = rc->rc_header.h_length - sizeof(struct dlm_rcom) -
			 sizeof(struct rcom_lock);
		memcpy(lkb->lkb_lvbptr, rl->rl_lvb, lvblen);
	}

	/* Conversions between PR and CW (middle modes) need special handling.
	   The real granted mode of these converting locks cannot be determined
	   until all locks have been rebuilt on the rsb (recover_conversion) */

	if (rl->rl_wait_type == DLM_MSG_CONVERT && middle_conversion(lkb)) {
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

int dlm_recover_master_copy(struct dlm_ls *ls, struct dlm_rcom *rc)
{
	struct rcom_lock *rl = (struct rcom_lock *) rc->rc_buf;
	struct dlm_rsb *r;
	struct dlm_lkb *lkb;
	int error;

	if (rl->rl_parent_lkid) {
		error = -EOPNOTSUPP;
		goto out;
	}

	error = find_rsb(ls, rl->rl_name, rl->rl_namelen, R_MASTER, &r);
	if (error)
		goto out;

	lock_rsb(r);

	lkb = search_remid(r, rc->rc_header.h_nodeid, rl->rl_lkid);
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

 out_remid:
	/* this is the new value returned to the lock holder for
	   saving in its process-copy lkb */
	rl->rl_remid = lkb->lkb_id;

 out_unlock:
	unlock_rsb(r);
	put_rsb(r);
 out:
	if (error)
		log_print("recover_master_copy %d %x", error, rl->rl_lkid);
	rl->rl_result = error;
	return error;
}

int dlm_recover_process_copy(struct dlm_ls *ls, struct dlm_rcom *rc)
{
	struct rcom_lock *rl = (struct rcom_lock *) rc->rc_buf;
	struct dlm_rsb *r;
	struct dlm_lkb *lkb;
	int error;

	error = find_lkb(ls, rl->rl_lkid, &lkb);
	if (error) {
		log_error(ls, "recover_process_copy no lkid %x", rl->rl_lkid);
		return error;
	}

	DLM_ASSERT(is_process_copy(lkb), dlm_print_lkb(lkb););

	error = rl->rl_result;

	r = lkb->lkb_resource;
	hold_rsb(r);
	lock_rsb(r);

	switch (error) {
	case -EBADR:
		/* There's a chance the new master received our lock before
		   dlm_recover_master_reply(), this wouldn't happen if we did
		   a barrier between recover_masters and recover_locks. */
		log_debug(ls, "master copy not ready %x r %lx %s", lkb->lkb_id,
			  (unsigned long)r, r->res_name);
		dlm_send_rcom_lock(r, lkb);
		goto out;
	case -EEXIST:
		log_debug(ls, "master copy exists %x", lkb->lkb_id);
		/* fall through */
	case 0:
		lkb->lkb_remid = rl->rl_remid;
		break;
	default:
		log_error(ls, "dlm_recover_process_copy unknown error %d %x",
			  error, lkb->lkb_id);
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
		     uint32_t parent_lkid)
{
	struct dlm_lkb *lkb;
	struct dlm_args args;
	int error;

	lock_recovery(ls);

	error = create_lkb(ls, &lkb);
	if (error) {
		kfree(ua);
		goto out;
	}

	if (flags & DLM_LKF_VALBLK) {
		ua->lksb.sb_lvbptr = kzalloc(DLM_USER_LVB_LEN, GFP_KERNEL);
		if (!ua->lksb.sb_lvbptr) {
			kfree(ua);
			__put_lkb(ls, lkb);
			error = -ENOMEM;
			goto out;
		}
	}

	/* After ua is attached to lkb it will be freed by free_lkb().
	   When DLM_IFL_USER is set, the dlm knows that this is a userspace
	   lock and that lkb_astparam is the dlm_user_args structure. */

	error = set_lock_args(mode, &ua->lksb, flags, namelen, parent_lkid,
			      DLM_FAKE_USER_AST, ua, DLM_FAKE_USER_AST, &args);
	lkb->lkb_flags |= DLM_IFL_USER;
	ua->old_mode = DLM_LOCK_IV;

	if (error) {
		__put_lkb(ls, lkb);
		goto out;
	}

	error = request_lock(ls, lkb, name, namelen, &args);

	switch (error) {
	case 0:
		break;
	case -EINPROGRESS:
		error = 0;
		break;
	case -EAGAIN:
		error = 0;
		/* fall through */
	default:
		__put_lkb(ls, lkb);
		goto out;
	}

	/* add this new lkb to the per-process list of locks */
	spin_lock(&ua->proc->locks_spin);
	kref_get(&lkb->lkb_ref);
	list_add_tail(&lkb->lkb_ownqueue, &ua->proc->locks);
	spin_unlock(&ua->proc->locks_spin);
 out:
	unlock_recovery(ls);
	return error;
}

int dlm_user_convert(struct dlm_ls *ls, struct dlm_user_args *ua_tmp,
		     int mode, uint32_t flags, uint32_t lkid, char *lvb_in)
{
	struct dlm_lkb *lkb;
	struct dlm_args args;
	struct dlm_user_args *ua;
	int error;

	lock_recovery(ls);

	error = find_lkb(ls, lkid, &lkb);
	if (error)
		goto out;

	/* user can change the params on its lock when it converts it, or
	   add an lvb that didn't exist before */

	ua = (struct dlm_user_args *)lkb->lkb_astparam;

	if (flags & DLM_LKF_VALBLK && !ua->lksb.sb_lvbptr) {
		ua->lksb.sb_lvbptr = kzalloc(DLM_USER_LVB_LEN, GFP_KERNEL);
		if (!ua->lksb.sb_lvbptr) {
			error = -ENOMEM;
			goto out_put;
		}
	}
	if (lvb_in && ua->lksb.sb_lvbptr)
		memcpy(ua->lksb.sb_lvbptr, lvb_in, DLM_USER_LVB_LEN);

	ua->castparam = ua_tmp->castparam;
	ua->castaddr = ua_tmp->castaddr;
	ua->bastparam = ua_tmp->bastparam;
	ua->bastaddr = ua_tmp->bastaddr;
	ua->user_lksb = ua_tmp->user_lksb;
	ua->old_mode = lkb->lkb_grmode;

	error = set_lock_args(mode, &ua->lksb, flags, 0, 0, DLM_FAKE_USER_AST,
			      ua, DLM_FAKE_USER_AST, &args);
	if (error)
		goto out_put;

	error = convert_lock(ls, lkb, &args);

	if (error == -EINPROGRESS || error == -EAGAIN)
		error = 0;
 out_put:
	dlm_put_lkb(lkb);
 out:
	unlock_recovery(ls);
	kfree(ua_tmp);
	return error;
}

int dlm_user_unlock(struct dlm_ls *ls, struct dlm_user_args *ua_tmp,
		    uint32_t flags, uint32_t lkid, char *lvb_in)
{
	struct dlm_lkb *lkb;
	struct dlm_args args;
	struct dlm_user_args *ua;
	int error;

	lock_recovery(ls);

	error = find_lkb(ls, lkid, &lkb);
	if (error)
		goto out;

	ua = (struct dlm_user_args *)lkb->lkb_astparam;

	if (lvb_in && ua->lksb.sb_lvbptr)
		memcpy(ua->lksb.sb_lvbptr, lvb_in, DLM_USER_LVB_LEN);
	ua->castparam = ua_tmp->castparam;
	ua->user_lksb = ua_tmp->user_lksb;

	error = set_unlock_args(flags, ua, &args);
	if (error)
		goto out_put;

	error = unlock_lock(ls, lkb, &args);

	if (error == -DLM_EUNLOCK)
		error = 0;
	if (error)
		goto out_put;

	spin_lock(&ua->proc->locks_spin);
	/* dlm_user_add_ast() may have already taken lkb off the proc list */
	if (!list_empty(&lkb->lkb_ownqueue))
		list_move(&lkb->lkb_ownqueue, &ua->proc->unlocking);
	spin_unlock(&ua->proc->locks_spin);
 out_put:
	dlm_put_lkb(lkb);
 out:
	unlock_recovery(ls);
	return error;
}

int dlm_user_cancel(struct dlm_ls *ls, struct dlm_user_args *ua_tmp,
		    uint32_t flags, uint32_t lkid)
{
	struct dlm_lkb *lkb;
	struct dlm_args args;
	struct dlm_user_args *ua;
	int error;

	lock_recovery(ls);

	error = find_lkb(ls, lkid, &lkb);
	if (error)
		goto out;

	ua = (struct dlm_user_args *)lkb->lkb_astparam;
	ua->castparam = ua_tmp->castparam;
	ua->user_lksb = ua_tmp->user_lksb;

	error = set_unlock_args(flags, ua, &args);
	if (error)
		goto out_put;

	error = cancel_lock(ls, lkb, &args);

	if (error == -DLM_ECANCEL)
		error = 0;
	if (error)
		goto out_put;

	/* this lkb was removed from the WAITING queue */
	if (lkb->lkb_grmode == DLM_LOCK_IV) {
		spin_lock(&ua->proc->locks_spin);
		list_move(&lkb->lkb_ownqueue, &ua->proc->unlocking);
		spin_unlock(&ua->proc->locks_spin);
	}
 out_put:
	dlm_put_lkb(lkb);
 out:
	unlock_recovery(ls);
	return error;
}

static int orphan_proc_lock(struct dlm_ls *ls, struct dlm_lkb *lkb)
{
	struct dlm_user_args *ua = (struct dlm_user_args *)lkb->lkb_astparam;

	if (ua->lksb.sb_lvbptr)
		kfree(ua->lksb.sb_lvbptr);
	kfree(ua);
	lkb->lkb_astparam = (long)NULL;

	/* TODO: propogate to master if needed */
	return 0;
}

/* The force flag allows the unlock to go ahead even if the lkb isn't granted.
   Regardless of what rsb queue the lock is on, it's removed and freed. */

static int unlock_proc_lock(struct dlm_ls *ls, struct dlm_lkb *lkb)
{
	struct dlm_user_args *ua = (struct dlm_user_args *)lkb->lkb_astparam;
	struct dlm_args args;
	int error;

	/* FIXME: we need to handle the case where the lkb is in limbo
	   while the rsb is being looked up, currently we assert in
	   _unlock_lock/is_remote because rsb nodeid is -1. */

	set_unlock_args(DLM_LKF_FORCEUNLOCK, ua, &args);

	error = unlock_lock(ls, lkb, &args);
	if (error == -DLM_EUNLOCK)
		error = 0;
	return error;
}

/* The ls_clear_proc_locks mutex protects against dlm_user_add_asts() which
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

	lock_recovery(ls);
	mutex_lock(&ls->ls_clear_proc_locks);

	list_for_each_entry_safe(lkb, safe, &proc->locks, lkb_ownqueue) {
		list_del_init(&lkb->lkb_ownqueue);

		if (lkb->lkb_exflags & DLM_LKF_PERSISTENT) {
			lkb->lkb_flags |= DLM_IFL_ORPHAN;
			orphan_proc_lock(ls, lkb);
		} else {
			lkb->lkb_flags |= DLM_IFL_DEAD;
			unlock_proc_lock(ls, lkb);
		}

		/* this removes the reference for the proc->locks list
		   added by dlm_user_request, it may result in the lkb
		   being freed */

		dlm_put_lkb(lkb);
	}

	/* in-progress unlocks */
	list_for_each_entry_safe(lkb, safe, &proc->unlocking, lkb_ownqueue) {
		list_del_init(&lkb->lkb_ownqueue);
		lkb->lkb_flags |= DLM_IFL_DEAD;
		dlm_put_lkb(lkb);
	}

	list_for_each_entry_safe(lkb, safe, &proc->asts, lkb_astqueue) {
		list_del(&lkb->lkb_astqueue);
		dlm_put_lkb(lkb);
	}

	mutex_unlock(&ls->ls_clear_proc_locks);
	unlock_recovery(ls);
}

