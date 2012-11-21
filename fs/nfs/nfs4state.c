/*
 *  fs/nfs/nfs4state.c
 *
 *  Client-side XDR for NFSv4.
 *
 *  Copyright (c) 2002 The Regents of the University of Michigan.
 *  All rights reserved.
 *
 *  Kendrick Smith <kmsmith@umich.edu>
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions
 *  are met:
 *
 *  1. Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *  2. Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *  3. Neither the name of the University nor the names of its
 *     contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 *
 *  THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESS OR IMPLIED
 *  WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 *  MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 *  DISCLAIMED. IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 *  FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 *  CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 *  SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 *  BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 *  LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 *  NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 *  SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * Implementation of the NFSv4 state model.  For the time being,
 * this is minimal, but will be made much more complex in a
 * subsequent patch.
 */

#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/nfs_fs.h>
#include <linux/nfs_idmap.h>
#include <linux/kthread.h>
#include <linux/module.h>
#include <linux/random.h>
#include <linux/ratelimit.h>
#include <linux/workqueue.h>
#include <linux/bitops.h>
#include <linux/jiffies.h>

#include <linux/sunrpc/clnt.h>

#include "nfs4_fs.h"
#include "callback.h"
#include "delegation.h"
#include "internal.h"
#include "pnfs.h"
#include "netns.h"

#define NFSDBG_FACILITY		NFSDBG_STATE

#define OPENOWNER_POOL_SIZE	8

const nfs4_stateid zero_stateid;
static DEFINE_MUTEX(nfs_clid_init_mutex);
static LIST_HEAD(nfs4_clientid_list);

int nfs4_init_clientid(struct nfs_client *clp, struct rpc_cred *cred)
{
	struct nfs4_setclientid_res clid = {
		.clientid = clp->cl_clientid,
		.confirm = clp->cl_confirm,
	};
	unsigned short port;
	int status;
	struct nfs_net *nn = net_generic(clp->cl_net, nfs_net_id);

	if (test_bit(NFS4CLNT_LEASE_CONFIRM, &clp->cl_state))
		goto do_confirm;
	port = nn->nfs_callback_tcpport;
	if (clp->cl_addr.ss_family == AF_INET6)
		port = nn->nfs_callback_tcpport6;

	status = nfs4_proc_setclientid(clp, NFS4_CALLBACK, port, cred, &clid);
	if (status != 0)
		goto out;
	clp->cl_clientid = clid.clientid;
	clp->cl_confirm = clid.confirm;
	set_bit(NFS4CLNT_LEASE_CONFIRM, &clp->cl_state);
do_confirm:
	status = nfs4_proc_setclientid_confirm(clp, &clid, cred);
	if (status != 0)
		goto out;
	clear_bit(NFS4CLNT_LEASE_CONFIRM, &clp->cl_state);
	nfs4_schedule_state_renewal(clp);
out:
	return status;
}

/**
 * nfs40_discover_server_trunking - Detect server IP address trunking (mv0)
 *
 * @clp: nfs_client under test
 * @result: OUT: found nfs_client, or clp
 * @cred: credential to use for trunking test
 *
 * Returns zero, a negative errno, or a negative NFS4ERR status.
 * If zero is returned, an nfs_client pointer is planted in
 * "result".
 *
 * Note: The returned client may not yet be marked ready.
 */
int nfs40_discover_server_trunking(struct nfs_client *clp,
				   struct nfs_client **result,
				   struct rpc_cred *cred)
{
	struct nfs4_setclientid_res clid = {
		.clientid = clp->cl_clientid,
		.confirm = clp->cl_confirm,
	};
	struct nfs_net *nn = net_generic(clp->cl_net, nfs_net_id);
	unsigned short port;
	int status;

	port = nn->nfs_callback_tcpport;
	if (clp->cl_addr.ss_family == AF_INET6)
		port = nn->nfs_callback_tcpport6;

	status = nfs4_proc_setclientid(clp, NFS4_CALLBACK, port, cred, &clid);
	if (status != 0)
		goto out;
	clp->cl_clientid = clid.clientid;
	clp->cl_confirm = clid.confirm;

	status = nfs40_walk_client_list(clp, result, cred);
	switch (status) {
	case -NFS4ERR_STALE_CLIENTID:
		set_bit(NFS4CLNT_LEASE_CONFIRM, &clp->cl_state);
	case 0:
		/* Sustain the lease, even if it's empty.  If the clientid4
		 * goes stale it's of no use for trunking discovery. */
		nfs4_schedule_state_renewal(*result);
		break;
	}

out:
	return status;
}

struct rpc_cred *nfs4_get_machine_cred_locked(struct nfs_client *clp)
{
	struct rpc_cred *cred = NULL;

	if (clp->cl_machine_cred != NULL)
		cred = get_rpccred(clp->cl_machine_cred);
	return cred;
}

static void nfs4_clear_machine_cred(struct nfs_client *clp)
{
	struct rpc_cred *cred;

	spin_lock(&clp->cl_lock);
	cred = clp->cl_machine_cred;
	clp->cl_machine_cred = NULL;
	spin_unlock(&clp->cl_lock);
	if (cred != NULL)
		put_rpccred(cred);
}

static struct rpc_cred *
nfs4_get_renew_cred_server_locked(struct nfs_server *server)
{
	struct rpc_cred *cred = NULL;
	struct nfs4_state_owner *sp;
	struct rb_node *pos;

	for (pos = rb_first(&server->state_owners);
	     pos != NULL;
	     pos = rb_next(pos)) {
		sp = rb_entry(pos, struct nfs4_state_owner, so_server_node);
		if (list_empty(&sp->so_states))
			continue;
		cred = get_rpccred(sp->so_cred);
		break;
	}
	return cred;
}

/**
 * nfs4_get_renew_cred_locked - Acquire credential for a renew operation
 * @clp: client state handle
 *
 * Returns an rpc_cred with reference count bumped, or NULL.
 * Caller must hold clp->cl_lock.
 */
struct rpc_cred *nfs4_get_renew_cred_locked(struct nfs_client *clp)
{
	struct rpc_cred *cred = NULL;
	struct nfs_server *server;

	/* Use machine credentials if available */
	cred = nfs4_get_machine_cred_locked(clp);
	if (cred != NULL)
		goto out;

	rcu_read_lock();
	list_for_each_entry_rcu(server, &clp->cl_superblocks, client_link) {
		cred = nfs4_get_renew_cred_server_locked(server);
		if (cred != NULL)
			break;
	}
	rcu_read_unlock();

out:
	return cred;
}

#if defined(CONFIG_NFS_V4_1)

static int nfs41_setup_state_renewal(struct nfs_client *clp)
{
	int status;
	struct nfs_fsinfo fsinfo;

	if (!test_bit(NFS_CS_CHECK_LEASE_TIME, &clp->cl_res_state)) {
		nfs4_schedule_state_renewal(clp);
		return 0;
	}

	status = nfs4_proc_get_lease_time(clp, &fsinfo);
	if (status == 0) {
		/* Update lease time and schedule renewal */
		spin_lock(&clp->cl_lock);
		clp->cl_lease_time = fsinfo.lease_time * HZ;
		clp->cl_last_renewal = jiffies;
		spin_unlock(&clp->cl_lock);

		nfs4_schedule_state_renewal(clp);
	}

	return status;
}

/*
 * Back channel returns NFS4ERR_DELAY for new requests when
 * NFS4_SESSION_DRAINING is set so there is no work to be done when draining
 * is ended.
 */
static void nfs4_end_drain_session(struct nfs_client *clp)
{
	struct nfs4_session *ses = clp->cl_session;
	struct nfs4_slot_table *tbl;
	int max_slots;

	if (ses == NULL)
		return;
	tbl = &ses->fc_slot_table;
	if (test_and_clear_bit(NFS4_SESSION_DRAINING, &ses->session_state)) {
		spin_lock(&tbl->slot_tbl_lock);
		max_slots = tbl->max_slots;
		while (max_slots--) {
			if (rpc_wake_up_first(&tbl->slot_tbl_waitq,
						nfs4_set_task_privileged,
						NULL) == NULL)
				break;
		}
		spin_unlock(&tbl->slot_tbl_lock);
	}
}

static int nfs4_wait_on_slot_tbl(struct nfs4_slot_table *tbl)
{
	spin_lock(&tbl->slot_tbl_lock);
	if (tbl->highest_used_slotid != NFS4_NO_SLOT) {
		INIT_COMPLETION(tbl->complete);
		spin_unlock(&tbl->slot_tbl_lock);
		return wait_for_completion_interruptible(&tbl->complete);
	}
	spin_unlock(&tbl->slot_tbl_lock);
	return 0;
}

static int nfs4_begin_drain_session(struct nfs_client *clp)
{
	struct nfs4_session *ses = clp->cl_session;
	int ret = 0;

	set_bit(NFS4_SESSION_DRAINING, &ses->session_state);
	/* back channel */
	ret = nfs4_wait_on_slot_tbl(&ses->bc_slot_table);
	if (ret)
		return ret;
	/* fore channel */
	return nfs4_wait_on_slot_tbl(&ses->fc_slot_table);
}

static void nfs41_finish_session_reset(struct nfs_client *clp)
{
	clear_bit(NFS4CLNT_LEASE_CONFIRM, &clp->cl_state);
	clear_bit(NFS4CLNT_SESSION_RESET, &clp->cl_state);
	/* create_session negotiated new slot table */
	clear_bit(NFS4CLNT_RECALL_SLOT, &clp->cl_state);
	clear_bit(NFS4CLNT_BIND_CONN_TO_SESSION, &clp->cl_state);
	nfs41_setup_state_renewal(clp);
}

