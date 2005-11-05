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

#include <linux/config.h>
#include <linux/slab.h>
#include <linux/smp_lock.h>
#include <linux/nfs_fs.h>
#include <linux/nfs_idmap.h>
#include <linux/workqueue.h>
#include <linux/bitops.h>

#include "nfs4_fs.h"
#include "callback.h"
#include "delegation.h"

#define OPENOWNER_POOL_SIZE	8

const nfs4_stateid zero_stateid;

static DEFINE_SPINLOCK(state_spinlock);
static LIST_HEAD(nfs4_clientid_list);

static void nfs4_recover_state(void *);

void
init_nfsv4_state(struct nfs_server *server)
{
	server->nfs4_state = NULL;
	INIT_LIST_HEAD(&server->nfs4_siblings);
}

void
destroy_nfsv4_state(struct nfs_server *server)
{
	if (server->mnt_path) {
		kfree(server->mnt_path);
		server->mnt_path = NULL;
	}
	if (server->nfs4_state) {
		nfs4_put_client(server->nfs4_state);
		server->nfs4_state = NULL;
	}
}

/*
 * nfs4_get_client(): returns an empty client structure
 * nfs4_put_client(): drops reference to client structure
 *
 * Since these are allocated/deallocated very rarely, we don't
 * bother putting them in a slab cache...
 */
static struct nfs4_client *
nfs4_alloc_client(struct in_addr *addr)
{
	struct nfs4_client *clp;

	if (nfs_callback_up() < 0)
		return NULL;
	if ((clp = kmalloc(sizeof(*clp), GFP_KERNEL)) == NULL) {
		nfs_callback_down();
		return NULL;
	}
	memset(clp, 0, sizeof(*clp));
	memcpy(&clp->cl_addr, addr, sizeof(clp->cl_addr));
	init_rwsem(&clp->cl_sem);
	INIT_LIST_HEAD(&clp->cl_delegations);
	INIT_LIST_HEAD(&clp->cl_state_owners);
	INIT_LIST_HEAD(&clp->cl_unused);
	spin_lock_init(&clp->cl_lock);
	atomic_set(&clp->cl_count, 1);
	INIT_WORK(&clp->cl_recoverd, nfs4_recover_state, clp);
	INIT_WORK(&clp->cl_renewd, nfs4_renew_state, clp);
	INIT_LIST_HEAD(&clp->cl_superblocks);
	init_waitqueue_head(&clp->cl_waitq);
	rpc_init_wait_queue(&clp->cl_rpcwaitq, "NFS4 client");
	clp->cl_rpcclient = ERR_PTR(-EINVAL);
	clp->cl_boot_time = CURRENT_TIME;
	clp->cl_state = 1 << NFS4CLNT_OK;
	return clp;
}

static void
nfs4_free_client(struct nfs4_client *clp)
{
	struct nfs4_state_owner *sp;

	while (!list_empty(&clp->cl_unused)) {
		sp = list_entry(clp->cl_unused.next,
				struct nfs4_state_owner,
				so_list);
		list_del(&sp->so_list);
		kfree(sp);
	}
	BUG_ON(!list_empty(&clp->cl_state_owners));
	if (clp->cl_cred)
		put_rpccred(clp->cl_cred);
	nfs_idmap_delete(clp);
	if (!IS_ERR(clp->cl_rpcclient))
		rpc_shutdown_client(clp->cl_rpcclient);
	kfree(clp);
	nfs_callback_down();
}

static struct nfs4_client *__nfs4_find_client(struct in_addr *addr)
{
	struct nfs4_client *clp;
	list_for_each_entry(clp, &nfs4_clientid_list, cl_servers) {
		if (memcmp(&clp->cl_addr, addr, sizeof(clp->cl_addr)) == 0) {
			atomic_inc(&clp->cl_count);
			return clp;
		}
	}
	return NULL;
}

struct nfs4_client *nfs4_find_client(struct in_addr *addr)
{
	struct nfs4_client *clp;
	spin_lock(&state_spinlock);
	clp = __nfs4_find_client(addr);
	spin_unlock(&state_spinlock);
	return clp;
}

struct nfs4_client *
nfs4_get_client(struct in_addr *addr)
{
	struct nfs4_client *clp, *new = NULL;

	spin_lock(&state_spinlock);
	for (;;) {
		clp = __nfs4_find_client(addr);
		if (clp != NULL)
			break;
		clp = new;
		if (clp != NULL) {
			list_add(&clp->cl_servers, &nfs4_clientid_list);
			new = NULL;
			break;
		}
		spin_unlock(&state_spinlock);
		new = nfs4_alloc_client(addr);
		spin_lock(&state_spinlock);
		if (new == NULL)
			break;
	}
	spin_unlock(&state_spinlock);
	if (new)
		nfs4_free_client(new);
	return clp;
}

