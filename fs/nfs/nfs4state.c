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

#include "nfs4_fs.h"
#include "callback.h"
#include "delegation.h"
#include "internal.h"
#include "pnfs.h"

#define OPENOWNER_POOL_SIZE	8

const nfs4_stateid zero_stateid;

static LIST_HEAD(nfs4_clientid_list);

int nfs4_init_clientid(struct nfs_client *clp, struct rpc_cred *cred)
{
	struct nfs4_setclientid_res clid = {
		.clientid = clp->cl_clientid,
		.confirm = clp->cl_confirm,
	};
	unsigned short port;
	int status;

	if (test_bit(NFS4CLNT_LEASE_CONFIRM, &clp->cl_state))
		goto do_confirm;
	port = nfs_callback_tcpport;
	if (clp->cl_addr.ss_family == AF_INET6)
		port = nfs_callback_tcpport6;

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

	rcu_read_lock();
	list_for_each_entry_rcu(server, &clp->cl_superblocks, client_link) {
		cred = nfs4_get_renew_cred_server_locked(server);
		if (cred != NULL)
			break;
	}
	rcu_read_unlock();
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
	int max_slots;

	if (ses == NULL)
		return;
	if (test_and_clear_bit(NFS4_SESSION_DRAINING, &ses->session_state)) {
		spin_lock(&ses->fc_slot_table.slot_tbl_lock);
		max_slots = ses->fc_slot_table.max_slots;
		while (max_slots--) {
			struct rpc_task *task;

			task = rpc_wake_up_next(&ses->fc_slot_table.
						slot_tbl_waitq);
			if (!task)
				break;
			rpc_task_set_priority(task, RPC_PRIORITY_PRIVILEGED);
		}
		spin_unlock(&ses->fc_slot_table.slot_tbl_lock);
	}
}