int nfs41_init_clientid(struct nfs_client *clp, struct rpc_cred *cred)
{
	int status;

	if (test_bit(NFS4CLNT_LEASE_CONFIRM, &clp->cl_state))
		goto do_confirm;
	nfs4_begin_drain_session(clp);
	status = nfs4_proc_exchange_id(clp, cred);
	if (status != 0)
		goto out;
	set_bit(NFS4CLNT_LEASE_CONFIRM, &clp->cl_state);
do_confirm:
	status = nfs4_proc_create_session(clp, cred);
	if (status != 0)
		goto out;
	nfs41_finish_session_reset(clp);
	nfs_mark_client_ready(clp, NFS_CS_READY);
out:
	return status;
}

/**
 * nfs41_discover_server_trunking - Detect server IP address trunking (mv1)
 *
 * @clp: nfs_client under test
 * @result: OUT: found nfs_client, or clp
 * @cred: credential to use for trunking test
 *
 * Returns NFS4_OK, a negative errno, or a negative NFS4ERR status.
 * If NFS4_OK is returned, an nfs_client pointer is planted in
 * "result".
 *
 * Note: The returned client may not yet be marked ready.
 */
int nfs41_discover_server_trunking(struct nfs_client *clp,
				   struct nfs_client **result,
				   struct rpc_cred *cred)
{
	int status;

	status = nfs4_proc_exchange_id(clp, cred);
	if (status != NFS4_OK)
		return status;
	set_bit(NFS4CLNT_LEASE_CONFIRM, &clp->cl_state);

	return nfs41_walk_client_list(clp, result, cred);
}

struct rpc_cred *nfs4_get_exchange_id_cred(struct nfs_client *clp)
{
	struct rpc_cred *cred;

	spin_lock(&clp->cl_lock);
	cred = nfs4_get_machine_cred_locked(clp);
	spin_unlock(&clp->cl_lock);
	return cred;
}

#endif /* CONFIG_NFS_V4_1 */

static struct rpc_cred *
nfs4_get_setclientid_cred_server(struct nfs_server *server)
{
	struct nfs_client *clp = server->nfs_client;
	struct rpc_cred *cred = NULL;
	struct nfs4_state_owner *sp;
	struct rb_node *pos;

	spin_lock(&clp->cl_lock);
	pos = rb_first(&server->state_owners);
	if (pos != NULL) {
		sp = rb_entry(pos, struct nfs4_state_owner, so_server_node);
		cred = get_rpccred(sp->so_cred);
	}
	spin_unlock(&clp->cl_lock);
	return cred;
}

/**
 * nfs4_get_setclientid_cred - Acquire credential for a setclientid operation
 * @clp: client state handle
 *
 * Returns an rpc_cred with reference count bumped, or NULL.
 */
struct rpc_cred *nfs4_get_setclientid_cred(struct nfs_client *clp)
{
	struct nfs_server *server;
	struct rpc_cred *cred;

	spin_lock(&clp->cl_lock);
	cred = nfs4_get_machine_cred_locked(clp);
	spin_unlock(&clp->cl_lock);
	if (cred != NULL)
		goto out;

	rcu_read_lock();
	list_for_each_entry_rcu(server, &clp->cl_superblocks, client_link) {
		cred = nfs4_get_setclientid_cred_server(server);
		if (cred != NULL)
			break;
	}
	rcu_read_unlock();

out:
	return cred;
}

static struct nfs4_state_owner *
nfs4_find_state_owner_locked(struct nfs_server *server, struct rpc_cred *cred)
{
	struct rb_node **p = &server->state_owners.rb_node,
		       *parent = NULL;
	struct nfs4_state_owner *sp;

	while (*p != NULL) {
		parent = *p;
		sp = rb_entry(parent, struct nfs4_state_owner, so_server_node);

		if (cred < sp->so_cred)
			p = &parent->rb_left;
		else if (cred > sp->so_cred)
			p = &parent->rb_right;
		else {
			if (!list_empty(&sp->so_lru))
				list_del_init(&sp->so_lru);
			atomic_inc(&sp->so_count);
			return sp;
		}
	}
	return NULL;
}

static struct nfs4_state_owner *
nfs4_insert_state_owner_locked(struct nfs4_state_owner *new)
{
	struct nfs_server *server = new->so_server;
	struct rb_node **p = &server->state_owners.rb_node,
		       *parent = NULL;
	struct nfs4_state_owner *sp;
	int err;

	while (*p != NULL) {
		parent = *p;
		sp = rb_entry(parent, struct nfs4_state_owner, so_server_node);

		if (new->so_cred < sp->so_cred)
			p = &parent->rb_left;
		else if (new->so_cred > sp->so_cred)
			p = &parent->rb_right;
		else {
			if (!list_empty(&sp->so_lru))
				list_del_init(&sp->so_lru);
			atomic_inc(&sp->so_count);
			return sp;
		}
	}
	err = ida_get_new(&server->openowner_id, &new->so_seqid.owner_id);
	if (err)
		return ERR_PTR(err);
	rb_link_node(&new->so_server_node, parent, p);
	rb_insert_color(&new->so_server_node, &server->state_owners);
	return new;
}

static void
nfs4_remove_state_owner_locked(struct nfs4_state_owner *sp)
{
	struct nfs_server *server = sp->so_server;

	if (!RB_EMPTY_NODE(&sp->so_server_node))
		rb_erase(&sp->so_server_node, &server->state_owners);
	ida_remove(&server->openowner_id, sp->so_seqid.owner_id);
}

static void
nfs4_init_seqid_counter(struct nfs_seqid_counter *sc)
{
	sc->create_time = ktime_get();
	sc->flags = 0;
	sc->counter = 0;
	spin_lock_init(&sc->lock);
	INIT_LIST_HEAD(&sc->list);
	rpc_init_wait_queue(&sc->wait, "Seqid_waitqueue");
}

static void
nfs4_destroy_seqid_counter(struct nfs_seqid_counter *sc)
{
	rpc_destroy_wait_queue(&sc->wait);
}

/*
 * nfs4_alloc_state_owner(): this is called on the OPEN or CREATE path to
 * create a new state_owner.
 *
 */
static struct nfs4_state_owner *
nfs4_alloc_state_owner(struct nfs_server *server,
		struct rpc_cred *cred,
		gfp_t gfp_flags)
{
	struct nfs4_state_owner *sp;

	sp = kzalloc(sizeof(*sp), gfp_flags);
	if (!sp)
		return NULL;
	sp->so_server = server;
	sp->so_cred = get_rpccred(cred);
	spin_lock_init(&sp->so_lock);
	INIT_LIST_HEAD(&sp->so_states);
	nfs4_init_seqid_counter(&sp->so_seqid);
	atomic_set(&sp->so_count, 1);
	INIT_LIST_HEAD(&sp->so_lru);
	return sp;
}

static void
nfs4_drop_state_owner(struct nfs4_state_owner *sp)
{
	struct rb_node *rb_node = &sp->so_server_node;

	if (!RB_EMPTY_NODE(rb_node)) {
		struct nfs_server *server = sp->so_server;
		struct nfs_client *clp = server->nfs_client;

		spin_lock(&clp->cl_lock);
		if (!RB_EMPTY_NODE(rb_node)) {
			rb_erase(rb_node, &server->state_owners);
			RB_CLEAR_NODE(rb_node);
		}
		spin_unlock(&clp->cl_lock);
	}
}

static void nfs4_free_state_owner(struct nfs4_state_owner *sp)
{
	nfs4_destroy_seqid_counter(&sp->so_seqid);
	put_rpccred(sp->so_cred);
	kfree(sp);
}

static void nfs4_gc_state_owners(struct nfs_server *server)
{
	struct nfs_client *clp = server->nfs_client;
	struct nfs4_state_owner *sp, *tmp;
	unsigned long time_min, time_max;
	LIST_HEAD(doomed);

	spin_lock(&clp->cl_lock);
	time_max = jiffies;
	time_min = (long)time_max - (long)clp->cl_lease_time;
	list_for_each_entry_safe(sp, tmp, &server->state_owners_lru, so_lru) {
		/* NB: LRU is sorted so that oldest is at the head */
		if (time_in_range(sp->so_expires, time_min, time_max))
			break;
		list_move(&sp->so_lru, &doomed);
		nfs4_remove_state_owner_locked(sp);
	}
	spin_unlock(&clp->cl_lock);

	list_for_each_entry_safe(sp, tmp, &doomed, so_lru) {
		list_del(&sp->so_lru);
		nfs4_free_state_owner(sp);
	}
}

/**
 * nfs4_get_state_owner - Look up a state owner given a credential
 * @server: nfs_server to search
 * @cred: RPC credential to match
 *
 * Returns a pointer to an instantiated nfs4_state_owner struct, or NULL.
 */
struct nfs4_state_owner *nfs4_get_state_owner(struct nfs_server *server,
					      struct rpc_cred *cred,
					      gfp_t gfp_flags)
{
	struct nfs_client *clp = server->nfs_client;
	struct nfs4_state_owner *sp, *new;

	spin_lock(&clp->cl_lock);
	sp = nfs4_find_state_owner_locked(server, cred);
	spin_unlock(&clp->cl_lock);
	if (sp != NULL)
		goto out;
	new = nfs4_alloc_state_owner(server, cred, gfp_flags);
	if (new == NULL)
		goto out;
	do {
		if (ida_pre_get(&server->openowner_id, gfp_flags) == 0)
			break;
		spin_lock(&clp->cl_lock);
		sp = nfs4_insert_state_owner_locked(new);
		spin_unlock(&clp->cl_lock);
	} while (sp == ERR_PTR(-EAGAIN));
	if (sp != new)
		nfs4_free_state_owner(new);
out:
	nfs4_gc_state_owners(server);
	return sp;
}

