/*
 * Copyright (c) 2014 Anna Schumaker <Anna.Schumaker@Netapp.com>
 */
#include <linux/fs.h>
#include <linux/sunrpc/sched.h>
#include <linux/nfs.h>
#include <linux/nfs3.h>
#include <linux/nfs4.h>
#include <linux/nfs_xdr.h>
#include <linux/nfs_fs.h>
#include "nfs4_fs.h"
#include "nfs42.h"
#include "iostat.h"
#include "pnfs.h"
#include "internal.h"

#define NFSDBG_FACILITY NFSDBG_PROC

static int _nfs42_proc_fallocate(struct rpc_message *msg, struct file *filep,
		struct nfs_lock_context *lock, loff_t offset, loff_t len)
{
	struct inode *inode = file_inode(filep);
	struct nfs_server *server = NFS_SERVER(inode);
	struct nfs42_falloc_args args = {
		.falloc_fh	= NFS_FH(inode),
		.falloc_offset	= offset,
		.falloc_length	= len,
		.falloc_bitmask	= server->cache_consistency_bitmask,
	};
	struct nfs42_falloc_res res = {
		.falloc_server	= server,
	};
	int status;

	msg->rpc_argp = &args;
	msg->rpc_resp = &res;

	status = nfs4_set_rw_stateid(&args.falloc_stateid, lock->open_context,
			lock, FMODE_WRITE);
	if (status)
		return status;

	res.falloc_fattr = nfs_alloc_fattr();
	if (!res.falloc_fattr)
		return -ENOMEM;

	status = nfs4_call_sync(server->client, server, msg,
				&args.seq_args, &res.seq_res, 0);
	if (status == 0)
		status = nfs_post_op_update_inode(inode, res.falloc_fattr);

	kfree(res.falloc_fattr);
	return status;
}

static int nfs42_proc_fallocate(struct rpc_message *msg, struct file *filep,
				loff_t offset, loff_t len)
{
	struct nfs_server *server = NFS_SERVER(file_inode(filep));
	struct nfs4_exception exception = { };
	struct nfs_lock_context *lock;
	int err;

	lock = nfs_get_lock_context(nfs_file_open_context(filep));
	if (IS_ERR(lock))
		return PTR_ERR(lock);

	exception.inode = file_inode(filep);
	exception.state = lock->open_context->state;

	do {
		err = _nfs42_proc_fallocate(msg, filep, lock, offset, len);
		if (err == -ENOTSUPP) {
			err = -EOPNOTSUPP;
			break;
		}
		err = nfs4_handle_exception(server, err, &exception);
	} while (exception.retry);

	nfs_put_lock_context(lock);
	return err;
}

int nfs42_proc_allocate(struct file *filep, loff_t offset, loff_t len)
{
	struct rpc_message msg = {
		.rpc_proc = &nfs4_procedures[NFSPROC4_CLNT_ALLOCATE],
	};
	struct inode *inode = file_inode(filep);
	int err;

	if (!nfs_server_capable(inode, NFS_CAP_ALLOCATE))
		return -EOPNOTSUPP;

	inode_lock(inode);

	err = nfs42_proc_fallocate(&msg, filep, offset, len);
	if (err == -EOPNOTSUPP)
		NFS_SERVER(inode)->caps &= ~NFS_CAP_ALLOCATE;

	inode_unlock(inode);
	return err;
}

int nfs42_proc_deallocate(struct file *filep, loff_t offset, loff_t len)
{
	struct rpc_message msg = {
		.rpc_proc = &nfs4_procedures[NFSPROC4_CLNT_DEALLOCATE],
	};
	struct inode *inode = file_inode(filep);
	int err;

	if (!nfs_server_capable(inode, NFS_CAP_DEALLOCATE))
		return -EOPNOTSUPP;

	inode_lock(inode);
	err = nfs_sync_inode(inode);
	if (err)
		goto out_unlock;

	err = nfs42_proc_fallocate(&msg, filep, offset, len);
	if (err == 0)
		truncate_pagecache_range(inode, offset, (offset + len) -1);
	if (err == -EOPNOTSUPP)
		NFS_SERVER(inode)->caps &= ~NFS_CAP_DEALLOCATE;
out_unlock:
	inode_unlock(inode);
	return err;
}

