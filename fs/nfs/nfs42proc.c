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
	struct nfs42_falloc_args args = {
		.falloc_fh	= NFS_FH(inode),
		.falloc_offset	= offset,
		.falloc_length	= len,
	};
	struct nfs42_falloc_res res;
	struct nfs_server *server = NFS_SERVER(inode);
	int status;

	msg->rpc_argp = &args;
	msg->rpc_resp = &res;

	status = nfs42_set_rw_stateid(&args.falloc_stateid, filep, FMODE_WRITE);
	if (status)
		return status;

	return nfs4_call_sync(server->client, server, msg,
			      &args.seq_args, &res.seq_res, 0);
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

	err = nfs42_proc_fallocate(&msg, filep, offset, len);
	if (err == -EOPNOTSUPP)
		NFS_SERVER(inode)->caps &= ~NFS_CAP_ALLOCATE;
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

	err = nfs42_proc_fallocate(&msg, filep, offset, len);
	if (err == -EOPNOTSUPP)
		NFS_SERVER(inode)->caps &= ~NFS_CAP_DEALLOCATE;
	return err;
}

loff_t nfs42_proc_llseek(struct file *filep, loff_t offset, int whence)
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