/**
 * nfs4_put_state_owner - Release a nfs4_state_owner
 * @sp: state owner data to release
 *
 * Note that we keep released state owners on an LRU
 * list.
 * This caches valid state owners so that they can be
 * reused, to avoid the OPEN_CONFIRM on minor version 0.
 * It also pins the uniquifier of dropped state owners for
 * a while, to ensure that those state owner names are
 * never reused.
 */
void nfs4_put_state_owner(struct nfs4_state_owner *sp)
{
	struct nfs_server *server = sp->so_server;
	struct nfs_client *clp = server->nfs_client;

	if (!atomic_dec_and_lock(&sp->so_count, &clp->cl_lock))
		return;

	sp->so_expires = jiffies;
	list_add_tail(&sp->so_lru, &server->state_owners_lru);
	spin_unlock(&clp->cl_lock);
}

/**
 * nfs4_purge_state_owners - Release all cached state owners
 * @server: nfs_server with cached state owners to release
 *
 * Called at umount time.  Remaining state owners will be on
 * the LRU with ref count of zero.
 */
void nfs4_purge_state_owners(struct nfs_server *server)
{
	struct nfs_client *clp = server->nfs_client;
	struct nfs4_state_owner *sp, *tmp;
	LIST_HEAD(doomed);

	spin_lock(&clp->cl_lock);
	list_for_each_entry_safe(sp, tmp, &server->state_owners_lru, so_lru) {
		list_move(&sp->so_lru, &doomed);
		nfs4_remove_state_owner_locked(sp);
	}
	spin_unlock(&clp->cl_lock);

	list_for_each_entry_safe(sp, tmp, &doomed, so_lru) {
		list_del(&sp->so_lru);
		nfs4_free_state_owner(sp);
	}
}

static struct nfs4_state *
nfs4_alloc_open_state(void)
{
	struct nfs4_state *state;

	state = kzalloc(sizeof(*state), GFP_NOFS);
	if (!state)
		return NULL;
	atomic_set(&state->count, 1);
	INIT_LIST_HEAD(&state->lock_states);
	spin_lock_init(&state->state_lock);
	seqlock_init(&state->seqlock);
	return state;
}

void
nfs4_state_set_mode_locked(struct nfs4_state *state, fmode_t fmode)
{
	if (state->state == fmode)
		return;
	/* NB! List reordering - see the reclaim code for why.  */
	if ((fmode & FMODE_WRITE) != (state->state & FMODE_WRITE)) {
		if (fmode & FMODE_WRITE)
			list_move(&state->open_states, &state->owner->so_states);
		else
			list_move_tail(&state->open_states, &state->owner->so_states);
	}
	state->state = fmode;
}

static struct nfs4_state *
__nfs4_find_state_byowner(struct inode *inode, struct nfs4_state_owner *owner)
{
	struct nfs_inode *nfsi = NFS_I(inode);
	struct nfs4_state *state;

	list_for_each_entry(state, &nfsi->open_states, inode_states) {
		if (state->owner != owner)
			continue;
		if (atomic_inc_not_zero(&state->count))
			return state;
	}
	return NULL;
}

static void
nfs4_free_open_state(struct nfs4_state *state)
{
	kfree(state);
}

struct nfs4_state *
nfs4_get_open_state(struct inode *inode, struct nfs4_state_owner *owner)
{
	struct nfs4_state *state, *new;
	struct nfs_inode *nfsi = NFS_I(inode);

	spin_lock(&inode->i_lock);
	state = __nfs4_find_state_byowner(inode, owner);
	spin_unlock(&inode->i_lock);
	if (state)
		goto out;
	new = nfs4_alloc_open_state();
	spin_lock(&owner->so_lock);
	spin_lock(&inode->i_lock);
	state = __nfs4_find_state_byowner(inode, owner);
	if (state == NULL && new != NULL) {
		state = new;
		state->owner = owner;
		atomic_inc(&owner->so_count);
		list_add(&state->inode_states, &nfsi->open_states);
		ihold(inode);
		state->inode = inode;
		spin_unlock(&inode->i_lock);
		/* Note: The reclaim code dictates that we add stateless
		 * and read-only stateids to the end of the list */
		list_add_tail(&state->open_states, &owner->so_states);
		spin_unlock(&owner->so_lock);
	} else {
		spin_unlock(&inode->i_lock);
		spin_unlock(&owner->so_lock);
		if (new)
			nfs4_free_open_state(new);
	}
out:
	return state;
}

void nfs4_put_open_state(struct nfs4_state *state)
{
	struct inode *inode = state->inode;
	struct nfs4_state_owner *owner = state->owner;

	if (!atomic_dec_and_lock(&state->count, &owner->so_lock))
		return;
	spin_lock(&inode->i_lock);
	list_del(&state->inode_states);
	list_del(&state->open_states);
	spin_unlock(&inode->i_lock);
	spin_unlock(&owner->so_lock);
	iput(inode);
	nfs4_free_open_state(state);
	nfs4_put_state_owner(owner);
}

/*
 * Close the current file.
 */
static void __nfs4_close(struct nfs4_state *state,
		fmode_t fmode, gfp_t gfp_mask, int wait)
{
	struct nfs4_state_owner *owner = state->owner;
	int call_close = 0;
	fmode_t newstate;

	atomic_inc(&owner->so_count);
	/* Protect against nfs4_find_state() */
	spin_lock(&owner->so_lock);
	switch (fmode & (FMODE_READ | FMODE_WRITE)) {
		case FMODE_READ:
			state->n_rdonly--;
			break;
		case FMODE_WRITE:
			state->n_wronly--;
			break;
		case FMODE_READ|FMODE_WRITE:
			state->n_rdwr--;
	}
	newstate = FMODE_READ|FMODE_WRITE;
	if (state->n_rdwr == 0) {
		if (state->n_rdonly == 0) {
			newstate &= ~FMODE_READ;
			call_close |= test_bit(NFS_O_RDONLY_STATE, &state->flags);
			call_close |= test_bit(NFS_O_RDWR_STATE, &state->flags);
		}
		if (state->n_wronly == 0) {
			newstate &= ~FMODE_WRITE;
			call_close |= test_bit(NFS_O_WRONLY_STATE, &state->flags);
			call_close |= test_bit(NFS_O_RDWR_STATE, &state->flags);
		}
		if (newstate == 0)
			clear_bit(NFS_DELEGATED_STATE, &state->flags);
	}
	nfs4_state_set_mode_locked(state, newstate);
	spin_unlock(&owner->so_lock);

	if (!call_close) {
		nfs4_put_open_state(state);
		nfs4_put_state_owner(owner);
	} else
		nfs4_do_close(state, gfp_mask, wait);
}

void nfs4_close_state(struct nfs4_state *state, fmode_t fmode)
{
	__nfs4_close(state, fmode, GFP_NOFS, 0);
}

void nfs4_close_sync(struct nfs4_state *state, fmode_t fmode)
{
	__nfs4_close(state, fmode, GFP_KERNEL, 1);
}

/*
 * Search the state->lock_states for an existing lock_owner
 * that is compatible with current->files
 */
static struct nfs4_lock_state *
__nfs4_find_lock_state(struct nfs4_state *state, fl_owner_t fl_owner, pid_t fl_pid, unsigned int type)
{
	struct nfs4_lock_state *pos;
	list_for_each_entry(pos, &state->lock_states, ls_locks) {
		if (type != NFS4_ANY_LOCK_TYPE && pos->ls_owner.lo_type != type)
			continue;
		switch (pos->ls_owner.lo_type) {
		case NFS4_POSIX_LOCK_TYPE:
			if (pos->ls_owner.lo_u.posix_owner != fl_owner)
				continue;
			break;
		case NFS4_FLOCK_LOCK_TYPE:
			if (pos->ls_owner.lo_u.flock_owner != fl_pid)
				continue;
		}
		atomic_inc(&pos->ls_count);
		return pos;
	}
	return NULL;
}

/*
 * Return a compatible lock_state. If no initialized lock_state structure
 * exists, return an uninitialized one.
 *
 */
static struct nfs4_lock_state *nfs4_alloc_lock_state(struct nfs4_state *state, fl_owner_t fl_owner, pid_t fl_pid, unsigned int type)
{
	struct nfs4_lock_state *lsp;
	struct nfs_server *server = state->owner->so_server;

	lsp = kzalloc(sizeof(*lsp), GFP_NOFS);
	if (lsp == NULL)
		return NULL;
	nfs4_init_seqid_counter(&lsp->ls_seqid);
	atomic_set(&lsp->ls_count, 1);
	lsp->ls_state = state;
	lsp->ls_owner.lo_type = type;
	switch (lsp->ls_owner.lo_type) {
	case NFS4_FLOCK_LOCK_TYPE:
		lsp->ls_owner.lo_u.flock_owner = fl_pid;
		break;
	case NFS4_POSIX_LOCK_TYPE:
		lsp->ls_owner.lo_u.posix_owner = fl_owner;
		break;
	default:
		goto out_free;
	}
	lsp->ls_seqid.owner_id = ida_simple_get(&server->lockowner_id, 0, 0, GFP_NOFS);
	if (lsp->ls_seqid.owner_id < 0)
		goto out_free;
	INIT_LIST_HEAD(&lsp->ls_locks);
	return lsp;
out_free:
	kfree(lsp);
	return NULL;
}

void nfs4_free_lock_state(struct nfs_server *server, struct nfs4_lock_state *lsp)
{
	ida_simple_remove(&server->lockowner_id, lsp->ls_seqid.owner_id);
	nfs4_destroy_seqid_counter(&lsp->ls_seqid);
	kfree(lsp);
}