static ssize_t _nfs42_proc_copy(struct file *src, loff_t pos_src,
				struct nfs_lock_context *src_lock,
				struct file *dst, loff_t pos_dst,
				struct nfs_lock_context *dst_lock,
				size_t count)
{
	struct nfs42_copy_args args = {
		.src_fh		= NFS_FH(file_inode(src)),
		.src_pos	= pos_src,
		.dst_fh		= NFS_FH(file_inode(dst)),
		.dst_pos	= pos_dst,
		.count		= count,
	};
	struct nfs42_copy_res res;
	struct rpc_message msg = {
		.rpc_proc = &nfs4_procedures[NFSPROC4_CLNT_COPY],
		.rpc_argp = &args,
		.rpc_resp = &res,
	};
	struct inode *dst_inode = file_inode(dst);
	struct nfs_server *server = NFS_SERVER(dst_inode);
	int status;

	status = nfs4_set_rw_stateid(&args.src_stateid, src_lock->open_context,
				     src_lock, FMODE_READ);
	if (status)
		return status;

	status = nfs_filemap_write_and_wait_range(file_inode(src)->i_mapping,
			pos_src, pos_src + (loff_t)count - 1);
	if (status)
		return status;

	status = nfs4_set_rw_stateid(&args.dst_stateid, dst_lock->open_context,
				     dst_lock, FMODE_WRITE);
	if (status)
		return status;

	status = nfs_sync_inode(dst_inode);
	if (status)
		return status;

	status = nfs4_call_sync(server->client, server, &msg,
				&args.seq_args, &res.seq_res, 0);
	if (status == -ENOTSUPP)
		server->caps &= ~NFS_CAP_COPY;
	if (status)
		return status;

	if (res.write_res.verifier.committed != NFS_FILE_SYNC) {
		status = nfs_commit_file(dst, &res.write_res.verifier.verifier);
		if (status)
			return status;
	}

	truncate_pagecache_range(dst_inode, pos_dst,
				 pos_dst + res.write_res.count);

	return res.write_res.count;
}

ssize_t nfs42_proc_copy(struct file *src, loff_t pos_src,
			struct file *dst, loff_t pos_dst,
			size_t count)
{
	struct nfs_server *server = NFS_SERVER(file_inode(dst));
	struct nfs_lock_context *src_lock;
	struct nfs_lock_context *dst_lock;
	struct nfs4_exception src_exception = { };
	struct nfs4_exception dst_exception = { };
	ssize_t err, err2;

	if (!nfs_server_capable(file_inode(dst), NFS_CAP_COPY))
		return -EOPNOTSUPP;

	src_lock = nfs_get_lock_context(nfs_file_open_context(src));
	if (IS_ERR(src_lock))
		return PTR_ERR(src_lock);

	src_exception.inode = file_inode(src);
	src_exception.state = src_lock->open_context->state;

	dst_lock = nfs_get_lock_context(nfs_file_open_context(dst));
	if (IS_ERR(dst_lock)) {
		err = PTR_ERR(dst_lock);
		goto out_put_src_lock;
	}

	dst_exception.inode = file_inode(dst);
	dst_exception.state = dst_lock->open_context->state;

	do {
		inode_lock(file_inode(dst));
		err = _nfs42_proc_copy(src, pos_src, src_lock,
				       dst, pos_dst, dst_lock, count);
		inode_unlock(file_inode(dst));

		if (err == -ENOTSUPP) {
			err = -EOPNOTSUPP;
			break;
		}

		err2 = nfs4_handle_exception(server, err, &src_exception);
		err  = nfs4_handle_exception(server, err, &dst_exception);
		if (!err)
			err = err2;
	} while (src_exception.retry || dst_exception.retry);

	nfs_put_lock_context(dst_lock);
out_put_src_lock:
	nfs_put_lock_context(src_lock);
	return err;
}

static loff_t _nfs42_proc_llseek(struct file *filep,
		struct nfs_lock_context *lock, loff_t offset, int whence)
{
	struct inode *inode = file_inode(filep);
	struct nfs42_seek_args args = {
		.sa_fh		= NFS_FH(inode),
		.sa_offset	= offset,
		.sa_what	= (whence == SEEK_HOLE) ?
					NFS4_CONTENT_HOLE : NFS4_CONTENT_DATA,
	};
	struct nfs42_seek_res res;
	struct rpc_message msg = {
		.rpc_proc = &nfs4_procedures[NFSPROC4_CLNT_SEEK],
		.rpc_argp = &args,
		.rpc_resp = &res,
	};
	struct nfs_server *server = NFS_SERVER(inode);
	int status;

	if (!nfs_server_capable(inode, NFS_CAP_SEEK))
		return -ENOTSUPP;

