// SPDX-License-Identifier: GPL-2.0-only
/*
 * linux/fs/nfs/delegation.c
 *
 * Copyright (C) 2004 Trond Myklebust
 *
 * NFS file delegation management
 *
 */
#include <linux/completion.h>
#include <linux/kthread.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/iversion.h>

#include <linux/nfs4.h>
#include <linux/nfs_fs.h>
#include <linux/nfs_xdr.h>

#include "nfs4_fs.h"
#include "nfs4session.h"
#include "delegation.h"
#include "internal.h"
#include "nfs4trace.h"

#define NFS_DEFAULT_DELEGATION_WATERMARK (5000U)

static atomic_long_t nfs_active_delegations;
static unsigned nfs_delegation_watermark = NFS_DEFAULT_DELEGATION_WATERMARK;

static void __nfs_free_delegation(struct nfs_delegation *delegation)
{
	put_cred(delegation->cred);
	delegation->cred = NULL;
	kfree_rcu(delegation, rcu);
}

static void nfs_mark_delegation_revoked(struct nfs_delegation *delegation)
{
	if (!test_and_set_bit(NFS_DELEGATION_REVOKED, &delegation->flags)) {
		delegation->stateid.type = NFS4_INVALID_STATEID_TYPE;
		atomic_long_dec(&nfs_active_delegations);
		if (!test_bit(NFS_DELEGATION_RETURNING, &delegation->flags))
			nfs_clear_verifier_delegated(delegation->inode);
	}
}

static struct nfs_delegation *nfs_get_delegation(struct nfs_delegation *delegation)
{
	refcount_inc(&delegation->refcount);
	return delegation;
}

static void nfs_put_delegation(struct nfs_delegation *delegation)
{
	if (refcount_dec_and_test(&delegation->refcount))
		__nfs_free_delegation(delegation);
}

static void nfs_free_delegation(struct nfs_delegation *delegation)
{
	nfs_mark_delegation_revoked(delegation);
	nfs_put_delegation(delegation);
}

/**
 * nfs_mark_delegation_referenced - set delegation's REFERENCED flag
 * @delegation: delegation to process
 *
 */
void nfs_mark_delegation_referenced(struct nfs_delegation *delegation)
{
	set_bit(NFS_DELEGATION_REFERENCED, &delegation->flags);
}

static void nfs_mark_return_delegation(struct nfs_server *server,
				       struct nfs_delegation *delegation)
{
	set_bit(NFS_DELEGATION_RETURN, &delegation->flags);
	set_bit(NFS4CLNT_DELEGRETURN, &server->nfs_client->cl_state);
}

static bool
nfs4_is_valid_delegation(const struct nfs_delegation *delegation,
		fmode_t flags)
{
	if (delegation != NULL && (delegation->type & flags) == flags &&
	    !test_bit(NFS_DELEGATION_REVOKED, &delegation->flags) &&
	    !test_bit(NFS_DELEGATION_RETURNING, &delegation->flags))
		return true;
	return false;
}

struct nfs_delegation *nfs4_get_valid_delegation(const struct inode *inode)
{
	struct nfs_delegation *delegation;

	delegation = rcu_dereference(NFS_I(inode)->delegation);
	if (nfs4_is_valid_delegation(delegation, 0))
		return delegation;
	return NULL;
}

static int
nfs4_do_check_delegation(struct inode *inode, fmode_t flags, bool mark)
{
	struct nfs_delegation *delegation;
	int ret = 0;

	flags &= FMODE_READ|FMODE_WRITE;
	rcu_read_lock();
	delegation = rcu_dereference(NFS_I(inode)->delegation);
	if (nfs4_is_valid_delegation(delegation, flags)) {
		if (mark)
			nfs_mark_delegation_referenced(delegation);
		ret = 1;
	}
	rcu_read_unlock();
	return ret;
}
/**
 * nfs4_have_delegation - check if inode has a delegation, mark it
 * NFS_DELEGATION_REFERENCED if there is one.
 * @inode: inode to check
 * @flags: delegation types to check for
 *
 * Returns one if inode has the indicated delegation, otherwise zero.
 */
int nfs4_have_delegation(struct inode *inode, fmode_t flags)
{
	return nfs4_do_check_delegation(inode, flags, true);
}

/*
 * nfs4_check_delegation - check if inode has a delegation, do not mark
 * NFS_DELEGATION_REFERENCED if it has one.
 */
int nfs4_check_delegation(struct inode *inode, fmode_t flags)
{
	return nfs4_do_check_delegation(inode, flags, false);
}

static int nfs_delegation_claim_locks(struct nfs4_state *state, const nfs4_stateid *stateid)
{
	struct inode *inode = state->inode;
	struct file_lock *fl;
	struct file_lock_context *flctx = inode->i_flctx;
	struct list_head *list;
	int status = 0;

	if (flctx == NULL)
		goto out;

	list = &flctx->flc_posix;
	spin_lock(&flctx->flc_lock);
restart:
	list_for_each_entry(fl, list, fl_list) {
		if (nfs_file_open_context(fl->fl_file)->state != state)
			continue;
		spin_unlock(&flctx->flc_lock);
		status = nfs4_lock_delegation_recall(fl, state, stateid);
		if (status < 0)
			goto out;
		spin_lock(&flctx->flc_lock);
	}
	if (list == &flctx->flc_posix) {
		list = &flctx->flc_flock;
		goto restart;
	}
	spin_unlock(&flctx->flc_lock);
out:
	return status;
}

