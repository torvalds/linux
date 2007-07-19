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


struct nfs_unlinkdata {
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

/**
 * nfs_async_unlink_init - Initialize the RPC info
 * task: rpc_task of the sillydelete
 */
static void nfs_async_unlink_init(struct rpc_task *task, void *calldata)
{
	struct nfs_unlinkdata *data = calldata;
	struct inode *dir = data->dir;
	struct rpc_message msg = {
		.rpc_argp = &data->args,
		.rpc_resp = &data->res,
		.rpc_cred = data->cred,
	};

	nfs_begin_data_update(dir);
	NFS_PROTO(dir)->unlink_setup(&msg, dir);
	rpc_call_setup(task, &msg, 0);
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
	else
		nfs_end_data_update(dir);
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
	nfs_free_unlinkdata(data);
}

static const struct rpc_call_ops nfs_unlink_ops = {
	.rpc_call_prepare = nfs_async_unlink_init,
	.rpc_call_done = nfs_async_unlink_done,
	.rpc_release = nfs_async_unlink_release,
};

static int nfs_call_unlink(struct dentry *dentry, struct nfs_unlinkdata *data)
{
	struct rpc_task *task;
	struct dentry *parent;
	struct inode *dir;

	if (nfs_copy_dname(dentry, data) < 0)
		goto out_free;

	parent = dget_parent(dentry);
	if (parent == NULL)
		goto out_free;
	dir = igrab(parent->d_inode);
	dput(parent);
	if (dir == NULL)
		goto out_free;

	data->dir = dir;
	data->args.fh = NFS_FH(dir);
	nfs_fattr_init(&data->res.dir_attr);

	task = rpc_run_task(NFS_CLIENT(dir), RPC_TASK_ASYNC, &nfs_unlink_ops, data);
	if (!IS_ERR(task))
		rpc_put_task(task);
	return 1;
out_free:
	return 0;
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

	data->cred = rpcauth_lookupcred(NFS_CLIENT(dir)->cl_auth, 0);
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