	status = nfs4_set_rw_stateid(&args.sa_stateid, lock->open_context,
			lock, FMODE_READ);
	if (status)
		return status;

	status = nfs_filemap_write_and_wait_range(inode->i_mapping,
			offset, LLONG_MAX);
	if (status)
		return status;

	status = nfs4_call_sync(server->client, server, &msg,
				&args.seq_args, &res.seq_res, 0);
	if (status == -ENOTSUPP)
		server->caps &= ~NFS_CAP_SEEK;
	if (status)
		return status;

	return vfs_setpos(filep, res.sr_offset, inode->i_sb->s_maxbytes);
}

loff_t nfs42_proc_llseek(struct file *filep, loff_t offset, int whence)
{
	struct nfs_server *server = NFS_SERVER(file_inode(filep));
	struct nfs4_exception exception = { };
	struct nfs_lock_context *lock;
	loff_t err;

	lock = nfs_get_lock_context(nfs_file_open_context(filep));
	if (IS_ERR(lock))
		return PTR_ERR(lock);

	exception.inode = file_inode(filep);
	exception.state = lock->open_context->state;

	do {
		err = _nfs42_proc_llseek(filep, lock, offset, whence);
		if (err >= 0)
			break;
		if (err == -ENOTSUPP) {
			err = -EOPNOTSUPP;
			break;
		}
		err = nfs4_handle_exception(server, err, &exception);
	} while (exception.retry);

	nfs_put_lock_context(lock);
	return err;
}


static void
nfs42_layoutstat_prepare(struct rpc_task *task, void *calldata)
{
	struct nfs42_layoutstat_data *data = calldata;
	struct nfs_server *server = NFS_SERVER(data->args.inode);

	nfs41_setup_sequence(nfs4_get_session(server), &data->args.seq_args,
			     &data->res.seq_res, task);
}

static void
nfs42_layoutstat_done(struct rpc_task *task, void *calldata)
{
	struct nfs42_layoutstat_data *data = calldata;
	struct inode *inode = data->inode;
	struct pnfs_layout_hdr *lo;

	if (!nfs4_sequence_done(task, &data->res.seq_res))
		return;

	switch (task->tk_status) {
	case 0:
		break;
	case -NFS4ERR_EXPIRED:
	case -NFS4ERR_STALE_STATEID:
	case -NFS4ERR_OLD_STATEID:
	case -NFS4ERR_BAD_STATEID:
		spin_lock(&inode->i_lock);
		lo = NFS_I(inode)->layout;
		if (lo && nfs4_stateid_match(&data->args.stateid,
					     &lo->plh_stateid)) {
			LIST_HEAD(head);

			/*
			 * Mark the bad layout state as invalid, then retry
			 * with the current stateid.
			 */
			pnfs_mark_layout_stateid_invalid(lo, &head);
			spin_unlock(&inode->i_lock);
			pnfs_free_lseg_list(&head);
		} else
			spin_unlock(&inode->i_lock);
		break;
	case -ENOTSUPP:
	case -EOPNOTSUPP:
		NFS_SERVER(inode)->caps &= ~NFS_CAP_LAYOUTSTATS;
	default:
		break;
	}

	dprintk("%s server returns %d\n", __func__, task->tk_status);
}

static void
nfs42_layoutstat_release(void *calldata)
{
	struct nfs42_layoutstat_data *data = calldata;
	struct nfs_server *nfss = NFS_SERVER(data->args.inode);

	if (nfss->pnfs_curr_ld->cleanup_layoutstats)
		nfss->pnfs_curr_ld->cleanup_layoutstats(data);

	pnfs_put_layout_hdr(NFS_I(data->args.inode)->layout);
	smp_mb__before_atomic();
	clear_bit(NFS_INO_LAYOUTSTATS, &NFS_I(data->args.inode)->flags);
	smp_mb__after_atomic();
	nfs_iput_and_deactive(data->inode);
	kfree(data->args.devinfo);
	kfree(data);
}

static const struct rpc_call_ops nfs42_layoutstat_ops = {
	.rpc_call_prepare = nfs42_layoutstat_prepare,
	.rpc_call_done = nfs42_layoutstat_done,
	.rpc_release = nfs42_layoutstat_release,
};