static int nfs_delegation_claim_opens(struct inode *inode,
		const nfs4_stateid *stateid, fmode_t type)
{
	struct nfs_inode *nfsi = NFS_I(inode);
	struct nfs_open_context *ctx;
	struct nfs4_state_owner *sp;
	struct nfs4_state *state;
	unsigned int seq;
	int err;

again:
	rcu_read_lock();
	list_for_each_entry_rcu(ctx, &nfsi->open_files, list) {
		state = ctx->state;
		if (state == NULL)
			continue;
		if (!test_bit(NFS_DELEGATED_STATE, &state->flags))
			continue;
		if (!nfs4_valid_open_stateid(state))
			continue;
		if (!nfs4_stateid_match(&state->stateid, stateid))
			continue;
		if (!get_nfs_open_context(ctx))
			continue;
		rcu_read_unlock();
		sp = state->owner;
		/* Block nfs4_proc_unlck */
		mutex_lock(&sp->so_delegreturn_mutex);
		seq = raw_seqcount_begin(&sp->so_reclaim_seqcount);
		err = nfs4_open_delegation_recall(ctx, state, stateid);
		if (!err)
			err = nfs_delegation_claim_locks(state, stateid);
		if (!err && read_seqcount_retry(&sp->so_reclaim_seqcount, seq))
			err = -EAGAIN;
		mutex_unlock(&sp->so_delegreturn_mutex);
		put_nfs_open_context(ctx);
		if (err != 0)
			return err;
		goto again;
	}
	rcu_read_unlock();
	return 0;
}

/**
 * nfs_inode_reclaim_delegation - process a delegation reclaim request
 * @inode: inode to process
 * @cred: credential to use for request
 * @type: delegation type
 * @stateid: delegation stateid
 * @pagemod_limit: write delegation "space_limit"
 *
 */
void nfs_inode_reclaim_delegation(struct inode *inode, const struct cred *cred,
				  fmode_t type, const nfs4_stateid *stateid,
				  unsigned long pagemod_limit)
{
	struct nfs_delegation *delegation;
	const struct cred *oldcred = NULL;

	rcu_read_lock();
	delegation = rcu_dereference(NFS_I(inode)->delegation);
	if (delegation != NULL) {
		spin_lock(&delegation->lock);
		nfs4_stateid_copy(&delegation->stateid, stateid);
		delegation->type = type;
		delegation->pagemod_limit = pagemod_limit;
		oldcred = delegation->cred;
		delegation->cred = get_cred(cred);
		clear_bit(NFS_DELEGATION_NEED_RECLAIM, &delegation->flags);
		if (test_and_clear_bit(NFS_DELEGATION_REVOKED,
				       &delegation->flags))
			atomic_long_inc(&nfs_active_delegations);
		spin_unlock(&delegation->lock);
		rcu_read_unlock();
		put_cred(oldcred);
		trace_nfs4_reclaim_delegation(inode, type);
	} else {
		rcu_read_unlock();
		nfs_inode_set_delegation(inode, cred, type, stateid,
					 pagemod_limit);
	}
}

static int nfs_do_return_delegation(struct inode *inode, struct nfs_delegation *delegation, int issync)
{
	const struct cred *cred;
	int res = 0;

	if (!test_bit(NFS_DELEGATION_REVOKED, &delegation->flags)) {
		spin_lock(&delegation->lock);
		cred = get_cred(delegation->cred);
		spin_unlock(&delegation->lock);
		res = nfs4_proc_delegreturn(inode, cred,
				&delegation->stateid,
				issync);
		put_cred(cred);
	}
	return res;
}

static struct inode *nfs_delegation_grab_inode(struct nfs_delegation *delegation)
{
	struct inode *inode = NULL;

	spin_lock(&delegation->lock);
	if (delegation->inode != NULL)
		inode = igrab(delegation->inode);
	if (!inode)
		set_bit(NFS_DELEGATION_INODE_FREEING, &delegation->flags);
	spin_unlock(&delegation->lock);
	return inode;
}

static struct nfs_delegation *
nfs_start_delegation_return_locked(struct nfs_inode *nfsi)
{
	struct nfs_delegation *ret = NULL;
	struct nfs_delegation *delegation = rcu_dereference(nfsi->delegation);

	if (delegation == NULL)
		goto out;
	spin_lock(&delegation->lock);
	if (!test_and_set_bit(NFS_DELEGATION_RETURNING, &delegation->flags)) {
		clear_bit(NFS_DELEGATION_RETURN_DELAYED, &delegation->flags);
		/* Refcount matched in nfs_end_delegation_return() */
		ret = nfs_get_delegation(delegation);
	}
	spin_unlock(&delegation->lock);
	if (ret)
		nfs_clear_verifier_delegated(&nfsi->vfs_inode);
out:
	return ret;
}

static struct nfs_delegation *
nfs_start_delegation_return(struct nfs_inode *nfsi)
{
	struct nfs_delegation *delegation;

	rcu_read_lock();
	delegation = nfs_start_delegation_return_locked(nfsi);
	rcu_read_unlock();
	return delegation;
}

static void nfs_abort_delegation_return(struct nfs_delegation *delegation,
					struct nfs_client *clp, int err)
{

	spin_lock(&delegation->lock);
	clear_bit(NFS_DELEGATION_RETURNING, &delegation->flags);
	if (err == -EAGAIN) {
		set_bit(NFS_DELEGATION_RETURN_DELAYED, &delegation->flags);
		set_bit(NFS4CLNT_DELEGRETURN_DELAYED, &clp->cl_state);
	}
	spin_unlock(&delegation->lock);
}

static struct nfs_delegation *
nfs_detach_delegation_locked(struct nfs_inode *nfsi,
		struct nfs_delegation *delegation,
		struct nfs_client *clp)
{
	struct nfs_delegation *deleg_cur =
		rcu_dereference_protected(nfsi->delegation,
				lockdep_is_held(&clp->cl_lock));

	if (deleg_cur == NULL || delegation != deleg_cur)
		return NULL;

	spin_lock(&delegation->lock);
	if (!delegation->inode) {
		spin_unlock(&delegation->lock);
		return NULL;
	}
	list_del_rcu(&delegation->super_list);
	delegation->inode = NULL;
	rcu_assign_pointer(nfsi->delegation, NULL);
	spin_unlock(&delegation->lock);
	return delegation;
}

static struct nfs_delegation *nfs_detach_delegation(struct nfs_inode *nfsi,
		struct nfs_delegation *delegation,
		struct nfs_server *server)
{
	struct nfs_client *clp = server->nfs_client;

	spin_lock(&clp->cl_lock);
	delegation = nfs_detach_delegation_locked(nfsi, delegation, clp);
	spin_unlock(&clp->cl_lock);
	return delegation;
}

