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

static void nfs_free_delegation(struct nfs_delegation *delegation)
{
	if (delegation->cred)
		put_rpccred(delegation->cred);
	kfree(delegation);
}

static void nfs_free_delegation_callback(struct rcu_head *head)
{
	struct nfs_delegation *delegation = container_of(head, struct nfs_delegation, rcu);

	nfs_free_delegation(delegation);
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
	struct nfs_client *clp = NFS_SERVER(inode)->nfs_client;
	struct nfs_inode *nfsi = NFS_I(inode);
	struct nfs_delegation *delegation;
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

	spin_lock(&clp->cl_lock);
	if (rcu_dereference(nfsi->delegation) == NULL) {
		list_add_rcu(&delegation->super_list, &clp->cl_delegations);
		nfsi->delegation_state = delegation->type;
		rcu_assign_pointer(nfsi->delegation, delegation);
		delegation = NULL;
	} else {
		if (memcmp(&delegation->stateid, &nfsi->delegation->stateid,
					sizeof(delegation->stateid)) != 0 ||
				delegation->type != nfsi->delegation->type) {
			printk("%s: server %u.%u.%u.%u, handed out a duplicate delegation!\n",
					__FUNCTION__, NIPQUAD(clp->cl_addr.sin_addr));
			status = -EIO;
		}
	}

	/* Ensure we revalidate the attributes and page cache! */
	spin_lock(&inode->i_lock);
	nfsi->cache_validity |= NFS_INO_REVAL_FORCED;
	spin_unlock(&inode->i_lock);

	spin_unlock(&clp->cl_lock);
	kfree(delegation);
	return status;
}