int nfs42_proc_layoutstats_generic(struct nfs_server *server,
				   struct nfs42_layoutstat_data *data)
{
	struct rpc_message msg = {
		.rpc_proc = &nfs4_procedures[NFSPROC4_CLNT_LAYOUTSTATS],
		.rpc_argp = &data->args,
		.rpc_resp = &data->res,
	};
	struct rpc_task_setup task_setup = {
		.rpc_client = server->client,
		.rpc_message = &msg,
		.callback_ops = &nfs42_layoutstat_ops,
		.callback_data = data,
		.flags = RPC_TASK_ASYNC,
	};
	struct rpc_task *task;

	data->inode = nfs_igrab_and_active(data->args.inode);
	if (!data->inode) {
		nfs42_layoutstat_release(data);
		return -EAGAIN;
	}
	nfs4_init_sequence(&data->args.seq_args, &data->res.seq_res, 0);
	task = rpc_run_task(&task_setup);
	if (IS_ERR(task))
		return PTR_ERR(task);
	return 0;
}

static int _nfs42_proc_clone(struct rpc_message *msg, struct file *src_f,
		struct file *dst_f, struct nfs_lock_context *src_lock,
		struct nfs_lock_context *dst_lock, loff_t src_offset,
		loff_t dst_offset, loff_t count)
{
	struct inode *src_inode = file_inode(src_f);
	struct inode *dst_inode = file_inode(dst_f);
	struct nfs_server *server = NFS_SERVER(dst_inode);
	struct nfs42_clone_args args = {
		.src_fh = NFS_FH(src_inode),
		.dst_fh = NFS_FH(dst_inode),
		.src_offset = src_offset,
		.dst_offset = dst_offset,
		.count = count,
		.dst_bitmask = server->cache_consistency_bitmask,
	};
	struct nfs42_clone_res res = {
		.server	= server,
	};
	int status;

	msg->rpc_argp = &args;
	msg->rpc_resp = &res;

	status = nfs4_set_rw_stateid(&args.src_stateid, src_lock->open_context,
			src_lock, FMODE_READ);
	if (status)
		return status;

	status = nfs4_set_rw_stateid(&args.dst_stateid, dst_lock->open_context,
			dst_lock, FMODE_WRITE);
	if (status)
		return status;

	res.dst_fattr = nfs_alloc_fattr();
	if (!res.dst_fattr)
		return -ENOMEM;

	status = nfs4_call_sync(server->client, server, msg,
				&args.seq_args, &res.seq_res, 0);
	if (status == 0)
		status = nfs_post_op_update_inode(dst_inode, res.dst_fattr);

	kfree(res.dst_fattr);
	return status;
}

int nfs42_proc_clone(struct file *src_f, struct file *dst_f,
		     loff_t src_offset, loff_t dst_offset, loff_t count)
{
	struct rpc_message msg = {
		.rpc_proc = &nfs4_procedures[NFSPROC4_CLNT_CLONE],
	};
	struct inode *inode = file_inode(src_f);
	struct nfs_server *server = NFS_SERVER(file_inode(src_f));
	struct nfs_lock_context *src_lock;
	struct nfs_lock_context *dst_lock;
	struct nfs4_exception src_exception = { };
	struct nfs4_exception dst_exception = { };
	int err, err2;

	if (!nfs_server_capable(inode, NFS_CAP_CLONE))
		return -EOPNOTSUPP;

	src_lock = nfs_get_lock_context(nfs_file_open_context(src_f));
	if (IS_ERR(src_lock))
		return PTR_ERR(src_lock);

	src_exception.inode = file_inode(src_f);
	src_exception.state = src_lock->open_context->state;

	dst_lock = nfs_get_lock_context(nfs_file_open_context(dst_f));
	if (IS_ERR(dst_lock)) {
		err = PTR_ERR(dst_lock);
		goto out_put_src_lock;
	}

	dst_exception.inode = file_inode(dst_f);
	dst_exception.state = dst_lock->open_context->state;

	do {
		err = _nfs42_proc_clone(&msg, src_f, dst_f, src_lock, dst_lock,
					src_offset, dst_offset, count);
		if (err == -ENOTSUPP || err == -EOPNOTSUPP) {
			NFS_SERVER(inode)->caps &= ~NFS_CAP_CLONE;
			err = -EOPNOTSUPP;
			break;
		}

		err2 = nfs4_handle_exception(server, err, &src_exception);
		err = nfs4_handle_exception(server, err, &dst_exception);
		if (!err)
			err = err2;
	} while (src_exception.retry || dst_exception.retry);

	nfs_put_lock_context(dst_lock);
out_put_src_lock:
	nfs_put_lock_context(src_lock);
	return err;
}