/*
 * Return a compatible lock_state. If no initialized lock_state structure
 * exists, return an uninitialized one.
 *
 */
static struct nfs4_lock_state *nfs4_get_lock_state(struct nfs4_state *state, fl_owner_t owner, pid_t pid, unsigned int type)
{
	struct nfs4_lock_state *lsp, *new = NULL;
	
	for(;;) {
		spin_lock(&state->state_lock);
		lsp = __nfs4_find_lock_state(state, owner, pid, type);
		if (lsp != NULL)
			break;
		if (new != NULL) {
			list_add(&new->ls_locks, &state->lock_states);
			set_bit(LK_STATE_IN_USE, &state->flags);
			lsp = new;
			new = NULL;
			break;
		}
		spin_unlock(&state->state_lock);
		new = nfs4_alloc_lock_state(state, owner, pid, type);
		if (new == NULL)
			return NULL;
	}
	spin_unlock(&state->state_lock);
	if (new != NULL)
		nfs4_free_lock_state(state->owner->so_server, new);
	return lsp;
}

/*
 * Release reference to lock_state, and free it if we see that
 * it is no longer in use
 */
void nfs4_put_lock_state(struct nfs4_lock_state *lsp)
{
	struct nfs4_state *state;

	if (lsp == NULL)
		return;
	state = lsp->ls_state;
	if (!atomic_dec_and_lock(&lsp->ls_count, &state->state_lock))
		return;
	list_del(&lsp->ls_locks);
	if (list_empty(&state->lock_states))
		clear_bit(LK_STATE_IN_USE, &state->flags);
	spin_unlock(&state->state_lock);
	if (test_bit(NFS_LOCK_INITIALIZED, &lsp->ls_flags)) {
		if (nfs4_release_lockowner(lsp) == 0)
			return;
	}
	nfs4_free_lock_state(lsp->ls_state->owner->so_server, lsp);
}

static void nfs4_fl_copy_lock(struct file_lock *dst, struct file_lock *src)
{
	struct nfs4_lock_state *lsp = src->fl_u.nfs4_fl.owner;

	dst->fl_u.nfs4_fl.owner = lsp;
	atomic_inc(&lsp->ls_count);
}

static void nfs4_fl_release_lock(struct file_lock *fl)
{
	nfs4_put_lock_state(fl->fl_u.nfs4_fl.owner);
}

static const struct file_lock_operations nfs4_fl_lock_ops = {
	.fl_copy_lock = nfs4_fl_copy_lock,
	.fl_release_private = nfs4_fl_release_lock,
};

int nfs4_set_lock_state(struct nfs4_state *state, struct file_lock *fl)
{
	struct nfs4_lock_state *lsp;

	if (fl->fl_ops != NULL)
		return 0;
	if (fl->fl_flags & FL_POSIX)
		lsp = nfs4_get_lock_state(state, fl->fl_owner, 0, NFS4_POSIX_LOCK_TYPE);
	else if (fl->fl_flags & FL_FLOCK)
		lsp = nfs4_get_lock_state(state, NULL, fl->fl_pid,
				NFS4_FLOCK_LOCK_TYPE);
	else
		return -EINVAL;
	if (lsp == NULL)
		return -ENOMEM;
	fl->fl_u.nfs4_fl.owner = lsp;
	fl->fl_ops = &nfs4_fl_lock_ops;
	return 0;
}

static bool nfs4_copy_lock_stateid(nfs4_stateid *dst, struct nfs4_state *state,
		const struct nfs_lockowner *lockowner)
{
	struct nfs4_lock_state *lsp;
	fl_owner_t fl_owner;
	pid_t fl_pid;
	bool ret = false;


	if (lockowner == NULL)
		goto out;

	if (test_bit(LK_STATE_IN_USE, &state->flags) == 0)
		goto out;

	fl_owner = lockowner->l_owner;
	fl_pid = lockowner->l_pid;
	spin_lock(&state->state_lock);
	lsp = __nfs4_find_lock_state(state, fl_owner, fl_pid, NFS4_ANY_LOCK_TYPE);
	if (lsp != NULL && test_bit(NFS_LOCK_INITIALIZED, &lsp->ls_flags) != 0) {
		nfs4_stateid_copy(dst, &lsp->ls_stateid);
		ret = true;
	}
	spin_unlock(&state->state_lock);
	nfs4_put_lock_state(lsp);
out:
	return ret;
}

static void nfs4_copy_open_stateid(nfs4_stateid *dst, struct nfs4_state *state)
{
	int seq;

	do {
		seq = read_seqbegin(&state->seqlock);
		nfs4_stateid_copy(dst, &state->stateid);
	} while (read_seqretry(&state->seqlock, seq));
}

/*
 * Byte-range lock aware utility to initialize the stateid of read/write
 * requests.
 */
void nfs4_select_rw_stateid(nfs4_stateid *dst, struct nfs4_state *state,
		fmode_t fmode, const struct nfs_lockowner *lockowner)
{
	if (nfs4_copy_delegation_stateid(dst, state->inode, fmode))
		return;
	if (nfs4_copy_lock_stateid(dst, state, lockowner))
		return;
	nfs4_copy_open_stateid(dst, state);
}

struct nfs_seqid *nfs_alloc_seqid(struct nfs_seqid_counter *counter, gfp_t gfp_mask)
{
	struct nfs_seqid *new;

	new = kmalloc(sizeof(*new), gfp_mask);
	if (new != NULL) {
		new->sequence = counter;
		INIT_LIST_HEAD(&new->list);
		new->task = NULL;
	}
	return new;
}

void nfs_release_seqid(struct nfs_seqid *seqid)
{
	struct nfs_seqid_counter *sequence;

	if (list_empty(&seqid->list))
		return;
	sequence = seqid->sequence;
	spin_lock(&sequence->lock);
	list_del_init(&seqid->list);
	if (!list_empty(&sequence->list)) {
		struct nfs_seqid *next;

		next = list_first_entry(&sequence->list,
				struct nfs_seqid, list);
		rpc_wake_up_queued_task(&sequence->wait, next->task);
	}
	spin_unlock(&sequence->lock);
}

void nfs_free_seqid(struct nfs_seqid *seqid)
{
	nfs_release_seqid(seqid);
	kfree(seqid);
}

/*
 * Increment the seqid if the OPEN/OPEN_DOWNGRADE/CLOSE succeeded, or
 * failed with a seqid incrementing error -
 * see comments nfs_fs.h:seqid_mutating_error()
 */
static void nfs_increment_seqid(int status, struct nfs_seqid *seqid)
{
	switch (status) {
		case 0:
			break;
		case -NFS4ERR_BAD_SEQID:
			if (seqid->sequence->flags & NFS_SEQID_CONFIRMED)
				return;
			pr_warn_ratelimited("NFS: v4 server returned a bad"
					" sequence-id error on an"
					" unconfirmed sequence %p!\n",
					seqid->sequence);
		case -NFS4ERR_STALE_CLIENTID:
		case -NFS4ERR_STALE_STATEID:
		case -NFS4ERR_BAD_STATEID:
		case -NFS4ERR_BADXDR:
		case -NFS4ERR_RESOURCE:
		case -NFS4ERR_NOFILEHANDLE:
			/* Non-seqid mutating errors */
			return;
	};
	/*
	 * Note: no locking needed as we are guaranteed to be first
	 * on the sequence list
	 */
	seqid->sequence->counter++;
}

void nfs_increment_open_seqid(int status, struct nfs_seqid *seqid)
{
	struct nfs4_state_owner *sp = container_of(seqid->sequence,
					struct nfs4_state_owner, so_seqid);
	struct nfs_server *server = sp->so_server;

	if (status == -NFS4ERR_BAD_SEQID)
		nfs4_drop_state_owner(sp);
	if (!nfs4_has_session(server->nfs_client))
		nfs_increment_seqid(status, seqid);
}

/*
 * Increment the seqid if the LOCK/LOCKU succeeded, or
 * failed with a seqid incrementing error -
 * see comments nfs_fs.h:seqid_mutating_error()
 */
void nfs_increment_lock_seqid(int status, struct nfs_seqid *seqid)
{
	nfs_increment_seqid(status, seqid);
}

int nfs_wait_on_sequence(struct nfs_seqid *seqid, struct rpc_task *task)
{
	struct nfs_seqid_counter *sequence = seqid->sequence;
	int status = 0;

	spin_lock(&sequence->lock);
	seqid->task = task;
	if (list_empty(&seqid->list))
		list_add_tail(&seqid->list, &sequence->list);
	if (list_first_entry(&sequence->list, struct nfs_seqid, list) == seqid)
		goto unlock;
	rpc_sleep_on(&sequence->wait, task, NULL);
	status = -EAGAIN;
unlock:
	spin_unlock(&sequence->lock);
	return status;
}

static int nfs4_run_state_manager(void *);

static void nfs4_clear_state_manager_bit(struct nfs_client *clp)
{
	smp_mb__before_clear_bit();
	clear_bit(NFS4CLNT_MANAGER_RUNNING, &clp->cl_state);
	smp_mb__after_clear_bit();
	wake_up_bit(&clp->cl_state, NFS4CLNT_MANAGER_RUNNING);
	rpc_wake_up(&clp->cl_rpcwaitq);
}

/*
 * Schedule the nfs_client asynchronous state management routine
 */