static int nfs4_wait_on_slot_tbl(struct nfs4_slot_table *tbl)
{
	spin_lock(&tbl->slot_tbl_lock);
	if (tbl->highest_used_slotid != -1) {
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
	status = nfs4_proc_create_session(clp);
	if (status != 0)
		goto out;
	clear_bit(NFS4CLNT_LEASE_CONFIRM, &clp->cl_state);
	nfs41_setup_state_renewal(clp);
	nfs_mark_client_ready(clp, NFS_CS_READY);
out:
	return status;
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

static void nfs_alloc_unique_id_locked(struct rb_root *root,
				       struct nfs_unique_id *new,
				       __u64 minval, int maxbits)
{
	struct rb_node **p, *parent;
	struct nfs_unique_id *pos;
	__u64 mask = ~0ULL;

	if (maxbits < 64)
		mask = (1ULL << maxbits) - 1ULL;

	/* Ensure distribution is more or less flat */
	get_random_bytes(&new->id, sizeof(new->id));
	new->id &= mask;
	if (new->id < minval)
		new->id += minval;
retry:
	p = &root->rb_node;
	parent = NULL;

	while (*p != NULL) {
		parent = *p;
		pos = rb_entry(parent, struct nfs_unique_id, rb_node);

		if (new->id < pos->id)
			p = &(*p)->rb_left;
		else if (new->id > pos->id)
			p = &(*p)->rb_right;
		else
			goto id_exists;
	}
	rb_link_node(&new->rb_node, parent, p);
	rb_insert_color(&new->rb_node, root);
	return;
id_exists:
	for (;;) {
		new->id++;
		if (new->id < minval || (new->id & mask) != new->id) {
			new->id = minval;
			break;
		}
		parent = rb_next(parent);
		if (parent == NULL)
			break;
		pos = rb_entry(parent, struct nfs_unique_id, rb_node);
		if (new->id < pos->id)
			break;
	}
	goto retry;
}

static void nfs_free_unique_id(struct rb_root *root, struct nfs_unique_id *id)
{
	rb_erase(&id->rb_node, root);
}

static struct nfs4_state_owner *
nfs4_find_state_owner_locked(struct nfs_server *server, struct rpc_cred *cred)
{
	struct rb_node **p = &server->state_owners.rb_node,
		       *parent = NULL;
	struct nfs4_state_owner *sp, *res = NULL;

	while (*p != NULL) {
		parent = *p;
		sp = rb_entry(parent, struct nfs4_state_owner, so_server_node);

		if (server < sp->so_server) {
			p = &parent->rb_left;
			continue;
		}
		if (server > sp->so_server) {
			p = &parent->rb_right;
			continue;
		}
		if (cred < sp->so_cred)
			p = &parent->rb_left;
		else if (cred > sp->so_cred)
			p = &parent->rb_right;
		else {
			atomic_inc(&sp->so_count);
			res = sp;
			break;
		}
	}
	return res;
}

static struct nfs4_state_owner *
nfs4_insert_state_owner_locked(struct nfs4_state_owner *new)
{
	struct nfs_server *server = new->so_server;
	struct rb_node **p = &server->state_owners.rb_node,
		       *parent = NULL;
	struct nfs4_state_owner *sp;

	while (*p != NULL) {
		parent = *p;
		sp = rb_entry(parent, struct nfs4_state_owner, so_server_node);

		if (new->so_cred < sp->so_cred)
			p = &parent->rb_left;
		else if (new->so_cred > sp->so_cred)
			p = &parent->rb_right;
		else {
			atomic_inc(&sp->so_count);
			return sp;
		}
	}
	nfs_alloc_unique_id_locked(&server->openowner_id,
					&new->so_owner_id, 1, 64);
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
	nfs_free_unique_id(&server->openowner_id, &sp->so_owner_id);
}

/*
 * nfs4_alloc_state_owner(): this is called on the OPEN or CREATE path to
 * create a new state_owner.
 *
 */
static struct nfs4_state_owner *
nfs4_alloc_state_owner(void)
{
	struct nfs4_state_owner *sp;

	sp = kzalloc(sizeof(*sp),GFP_NOFS);
	if (!sp)
		return NULL;
	spin_lock_init(&sp->so_lock);
	INIT_LIST_HEAD(&sp->so_states);
	rpc_init_wait_queue(&sp->so_sequence.wait, "Seqid_waitqueue");
	sp->so_seqid.sequence = &sp->so_sequence;
	spin_lock_init(&sp->so_sequence.lock);
	INIT_LIST_HEAD(&sp->so_sequence.list);
	atomic_set(&sp->so_count, 1);
	return sp;
}

static void
nfs4_drop_state_owner(struct nfs4_state_owner *sp)
{
	if (!RB_EMPTY_NODE(&sp->so_server_node)) {
		struct nfs_server *server = sp->so_server;
		struct nfs_client *clp = server->nfs_client;

		spin_lock(&clp->cl_lock);
		rb_erase(&sp->so_server_node, &server->state_owners);
		RB_CLEAR_NODE(&sp->so_server_node);
		spin_unlock(&clp->cl_lock);
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
					      struct rpc_cred *cred)
{
	struct nfs_client *clp = server->nfs_client;
	struct nfs4_state_owner *sp, *new;

	spin_lock(&clp->cl_lock);
	sp = nfs4_find_state_owner_locked(server, cred);
	spin_unlock(&clp->cl_lock);
	if (sp != NULL)
		return sp;
	new = nfs4_alloc_state_owner();
	if (new == NULL)
		return NULL;
	new->so_server = server;
	new->so_cred = cred;
	spin_lock(&clp->cl_lock);
	sp = nfs4_insert_state_owner_locked(new);
	spin_unlock(&clp->cl_lock);
	if (sp == new)
		get_rpccred(cred);
	else {
		rpc_destroy_wait_queue(&new->so_sequence.wait);
		kfree(new);
	}
	return sp;
}

/**
 * nfs4_put_state_owner - Release a nfs4_state_owner
 * @sp: state owner data to release
 *
 */
void nfs4_put_state_owner(struct nfs4_state_owner *sp)
{
	struct nfs_client *clp = sp->so_server->nfs_client;
	struct rpc_cred *cred = sp->so_cred;

	if (!atomic_dec_and_lock(&sp->so_count, &clp->cl_lock))
		return;
	nfs4_remove_state_owner_locked(sp);
	spin_unlock(&clp->cl_lock);
	rpc_destroy_wait_queue(&sp->so_sequence.wait);
	put_rpccred(cred);
	kfree(sp);
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
static void __nfs4_close(struct path *path, struct nfs4_state *state,
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
	} else {
		bool roc = pnfs_roc(state->inode);

		nfs4_do_close(path, state, gfp_mask, wait, roc);
	}
}

void nfs4_close_state(struct path *path, struct nfs4_state *state, fmode_t fmode)
{
	__nfs4_close(path, state, fmode, GFP_NOFS, 0);
}

void nfs4_close_sync(struct path *path, struct nfs4_state *state, fmode_t fmode)
{
	__nfs4_close(path, state, fmode, GFP_KERNEL, 1);
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
	struct nfs_client *clp = server->nfs_client;

	lsp = kzalloc(sizeof(*lsp), GFP_NOFS);
	if (lsp == NULL)
		return NULL;
	rpc_init_wait_queue(&lsp->ls_sequence.wait, "lock_seqid_waitqueue");
	spin_lock_init(&lsp->ls_sequence.lock);
	INIT_LIST_HEAD(&lsp->ls_sequence.list);
	lsp->ls_seqid.sequence = &lsp->ls_sequence;
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
		kfree(lsp);
		return NULL;
	}
	spin_lock(&clp->cl_lock);
	nfs_alloc_unique_id_locked(&server->lockowner_id, &lsp->ls_id, 1, 64);
	spin_unlock(&clp->cl_lock);
	INIT_LIST_HEAD(&lsp->ls_locks);
	return lsp;
}

static void nfs4_free_lock_state(struct nfs4_lock_state *lsp)
{
	struct nfs_server *server = lsp->ls_state->owner->so_server;
	struct nfs_client *clp = server->nfs_client;

	spin_lock(&clp->cl_lock);
	nfs_free_unique_id(&server->lockowner_id, &lsp->ls_id);
	spin_unlock(&clp->cl_lock);
	rpc_destroy_wait_queue(&lsp->ls_sequence.wait);
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
		nfs4_free_lock_state(new);
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
	if (lsp->ls_flags & NFS_LOCK_INITIALIZED)
		nfs4_release_lockowner(lsp);
	nfs4_free_lock_state(lsp);
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
		lsp = nfs4_get_lock_state(state, 0, fl->fl_pid, NFS4_FLOCK_LOCK_TYPE);
	else
		return -EINVAL;
	if (lsp == NULL)
		return -ENOMEM;
	fl->fl_u.nfs4_fl.owner = lsp;
	fl->fl_ops = &nfs4_fl_lock_ops;
	return 0;
}

/*
 * Byte-range lock aware utility to initialize the stateid of read/write
 * requests.
 */
void nfs4_copy_stateid(nfs4_stateid *dst, struct nfs4_state *state, fl_owner_t fl_owner, pid_t fl_pid)
{
	struct nfs4_lock_state *lsp;
	int seq;

	do {
		seq = read_seqbegin(&state->seqlock);
		memcpy(dst, &state->stateid, sizeof(*dst));
	} while (read_seqretry(&state->seqlock, seq));
	if (test_bit(LK_STATE_IN_USE, &state->flags) == 0)
		return;

	spin_lock(&state->state_lock);
	lsp = __nfs4_find_lock_state(state, fl_owner, fl_pid, NFS4_ANY_LOCK_TYPE);
	if (lsp != NULL && (lsp->ls_flags & NFS_LOCK_INITIALIZED) != 0)
		memcpy(dst, &lsp->ls_stateid, sizeof(*dst));
	spin_unlock(&state->state_lock);
	nfs4_put_lock_state(lsp);
}

struct nfs_seqid *nfs_alloc_seqid(struct nfs_seqid_counter *counter, gfp_t gfp_mask)
{
	struct nfs_seqid *new;

	new = kmalloc(sizeof(*new), gfp_mask);
	if (new != NULL) {
		new->sequence = counter;
		INIT_LIST_HEAD(&new->list);
	}
	return new;
}

void nfs_release_seqid(struct nfs_seqid *seqid)
{
	if (!list_empty(&seqid->list)) {
		struct rpc_sequence *sequence = seqid->sequence->sequence;

		spin_lock(&sequence->lock);
		list_del_init(&seqid->list);
		spin_unlock(&sequence->lock);
		rpc_wake_up(&sequence->wait);
	}
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
	BUG_ON(list_first_entry(&seqid->sequence->sequence->list, struct nfs_seqid, list) != seqid);
	switch (status) {
		case 0:
			break;
		case -NFS4ERR_BAD_SEQID:
			if (seqid->sequence->flags & NFS_SEQID_CONFIRMED)
				return;
			printk(KERN_WARNING "NFS: v4 server returned a bad"
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
	struct rpc_sequence *sequence = seqid->sequence->sequence;
	int status = 0;

	spin_lock(&sequence->lock);
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

	if (test_and_set_bit(NFS4CLNT_MANAGER_RUNNING, &clp->cl_state) != 0)
		return;
	__module_get(THIS_MODULE);
	atomic_inc(&clp->cl_count);
	task = kthread_run(nfs4_run_state_manager, clp, "%s-manager",
				rpc_peeraddr2str(clp->cl_rpcclient,
							RPC_DISPLAY_ADDR));
	if (!IS_ERR(task))
		return;
	nfs4_clear_state_manager_bit(clp);
	nfs_put_client(clp);
	module_put(THIS_MODULE);
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
				printk(KERN_ERR "%s: unhandled error %d. Zeroing state\n",
						__func__, status);
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
				list_for_each_entry(lock, &state->lock_states, ls_locks) {
					if (!(lock->ls_flags & NFS_LOCK_INITIALIZED))
						printk("%s: Lock reclaim failed!\n",
							__func__);
				}
				nfs4_put_open_state(state);
				goto restart;
			}
		}
		switch (status) {
			default:
				printk(KERN_ERR "%s: unhandled error %d. Zeroing state\n",
						__func__, status);
			case -ENOENT:
			case -ENOMEM:
			case -ESTALE:
				/*
				 * Open state on this file cannot be recovered
				 * All we can do is revert to using the zero stateid.
				 */
				memset(state->stateid.data, 0,
					sizeof(state->stateid.data));
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
	list_for_each_entry(lock, &state->lock_states, ls_locks) {
		lock->ls_seqid.flags = 0;
		lock->ls_flags &= ~NFS_LOCK_INITIALIZED;
	}
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
		case -NFS4ERR_CB_PATH_DOWN:
			nfs_handle_cb_pathdown(clp);
			return 0;
		case -NFS4ERR_NO_GRACE:
			nfs4_state_end_reclaim_reboot(clp);
			return 0;
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
		case -NFS4ERR_CONN_NOT_BOUND_TO_SESSION:
		case -NFS4ERR_SEQ_FALSE_RETRY:
		case -NFS4ERR_SEQ_MISORDERED:
			set_bit(NFS4CLNT_SESSION_RESET, &clp->cl_state);
			/* Zero session reset errors */
			return 0;
		case -EKEYEXPIRED:
			/* Nothing we can do */
			nfs4_warn_keyexpired(clp->cl_hostname);
			return 0;
	}
	return error;
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
	int status = -NFS4ERR_EXPIRED;

	/* Is the client already known to have an expired lease? */
	if (test_bit(NFS4CLNT_LEASE_EXPIRED, &clp->cl_state))
		return 0;
	spin_lock(&clp->cl_lock);
	cred = ops->get_state_renewal_cred_locked(clp);
	spin_unlock(&clp->cl_lock);
	if (cred == NULL) {
		cred = nfs4_get_setclientid_cred(clp);
		if (cred == NULL)
			goto out;
	}
	status = ops->renew_lease(clp, cred);
	put_rpccred(cred);
out:
	return nfs4_recovery_handle_error(clp, status);
}

static int nfs4_reclaim_lease(struct nfs_client *clp)
{
	struct rpc_cred *cred;
	const struct nfs4_state_recovery_ops *ops =
		clp->cl_mvops->reboot_recovery_ops;
	int status = -ENOENT;

	cred = ops->get_clid_cred(clp);
	if (cred != NULL) {
		status = ops->establish_clid(clp, cred);
		put_rpccred(cred);
		/* Handle case where the user hasn't set up machine creds */
		if (status == -EACCES && cred == clp->cl_machine_cred) {
			nfs4_clear_machine_cred(clp);
			status = -EAGAIN;
		}
		if (status == -NFS4ERR_MINOR_VERS_MISMATCH)
			status = -EPROTONOSUPPORT;
	}
	return status;
}

#ifdef CONFIG_NFS_V4_1
void nfs4_schedule_session_recovery(struct nfs4_session *session)
{
	nfs4_schedule_lease_recovery(session->clp);
}
EXPORT_SYMBOL_GPL(nfs4_schedule_session_recovery);

void nfs41_handle_recall_slot(struct nfs_client *clp)
{
	set_bit(NFS4CLNT_RECALL_SLOT, &clp->cl_state);
	nfs4_schedule_state_manager(clp);
}

static void nfs4_reset_all_state(struct nfs_client *clp)
{
	if (test_and_set_bit(NFS4CLNT_LEASE_EXPIRED, &clp->cl_state) == 0) {
		clp->cl_boot_time = CURRENT_TIME;
		nfs4_state_start_reclaim_nograce(clp);
		nfs4_schedule_state_manager(clp);
	}
}

static void nfs41_handle_server_reboot(struct nfs_client *clp)
{
	if (test_and_set_bit(NFS4CLNT_LEASE_EXPIRED, &clp->cl_state) == 0) {
		nfs4_state_start_reclaim_reboot(clp);
		nfs4_schedule_state_manager(clp);
	}
}

static void nfs41_handle_state_revoked(struct nfs_client *clp)
{
	/* Temporary */
	nfs4_reset_all_state(clp);
}

static void nfs41_handle_recallable_state_revoked(struct nfs_client *clp)
{
	/* This will need to handle layouts too */
	nfs_expire_all_delegations(clp);
}

static void nfs41_handle_cb_path_down(struct nfs_client *clp)
{
	nfs_expire_all_delegations(clp);
	if (test_and_set_bit(NFS4CLNT_SESSION_RESET, &clp->cl_state) == 0)
		nfs4_schedule_state_manager(clp);
}

void nfs41_handle_sequence_flag_errors(struct nfs_client *clp, u32 flags)
{
	if (!flags)
		return;
	else if (flags & SEQ4_STATUS_RESTART_RECLAIM_NEEDED)
		nfs41_handle_server_reboot(clp);
	else if (flags & (SEQ4_STATUS_EXPIRED_ALL_STATE_REVOKED |
			    SEQ4_STATUS_EXPIRED_SOME_STATE_REVOKED |
			    SEQ4_STATUS_ADMIN_STATE_REVOKED |
			    SEQ4_STATUS_LEASE_MOVED))
		nfs41_handle_state_revoked(clp);
	else if (flags & SEQ4_STATUS_RECALLABLE_STATE_REVOKED)
		nfs41_handle_recallable_state_revoked(clp);
	else if (flags & (SEQ4_STATUS_CB_PATH_DOWN |
			    SEQ4_STATUS_BACKCHANNEL_FAULT |
			    SEQ4_STATUS_CB_PATH_DOWN_SESSION))
		nfs41_handle_cb_path_down(clp);
}

static int nfs4_reset_session(struct nfs_client *clp)
{
	int status;

	nfs4_begin_drain_session(clp);
	status = nfs4_proc_destroy_session(clp->cl_session);
	if (status && status != -NFS4ERR_BADSESSION &&
	    status != -NFS4ERR_DEADSESSION) {
		status = nfs4_recovery_handle_error(clp, status);
		goto out;
	}

	memset(clp->cl_session->sess_id.data, 0, NFS4_MAX_SESSIONID_LEN);
	status = nfs4_proc_create_session(clp);
	if (status) {
		status = nfs4_recovery_handle_error(clp, status);
		goto out;
	}
	/* create_session negotiated new slot table */
	clear_bit(NFS4CLNT_RECALL_SLOT, &clp->cl_state);

	 /* Let the state manager reestablish state */
	if (!test_bit(NFS4CLNT_LEASE_EXPIRED, &clp->cl_state))
		nfs41_setup_state_renewal(clp);
out:
	return status;
}

static int nfs4_recall_slot(struct nfs_client *clp)
{
	struct nfs4_slot_table *fc_tbl = &clp->cl_session->fc_slot_table;
	struct nfs4_channel_attrs *fc_attrs = &clp->cl_session->fc_attrs;
	struct nfs4_slot *new, *old;
	int i;

	nfs4_begin_drain_session(clp);
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
	fc_attrs->max_reqs = fc_tbl->max_slots;
	spin_unlock(&fc_tbl->slot_tbl_lock);

	kfree(old);
	nfs4_end_drain_session(clp);
	return 0;
}

#else /* CONFIG_NFS_V4_1 */
static int nfs4_reset_session(struct nfs_client *clp) { return 0; }
static int nfs4_end_drain_session(struct nfs_client *clp) { return 0; }
static int nfs4_recall_slot(struct nfs_client *clp) { return 0; }
#endif /* CONFIG_NFS_V4_1 */

/* Set NFS4CLNT_LEASE_EXPIRED for all v4.0 errors and for recoverable errors
 * on EXCHANGE_ID for v4.1
 */
static void nfs4_set_lease_expired(struct nfs_client *clp, int status)
{
	switch (status) {
	case -NFS4ERR_CLID_INUSE:
	case -NFS4ERR_STALE_CLIENTID:
		clear_bit(NFS4CLNT_LEASE_CONFIRM, &clp->cl_state);
		break;
	case -NFS4ERR_DELAY:
	case -ETIMEDOUT:
	case -EAGAIN:
		ssleep(1);
		break;

	case -EKEYEXPIRED:
		nfs4_warn_keyexpired(clp->cl_hostname);
	case -NFS4ERR_NOT_SAME: /* FixMe: implement recovery
				 * in nfs4_exchange_id */
	default:
		return;
	}
	set_bit(NFS4CLNT_LEASE_EXPIRED, &clp->cl_state);
}

static void nfs4_state_manager(struct nfs_client *clp)
{
	int status = 0;

	/* Ensure exclusive access to NFSv4 state */
	do {
		if (test_and_clear_bit(NFS4CLNT_LEASE_EXPIRED, &clp->cl_state)) {
			/* We're going to have to re-establish a clientid */
			status = nfs4_reclaim_lease(clp);
			if (status) {
				nfs4_set_lease_expired(clp, status);
				if (test_bit(NFS4CLNT_LEASE_EXPIRED,
							&clp->cl_state))
					continue;
				if (clp->cl_cons_state ==
							NFS_CS_SESSION_INITING)
					nfs_mark_client_ready(clp, status);
				goto out_error;
			}
			clear_bit(NFS4CLNT_CHECK_LEASE, &clp->cl_state);
			set_bit(NFS4CLNT_RECLAIM_REBOOT, &clp->cl_state);
			pnfs_destroy_all_layouts(clp);
		}

		if (test_and_clear_bit(NFS4CLNT_CHECK_LEASE, &clp->cl_state)) {
			status = nfs4_check_lease(clp);
			if (test_bit(NFS4CLNT_LEASE_EXPIRED, &clp->cl_state))
				continue;
			if (status < 0 && status != -NFS4ERR_CB_PATH_DOWN)
				goto out_error;
		}

		/* Initialize or reset the session */
		if (test_and_clear_bit(NFS4CLNT_SESSION_RESET, &clp->cl_state)
		   && nfs4_has_session(clp)) {
			status = nfs4_reset_session(clp);
			if (test_bit(NFS4CLNT_LEASE_EXPIRED, &clp->cl_state))
				continue;
			if (status < 0)
				goto out_error;
		}

		/* First recover reboot state... */
		if (test_bit(NFS4CLNT_RECLAIM_REBOOT, &clp->cl_state)) {
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
		/* Recall session slots */
		if (test_and_clear_bit(NFS4CLNT_RECALL_SLOT, &clp->cl_state)
		   && nfs4_has_session(clp)) {
			status = nfs4_recall_slot(clp);
			if (status < 0)
				goto out_error;
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
	printk(KERN_WARNING "Error: state manager failed on NFSv4 server %s"
			" with error %d\n", clp->cl_hostname, -status);
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
