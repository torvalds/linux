// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2014 Anna Schumaker <Anna.Schumaker@Netapp.com>
 */
#include <linux/fs.h>
#include <linux/sunrpc/addr.h>
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
#include "nfs4session.h"
#include "internal.h"
#include "delegation.h"
#include "nfs4trace.h"

#define NFSDBG_FACILITY NFSDBG_PROC
static int nfs42_do_offload_cancel_async(struct file *dst, nfs4_stateid *std);

static void nfs42_set_netaddr(struct file *filep, struct nfs42_netaddr *naddr)
{
	struct nfs_client *clp = (NFS_SERVER(file_inode(filep)))->nfs_client;
	unsigned short port = 2049;

	rcu_read_lock();
	naddr->netid_len = scnprintf(naddr->netid,
					sizeof(naddr->netid), "%s",
					rpc_peeraddr2str(clp->cl_rpcclient,
					RPC_DISPLAY_NETID));
	naddr->addr_len = scnprintf(naddr->addr,
					sizeof(naddr->addr),
					"%s.%u.%u",
					rpc_peeraddr2str(clp->cl_rpcclient,
					RPC_DISPLAY_ADDR),
					port >> 8, port & 255);
	rcu_read_unlock();
}

static int _nfs42_proc_fallocate(struct rpc_message *msg, struct file *filep,
		struct nfs_lock_context *lock, loff_t offset, loff_t len)
{
	struct inode *inode = file_inode(filep);
	struct nfs_server *server = NFS_SERVER(inode);
	u32 bitmask[3];
	struct nfs42_falloc_args args = {
		.falloc_fh	= NFS_FH(inode),
		.falloc_offset	= offset,
		.falloc_length	= len,
		.falloc_bitmask	= bitmask,
	};
	struct nfs42_falloc_res res = {
		.falloc_server	= server,
	};
	int status;

	msg->rpc_argp = &args;
	msg->rpc_resp = &res;

	status = nfs4_set_rw_stateid(&args.falloc_stateid, lock->open_context,
			lock, FMODE_WRITE);
	if (status) {
		if (status == -EAGAIN)
			status = -NFS4ERR_BAD_STATEID;
		return status;
	}

	memcpy(bitmask, server->cache_consistency_bitmask, sizeof(bitmask));
	if (server->attr_bitmask[1] & FATTR4_WORD1_SPACE_USED)
		bitmask[1] |= FATTR4_WORD1_SPACE_USED;

	res.falloc_fattr = nfs_alloc_fattr();
	if (!res.falloc_fattr)
		return -ENOMEM;

	status = nfs4_call_sync(server->client, server, msg,
				&args.seq_args, &res.seq_res, 0);
	if (status == 0)
		status = nfs_post_op_update_inode_force_wcc(inode,
							    res.falloc_fattr);

	if (msg->rpc_proc == &nfs4_procedures[NFSPROC4_CLNT_ALLOCATE])
		trace_nfs4_fallocate(inode, &args, status);
	else
		trace_nfs4_deallocate(inode, &args, status);
	kfree(res.falloc_fattr);
	return status;
}

static int nfs42_proc_fallocate(struct rpc_message *msg, struct file *filep,
				loff_t offset, loff_t len)
{
	struct inode *inode = file_inode(filep);
	struct nfs_server *server = NFS_SERVER(inode);
	struct nfs4_exception exception = { };
	struct nfs_lock_context *lock;
	int err;

	lock = nfs_get_lock_context(nfs_file_open_context(filep));
	if (IS_ERR(lock))
		return PTR_ERR(lock);

	exception.inode = inode;
	exception.state = lock->open_context->state;

	err = nfs_sync_inode(inode);
	if (err)
		goto out;

	do {
		err = _nfs42_proc_fallocate(msg, filep, lock, offset, len);
		if (err == -ENOTSUPP) {
			err = -EOPNOTSUPP;
			break;
		}
		err = nfs4_handle_exception(server, err, &exception);
	} while (exception.retry);
out:
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

	err = nfs42_proc_fallocate(&msg, filep, offset, len);
	if (err == 0)
		truncate_pagecache_range(inode, offset, (offset + len) -1);
	if (err == -EOPNOTSUPP)
		NFS_SERVER(inode)->caps &= ~NFS_CAP_DEALLOCATE;

	inode_unlock(inode);
	return err;
}

