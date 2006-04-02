/*
 *  linux/fs/nfs/unlink.c
 *
 * nfs sillydelete handling
 *
 * NOTE: we rely on holding the BKL for list manipulation protection.
 */

#include <linux/slab.h>
#include <linux/string.h>
#include <linux/dcache.h>
#include <linux/sunrpc/sched.h>
#include <linux/sunrpc/clnt.h>
#include <linux/nfs_fs.h>


struct nfs_unlinkdata {
	struct nfs_unlinkdata	*next;
	struct dentry	*dir, *dentry;
	struct qstr	name;
	struct rpc_task	task;
	struct rpc_cred	*cred;
	unsigned int	count;
};

static struct nfs_unlinkdata	*nfs_deletes;
static RPC_WAITQ(nfs_delete_queue, "nfs_delete_queue");

/**
 * nfs_detach_unlinkdata - Remove asynchronous unlink from global list
 * @data: pointer to descriptor
 */
static inline void
nfs_detach_unlinkdata(struct nfs_unlinkdata *data)
{
	struct nfs_unlinkdata	**q;

	for (q = &nfs_deletes; *q != NULL; q = &((*q)->next)) {
		if (*q == data) {
			*q = data->next;
			break;
		}
	}
}

/**
 * nfs_put_unlinkdata - release data from a sillydelete operation.
 * @data: pointer to unlink structure.
 */
static void
nfs_put_unlinkdata(struct nfs_unlinkdata *data)
{
	if (--data->count == 0) {
		nfs_detach_unlinkdata(data);
		kfree(data->name.name);
		kfree(data);
	}
}

#define NAME_ALLOC_LEN(len)	((len+16) & ~15)
/**
 * nfs_copy_dname - copy dentry name to data structure
 * @dentry: pointer to dentry
 * @data: nfs_unlinkdata
 */
static inline void
nfs_copy_dname(struct dentry *dentry, struct nfs_unlinkdata *data)
{
	char		*str;
	int		len = dentry->d_name.len;

	str = kmalloc(NAME_ALLOC_LEN(len), GFP_KERNEL);
	if (!str)
		return;
	memcpy(str, dentry->d_name.name, len);
	if (!data->name.len) {
		data->name.len = len;
		data->name.name = str;
	} else
		kfree(str);
}

/**
 * nfs_async_unlink_init - Initialize the RPC info
 * @task: rpc_task of the sillydelete
 *
 * We delay initializing RPC info until after the call to dentry_iput()
 * in order to minimize races against rename().
 */
static void nfs_async_unlink_init(struct rpc_task *task, void *calldata)
{
	struct nfs_unlinkdata	*data = calldata;
	struct dentry		*dir = data->dir;
	struct rpc_message	msg = {
		.rpc_cred	= data->cred,
	};
	int			status = -ENOENT;

	if (!data->name.len)
		goto out_err;

	status = NFS_PROTO(dir->d_inode)->unlink_setup(&msg, dir, &data->name);
	if (status < 0)
		goto out_err;
	nfs_begin_data_update(dir->d_inode);
	rpc_call_setup(task, &msg, 0);
	return;
 out_err:
	rpc_exit(task, status);
}

/**
 * nfs_async_unlink_done - Sillydelete post-processing
 * @task: rpc_task of the sillydelete
 *
 * Do the directory attribute update.
 */
static void nfs_async_unlink_done(struct rpc_task *task, void *calldata)
{
	struct nfs_unlinkdata	*data = calldata;
	struct dentry		*dir = data->dir;
	struct inode		*dir_i;

	if (!dir)
		return;
	dir_i = dir->d_inode;
	nfs_end_data_update(dir_i);
	if (NFS_PROTO(dir_i)->unlink_done(dir, task))
		return;
	put_rpccred(data->cred);
	data->cred = NULL;
	dput(dir);
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
	nfs_put_unlinkdata(data);
}

static const struct rpc_call_ops nfs_unlink_ops = {
	.rpc_call_prepare = nfs_async_unlink_init,
	.rpc_call_done = nfs_async_unlink_done,
	.rpc_release = nfs_async_unlink_release,
};

/**
 * nfs_async_unlink - asynchronous unlinking of a file
 * @dentry: dentry to unlink
 */
int
nfs_async_unlink(struct dentry *dentry)
{
	struct dentry	*dir = dentry->d_parent;
	struct nfs_unlinkdata	*data;
	struct rpc_clnt	*clnt = NFS_CLIENT(dir->d_inode);
	int		status = -ENOMEM;

	data = kzalloc(sizeof(*data), GFP_KERNEL);
	if (!data)
		goto out;

	data->cred = rpcauth_lookupcred(clnt->cl_auth, 0);
	if (IS_ERR(data->cred)) {
		status = PTR_ERR(data->cred);
		goto out_free;
	}
	data->dir = dget(dir);
	data->dentry = dentry;

	data->next = nfs_deletes;
	nfs_deletes = data;
	data->count = 1;

	rpc_init_task(&data->task, clnt, RPC_TASK_ASYNC, &nfs_unlink_ops, data);

	spin_lock(&dentry->d_lock);
	dentry->d_flags |= DCACHE_NFSFS_RENAMED;
	spin_unlock(&dentry->d_lock);

	rpc_sleep_on(&nfs_delete_queue, &data->task, NULL, NULL);
	status = 0;
 out:
	return status;
out_free:
	kfree(data);
	return status;
}

/**
 * nfs_complete_unlink - Initialize completion of the sillydelete
 * @dentry: dentry to delete
 *
 * Since we're most likely to be called by dentry_iput(), we
 * only use the dentry to find the sillydelete. We then copy the name
 * into the qstr.
 */
void
nfs_complete_unlink(struct dentry *dentry)
{
	struct nfs_unlinkdata	*data;

	for(data = nfs_deletes; data != NULL; data = data->next) {
		if (dentry == data->dentry)
			break;
	}
	if (!data)
		return;
	data->count++;
	nfs_copy_dname(dentry, data);
	spin_lock(&dentry->d_lock);
	dentry->d_flags &= ~DCACHE_NFSFS_RENAMED;
	spin_unlock(&dentry->d_lock);
	rpc_wake_up_task(&data->task);
	nfs_put_unlinkdata(data);
}
