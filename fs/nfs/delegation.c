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
#include <linux/spinlock.h>

#include <linux/nfs4.h>
#include <linux/nfs_fs.h>
#include <linux/nfs_xdr.h>

#include "nfs4_fs.h"
#include "delegation.h"
#include "internal.h"

static void nfs_do_free_delegation(struct nfs_delegation *delegation)
{
	kfree(delegation);
}

static void nfs_free_delegation_callback(struct rcu_head *head)
{
	struct nfs_delegation *delegation = container_of(head, struct nfs_delegation, rcu);

	nfs_do_free_delegation(delegation);
}

static void nfs_free_delegation(struct nfs_delegation *delegation)
{
	struct rpc_cred *cred;

	cred = rcu_dereference(delegation->cred);
	rcu_assign_pointer(delegation->cred, NULL);
	call_rcu(&delegation->rcu, nfs_free_delegation_callback);
	if (cred)
		put_rpccred(cred);
}

void nfs_mark_delegation_referenced(struct nfs_delegation *delegation)
{
	set_bit(NFS_DELEGATION_REFERENCED, &delegation->flags);
}

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
	int status;

	for (fl = inode->i_flock; fl != NULL; fl = fl->fl_next) {
		if (!(fl->fl_flags & (FL_POSIX|FL_FLOCK)))
			continue;
		if (nfs_file_open_context(fl->fl_file) != ctx)
			continue;
		status = nfs4_lock_delegation_recall(state, fl);
		if (status >= 0)
			continue;
		switch (status) {
			default:
				printk(KERN_ERR "%s: unhandled error %d.\n",
						__func__, status);
			case -NFS4ERR_EXPIRED:
				/* kill_proc(fl->fl_pid, SIGLOST, 1); */
			case -NFS4ERR_STALE_CLIENTID:
				nfs4_schedule_state_recovery(NFS_SERVER(inode)->nfs_client);
				goto out_err;
		}
	}
	return 0;
out_err:
	return status;
}

static void nfs_delegation_claim_opens(struct inode *inode, const nfs4_stateid *stateid)
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
		if (memcmp(state->stateid.data, stateid->data, sizeof(state->stateid.data)) != 0)
			continue;
		get_nfs_open_context(ctx);
		spin_unlock(&inode->i_lock);
		err = nfs4_open_delegation_recall(ctx, state, stateid);
		if (err >= 0)
			err = nfs_delegation_claim_locks(ctx, state);
		put_nfs_open_context(ctx);
		if (err != 0)
			return;
		goto again;
	}
	spin_unlock(&inode->i_lock);
}

/*
 * Set up a delegation on an inode
 */
