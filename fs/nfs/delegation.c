/*
 * linux/fs/nfs/delegation.c
 *
 * Copyright (C) 2004 Trond Myklebust
 *
 * NFS file delegation management
 *
 */
#include <linux/config.h>
#include <linux/completion.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/spinlock.h>

#include <linux/nfs4.h>
#include <linux/nfs_fs.h>
#include <linux/nfs_xdr.h>

#include "nfs4_fs.h"
#include "delegation.h"

static struct nfs_delegation *nfs_alloc_delegation(void)
{
	return (struct nfs_delegation *)kmalloc(sizeof(struct nfs_delegation), GFP_KERNEL);
}

static void nfs_free_delegation(struct nfs_delegation *delegation)
{
	if (delegation->cred)
		put_rpccred(delegation->cred);
	kfree(delegation);
}

static int nfs_delegation_claim_locks(struct nfs_open_context *ctx, struct nfs4_state *state)
{
	struct inode *inode = state->inode;
	struct file_lock *fl;
	int status;

	for (fl = inode->i_flock; fl != 0; fl = fl->fl_next) {
		if (!(fl->fl_flags & (FL_POSIX|FL_FLOCK)))
			continue;
		if ((struct nfs_open_context *)fl->fl_file->private_data != ctx)
			continue;
		status = nfs4_lock_delegation_recall(state, fl);
		if (status >= 0)
			continue;
		switch (status) {
			default:
				printk(KERN_ERR "%s: unhandled error %d.\n",
						__FUNCTION__, status);
			case -NFS4ERR_EXPIRED:
				/* kill_proc(fl->fl_pid, SIGLOST, 1); */
			case -NFS4ERR_STALE_CLIENTID:
				nfs4_schedule_state_recovery(NFS_SERVER(inode)->nfs4_state);
				goto out_err;
		}
	}
	return 0;
out_err:
	return status;
}

static void nfs_delegation_claim_opens(struct inode *inode)
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
		get_nfs_open_context(ctx);
		spin_unlock(&inode->i_lock);
		err = nfs4_open_delegation_recall(ctx->dentry, state);
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

	if (delegation == NULL)
		return;
	memcpy(delegation->stateid.data, res->delegation.data,
			sizeof(delegation->stateid.data));
	delegation->type = res->delegation_type;
	delegation->maxsize = res->maxsize;
	put_rpccred(cred);
	delegation->cred = get_rpccred(cred);
	delegation->flags &= ~NFS_DELEGATION_NEED_RECLAIM;
	NFS_I(inode)->delegation_state = delegation->type;
	smp_wmb();
}

/*
 * Set up a delegation on an inode
 */
int nfs_inode_set_delegation(struct inode *inode, struct rpc_cred *cred, struct nfs_openres *res)
{
	struct nfs4_client *clp = NFS_SERVER(inode)->nfs4_state;
	struct nfs_inode *nfsi = NFS_I(inode);
	struct nfs_delegation *delegation;
	int status = 0;

	/* Ensure we first revalidate the attributes and page cache! */
	if ((nfsi->cache_validity & (NFS_INO_REVAL_PAGECACHE|NFS_INO_INVALID_ATTR)))
		__nfs_revalidate_inode(NFS_SERVER(inode), inode);

	delegation = nfs_alloc_delegation();
	if (delegation == NULL)
		return -ENOMEM;
	memcpy(delegation->stateid.data, res->delegation.data,
			sizeof(delegation->stateid.data));
	delegation->type = res->delegation_type;
	delegation->maxsize = res->maxsize;
	delegation->cred = get_rpccred(cred);
	delegation->inode = inode;

	spin_lock(&clp->cl_lock);
	if (nfsi->delegation == NULL) {
		list_add(&delegation->super_list, &clp->cl_delegations);
		nfsi->delegation = delegation;
		nfsi->delegation_state = delegation->type;
		delegation = NULL;
	} else {
		if (memcmp(&delegation->stateid, &nfsi->delegation->stateid,
					sizeof(delegation->stateid)) != 0 ||
				delegation->type != nfsi->delegation->type) {
			printk("%s: server %u.%u.%u.%u, handed out a duplicate delegation!\n",
					__FUNCTION__, NIPQUAD(clp->cl_addr));
			status = -EIO;
		}
	}
	spin_unlock(&clp->cl_lock);
	if (delegation != NULL)
		kfree(delegation);
	return status;
}