void nfs4_schedule_state_manager(struct nfs_client *clp)
{
	struct task_struct *task;
	char buf[INET6_ADDRSTRLEN + sizeof("-manager") + 1];

	if (test_and_set_bit(NFS4CLNT_MANAGER_RUNNING, &clp->cl_state) != 0)
		return;
	__module_get(THIS_MODULE);
	atomic_inc(&clp->cl_count);

	/* The rcu_read_lock() is not strictly necessary, as the state
	 * manager is the only thread that ever changes the rpc_xprt
	 * after it's initialized.  At this point, we're single threaded. */
	rcu_read_lock();
	snprintf(buf, sizeof(buf), "%s-manager",
			rpc_peeraddr2str(clp->cl_rpcclient, RPC_DISPLAY_ADDR));
	rcu_read_unlock();
	task = kthread_run(nfs4_run_state_manager, clp, buf);
	if (IS_ERR(task)) {
		printk(KERN_ERR "%s: kthread_run: %ld\n",
			__func__, PTR_ERR(task));
		nfs4_clear_state_manager_bit(clp);
		nfs_put_client(clp);
		module_put(THIS_MODULE);
	}
}

/*
 * Schedule a lease recovery attempt
 */
void nfs4_schedule_lease_recovery(struct nfs_client *clp)
{
	if (!clp)
		return;
	if (!test_bit(NFS4CLNT_LEASE_EXPIRED, &clp->cl_state))
		set_bit(NFS4CLNT_CHECK_LEASE, &clp->cl_state);
	dprintk("%s: scheduling lease recovery for server %s\n", __func__,
			clp->cl_hostname);
	nfs4_schedule_state_manager(clp);
}
EXPORT_SYMBOL_GPL(nfs4_schedule_lease_recovery);

/*
 * nfs40_handle_cb_pathdown - return all delegations after NFS4ERR_CB_PATH_DOWN
 * @clp: client to process
 *
 * Set the NFS4CLNT_LEASE_EXPIRED state in order to force a
 * resend of the SETCLIENTID and hence re-establish the
 * callback channel. Then return all existing delegations.
 */
static void nfs40_handle_cb_pathdown(struct nfs_client *clp)
{
	set_bit(NFS4CLNT_LEASE_EXPIRED, &clp->cl_state);
	nfs_expire_all_delegations(clp);
	dprintk("%s: handling CB_PATHDOWN recovery for server %s\n", __func__,
			clp->cl_hostname);
}

void nfs4_schedule_path_down_recovery(struct nfs_client *clp)
{
	nfs40_handle_cb_pathdown(clp);
	nfs4_schedule_state_manager(clp);
}

static int nfs4_state_mark_reclaim_reboot(struct nfs_client *clp, struct nfs4_state *state)
{

	set_bit(NFS_STATE_RECLAIM_REBOOT, &state->flags);
	/* Don't recover state that expired before the reboot */
	if (test_bit(NFS_STATE_RECLAIM_NOGRACE, &state->flags)) {
		clear_bit(NFS_STATE_RECLAIM_REBOOT, &state->flags);
		return 0;
	}
	set_bit(NFS_OWNER_RECLAIM_REBOOT, &state->owner->so_flags);
	set_bit(NFS4CLNT_RECLAIM_REBOOT, &clp->cl_state);
	return 1;
}

static int nfs4_state_mark_reclaim_nograce(struct nfs_client *clp, struct nfs4_state *state)
{
	set_bit(NFS_STATE_RECLAIM_NOGRACE, &state->flags);
	clear_bit(NFS_STATE_RECLAIM_REBOOT, &state->flags);
	set_bit(NFS_OWNER_RECLAIM_NOGRACE, &state->owner->so_flags);
	set_bit(NFS4CLNT_RECLAIM_NOGRACE, &clp->cl_state);
	return 1;
}

void nfs4_schedule_stateid_recovery(const struct nfs_server *server, struct nfs4_state *state)
{
	struct nfs_client *clp = server->nfs_client;

	nfs4_state_mark_reclaim_nograce(clp, state);
	dprintk("%s: scheduling stateid recovery for server %s\n", __func__,
			clp->cl_hostname);
	nfs4_schedule_state_manager(clp);
}
EXPORT_SYMBOL_GPL(nfs4_schedule_stateid_recovery);

void nfs_inode_find_state_and_recover(struct inode *inode,
		const nfs4_stateid *stateid)
{
	struct nfs_client *clp = NFS_SERVER(inode)->nfs_client;
	struct nfs_inode *nfsi = NFS_I(inode);
	struct nfs_open_context *ctx;
	struct nfs4_state *state;
	bool found = false;

	spin_lock(&inode->i_lock);
	list_for_each_entry(ctx, &nfsi->open_files, list) {
		state = ctx->state;
		if (state == NULL)
			continue;
		if (!test_bit(NFS_DELEGATED_STATE, &state->flags))
			continue;
		if (!nfs4_stateid_match(&state->stateid, stateid))
			continue;
		nfs4_state_mark_reclaim_nograce(clp, state);
		found = true;
	}
	spin_unlock(&inode->i_lock);
	if (found)
		nfs4_schedule_state_manager(clp);
}


static int nfs4_reclaim_locks(struct nfs4_state *state, const struct nfs4_state_recovery_ops *ops)
{
	struct inode *inode = state->inode;
	struct nfs_inode *nfsi = NFS_I(inode);
	struct file_lock *fl;
	int status = 0;

	if (inode->i_flock == NULL)
		return 0;

	/* Guard against delegation returns and new lock/unlock calls */
	down_write(&nfsi->rwsem);
	/* Protect inode->i_flock using the BKL */
	lock_flocks();
	for (fl = inode->i_flock; fl != NULL; fl = fl->fl_next) {
		if (!(fl->fl_flags & (FL_POSIX|FL_FLOCK)))
			continue;
		if (nfs_file_open_context(fl->fl_file)->state != state)
			continue;
		unlock_flocks();
		status = ops->recover_lock(state, fl);
		switch (status) {
			case 0:
				break;
			case -ESTALE:
			case -NFS4ERR_ADMIN_REVOKED:
			case -NFS4ERR_STALE_STATEID:
			case -NFS4ERR_BAD_STATEID:
			case -NFS4ERR_EXPIRED:
			case -NFS4ERR_NO_GRACE:
			case -NFS4ERR_STALE_CLIENTID:
			case -NFS4ERR_BADSESSION:
			case -NFS4ERR_BADSLOT:
			case -NFS4ERR_BAD_HIGH_SLOT:
			case -NFS4ERR_CONN_NOT_BOUND_TO_SESSION:
				goto out;
			default:
				printk(KERN_ERR "NFS: %s: unhandled error %d. "
					"Zeroing state\n", __func__, status);
			case -ENOMEM:
			case -NFS4ERR_DENIED:
			case -NFS4ERR_RECLAIM_BAD:
			case -NFS4ERR_RECLAIM_CONFLICT:
				/* kill_proc(fl->fl_pid, SIGLOST, 1); */
				status = 0;
		}
		lock_flocks();
	}
	unlock_flocks();
out:
	up_write(&nfsi->rwsem);
	return status;
}

static int nfs4_reclaim_open_state(struct nfs4_state_owner *sp, const struct nfs4_state_recovery_ops *ops)
{
	struct nfs4_state *state;
	struct nfs4_lock_state *lock;
	int status = 0;

	/* Note: we rely on the sp->so_states list being ordered 
	 * so that we always reclaim open(O_RDWR) and/or open(O_WRITE)
	 * states first.
	 * This is needed to ensure that the server won't give us any
	 * read delegations that we have to return if, say, we are
	 * recovering after a network partition or a reboot from a
	 * server that doesn't support a grace period.
	 */
restart:
	spin_lock(&sp->so_lock);
	list_for_each_entry(state, &sp->so_states, open_states) {
		if (!test_and_clear_bit(ops->state_flag_bit, &state->flags))
			continue;
		if (state->state == 0)
			continue;
		atomic_inc(&state->count);
		spin_unlock(&sp->so_lock);
		status = ops->recover_open(sp, state);
		if (status >= 0) {
			status = nfs4_reclaim_locks(state, ops);
			if (status >= 0) {
				spin_lock(&state->state_lock);
				list_for_each_entry(lock, &state->lock_states, ls_locks) {
					if (!test_bit(NFS_LOCK_INITIALIZED, &lock->ls_flags))
						pr_warn_ratelimited("NFS: "
							"%s: Lock reclaim "
							"failed!\n", __func__);
				}
				spin_unlock(&state->state_lock);
				nfs4_put_open_state(state);
				goto restart;
			}
		}
		switch (status) {
			default:
				printk(KERN_ERR "NFS: %s: unhandled error %d. "
					"Zeroing state\n", __func__, status);
			case -ENOENT:
			case -ENOMEM:
			case -ESTALE:
				/*
				 * Open state on this file cannot be recovered
				 * All we can do is revert to using the zero stateid.
				 */
				memset(&state->stateid, 0,
					sizeof(state->stateid));
				/* Mark the file as being 'closed' */
				state->state = 0;
				break;
			case -EKEYEXPIRED:
				/*
				 * User RPCSEC_GSS context has expired.
				 * We cannot recover this stateid now, so
				 * skip it and allow recovery thread to
				 * proceed.
				 */
				break;
			case -NFS4ERR_ADMIN_REVOKED:
			case -NFS4ERR_STALE_STATEID:
			case -NFS4ERR_BAD_STATEID:
			case -NFS4ERR_RECLAIM_BAD:
			case -NFS4ERR_RECLAIM_CONFLICT:
				nfs4_state_mark_reclaim_nograce(sp->so_server->nfs_client, state);
				break;
			case -NFS4ERR_EXPIRED:
			case -NFS4ERR_NO_GRACE:
				nfs4_state_mark_reclaim_nograce(sp->so_server->nfs_client, state);
			case -NFS4ERR_STALE_CLIENTID:
			case -NFS4ERR_BADSESSION:
			case -NFS4ERR_BADSLOT:
			case -NFS4ERR_BAD_HIGH_SLOT:
			case -NFS4ERR_CONN_NOT_BOUND_TO_SESSION:
				goto out_err;
		}
		nfs4_put_open_state(state);
		goto restart;
	}
	spin_unlock(&sp->so_lock);
	return 0;
out_err:
	nfs4_put_open_state(state);
	return status;
}

