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
#include <linux/smp_lock.h>
#include <linux/nfs_fs.h>
#include <linux/nfs_idmap.h>
#include <linux/kthread.h>
#include <linux/module.h>
#include <linux/random.h>
#include <linux/workqueue.h>
#include <linux/bitops.h>

#include "nfs4_fs.h"
#include "callback.h"
#include "delegation.h"
#include "internal.h"

#define OPENOWNER_POOL_SIZE	8

const nfs4_stateid zero_stateid;

static LIST_HEAD(nfs4_clientid_list);

static int nfs4_init_client(struct nfs_client *clp, struct rpc_cred *cred)
{
	int status = nfs4_proc_setclientid(clp, NFS4_CALLBACK,
			nfs_callback_tcpport, cred);
	if (status == 0)
		status = nfs4_proc_setclientid_confirm(clp, cred);
	if (status == 0)
		nfs4_schedule_state_renewal(clp);
	return status;
}

struct rpc_cred *nfs4_get_renew_cred(struct nfs_client *clp)
{
	struct nfs4_state_owner *sp;
	struct rb_node *pos;
	struct rpc_cred *cred = NULL;

	for (pos = rb_first(&clp->cl_state_owners); pos != NULL; pos = rb_next(pos)) {
		sp = rb_entry(pos, struct nfs4_state_owner, so_client_node);
		if (list_empty(&sp->so_states))
			continue;
		cred = get_rpccred(sp->so_cred);
		break;
	}
	return cred;
}

static struct rpc_cred *nfs4_get_setclientid_cred(struct nfs_client *clp)
{
	struct nfs4_state_owner *sp;
	struct rb_node *pos;

	pos = rb_first(&clp->cl_state_owners);
	if (pos != NULL) {
		sp = rb_entry(pos, struct nfs4_state_owner, so_client_node);
		return get_rpccred(sp->so_cred);
	}
	return NULL;
}