static struct nfs_delegation *
nfs_inode_detach_delegation(struct inode *inode)
{
	struct nfs_inode *nfsi = NFS_I(inode);
	struct nfs_server *server = NFS_SERVER(inode);
	struct nfs_delegation *delegation;

	rcu_read_lock();
	delegation = rcu_dereference(nfsi->delegation);
	if (delegation != NULL)
		delegation = nfs_detach_delegation(nfsi, delegation, server);
	rcu_read_unlock();
	return delegation;
}

static void
nfs_update_delegation_cred(struct nfs_delegation *delegation,
		const struct cred *cred)
{
	const struct cred *old;

	if (cred_fscmp(delegation->cred, cred) != 0) {
		old = xchg(&delegation->cred, get_cred(cred));
		put_cred(old);
	}
}

static void
nfs_update_inplace_delegation(struct nfs_delegation *delegation,
		const struct nfs_delegation *update)
{
	if (nfs4_stateid_is_newer(&update->stateid, &delegation->stateid)) {
		delegation->stateid.seqid = update->stateid.seqid;
		smp_wmb();
		delegation->type = update->type;
		delegation->pagemod_limit = update->pagemod_limit;
		if (test_bit(NFS_DELEGATION_REVOKED, &delegation->flags)) {
			delegation->change_attr = update->change_attr;
			nfs_update_delegation_cred(delegation, update->cred);
			/* smp_mb__before_atomic() is implicit due to xchg() */
			clear_bit(NFS_DELEGATION_REVOKED, &delegation->flags);
			atomic_long_inc(&nfs_active_delegations);
		}
	}
}

/**
 * nfs_inode_set_delegation - set up a delegation on an inode
 * @inode: inode to which delegation applies
 * @cred: cred to use for subsequent delegation processing
 * @type: delegation type
 * @stateid: delegation stateid
 * @pagemod_limit: write delegation "space_limit"
 *
 * Returns zero on success, or a negative errno value.
 */
int nfs_inode_set_delegation(struct inode *inode, const struct cred *cred,
				  fmode_t type,
				  const nfs4_stateid *stateid,
				  unsigned long pagemod_limit)
{
	struct nfs_server *server = NFS_SERVER(inode);
	struct nfs_client *clp = server->nfs_client;
	struct nfs_inode *nfsi = NFS_I(inode);
	struct nfs_delegation *delegation, *old_delegation;
	struct nfs_delegation *freeme = NULL;
	int status = 0;

	delegation = kmalloc(sizeof(*delegation), GFP_NOFS);
	if (delegation == NULL)
		return -ENOMEM;
	nfs4_stateid_copy(&delegation->stateid, stateid);
	refcount_set(&delegation->refcount, 1);
	delegation->type = type;
	delegation->pagemod_limit = pagemod_limit;
	delegation->change_attr = inode_peek_iversion_raw(inode);
	delegation->cred = get_cred(cred);
	delegation->inode = inode;
	delegation->flags = 1<<NFS_DELEGATION_REFERENCED;
	spin_lock_init(&delegation->lock);

	spin_lock(&clp->cl_lock);
	old_delegation = rcu_dereference_protected(nfsi->delegation,
					lockdep_is_held(&clp->cl_lock));
	if (old_delegation == NULL)
		goto add_new;
	/* Is this an update of the existing delegation? */
	if (nfs4_stateid_match_other(&old_delegation->stateid,
				&delegation->stateid)) {
		spin_lock(&old_delegation->lock);
		nfs_update_inplace_delegation(old_delegation,
				delegation);
		spin_unlock(&old_delegation->lock);
		goto out;
	}
	if (!test_bit(NFS_DELEGATION_REVOKED, &old_delegation->flags)) {
		/*
		 * Deal with broken servers that hand out two
		 * delegations for the same file.
		 * Allow for upgrades to a WRITE delegation, but
		 * nothing else.
		 */
		dfprintk(FILE, "%s: server %s handed out "
				"a duplicate delegation!\n",
				__func__, clp->cl_hostname);
		if (delegation->type == old_delegation->type ||
		    !(delegation->type & FMODE_WRITE)) {
			freeme = delegation;
			delegation = NULL;
			goto out;
		}
		if (test_and_set_bit(NFS_DELEGATION_RETURNING,
					&old_delegation->flags))
			goto out;
	}
	freeme = nfs_detach_delegation_locked(nfsi, old_delegation, clp);
	if (freeme == NULL)
		goto out;
add_new:
	/*
	 * If we didn't revalidate the change attribute before setting
	 * the delegation, then pre-emptively ask for a full attribute
	 * cache revalidation.
	 */
	spin_lock(&inode->i_lock);
	if (NFS_I(inode)->cache_validity & NFS_INO_INVALID_CHANGE)
		nfs_set_cache_invalid(inode,
			NFS_INO_INVALID_ATIME | NFS_INO_INVALID_CTIME |
			NFS_INO_INVALID_MTIME | NFS_INO_INVALID_SIZE |
			NFS_INO_INVALID_BLOCKS | NFS_INO_INVALID_NLINK |
			NFS_INO_INVALID_OTHER | NFS_INO_INVALID_DATA |
			NFS_INO_INVALID_ACCESS | NFS_INO_INVALID_ACL |
			NFS_INO_INVALID_XATTR);
	spin_unlock(&inode->i_lock);

	list_add_tail_rcu(&delegation->super_list, &server->delegations);
	rcu_assign_pointer(nfsi->delegation, delegation);
	delegation = NULL;

	atomic_long_inc(&nfs_active_delegations);

	trace_nfs4_set_delegation(inode, type);
out:
	spin_unlock(&clp->cl_lock);
	if (delegation != NULL)
		__nfs_free_delegation(delegation);
	if (freeme != NULL) {
		nfs_do_return_delegation(inode, freeme, 0);
		nfs_free_delegation(freeme);
	}
	return status;
}

/*
 * Basic procedure for returning a delegation to the server
 */