static void nfs4_clear_open_state(struct nfs4_state *state)
{
	struct nfs4_lock_state *lock;

	clear_bit(NFS_DELEGATED_STATE, &state->flags);
	clear_bit(NFS_O_RDONLY_STATE, &state->flags);
	clear_bit(NFS_O_WRONLY_STATE, &state->flags);
	clear_bit(NFS_O_RDWR_STATE, &state->flags);
	spin_lock(&state->state_lock);
	list_for_each_entry(lock, &state->lock_states, ls_locks) {
		lock->ls_seqid.flags = 0;
		clear_bit(NFS_LOCK_INITIALIZED, &lock->ls_flags);
	}
	spin_unlock(&state->state_lock);
}

static void nfs4_reset_seqids(struct nfs_server *server,
	int (*mark_reclaim)(struct nfs_client *clp, struct nfs4_state *state))
{
	struct nfs_client *clp = server->nfs_client;
	struct nfs4_state_owner *sp;
	struct rb_node *pos;
	struct nfs4_state *state;

	spin_lock(&clp->cl_lock);
	for (pos = rb_first(&server->state_owners);
	     pos != NULL;
	     pos = rb_next(pos)) {
		sp = rb_entry(pos, struct nfs4_state_owner, so_server_node);
		sp->so_seqid.flags = 0;
		spin_lock(&sp->so_lock);
		list_for_each_entry(state, &sp->so_states, open_states) {
			if (mark_reclaim(clp, state))
				nfs4_clear_open_state(state);
		}
		spin_unlock(&sp->so_lock);
	}
	spin_unlock(&clp->cl_lock);
}

static void nfs4_state_mark_reclaim_helper(struct nfs_client *clp,
	int (*mark_reclaim)(struct nfs_client *clp, struct nfs4_state *state))
{
	struct nfs_server *server;

	rcu_read_lock();
	list_for_each_entry_rcu(server, &clp->cl_superblocks, client_link)
		nfs4_reset_seqids(server, mark_reclaim);
	rcu_read_unlock();
}

static void nfs4_state_start_reclaim_reboot(struct nfs_client *clp)
{
	/* Mark all delegations for reclaim */
	nfs_delegation_mark_reclaim(clp);
	nfs4_state_mark_reclaim_helper(clp, nfs4_state_mark_reclaim_reboot);
}

static void nfs4_reclaim_complete(struct nfs_client *clp,
				 const struct nfs4_state_recovery_ops *ops)
{
	/* Notify the server we're done reclaiming our state */
	if (ops->reclaim_complete)
		(void)ops->reclaim_complete(clp);
}

static void nfs4_clear_reclaim_server(struct nfs_server *server)
{
	struct nfs_client *clp = server->nfs_client;
	struct nfs4_state_owner *sp;
	struct rb_node *pos;
	struct nfs4_state *state;

	spin_lock(&clp->cl_lock);
	for (pos = rb_first(&server->state_owners);
	     pos != NULL;
	     pos = rb_next(pos)) {
		sp = rb_entry(pos, struct nfs4_state_owner, so_server_node);
		spin_lock(&sp->so_lock);
		list_for_each_entry(state, &sp->so_states, open_states) {
			if (!test_and_clear_bit(NFS_STATE_RECLAIM_REBOOT,
						&state->flags))
				continue;
			nfs4_state_mark_reclaim_nograce(clp, state);
		}
		spin_unlock(&sp->so_lock);
	}
	spin_unlock(&clp->cl_lock);
}

static int nfs4_state_clear_reclaim_reboot(struct nfs_client *clp)
{
	struct nfs_server *server;

	if (!test_and_clear_bit(NFS4CLNT_RECLAIM_REBOOT, &clp->cl_state))
		return 0;

	rcu_read_lock();
	list_for_each_entry_rcu(server, &clp->cl_superblocks, client_link)
		nfs4_clear_reclaim_server(server);
	rcu_read_unlock();

	nfs_delegation_reap_unclaimed(clp);
	return 1;
}

static void nfs4_state_end_reclaim_reboot(struct nfs_client *clp)
{
	if (!nfs4_state_clear_reclaim_reboot(clp))
		return;
	nfs4_reclaim_complete(clp, clp->cl_mvops->reboot_recovery_ops);
}

static void nfs_delegation_clear_all(struct nfs_client *clp)
{
	nfs_delegation_mark_reclaim(clp);
	nfs_delegation_reap_unclaimed(clp);
}

static void nfs4_state_start_reclaim_nograce(struct nfs_client *clp)
{
	nfs_delegation_clear_all(clp);
	nfs4_state_mark_reclaim_helper(clp, nfs4_state_mark_reclaim_nograce);
}

static void nfs4_warn_keyexpired(const char *s)
{
	printk_ratelimited(KERN_WARNING "Error: state manager"
			" encountered RPCSEC_GSS session"
			" expired against NFSv4 server %s.\n",
			s);
}

static int nfs4_recovery_handle_error(struct nfs_client *clp, int error)
{
	switch (error) {
		case 0:
			break;
		case -NFS4ERR_CB_PATH_DOWN:
			nfs40_handle_cb_pathdown(clp);
			break;
		case -NFS4ERR_NO_GRACE:
			nfs4_state_end_reclaim_reboot(clp);
			break;
		case -NFS4ERR_STALE_CLIENTID:
		case -NFS4ERR_LEASE_MOVED:
			set_bit(NFS4CLNT_LEASE_EXPIRED, &clp->cl_state);
			nfs4_state_clear_reclaim_reboot(clp);
			nfs4_state_start_reclaim_reboot(clp);
			break;
		case -NFS4ERR_EXPIRED:
			set_bit(NFS4CLNT_LEASE_EXPIRED, &clp->cl_state);
			nfs4_state_start_reclaim_nograce(clp);
			break;
		case -NFS4ERR_BADSESSION:
		case -NFS4ERR_BADSLOT:
		case -NFS4ERR_BAD_HIGH_SLOT:
		case -NFS4ERR_DEADSESSION:
		case -NFS4ERR_SEQ_FALSE_RETRY:
		case -NFS4ERR_SEQ_MISORDERED:
			set_bit(NFS4CLNT_SESSION_RESET, &clp->cl_state);
			/* Zero session reset errors */
			break;
		case -NFS4ERR_CONN_NOT_BOUND_TO_SESSION:
			set_bit(NFS4CLNT_BIND_CONN_TO_SESSION, &clp->cl_state);
			break;
		case -EKEYEXPIRED:
			/* Nothing we can do */
			nfs4_warn_keyexpired(clp->cl_hostname);
			break;
		default:
			dprintk("%s: failed to handle error %d for server %s\n",
					__func__, error, clp->cl_hostname);
			return error;
	}
	dprintk("%s: handled error %d for server %s\n", __func__, error,
			clp->cl_hostname);
	return 0;
}

static int nfs4_do_reclaim(struct nfs_client *clp, const struct nfs4_state_recovery_ops *ops)
{
	struct nfs4_state_owner *sp;
	struct nfs_server *server;
	struct rb_node *pos;
	int status = 0;

restart:
	rcu_read_lock();
	list_for_each_entry_rcu(server, &clp->cl_superblocks, client_link) {
		nfs4_purge_state_owners(server);
		spin_lock(&clp->cl_lock);
		for (pos = rb_first(&server->state_owners);
		     pos != NULL;
		     pos = rb_next(pos)) {
			sp = rb_entry(pos,
				struct nfs4_state_owner, so_server_node);
			if (!test_and_clear_bit(ops->owner_flag_bit,
							&sp->so_flags))
				continue;
			atomic_inc(&sp->so_count);
			spin_unlock(&clp->cl_lock);
			rcu_read_unlock();

			status = nfs4_reclaim_open_state(sp, ops);
			if (status < 0) {
				set_bit(ops->owner_flag_bit, &sp->so_flags);
				nfs4_put_state_owner(sp);
				return nfs4_recovery_handle_error(clp, status);
			}

			nfs4_put_state_owner(sp);
			goto restart;
		}
		spin_unlock(&clp->cl_lock);
	}
	rcu_read_unlock();
	return status;
}

static int nfs4_check_lease(struct nfs_client *clp)
{
	struct rpc_cred *cred;
	const struct nfs4_state_maintenance_ops *ops =
		clp->cl_mvops->state_renewal_ops;
	int status;

	/* Is the client already known to have an expired lease? */
	if (test_bit(NFS4CLNT_LEASE_EXPIRED, &clp->cl_state))
		return 0;
	spin_lock(&clp->cl_lock);
	cred = ops->get_state_renewal_cred_locked(clp);
	spin_unlock(&clp->cl_lock);
	if (cred == NULL) {
		cred = nfs4_get_setclientid_cred(clp);
		status = -ENOKEY;
		if (cred == NULL)
			goto out;
	}
	status = ops->renew_lease(clp, cred);
	put_rpccred(cred);
out:
	return nfs4_recovery_handle_error(clp, status);
}

/* Set NFS4CLNT_LEASE_EXPIRED and reclaim reboot state for all v4.0 errors
 * and for recoverable errors on EXCHANGE_ID for v4.1
 */
