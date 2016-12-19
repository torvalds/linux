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

static int nfs42_set_rw_stateid(nfs4_stateid *dst, struct file *file,
				fmode_t fmode)
{
	struct nfs_open_context *open;
	struct nfs_lock_context *lock;
	int ret;

	open = get_nfs_open_context(nfs_file_open_context(file));
	lock = nfs_get_lock_context(open);
	if (IS_ERR(lock)) {
		put_nfs_open_context(open);
		return PTR_ERR(lock);
	}

	ret = nfs4_set_rw_stateid(dst, open, lock, fmode);

	nfs_put_lock_context(lock);
	put_nfs_open_context(open);
	return ret;
}

static int _nfs42_proc_fallocate(struct rpc_message *msg, struct file *filep,
				 loff_t offset, loff_t len)
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

	status = nfs42_set_rw_stateid(&args.falloc_stateid, filep, FMODE_WRITE);
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
	int err;

	do {
		err = _nfs42_proc_fallocate(msg, filep, offset, len);
		if (err == -ENOTSUPP)
			return -EOPNOTSUPP;
		err = nfs4_handle_exception(server, err, &exception);
	} while (exception.retry);

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

	mutex_lock(&inode->i_mutex);

	err = nfs42_proc_fallocate(&msg, filep, offset, len);
	if (err == -EOPNOTSUPP)
		NFS_SERVER(inode)->caps &= ~NFS_CAP_ALLOCATE;

	mutex_unlock(&inode->i_mutex);
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

	nfs_wb_all(inode);
	mutex_lock(&inode->i_mutex);

	err = nfs42_proc_fallocate(&msg, filep, offset, len);
	if (err == 0)
		truncate_pagecache_range(inode, offset, (offset + len) -1);
	if (err == -EOPNOTSUPP)
		NFS_SERVER(inode)->caps &= ~NFS_CAP_DEALLOCATE;

	mutex_unlock(&inode->i_mutex);
	return err;
}

static loff_t _nfs42_proc_llseek(struct file *filep, loff_t offset, int whence)
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

	status = nfs42_set_rw_stateid(&args.sa_stateid, filep, FMODE_READ);
	if (status)
		return status;

	nfs_wb_all(inode);
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
	loff_t err;

	do {
		err = _nfs42_proc_llseek(filep, offset, whence);
		if (err >= 0)
			break;
		if (err == -ENOTSUPP)
			return -EOPNOTSUPP;
		err = nfs4_handle_exception(server, err, &exception);
	} while (exception.retry);

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

	if (!nfs4_sequence_done(task, &data->res.seq_res))
		return;

	switch (task->tk_status) {
	case 0:
		break;
	case -ENOTSUPP:
	case -EOPNOTSUPP:
		NFS_SERVER(data->inode)->caps &= ~NFS_CAP_LAYOUTSTATS;
	default:
		dprintk("%s server returns %d\n", __func__, task->tk_status);
	}
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
	rpc_put_task(task);
	return 0;
}

static int _nfs42_proc_clone(struct rpc_message *msg, struct file *src_f,
			     struct file *dst_f, loff_t src_offset,
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

	status = nfs42_set_rw_stateid(&args.src_stateid, src_f, FMODE_READ);
	if (status)
		return status;

	status = nfs42_set_rw_stateid(&args.dst_stateid, dst_f, FMODE_WRITE);
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
	struct nfs4_exception exception = { };
	int err;

	if (!nfs_server_capable(inode, NFS_CAP_CLONE))
		return -EOPNOTSUPP;

	do {
		err = _nfs42_proc_clone(&msg, src_f, dst_f, src_offset,
					dst_offset, count);
		if (err == -ENOTSUPP || err == -EOPNOTSUPP) {
			NFS_SERVER(inode)->caps &= ~NFS_CAP_CLONE;
			return -EOPNOTSUPP;
		}
		err = nfs4_handle_exception(server, err, &exception);
	} while (exception.retry);

	return err;

}
