// SPDX-License-Identifier: GPL-2.0
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
#include <linux/namei.h>
#include <linux/fsnotify.h>

#include "internal.h"
#include "nfs4_fs.h"
#include "iostat.h"
#include "delegation.h"

#include "nfstrace.h"

/**
 * nfs_free_unlinkdata - release data from a sillydelete operation.
 * @data: pointer to unlink structure.
 */
static void
nfs_free_unlinkdata(struct nfs_unlinkdata *data)
{
	put_rpccred(data->cred);
	kfree(data->args.name.name);
	kfree(data);
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
	struct inode *dir = d_inode(data->dentry->d_parent);

	trace_nfs_sillyrename_unlink(data, task->tk_status);
	if (!NFS_PROTO(dir)->unlink_done(task, dir))
		rpc_restart_call_prepare(task);
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
	struct dentry *dentry = data->dentry;
	struct super_block *sb = dentry->d_sb;

	up_read_non_owner(&NFS_I(d_inode(dentry->d_parent))->rmdir_sem);
	d_lookup_done(dentry);
	nfs_free_unlinkdata(data);
	dput(dentry);
	nfs_sb_deactive(sb);
}

static void nfs_unlink_prepare(struct rpc_task *task, void *calldata)
{
	struct nfs_unlinkdata *data = calldata;
	struct inode *dir = d_inode(data->dentry->d_parent);
	NFS_PROTO(dir)->unlink_rpc_prepare(task, data);
}

static const struct rpc_call_ops nfs_unlink_ops = {
	.rpc_call_done = nfs_async_unlink_done,
	.rpc_release = nfs_async_unlink_release,
	.rpc_call_prepare = nfs_unlink_prepare,
};

static void nfs_do_call_unlink(struct nfs_unlinkdata *data)
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
	struct inode *dir = d_inode(data->dentry->d_parent);
	nfs_sb_active(dir->i_sb);
	data->args.fh = NFS_FH(dir);
	nfs_fattr_init(data->res.dir_attr);

	NFS_PROTO(dir)->unlink_setup(&msg, data->dentry);

	task_setup_data.rpc_client = NFS_CLIENT(dir);
	task = rpc_run_task(&task_setup_data);
	if (!IS_ERR(task))
		rpc_put_task_async(task);
}

static int nfs_call_unlink(struct dentry *dentry, struct nfs_unlinkdata *data)
{
	struct inode *dir = d_inode(dentry->d_parent);
	struct dentry *alias;

	down_read_non_owner(&NFS_I(dir)->rmdir_sem);
	alias = d_alloc_parallel(dentry->d_parent, &data->args.name, &data->wq);
	if (IS_ERR(alias)) {
		up_read_non_owner(&NFS_I(dir)->rmdir_sem);
		return 0;
	}
	if (!d_in_lookup(alias)) {
		int ret;
		void *devname_garbage = NULL;

		/*
		 * Hey, we raced with lookup... See if we need to transfer
		 * the sillyrename information to the aliased dentry.
		 */
		spin_lock(&alias->d_lock);
		if (d_really_is_positive(alias) &&
		    !(alias->d_flags & DCACHE_NFSFS_RENAMED)) {
			devname_garbage = alias->d_fsdata;
			alias->d_fsdata = data;
			alias->d_flags |= DCACHE_NFSFS_RENAMED;
			ret = 1;
		} else
			ret = 0;
		spin_unlock(&alias->d_lock);
		dput(alias);
		up_read_non_owner(&NFS_I(dir)->rmdir_sem);
		/*
		 * If we'd displaced old cached devname, free it.  At that
		 * point dentry is definitely not a root, so we won't need
		 * that anymore.
		 */
		kfree(devname_garbage);
		return ret;
	}
	data->dentry = alias;
	nfs_do_call_unlink(data);
	return 1;
}

/**
 * nfs_async_unlink - asynchronous unlinking of a file
 * @dir: parent directory of dentry
 * @dentry: dentry to unlink
 */