static int nfs4_handle_reclaim_lease_error(struct nfs_client *clp, int status)
{
	switch (status) {
	case -NFS4ERR_SEQ_MISORDERED:
		if (test_and_set_bit(NFS4CLNT_PURGE_STATE, &clp->cl_state))
			return -ESERVERFAULT;
		/* Lease confirmation error: retry after purging the lease */
		ssleep(1);
		clear_bit(NFS4CLNT_LEASE_CONFIRM, &clp->cl_state);
		break;
	case -NFS4ERR_STALE_CLIENTID:
		clear_bit(NFS4CLNT_LEASE_CONFIRM, &clp->cl_state);
		nfs4_state_clear_reclaim_reboot(clp);
		nfs4_state_start_reclaim_reboot(clp);
		break;
	case -NFS4ERR_CLID_INUSE:
		pr_err("NFS: Server %s reports our clientid is in use\n",
			clp->cl_hostname);
		nfs_mark_client_ready(clp, -EPERM);
		clear_bit(NFS4CLNT_LEASE_CONFIRM, &clp->cl_state);
		return -EPERM;
	case -EACCES:
		if (clp->cl_machine_cred == NULL)
			return -EACCES;
		/* Handle case where the user hasn't set up machine creds */
		nfs4_clear_machine_cred(clp);
	case -NFS4ERR_DELAY:
	case -ETIMEDOUT:
	case -EAGAIN:
		ssleep(1);
		break;

	case -NFS4ERR_MINOR_VERS_MISMATCH:
		if (clp->cl_cons_state == NFS_CS_SESSION_INITING)
			nfs_mark_client_ready(clp, -EPROTONOSUPPORT);
		dprintk("%s: exit with error %d for server %s\n",
				__func__, -EPROTONOSUPPORT, clp->cl_hostname);
		return -EPROTONOSUPPORT;
	case -EKEYEXPIRED:
		nfs4_warn_keyexpired(clp->cl_hostname);
	case -NFS4ERR_NOT_SAME: /* FixMe: implement recovery
				 * in nfs4_exchange_id */
	default:
		dprintk("%s: exit with error %d for server %s\n", __func__,
				status, clp->cl_hostname);
		return status;
	}
	set_bit(NFS4CLNT_LEASE_EXPIRED, &clp->cl_state);
	dprintk("%s: handled error %d for server %s\n", __func__, status,
			clp->cl_hostname);
	return 0;
}

static int nfs4_establish_lease(struct nfs_client *clp)
{
	struct rpc_cred *cred;
	const struct nfs4_state_recovery_ops *ops =
		clp->cl_mvops->reboot_recovery_ops;
	int status;

	cred = ops->get_clid_cred(clp);
	if (cred == NULL)
		return -ENOENT;
	status = ops->establish_clid(clp, cred);
	put_rpccred(cred);
	if (status != 0)
		return status;
	pnfs_destroy_all_layouts(clp);
	return 0;
}

/*
 * Returns zero or a negative errno.  NFS4ERR values are converted
 * to local errno values.
 */
static int nfs4_reclaim_lease(struct nfs_client *clp)
{
	int status;

	status = nfs4_establish_lease(clp);
	if (status < 0)
		return nfs4_handle_reclaim_lease_error(clp, status);
	if (test_and_clear_bit(NFS4CLNT_SERVER_SCOPE_MISMATCH, &clp->cl_state))
		nfs4_state_start_reclaim_nograce(clp);
	if (!test_bit(NFS4CLNT_RECLAIM_NOGRACE, &clp->cl_state))
		set_bit(NFS4CLNT_RECLAIM_REBOOT, &clp->cl_state);
	clear_bit(NFS4CLNT_CHECK_LEASE, &clp->cl_state);
	clear_bit(NFS4CLNT_LEASE_EXPIRED, &clp->cl_state);
	return 0;
}

static int nfs4_purge_lease(struct nfs_client *clp)
{
	int status;

	status = nfs4_establish_lease(clp);
	if (status < 0)
		return nfs4_handle_reclaim_lease_error(clp, status);
	clear_bit(NFS4CLNT_PURGE_STATE, &clp->cl_state);
	set_bit(NFS4CLNT_LEASE_EXPIRED, &clp->cl_state);
	nfs4_state_start_reclaim_nograce(clp);
	return 0;
}

/**
 * nfs4_discover_server_trunking - Detect server IP address trunking
 *
 * @clp: nfs_client under test
 * @result: OUT: found nfs_client, or clp
 *
 * Returns zero or a negative errno.  If zero is returned,
 * an nfs_client pointer is planted in "result".
 *
 * Note: since we are invoked in process context, and
 * not from inside the state manager, we cannot use
 * nfs4_handle_reclaim_lease_error().
 */
int nfs4_discover_server_trunking(struct nfs_client *clp,
				  struct nfs_client **result)
{
	const struct nfs4_state_recovery_ops *ops =
				clp->cl_mvops->reboot_recovery_ops;
	rpc_authflavor_t *flavors, flav, save;
	struct rpc_clnt *clnt;
	struct rpc_cred *cred;
	int i, len, status;

	dprintk("NFS: %s: testing '%s'\n", __func__, clp->cl_hostname);

	len = NFS_MAX_SECFLAVORS;
	flavors = kcalloc(len, sizeof(*flavors), GFP_KERNEL);
	if (flavors == NULL) {
		status = -ENOMEM;
		goto out;
	}
	len = rpcauth_list_flavors(flavors, len);
	if (len < 0) {
		status = len;
		goto out_free;
	}
	clnt = clp->cl_rpcclient;
	save = clnt->cl_auth->au_flavor;
	i = 0;

	mutex_lock(&nfs_clid_init_mutex);
	status  = -ENOENT;
again:
	cred = ops->get_clid_cred(clp);
	if (cred == NULL)
		goto out_unlock;

	status = ops->detect_trunking(clp, result, cred);
	put_rpccred(cred);
	switch (status) {
	case 0:
		break;

	case -EACCES:
		if (clp->cl_machine_cred == NULL)
			break;
		/* Handle case where the user hasn't set up machine creds */
		nfs4_clear_machine_cred(clp);
	case -NFS4ERR_DELAY:
	case -ETIMEDOUT:
	case -EAGAIN:
		ssleep(1);
		dprintk("NFS: %s after status %d, retrying\n",
			__func__, status);
		goto again;

	case -NFS4ERR_CLID_INUSE:
	case -NFS4ERR_WRONGSEC:
		status = -EPERM;
		if (i >= len)
			break;

		flav = flavors[i++];
		if (flav == save)
			flav = flavors[i++];
		clnt = rpc_clone_client_set_auth(clnt, flav);
		if (IS_ERR(clnt)) {
			status = PTR_ERR(clnt);
			break;
		}
		clp->cl_rpcclient = clnt;
		goto again;

	case -NFS4ERR_MINOR_VERS_MISMATCH:
		status = -EPROTONOSUPPORT;
		break;

	case -EKEYEXPIRED:
		nfs4_warn_keyexpired(clp->cl_hostname);
	case -NFS4ERR_NOT_SAME: /* FixMe: implement recovery
				 * in nfs4_exchange_id */
		status = -EKEYEXPIRED;
	}

out_unlock:
	mutex_unlock(&nfs_clid_init_mutex);
out_free:
	kfree(flavors);
out:
	dprintk("NFS: %s: status = %d\n", __func__, status);
	return status;
}

#ifdef CONFIG_NFS_V4_1
void nfs4_schedule_session_recovery(struct nfs4_session *session, int err)
{
	struct nfs_client *clp = session->clp;

	switch (err) {
	default:
		set_bit(NFS4CLNT_SESSION_RESET, &clp->cl_state);
		break;
	case -NFS4ERR_CONN_NOT_BOUND_TO_SESSION:
		set_bit(NFS4CLNT_BIND_CONN_TO_SESSION, &clp->cl_state);
	}
	nfs4_schedule_lease_recovery(clp);
}
EXPORT_SYMBOL_GPL(nfs4_schedule_session_recovery);

void nfs41_handle_recall_slot(struct nfs_client *clp)
{
	set_bit(NFS4CLNT_RECALL_SLOT, &clp->cl_state);
	dprintk("%s: scheduling slot recall for server %s\n", __func__,
			clp->cl_hostname);
	nfs4_schedule_state_manager(clp);
}

static void nfs4_reset_all_state(struct nfs_client *clp)
{
	if (test_and_set_bit(NFS4CLNT_LEASE_EXPIRED, &clp->cl_state) == 0) {
		set_bit(NFS4CLNT_PURGE_STATE, &clp->cl_state);
		clear_bit(NFS4CLNT_LEASE_CONFIRM, &clp->cl_state);
		nfs4_state_start_reclaim_nograce(clp);
		dprintk("%s: scheduling reset of all state for server %s!\n",
				__func__, clp->cl_hostname);
		nfs4_schedule_state_manager(clp);
	}
}

static void nfs41_handle_server_reboot(struct nfs_client *clp)
{
	if (test_and_set_bit(NFS4CLNT_LEASE_EXPIRED, &clp->cl_state) == 0) {
		nfs4_state_start_reclaim_reboot(clp);
		dprintk("%s: server %s rebooted!\n", __func__,
				clp->cl_hostname);
		nfs4_schedule_state_manager(clp);
	}
}

static void nfs41_handle_state_revoked(struct nfs_client *clp)
{
	nfs4_reset_all_state(clp);
	dprintk("%s: state revoked on server %s\n", __func__, clp->cl_hostname);
}

static void nfs41_handle_recallable_state_revoked(struct nfs_client *clp)
{
	/* This will need to handle layouts too */
	nfs_expire_all_delegations(clp);
	dprintk("%s: Recallable state revoked on server %s!\n", __func__,
			clp->cl_hostname);
}

static void nfs41_handle_backchannel_fault(struct nfs_client *clp)
{
	nfs_expire_all_delegations(clp);
	if (test_and_set_bit(NFS4CLNT_SESSION_RESET, &clp->cl_state) == 0)
		nfs4_schedule_state_manager(clp);
	dprintk("%s: server %s declared a backchannel fault\n", __func__,
			clp->cl_hostname);
}