void nfs_inode_reclaim_delegation(struct inode *inode, struct rpc_cred *cred, struct nfs_openres *res)
{
	struct nfs_delegation *delegation = NFS_I(inode)->delegation;
	struct rpc_cred *oldcred;

	if (delegation == NULL)
		return;
	memcpy(delegation->stateid.data, res->delegation.data,
			sizeof(delegation->stateid.data));
	delegation->type = res->delegation_type;
	delegation->maxsize = res->maxsize;
	oldcred = delegation->cred;
	delegation->cred = get_rpccred(cred);
	clear_bit(NFS_DELEGATION_NEED_RECLAIM, &delegation->flags);
	NFS_I(inode)->delegation_state = delegation->type;
	smp_wmb();
	put_rpccred(oldcred);
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

static struct nfs_delegation *nfs_detach_delegation_locked(struct nfs_inode *nfsi, const nfs4_stateid *stateid)
{
	struct nfs_delegation *delegation = rcu_dereference(nfsi->delegation);

	if (delegation == NULL)
		goto nomatch;
	spin_lock(&delegation->lock);
	if (stateid != NULL && memcmp(delegation->stateid.data, stateid->data,
				sizeof(delegation->stateid.data)) != 0)
		goto nomatch_unlock;
	list_del_rcu(&delegation->super_list);
	delegation->inode = NULL;
	nfsi->delegation_state = 0;
	rcu_assign_pointer(nfsi->delegation, NULL);
	spin_unlock(&delegation->lock);
	return delegation;
nomatch_unlock:
	spin_unlock(&delegation->lock);
nomatch:
	return NULL;
}

/*
 * Set up a delegation on an inode
 */
int nfs_inode_set_delegation(struct inode *inode, struct rpc_cred *cred, struct nfs_openres *res)
{
	struct nfs_client *clp = NFS_SERVER(inode)->nfs_client;
	struct nfs_inode *nfsi = NFS_I(inode);
	struct nfs_delegation *delegation;
	struct nfs_delegation *freeme = NULL;
	int status = 0;

	delegation = kmalloc(sizeof(*delegation), GFP_KERNEL);
	if (delegation == NULL)
		return -ENOMEM;
	memcpy(delegation->stateid.data, res->delegation.data,
			sizeof(delegation->stateid.data));
	delegation->type = res->delegation_type;
	delegation->maxsize = res->maxsize;
	delegation->change_attr = nfsi->change_attr;
	delegation->cred = get_rpccred(cred);
	delegation->inode = inode;
	delegation->flags = 1<<NFS_DELEGATION_REFERENCED;
	spin_lock_init(&delegation->lock);

	spin_lock(&clp->cl_lock);
	if (rcu_dereference(nfsi->delegation) != NULL) {
		if (memcmp(&delegation->stateid, &nfsi->delegation->stateid,
					sizeof(delegation->stateid)) == 0 &&
				delegation->type == nfsi->delegation->type) {
			goto out;
		}
		/*
		 * Deal with broken servers that hand out two
		 * delegations for the same file.
		 */
		dfprintk(FILE, "%s: server %s handed out "
				"a duplicate delegation!\n",
				__func__, clp->cl_hostname);
		if (delegation->type <= nfsi->delegation->type) {
			freeme = delegation;
			delegation = NULL;
			goto out;
		}
		freeme = nfs_detach_delegation_locked(nfsi, NULL);
	}
	list_add_rcu(&delegation->super_list, &clp->cl_delegations);
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

/* Sync all data to disk upon delegation return */
static void nfs_msync_inode(struct inode *inode)
{
	filemap_fdatawrite(inode->i_mapping);
	nfs_wb_all(inode);
	filemap_fdatawait(inode->i_mapping);
}

/*
 * Basic procedure for returning a delegation to the server
 */
static int __nfs_inode_return_delegation(struct inode *inode, struct nfs_delegation *delegation)
{
	struct nfs_inode *nfsi = NFS_I(inode);

	nfs_msync_inode(inode);
	/* Guard against new delegated open calls */
	down_write(&nfsi->rwsem);
	nfs_delegation_claim_opens(inode, &delegation->stateid);
	up_write(&nfsi->rwsem);
	nfs_msync_inode(inode);

	return nfs_do_return_delegation(inode, delegation, 1);
}

/*
 * Return all delegations that have been marked for return
 */
void nfs_client_return_marked_delegations(struct nfs_client *clp)
{
	struct nfs_delegation *delegation;
	struct inode *inode;

restart:
	rcu_read_lock();
	list_for_each_entry_rcu(delegation, &clp->cl_delegations, super_list) {
		if (!test_and_clear_bit(NFS_DELEGATION_RETURN, &delegation->flags))
			continue;
		inode = nfs_delegation_grab_inode(delegation);
		if (inode == NULL)
			continue;
		spin_lock(&clp->cl_lock);
		delegation = nfs_detach_delegation_locked(NFS_I(inode), NULL);
		spin_unlock(&clp->cl_lock);
		rcu_read_unlock();
		if (delegation != NULL)
			__nfs_inode_return_delegation(inode, delegation);
		iput(inode);
		goto restart;
	}
	rcu_read_unlock();
}

/*
 * This function returns the delegation without reclaiming opens
 * or protecting against delegation reclaims.
 * It is therefore really only safe to be called from
 * nfs4_clear_inode()
 */
void nfs_inode_return_delegation_noreclaim(struct inode *inode)
{
	struct nfs_client *clp = NFS_SERVER(inode)->nfs_client;
	struct nfs_inode *nfsi = NFS_I(inode);
	struct nfs_delegation *delegation;

	if (rcu_dereference(nfsi->delegation) != NULL) {
		spin_lock(&clp->cl_lock);
		delegation = nfs_detach_delegation_locked(nfsi, NULL);
		spin_unlock(&clp->cl_lock);
		if (delegation != NULL)
			nfs_do_return_delegation(inode, delegation, 0);
	}
}

int nfs_inode_return_delegation(struct inode *inode)
{
	struct nfs_client *clp = NFS_SERVER(inode)->nfs_client;
	struct nfs_inode *nfsi = NFS_I(inode);
	struct nfs_delegation *delegation;
	int err = 0;

	if (rcu_dereference(nfsi->delegation) != NULL) {
		spin_lock(&clp->cl_lock);
		delegation = nfs_detach_delegation_locked(nfsi, NULL);
		spin_unlock(&clp->cl_lock);
		if (delegation != NULL)
			err = __nfs_inode_return_delegation(inode, delegation);
	}
	return err;
}

static void nfs_mark_return_delegation(struct nfs_client *clp, struct nfs_delegation *delegation)
{
	set_bit(NFS_DELEGATION_RETURN, &delegation->flags);
	set_bit(NFS4CLNT_DELEGRETURN, &clp->cl_state);
}

/*
 * Return all delegations associated to a super block
 */
void nfs_super_return_all_delegations(struct super_block *sb)
{
	struct nfs_client *clp = NFS_SB(sb)->nfs_client;
	struct nfs_delegation *delegation;

	if (clp == NULL)
		return;
	rcu_read_lock();
	list_for_each_entry_rcu(delegation, &clp->cl_delegations, super_list) {
		spin_lock(&delegation->lock);
		if (delegation->inode != NULL && delegation->inode->i_sb == sb)
			set_bit(NFS_DELEGATION_RETURN, &delegation->flags);
		spin_unlock(&delegation->lock);
	}
	rcu_read_unlock();
	nfs_client_return_marked_delegations(clp);
}

static void nfs_client_mark_return_all_delegations(struct nfs_client *clp)
{
	struct nfs_delegation *delegation;

	rcu_read_lock();
	list_for_each_entry_rcu(delegation, &clp->cl_delegations, super_list) {
		set_bit(NFS_DELEGATION_RETURN, &delegation->flags);
		set_bit(NFS4CLNT_DELEGRETURN, &clp->cl_state);
	}
	rcu_read_unlock();
}

static void nfs_delegation_run_state_manager(struct nfs_client *clp)
{
	if (test_bit(NFS4CLNT_DELEGRETURN, &clp->cl_state))
		nfs4_schedule_state_manager(clp);
}

void nfs_expire_all_delegations(struct nfs_client *clp)
{
	nfs_client_mark_return_all_delegations(clp);
	nfs_delegation_run_state_manager(clp);
}

/*
 * Return all delegations following an NFS4ERR_CB_PATH_DOWN error.
 */
void nfs_handle_cb_pathdown(struct nfs_client *clp)
{
	if (clp == NULL)
		return;
	nfs_client_mark_return_all_delegations(clp);
}

static void nfs_client_mark_return_unreferenced_delegations(struct nfs_client *clp)
{
	struct nfs_delegation *delegation;

	rcu_read_lock();
	list_for_each_entry_rcu(delegation, &clp->cl_delegations, super_list) {
		if (test_and_clear_bit(NFS_DELEGATION_REFERENCED, &delegation->flags))
			continue;
		set_bit(NFS_DELEGATION_RETURN, &delegation->flags);
		set_bit(NFS4CLNT_DELEGRETURN, &clp->cl_state);
	}
	rcu_read_unlock();
}

void nfs_expire_unreferenced_delegations(struct nfs_client *clp)
{
	nfs_client_mark_return_unreferenced_delegations(clp);
	nfs_delegation_run_state_manager(clp);
}

/*
 * Asynchronous delegation recall!
 */
int nfs_async_inode_return_delegation(struct inode *inode, const nfs4_stateid *stateid)
{
	struct nfs_client *clp = NFS_SERVER(inode)->nfs_client;
	struct nfs_delegation *delegation;

	rcu_read_lock();
	delegation = rcu_dereference(NFS_I(inode)->delegation);
	if (delegation == NULL || memcmp(delegation->stateid.data, stateid->data,
				sizeof(delegation->stateid.data)) != 0) {
		rcu_read_unlock();
		return -ENOENT;
	}
	nfs_mark_return_delegation(clp, delegation);
	rcu_read_unlock();
	nfs_delegation_run_state_manager(clp);
	return 0;
}

/*
 * Retrieve the inode associated with a delegation
 */
struct inode *nfs_delegation_find_inode(struct nfs_client *clp, const struct nfs_fh *fhandle)
{
	struct nfs_delegation *delegation;
	struct inode *res = NULL;
	rcu_read_lock();
	list_for_each_entry_rcu(delegation, &clp->cl_delegations, super_list) {
		spin_lock(&delegation->lock);
		if (delegation->inode != NULL &&
		    nfs_compare_fh(fhandle, &NFS_I(delegation->inode)->fh) == 0) {
			res = igrab(delegation->inode);
		}
		spin_unlock(&delegation->lock);
		if (res != NULL)
			break;
	}
	rcu_read_unlock();
	return res;
}

/*
 * Mark all delegations as needing to be reclaimed
 */
void nfs_delegation_mark_reclaim(struct nfs_client *clp)
{
	struct nfs_delegation *delegation;
	rcu_read_lock();
	list_for_each_entry_rcu(delegation, &clp->cl_delegations, super_list)
		set_bit(NFS_DELEGATION_NEED_RECLAIM, &delegation->flags);
	rcu_read_unlock();
}

/*
 * Reap all unclaimed delegations after reboot recovery is done
 */
void nfs_delegation_reap_unclaimed(struct nfs_client *clp)
{
	struct nfs_delegation *delegation;
	struct inode *inode;
restart:
	rcu_read_lock();
	list_for_each_entry_rcu(delegation, &clp->cl_delegations, super_list) {
		if (test_bit(NFS_DELEGATION_NEED_RECLAIM, &delegation->flags) == 0)
			continue;
		inode = nfs_delegation_grab_inode(delegation);
		if (inode == NULL)
			continue;
		spin_lock(&clp->cl_lock);
		delegation = nfs_detach_delegation_locked(NFS_I(inode), NULL);
		spin_unlock(&clp->cl_lock);
		rcu_read_unlock();
		if (delegation != NULL)
			nfs_free_delegation(delegation);
		iput(inode);
		goto restart;
	}
	rcu_read_unlock();
}

int nfs4_copy_delegation_stateid(nfs4_stateid *dst, struct inode *inode)
{
	struct nfs_inode *nfsi = NFS_I(inode);
	struct nfs_delegation *delegation;
	int ret = 0;

	rcu_read_lock();
	delegation = rcu_dereference(nfsi->delegation);
	if (delegation != NULL) {
		memcpy(dst->data, delegation->stateid.data, sizeof(dst->data));
		ret = 1;
	}
	rcu_read_unlock();
	return ret;
}