static int nfs_end_delegation_return(struct inode *inode, struct nfs_delegation *delegation, int issync)
{
	struct nfs_client *clp = NFS_SERVER(inode)->nfs_client;
	unsigned int mode = O_WRONLY | O_RDWR;
	int err = 0;

	if (delegation == NULL)
		return 0;

	if (!issync)
		mode |= O_NONBLOCK;
	/* Recall of any remaining application leases */
	err = break_lease(inode, mode);

	while (err == 0) {
		if (test_bit(NFS_DELEGATION_REVOKED, &delegation->flags))
			break;
		err = nfs_delegation_claim_opens(inode, &delegation->stateid,
				delegation->type);
		if (!issync || err != -EAGAIN)
			break;
		/*
		 * Guard against state recovery
		 */
		err = nfs4_wait_clnt_recover(clp);
	}

	if (err) {
		nfs_abort_delegation_return(delegation, clp, err);
		goto out;
	}

	err = nfs_do_return_delegation(inode, delegation, issync);
out:
	/* Refcount matched in nfs_start_delegation_return_locked() */
	nfs_put_delegation(delegation);
	return err;
}

static bool nfs_delegation_need_return(struct nfs_delegation *delegation)
{
	bool ret = false;

	if (test_and_clear_bit(NFS_DELEGATION_RETURN, &delegation->flags))
		ret = true;
	else if (test_bit(NFS_DELEGATION_RETURN_IF_CLOSED, &delegation->flags)) {
		struct inode *inode;

		spin_lock(&delegation->lock);
		inode = delegation->inode;
		if (inode && list_empty(&NFS_I(inode)->open_files))
			ret = true;
		spin_unlock(&delegation->lock);
	}
	if (ret)
		clear_bit(NFS_DELEGATION_RETURN_IF_CLOSED, &delegation->flags);
	if (test_bit(NFS_DELEGATION_RETURNING, &delegation->flags) ||
	    test_bit(NFS_DELEGATION_RETURN_DELAYED, &delegation->flags) ||
	    test_bit(NFS_DELEGATION_REVOKED, &delegation->flags))
		ret = false;

	return ret;
}

static int nfs_server_return_marked_delegations(struct nfs_server *server,
		void __always_unused *data)
{
	struct nfs_delegation *delegation;
	struct nfs_delegation *prev;
	struct inode *inode;
	struct inode *place_holder = NULL;
	struct nfs_delegation *place_holder_deleg = NULL;
	int err = 0;

restart:
	/*
	 * To avoid quadratic looping we hold a reference
	 * to an inode place_holder.  Each time we restart, we
	 * list delegation in the server from the delegations
	 * of that inode.
	 * prev is an RCU-protected pointer to a delegation which
	 * wasn't marked for return and might be a good choice for
	 * the next place_holder.
	 */
	prev = NULL;
	delegation = NULL;
	rcu_read_lock();
	if (place_holder)
		delegation = rcu_dereference(NFS_I(place_holder)->delegation);
	if (!delegation || delegation != place_holder_deleg)
		delegation = list_entry_rcu(server->delegations.next,
					    struct nfs_delegation, super_list);
	list_for_each_entry_from_rcu(delegation, &server->delegations, super_list) {
		struct inode *to_put = NULL;

		if (test_bit(NFS_DELEGATION_INODE_FREEING, &delegation->flags))
			continue;
		if (!nfs_delegation_need_return(delegation)) {
			if (nfs4_is_valid_delegation(delegation, 0))
				prev = delegation;
			continue;
		}

		if (prev) {
			struct inode *tmp = nfs_delegation_grab_inode(prev);
			if (tmp) {
				to_put = place_holder;
				place_holder = tmp;
				place_holder_deleg = prev;
			}
		}

		inode = nfs_delegation_grab_inode(delegation);
		if (inode == NULL) {
			rcu_read_unlock();
			iput(to_put);
			goto restart;
		}
		delegation = nfs_start_delegation_return_locked(NFS_I(inode));
		rcu_read_unlock();

		iput(to_put);

		err = nfs_end_delegation_return(inode, delegation, 0);
		iput(inode);
		cond_resched();
		if (!err)
			goto restart;
		set_bit(NFS4CLNT_DELEGRETURN, &server->nfs_client->cl_state);
		goto out;
	}
	rcu_read_unlock();
out:
	iput(place_holder);
	return err;
}

static bool nfs_server_clear_delayed_delegations(struct nfs_server *server)
{
	struct nfs_delegation *d;
	bool ret = false;

	list_for_each_entry_rcu (d, &server->delegations, super_list) {
		if (!test_bit(NFS_DELEGATION_RETURN_DELAYED, &d->flags))
			continue;
		nfs_mark_return_delegation(server, d);
		clear_bit(NFS_DELEGATION_RETURN_DELAYED, &d->flags);
		ret = true;
	}
	return ret;
}

static bool nfs_client_clear_delayed_delegations(struct nfs_client *clp)
{
	struct nfs_server *server;
	bool ret = false;

	if (!test_and_clear_bit(NFS4CLNT_DELEGRETURN_DELAYED, &clp->cl_state))
		goto out;
	rcu_read_lock();
	list_for_each_entry_rcu (server, &clp->cl_superblocks, client_link) {
		if (nfs_server_clear_delayed_delegations(server))
			ret = true;
	}
	rcu_read_unlock();
out:
	return ret;
}

/**
 * nfs_client_return_marked_delegations - return previously marked delegations
 * @clp: nfs_client to process
 *
 * Note that this function is designed to be called by the state
 * manager thread. For this reason, it cannot flush the dirty data,
 * since that could deadlock in case of a state recovery error.
 *
 * Returns zero on success, or a negative errno value.
 */
int nfs_client_return_marked_delegations(struct nfs_client *clp)
{
	int err = nfs_client_for_each_server(
		clp, nfs_server_return_marked_delegations, NULL);
	if (err)
		return err;
	/* If a return was delayed, sleep to prevent hard looping */
	if (nfs_client_clear_delayed_delegations(clp))
		ssleep(1);
	return 0;
}

/**
 * nfs_inode_evict_delegation - return delegation, don't reclaim opens
 * @inode: inode to process
 *
 * Does not protect against delegation reclaims, therefore really only safe
 * to be called from nfs4_clear_inode(). Guaranteed to always free
 * the delegation structure.
 */