void
nfs4_put_client(struct nfs4_client *clp)
{
	if (!atomic_dec_and_lock(&clp->cl_count, &state_spinlock))
		return;
	list_del(&clp->cl_servers);
	spin_unlock(&state_spinlock);
	BUG_ON(!list_empty(&clp->cl_superblocks));
	wake_up_all(&clp->cl_waitq);
	rpc_wake_up(&clp->cl_rpcwaitq);
	nfs4_kill_renewd(clp);
	nfs4_free_client(clp);
}

static int __nfs4_init_client(struct nfs4_client *clp)
{
	int status = nfs4_proc_setclientid(clp, NFS4_CALLBACK, nfs_callback_tcpport);
	if (status == 0)
		status = nfs4_proc_setclientid_confirm(clp);
	if (status == 0)
		nfs4_schedule_state_renewal(clp);
	return status;
}

int nfs4_init_client(struct nfs4_client *clp)
{
	return nfs4_map_errors(__nfs4_init_client(clp));
}

u32
nfs4_alloc_lockowner_id(struct nfs4_client *clp)
{
	return clp->cl_lockowner_id ++;
}

static struct nfs4_state_owner *
nfs4_client_grab_unused(struct nfs4_client *clp, struct rpc_cred *cred)
{
	struct nfs4_state_owner *sp = NULL;

	if (!list_empty(&clp->cl_unused)) {
		sp = list_entry(clp->cl_unused.next, struct nfs4_state_owner, so_list);
		atomic_inc(&sp->so_count);
		sp->so_cred = cred;
		list_move(&sp->so_list, &clp->cl_state_owners);
		clp->cl_nunused--;
	}
	return sp;
}

static struct nfs4_state_owner *
nfs4_find_state_owner(struct nfs4_client *clp, struct rpc_cred *cred)
{
	struct nfs4_state_owner *sp, *res = NULL;

	list_for_each_entry(sp, &clp->cl_state_owners, so_list) {
		if (sp->so_cred != cred)
			continue;
		atomic_inc(&sp->so_count);
		/* Move to the head of the list */
		list_move(&sp->so_list, &clp->cl_state_owners);
		res = sp;
		break;
	}
	return res;
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
	struct nfs4_client *clp = sp->so_client;
	spin_lock(&clp->cl_lock);
	list_del_init(&sp->so_list);
	spin_unlock(&clp->cl_lock);
}

/*
 * Note: must be called with clp->cl_sem held in order to prevent races
 *       with reboot recovery!
 */
struct nfs4_state_owner *nfs4_get_state_owner(struct nfs_server *server, struct rpc_cred *cred)
{
	struct nfs4_client *clp = server->nfs4_state;
	struct nfs4_state_owner *sp, *new;

	get_rpccred(cred);
	new = nfs4_alloc_state_owner();
	spin_lock(&clp->cl_lock);
	sp = nfs4_find_state_owner(clp, cred);
	if (sp == NULL)
		sp = nfs4_client_grab_unused(clp, cred);
	if (sp == NULL && new != NULL) {
		list_add(&new->so_list, &clp->cl_state_owners);
		new->so_client = clp;
		new->so_id = nfs4_alloc_lockowner_id(clp);
		new->so_cred = cred;
		sp = new;
		new = NULL;
	}
	spin_unlock(&clp->cl_lock);
	if (new)
		kfree(new);
	if (sp != NULL)
		return sp;
	put_rpccred(cred);
	return NULL;
}

/*
 * Must be called with clp->cl_sem held in order to avoid races
 * with state recovery...
 */
void nfs4_put_state_owner(struct nfs4_state_owner *sp)
{
	struct nfs4_client *clp = sp->so_client;
	struct rpc_cred *cred = sp->so_cred;

	if (!atomic_dec_and_lock(&sp->so_count, &clp->cl_lock))
		return;
	if (clp->cl_nunused >= OPENOWNER_POOL_SIZE)
		goto out_free;
	if (list_empty(&sp->so_list))
		goto out_free;
	list_move(&sp->so_list, &clp->cl_unused);
	clp->cl_nunused++;
	spin_unlock(&clp->cl_lock);
	put_rpccred(cred);
	cred = NULL;
	return;
out_free:
	list_del(&sp->so_list);
	spin_unlock(&clp->cl_lock);
	put_rpccred(cred);
	kfree(sp);
}