static int handle_async_copy(struct nfs42_copy_res *res,
			     struct nfs_server *dst_server,
			     struct nfs_server *src_server,
			     struct file *src,
			     struct file *dst,
			     nfs4_stateid *src_stateid,
			     bool *restart)
{
	struct nfs4_copy_state *copy, *tmp_copy;
	int status = NFS4_OK;
	bool found_pending = false;
	struct nfs_open_context *dst_ctx = nfs_file_open_context(dst);
	struct nfs_open_context *src_ctx = nfs_file_open_context(src);

	copy = kzalloc(sizeof(struct nfs4_copy_state), GFP_NOFS);
	if (!copy)
		return -ENOMEM;

	spin_lock(&dst_server->nfs_client->cl_lock);
	list_for_each_entry(tmp_copy,
				&dst_server->nfs_client->pending_cb_stateids,
				copies) {
		if (memcmp(&res->write_res.stateid, &tmp_copy->stateid,
				NFS4_STATEID_SIZE))
			continue;
		found_pending = true;
		list_del(&tmp_copy->copies);
		break;
	}
	if (found_pending) {
		spin_unlock(&dst_server->nfs_client->cl_lock);
		kfree(copy);
		copy = tmp_copy;
		goto out;
	}

	memcpy(&copy->stateid, &res->write_res.stateid, NFS4_STATEID_SIZE);
	init_completion(&copy->completion);
	copy->parent_dst_state = dst_ctx->state;
	copy->parent_src_state = src_ctx->state;

	list_add_tail(&copy->copies, &dst_server->ss_copies);
	spin_unlock(&dst_server->nfs_client->cl_lock);

	if (dst_server != src_server) {
		spin_lock(&src_server->nfs_client->cl_lock);
		list_add_tail(&copy->src_copies, &src_server->ss_copies);
		spin_unlock(&src_server->nfs_client->cl_lock);
	}

	status = wait_for_completion_interruptible(&copy->completion);
	spin_lock(&dst_server->nfs_client->cl_lock);
	list_del_init(&copy->copies);
	spin_unlock(&dst_server->nfs_client->cl_lock);
	if (dst_server != src_server) {
		spin_lock(&src_server->nfs_client->cl_lock);
		list_del_init(&copy->src_copies);
		spin_unlock(&src_server->nfs_client->cl_lock);
	}
	if (status == -ERESTARTSYS) {
		goto out_cancel;
	} else if (copy->flags || copy->error == NFS4ERR_PARTNER_NO_AUTH) {
		status = -EAGAIN;
		*restart = true;
		goto out_cancel;
	}
out:
	res->write_res.count = copy->count;
	memcpy(&res->write_res.verifier, &copy->verf, sizeof(copy->verf));
	status = -copy->error;

out_free:
	kfree(copy);
	return status;
out_cancel:
	nfs42_do_offload_cancel_async(dst, &copy->stateid);
	if (!nfs42_files_from_same_server(src, dst))
		nfs42_do_offload_cancel_async(src, src_stateid);
	goto out_free;
}

static int process_copy_commit(struct file *dst, loff_t pos_dst,
			       struct nfs42_copy_res *res)
{
	struct nfs_commitres cres;
	int status = -ENOMEM;

	cres.verf = kzalloc(sizeof(struct nfs_writeverf), GFP_NOFS);
	if (!cres.verf)
		goto out;

	status = nfs4_proc_commit(dst, pos_dst, res->write_res.count, &cres);
	if (status)
		goto out_free;
	if (nfs_write_verifier_cmp(&res->write_res.verifier.verifier,
				    &cres.verf->verifier)) {
		dprintk("commit verf differs from copy verf\n");
		status = -EAGAIN;
	}
out_free:
	kfree(cres.verf);
out:
	return status;
}

/**
 * nfs42_copy_dest_done - perform inode cache updates after clone/copy offload
 * @inode: pointer to destination inode
 * @pos: destination offset
 * @len: copy length
 *
 * Punch a hole in the inode page cache, so that the NFS client will
 * know to retrieve new data.
 * Update the file size if necessary, and then mark the inode as having
 * invalid cached values for change attribute, ctime, mtime and space used.
 */
static void nfs42_copy_dest_done(struct inode *inode, loff_t pos, loff_t len)
{
	loff_t newsize = pos + len;
	loff_t end = newsize - 1;

	truncate_pagecache_range(inode, pos, end);
	spin_lock(&inode->i_lock);
	if (newsize > i_size_read(inode))
		i_size_write(inode, newsize);
	nfs_set_cache_invalid(inode, NFS_INO_INVALID_CHANGE |
					     NFS_INO_INVALID_CTIME |
					     NFS_INO_INVALID_MTIME |
					     NFS_INO_INVALID_BLOCKS);
	spin_unlock(&inode->i_lock);
}

