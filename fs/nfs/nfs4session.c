/*
 * fs/nfs/nfs4session.c
 *
 * Copyright (c) 2012 Trond Myklebust <Trond.Myklebust@netapp.com>
 *
 */
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/printk.h>
#include <linux/slab.h>
#include <linux/sunrpc/sched.h>
#include <linux/sunrpc/bc_xprt.h>
#include <linux/nfs.h>
#include <linux/nfs4.h>
#include <linux/nfs_fs.h>
#include <linux/module.h>

#include "nfs4_fs.h"
#include "internal.h"
#include "nfs4session.h"
#include "callback.h"

#define NFSDBG_FACILITY		NFSDBG_STATE

static void nfs4_init_slot_table(struct nfs4_slot_table *tbl, const char *queue)
{
	tbl->highest_used_slotid = NFS4_NO_SLOT;
	spin_lock_init(&tbl->slot_tbl_lock);
	rpc_init_priority_wait_queue(&tbl->slot_tbl_waitq, queue);
	init_completion(&tbl->complete);
}

/*
 * nfs4_shrink_slot_table - free retired slots from the slot table
 */
static void nfs4_shrink_slot_table(struct nfs4_slot_table  *tbl, u32 newsize)
{
	struct nfs4_slot **p;
	if (newsize >= tbl->max_slots)
		return;

	p = &tbl->slots;
	while (newsize--)
		p = &(*p)->next;
	while (*p) {
		struct nfs4_slot *slot = *p;

		*p = slot->next;
		kfree(slot);
		tbl->max_slots--;
	}
}

/**
 * nfs4_slot_tbl_drain_complete - wake waiters when drain is complete
 * @tbl - controlling slot table
 *
 */
void nfs4_slot_tbl_drain_complete(struct nfs4_slot_table *tbl)
{
	if (nfs4_slot_tbl_draining(tbl))
		complete(&tbl->complete);
}

/*
 * nfs4_free_slot - free a slot and efficiently update slot table.
 *
 * freeing a slot is trivially done by clearing its respective bit
 * in the bitmap.
 * If the freed slotid equals highest_used_slotid we want to update it
 * so that the server would be able to size down the slot table if needed,
 * otherwise we know that the highest_used_slotid is still in use.
 * When updating highest_used_slotid there may be "holes" in the bitmap
 * so we need to scan down from highest_used_slotid to 0 looking for the now
 * highest slotid in use.
 * If none found, highest_used_slotid is set to NFS4_NO_SLOT.
 *
 * Must be called while holding tbl->slot_tbl_lock
 */
void nfs4_free_slot(struct nfs4_slot_table *tbl, struct nfs4_slot *slot)
{
	u32 slotid = slot->slot_nr;

	/* clear used bit in bitmap */
	__clear_bit(slotid, tbl->used_slots);

	/* update highest_used_slotid when it is freed */
	if (slotid == tbl->highest_used_slotid) {
		u32 new_max = find_last_bit(tbl->used_slots, slotid);
		if (new_max < slotid)
			tbl->highest_used_slotid = new_max;
		else {
			tbl->highest_used_slotid = NFS4_NO_SLOT;
			nfs4_slot_tbl_drain_complete(tbl);
		}
	}
	dprintk("%s: slotid %u highest_used_slotid %u\n", __func__,
		slotid, tbl->highest_used_slotid);
}

static struct nfs4_slot *nfs4_new_slot(struct nfs4_slot_table  *tbl,
		u32 slotid, u32 seq_init, gfp_t gfp_mask)
{
	struct nfs4_slot *slot;

	slot = kzalloc(sizeof(*slot), gfp_mask);
	if (slot) {
		slot->table = tbl;
		slot->slot_nr = slotid;
		slot->seq_nr = seq_init;
	}
	return slot;
}

static struct nfs4_slot *nfs4_find_or_create_slot(struct nfs4_slot_table  *tbl,
		u32 slotid, u32 seq_init, gfp_t gfp_mask)
{
	struct nfs4_slot **p, *slot;

	p = &tbl->slots;
	for (;;) {
		if (*p == NULL) {
			*p = nfs4_new_slot(tbl, tbl->max_slots,
					seq_init, gfp_mask);
			if (*p == NULL)
				break;
			tbl->max_slots++;
		}
		slot = *p;
		if (slot->slot_nr == slotid)
			return slot;
		p = &slot->next;
	}
	return ERR_PTR(-ENOMEM);
}

/*
 * nfs4_alloc_slot - efficiently look for a free slot
 *
 * nfs4_alloc_slot looks for an unset bit in the used_slots bitmap.
 * If found, we mark the slot as used, update the highest_used_slotid,
 * and respectively set up the sequence operation args.
 *
 * Note: must be called with under the slot_tbl_lock.
 */
