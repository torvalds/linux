/*
 *  linux/fs/nfs/unlink.c
 *
 * nfs sillydelete handling
 *
 */

#include <linux/slab.h>
#include <linux/string.h>
#include <linux/dcache.h>
#include <linux/sunrpc/sched.h>
#include <linux/sunrpc/clnt.h>
#include <linux/nfs_fs.h>
#include <linux/sched.h>
#include <linux/wait.h>

#include "internal.h"

struct nfs_unlinkdata {
	struct hlist_node list;
	struct nfs_removeargs args;
	struct nfs_removeres res;
	struct inode *dir;
	struct rpc_cred	*cred;
};

/**
 * nfs_free_unlinkdata - release data from a sillydelete operation.
 * @data: pointer to unlink structure.
 */
static void
nfs_free_unlinkdata(struct nfs_unlinkdata *data)
{
	iput(data->dir);
	put_rpccred(data->cred);
	kfree(data->args.name.name);
	kfree(data);
}

#define NAME_ALLOC_LEN(len)	((len+16) & ~15)
/**
 * nfs_copy_dname - copy dentry name to data structure
 * @dentry: pointer to dentry
 * @data: nfs_unlinkdata
 */
static int nfs_copy_dname(struct dentry *dentry, struct nfs_unlinkdata *data)
{
	char		*str;
	int		len = dentry->d_name.len;

	str = kmemdup(dentry->d_name.name, NAME_ALLOC_LEN(len), GFP_KERNEL);
	if (!str)
		return -ENOMEM;
	data->args.name.len = len;
	data->args.name.name = str;
	return 0;
}

static void nfs_free_dname(struct nfs_unlinkdata *data)
{
	kfree(data->args.name.name);
	data->args.name.name = NULL;
	data->args.name.len = 0;
}

static void nfs_dec_sillycount(struct inode *dir)
{
	struct nfs_inode *nfsi = NFS_I(dir);
	if (atomic_dec_return(&nfsi->silly_count) == 1)
		wake_up(&nfsi->waitqueue);
}

/**
 * nfs_async_unlink_done - Sillydelete post-processing
 * @task: rpc_task of the sillydelete
 *
 * Do the directory attribute update.
 */
static void nfs_async_unlink_done(struct rpc_task *task, void *calldata)
{
	struct nfs_unlinkdata *data = calldata;
	struct inode *dir = data->dir;

	if (!NFS_PROTO(dir)->unlink_done(task, dir))
		rpc_restart_call(task);
}

/**
 * nfs_async_unlink_release - Release the sillydelete data.
 * @task: rpc_task of the sillydelete
 *
 * We need to call nfs_put_unlinkdata as a 'tk_release' task since the
 * rpc_task would be freed too.
 */
static void nfs_async_unlink_release(void *calldata)
{
	struct nfs_unlinkdata	*data = calldata;
	struct super_block *sb = data->dir->i_sb;

	nfs_dec_sillycount(data->dir);
	nfs_free_unlinkdata(data);
	nfs_sb_deactive(sb);
}

static const struct rpc_call_ops nfs_unlink_ops = {
	.rpc_call_done = nfs_async_unlink_done,
	.rpc_release = nfs_async_unlink_release,
};

static int nfs_do_call_unlink(struct dentry *parent, struct inode *dir, struct nfs_unlinkdata *data)
{
	struct rpc_message msg = {
		.rpc_argp = &data->args,
		.rpc_resp = &data->res,
		.rpc_cred = data->cred,
	};
	struct rpc_task_setup task_setup_data = {
		.rpc_message = &msg,
		.callback_ops = &nfs_unlink_ops,
		.callback_data = data,
		.workqueue = nfsiod_workqueue,
		.flags = RPC_TASK_ASYNC,
	};
	struct rpc_task *task;
	struct dentry *alias;

	alias = d_lookup(parent, &data->args.name);
	if (alias != NULL) {
		int ret = 0;

		/*
		 * Hey, we raced with lookup... See if we need to transfer
		 * the sillyrename information to the aliased dentry.
		 */
		nfs_free_dname(data);
		spin_lock(&alias->d_lock);
		if (alias->d_inode != NULL &&
		    !(alias->d_flags & DCACHE_NFSFS_RENAMED)) {
			alias->d_fsdata = data;
			alias->d_flags |= DCACHE_NFSFS_RENAMED;
			ret = 1;
		}
		spin_unlock(&alias->d_lock);
		nfs_dec_sillycount(dir);
		dput(alias);
		return ret;
	}
	data->dir = igrab(dir);
	if (!data->dir) {
		nfs_dec_sillycount(dir);
		return 0;
	}
	nfs_sb_active(dir->i_sb);
	data->args.fh = NFS_FH(dir);
	nfs_fattr_init(&data->res.dir_attr);

	NFS_PROTO(dir)->unlink_setup(&msg, dir);

	task_setup_data.rpc_client = NFS_CLIENT(dir);
	task = rpc_run_task(&task_setup_data);
	if (!IS_ERR(task))
		rpc_put_task(task);
	return 1;
}