static ssize_t _nfs42_proc_copy(struct file *src,
				struct nfs_lock_context *src_lock,
				struct file *dst,
				struct nfs_lock_context *dst_lock,
				struct nfs42_copy_args *args,
				struct nfs42_copy_res *res,
				struct nl4_server *nss,
				nfs4_stateid *cnr_stateid,
				bool *restart)
{
	struct rpc_message msg = {
		.rpc_proc = &nfs4_procedures[NFSPROC4_CLNT_COPY],
		.rpc_argp = args,
		.rpc_resp = res,
	};
	struct inode *dst_inode = file_inode(dst);
	struct inode *src_inode = file_inode(src);
	struct nfs_server *dst_server = NFS_SERVER(dst_inode);
	struct nfs_server *src_server = NFS_SERVER(src_inode);
	loff_t pos_src = args->src_pos;
	loff_t pos_dst = args->dst_pos;
	size_t count = args->count;
	ssize_t status;

	if (nss) {
		args->cp_src = nss;
		nfs4_stateid_copy(&args->src_stateid, cnr_stateid);
	} else {
		status = nfs4_set_rw_stateid(&args->src_stateid,
				src_lock->open_context, src_lock, FMODE_READ);
		if (status) {
			if (status == -EAGAIN)
				status = -NFS4ERR_BAD_STATEID;
			return status;
		}
	}
	status = nfs_filemap_write_and_wait_range(file_inode(src)->i_mapping,
			pos_src, pos_src + (loff_t)count - 1);
	if (status)
		return status;

	status = nfs4_set_rw_stateid(&args->dst_stateid, dst_lock->open_context,
				     dst_lock, FMODE_WRITE);
	if (status) {
		if (status == -EAGAIN)
			status = -NFS4ERR_BAD_STATEID;
		return status;
	}

	status = nfs_sync_inode(dst_inode);
	if (status)
		return status;

	res->commit_res.verf = NULL;
	if (args->sync) {
		res->commit_res.verf =
			kzalloc(sizeof(struct nfs_writeverf), GFP_NOFS);
		if (!res->commit_res.verf)
			return -ENOMEM;
	}
	set_bit(NFS_CLNT_SRC_SSC_COPY_STATE,
		&src_lock->open_context->state->flags);
	set_bit(NFS_CLNT_DST_SSC_COPY_STATE,
		&dst_lock->open_context->state->flags);

	status = nfs4_call_sync(dst_server->client, dst_server, &msg,
				&args->seq_args, &res->seq_res, 0);
	trace_nfs4_copy(src_inode, dst_inode, args, res, nss, status);
	if (status == -ENOTSUPP)
		dst_server->caps &= ~NFS_CAP_COPY;
	if (status)
		goto out;

	if (args->sync &&
		nfs_write_verifier_cmp(&res->write_res.verifier.verifier,
				    &res->commit_res.verf->verifier)) {
		status = -EAGAIN;
		goto out;
	}

	if (!res->synchronous) {
		status = handle_async_copy(res, dst_server, src_server, src,
				dst, &args->src_stateid, restart);
		if (status)
			goto out;
	}

	if ((!res->synchronous || !args->sync) &&
			res->write_res.verifier.committed != NFS_FILE_SYNC) {
		status = process_copy_commit(dst, pos_dst, res);
		if (status)
			goto out;
	}

	nfs42_copy_dest_done(dst_inode, pos_dst, res->write_res.count);
	nfs_invalidate_atime(src_inode);
	status = res->write_res.count;
out:
	if (args->sync)
		kfree(res->commit_res.verf);
	return status;
}