static struct nfs4_state *
nfs4_alloc_open_state(void)
{
	struct nfs4_state *state;

	state = kmalloc(sizeof(*state), GFP_KERNEL);
	if (!state)
		return NULL;
	state->state = 0;
	state->nreaders = 0;
	state->nwriters = 0;
	state->flags = 0;
	memset(state->stateid.data, 0, sizeof(state->stateid.data));
	atomic_set(&state->count, 1);
	INIT_LIST_HEAD(&state->lock_states);
	spin_lock_init(&state->state_lock);
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
	if (mode == 0)
		list_del_init(&state->inode_states);
	state->state = mode;
}

static struct nfs4_state *
__nfs4_find_state_byowner(struct inode *inode, struct nfs4_state_owner *owner)
{
	struct nfs_inode *nfsi = NFS_I(inode);
	struct nfs4_state *state;

	list_for_each_entry(state, &nfsi->open_states, inode_states) {
		/* Is this in the process of being freed? */
		if (state->state == 0)
			continue;
		if (state->owner == owner) {
			atomic_inc(&state->count);
			return state;
		}
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
	if (!list_empty(&state->inode_states))
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
void nfs4_close_state(struct nfs4_state *state, mode_t mode)
{
	struct inode *inode = state->inode;
	struct nfs4_state_owner *owner = state->owner;
	int oldstate, newstate = 0;

	atomic_inc(&owner->so_count);
	/* Protect against nfs4_find_state() */
	spin_lock(&owner->so_lock);
	spin_lock(&inode->i_lock);
	if (mode & FMODE_READ)
		state->nreaders--;
	if (mode & FMODE_WRITE)
		state->nwriters--;
	oldstate = newstate = state->state;
	if (state->nreaders == 0)
		newstate &= ~FMODE_READ;
	if (state->nwriters == 0)
		newstate &= ~FMODE_WRITE;
	if (test_bit(NFS_DELEGATED_STATE, &state->flags)) {
		nfs4_state_set_mode_locked(state, newstate);
		oldstate = newstate;
	}
	spin_unlock(&inode->i_lock);
	spin_unlock(&owner->so_lock);

	if (oldstate != newstate && nfs4_do_close(inode, state) == 0)
		return;
	nfs4_put_open_state(state);
	nfs4_put_state_owner(owner);
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
	struct nfs4_client *clp = state->owner->so_client;

	lsp = kzalloc(sizeof(*lsp), GFP_KERNEL);
	if (lsp == NULL)
		return NULL;
	lsp->ls_seqid.sequence = &state->owner->so_sequence;
	atomic_set(&lsp->ls_count, 1);
	lsp->ls_owner = fl_owner;
	spin_lock(&clp->cl_lock);
	lsp->ls_id = nfs4_alloc_lockowner_id(clp);
	spin_unlock(&clp->cl_lock);
	INIT_LIST_HEAD(&lsp->ls_locks);
	return lsp;
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
	kfree(new);
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
	kfree(lsp);
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

	memcpy(dst, &state->stateid, sizeof(*dst));
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
	struct nfs_seqid *new;

	new = kmalloc(sizeof(*new), GFP_KERNEL);
	if (new != NULL) {
		new->sequence = counter;
		INIT_LIST_HEAD(&new->list);
	}
	return new;
}

void nfs_free_seqid(struct nfs_seqid *seqid)
{
	struct rpc_sequence *sequence = seqid->sequence->sequence;

	if (!list_empty(&seqid->list)) {
		spin_lock(&sequence->lock);
		list_del(&seqid->list);
		spin_unlock(&sequence->lock);
	}
	rpc_wake_up_next(&sequence->wait);
	kfree(seqid);
}

/*
 * Increment the seqid if the OPEN/OPEN_DOWNGRADE/CLOSE succeeded, or
 * failed with a seqid incrementing error -
 * see comments nfs_fs.h:seqid_mutating_error()
 */
static inline void nfs_increment_seqid(int status, struct nfs_seqid *seqid)
{
	switch (status) {
		case 0:
			break;
		case -NFS4ERR_BAD_SEQID:
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
	return nfs_increment_seqid(status, seqid);
}

/*
 * Increment the seqid if the LOCK/LOCKU succeeded, or
 * failed with a seqid incrementing error -
 * see comments nfs_fs.h:seqid_mutating_error()
 */
void nfs_increment_lock_seqid(int status, struct nfs_seqid *seqid)
{
	return nfs_increment_seqid(status, seqid);
}

int nfs_wait_on_sequence(struct nfs_seqid *seqid, struct rpc_task *task)
{
	struct rpc_sequence *sequence = seqid->sequence->sequence;
	int status = 0;

	if (sequence->list.next == &seqid->list)
		goto out;
	spin_lock(&sequence->lock);
	if (!list_empty(&sequence->list)) {
		rpc_sleep_on(&sequence->wait, task, NULL, NULL);
		status = -EAGAIN;
	} else
		list_add(&seqid->list, &sequence->list);
	spin_unlock(&sequence->lock);
out:
	return status;
}

static int reclaimer(void *);
struct reclaimer_args {
	struct nfs4_client *clp;
	struct completion complete;
};

/*
 * State recovery routine
 */
void
nfs4_recover_state(void *data)
{
	struct nfs4_client *clp = (struct nfs4_client *)data;
	struct reclaimer_args args = {
		.clp = clp,
	};
	might_sleep();

	init_completion(&args.complete);

	if (kernel_thread(reclaimer, &args, CLONE_KERNEL) < 0)
		goto out_failed_clear;
	wait_for_completion(&args.complete);
	return;
out_failed_clear:
	set_bit(NFS4CLNT_OK, &clp->cl_state);
	wake_up_all(&clp->cl_waitq);
	rpc_wake_up(&clp->cl_rpcwaitq);
}

/*
 * Schedule a state recovery attempt
 */
void
nfs4_schedule_state_recovery(struct nfs4_client *clp)
{
	if (!clp)
		return;
	if (test_and_clear_bit(NFS4CLNT_OK, &clp->cl_state))
		schedule_work(&clp->cl_recoverd);
}

static int nfs4_reclaim_locks(struct nfs4_state_recovery_ops *ops, struct nfs4_state *state)
{
	struct inode *inode = state->inode;
	struct file_lock *fl;
	int status = 0;

	for (fl = inode->i_flock; fl != 0; fl = fl->fl_next) {
		if (!(fl->fl_flags & (FL_POSIX|FL_FLOCK)))
			continue;
		if (((struct nfs_open_context *)fl->fl_file->private_data)->state != state)
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

static void nfs4_state_mark_reclaim(struct nfs4_client *clp)
{
	struct nfs4_state_owner *sp;
	struct nfs4_state *state;
	struct nfs4_lock_state *lock;

	/* Reset all sequence ids to zero */
	list_for_each_entry(sp, &clp->cl_state_owners, so_list) {
		sp->so_seqid.counter = 0;
		sp->so_seqid.flags = 0;
		spin_lock(&sp->so_lock);
		list_for_each_entry(state, &sp->so_states, open_states) {
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
	struct reclaimer_args *args = (struct reclaimer_args *)ptr;
	struct nfs4_client *clp = args->clp;
	struct nfs4_state_owner *sp;
	struct nfs4_state_recovery_ops *ops;
	int status = 0;

	daemonize("%u.%u.%u.%u-reclaim", NIPQUAD(clp->cl_addr));
	allow_signal(SIGKILL);

	atomic_inc(&clp->cl_count);
	complete(&args->complete);

	/* Ensure exclusive access to NFSv4 state */
	lock_kernel();
	down_write(&clp->cl_sem);
	/* Are there any NFS mounts out there? */
	if (list_empty(&clp->cl_superblocks))
		goto out;
restart_loop:
	status = nfs4_proc_renew(clp);
	switch (status) {
		case 0:
		case -NFS4ERR_CB_PATH_DOWN:
			goto out;
		case -NFS4ERR_STALE_CLIENTID:
		case -NFS4ERR_LEASE_MOVED:
			ops = &nfs4_reboot_recovery_ops;
			break;
		default:
			ops = &nfs4_network_partition_recovery_ops;
	};
	nfs4_state_mark_reclaim(clp);
	status = __nfs4_init_client(clp);
	if (status)
		goto out_error;
	/* Mark all delegations for reclaim */
	nfs_delegation_mark_reclaim(clp);
	/* Note: list is protected by exclusive lock on cl->cl_sem */
	list_for_each_entry(sp, &clp->cl_state_owners, so_list) {
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
	set_bit(NFS4CLNT_OK, &clp->cl_state);
	up_write(&clp->cl_sem);
	unlock_kernel();
	wake_up_all(&clp->cl_waitq);
	rpc_wake_up(&clp->cl_rpcwaitq);
	if (status == -NFS4ERR_CB_PATH_DOWN)
		nfs_handle_cb_pathdown(clp);
	nfs4_put_client(clp);
	return 0;
out_error:
	printk(KERN_WARNING "Error: state recovery failed on NFSv4 server %u.%u.%u.%u with error %d\n",
				NIPQUAD(clp->cl_addr.s_addr), -status);
	goto out;
}

/*
 * Local variables:
 *  c-basic-offset: 8
 * End:
 */