static int
nfs_async_unlink(struct dentry *dentry, const struct qstr *name)
{
	struct nfs_unlinkdata *data;
	int status = -ENOMEM;
	void *devname_garbage = NULL;

	data = kzalloc(sizeof(*data), GFP_KERNEL);
	if (data == NULL)
		goto out;
	data->args.name.name = kstrdup(name->name, GFP_KERNEL);
	if (!data->args.name.name)
		goto out_free;
	data->args.name.len = name->len;

	data->cred = rpc_lookup_cred();
	if (IS_ERR(data->cred)) {
		status = PTR_ERR(data->cred);
		goto out_free_name;
	}
	data->res.dir_attr = &data->dir_attr;
	init_waitqueue_head(&data->wq);

	status = -EBUSY;
	spin_lock(&dentry->d_lock);
	if (dentry->d_flags & DCACHE_NFSFS_RENAMED)
		goto out_unlock;
	dentry->d_flags |= DCACHE_NFSFS_RENAMED;
	devname_garbage = dentry->d_fsdata;
	dentry->d_fsdata = data;
	spin_unlock(&dentry->d_lock);
	/*
	 * If we'd displaced old cached devname, free it.  At that
	 * point dentry is definitely not a root, so we won't need
	 * that anymore.
	 */
	kfree(devname_garbage);
	return 0;
out_unlock:
	spin_unlock(&dentry->d_lock);
	put_rpccred(data->cred);
out_free_name:
	kfree(data->args.name.name);
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
	struct nfs_unlinkdata	*data;

	spin_lock(&dentry->d_lock);
	dentry->d_flags &= ~DCACHE_NFSFS_RENAMED;
	data = dentry->d_fsdata;
	dentry->d_fsdata = NULL;
	spin_unlock(&dentry->d_lock);

	if (NFS_STALE(inode) || !nfs_call_unlink(dentry, data))
		nfs_free_unlinkdata(data);
}

/* Cancel a queued async unlink. Called when a sillyrename run fails. */
static void
nfs_cancel_async_unlink(struct dentry *dentry)
{
	spin_lock(&dentry->d_lock);
	if (dentry->d_flags & DCACHE_NFSFS_RENAMED) {
		struct nfs_unlinkdata *data = dentry->d_fsdata;

		dentry->d_flags &= ~DCACHE_NFSFS_RENAMED;
		dentry->d_fsdata = NULL;
		spin_unlock(&dentry->d_lock);
		nfs_free_unlinkdata(data);
		return;
	}
	spin_unlock(&dentry->d_lock);
}

/**
 * nfs_async_rename_done - Sillyrename post-processing
 * @task: rpc_task of the sillyrename
 * @calldata: nfs_renamedata for the sillyrename
 *
 * Do the directory attribute updates and the d_move
 */
static void nfs_async_rename_done(struct rpc_task *task, void *calldata)
{
	struct nfs_renamedata *data = calldata;
	struct inode *old_dir = data->old_dir;
	struct inode *new_dir = data->new_dir;
	struct dentry *old_dentry = data->old_dentry;

	trace_nfs_sillyrename_rename(old_dir, old_dentry,
			new_dir, data->new_dentry, task->tk_status);
	if (!NFS_PROTO(old_dir)->rename_done(task, old_dir, new_dir)) {
		rpc_restart_call_prepare(task);
		return;
	}

	if (data->complete)
		data->complete(task, data);
}

/**
 * nfs_async_rename_release - Release the sillyrename data.
 * @calldata: the struct nfs_renamedata to be released
 */
static void nfs_async_rename_release(void *calldata)
{
	struct nfs_renamedata	*data = calldata;
	struct super_block *sb = data->old_dir->i_sb;

	if (d_really_is_positive(data->old_dentry))
		nfs_mark_for_revalidate(d_inode(data->old_dentry));

	/* The result of the rename is unknown. Play it safe by
	 * forcing a new lookup */
	if (data->cancelled) {
		spin_lock(&data->old_dir->i_lock);
		nfs_force_lookup_revalidate(data->old_dir);
		spin_unlock(&data->old_dir->i_lock);
		if (data->new_dir != data->old_dir) {
			spin_lock(&data->new_dir->i_lock);
			nfs_force_lookup_revalidate(data->new_dir);
			spin_unlock(&data->new_dir->i_lock);
		}
	}

	dput(data->old_dentry);
	dput(data->new_dentry);
	iput(data->old_dir);
	iput(data->new_dir);
	nfs_sb_deactive(sb);
	put_rpccred(data->cred);
	kfree(data);
}