ssize_t nfs42_proc_copy(struct file *src, loff_t pos_src,
			struct file *dst, loff_t pos_dst, size_t count,
			struct nl4_server *nss,
			nfs4_stateid *cnr_stateid, bool sync)
{
	struct nfs_server *server = NFS_SERVER(file_inode(dst));
	struct nfs_lock_context *src_lock;
	struct nfs_lock_context *dst_lock;
	struct nfs42_copy_args args = {
		.src_fh		= NFS_FH(file_inode(src)),
		.src_pos	= pos_src,
		.dst_fh		= NFS_FH(file_inode(dst)),
		.dst_pos	= pos_dst,
		.count		= count,
		.sync		= sync,
	};
	struct nfs42_copy_res res;
	struct nfs4_exception src_exception = {
		.inode		= file_inode(src),
		.stateid	= &args.src_stateid,
	};
	struct nfs4_exception dst_exception = {
		.inode		= file_inode(dst),
		.stateid	= &args.dst_stateid,
	};
	ssize_t err, err2;
	bool restart = false;

	src_lock = nfs_get_lock_context(nfs_file_open_context(src));
	if (IS_ERR(src_lock))
		return PTR_ERR(src_lock);

	src_exception.state = src_lock->open_context->state;

	dst_lock = nfs_get_lock_context(nfs_file_open_context(dst));
	if (IS_ERR(dst_lock)) {
		err = PTR_ERR(dst_lock);
		goto out_put_src_lock;
	}

	dst_exception.state = dst_lock->open_context->state;

	do {
		inode_lock(file_inode(dst));
		err = _nfs42_proc_copy(src, src_lock,
				dst, dst_lock,
				&args, &res,
				nss, cnr_stateid, &restart);
		inode_unlock(file_inode(dst));

		if (err >= 0)
			break;
		if (err == -ENOTSUPP &&
				nfs42_files_from_same_server(src, dst)) {
			err = -EOPNOTSUPP;
			break;
		} else if (err == -EAGAIN) {
			if (!restart) {
				dst_exception.retry = 1;
				continue;
			}
			break;
		} else if (err == -NFS4ERR_OFFLOAD_NO_REQS && !args.sync) {
			args.sync = true;
			dst_exception.retry = 1;
			continue;
		} else if ((err == -ESTALE ||
				err == -NFS4ERR_OFFLOAD_DENIED ||
				err == -ENOTSUPP) &&
				!nfs42_files_from_same_server(src, dst)) {
			nfs42_do_offload_cancel_async(src, &args.src_stateid);
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

struct nfs42_offloadcancel_data {
	struct nfs_server *seq_server;
	struct nfs42_offload_status_args args;
	struct nfs42_offload_status_res res;
};

static void nfs42_offload_cancel_prepare(struct rpc_task *task, void *calldata)
{
	struct nfs42_offloadcancel_data *data = calldata;

	nfs4_setup_sequence(data->seq_server->nfs_client,
				&data->args.osa_seq_args,
				&data->res.osr_seq_res, task);
}

static void nfs42_offload_cancel_done(struct rpc_task *task, void *calldata)
{
	struct nfs42_offloadcancel_data *data = calldata;

	nfs41_sequence_done(task, &data->res.osr_seq_res);
	if (task->tk_status &&
		nfs4_async_handle_error(task, data->seq_server, NULL,
			NULL) == -EAGAIN)
		rpc_restart_call_prepare(task);
}

static void nfs42_free_offloadcancel_data(void *data)
{
	kfree(data);
}

static const struct rpc_call_ops nfs42_offload_cancel_ops = {
	.rpc_call_prepare = nfs42_offload_cancel_prepare,
	.rpc_call_done = nfs42_offload_cancel_done,
	.rpc_release = nfs42_free_offloadcancel_data,
};

static int nfs42_do_offload_cancel_async(struct file *dst,
					 nfs4_stateid *stateid)
{
	struct nfs_server *dst_server = NFS_SERVER(file_inode(dst));
	struct nfs42_offloadcancel_data *data = NULL;
	struct nfs_open_context *ctx = nfs_file_open_context(dst);
	struct rpc_task *task;
	struct rpc_message msg = {
		.rpc_proc = &nfs4_procedures[NFSPROC4_CLNT_OFFLOAD_CANCEL],
		.rpc_cred = ctx->cred,
	};
	struct rpc_task_setup task_setup_data = {
		.rpc_client = dst_server->client,
		.rpc_message = &msg,
		.callback_ops = &nfs42_offload_cancel_ops,
		.workqueue = nfsiod_workqueue,
		.flags = RPC_TASK_ASYNC,
	};
	int status;

	if (!(dst_server->caps & NFS_CAP_OFFLOAD_CANCEL))
		return -EOPNOTSUPP;

	data = kzalloc(sizeof(struct nfs42_offloadcancel_data), GFP_NOFS);
	if (data == NULL)
		return -ENOMEM;

	data->seq_server = dst_server;
	data->args.osa_src_fh = NFS_FH(file_inode(dst));
	memcpy(&data->args.osa_stateid, stateid,
		sizeof(data->args.osa_stateid));
	msg.rpc_argp = &data->args;
	msg.rpc_resp = &data->res;
	task_setup_data.callback_data = data;
	nfs4_init_sequence(&data->args.osa_seq_args, &data->res.osr_seq_res,
			   1, 0);
	task = rpc_run_task(&task_setup_data);
	if (IS_ERR(task))
		return PTR_ERR(task);
	status = rpc_wait_for_completion_task(task);
	if (status == -ENOTSUPP)
		dst_server->caps &= ~NFS_CAP_OFFLOAD_CANCEL;
	rpc_put_task(task);
	return status;
}

static int _nfs42_proc_copy_notify(struct file *src, struct file *dst,
				   struct nfs42_copy_notify_args *args,
				   struct nfs42_copy_notify_res *res)
{
	struct nfs_server *src_server = NFS_SERVER(file_inode(src));
	struct rpc_message msg = {
		.rpc_proc = &nfs4_procedures[NFSPROC4_CLNT_COPY_NOTIFY],
		.rpc_argp = args,
		.rpc_resp = res,
	};
	int status;
	struct nfs_open_context *ctx;
	struct nfs_lock_context *l_ctx;

	ctx = get_nfs_open_context(nfs_file_open_context(src));
	l_ctx = nfs_get_lock_context(ctx);
	if (IS_ERR(l_ctx))
		return PTR_ERR(l_ctx);

	status = nfs4_set_rw_stateid(&args->cna_src_stateid, ctx, l_ctx,
				     FMODE_READ);
	nfs_put_lock_context(l_ctx);
	if (status) {
		if (status == -EAGAIN)
			status = -NFS4ERR_BAD_STATEID;
		return status;
	}

	status = nfs4_call_sync(src_server->client, src_server, &msg,
				&args->cna_seq_args, &res->cnr_seq_res, 0);
	if (status == -ENOTSUPP)
		src_server->caps &= ~NFS_CAP_COPY_NOTIFY;

	put_nfs_open_context(nfs_file_open_context(src));
	return status;
}

int nfs42_proc_copy_notify(struct file *src, struct file *dst,
				struct nfs42_copy_notify_res *res)
{
	struct nfs_server *src_server = NFS_SERVER(file_inode(src));
	struct nfs42_copy_notify_args *args;
	struct nfs4_exception exception = {
		.inode = file_inode(src),
	};
	int status;

	if (!(src_server->caps & NFS_CAP_COPY_NOTIFY))
		return -EOPNOTSUPP;

	args = kzalloc(sizeof(struct nfs42_copy_notify_args), GFP_NOFS);
	if (args == NULL)
		return -ENOMEM;

	args->cna_src_fh  = NFS_FH(file_inode(src)),
	args->cna_dst.nl4_type = NL4_NETADDR;
	nfs42_set_netaddr(dst, &args->cna_dst.u.nl4_addr);
	exception.stateid = &args->cna_src_stateid;

	do {
		status = _nfs42_proc_copy_notify(src, dst, args, res);
		if (status == -ENOTSUPP) {
			status = -EOPNOTSUPP;
			goto out;
		}
		status = nfs4_handle_exception(src_server, status, &exception);
	} while (exception.retry);

out:
	kfree(args);
	return status;
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
	if (status) {
		if (status == -EAGAIN)
			status = -NFS4ERR_BAD_STATEID;
		return status;
	}

	status = nfs_filemap_write_and_wait_range(inode->i_mapping,
			offset, LLONG_MAX);
	if (status)
		return status;

	status = nfs4_call_sync(server->client, server, &msg,
				&args.seq_args, &res.seq_res, 0);
	trace_nfs4_llseek(inode, &args, &res, status);
	if (status == -ENOTSUPP)
		server->caps &= ~NFS_CAP_SEEK;
	if (status)
		return status;

	if (whence == SEEK_DATA && res.sr_eof)
		return -NFS4ERR_NXIO;
	else
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
	struct inode *inode = data->inode;
	struct nfs_server *server = NFS_SERVER(inode);
	struct pnfs_layout_hdr *lo;

	spin_lock(&inode->i_lock);
	lo = NFS_I(inode)->layout;
	if (!pnfs_layout_is_valid(lo)) {
		spin_unlock(&inode->i_lock);
		rpc_exit(task, 0);
		return;
	}
	nfs4_stateid_copy(&data->args.stateid, &lo->plh_stateid);
	spin_unlock(&inode->i_lock);
	nfs4_setup_sequence(server->nfs_client, &data->args.seq_args,
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
		return;
	case -NFS4ERR_BADHANDLE:
	case -ESTALE:
		pnfs_destroy_layout(NFS_I(inode));
		break;
	case -NFS4ERR_EXPIRED:
	case -NFS4ERR_ADMIN_REVOKED:
	case -NFS4ERR_DELEG_REVOKED:
	case -NFS4ERR_STALE_STATEID:
	case -NFS4ERR_BAD_STATEID:
		spin_lock(&inode->i_lock);
		lo = NFS_I(inode)->layout;
		if (pnfs_layout_is_valid(lo) &&
		    nfs4_stateid_match(&data->args.stateid,
					     &lo->plh_stateid)) {
			LIST_HEAD(head);

			/*
			 * Mark the bad layout state as invalid, then retry
			 * with the current stateid.
			 */
			pnfs_mark_layout_stateid_invalid(lo, &head);
			spin_unlock(&inode->i_lock);
			pnfs_free_lseg_list(&head);
			nfs_commit_inode(inode, 0);
		} else
			spin_unlock(&inode->i_lock);
		break;
	case -NFS4ERR_OLD_STATEID:
		spin_lock(&inode->i_lock);
		lo = NFS_I(inode)->layout;
		if (pnfs_layout_is_valid(lo) &&
		    nfs4_stateid_match_other(&data->args.stateid,
					&lo->plh_stateid)) {
			/* Do we need to delay before resending? */
			if (!nfs4_stateid_is_newer(&lo->plh_stateid,
						&data->args.stateid))
				rpc_delay(task, HZ);
			rpc_restart_call_prepare(task);
		}
		spin_unlock(&inode->i_lock);
		break;
	case -ENOTSUPP:
	case -EOPNOTSUPP:
		NFS_SERVER(inode)->caps &= ~NFS_CAP_LAYOUTSTATS;
	}

	trace_nfs4_layoutstats(inode, &data->args.stateid, task->tk_status);
}

static void
nfs42_layoutstat_release(void *calldata)
{
	struct nfs42_layoutstat_data *data = calldata;
	struct nfs42_layoutstat_devinfo *devinfo = data->args.devinfo;
	int i;

	for (i = 0; i < data->args.num_dev; i++) {
		if (devinfo[i].ld_private.ops && devinfo[i].ld_private.ops->free)
			devinfo[i].ld_private.ops->free(&devinfo[i].ld_private);
	}

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
	nfs4_init_sequence(&data->args.seq_args, &data->res.seq_res, 0, 0);
	task = rpc_run_task(&task_setup);
	if (IS_ERR(task))
		return PTR_ERR(task);
	rpc_put_task(task);
	return 0;
}

static struct nfs42_layouterror_data *
nfs42_alloc_layouterror_data(struct pnfs_layout_segment *lseg, gfp_t gfp_flags)
{
	struct nfs42_layouterror_data *data;
	struct inode *inode = lseg->pls_layout->plh_inode;

	data = kzalloc(sizeof(*data), gfp_flags);
	if (data) {
		data->args.inode = data->inode = nfs_igrab_and_active(inode);
		if (data->inode) {
			data->lseg = pnfs_get_lseg(lseg);
			if (data->lseg)
				return data;
			nfs_iput_and_deactive(data->inode);
		}
		kfree(data);
	}
	return NULL;
}

static void
nfs42_free_layouterror_data(struct nfs42_layouterror_data *data)
{
	pnfs_put_lseg(data->lseg);
	nfs_iput_and_deactive(data->inode);
	kfree(data);
}

static void
nfs42_layouterror_prepare(struct rpc_task *task, void *calldata)
{
	struct nfs42_layouterror_data *data = calldata;
	struct inode *inode = data->inode;
	struct nfs_server *server = NFS_SERVER(inode);
	struct pnfs_layout_hdr *lo = data->lseg->pls_layout;
	unsigned i;

	spin_lock(&inode->i_lock);
	if (!pnfs_layout_is_valid(lo)) {
		spin_unlock(&inode->i_lock);
		rpc_exit(task, 0);
		return;
	}
	for (i = 0; i < data->args.num_errors; i++)
		nfs4_stateid_copy(&data->args.errors[i].stateid,
				&lo->plh_stateid);
	spin_unlock(&inode->i_lock);
	nfs4_setup_sequence(server->nfs_client, &data->args.seq_args,
			    &data->res.seq_res, task);
}

static void
nfs42_layouterror_done(struct rpc_task *task, void *calldata)
{
	struct nfs42_layouterror_data *data = calldata;
	struct inode *inode = data->inode;
	struct pnfs_layout_hdr *lo = data->lseg->pls_layout;

	if (!nfs4_sequence_done(task, &data->res.seq_res))
		return;

	switch (task->tk_status) {
	case 0:
		return;
	case -NFS4ERR_BADHANDLE:
	case -ESTALE:
		pnfs_destroy_layout(NFS_I(inode));
		break;
	case -NFS4ERR_EXPIRED:
	case -NFS4ERR_ADMIN_REVOKED:
	case -NFS4ERR_DELEG_REVOKED:
	case -NFS4ERR_STALE_STATEID:
	case -NFS4ERR_BAD_STATEID:
		spin_lock(&inode->i_lock);
		if (pnfs_layout_is_valid(lo) &&
		    nfs4_stateid_match(&data->args.errors[0].stateid,
					     &lo->plh_stateid)) {
			LIST_HEAD(head);

			/*
			 * Mark the bad layout state as invalid, then retry
			 * with the current stateid.
			 */
			pnfs_mark_layout_stateid_invalid(lo, &head);
			spin_unlock(&inode->i_lock);
			pnfs_free_lseg_list(&head);
			nfs_commit_inode(inode, 0);
		} else
			spin_unlock(&inode->i_lock);
		break;
	case -NFS4ERR_OLD_STATEID:
		spin_lock(&inode->i_lock);
		if (pnfs_layout_is_valid(lo) &&
		    nfs4_stateid_match_other(&data->args.errors[0].stateid,
					&lo->plh_stateid)) {
			/* Do we need to delay before resending? */
			if (!nfs4_stateid_is_newer(&lo->plh_stateid,
						&data->args.errors[0].stateid))
				rpc_delay(task, HZ);
			rpc_restart_call_prepare(task);
		}
		spin_unlock(&inode->i_lock);
		break;
	case -ENOTSUPP:
	case -EOPNOTSUPP:
		NFS_SERVER(inode)->caps &= ~NFS_CAP_LAYOUTERROR;
	}

	trace_nfs4_layouterror(inode, &data->args.errors[0].stateid,
			       task->tk_status);
}

static void
nfs42_layouterror_release(void *calldata)
{
	struct nfs42_layouterror_data *data = calldata;

	nfs42_free_layouterror_data(data);
}

static const struct rpc_call_ops nfs42_layouterror_ops = {
	.rpc_call_prepare = nfs42_layouterror_prepare,
	.rpc_call_done = nfs42_layouterror_done,
	.rpc_release = nfs42_layouterror_release,
};

int nfs42_proc_layouterror(struct pnfs_layout_segment *lseg,
		const struct nfs42_layout_error *errors, size_t n)
{
	struct inode *inode = lseg->pls_layout->plh_inode;
	struct nfs42_layouterror_data *data;
	struct rpc_task *task;
	struct rpc_message msg = {
		.rpc_proc = &nfs4_procedures[NFSPROC4_CLNT_LAYOUTERROR],
	};
	struct rpc_task_setup task_setup = {
		.rpc_message = &msg,
		.callback_ops = &nfs42_layouterror_ops,
		.flags = RPC_TASK_ASYNC,
	};
	unsigned int i;

	if (!nfs_server_capable(inode, NFS_CAP_LAYOUTERROR))
		return -EOPNOTSUPP;
	if (n > NFS42_LAYOUTERROR_MAX)
		return -EINVAL;
	data = nfs42_alloc_layouterror_data(lseg, GFP_NOFS);
	if (!data)
		return -ENOMEM;
	for (i = 0; i < n; i++) {
		data->args.errors[i] = errors[i];
		data->args.num_errors++;
		data->res.num_errors++;
	}
	msg.rpc_argp = &data->args;
	msg.rpc_resp = &data->res;
	task_setup.callback_data = data;
	task_setup.rpc_client = NFS_SERVER(inode)->client;
	nfs4_init_sequence(&data->args.seq_args, &data->res.seq_res, 0, 0);
	task = rpc_run_task(&task_setup);
	if (IS_ERR(task))
		return PTR_ERR(task);
	rpc_put_task(task);
	return 0;
}
EXPORT_SYMBOL_GPL(nfs42_proc_layouterror);

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
	if (status) {
		if (status == -EAGAIN)
			status = -NFS4ERR_BAD_STATEID;
		return status;
	}
	status = nfs4_set_rw_stateid(&args.dst_stateid, dst_lock->open_context,
			dst_lock, FMODE_WRITE);
	if (status) {
		if (status == -EAGAIN)
			status = -NFS4ERR_BAD_STATEID;
		return status;
	}

	res.dst_fattr = nfs_alloc_fattr();
	if (!res.dst_fattr)
		return -ENOMEM;

	status = nfs4_call_sync(server->client, server, msg,
				&args.seq_args, &res.seq_res, 0);
	trace_nfs4_clone(src_inode, dst_inode, &args, status);
	if (status == 0) {
		nfs42_copy_dest_done(dst_inode, dst_offset, count);
		status = nfs_post_op_update_inode(dst_inode, res.dst_fattr);
	}

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

#define NFS4XATTR_MAXPAGES DIV_ROUND_UP(XATTR_SIZE_MAX, PAGE_SIZE)

static int _nfs42_proc_removexattr(struct inode *inode, const char *name)
{
	struct nfs_server *server = NFS_SERVER(inode);
	struct nfs42_removexattrargs args = {
		.fh = NFS_FH(inode),
		.xattr_name = name,
	};
	struct nfs42_removexattrres res;
	struct rpc_message msg = {
		.rpc_proc = &nfs4_procedures[NFSPROC4_CLNT_REMOVEXATTR],
		.rpc_argp = &args,
		.rpc_resp = &res,
	};
	int ret;
	unsigned long timestamp = jiffies;

	ret = nfs4_call_sync(server->client, server, &msg, &args.seq_args,
	    &res.seq_res, 1);
	if (!ret)
		nfs4_update_changeattr(inode, &res.cinfo, timestamp, 0);

	return ret;
}

static int _nfs42_proc_setxattr(struct inode *inode, const char *name,
				const void *buf, size_t buflen, int flags)
{
	struct nfs_server *server = NFS_SERVER(inode);
	struct page *pages[NFS4XATTR_MAXPAGES];
	struct nfs42_setxattrargs arg = {
		.fh		= NFS_FH(inode),
		.xattr_pages	= pages,
		.xattr_len	= buflen,
		.xattr_name	= name,
		.xattr_flags	= flags,
	};
	struct nfs42_setxattrres res;
	struct rpc_message msg = {
		.rpc_proc	= &nfs4_procedures[NFSPROC4_CLNT_SETXATTR],
		.rpc_argp	= &arg,
		.rpc_resp	= &res,
	};
	int ret, np;
	unsigned long timestamp = jiffies;

	if (buflen > server->sxasize)
		return -ERANGE;

	if (buflen > 0) {
		np = nfs4_buf_to_pages_noslab(buf, buflen, arg.xattr_pages);
		if (np < 0)
			return np;
	} else
		np = 0;

	ret = nfs4_call_sync(server->client, server, &msg, &arg.seq_args,
	    &res.seq_res, 1);

	for (; np > 0; np--)
		put_page(pages[np - 1]);

	if (!ret)
		nfs4_update_changeattr(inode, &res.cinfo, timestamp, 0);

	return ret;
}

static ssize_t _nfs42_proc_getxattr(struct inode *inode, const char *name,
				void *buf, size_t buflen, struct page **pages,
				size_t plen)
{
	struct nfs_server *server = NFS_SERVER(inode);
	struct nfs42_getxattrargs arg = {
		.fh		= NFS_FH(inode),
		.xattr_name	= name,
	};
	struct nfs42_getxattrres res;
	struct rpc_message msg = {
		.rpc_proc	= &nfs4_procedures[NFSPROC4_CLNT_GETXATTR],
		.rpc_argp	= &arg,
		.rpc_resp	= &res,
	};
	ssize_t ret;

	arg.xattr_len = plen;
	arg.xattr_pages = pages;

	ret = nfs4_call_sync(server->client, server, &msg, &arg.seq_args,
	    &res.seq_res, 0);
	if (ret < 0)
		return ret;

	/*
	 * Normally, the caching is done one layer up, but for successful
	 * RPCS, always cache the result here, even if the caller was
	 * just querying the length, or if the reply was too big for
	 * the caller. This avoids a second RPC in the case of the
	 * common query-alloc-retrieve cycle for xattrs.
	 *
	 * Note that xattr_len is always capped to XATTR_SIZE_MAX.
	 */

	nfs4_xattr_cache_add(inode, name, NULL, pages, res.xattr_len);

	if (buflen) {
		if (res.xattr_len > buflen)
			return -ERANGE;
		_copy_from_pages(buf, pages, 0, res.xattr_len);
	}

	return res.xattr_len;
}

static ssize_t _nfs42_proc_listxattrs(struct inode *inode, void *buf,
				 size_t buflen, u64 *cookiep, bool *eofp)
{
	struct nfs_server *server = NFS_SERVER(inode);
	struct page **pages;
	struct nfs42_listxattrsargs arg = {
		.fh		= NFS_FH(inode),
		.cookie		= *cookiep,
	};
	struct nfs42_listxattrsres res = {
		.eof = false,
		.xattr_buf = buf,
		.xattr_len = buflen,
	};
	struct rpc_message msg = {
		.rpc_proc	= &nfs4_procedures[NFSPROC4_CLNT_LISTXATTRS],
		.rpc_argp	= &arg,
		.rpc_resp	= &res,
	};
	u32 xdrlen;
	int ret, np, i;


	ret = -ENOMEM;
	res.scratch = alloc_page(GFP_KERNEL);
	if (!res.scratch)
		goto out;

	xdrlen = nfs42_listxattr_xdrsize(buflen);
	if (xdrlen > server->lxasize)
		xdrlen = server->lxasize;
	np = xdrlen / PAGE_SIZE + 1;

	pages = kcalloc(np, sizeof(struct page *), GFP_KERNEL);
	if (!pages)
		goto out_free_scratch;
	for (i = 0; i < np; i++) {
		pages[i] = alloc_page(GFP_KERNEL);
		if (!pages[i])
			goto out_free_pages;
	}

	arg.xattr_pages = pages;
	arg.count = xdrlen;

	ret = nfs4_call_sync(server->client, server, &msg, &arg.seq_args,
	    &res.seq_res, 0);

	if (ret >= 0) {
		ret = res.copied;
		*cookiep = res.cookie;
		*eofp = res.eof;
	}

out_free_pages:
	while (--np >= 0) {
		if (pages[np])
			__free_page(pages[np]);
	}
	kfree(pages);
out_free_scratch:
	__free_page(res.scratch);
out:
	return ret;

}

ssize_t nfs42_proc_getxattr(struct inode *inode, const char *name,
			      void *buf, size_t buflen)
{
	struct nfs4_exception exception = { };
	ssize_t err, np, i;
	struct page **pages;

	np = nfs_page_array_len(0, buflen ?: XATTR_SIZE_MAX);
	pages = kmalloc_array(np, sizeof(*pages), GFP_KERNEL);
	if (!pages)
		return -ENOMEM;

	for (i = 0; i < np; i++) {
		pages[i] = alloc_page(GFP_KERNEL);
		if (!pages[i]) {
			np = i + 1;
			err = -ENOMEM;
			goto out;
		}
	}

	/*
	 * The GETXATTR op has no length field in the call, and the
	 * xattr data is at the end of the reply.
	 *
	 * There is no downside in using the page-aligned length. It will
	 * allow receiving and caching xattrs that are too large for the
	 * caller but still fit in the page-rounded value.
	 */
	do {
		err = _nfs42_proc_getxattr(inode, name, buf, buflen,
			pages, np * PAGE_SIZE);
		if (err >= 0)
			break;
		err = nfs4_handle_exception(NFS_SERVER(inode), err,
				&exception);
	} while (exception.retry);

out:
	while (--np >= 0)
		__free_page(pages[np]);
	kfree(pages);

	return err;
}

int nfs42_proc_setxattr(struct inode *inode, const char *name,
			      const void *buf, size_t buflen, int flags)
{
	struct nfs4_exception exception = { };
	int err;

	do {
		err = _nfs42_proc_setxattr(inode, name, buf, buflen, flags);
		if (!err)
			break;
		err = nfs4_handle_exception(NFS_SERVER(inode), err,
				&exception);
	} while (exception.retry);

	return err;
}

ssize_t nfs42_proc_listxattrs(struct inode *inode, void *buf,
			      size_t buflen, u64 *cookiep, bool *eofp)
{
	struct nfs4_exception exception = { };
	ssize_t err;

	do {
		err = _nfs42_proc_listxattrs(inode, buf, buflen,
		    cookiep, eofp);
		if (err >= 0)
			break;
		err = nfs4_handle_exception(NFS_SERVER(inode), err,
				&exception);
	} while (exception.retry);

	return err;
}

int nfs42_proc_removexattr(struct inode *inode, const char *name)
{
	struct nfs4_exception exception = { };
	int err;

	do {
		err = _nfs42_proc_removexattr(inode, name);
		if (!err)
			break;
		err = nfs4_handle_exception(NFS_SERVER(inode), err,
				&exception);
	} while (exception.retry);

	return err;
}
