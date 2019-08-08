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

static void nfs_free_delegation(struct nfs_delegation *delegation)
{
	put_cred(delegation->cred);
	delegation->cred = NULL;
	kfree_rcu(delegation, rcu);
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
 * nfs_have_delegation - check if inode has a delegation, mark it
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
				  fmode_t type,
				  const nfs4_stateid *stateid,
				  unsigned long pagemod_limit)
{
	struct nfs_delegation *delegation;
	const struct cred *oldcred = NULL;

	rcu_read_lock();
	delegation = rcu_dereference(NFS_I(inode)->delegation);
	if (delegation != NULL) {
		spin_lock(&delegation->lock);
		if (delegation->inode != NULL) {
			nfs4_stateid_copy(&delegation->stateid, stateid);
			delegation->type = type;
			delegation->pagemod_limit = pagemod_limit;
			oldcred = delegation->cred;
			delegation->cred = get_cred(cred);
			clear_bit(NFS_DELEGATION_NEED_RECLAIM,
				  &delegation->flags);
			spin_unlock(&delegation->lock);
			rcu_read_unlock();
			put_cred(oldcred);
			trace_nfs4_reclaim_delegation(inode, type);
			return;
		}
		/* We appear to have raced with a delegation return. */
		spin_unlock(&delegation->lock);
	}
	rcu_read_unlock();
	nfs_inode_set_delegation(inode, cred, type, stateid, pagemod_limit);
}