static void nfs_rename_prepare(struct rpc_task *task, void *calldata)
{
	struct nfs_renamedata *data = calldata;
	NFS_PROTO(data->old_dir)->rename_rpc_prepare(task, data);
}

static const struct rpc_call_ops nfs_rename_ops = {
	.rpc_call_done = nfs_async_rename_done,
	.rpc_release = nfs_async_rename_release,
	.rpc_call_prepare = nfs_rename_prepare,
};

/**
 * nfs_async_rename - perform an asynchronous rename operation
 * @old_dir: directory that currently holds the dentry to be renamed
 * @new_dir: target directory for the rename
 * @old_dentry: original dentry to be renamed
 * @new_dentry: dentry to which the old_dentry should be renamed
 *
 * It's expected that valid references to the dentries and inodes are held
 */
struct rpc_task *
nfs_async_rename(struct inode *old_dir, struct inode *new_dir,
		 struct dentry *old_dentry, struct dentry *new_dentry,
		 void (*complete)(struct rpc_task *, struct nfs_renamedata *))
{
	struct nfs_renamedata *data;
	struct rpc_message msg = { };
	struct rpc_task_setup task_setup_data = {
		.rpc_message = &msg,
		.callback_ops = &nfs_rename_ops,
		.workqueue = nfsiod_workqueue,
		.rpc_client = NFS_CLIENT(old_dir),
		.flags = RPC_TASK_ASYNC,
	};

	data = kzalloc(sizeof(*data), GFP_KERNEL);
	if (data == NULL)
		return ERR_PTR(-ENOMEM);
	task_setup_data.callback_data = data;

	data->cred = rpc_lookup_cred();
	if (IS_ERR(data->cred)) {
		struct rpc_task *task = ERR_CAST(data->cred);
		kfree(data);
		return task;
	}

	msg.rpc_argp = &data->args;
	msg.rpc_resp = &data->res;
	msg.rpc_cred = data->cred;

	/* set up nfs_renamedata */
	data->old_dir = old_dir;
	ihold(old_dir);
	data->new_dir = new_dir;
	ihold(new_dir);
	data->old_dentry = dget(old_dentry);
	data->new_dentry = dget(new_dentry);
	nfs_fattr_init(&data->old_fattr);
	nfs_fattr_init(&data->new_fattr);
	data->complete = complete;

	/* set up nfs_renameargs */
	data->args.old_dir = NFS_FH(old_dir);
	data->args.old_name = &old_dentry->d_name;
	data->args.new_dir = NFS_FH(new_dir);
	data->args.new_name = &new_dentry->d_name;

	/* set up nfs_renameres */
	data->res.old_fattr = &data->old_fattr;
	data->res.new_fattr = &data->new_fattr;

	nfs_sb_active(old_dir->i_sb);

	NFS_PROTO(data->old_dir)->rename_setup(&msg, old_dentry, new_dentry);

	return rpc_run_task(&task_setup_data);
}

/*
 * Perform tasks needed when a sillyrename is done such as cancelling the
 * queued async unlink if it failed.
 */
static void
nfs_complete_sillyrename(struct rpc_task *task, struct nfs_renamedata *data)
{
	struct dentry *dentry = data->old_dentry;

	if (task->tk_status != 0) {
		nfs_cancel_async_unlink(dentry);
		return;
	}

	/*
	 * vfs_unlink and the like do not issue this when a file is
	 * sillyrenamed, so do it here.
	 */
	fsnotify_nameremove(dentry, 0);
}