void nfs_inode_evict_delegation(struct inode *inode)
{
	struct nfs_delegation *delegation;

	delegation = nfs_inode_detach_delegation(inode);
	if (delegation != NULL) {
		set_bit(NFS_DELEGATION_RETURNING, &delegation->flags);
		set_bit(NFS_DELEGATION_INODE_FREEING, &delegation->flags);
		nfs_do_return_delegation(inode, delegation, 1);
		nfs_free_delegation(delegation);
	}
}

/**
 * nfs4_inode_return_delegation - synchronously return a delegation
 * @inode: inode to process
 *
 * This routine will always flush any dirty data to disk on the
 * assumption that if we need to return the delegation, then
 * we should stop caching.
 *
 * Returns zero on success, or a negative errno value.
 */
int nfs4_inode_return_delegation(struct inode *inode)
{
	struct nfs_inode *nfsi = NFS_I(inode);
	struct nfs_delegation *delegation;

	delegation = nfs_start_delegation_return(nfsi);
	if (delegation != NULL) {
		/* Synchronous recall of any application leases */
		break_lease(inode, O_WRONLY | O_RDWR);
		if (S_ISREG(inode->i_mode))
			nfs_wb_all(inode);
		return nfs_end_delegation_return(inode, delegation, 1);
	}
	return 0;
}

/**
 * nfs4_inode_return_delegation_on_close - asynchronously return a delegation
 * @inode: inode to process
 *
 * This routine is called on file close in order to determine if the
 * inode delegation needs to be returned immediately.
 */
void nfs4_inode_return_delegation_on_close(struct inode *inode)
{
	struct nfs_delegation *delegation;
	struct nfs_delegation *ret = NULL;

	if (!inode)
		return;
	rcu_read_lock();
	delegation = nfs4_get_valid_delegation(inode);
	if (!delegation)
		goto out;
	if (test_bit(NFS_DELEGATION_RETURN_IF_CLOSED, &delegation->flags) ||
	    atomic_long_read(&nfs_active_delegations) >= nfs_delegation_watermark) {
		spin_lock(&delegation->lock);
		if (delegation->inode &&
		    list_empty(&NFS_I(inode)->open_files) &&
		    !test_and_set_bit(NFS_DELEGATION_RETURNING, &delegation->flags)) {
			clear_bit(NFS_DELEGATION_RETURN_IF_CLOSED, &delegation->flags);
			/* Refcount matched in nfs_end_delegation_return() */
			ret = nfs_get_delegation(delegation);
		}
		spin_unlock(&delegation->lock);
		if (ret)
			nfs_clear_verifier_delegated(inode);
	}
out:
	rcu_read_unlock();
	nfs_end_delegation_return(inode, ret, 0);
}

/**
 * nfs4_inode_make_writeable
 * @inode: pointer to inode
 *
 * Make the inode writeable by returning the delegation if necessary
 *
 * Returns zero on success, or a negative errno value.
 */
int nfs4_inode_make_writeable(struct inode *inode)
{
	struct nfs_delegation *delegation;

	rcu_read_lock();
	delegation = nfs4_get_valid_delegation(inode);
	if (delegation == NULL ||
	    (nfs4_has_session(NFS_SERVER(inode)->nfs_client) &&
	     (delegation->type & FMODE_WRITE))) {
		rcu_read_unlock();
		return 0;
	}
	rcu_read_unlock();
	return nfs4_inode_return_delegation(inode);
}

static void nfs_mark_return_if_closed_delegation(struct nfs_server *server,
		struct nfs_delegation *delegation)
{
	set_bit(NFS_DELEGATION_RETURN_IF_CLOSED, &delegation->flags);
	set_bit(NFS4CLNT_DELEGRETURN, &server->nfs_client->cl_state);
}

static bool nfs_server_mark_return_all_delegations(struct nfs_server *server)
{
	struct nfs_delegation *delegation;
	bool ret = false;

	list_for_each_entry_rcu(delegation, &server->delegations, super_list) {
		nfs_mark_return_delegation(server, delegation);
		ret = true;
	}
	return ret;
}

static void nfs_client_mark_return_all_delegations(struct nfs_client *clp)
{
	struct nfs_server *server;

	rcu_read_lock();
	list_for_each_entry_rcu(server, &clp->cl_superblocks, client_link)
		nfs_server_mark_return_all_delegations(server);
	rcu_read_unlock();
}

static void nfs_delegation_run_state_manager(struct nfs_client *clp)
{
	if (test_bit(NFS4CLNT_DELEGRETURN, &clp->cl_state))
		nfs4_schedule_state_manager(clp);
}

/**
 * nfs_expire_all_delegations
 * @clp: client to process
 *
 */
void nfs_expire_all_delegations(struct nfs_client *clp)
{
	nfs_client_mark_return_all_delegations(clp);
	nfs_delegation_run_state_manager(clp);
}

/**
 * nfs_server_return_all_delegations - return delegations for one superblock
 * @server: pointer to nfs_server to process
 *
 */
void nfs_server_return_all_delegations(struct nfs_server *server)
{
	struct nfs_client *clp = server->nfs_client;
	bool need_wait;

	if (clp == NULL)
		return;

	rcu_read_lock();
	need_wait = nfs_server_mark_return_all_delegations(server);
	rcu_read_unlock();

	if (need_wait) {
		nfs4_schedule_state_manager(clp);
		nfs4_wait_clnt_recover(clp);
	}
}

static void nfs_mark_return_unused_delegation_types(struct nfs_server *server,
						 fmode_t flags)
{
	struct nfs_delegation *delegation;

	list_for_each_entry_rcu(delegation, &server->delegations, super_list) {
		if ((delegation->type == (FMODE_READ|FMODE_WRITE)) && !(flags & FMODE_WRITE))
			continue;
		if (delegation->type & flags)
			nfs_mark_return_if_closed_delegation(server, delegation);
	}
}

static void nfs_client_mark_return_unused_delegation_types(struct nfs_client *clp,
							fmode_t flags)
{
	struct nfs_server *server;

	rcu_read_lock();
	list_for_each_entry_rcu(server, &clp->cl_superblocks, client_link)
		nfs_mark_return_unused_delegation_types(server, flags);
	rcu_read_unlock();
}