static void nfs_alloc_unique_id(struct rb_root *root, struct nfs_unique_id *new,
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
nfs4_find_state_owner(struct nfs_server *server, struct rpc_cred *cred)
{
	struct nfs_client *clp = server->nfs_client;
	struct rb_node **p = &clp->cl_state_owners.rb_node,
		       *parent = NULL;
	struct nfs4_state_owner *sp, *res = NULL;

	while (*p != NULL) {
		parent = *p;
		sp = rb_entry(parent, struct nfs4_state_owner, so_client_node);

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
nfs4_insert_state_owner(struct nfs_client *clp, struct nfs4_state_owner *new)
{
	struct rb_node **p = &clp->cl_state_owners.rb_node,
		       *parent = NULL;
	struct nfs4_state_owner *sp;

	while (*p != NULL) {
		parent = *p;
		sp = rb_entry(parent, struct nfs4_state_owner, so_client_node);

		if (new->so_server < sp->so_server) {
			p = &parent->rb_left;
			continue;
		}
		if (new->so_server > sp->so_server) {
			p = &parent->rb_right;
			continue;
		}
		if (new->so_cred < sp->so_cred)
			p = &parent->rb_left;
		else if (new->so_cred > sp->so_cred)
			p = &parent->rb_right;
		else {
			atomic_inc(&sp->so_count);
			return sp;
		}
	}
	nfs_alloc_unique_id(&clp->cl_openowner_id, &new->so_owner_id, 1, 64);
	rb_link_node(&new->so_client_node, parent, p);
	rb_insert_color(&new->so_client_node, &clp->cl_state_owners);
	return new;
}

static void
nfs4_remove_state_owner(struct nfs_client *clp, struct nfs4_state_owner *sp)
{
	if (!RB_EMPTY_NODE(&sp->so_client_node))
		rb_erase(&sp->so_client_node, &clp->cl_state_owners);
	nfs_free_unique_id(&clp->cl_openowner_id, &sp->so_owner_id);
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

	sp = kzalloc(sizeof(*sp),GFP_KERNEL);
	if (!sp)
		return NULL;
	spin_lock_init(&sp->so_lock);
	INIT_LIST_HEAD(&sp->so_states);
	INIT_LIST_HEAD(&sp->so_delegations);
	rpc_init_wait_queue(&sp->so_sequence.wait, "Seqid_waitqueue");
	sp->so_seqid.sequence = &sp->so_sequence;
	spin_lock_init(&sp->so_sequence.lock);
	INIT_LIST_HEAD(&sp->so_sequence.list);
	atomic_set(&sp->so_count, 1);
	return sp;
}

void
nfs4_drop_state_owner(struct nfs4_state_owner *sp)
{
	if (!RB_EMPTY_NODE(&sp->so_client_node)) {
		struct nfs_client *clp = sp->so_client;

		spin_lock(&clp->cl_lock);
		rb_erase(&sp->so_client_node, &clp->cl_state_owners);
		RB_CLEAR_NODE(&sp->so_client_node);
		spin_unlock(&clp->cl_lock);
	}
}

/*
 * Note: must be called with clp->cl_sem held in order to prevent races
 *       with reboot recovery!
 */
struct nfs4_state_owner *nfs4_get_state_owner(struct nfs_server *server, struct rpc_cred *cred)
{
	struct nfs_client *clp = server->nfs_client;
	struct nfs4_state_owner *sp, *new;

	spin_lock(&clp->cl_lock);
	sp = nfs4_find_state_owner(server, cred);
	spin_unlock(&clp->cl_lock);
	if (sp != NULL)
		return sp;
	new = nfs4_alloc_state_owner();
	if (new == NULL)
		return NULL;
	new->so_client = clp;
	new->so_server = server;
	new->so_cred = cred;
	spin_lock(&clp->cl_lock);
	sp = nfs4_insert_state_owner(clp, new);
	spin_unlock(&clp->cl_lock);
	if (sp == new)
		get_rpccred(cred);
	else
		kfree(new);
	return sp;
}

/*
 * Must be called with clp->cl_sem held in order to avoid races
 * with state recovery...
 */
void nfs4_put_state_owner(struct nfs4_state_owner *sp)
{
	struct nfs_client *clp = sp->so_client;
	struct rpc_cred *cred = sp->so_cred;

	if (!atomic_dec_and_lock(&sp->so_count, &clp->cl_lock))
		return;
	nfs4_remove_state_owner(clp, sp);
	spin_unlock(&clp->cl_lock);
	put_rpccred(cred);
	kfree(sp);
}

static struct nfs4_state *
nfs4_alloc_open_state(void)
{
	struct nfs4_state *state;

	state = kzalloc(sizeof(*state), GFP_KERNEL);
	if (!state)
		return NULL;
	atomic_set(&state->count, 1);
	INIT_LIST_HEAD(&state->lock_states);
	spin_lock_init(&state->state_lock);
	seqlock_init(&state->seqlock);
	return state;
}

void
nfs4_state_set_mode_locked(struct nfs4_state *state, mode_t mode)
{
	if (state->state == mode)
		return;
	/* NB! List reordering - see the reclaim code for why.  */
	if ((mode & FMODE_WRITE) != (state->state & FMODE_WRITE)) {
		if (mode & FMODE_WRITE)
			list_move(&state->open_states, &state->owner->so_states);
		else
			list_move_tail(&state->open_states, &state->owner->so_states);
	}
	state->state = mode;
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
		state->inode = igrab(inode);
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

/*
 * Beware! Caller must be holding exactly one
 * reference to clp->cl_sem!
 */
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
static void __nfs4_close(struct path *path, struct nfs4_state *state, mode_t mode, int wait)
{
	struct nfs4_state_owner *owner = state->owner;
	int call_close = 0;
	int newstate;

	atomic_inc(&owner->so_count);
	/* Protect against nfs4_find_state() */
	spin_lock(&owner->so_lock);
	switch (mode & (FMODE_READ | FMODE_WRITE)) {
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
		nfs4_do_close(path, state, wait);
}

void nfs4_close_state(struct path *path, struct nfs4_state *state, mode_t mode)
{
	__nfs4_close(path, state, mode, 0);
}

void nfs4_close_sync(struct path *path, struct nfs4_state *state, mode_t mode)
{
	__nfs4_close(path, state, mode, 1);
}

/*
 * Search the state->lock_states for an existing lock_owner
 * that is compatible with current->files
 */
static struct nfs4_lock_state *
__nfs4_find_lock_state(struct nfs4_state *state, fl_owner_t fl_owner)
{
	struct nfs4_lock_state *pos;
	list_for_each_entry(pos, &state->lock_states, ls_locks) {
		if (pos->ls_owner != fl_owner)
			continue;
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
static struct nfs4_lock_state *nfs4_alloc_lock_state(struct nfs4_state *state, fl_owner_t fl_owner)
{
	struct nfs4_lock_state *lsp;
	struct nfs_client *clp = state->owner->so_client;

	lsp = kzalloc(sizeof(*lsp), GFP_KERNEL);
	if (lsp == NULL)
		return NULL;
	rpc_init_wait_queue(&lsp->ls_sequence.wait, "lock_seqid_waitqueue");
	spin_lock_init(&lsp->ls_sequence.lock);
	INIT_LIST_HEAD(&lsp->ls_sequence.list);
	lsp->ls_seqid.sequence = &lsp->ls_sequence;
	atomic_set(&lsp->ls_count, 1);
	lsp->ls_owner = fl_owner;
	spin_lock(&clp->cl_lock);
	nfs_alloc_unique_id(&clp->cl_lockowner_id, &lsp->ls_id, 1, 64);
	spin_unlock(&clp->cl_lock);
	INIT_LIST_HEAD(&lsp->ls_locks);
	return lsp;
}

static void nfs4_free_lock_state(struct nfs4_lock_state *lsp)
{
	struct nfs_client *clp = lsp->ls_state->owner->so_client;

	spin_lock(&clp->cl_lock);
	nfs_free_unique_id(&clp->cl_lockowner_id, &lsp->ls_id);
	spin_unlock(&clp->cl_lock);
	kfree(lsp);
}

/*
 * Return a compatible lock_state. If no initialized lock_state structure
 * exists, return an uninitialized one.
 *
 * The caller must be holding clp->cl_sem
 */
static struct nfs4_lock_state *nfs4_get_lock_state(struct nfs4_state *state, fl_owner_t owner)
{
	struct nfs4_lock_state *lsp, *new = NULL;
	
	for(;;) {
		spin_lock(&state->state_lock);
		lsp = __nfs4_find_lock_state(state, owner);
		if (lsp != NULL)
			break;
		if (new != NULL) {
			new->ls_state = state;
			list_add(&new->ls_locks, &state->lock_states);
			set_bit(LK_STATE_IN_USE, &state->flags);
			lsp = new;
			new = NULL;
			break;
		}
		spin_unlock(&state->state_lock);
		new = nfs4_alloc_lock_state(state, owner);
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

static struct file_lock_operations nfs4_fl_lock_ops = {
	.fl_copy_lock = nfs4_fl_copy_lock,
	.fl_release_private = nfs4_fl_release_lock,
};

int nfs4_set_lock_state(struct nfs4_state *state, struct file_lock *fl)
{
	struct nfs4_lock_state *lsp;

	if (fl->fl_ops != NULL)
		return 0;
	lsp = nfs4_get_lock_state(state, fl->fl_owner);
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
void nfs4_copy_stateid(nfs4_stateid *dst, struct nfs4_state *state, fl_owner_t fl_owner)
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
	lsp = __nfs4_find_lock_state(state, fl_owner);
	if (lsp != NULL && (lsp->ls_flags & NFS_LOCK_INITIALIZED) != 0)
		memcpy(dst, &lsp->ls_stateid, sizeof(*dst));
	spin_unlock(&state->state_lock);
	nfs4_put_lock_state(lsp);
}

struct nfs_seqid *nfs_alloc_seqid(struct nfs_seqid_counter *counter)
{
	struct rpc_sequence *sequence = counter->sequence;
	struct nfs_seqid *new;

	new = kmalloc(sizeof(*new), GFP_KERNEL);
	if (new != NULL) {
		new->sequence = counter;
		spin_lock(&sequence->lock);
		list_add_tail(&new->list, &sequence->list);
		spin_unlock(&sequence->lock);
	}
	return new;
}

void nfs_free_seqid(struct nfs_seqid *seqid)
{
	struct rpc_sequence *sequence = seqid->sequence->sequence;

	spin_lock(&sequence->lock);
	list_del(&seqid->list);
	spin_unlock(&sequence->lock);
	rpc_wake_up(&sequence->wait);
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
			printk(KERN_WARNING "NFS: v4 server returned a bad"
					"sequence-id error on an"
					"unconfirmed sequence %p!\n",
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
	if (status == -NFS4ERR_BAD_SEQID) {
		struct nfs4_state_owner *sp = container_of(seqid->sequence,
				struct nfs4_state_owner, so_seqid);
		nfs4_drop_state_owner(sp);
	}
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

	if (sequence->list.next == &seqid->list)
		goto out;
	spin_lock(&sequence->lock);
	if (sequence->list.next != &seqid->list) {
		rpc_sleep_on(&sequence->wait, task, NULL, NULL);
		status = -EAGAIN;
	}
	spin_unlock(&sequence->lock);
out:
	return status;
}

static int reclaimer(void *);

static inline void nfs4_clear_recover_bit(struct nfs_client *clp)
{
	smp_mb__before_clear_bit();
	clear_bit(NFS4CLNT_STATE_RECOVER, &clp->cl_state);
	smp_mb__after_clear_bit();
	wake_up_bit(&clp->cl_state, NFS4CLNT_STATE_RECOVER);
	rpc_wake_up(&clp->cl_rpcwaitq);
}

/*
 * State recovery routine
 */
static void nfs4_recover_state(struct nfs_client *clp)
{
	struct task_struct *task;

	__module_get(THIS_MODULE);
	atomic_inc(&clp->cl_count);
	task = kthread_run(reclaimer, clp, "%u.%u.%u.%u-reclaim",
			NIPQUAD(clp->cl_addr.sin_addr));
	if (!IS_ERR(task))
		return;
	nfs4_clear_recover_bit(clp);
	nfs_put_client(clp);
	module_put(THIS_MODULE);
}

/*
 * Schedule a state recovery attempt
 */
void nfs4_schedule_state_recovery(struct nfs_client *clp)
{
	if (!clp)
		return;
	if (test_and_set_bit(NFS4CLNT_STATE_RECOVER, &clp->cl_state) == 0)
		nfs4_recover_state(clp);
}

static int nfs4_reclaim_locks(struct nfs4_state_recovery_ops *ops, struct nfs4_state *state)
{
	struct inode *inode = state->inode;
	struct file_lock *fl;
	int status = 0;

	for (fl = inode->i_flock; fl != 0; fl = fl->fl_next) {
		if (!(fl->fl_flags & (FL_POSIX|FL_FLOCK)))
			continue;
		if (nfs_file_open_context(fl->fl_file)->state != state)
			continue;
		status = ops->recover_lock(state, fl);
		if (status >= 0)
			continue;
		switch (status) {
			default:
				printk(KERN_ERR "%s: unhandled error %d. Zeroing state\n",
						__FUNCTION__, status);
			case -NFS4ERR_EXPIRED:
			case -NFS4ERR_NO_GRACE:
			case -NFS4ERR_RECLAIM_BAD:
			case -NFS4ERR_RECLAIM_CONFLICT:
				/* kill_proc(fl->fl_pid, SIGLOST, 1); */
				break;
			case -NFS4ERR_STALE_CLIENTID:
				goto out_err;
		}
	}
	return 0;
out_err:
	return status;
}

static int nfs4_reclaim_open_state(struct nfs4_state_recovery_ops *ops, struct nfs4_state_owner *sp)
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
	list_for_each_entry(state, &sp->so_states, open_states) {
		if (state->state == 0)
			continue;
		status = ops->recover_open(sp, state);
		if (status >= 0) {
			status = nfs4_reclaim_locks(ops, state);
			if (status < 0)
				goto out_err;
			list_for_each_entry(lock, &state->lock_states, ls_locks) {
				if (!(lock->ls_flags & NFS_LOCK_INITIALIZED))
					printk("%s: Lock reclaim failed!\n",
							__FUNCTION__);
			}
			continue;
		}
		switch (status) {
			default:
				printk(KERN_ERR "%s: unhandled error %d. Zeroing state\n",
						__FUNCTION__, status);
			case -ENOENT:
			case -NFS4ERR_RECLAIM_BAD:
			case -NFS4ERR_RECLAIM_CONFLICT:
				/*
				 * Open state on this file cannot be recovered
				 * All we can do is revert to using the zero stateid.
				 */
				memset(state->stateid.data, 0,
					sizeof(state->stateid.data));
				/* Mark the file as being 'closed' */
				state->state = 0;
				break;
			case -NFS4ERR_EXPIRED:
			case -NFS4ERR_NO_GRACE:
			case -NFS4ERR_STALE_CLIENTID:
				goto out_err;
		}
	}
	return 0;
out_err:
	return status;
}

static void nfs4_state_mark_reclaim(struct nfs_client *clp)
{
	struct nfs4_state_owner *sp;
	struct rb_node *pos;
	struct nfs4_state *state;
	struct nfs4_lock_state *lock;

	/* Reset all sequence ids to zero */
	for (pos = rb_first(&clp->cl_state_owners); pos != NULL; pos = rb_next(pos)) {
		sp = rb_entry(pos, struct nfs4_state_owner, so_client_node);
		sp->so_seqid.counter = 0;
		sp->so_seqid.flags = 0;
		spin_lock(&sp->so_lock);
		list_for_each_entry(state, &sp->so_states, open_states) {
			clear_bit(NFS_DELEGATED_STATE, &state->flags);
			clear_bit(NFS_O_RDONLY_STATE, &state->flags);
			clear_bit(NFS_O_WRONLY_STATE, &state->flags);
			clear_bit(NFS_O_RDWR_STATE, &state->flags);
			list_for_each_entry(lock, &state->lock_states, ls_locks) {
				lock->ls_seqid.counter = 0;
				lock->ls_seqid.flags = 0;
				lock->ls_flags &= ~NFS_LOCK_INITIALIZED;
			}
		}
		spin_unlock(&sp->so_lock);
	}
}

static int reclaimer(void *ptr)
{
	struct nfs_client *clp = ptr;
	struct nfs4_state_owner *sp;
	struct rb_node *pos;
	struct nfs4_state_recovery_ops *ops;
	struct rpc_cred *cred;
	int status = 0;

	allow_signal(SIGKILL);

	/* Ensure exclusive access to NFSv4 state */
	lock_kernel();
	down_write(&clp->cl_sem);
	/* Are there any NFS mounts out there? */
	if (list_empty(&clp->cl_superblocks))
		goto out;
restart_loop:
	ops = &nfs4_network_partition_recovery_ops;
	/* Are there any open files on this volume? */
	cred = nfs4_get_renew_cred(clp);
	if (cred != NULL) {
		/* Yes there are: try to renew the old lease */
		status = nfs4_proc_renew(clp, cred);
		switch (status) {
			case 0:
			case -NFS4ERR_CB_PATH_DOWN:
				put_rpccred(cred);
				goto out;
			case -NFS4ERR_STALE_CLIENTID:
			case -NFS4ERR_LEASE_MOVED:
				ops = &nfs4_reboot_recovery_ops;
		}
	} else {
		/* "reboot" to ensure we clear all state on the server */
		clp->cl_boot_time = CURRENT_TIME;
		cred = nfs4_get_setclientid_cred(clp);
	}
	/* We're going to have to re-establish a clientid */
	nfs4_state_mark_reclaim(clp);
	status = -ENOENT;
	if (cred != NULL) {
		status = nfs4_init_client(clp, cred);
		put_rpccred(cred);
	}
	if (status)
		goto out_error;
	/* Mark all delegations for reclaim */
	nfs_delegation_mark_reclaim(clp);
	/* Note: list is protected by exclusive lock on cl->cl_sem */
	for (pos = rb_first(&clp->cl_state_owners); pos != NULL; pos = rb_next(pos)) {
		sp = rb_entry(pos, struct nfs4_state_owner, so_client_node);
		status = nfs4_reclaim_open_state(ops, sp);
		if (status < 0) {
			if (status == -NFS4ERR_NO_GRACE) {
				ops = &nfs4_network_partition_recovery_ops;
				status = nfs4_reclaim_open_state(ops, sp);
			}
			if (status == -NFS4ERR_STALE_CLIENTID)
				goto restart_loop;
			if (status == -NFS4ERR_EXPIRED)
				goto restart_loop;
		}
	}
	nfs_delegation_reap_unclaimed(clp);
out:
	up_write(&clp->cl_sem);
	unlock_kernel();
	if (status == -NFS4ERR_CB_PATH_DOWN)
		nfs_handle_cb_pathdown(clp);
	nfs4_clear_recover_bit(clp);
	nfs_put_client(clp);
	module_put_and_exit(0);
	return 0;
out_error:
	printk(KERN_WARNING "Error: state recovery failed on NFSv4 server %u.%u.%u.%u with error %d\n",
				NIPQUAD(clp->cl_addr.sin_addr), -status);
	set_bit(NFS4CLNT_LEASE_EXPIRED, &clp->cl_state);
	goto out;
}

/*
 * Local variables:
 *  c-basic-offset: 8
 * End:
 */