static int nfs_call_unlink(struct dentry *dentry, struct nfs_unlinkdata *data)
{
	struct dentry *parent;
	struct inode *dir;
	int ret = 0;


	parent = dget_parent(dentry);
	if (parent == NULL)
		goto out_free;
	dir = parent->d_inode;
	if (nfs_copy_dname(dentry, data) != 0)
		goto out_dput;
	/* Non-exclusive lock protects against concurrent lookup() calls */
	spin_lock(&dir->i_lock);
	if (atomic_inc_not_zero(&NFS_I(dir)->silly_count) == 0) {
		/* Deferred delete */
		hlist_add_head(&data->list, &NFS_I(dir)->silly_list);
		spin_unlock(&dir->i_lock);
		ret = 1;
		goto out_dput;
	}
	spin_unlock(&dir->i_lock);
	ret = nfs_do_call_unlink(parent, dir, data);
out_dput:
	dput(parent);
out_free:
	return ret;
}

void nfs_block_sillyrename(struct dentry *dentry)
{
	struct nfs_inode *nfsi = NFS_I(dentry->d_inode);

	wait_event(nfsi->waitqueue, atomic_cmpxchg(&nfsi->silly_count, 1, 0) == 1);
}

void nfs_unblock_sillyrename(struct dentry *dentry)
{
	struct inode *dir = dentry->d_inode;
	struct nfs_inode *nfsi = NFS_I(dir);
	struct nfs_unlinkdata *data;

	atomic_inc(&nfsi->silly_count);
	spin_lock(&dir->i_lock);
	while (!hlist_empty(&nfsi->silly_list)) {
		if (!atomic_inc_not_zero(&nfsi->silly_count))
			break;
		data = hlist_entry(nfsi->silly_list.first, struct nfs_unlinkdata, list);
		hlist_del(&data->list);
		spin_unlock(&dir->i_lock);
		if (nfs_do_call_unlink(dentry, dir, data) == 0)
			nfs_free_unlinkdata(data);
		spin_lock(&dir->i_lock);
	}
	spin_unlock(&dir->i_lock);
}

/**
 * nfs_async_unlink - asynchronous unlinking of a file
 * @dir: parent directory of dentry
 * @dentry: dentry to unlink
 */
int
nfs_async_unlink(struct inode *dir, struct dentry *dentry)
{
	struct nfs_unlinkdata *data;
	int status = -ENOMEM;

	data = kzalloc(sizeof(*data), GFP_KERNEL);
	if (data == NULL)
		goto out;

	data->cred = rpc_lookup_cred();
	if (IS_ERR(data->cred)) {
		status = PTR_ERR(data->cred);
		goto out_free;
	}

	status = -EBUSY;
	spin_lock(&dentry->d_lock);
	if (dentry->d_flags & DCACHE_NFSFS_RENAMED)
		goto out_unlock;
	dentry->d_flags |= DCACHE_NFSFS_RENAMED;
	dentry->d_fsdata = data;
	spin_unlock(&dentry->d_lock);
	return 0;
out_unlock:
	spin_unlock(&dentry->d_lock);
	put_rpccred(data->cred);
out_free:
	kfree(data);
out:
	return status;
}

/**
 * nfs_complete_unlink - Initialize completion of the sillydelete
 * @dentry: dentry to delete
 * @inode: inode
 *
 * Since we're most likely to be called by dentry_iput(), we
 * only use the dentry to find the sillydelete. We then copy the name
 * into the qstr.
 */
void
nfs_complete_unlink(struct dentry *dentry, struct inode *inode)
{
	struct nfs_unlinkdata	*data = NULL;

	spin_lock(&dentry->d_lock);
	if (dentry->d_flags & DCACHE_NFSFS_RENAMED) {
		dentry->d_flags &= ~DCACHE_NFSFS_RENAMED;
		data = dentry->d_fsdata;
	}
	spin_unlock(&dentry->d_lock);

	if (data != NULL && (NFS_STALE(inode) || !nfs_call_unlink(dentry, data)))
		nfs_free_unlinkdata(data);
}