static int nfs_do_return_delegation(struct inode *inode, struct nfs_delegation *delegation, int issync)
{
	int res = 0;

	if (!test_bit(NFS_DELEGATION_REVOKED, &delegation->flags))
		res = nfs4_proc_delegreturn(inode,
				delegation->cred,
				&delegation->stateid,
				issync);
	nfs_free_delegation(delegation);
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
	if (!test_and_set_bit(NFS_DELEGATION_RETURNING, &delegation->flags))
		ret = delegation;
	spin_unlock(&delegation->lock);
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

static void
nfs_abort_delegation_return(struct nfs_delegation *delegation,
		struct nfs_client *clp)
{

	spin_lock(&delegation->lock);
	clear_bit(NFS_DELEGATION_RETURNING, &delegation->flags);
	set_bit(NFS_DELEGATION_RETURN, &delegation->flags);
	spin_unlock(&delegation->lock);
	set_bit(NFS4CLNT_DELEGRETURN, &clp->cl_state);
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
	set_bit(NFS_DELEGATION_RETURNING, &delegation->flags);
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

	delegation = nfs_start_delegation_return(nfsi);
	if (delegation == NULL)
		return NULL;
	return nfs_detach_delegation(nfsi, delegation, server);
}

static void
nfs_update_inplace_delegation(struct nfs_delegation *delegation,
		const struct nfs_delegation *update)
{
	if (nfs4_stateid_is_newer(&update->stateid, &delegation->stateid)) {
		delegation->stateid.seqid = update->stateid.seqid;
		smp_wmb();
		delegation->type = update->type;
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
	if (old_delegation != NULL) {
		/* Is this an update of the existing delegation? */
		if (nfs4_stateid_match_other(&old_delegation->stateid,
					&delegation->stateid)) {
			nfs_update_inplace_delegation(old_delegation,
					delegation);
			goto out;
		}
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
		freeme = nfs_detach_delegation_locked(nfsi,
				old_delegation, clp);
		if (freeme == NULL)
			goto out;
	}
	list_add_tail_rcu(&delegation->super_list, &server->delegations);
	rcu_assign_pointer(nfsi->delegation, delegation);
	delegation = NULL;

	trace_nfs4_set_delegation(inode, type);

	spin_lock(&inode->i_lock);
	if (NFS_I(inode)->cache_validity & (NFS_INO_INVALID_ATTR|NFS_INO_INVALID_ATIME))
		NFS_I(inode)->cache_validity |= NFS_INO_REVAL_FORCED;
	spin_unlock(&inode->i_lock);
out:
	spin_unlock(&clp->cl_lock);
	if (delegation != NULL)
		nfs_free_delegation(delegation);
	if (freeme != NULL)
		nfs_do_return_delegation(inode, freeme, 0);
	return status;
}

/*
 * Basic procedure for returning a delegation to the server
 */
static int nfs_end_delegation_return(struct inode *inode, struct nfs_delegation *delegation, int issync)
{
	struct nfs_client *clp = NFS_SERVER(inode)->nfs_client;
	struct nfs_inode *nfsi = NFS_I(inode);
	int err = 0;

	if (delegation == NULL)
		return 0;
	do {
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
	} while (err == 0);

	if (err) {
		nfs_abort_delegation_return(delegation, clp);
		goto out;
	}
	if (!nfs_detach_delegation(nfsi, delegation, NFS_SERVER(inode)))
		goto out;

	err = nfs_do_return_delegation(inode, delegation, issync);
out:
	return err;
}

static bool nfs_delegation_need_return(struct nfs_delegation *delegation)
{
	bool ret = false;

	if (test_bit(NFS_DELEGATION_RETURNING, &delegation->flags))
		goto out;
	if (test_and_clear_bit(NFS_DELEGATION_RETURN, &delegation->flags))
		ret = true;
	if (test_and_clear_bit(NFS_DELEGATION_RETURN_IF_CLOSED, &delegation->flags) && !ret) {
		struct inode *inode;

		spin_lock(&delegation->lock);
		inode = delegation->inode;
		if (inode && list_empty(&NFS_I(inode)->open_files))
			ret = true;
		spin_unlock(&delegation->lock);
	}
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
	struct nfs_delegation *delegation;
	struct nfs_delegation *prev;
	struct nfs_server *server;
	struct inode *inode;
	struct inode *place_holder = NULL;
	struct nfs_delegation *place_holder_deleg = NULL;
	int err = 0;

restart:
	/*
	 * To avoid quadratic looping we hold a reference
	 * to an inode place_holder.  Each time we restart, we
	 * list nfs_servers from the server of that inode, and
	 * delegation in the server from the delegations of that
	 * inode.
	 * prev is an RCU-protected pointer to a delegation which
	 * wasn't marked for return and might be a good choice for
	 * the next place_holder.
	 */
	rcu_read_lock();
	prev = NULL;
	if (place_holder)
		server = NFS_SERVER(place_holder);
	else
		server = list_entry_rcu(clp->cl_superblocks.next,
					struct nfs_server, client_link);
	list_for_each_entry_from_rcu(server, &clp->cl_superblocks, client_link) {
		delegation = NULL;
		if (place_holder && server == NFS_SERVER(place_holder))
			delegation = rcu_dereference(NFS_I(place_holder)->delegation);
		if (!delegation || delegation != place_holder_deleg)
			delegation = list_entry_rcu(server->delegations.next,
						    struct nfs_delegation, super_list);
		list_for_each_entry_from_rcu(delegation, &server->delegations, super_list) {
			struct inode *to_put = NULL;

			if (!nfs_delegation_need_return(delegation)) {
				prev = delegation;
				continue;
			}
			if (!nfs_sb_active(server->super))
				break; /* continue in outer loop */

			if (prev) {
				struct inode *tmp;

				tmp = nfs_delegation_grab_inode(prev);
				if (tmp) {
					to_put = place_holder;
					place_holder = tmp;
					place_holder_deleg = prev;
				}
			}

			inode = nfs_delegation_grab_inode(delegation);
			if (inode == NULL) {
				rcu_read_unlock();
				if (to_put)
					iput(to_put);
				nfs_sb_deactive(server->super);
				goto restart;
			}
			delegation = nfs_start_delegation_return_locked(NFS_I(inode));
			rcu_read_unlock();

			if (to_put)
				iput(to_put);

			err = nfs_end_delegation_return(inode, delegation, 0);
			iput(inode);
			nfs_sb_deactive(server->super);
			cond_resched();
			if (!err)
				goto restart;
			set_bit(NFS4CLNT_DELEGRETURN, &clp->cl_state);
			if (place_holder)
				iput(place_holder);
			return err;
		}
	}
	rcu_read_unlock();
	if (place_holder)
		iput(place_holder);
	return 0;
}

/**
 * nfs_inode_return_delegation_noreclaim - return delegation, don't reclaim opens
 * @inode: inode to process
 *
 * Does not protect against delegation reclaims, therefore really only safe
 * to be called from nfs4_clear_inode().
 */
void nfs_inode_return_delegation_noreclaim(struct inode *inode)
{
	struct nfs_delegation *delegation;

	delegation = nfs_inode_detach_delegation(inode);
	if (delegation != NULL)
		nfs_do_return_delegation(inode, delegation, 1);
}

/**
 * nfs_inode_return_delegation - synchronously return a delegation
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
	int err = 0;

	nfs_wb_all(inode);
	delegation = nfs_start_delegation_return(nfsi);
	if (delegation != NULL)
		err = nfs_end_delegation_return(inode, delegation, 1);
	return err;
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
	if (!nfs4_has_session(NFS_SERVER(inode)->nfs_client) ||
	    !nfs4_check_delegation(inode, FMODE_WRITE))
		return nfs4_inode_return_delegation(inode);
	return 0;
}

static void nfs_mark_return_if_closed_delegation(struct nfs_server *server,
		struct nfs_delegation *delegation)
{
	set_bit(NFS_DELEGATION_RETURN_IF_CLOSED, &delegation->flags);
	set_bit(NFS4CLNT_DELEGRETURN, &server->nfs_client->cl_state);
}

static void nfs_mark_return_delegation(struct nfs_server *server,
		struct nfs_delegation *delegation)
{
	set_bit(NFS_DELEGATION_RETURN, &delegation->flags);
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
 * nfs_super_return_all_delegations - return delegations for one superblock
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

static void nfs_mark_delegation_revoked(struct nfs_server *server,
		struct nfs_delegation *delegation)
{
	set_bit(NFS_DELEGATION_REVOKED, &delegation->flags);
	delegation->stateid.type = NFS4_INVALID_STATEID_TYPE;
	nfs_mark_return_delegation(server, delegation);
}

static bool nfs_revoke_delegation(struct inode *inode,
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
	} else if (!nfs4_stateid_match(stateid, &delegation->stateid))
		goto out;
	nfs_mark_delegation_revoked(NFS_SERVER(inode), delegation);
	ret = true;
out:
	rcu_read_unlock();
	if (ret)
		nfs_inode_find_state_and_recover(inode, stateid);
	return ret;
}

void nfs_remove_bad_delegation(struct inode *inode,
		const nfs4_stateid *stateid)
{
	struct nfs_delegation *delegation;

	if (!nfs_revoke_delegation(inode, stateid))
		return;
	delegation = nfs_inode_detach_delegation(inode);
	if (delegation)
		nfs_free_delegation(delegation);
}
EXPORT_SYMBOL_GPL(nfs_remove_bad_delegation);

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
	delegation = rcu_dereference(NFS_I(inode)->delegation);
	if (delegation == NULL)
		goto out_enoent;
	if (stateid != NULL &&
	    !clp->cl_mvops->match_stateid(&delegation->stateid, stateid))
		goto out_enoent;
	nfs_mark_return_delegation(server, delegation);
	rcu_read_unlock();

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
	struct inode *freeme, *res = NULL;

	list_for_each_entry_rcu(delegation, &server->delegations, super_list) {
		spin_lock(&delegation->lock);
		if (delegation->inode != NULL &&
		    nfs_compare_fh(fhandle, &NFS_I(delegation->inode)->fh) == 0) {
			freeme = igrab(delegation->inode);
			if (freeme && nfs_sb_active(freeme->i_sb))
				res = freeme;
			spin_unlock(&delegation->lock);
			if (res != NULL)
				return res;
			if (freeme) {
				rcu_read_unlock();
				iput(freeme);
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

/**
 * nfs_delegation_reap_unclaimed - reap unclaimed delegations after reboot recovery is done
 * @clp: nfs_client to process
 *
 */
void nfs_delegation_reap_unclaimed(struct nfs_client *clp)
{
	struct nfs_delegation *delegation;
	struct nfs_server *server;
	struct inode *inode;

restart:
	rcu_read_lock();
	list_for_each_entry_rcu(server, &clp->cl_superblocks, client_link) {
		list_for_each_entry_rcu(delegation, &server->delegations,
								super_list) {
			if (test_bit(NFS_DELEGATION_INODE_FREEING,
						&delegation->flags) ||
			    test_bit(NFS_DELEGATION_RETURNING,
						&delegation->flags) ||
			    test_bit(NFS_DELEGATION_NEED_RECLAIM,
						&delegation->flags) == 0)
				continue;
			if (!nfs_sb_active(server->super))
				break; /* continue in outer loop */
			inode = nfs_delegation_grab_inode(delegation);
			if (inode == NULL) {
				rcu_read_unlock();
				nfs_sb_deactive(server->super);
				goto restart;
			}
			delegation = nfs_start_delegation_return_locked(NFS_I(inode));
			rcu_read_unlock();
			if (delegation != NULL) {
				delegation = nfs_detach_delegation(NFS_I(inode),
					delegation, server);
				if (delegation != NULL)
					nfs_free_delegation(delegation);
			}
			iput(inode);
			nfs_sb_deactive(server->super);
			cond_resched();
			goto restart;
		}
	}
	rcu_read_unlock();
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
	struct nfs_delegation *delegation;
	struct nfs_server *server;
	struct inode *inode;
	const struct cred *cred;
	nfs4_stateid stateid;

restart:
	rcu_read_lock();
	list_for_each_entry_rcu(server, &clp->cl_superblocks, client_link) {
		list_for_each_entry_rcu(delegation, &server->delegations,
								super_list) {
			if (test_bit(NFS_DELEGATION_INODE_FREEING,
						&delegation->flags) ||
			    test_bit(NFS_DELEGATION_RETURNING,
						&delegation->flags) ||
			    test_bit(NFS_DELEGATION_TEST_EXPIRED,
						&delegation->flags) == 0)
				continue;
			if (!nfs_sb_active(server->super))
				break; /* continue in outer loop */
			inode = nfs_delegation_grab_inode(delegation);
			if (inode == NULL) {
				rcu_read_unlock();
				nfs_sb_deactive(server->super);
				goto restart;
			}
			cred = get_cred_rcu(delegation->cred);
			nfs4_stateid_copy(&stateid, &delegation->stateid);
			clear_bit(NFS_DELEGATION_TEST_EXPIRED, &delegation->flags);
			rcu_read_unlock();
			nfs_delegation_test_free_expired(inode, &stateid, cred);
			put_cred(cred);
			if (nfs4_server_rebooted(clp)) {
				nfs_inode_mark_test_expired_delegation(server,inode);
				iput(inode);
				nfs_sb_deactive(server->super);
				return;
			}
			iput(inode);
			nfs_sb_deactive(server->super);
			cond_resched();
			goto restart;
		}
	}
	rcu_read_unlock();
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
	    nfs4_stateid_match_other(&delegation->stateid, stateid)) {
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
	    nfs4_stateid_match_other(dst, &delegation->stateid)) {
		dst->seqid = delegation->stateid.seqid;
		return ret;
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
	bool ret;

	flags &= FMODE_READ|FMODE_WRITE;
	rcu_read_lock();
	delegation = rcu_dereference(nfsi->delegation);
	ret = nfs4_is_valid_delegation(delegation, flags);
	if (ret) {
		nfs4_stateid_copy(dst, &delegation->stateid);
		nfs_mark_delegation_referenced(delegation);
		if (cred)
			*cred = get_cred(delegation->cred);
	}
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