#define SILLYNAME_PREFIX ".nfs"
#define SILLYNAME_PREFIX_LEN ((unsigned)sizeof(SILLYNAME_PREFIX) - 1)
#define SILLYNAME_FILEID_LEN ((unsigned)sizeof(u64) << 1)
#define SILLYNAME_COUNTER_LEN ((unsigned)sizeof(unsigned int) << 1)
#define SILLYNAME_LEN (SILLYNAME_PREFIX_LEN + \
		SILLYNAME_FILEID_LEN + \
		SILLYNAME_COUNTER_LEN)

/**
 * nfs_sillyrename - Perform a silly-rename of a dentry
 * @dir: inode of directory that contains dentry
 * @dentry: dentry to be sillyrenamed
 *
 * NFSv2/3 is stateless and the server doesn't know when the client is
 * holding a file open. To prevent application problems when a file is
 * unlinked while it's still open, the client performs a "silly-rename".
 * That is, it renames the file to a hidden file in the same directory,
 * and only performs the unlink once the last reference to it is put.
 *
 * The final cleanup is done during dentry_iput.
 *
 * (Note: NFSv4 is stateful, and has opens, so in theory an NFSv4 server
 * could take responsibility for keeping open files referenced.  The server
 * would also need to ensure that opened-but-deleted files were kept over
 * reboots.  However, we may not assume a server does so.  (RFC 5661
 * does provide an OPEN4_RESULT_PRESERVE_UNLINKED flag that a server can
 * use to advertise that it does this; some day we may take advantage of
 * it.))
 */
int
nfs_sillyrename(struct inode *dir, struct dentry *dentry)
{
	static unsigned int sillycounter;
	unsigned char silly[SILLYNAME_LEN + 1];
	unsigned long long fileid;
	struct dentry *sdentry;
	struct rpc_task *task;
	int            error = -EBUSY;

	dfprintk(VFS, "NFS: silly-rename(%pd2, ct=%d)\n",
		dentry, d_count(dentry));
	nfs_inc_stats(dir, NFSIOS_SILLYRENAME);

	/*
	 * We don't allow a dentry to be silly-renamed twice.
	 */
	if (dentry->d_flags & DCACHE_NFSFS_RENAMED)
		goto out;

	fileid = NFS_FILEID(d_inode(dentry));

	sdentry = NULL;
	do {
		int slen;
		dput(sdentry);
		sillycounter++;
		slen = scnprintf(silly, sizeof(silly),
				SILLYNAME_PREFIX "%0*llx%0*x",
				SILLYNAME_FILEID_LEN, fileid,
				SILLYNAME_COUNTER_LEN, sillycounter);

		dfprintk(VFS, "NFS: trying to rename %pd to %s\n",
				dentry, silly);

		sdentry = lookup_one_len(silly, dentry->d_parent, slen);
		/*
		 * N.B. Better to return EBUSY here ... it could be
		 * dangerous to delete the file while it's in use.
		 */
		if (IS_ERR(sdentry))
			goto out;
	} while (d_inode(sdentry) != NULL); /* need negative lookup */

	/* queue unlink first. Can't do this from rpc_release as it
	 * has to allocate memory
	 */
	error = nfs_async_unlink(dentry, &sdentry->d_name);
	if (error)
		goto out_dput;

	/* run the rename task, undo unlink if it fails */
	task = nfs_async_rename(dir, dir, dentry, sdentry,
					nfs_complete_sillyrename);
	if (IS_ERR(task)) {
		error = -EBUSY;
		nfs_cancel_async_unlink(dentry);
		goto out_dput;
	}

	/* wait for the RPC task to complete, unless a SIGKILL intervenes */
	error = rpc_wait_for_completion_task(task);
	if (error == 0)
		error = task->tk_status;
	switch (error) {
	case 0:
		/* The rename succeeded */
		nfs_set_verifier(dentry, nfs_save_change_attribute(dir));
		d_move(dentry, sdentry);
		break;
	case -ERESTARTSYS:
		/* The result of the rename is unknown. Play it safe by
		 * forcing a new lookup */
		d_drop(dentry);
		d_drop(sdentry);
	}
	rpc_put_task(task);
out_dput:
	dput(sdentry);
out:
	return error;
}