static int nfs_do_return_delegation(struct inode *inode, struct nfs_delegation *delegation)
{
	int res = 0;

	__nfs_revalidate_inode(NFS_SERVER(inode), inode);

	res = nfs4_proc_delegreturn(inode, delegation->cred, &delegation->stateid);
	nfs_free_delegation(delegation);
	return res;
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
int __nfs_inode_return_delegation(struct inode *inode)
{
	struct nfs4_client *clp = NFS_SERVER(inode)->nfs4_state;
	struct nfs_inode *nfsi = NFS_I(inode);
	struct nfs_delegation *delegation;
	int res = 0;

	nfs_msync_inode(inode);
	down_read(&clp->cl_sem);
	/* Guard against new delegated open calls */
	down_write(&nfsi->rwsem);
	spin_lock(&clp->cl_lock);
	delegation = nfsi->delegation;
	if (delegation != NULL) {
		list_del_init(&delegation->super_list);
		nfsi->delegation = NULL;
		nfsi->delegation_state = 0;
	}
	spin_unlock(&clp->cl_lock);
	nfs_delegation_claim_opens(inode);
	up_write(&nfsi->rwsem);
	up_read(&clp->cl_sem);
	nfs_msync_inode(inode);

	if (delegation != NULL)
		res = nfs_do_return_delegation(inode, delegation);
	return res;
}

/*
 * Return all delegations associated to a super block
 */
void nfs_return_all_delegations(struct super_block *sb)
{
	struct nfs4_client *clp = NFS_SB(sb)->nfs4_state;
	struct nfs_delegation *delegation;
	struct inode *inode;

	if (clp == NULL)
		return;
restart:
	spin_lock(&clp->cl_lock);
	list_for_each_entry(delegation, &clp->cl_delegations, super_list) {
		if (delegation->inode->i_sb != sb)
			continue;
		inode = igrab(delegation->inode);
		if (inode == NULL)
			continue;
		spin_unlock(&clp->cl_lock);
		nfs_inode_return_delegation(inode);
		iput(inode);
		goto restart;
	}
	spin_unlock(&clp->cl_lock);
}

/*
 * Return all delegations following an NFS4ERR_CB_PATH_DOWN error.
 */
void nfs_handle_cb_pathdown(struct nfs4_client *clp)
{
	struct nfs_delegation *delegation;
	struct inode *inode;

	if (clp == NULL)
		return;
restart:
	spin_lock(&clp->cl_lock);
	list_for_each_entry(delegation, &clp->cl_delegations, super_list) {
		inode = igrab(delegation->inode);
		if (inode == NULL)
			continue;
		spin_unlock(&clp->cl_lock);
		nfs_inode_return_delegation(inode);
		iput(inode);
		goto restart;
	}
	spin_unlock(&clp->cl_lock);
}

struct recall_threadargs {
	struct inode *inode;
	struct nfs4_client *clp;
	const nfs4_stateid *stateid;

	struct completion started;
	int result;
};

static int recall_thread(void *data)
{
	struct recall_threadargs *args = (struct recall_threadargs *)data;
	struct inode *inode = igrab(args->inode);
	struct nfs4_client *clp = NFS_SERVER(inode)->nfs4_state;
	struct nfs_inode *nfsi = NFS_I(inode);
	struct nfs_delegation *delegation;

	daemonize("nfsv4-delegreturn");

	nfs_msync_inode(inode);
	down_read(&clp->cl_sem);
	down_write(&nfsi->rwsem);
	spin_lock(&clp->cl_lock);
	delegation = nfsi->delegation;
	if (delegation != NULL && memcmp(delegation->stateid.data,
				args->stateid->data,
				sizeof(delegation->stateid.data)) == 0) {
		list_del_init(&delegation->super_list);
		nfsi->delegation = NULL;
		nfsi->delegation_state = 0;
		args->result = 0;
	} else {
		delegation = NULL;
		args->result = -ENOENT;
	}
	spin_unlock(&clp->cl_lock);
	complete(&args->started);
	nfs_delegation_claim_opens(inode);
	up_write(&nfsi->rwsem);
	up_read(&clp->cl_sem);
	nfs_msync_inode(inode);

	if (delegation != NULL)
		nfs_do_return_delegation(inode, delegation);
	iput(inode);
	module_put_and_exit(0);
}

/*
 * Asynchronous delegation recall!
 */
int nfs_async_inode_return_delegation(struct inode *inode, const nfs4_stateid *stateid)
{
	struct recall_threadargs data = {
		.inode = inode,
		.stateid = stateid,
	};
	int status;

	init_completion(&data.started);
	__module_get(THIS_MODULE);
	status = kernel_thread(recall_thread, &data, CLONE_KERNEL);
	if (status < 0)
		goto out_module_put;
	wait_for_completion(&data.started);
	return data.result;
out_module_put:
	module_put(THIS_MODULE);
	return status;
}

/*
 * Retrieve the inode associated with a delegation
 */
struct inode *nfs_delegation_find_inode(struct nfs4_client *clp, const struct nfs_fh *fhandle)
{
	struct nfs_delegation *delegation;
	struct inode *res = NULL;
	spin_lock(&clp->cl_lock);
	list_for_each_entry(delegation, &clp->cl_delegations, super_list) {
		if (nfs_compare_fh(fhandle, &NFS_I(delegation->inode)->fh) == 0) {
			res = igrab(delegation->inode);
			break;
		}
	}
	spin_unlock(&clp->cl_lock);
	return res;
}

/*
 * Mark all delegations as needing to be reclaimed
 */
void nfs_delegation_mark_reclaim(struct nfs4_client *clp)
{
	struct nfs_delegation *delegation;
	spin_lock(&clp->cl_lock);
	list_for_each_entry(delegation, &clp->cl_delegations, super_list)
		delegation->flags |= NFS_DELEGATION_NEED_RECLAIM;
	spin_unlock(&clp->cl_lock);
}

/*
 * Reap all unclaimed delegations after reboot recovery is done
 */
void nfs_delegation_reap_unclaimed(struct nfs4_client *clp)
{
	struct nfs_delegation *delegation, *n;
	LIST_HEAD(head);
	spin_lock(&clp->cl_lock);
	list_for_each_entry_safe(delegation, n, &clp->cl_delegations, super_list) {
		if ((delegation->flags & NFS_DELEGATION_NEED_RECLAIM) == 0)
			continue;
		list_move(&delegation->super_list, &head);
		NFS_I(delegation->inode)->delegation = NULL;
		NFS_I(delegation->inode)->delegation_state = 0;
	}
	spin_unlock(&clp->cl_lock);
	while(!list_empty(&head)) {
		delegation = list_entry(head.next, struct nfs_delegation, super_list);
		list_del(&delegation->super_list);
		nfs_free_delegation(delegation);
	}
}