static int nfs_do_return_delegation(struct inode *inode, struct nfs_delegation *delegation)
{
	int res = 0;

	res = nfs4_proc_delegreturn(inode, delegation->cred, &delegation->stateid);
	call_rcu(&delegation->rcu, nfs_free_delegation_callback);
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
static int __nfs_inode_return_delegation(struct inode *inode, struct nfs_delegation *delegation)
{
	struct nfs_client *clp = NFS_SERVER(inode)->nfs_client;
	struct nfs_inode *nfsi = NFS_I(inode);

	nfs_msync_inode(inode);
	down_read(&clp->cl_sem);
	/* Guard against new delegated open calls */
	down_write(&nfsi->rwsem);
	nfs_delegation_claim_opens(inode, &delegation->stateid);
	up_write(&nfsi->rwsem);
	up_read(&clp->cl_sem);
	nfs_msync_inode(inode);

	return nfs_do_return_delegation(inode, delegation);
}

static struct nfs_delegation *nfs_detach_delegation_locked(struct nfs_inode *nfsi, const nfs4_stateid *stateid)
{
	struct nfs_delegation *delegation = rcu_dereference(nfsi->delegation);

	if (delegation == NULL)
		goto nomatch;
	if (stateid != NULL && memcmp(delegation->stateid.data, stateid->data,
				sizeof(delegation->stateid.data)) != 0)
		goto nomatch;
	list_del_rcu(&delegation->super_list);
	nfsi->delegation_state = 0;
	rcu_assign_pointer(nfsi->delegation, NULL);
	return delegation;
nomatch:
	return NULL;
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

/*
 * Return all delegations associated to a super block
 */
void nfs_return_all_delegations(struct super_block *sb)
{
	struct nfs_client *clp = NFS_SB(sb)->nfs_client;
	struct nfs_delegation *delegation;
	struct inode *inode;

	if (clp == NULL)
		return;
restart:
	rcu_read_lock();
	list_for_each_entry_rcu(delegation, &clp->cl_delegations, super_list) {
		if (delegation->inode->i_sb != sb)
			continue;
		inode = igrab(delegation->inode);
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

static int nfs_do_expire_all_delegations(void *ptr)
{
	struct nfs_client *clp = ptr;
	struct nfs_delegation *delegation;
	struct inode *inode;

	allow_signal(SIGKILL);
restart:
	if (test_bit(NFS4CLNT_STATE_RECOVER, &clp->cl_state) != 0)
		goto out;
	if (test_bit(NFS4CLNT_LEASE_EXPIRED, &clp->cl_state) == 0)
		goto out;
	rcu_read_lock();
	list_for_each_entry_rcu(delegation, &clp->cl_delegations, super_list) {
		inode = igrab(delegation->inode);
		if (inode == NULL)
			continue;
		spin_lock(&clp->cl_lock);
		delegation = nfs_detach_delegation_locked(NFS_I(inode), NULL);
		spin_unlock(&clp->cl_lock);
		rcu_read_unlock();
		if (delegation)
			__nfs_inode_return_delegation(inode, delegation);
		iput(inode);
		goto restart;
	}
	rcu_read_unlock();
out:
	nfs_put_client(clp);
	module_put_and_exit(0);
}

void nfs_expire_all_delegations(struct nfs_client *clp)
{
	struct task_struct *task;

	__module_get(THIS_MODULE);
	atomic_inc(&clp->cl_count);
	task = kthread_run(nfs_do_expire_all_delegations, clp,
			"%u.%u.%u.%u-delegreturn",
			NIPQUAD(clp->cl_addr.sin_addr));
	if (!IS_ERR(task))
		return;
	nfs_put_client(clp);
	module_put(THIS_MODULE);
}

/*
 * Return all delegations following an NFS4ERR_CB_PATH_DOWN error.
 */
void nfs_handle_cb_pathdown(struct nfs_client *clp)
{
	struct nfs_delegation *delegation;
	struct inode *inode;

	if (clp == NULL)
		return;
restart:
	rcu_read_lock();
	list_for_each_entry_rcu(delegation, &clp->cl_delegations, super_list) {
		inode = igrab(delegation->inode);
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

struct recall_threadargs {
	struct inode *inode;
	struct nfs_client *clp;
	const nfs4_stateid *stateid;

	struct completion started;
	int result;
};

static int recall_thread(void *data)
{
	struct recall_threadargs *args = (struct recall_threadargs *)data;
	struct inode *inode = igrab(args->inode);
	struct nfs_client *clp = NFS_SERVER(inode)->nfs_client;
	struct nfs_inode *nfsi = NFS_I(inode);
	struct nfs_delegation *delegation;

	daemonize("nfsv4-delegreturn");

	nfs_msync_inode(inode);
	down_read(&clp->cl_sem);
	down_write(&nfsi->rwsem);
	spin_lock(&clp->cl_lock);
	delegation = nfs_detach_delegation_locked(nfsi, args->stateid);
	if (delegation != NULL)
		args->result = 0;
	else
		args->result = -ENOENT;
	spin_unlock(&clp->cl_lock);
	complete(&args->started);
	nfs_delegation_claim_opens(inode, args->stateid);
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
struct inode *nfs_delegation_find_inode(struct nfs_client *clp, const struct nfs_fh *fhandle)
{
	struct nfs_delegation *delegation;
	struct inode *res = NULL;
	rcu_read_lock();
	list_for_each_entry_rcu(delegation, &clp->cl_delegations, super_list) {
		if (nfs_compare_fh(fhandle, &NFS_I(delegation->inode)->fh) == 0) {
			res = igrab(delegation->inode);
			break;
		}
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
		delegation->flags |= NFS_DELEGATION_NEED_RECLAIM;
	rcu_read_unlock();
}

/*
 * Reap all unclaimed delegations after reboot recovery is done
 */
void nfs_delegation_reap_unclaimed(struct nfs_client *clp)
{
	struct nfs_delegation *delegation;
restart:
	rcu_read_lock();
	list_for_each_entry_rcu(delegation, &clp->cl_delegations, super_list) {
		if ((delegation->flags & NFS_DELEGATION_NEED_RECLAIM) == 0)
			continue;
		spin_lock(&clp->cl_lock);
		delegation = nfs_detach_delegation_locked(NFS_I(delegation->inode), NULL);
		spin_unlock(&clp->cl_lock);
		rcu_read_unlock();
		if (delegation != NULL)
			call_rcu(&delegation->rcu, nfs_free_delegation_callback);
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
