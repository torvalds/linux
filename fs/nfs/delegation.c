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

#include <linux/nfs4.h>
#include <linux/nfs_fs.h>
#include <linux/nfs_xdr.h>

#include "nfs4_fs.h"
#include "delegation.h"
#include "internal.h"

static void nfs_free_delegation(struct nfs_delegation *delegation)
{
	if (delegation->cred) {
		put_rpccred(delegation->cred);
		delegation->cred = NULL;
	}
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

/**
 * nfs_have_delegation - check if inode has a delegation
 * @inode: inode to check
 * @flags: delegation types to check for
 *
 * Returns one if inode has the indicated delegation, otherwise zero.
 */
int nfs_have_delegation(struct inode *inode, fmode_t flags)
{
	struct nfs_delegation *delegation;
	int ret = 0;

	flags &= FMODE_READ|FMODE_WRITE;
	rcu_read_lock();
	delegation = rcu_dereference(NFS_I(inode)->delegation);
	if (delegation != NULL && (delegation->type & flags) == flags) {
		nfs_mark_delegation_referenced(delegation);
		ret = 1;
	}
	rcu_read_unlock();
	return ret;
}

static int nfs_delegation_claim_locks(struct nfs_open_context *ctx, struct nfs4_state *state)
{
	struct inode *inode = state->inode;
	struct file_lock *fl;
	int status = 0;

	if (inode->i_flock == NULL)
		goto out;

	/* Protect inode->i_flock using the file locks lock */
	lock_flocks();
	for (fl = inode->i_flock; fl != NULL; fl = fl->fl_next) {
		if (!(fl->fl_flags & (FL_POSIX|FL_FLOCK)))
			continue;
		if (nfs_file_open_context(fl->fl_file) != ctx)
			continue;
		unlock_flocks();
		status = nfs4_lock_delegation_recall(state, fl);
		if (status < 0)
			goto out;
		lock_flocks();
	}
	unlock_flocks();
out:
	return status;
}

static int nfs_delegation_claim_opens(struct inode *inode, const nfs4_stateid *stateid)
{
	struct nfs_inode *nfsi = NFS_I(inode);
	struct nfs_open_context *ctx;
	struct nfs4_state *state;
	int err;

again:
	spin_lock(&inode->i_lock);
	list_for_each_entry(ctx, &nfsi->open_files, list) {
		state = ctx->state;
		if (state == NULL)
			continue;
		if (!test_bit(NFS_DELEGATED_STATE, &state->flags))
			continue;
		if (!nfs4_stateid_match(&state->stateid, stateid))
			continue;
		get_nfs_open_context(ctx);
		spin_unlock(&inode->i_lock);
		err = nfs4_open_delegation_recall(ctx, state, stateid);
		if (err >= 0)
			err = nfs_delegation_claim_locks(ctx, state);
		put_nfs_open_context(ctx);
		if (err != 0)
			return err;
		goto again;
	}
	spin_unlock(&inode->i_lock);
	return 0;
}

/**
 * nfs_inode_reclaim_delegation - process a delegation reclaim request
 * @inode: inode to process
 * @cred: credential to use for request
 * @res: new delegation state from server
 *
 */
void nfs_inode_reclaim_delegation(struct inode *inode, struct rpc_cred *cred,
				  struct nfs_openres *res)
{
	struct nfs_delegation *delegation;
	struct rpc_cred *oldcred = NULL;

	rcu_read_lock();
	delegation = rcu_dereference(NFS_I(inode)->delegation);
	if (delegation != NULL) {
		spin_lock(&delegation->lock);
		if (delegation->inode != NULL) {
			nfs4_stateid_copy(&delegation->stateid, &res->delegation);
			delegation->type = res->delegation_type;
			delegation->maxsize = res->maxsize;
			oldcred = delegation->cred;
			delegation->cred = get_rpccred(cred);
			clear_bit(NFS_DELEGATION_NEED_RECLAIM,
				  &delegation->flags);
			NFS_I(inode)->delegation_state = delegation->type;
			spin_unlock(&delegation->lock);
			put_rpccred(oldcred);
			rcu_read_unlock();
		} else {
			/* We appear to have raced with a delegation return. */
			spin_unlock(&delegation->lock);
			rcu_read_unlock();
			nfs_inode_set_delegation(inode, cred, res);
		}
	} else {
		rcu_read_unlock();
	}
}

static int nfs_do_return_delegation(struct inode *inode, struct nfs_delegation *delegation, int issync)
{
	int res = 0;

	res = nfs4_proc_delegreturn(inode, delegation->cred, &delegation->stateid, issync);
	nfs_free_delegation(delegation);
	return res;
}

static struct inode *nfs_delegation_grab_inode(struct nfs_delegation *delegation)
{
	struct inode *inode = NULL;

	spin_lock(&delegation->lock);
	if (delegation->inode != NULL)
		inode = igrab(delegation->inode);
	spin_unlock(&delegation->lock);
	return inode;
}

static struct nfs_delegation *
nfs_detach_delegation_locked(struct nfs_inode *nfsi,
			     struct nfs_server *server)
{
	struct nfs_delegation *delegation =
		rcu_dereference_protected(nfsi->delegation,
				lockdep_is_held(&server->nfs_client->cl_lock));

	if (delegation == NULL)
		goto nomatch;

	spin_lock(&delegation->lock);
	list_del_rcu(&delegation->super_list);
	delegation->inode = NULL;
	nfsi->delegation_state = 0;
	rcu_assign_pointer(nfsi->delegation, NULL);
	spin_unlock(&delegation->lock);
	return delegation;
nomatch:
	return NULL;
}

static struct nfs_delegation *nfs_detach_delegation(struct nfs_inode *nfsi,
						    struct nfs_server *server)
{
	struct nfs_client *clp = server->nfs_client;
	struct nfs_delegation *delegation;

	spin_lock(&clp->cl_lock);
	delegation = nfs_detach_delegation_locked(nfsi, server);
	spin_unlock(&clp->cl_lock);
	return delegation;
}

/**
 * nfs_inode_set_delegation - set up a delegation on an inode
 * @inode: inode to which delegation applies
 * @cred: cred to use for subsequent delegation processing
 * @res: new delegation state from server
 *
 * Returns zero on success, or a negative errno value.
 */
int nfs_inode_set_delegation(struct inode *inode, struct rpc_cred *cred, struct nfs_openres *res)
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
	nfs4_stateid_copy(&delegation->stateid, &res->delegation);
	delegation->type = res->delegation_type;
	delegation->maxsize = res->maxsize;
	delegation->change_attr = inode->i_version;
	delegation->cred = get_rpccred(cred);
	delegation->inode = inode;
	delegation->flags = 1<<NFS_DELEGATION_REFERENCED;
	spin_lock_init(&delegation->lock);

	spin_lock(&clp->cl_lock);
	old_delegation = rcu_dereference_protected(nfsi->delegation,
					lockdep_is_held(&clp->cl_lock));
	if (old_delegation != NULL) {
		if (nfs4_stateid_match(&delegation->stateid,
					&old_delegation->stateid) &&
				delegation->type == old_delegation->type) {
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
		freeme = nfs_detach_delegation_locked(nfsi, server);
	}
	list_add_rcu(&delegation->super_list, &server->delegations);
	nfsi->delegation_state = delegation->type;
	rcu_assign_pointer(nfsi->delegation, delegation);
	delegation = NULL;

	/* Ensure we revalidate the attributes and page cache! */
	spin_lock(&inode->i_lock);
	nfsi->cache_validity |= NFS_INO_REVAL_FORCED;
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
static int __nfs_inode_return_delegation(struct inode *inode, struct nfs_delegation *delegation, int issync)
{
	struct nfs_inode *nfsi = NFS_I(inode);
	int err;

	/*
	 * Guard against new delegated open/lock/unlock calls and against
	 * state recovery
	 */
	down_write(&nfsi->rwsem);
	err = nfs_delegation_claim_opens(inode, &delegation->stateid);
	up_write(&nfsi->rwsem);
	if (err)
		goto out;

	err = nfs_do_return_delegation(inode, delegation, issync);
out:
	return err;
}

/**
 * nfs_client_return_marked_delegations - return previously marked delegations
 * @clp: nfs_client to process
 *
 * Returns zero on success, or a negative errno value.
 */
int nfs_client_return_marked_delegations(struct nfs_client *clp)
{
	struct nfs_delegation *delegation;
	struct nfs_server *server;
	struct inode *inode;
	int err = 0;

restart:
	rcu_read_lock();
	list_for_each_entry_rcu(server, &clp->cl_superblocks, client_link) {
		list_for_each_entry_rcu(delegation, &server->delegations,
								super_list) {
			if (!test_and_clear_bit(NFS_DELEGATION_RETURN,
							&delegation->flags))
				continue;
			inode = nfs_delegation_grab_inode(delegation);
			if (inode == NULL)
				continue;
			delegation = nfs_detach_delegation(NFS_I(inode),
								server);
			rcu_read_unlock();

			if (delegation != NULL) {
				filemap_flush(inode->i_mapping);
				err = __nfs_inode_return_delegation(inode,
								delegation, 0);
			}
			iput(inode);
			if (!err)
				goto restart;
			set_bit(NFS4CLNT_DELEGRETURN, &clp->cl_state);
			return err;
		}
	}
	rcu_read_unlock();
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
	struct nfs_server *server = NFS_SERVER(inode);
	struct nfs_inode *nfsi = NFS_I(inode);
	struct nfs_delegation *delegation;

	if (rcu_access_pointer(nfsi->delegation) != NULL) {
		delegation = nfs_detach_delegation(nfsi, server);
		if (delegation != NULL)
			nfs_do_return_delegation(inode, delegation, 0);
	}
}

/**
 * nfs_inode_return_delegation - synchronously return a delegation
 * @inode: inode to process
 *
 * Returns zero on success, or a negative errno value.
 */
int nfs_inode_return_delegation(struct inode *inode)
{
	struct nfs_server *server = NFS_SERVER(inode);
	struct nfs_inode *nfsi = NFS_I(inode);
	struct nfs_delegation *delegation;
	int err = 0;

	if (rcu_access_pointer(nfsi->delegation) != NULL) {
		delegation = nfs_detach_delegation(nfsi, server);
		if (delegation != NULL) {
			nfs_wb_all(inode);
			err = __nfs_inode_return_delegation(inode, delegation, 1);
		}
	}
	return err;
}

static void nfs_mark_return_delegation(struct nfs_server *server,
		struct nfs_delegation *delegation)
{
	set_bit(NFS_DELEGATION_RETURN, &delegation->flags);
	set_bit(NFS4CLNT_DELEGRETURN, &server->nfs_client->cl_state);
}

/**
 * nfs_super_return_all_delegations - return delegations for one superblock
 * @sb: sb to process
 *
 */
void nfs_super_return_all_delegations(struct super_block *sb)
{
	struct nfs_server *server = NFS_SB(sb);
	struct nfs_client *clp = server->nfs_client;
	struct nfs_delegation *delegation;

	if (clp == NULL)
		return;

	rcu_read_lock();
	list_for_each_entry_rcu(delegation, &server->delegations, super_list) {
		spin_lock(&delegation->lock);
		set_bit(NFS_DELEGATION_RETURN, &delegation->flags);
		spin_unlock(&delegation->lock);
	}
	rcu_read_unlock();

	if (nfs_client_return_marked_delegations(clp) != 0)
		nfs4_schedule_state_manager(clp);
}

static void nfs_mark_return_all_delegation_types(struct nfs_server *server,
						 fmode_t flags)
{
	struct nfs_delegation *delegation;

	list_for_each_entry_rcu(delegation, &server->delegations, super_list) {
		if ((delegation->type == (FMODE_READ|FMODE_WRITE)) && !(flags & FMODE_WRITE))
			continue;
		if (delegation->type & flags)
			nfs_mark_return_delegation(server, delegation);
	}
}

static void nfs_client_mark_return_all_delegation_types(struct nfs_client *clp,
							fmode_t flags)
{
	struct nfs_server *server;

	rcu_read_lock();
	list_for_each_entry_rcu(server, &clp->cl_superblocks, client_link)
		nfs_mark_return_all_delegation_types(server, flags);
	rcu_read_unlock();
}

static void nfs_delegation_run_state_manager(struct nfs_client *clp)
{
	if (test_bit(NFS4CLNT_DELEGRETURN, &clp->cl_state))
		nfs4_schedule_state_manager(clp);
}

void nfs_remove_bad_delegation(struct inode *inode)
{
	struct nfs_delegation *delegation;

	delegation = nfs_detach_delegation(NFS_I(inode), NFS_SERVER(inode));
	if (delegation) {
		nfs_inode_find_state_and_recover(inode, &delegation->stateid);
		nfs_free_delegation(delegation);
	}
}
EXPORT_SYMBOL_GPL(nfs_remove_bad_delegation);

/**
 * nfs_expire_all_delegation_types
 * @clp: client to process
 * @flags: delegation types to expire
 *
 */
void nfs_expire_all_delegation_types(struct nfs_client *clp, fmode_t flags)
{
	nfs_client_mark_return_all_delegation_types(clp, flags);
	nfs_delegation_run_state_manager(clp);
}

/**
 * nfs_expire_all_delegations
 * @clp: client to process
 *
 */
void nfs_expire_all_delegations(struct nfs_client *clp)
{
	nfs_expire_all_delegation_types(clp, FMODE_READ|FMODE_WRITE);
}

static void nfs_mark_return_unreferenced_delegations(struct nfs_server *server)
{
	struct nfs_delegation *delegation;

	list_for_each_entry_rcu(delegation, &server->delegations, super_list) {
		if (test_and_clear_bit(NFS_DELEGATION_REFERENCED, &delegation->flags))
			continue;
		nfs_mark_return_delegation(server, delegation);
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

	if (!clp->cl_mvops->match_stateid(&delegation->stateid, stateid))
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
	struct inode *res = NULL;

	list_for_each_entry_rcu(delegation, &server->delegations, super_list) {
		spin_lock(&delegation->lock);
		if (delegation->inode != NULL &&
		    nfs_compare_fh(fhandle, &NFS_I(delegation->inode)->fh) == 0) {
			res = igrab(delegation->inode);
		}
		spin_unlock(&delegation->lock);
		if (res != NULL)
			break;
	}
	return res;
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
	struct inode *res = NULL;

	rcu_read_lock();
	list_for_each_entry_rcu(server, &clp->cl_superblocks, client_link) {
		res = nfs_delegation_find_inode_server(server, fhandle);
		if (res != NULL)
			break;
	}
	rcu_read_unlock();
	return res;
}

static void nfs_delegation_mark_reclaim_server(struct nfs_server *server)
{
	struct nfs_delegation *delegation;

	list_for_each_entry_rcu(delegation, &server->delegations, super_list)
		set_bit(NFS_DELEGATION_NEED_RECLAIM, &delegation->flags);
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
			if (test_bit(NFS_DELEGATION_NEED_RECLAIM,
						&delegation->flags) == 0)
				continue;
			inode = nfs_delegation_grab_inode(delegation);
			if (inode == NULL)
				continue;
			delegation = nfs_detach_delegation(NFS_I(inode),
								server);
			rcu_read_unlock();

			if (delegation != NULL)
				nfs_free_delegation(delegation);
			iput(inode);
			goto restart;
		}
	}
	rcu_read_unlock();
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
 * nfs4_copy_delegation_stateid - Copy inode's state ID information
 * @dst: stateid data structure to fill in
 * @inode: inode to check
 * @flags: delegation type requirement
 *
 * Returns "true" and fills in "dst->data" * if inode had a delegation,
 * otherwise "false" is returned.
 */
bool nfs4_copy_delegation_stateid(nfs4_stateid *dst, struct inode *inode,
		fmode_t flags)
{
	struct nfs_inode *nfsi = NFS_I(inode);
	struct nfs_delegation *delegation;
	bool ret;

	flags &= FMODE_READ|FMODE_WRITE;
	rcu_read_lock();
	delegation = rcu_dereference(nfsi->delegation);
	ret = (delegation != NULL && (delegation->type & flags) == flags);
	if (ret) {
		nfs4_stateid_copy(dst, &delegation->stateid);
		nfs_mark_delegation_referenced(delegation);
	}
	rcu_read_unlock();
	return ret;
}