struct nfs4_slot *nfs4_alloc_slot(struct nfs4_slot_table *tbl)
{
	struct nfs4_slot *ret = ERR_PTR(-EBUSY);
	u32 slotid;

	dprintk("--> %s used_slots=%04lx highest_used=%u max_slots=%u\n",
		__func__, tbl->used_slots[0], tbl->highest_used_slotid,
		tbl->max_slotid + 1);
	slotid = find_first_zero_bit(tbl->used_slots, tbl->max_slotid + 1);
	if (slotid > tbl->max_slotid)
		goto out;
	ret = nfs4_find_or_create_slot(tbl, slotid, 1, GFP_NOWAIT);
	if (IS_ERR(ret))
		goto out;
	__set_bit(slotid, tbl->used_slots);
	if (slotid > tbl->highest_used_slotid ||
			tbl->highest_used_slotid == NFS4_NO_SLOT)
		tbl->highest_used_slotid = slotid;
	ret->generation = tbl->generation;

out:
	dprintk("<-- %s used_slots=%04lx highest_used=%u slotid=%u\n",
		__func__, tbl->used_slots[0], tbl->highest_used_slotid,
		!IS_ERR(ret) ? ret->slot_nr : NFS4_NO_SLOT);
	return ret;
}

static int nfs4_grow_slot_table(struct nfs4_slot_table *tbl,
		 u32 max_reqs, u32 ivalue)
{
	if (max_reqs <= tbl->max_slots)
		return 0;
	if (!IS_ERR(nfs4_find_or_create_slot(tbl, max_reqs - 1, ivalue, GFP_NOFS)))
		return 0;
	return -ENOMEM;
}

static void nfs4_reset_slot_table(struct nfs4_slot_table *tbl,
		u32 server_highest_slotid,
		u32 ivalue)
{
	struct nfs4_slot **p;

	nfs4_shrink_slot_table(tbl, server_highest_slotid + 1);
	p = &tbl->slots;
	while (*p) {
		(*p)->seq_nr = ivalue;
		(*p)->interrupted = 0;
		p = &(*p)->next;
	}
	tbl->highest_used_slotid = NFS4_NO_SLOT;
	tbl->target_highest_slotid = server_highest_slotid;
	tbl->server_highest_slotid = server_highest_slotid;
	tbl->d_target_highest_slotid = 0;
	tbl->d2_target_highest_slotid = 0;
	tbl->max_slotid = server_highest_slotid;
}

/*
 * (re)Initialise a slot table
 */
static int nfs4_realloc_slot_table(struct nfs4_slot_table *tbl,
		u32 max_reqs, u32 ivalue)
{
	int ret;

	dprintk("--> %s: max_reqs=%u, tbl->max_slots %u\n", __func__,
		max_reqs, tbl->max_slots);

	if (max_reqs > NFS4_MAX_SLOT_TABLE)
		max_reqs = NFS4_MAX_SLOT_TABLE;

	ret = nfs4_grow_slot_table(tbl, max_reqs, ivalue);
	if (ret)
		goto out;

	spin_lock(&tbl->slot_tbl_lock);
	nfs4_reset_slot_table(tbl, max_reqs - 1, ivalue);
	spin_unlock(&tbl->slot_tbl_lock);

	dprintk("%s: tbl=%p slots=%p max_slots=%u\n", __func__,
		tbl, tbl->slots, tbl->max_slots);
out:
	dprintk("<-- %s: return %d\n", __func__, ret);
	return ret;
}

/**
 * nfs4_release_slot_table - release resources attached to a slot table
 * @tbl: slot table to shut down
 *
 */
void nfs4_release_slot_table(struct nfs4_slot_table *tbl)
{
	nfs4_shrink_slot_table(tbl, 0);
}

/**
 * nfs4_setup_slot_table - prepare a stand-alone slot table for use
 * @tbl: slot table to set up
 * @max_reqs: maximum number of requests allowed
 * @queue: name to give RPC wait queue
 *
 * Returns zero on success, or a negative errno.
 */
int nfs4_setup_slot_table(struct nfs4_slot_table *tbl, unsigned int max_reqs,
		const char *queue)
{
	nfs4_init_slot_table(tbl, queue);
	return nfs4_realloc_slot_table(tbl, max_reqs, 0);
}

static bool nfs41_assign_slot(struct rpc_task *task, void *pslot)
{
	struct nfs4_sequence_args *args = task->tk_msg.rpc_argp;
	struct nfs4_sequence_res *res = task->tk_msg.rpc_resp;
	struct nfs4_slot *slot = pslot;
	struct nfs4_slot_table *tbl = slot->table;

	if (nfs4_slot_tbl_draining(tbl) && !args->sa_privileged)
		return false;
	slot->generation = tbl->generation;
	args->sa_slot = slot;
	res->sr_timestamp = jiffies;
	res->sr_slot = slot;
	res->sr_status_flags = 0;
	res->sr_status = 1;
	return true;
}