static void nfs_revoke_delegation(struct inode *inode,
		const nfs4_stateid *stateid)
{
	struct nfs_delegation *delegation;
	nfs4_stateid tmp;
	bool ret = false;

	rcu_read_lock();
	delegation = rcu_dereference(NFS_I(inode)->delegation);
	if (delegation == NULL)
		goto out;
	if (stateid == NULL) {
		nfs4_stateid_copy(&tmp, &delegation->stateid);
		stateid = &tmp;
	} else {
		if (!nfs4_stateid_match_other(stateid, &delegation->stateid))
			goto out;
		spin_lock(&delegation->lock);
		if (stateid->seqid) {
			if (nfs4_stateid_is_newer(&delegation->stateid, stateid)) {
				spin_unlock(&delegation->lock);
				goto out;
			}
			delegation->stateid.seqid = stateid->seqid;
		}
		spin_unlock(&delegation->lock);
	}
	nfs_mark_delegation_revoked(delegation);
	ret = true;
out:
	rcu_read_unlock();
	if (ret)
		nfs_inode_find_state_and_recover(inode, stateid);
}

void nfs_remove_bad_delegation(struct inode *inode,
		const nfs4_stateid *stateid)
{
	nfs_revoke_delegation(inode, stateid);
}
EXPORT_SYMBOL_GPL(nfs_remove_bad_delegation);

void nfs_delegation_mark_returned(struct inode *inode,
		const nfs4_stateid *stateid)
{
	struct nfs_delegation *delegation;

	if (!inode)
		return;

	rcu_read_lock();
	delegation = rcu_dereference(NFS_I(inode)->delegation);
	if (!delegation)
		goto out_rcu_unlock;

	spin_lock(&delegation->lock);
	if (!nfs4_stateid_match_other(stateid, &delegation->stateid))
		goto out_spin_unlock;
	if (stateid->seqid) {
		/* If delegation->stateid is newer, dont mark as returned */
		if (nfs4_stateid_is_newer(&delegation->stateid, stateid))
			goto out_clear_returning;
		if (delegation->stateid.seqid != stateid->seqid)
			delegation->stateid.seqid = stateid->seqid;
	}

	nfs_mark_delegation_revoked(delegation);

out_clear_returning:
	clear_bit(NFS_DELEGATION_RETURNING, &delegation->flags);
out_spin_unlock:
	spin_unlock(&delegation->lock);
out_rcu_unlock:
	rcu_read_unlock();

	nfs_inode_find_state_and_recover(inode, stateid);
}

/**
 * nfs_expire_unused_delegation_types
 * @clp: client to process
 * @flags: delegation types to expire
 *
 */
void nfs_expire_unused_delegation_types(struct nfs_client *clp, fmode_t flags)
{
	nfs_client_mark_return_unused_delegation_types(clp, flags);
	nfs_delegation_run_state_manager(clp);
}

static void nfs_mark_return_unreferenced_delegations(struct nfs_server *server)
{
	struct nfs_delegation *delegation;

	list_for_each_entry_rcu(delegation, &server->delegations, super_list) {
		if (test_and_clear_bit(NFS_DELEGATION_REFERENCED, &delegation->flags))
			continue;
		nfs_mark_return_if_closed_delegation(server, delegation);
	}
}

/**
 * nfs_expire_unreferenced_delegations - Eliminate unused delegations
 * @clp: nfs_client to process
 *
 */
void nfs_expire_unreferenced_delegations(struct nfs_client *clp)
{
	struct nfs_server *server;

	rcu_read_lock();
	list_for_each_entry_rcu(server, &clp->cl_superblocks, client_link)
		nfs_mark_return_unreferenced_delegations(server);
	rcu_read_unlock();

	nfs_delegation_run_state_manager(clp);
}

/**
 * nfs_async_inode_return_delegation - asynchronously return a delegation
 * @inode: inode to process
 * @stateid: state ID information
 *
 * Returns zero on success, or a negative errno value.
 */
int nfs_async_inode_return_delegation(struct inode *inode,
				      const nfs4_stateid *stateid)
{
	struct nfs_server *server = NFS_SERVER(inode);
	struct nfs_client *clp = server->nfs_client;
	struct nfs_delegation *delegation;

	rcu_read_lock();
	delegation = nfs4_get_valid_delegation(inode);
	if (delegation == NULL)
		goto out_enoent;
	if (stateid != NULL &&
	    !clp->cl_mvops->match_stateid(&delegation->stateid, stateid))
		goto out_enoent;
	nfs_mark_return_delegation(server, delegation);
	rcu_read_unlock();

	/* If there are any application leases or delegations, recall them */
	break_lease(inode, O_WRONLY | O_RDWR | O_NONBLOCK);

	nfs_delegation_run_state_manager(clp);
	return 0;
out_enoent:
	rcu_read_unlock();
	return -ENOENT;
}

static struct inode *
nfs_delegation_find_inode_server(struct nfs_server *server,
				 const struct nfs_fh *fhandle)
{
	struct nfs_delegation *delegation;
	struct super_block *freeme = NULL;
	struct inode *res = NULL;

	list_for_each_entry_rcu(delegation, &server->delegations, super_list) {
		spin_lock(&delegation->lock);
		if (delegation->inode != NULL &&
		    !test_bit(NFS_DELEGATION_REVOKED, &delegation->flags) &&
		    nfs_compare_fh(fhandle, &NFS_I(delegation->inode)->fh) == 0) {
			if (nfs_sb_active(server->super)) {
				freeme = server->super;
				res = igrab(delegation->inode);
			}
			spin_unlock(&delegation->lock);
			if (res != NULL)
				return res;
			if (freeme) {
				rcu_read_unlock();
				nfs_sb_deactive(freeme);
				rcu_read_lock();
			}
			return ERR_PTR(-EAGAIN);
		}
		spin_unlock(&delegation->lock);
	}
	return ERR_PTR(-ENOENT);
}

/**
 * nfs_delegation_find_inode - retrieve the inode associated with a delegation
 * @clp: client state handle
 * @fhandle: filehandle from a delegation recall
 *
 * Returns pointer to inode matching "fhandle," or NULL if a matching inode
 * cannot be found.
 */