static void nfs41_handle_cb_path_down(struct nfs_client *clp)
{
	if (test_and_set_bit(NFS4CLNT_BIND_CONN_TO_SESSION,
		&clp->cl_state) == 0)
		nfs4_schedule_state_manager(clp);
}

void nfs41_handle_sequence_flag_errors(struct nfs_client *clp, u32 flags)
{
	if (!flags)
		return;

	dprintk("%s: \"%s\" (client ID %llx) flags=0x%08x\n",
		__func__, clp->cl_hostname, clp->cl_clientid, flags);

	if (flags & SEQ4_STATUS_RESTART_RECLAIM_NEEDED)
		nfs41_handle_server_reboot(clp);
	if (flags & (SEQ4_STATUS_EXPIRED_ALL_STATE_REVOKED |
			    SEQ4_STATUS_EXPIRED_SOME_STATE_REVOKED |
			    SEQ4_STATUS_ADMIN_STATE_REVOKED |
			    SEQ4_STATUS_LEASE_MOVED))
		nfs41_handle_state_revoked(clp);
	if (flags & SEQ4_STATUS_RECALLABLE_STATE_REVOKED)
		nfs41_handle_recallable_state_revoked(clp);
	if (flags & SEQ4_STATUS_BACKCHANNEL_FAULT)
		nfs41_handle_backchannel_fault(clp);
	else if (flags & (SEQ4_STATUS_CB_PATH_DOWN |
				SEQ4_STATUS_CB_PATH_DOWN_SESSION))
		nfs41_handle_cb_path_down(clp);
}

static int nfs4_reset_session(struct nfs_client *clp)
{
	struct rpc_cred *cred;
	int status;

	if (!nfs4_has_session(clp))
		return 0;
	nfs4_begin_drain_session(clp);
	cred = nfs4_get_exchange_id_cred(clp);
	status = nfs4_proc_destroy_session(clp->cl_session, cred);
	if (status && status != -NFS4ERR_BADSESSION &&
	    status != -NFS4ERR_DEADSESSION) {
		status = nfs4_recovery_handle_error(clp, status);
		goto out;
	}

	memset(clp->cl_session->sess_id.data, 0, NFS4_MAX_SESSIONID_LEN);
	status = nfs4_proc_create_session(clp, cred);
	if (status) {
		dprintk("%s: session reset failed with status %d for server %s!\n",
			__func__, status, clp->cl_hostname);
		status = nfs4_handle_reclaim_lease_error(clp, status);
		goto out;
	}
	nfs41_finish_session_reset(clp);
	dprintk("%s: session reset was successful for server %s!\n",
			__func__, clp->cl_hostname);
out:
	if (cred)
		put_rpccred(cred);
	return status;
}

static int nfs4_recall_slot(struct nfs_client *clp)
{
	struct nfs4_slot_table *fc_tbl;
	struct nfs4_slot *new, *old;
	int i;

	if (!nfs4_has_session(clp))
		return 0;
	nfs4_begin_drain_session(clp);
	fc_tbl = &clp->cl_session->fc_slot_table;
	new = kmalloc(fc_tbl->target_max_slots * sizeof(struct nfs4_slot),
		      GFP_NOFS);
        if (!new)
		return -ENOMEM;

	spin_lock(&fc_tbl->slot_tbl_lock);
	for (i = 0; i < fc_tbl->target_max_slots; i++)
		new[i].seq_nr = fc_tbl->slots[i].seq_nr;
	old = fc_tbl->slots;
	fc_tbl->slots = new;
	fc_tbl->max_slots = fc_tbl->target_max_slots;
	fc_tbl->target_max_slots = 0;
	clp->cl_session->fc_attrs.max_reqs = fc_tbl->max_slots;
	spin_unlock(&fc_tbl->slot_tbl_lock);

	kfree(old);
	return 0;
}

static int nfs4_bind_conn_to_session(struct nfs_client *clp)
{
	struct rpc_cred *cred;
	int ret;

	if (!nfs4_has_session(clp))
		return 0;
	nfs4_begin_drain_session(clp);
	cred = nfs4_get_exchange_id_cred(clp);
	ret = nfs4_proc_bind_conn_to_session(clp, cred);
	if (cred)
		put_rpccred(cred);
	clear_bit(NFS4CLNT_BIND_CONN_TO_SESSION, &clp->cl_state);
	switch (ret) {
	case 0:
		dprintk("%s: bind_conn_to_session was successful for server %s!\n",
			__func__, clp->cl_hostname);
		break;
	case -NFS4ERR_DELAY:
		ssleep(1);
		set_bit(NFS4CLNT_BIND_CONN_TO_SESSION, &clp->cl_state);
		break;
	default:
		return nfs4_recovery_handle_error(clp, ret);
	}
	return 0;
}
#else /* CONFIG_NFS_V4_1 */
static int nfs4_reset_session(struct nfs_client *clp) { return 0; }
static int nfs4_end_drain_session(struct nfs_client *clp) { return 0; }
static int nfs4_recall_slot(struct nfs_client *clp) { return 0; }

static int nfs4_bind_conn_to_session(struct nfs_client *clp)
{
	return 0;
}
#endif /* CONFIG_NFS_V4_1 */

static void nfs4_state_manager(struct nfs_client *clp)
{
	int status = 0;
	const char *section = "", *section_sep = "";

	/* Ensure exclusive access to NFSv4 state */
	do {
		if (test_bit(NFS4CLNT_PURGE_STATE, &clp->cl_state)) {
			section = "purge state";
			status = nfs4_purge_lease(clp);
			if (status < 0)
				goto out_error;
			continue;
		}

		if (test_bit(NFS4CLNT_LEASE_EXPIRED, &clp->cl_state)) {
			section = "lease expired";
			/* We're going to have to re-establish a clientid */
			status = nfs4_reclaim_lease(clp);
			if (status < 0)
				goto out_error;
			continue;
		}

		/* Initialize or reset the session */
		if (test_and_clear_bit(NFS4CLNT_SESSION_RESET, &clp->cl_state)) {
			section = "reset session";
			status = nfs4_reset_session(clp);
			if (test_bit(NFS4CLNT_LEASE_EXPIRED, &clp->cl_state))
				continue;
			if (status < 0)
				goto out_error;
		}

		/* Send BIND_CONN_TO_SESSION */
		if (test_and_clear_bit(NFS4CLNT_BIND_CONN_TO_SESSION,
				&clp->cl_state)) {
			section = "bind conn to session";
			status = nfs4_bind_conn_to_session(clp);
			if (status < 0)
				goto out_error;
			continue;
		}

		if (test_and_clear_bit(NFS4CLNT_CHECK_LEASE, &clp->cl_state)) {
			section = "check lease";
			status = nfs4_check_lease(clp);
			if (status < 0)
				goto out_error;
			continue;
		}

		/* Recall session slots */
		if (test_and_clear_bit(NFS4CLNT_RECALL_SLOT, &clp->cl_state)) {
			section = "recall slot";
			status = nfs4_recall_slot(clp);
			if (status < 0)
				goto out_error;
			continue;
		}

		/* First recover reboot state... */
		if (test_bit(NFS4CLNT_RECLAIM_REBOOT, &clp->cl_state)) {
			section = "reclaim reboot";
			status = nfs4_do_reclaim(clp,
				clp->cl_mvops->reboot_recovery_ops);
			if (test_bit(NFS4CLNT_LEASE_EXPIRED, &clp->cl_state) ||
			    test_bit(NFS4CLNT_SESSION_RESET, &clp->cl_state))
				continue;
			nfs4_state_end_reclaim_reboot(clp);
			if (test_bit(NFS4CLNT_RECLAIM_NOGRACE, &clp->cl_state))
				continue;
			if (status < 0)
				goto out_error;
		}

		/* Now recover expired state... */
		if (test_and_clear_bit(NFS4CLNT_RECLAIM_NOGRACE, &clp->cl_state)) {
			section = "reclaim nograce";
			status = nfs4_do_reclaim(clp,
				clp->cl_mvops->nograce_recovery_ops);
			if (test_bit(NFS4CLNT_LEASE_EXPIRED, &clp->cl_state) ||
			    test_bit(NFS4CLNT_SESSION_RESET, &clp->cl_state) ||
			    test_bit(NFS4CLNT_RECLAIM_REBOOT, &clp->cl_state))
				continue;
			if (status < 0)
				goto out_error;
		}

		nfs4_end_drain_session(clp);
		if (test_and_clear_bit(NFS4CLNT_DELEGRETURN, &clp->cl_state)) {
			nfs_client_return_marked_delegations(clp);
			continue;
		}

		nfs4_clear_state_manager_bit(clp);
		/* Did we race with an attempt to give us more work? */
		if (clp->cl_state == 0)
			break;
		if (test_and_set_bit(NFS4CLNT_MANAGER_RUNNING, &clp->cl_state) != 0)
			break;
	} while (atomic_read(&clp->cl_count) > 1);
	return;
out_error:
	if (strlen(section))
		section_sep = ": ";
	pr_warn_ratelimited("NFS: state manager%s%s failed on NFSv4 server %s"
			" with error %d\n", section_sep, section,
			clp->cl_hostname, -status);
	ssleep(1);
	nfs4_end_drain_session(clp);
	nfs4_clear_state_manager_bit(clp);
}

static int nfs4_run_state_manager(void *ptr)
{
	struct nfs_client *clp = ptr;

	allow_signal(SIGKILL);
	nfs4_state_manager(clp);
	nfs_put_client(clp);
	module_put_and_exit(0);
	return 0;
}

/*
 * Local variables:
 *  c-basic-offset: 8
 * End:
 */