static bool __nfs41_wake_and_assign_slot(struct nfs4_slot_table *tbl,
		struct nfs4_slot *slot)
{
	if (rpc_wake_up_first(&tbl->slot_tbl_waitq, nfs41_assign_slot, slot))
		return true;
	return false;
}

bool nfs41_wake_and_assign_slot(struct nfs4_slot_table *tbl,
		struct nfs4_slot *slot)
{
	if (slot->slot_nr > tbl->max_slotid)
		return false;
	return __nfs41_wake_and_assign_slot(tbl, slot);
}

static bool nfs41_try_wake_next_slot_table_entry(struct nfs4_slot_table *tbl)
{
	struct nfs4_slot *slot = nfs4_alloc_slot(tbl);
	if (!IS_ERR(slot)) {
		bool ret = __nfs41_wake_and_assign_slot(tbl, slot);
		if (ret)
			return ret;
		nfs4_free_slot(tbl, slot);
	}
	return false;
}

void nfs41_wake_slot_table(struct nfs4_slot_table *tbl)
{
	for (;;) {
		if (!nfs41_try_wake_next_slot_table_entry(tbl))
			break;
	}
}

#if defined(CONFIG_NFS_V4_1)

static void nfs41_set_max_slotid_locked(struct nfs4_slot_table *tbl,
		u32 target_highest_slotid)
{
	u32 max_slotid;

	max_slotid = min(NFS4_MAX_SLOT_TABLE - 1, target_highest_slotid);
	if (max_slotid > tbl->server_highest_slotid)
		max_slotid = tbl->server_highest_slotid;
	if (max_slotid > tbl->target_highest_slotid)
		max_slotid = tbl->target_highest_slotid;
	tbl->max_slotid = max_slotid;
	nfs41_wake_slot_table(tbl);
}

/* Update the client's idea of target_highest_slotid */
static void nfs41_set_target_slotid_locked(struct nfs4_slot_table *tbl,
		u32 target_highest_slotid)
{
	if (tbl->target_highest_slotid == target_highest_slotid)
		return;
	tbl->target_highest_slotid = target_highest_slotid;
	tbl->generation++;
}

void nfs41_set_target_slotid(struct nfs4_slot_table *tbl,
		u32 target_highest_slotid)
{
	spin_lock(&tbl->slot_tbl_lock);
	nfs41_set_target_slotid_locked(tbl, target_highest_slotid);
	tbl->d_target_highest_slotid = 0;
	tbl->d2_target_highest_slotid = 0;
	nfs41_set_max_slotid_locked(tbl, target_highest_slotid);
	spin_unlock(&tbl->slot_tbl_lock);
}

static void nfs41_set_server_slotid_locked(struct nfs4_slot_table *tbl,
		u32 highest_slotid)
{
	if (tbl->server_highest_slotid == highest_slotid)
		return;
	if (tbl->highest_used_slotid > highest_slotid)
		return;
	/* Deallocate slots */
	nfs4_shrink_slot_table(tbl, highest_slotid + 1);
	tbl->server_highest_slotid = highest_slotid;
}

static s32 nfs41_derivative_target_slotid(s32 s1, s32 s2)
{
	s1 -= s2;
	if (s1 == 0)
		return 0;
	if (s1 < 0)
		return (s1 - 1) >> 1;
	return (s1 + 1) >> 1;
}

static int nfs41_sign_s32(s32 s1)
{
	if (s1 > 0)
		return 1;
	if (s1 < 0)
		return -1;
	return 0;
}

static bool nfs41_same_sign_or_zero_s32(s32 s1, s32 s2)
{
	if (!s1 || !s2)
		return true;
	return nfs41_sign_s32(s1) == nfs41_sign_s32(s2);
}

/* Try to eliminate outliers by checking for sharp changes in the
 * derivatives and second derivatives
 */
static bool nfs41_is_outlier_target_slotid(struct nfs4_slot_table *tbl,
		u32 new_target)
{
	s32 d_target, d2_target;
	bool ret = true;

	d_target = nfs41_derivative_target_slotid(new_target,
			tbl->target_highest_slotid);
	d2_target = nfs41_derivative_target_slotid(d_target,
			tbl->d_target_highest_slotid);
	/* Is first derivative same sign? */
	if (nfs41_same_sign_or_zero_s32(d_target, tbl->d_target_highest_slotid))
		ret = false;
	/* Is second derivative same sign? */
	if (nfs41_same_sign_or_zero_s32(d2_target, tbl->d2_target_highest_slotid))
		ret = false;
	tbl->d_target_highest_slotid = d_target;
	tbl->d2_target_highest_slotid = d2_target;
	return ret;
}