struct inode *nfs_delegation_find_inode(struct nfs_client *clp,
					const struct nfs_fh *fhandle)
{
	struct nfs_server *server;
	struct inode *res;

	rcu_read_lock();
	list_for_each_entry_rcu(server, &clp->cl_superblocks, client_link) {
		res = nfs_delegation_find_inode_server(server, fhandle);
		if (res != ERR_PTR(-ENOENT)) {
			rcu_read_unlock();
			return res;
		}
	}
	rcu_read_unlock();
	return ERR_PTR(-ENOENT);
}

static void nfs_delegation_mark_reclaim_server(struct nfs_server *server)
{
	struct nfs_delegation *delegation;

	list_for_each_entry_rcu(delegation, &server->delegations, super_list) {
		/*
		 * If the delegation may have been admin revoked, then we
		 * cannot reclaim it.
		 */
		if (test_bit(NFS_DELEGATION_TEST_EXPIRED, &delegation->flags))
			continue;
		set_bit(NFS_DELEGATION_NEED_RECLAIM, &delegation->flags);
	}
}

/**
 * nfs_delegation_mark_reclaim - mark all delegations as needing to be reclaimed
 * @clp: nfs_client to process
 *
 */
void nfs_delegation_mark_reclaim(struct nfs_client *clp)
{
	struct nfs_server *server;

	rcu_read_lock();
	list_for_each_entry_rcu(server, &clp->cl_superblocks, client_link)
		nfs_delegation_mark_reclaim_server(server);
	rcu_read_unlock();
}

static int nfs_server_reap_unclaimed_delegations(struct nfs_server *server,
		void __always_unused *data)
{
	struct nfs_delegation *delegation;
	struct inode *inode;
restart:
	rcu_read_lock();
restart_locked:
	list_for_each_entry_rcu(delegation, &server->delegations, super_list) {
		if (test_bit(NFS_DELEGATION_INODE_FREEING,
					&delegation->flags) ||
		    test_bit(NFS_DELEGATION_RETURNING,
					&delegation->flags) ||
		    test_bit(NFS_DELEGATION_NEED_RECLAIM,
					&delegation->flags) == 0)
			continue;
		inode = nfs_delegation_grab_inode(delegation);
		if (inode == NULL)
			goto restart_locked;
		delegation = nfs_start_delegation_return_locked(NFS_I(inode));
		rcu_read_unlock();
		if (delegation != NULL) {
			if (nfs_detach_delegation(NFS_I(inode), delegation,
						server) != NULL)
				nfs_free_delegation(delegation);
			/* Match nfs_start_delegation_return_locked */
			nfs_put_delegation(delegation);
		}
		iput(inode);
		cond_resched();
		goto restart;
	}
	rcu_read_unlock();
	return 0;
}

/**
 * nfs_delegation_reap_unclaimed - reap unclaimed delegations after reboot recovery is done
 * @clp: nfs_client to process
 *
 */
void nfs_delegation_reap_unclaimed(struct nfs_client *clp)
{
	nfs_client_for_each_server(clp, nfs_server_reap_unclaimed_delegations,
			NULL);
}

static inline bool nfs4_server_rebooted(const struct nfs_client *clp)
{
	return (clp->cl_state & (BIT(NFS4CLNT_CHECK_LEASE) |
				BIT(NFS4CLNT_LEASE_EXPIRED) |
				BIT(NFS4CLNT_SESSION_RESET))) != 0;
}

static void nfs_mark_test_expired_delegation(struct nfs_server *server,
	    struct nfs_delegation *delegation)
{
	if (delegation->stateid.type == NFS4_INVALID_STATEID_TYPE)
		return;
	clear_bit(NFS_DELEGATION_NEED_RECLAIM, &delegation->flags);
	set_bit(NFS_DELEGATION_TEST_EXPIRED, &delegation->flags);
	set_bit(NFS4CLNT_DELEGATION_EXPIRED, &server->nfs_client->cl_state);
}

static void nfs_inode_mark_test_expired_delegation(struct nfs_server *server,
		struct inode *inode)
{
	struct nfs_delegation *delegation;

	rcu_read_lock();
	delegation = rcu_dereference(NFS_I(inode)->delegation);
	if (delegation)
		nfs_mark_test_expired_delegation(server, delegation);
	rcu_read_unlock();

}

static void nfs_delegation_mark_test_expired_server(struct nfs_server *server)
{
	struct nfs_delegation *delegation;

	list_for_each_entry_rcu(delegation, &server->delegations, super_list)
		nfs_mark_test_expired_delegation(server, delegation);
}

/**
 * nfs_mark_test_expired_all_delegations - mark all delegations for testing
 * @clp: nfs_client to process
 *
 * Iterates through all the delegations associated with this server and
 * marks them as needing to be checked for validity.
 */
void nfs_mark_test_expired_all_delegations(struct nfs_client *clp)
{
	struct nfs_server *server;

	rcu_read_lock();
	list_for_each_entry_rcu(server, &clp->cl_superblocks, client_link)
		nfs_delegation_mark_test_expired_server(server);
	rcu_read_unlock();
}

/**
 * nfs_test_expired_all_delegations - test all delegations for a client
 * @clp: nfs_client to process
 *
 * Helper for handling "recallable state revoked" status from server.
 */
void nfs_test_expired_all_delegations(struct nfs_client *clp)
{
	nfs_mark_test_expired_all_delegations(clp);
	nfs4_schedule_state_manager(clp);
}

static void
nfs_delegation_test_free_expired(struct inode *inode,
		nfs4_stateid *stateid,
		const struct cred *cred)
{
	struct nfs_server *server = NFS_SERVER(inode);
	const struct nfs4_minor_version_ops *ops = server->nfs_client->cl_mvops;
	int status;

	if (!cred)
		return;
	status = ops->test_and_free_expired(server, stateid, cred);
	if (status == -NFS4ERR_EXPIRED || status == -NFS4ERR_BAD_STATEID)
		nfs_remove_bad_delegation(inode, stateid);
}

static int nfs_server_reap_expired_delegations(struct nfs_server *server,
		void __always_unused *data)
{
	struct nfs_delegation *delegation;
	struct inode *inode;
	const struct cred *cred;
	nfs4_stateid stateid;
restart:
	rcu_read_lock();
restart_locked:
	list_for_each_entry_rcu(delegation, &server->delegations, super_list) {
		if (test_bit(NFS_DELEGATION_INODE_FREEING,
					&delegation->flags) ||
		    test_bit(NFS_DELEGATION_RETURNING,
					&delegation->flags) ||
		    test_bit(NFS_DELEGATION_TEST_EXPIRED,
					&delegation->flags) == 0)
			continue;
		inode = nfs_delegation_grab_inode(delegation);
		if (inode == NULL)
			goto restart_locked;
		spin_lock(&delegation->lock);
		cred = get_cred_rcu(delegation->cred);
		nfs4_stateid_copy(&stateid, &delegation->stateid);
		spin_unlock(&delegation->lock);
		clear_bit(NFS_DELEGATION_TEST_EXPIRED, &delegation->flags);
		rcu_read_unlock();
		nfs_delegation_test_free_expired(inode, &stateid, cred);
		put_cred(cred);
		if (!nfs4_server_rebooted(server->nfs_client)) {
			iput(inode);
			cond_resched();
			goto restart;
		}
		nfs_inode_mark_test_expired_delegation(server,inode);
		iput(inode);
		return -EAGAIN;
	}
	rcu_read_unlock();
	return 0;
}

/**
 * nfs_reap_expired_delegations - reap expired delegations
 * @clp: nfs_client to process
 *
 * Iterates through all the delegations associated with this server and
 * checks if they have may have been revoked. This function is usually
 * expected to be called in cases where the server may have lost its
 * lease.
 */
void nfs_reap_expired_delegations(struct nfs_client *clp)
{
	nfs_client_for_each_server(clp, nfs_server_reap_expired_delegations,
			NULL);
}

void nfs_inode_find_delegation_state_and_recover(struct inode *inode,
		const nfs4_stateid *stateid)
{
	struct nfs_client *clp = NFS_SERVER(inode)->nfs_client;
	struct nfs_delegation *delegation;
	bool found = false;

	rcu_read_lock();
	delegation = rcu_dereference(NFS_I(inode)->delegation);
	if (delegation &&
	    nfs4_stateid_match_or_older(&delegation->stateid, stateid) &&
	    !test_bit(NFS_DELEGATION_REVOKED, &delegation->flags)) {
		nfs_mark_test_expired_delegation(NFS_SERVER(inode), delegation);
		found = true;
	}
	rcu_read_unlock();
	if (found)
		nfs4_schedule_state_manager(clp);
}

/**
 * nfs_delegations_present - check for existence of delegations
 * @clp: client state handle
 *
 * Returns one if there are any nfs_delegation structures attached
 * to this nfs_client.
 */
int nfs_delegations_present(struct nfs_client *clp)
{
	struct nfs_server *server;
	int ret = 0;

	rcu_read_lock();
	list_for_each_entry_rcu(server, &clp->cl_superblocks, client_link)
		if (!list_empty(&server->delegations)) {
			ret = 1;
			break;
		}
	rcu_read_unlock();
	return ret;
}

/**
 * nfs4_refresh_delegation_stateid - Update delegation stateid seqid
 * @dst: stateid to refresh
 * @inode: inode to check
 *
 * Returns "true" and updates "dst->seqid" * if inode had a delegation
 * that matches our delegation stateid. Otherwise "false" is returned.
 */
bool nfs4_refresh_delegation_stateid(nfs4_stateid *dst, struct inode *inode)
{
	struct nfs_delegation *delegation;
	bool ret = false;
	if (!inode)
		goto out;

	rcu_read_lock();
	delegation = rcu_dereference(NFS_I(inode)->delegation);
	if (delegation != NULL &&
	    nfs4_stateid_match_other(dst, &delegation->stateid) &&
	    nfs4_stateid_is_newer(&delegation->stateid, dst) &&
	    !test_bit(NFS_DELEGATION_REVOKED, &delegation->flags)) {
		dst->seqid = delegation->stateid.seqid;
		ret = true;
	}
	rcu_read_unlock();
out:
	return ret;
}

/**
 * nfs4_copy_delegation_stateid - Copy inode's state ID information
 * @inode: inode to check
 * @flags: delegation type requirement
 * @dst: stateid data structure to fill in
 * @cred: optional argument to retrieve credential
 *
 * Returns "true" and fills in "dst->data" * if inode had a delegation,
 * otherwise "false" is returned.
 */
bool nfs4_copy_delegation_stateid(struct inode *inode, fmode_t flags,
		nfs4_stateid *dst, const struct cred **cred)
{
	struct nfs_inode *nfsi = NFS_I(inode);
	struct nfs_delegation *delegation;
	bool ret = false;

	flags &= FMODE_READ|FMODE_WRITE;
	rcu_read_lock();
	delegation = rcu_dereference(nfsi->delegation);
	if (!delegation)
		goto out;
	spin_lock(&delegation->lock);
	ret = nfs4_is_valid_delegation(delegation, flags);
	if (ret) {
		nfs4_stateid_copy(dst, &delegation->stateid);
		nfs_mark_delegation_referenced(delegation);
		if (cred)
			*cred = get_cred(delegation->cred);
	}
	spin_unlock(&delegation->lock);
out:
	rcu_read_unlock();
	return ret;
}

/**
 * nfs4_delegation_flush_on_close - Check if we must flush file on close
 * @inode: inode to check
 *
 * This function checks the number of outstanding writes to the file
 * against the delegation 'space_limit' field to see if
 * the spec requires us to flush the file on close.
 */
bool nfs4_delegation_flush_on_close(const struct inode *inode)
{
	struct nfs_inode *nfsi = NFS_I(inode);
	struct nfs_delegation *delegation;
	bool ret = true;

	rcu_read_lock();
	delegation = rcu_dereference(nfsi->delegation);
	if (delegation == NULL || !(delegation->type & FMODE_WRITE))
		goto out;
	if (atomic_long_read(&nfsi->nrequests) < delegation->pagemod_limit)
		ret = false;
out:
	rcu_read_unlock();
	return ret;
}

module_param_named(delegation_watermark, nfs_delegation_watermark, uint, 0644);