void nfs41_update_target_slotid(struct nfs4_slot_table *tbl,
		struct nfs4_slot *slot,
		struct nfs4_sequence_res *res)
{
	spin_lock(&tbl->slot_tbl_lock);
	if (!nfs41_is_outlier_target_slotid(tbl, res->sr_target_highest_slotid))
		nfs41_set_target_slotid_locked(tbl, res->sr_target_highest_slotid);
	if (tbl->generation == slot->generation)
		nfs41_set_server_slotid_locked(tbl, res->sr_highest_slotid);
	nfs41_set_max_slotid_locked(tbl, res->sr_target_highest_slotid);
	spin_unlock(&tbl->slot_tbl_lock);
}

static void nfs4_destroy_session_slot_tables(struct nfs4_session *session)
{
	nfs4_release_slot_table(&session->fc_slot_table);
	nfs4_release_slot_table(&session->bc_slot_table);
}

/*
 * Initialize or reset the forechannel and backchannel tables
 */
int nfs4_setup_session_slot_tables(struct nfs4_session *ses)
{
	struct nfs4_slot_table *tbl;
	int status;

	dprintk("--> %s\n", __func__);
	/* Fore channel */
	tbl = &ses->fc_slot_table;
	tbl->session = ses;
	status = nfs4_realloc_slot_table(tbl, ses->fc_attrs.max_reqs, 1);
	if (status) /* -ENOMEM */
		return status;
	/* Back channel */
	tbl = &ses->bc_slot_table;
	tbl->session = ses;
	status = nfs4_realloc_slot_table(tbl, ses->bc_attrs.max_reqs, 0);
	if (status && tbl->slots == NULL)
		/* Fore and back channel share a connection so get
		 * both slot tables or neither */
		nfs4_destroy_session_slot_tables(ses);
	return status;
}

struct nfs4_session *nfs4_alloc_session(struct nfs_client *clp)
{
	struct nfs4_session *session;

	session = kzalloc(sizeof(struct nfs4_session), GFP_NOFS);
	if (!session)
		return NULL;

	nfs4_init_slot_table(&session->fc_slot_table, "ForeChannel Slot table");
	nfs4_init_slot_table(&session->bc_slot_table, "BackChannel Slot table");
	session->session_state = 1<<NFS4_SESSION_INITING;

	session->clp = clp;
	return session;
}

void nfs4_destroy_session(struct nfs4_session *session)
{
	struct rpc_xprt *xprt;
	struct rpc_cred *cred;

	cred = nfs4_get_clid_cred(session->clp);
	nfs4_proc_destroy_session(session, cred);
	if (cred)
		put_rpccred(cred);

	rcu_read_lock();
	xprt = rcu_dereference(session->clp->cl_rpcclient->cl_xprt);
	rcu_read_unlock();
	dprintk("%s Destroy backchannel for xprt %p\n",
		__func__, xprt);
	xprt_destroy_backchannel(xprt, NFS41_BC_MIN_CALLBACKS);
	nfs4_destroy_session_slot_tables(session);
	kfree(session);
}

/*
 * With sessions, the client is not marked ready until after a
 * successful EXCHANGE_ID and CREATE_SESSION.
 *
 * Map errors cl_cons_state errors to EPROTONOSUPPORT to indicate
 * other versions of NFS can be tried.
 */
static int nfs41_check_session_ready(struct nfs_client *clp)
{
	int ret;
	
	if (clp->cl_cons_state == NFS_CS_SESSION_INITING) {
		ret = nfs4_client_recover_expired_lease(clp);
		if (ret)
			return ret;
	}
	if (clp->cl_cons_state < NFS_CS_READY)
		return -EPROTONOSUPPORT;
	smp_rmb();
	return 0;
}

int nfs4_init_session(struct nfs_client *clp)
{
	if (!nfs4_has_session(clp))
		return 0;

	clear_bit(NFS4_SESSION_INITING, &clp->cl_session->session_state);
	return nfs41_check_session_ready(clp);
}

int nfs4_init_ds_session(struct nfs_client *clp, unsigned long lease_time)
{
	struct nfs4_session *session = clp->cl_session;
	int ret;

	spin_lock(&clp->cl_lock);
	if (test_and_clear_bit(NFS4_SESSION_INITING, &session->session_state)) {
		/*
		 * Do not set NFS_CS_CHECK_LEASE_TIME instead set the
		 * DS lease to be equal to the MDS lease.
		 */
		clp->cl_lease_time = lease_time;
		clp->cl_last_renewal = jiffies;
	}
	spin_unlock(&clp->cl_lock);

	ret = nfs41_check_session_ready(clp);
	if (ret)
		return ret;
	/* Test for the DS role */
	if (!is_ds_client(clp))
		return -ENODEV;
	return 0;
}
EXPORT_SYMBOL_GPL(nfs4_init_ds_session);

#endif	/* defined(CONFIG_NFS_V4_1) */
