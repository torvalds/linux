// SPDX-License-Identifier: GPL-2.0-only
/*
 * NFS client support for local clients to bypass network stack
 *
 * Copyright (C) 2014 Weston Andros Adamson <dros@primarydata.com>
 * Copyright (C) 2019 Trond Myklebust <trond.myklebust@hammerspace.com>
 * Copyright (C) 2024 Mike Snitzer <snitzer@hammerspace.com>
 * Copyright (C) 2024 NeilBrown <neilb@suse.de>
 */

#include <linux/module.h>
#include <linux/errno.h>
#include <linux/vfs.h>
#include <linux/file.h>
#include <linux/inet.h>
#include <linux/sunrpc/addr.h>
#include <linux/inetdevice.h>
#include <net/addrconf.h>
#include <linux/nfs_common.h>
#include <linux/nfslocalio.h>
#include <linux/bvec.h>

#include <linux/nfs.h>
#include <linux/nfs_fs.h>
#include <linux/nfs_xdr.h>

#include "internal.h"
#include "pnfs.h"
#include "nfstrace.h"

#define NFSDBG_FACILITY		NFSDBG_VFS

struct nfs_local_kiocb {
	struct kiocb		kiocb;
	struct bio_vec		*bvec;
	struct nfs_pgio_header	*hdr;
	struct work_struct	work;
	struct nfsd_file	*localio;
};

struct nfs_local_fsync_ctx {
	struct nfsd_file	*localio;
	struct nfs_commit_data	*data;
	struct work_struct	work;
	struct kref		kref;
	struct completion	*done;
};
static void nfs_local_fsync_work(struct work_struct *work);

static bool localio_enabled __read_mostly = true;
module_param(localio_enabled, bool, 0644);

static inline bool nfs_client_is_local(const struct nfs_client *clp)
{
	return !!test_bit(NFS_CS_LOCAL_IO, &clp->cl_flags);
}

bool nfs_server_is_local(const struct nfs_client *clp)
{
	return nfs_client_is_local(clp) && localio_enabled;
}
EXPORT_SYMBOL_GPL(nfs_server_is_local);

/*
 * UUID_IS_LOCAL XDR functions
 */

static void localio_xdr_enc_uuidargs(struct rpc_rqst *req,
				     struct xdr_stream *xdr,
				     const void *data)
{
	const u8 *uuid = data;

	encode_opaque_fixed(xdr, uuid, UUID_SIZE);
}

static int localio_xdr_dec_uuidres(struct rpc_rqst *req,
				   struct xdr_stream *xdr,
				   void *result)
{
	/* void return */
	return 0;
}

static const struct rpc_procinfo nfs_localio_procedures[] = {
	[LOCALIOPROC_UUID_IS_LOCAL] = {
		.p_proc = LOCALIOPROC_UUID_IS_LOCAL,
		.p_encode = localio_xdr_enc_uuidargs,
		.p_decode = localio_xdr_dec_uuidres,
		.p_arglen = XDR_QUADLEN(UUID_SIZE),
		.p_replen = 0,
		.p_statidx = LOCALIOPROC_UUID_IS_LOCAL,
		.p_name = "UUID_IS_LOCAL",
	},
};

static unsigned int nfs_localio_counts[ARRAY_SIZE(nfs_localio_procedures)];
static const struct rpc_version nfslocalio_version1 = {
	.number			= 1,
	.nrprocs		= ARRAY_SIZE(nfs_localio_procedures),
	.procs			= nfs_localio_procedures,
	.counts			= nfs_localio_counts,
};

static const struct rpc_version *nfslocalio_version[] = {
       [1]			= &nfslocalio_version1,
};

extern const struct rpc_program nfslocalio_program;
static struct rpc_stat		nfslocalio_rpcstat = { &nfslocalio_program };

const struct rpc_program nfslocalio_program = {
	.name			= "nfslocalio",
	.number			= NFS_LOCALIO_PROGRAM,
	.nrvers			= ARRAY_SIZE(nfslocalio_version),
	.version		= nfslocalio_version,
	.stats			= &nfslocalio_rpcstat,
};

/*
 * nfs_local_enable - enable local i/o for an nfs_client
 */
static void nfs_local_enable(struct nfs_client *clp)
{
	spin_lock(&clp->cl_localio_lock);
	set_bit(NFS_CS_LOCAL_IO, &clp->cl_flags);
	trace_nfs_local_enable(clp);
	spin_unlock(&clp->cl_localio_lock);
}

/*
 * nfs_local_disable - disable local i/o for an nfs_client
 */
void nfs_local_disable(struct nfs_client *clp)
{
	spin_lock(&clp->cl_localio_lock);
	if (test_and_clear_bit(NFS_CS_LOCAL_IO, &clp->cl_flags)) {
		trace_nfs_local_disable(clp);
		nfs_uuid_invalidate_one_client(&clp->cl_uuid);
	}
	spin_unlock(&clp->cl_localio_lock);
}

/*
 * nfs_init_localioclient - Initialise an NFS localio client connection
 */
static struct rpc_clnt *nfs_init_localioclient(struct nfs_client *clp)
{
	struct rpc_clnt *rpcclient_localio;

	rpcclient_localio = rpc_bind_new_program(clp->cl_rpcclient,
						 &nfslocalio_program, 1);

	dprintk_rcu("%s: server (%s) %s NFS LOCALIO.\n",
		__func__, rpc_peeraddr2str(clp->cl_rpcclient, RPC_DISPLAY_ADDR),
		(IS_ERR(rpcclient_localio) ? "does not support" : "supports"));

	return rpcclient_localio;
}

static bool nfs_server_uuid_is_local(struct nfs_client *clp)
{
	u8 uuid[UUID_SIZE];
	struct rpc_message msg = {
		.rpc_argp = &uuid,
	};
	struct rpc_clnt *rpcclient_localio;
	int status;

	rpcclient_localio = nfs_init_localioclient(clp);
	if (IS_ERR(rpcclient_localio))
		return false;

	export_uuid(uuid, &clp->cl_uuid.uuid);

	msg.rpc_proc = &nfs_localio_procedures[LOCALIOPROC_UUID_IS_LOCAL];
	status = rpc_call_sync(rpcclient_localio, &msg, 0);
	dprintk("%s: NFS reply UUID_IS_LOCAL: status=%d\n",
		__func__, status);
	rpc_shutdown_client(rpcclient_localio);

	/* Server is only local if it initialized required struct members */
	if (status || !clp->cl_uuid.net || !clp->cl_uuid.dom)
		return false;

	return true;
}

/*
 * nfs_local_probe - probe local i/o support for an nfs_server and nfs_client
 * - called after alloc_client and init_client (so cl_rpcclient exists)
 * - this function is idempotent, it can be called for old or new clients
 */
void nfs_local_probe(struct nfs_client *clp)
{
	/* Disallow localio if disabled via sysfs or AUTH_SYS isn't used */
	if (!localio_enabled ||
	    clp->cl_rpcclient->cl_auth->au_flavor != RPC_AUTH_UNIX) {
		nfs_local_disable(clp);
		return;
	}

	if (nfs_client_is_local(clp)) {
		/* If already enabled, disable and re-enable */
		nfs_local_disable(clp);
	}

	if (!nfs_uuid_begin(&clp->cl_uuid))
		return;
	if (nfs_server_uuid_is_local(clp))
		nfs_local_enable(clp);
	nfs_uuid_end(&clp->cl_uuid);
}
EXPORT_SYMBOL_GPL(nfs_local_probe);

/*
 * nfs_local_open_fh - open a local filehandle in terms of nfsd_file
 *
 * Returns a pointer to a struct nfsd_file or NULL
 */
struct nfsd_file *
nfs_local_open_fh(struct nfs_client *clp, const struct cred *cred,
		  struct nfs_fh *fh, const fmode_t mode)
{
	struct nfsd_file *localio;
	int status;

	if (!nfs_server_is_local(clp))
		return NULL;
	if (mode & ~(FMODE_READ | FMODE_WRITE))
		return NULL;

	localio = nfs_open_local_fh(&clp->cl_uuid, clp->cl_rpcclient,
				    cred, fh, mode);
	if (IS_ERR(localio)) {
		status = PTR_ERR(localio);
		trace_nfs_local_open_fh(fh, mode, status);
		switch (status) {
		case -ENOMEM:
		case -ENXIO:
		case -ENOENT:
			/* Revalidate localio, will disable if unsupported */
			nfs_local_probe(clp);
		}
		return NULL;
	}
	return localio;
}
EXPORT_SYMBOL_GPL(nfs_local_open_fh);

static struct bio_vec *
nfs_bvec_alloc_and_import_pagevec(struct page **pagevec,
		unsigned int npages, gfp_t flags)
{
	struct bio_vec *bvec, *p;

	bvec = kmalloc_array(npages, sizeof(*bvec), flags);
	if (bvec != NULL) {
		for (p = bvec; npages > 0; p++, pagevec++, npages--) {
			p->bv_page = *pagevec;
			p->bv_len = PAGE_SIZE;
			p->bv_offset = 0;
		}
	}
	return bvec;
}

static void
nfs_local_iocb_free(struct nfs_local_kiocb *iocb)
{
	kfree(iocb->bvec);
	kfree(iocb);
}

static struct nfs_local_kiocb *
nfs_local_iocb_alloc(struct nfs_pgio_header *hdr,
		     struct nfsd_file *localio, gfp_t flags)
{
	struct nfs_local_kiocb *iocb;

	iocb = kmalloc(sizeof(*iocb), flags);
	if (iocb == NULL)
		return NULL;
	iocb->bvec = nfs_bvec_alloc_and_import_pagevec(hdr->page_array.pagevec,
			hdr->page_array.npages, flags);
	if (iocb->bvec == NULL) {
		kfree(iocb);
		return NULL;
	}
	init_sync_kiocb(&iocb->kiocb, nfs_to->nfsd_file_file(localio));
	iocb->kiocb.ki_pos = hdr->args.offset;
	iocb->localio = localio;
	iocb->hdr = hdr;
	iocb->kiocb.ki_flags &= ~IOCB_APPEND;
	return iocb;
}

static void
nfs_local_iter_init(struct iov_iter *i, struct nfs_local_kiocb *iocb, int dir)
{
	struct nfs_pgio_header *hdr = iocb->hdr;

	iov_iter_bvec(i, dir, iocb->bvec, hdr->page_array.npages,
		      hdr->args.count + hdr->args.pgbase);
	if (hdr->args.pgbase != 0)
		iov_iter_advance(i, hdr->args.pgbase);
}

static void
nfs_local_hdr_release(struct nfs_pgio_header *hdr,
		const struct rpc_call_ops *call_ops)
{
	call_ops->rpc_call_done(&hdr->task, hdr);
	call_ops->rpc_release(hdr);
}

static void
nfs_local_pgio_init(struct nfs_pgio_header *hdr,
		const struct rpc_call_ops *call_ops)
{
	hdr->task.tk_ops = call_ops;
	if (!hdr->task.tk_start)
		hdr->task.tk_start = ktime_get();
}

static void
nfs_local_pgio_done(struct nfs_pgio_header *hdr, long status)
{
	if (status >= 0) {
		hdr->res.count = status;
		hdr->res.op_status = NFS4_OK;
		hdr->task.tk_status = 0;
	} else {
		hdr->res.op_status = nfs4_stat_to_errno(status);
		hdr->task.tk_status = status;
	}
}

static void
nfs_local_pgio_release(struct nfs_local_kiocb *iocb)
{
	struct nfs_pgio_header *hdr = iocb->hdr;

	nfs_to_nfsd_file_put_local(iocb->localio);
	nfs_local_iocb_free(iocb);
	nfs_local_hdr_release(hdr, hdr->task.tk_ops);
}

static void
nfs_local_read_done(struct nfs_local_kiocb *iocb, long status)
{
	struct nfs_pgio_header *hdr = iocb->hdr;
	struct file *filp = iocb->kiocb.ki_filp;

	nfs_local_pgio_done(hdr, status);

	if (hdr->res.count != hdr->args.count ||
	    hdr->args.offset + hdr->res.count >= i_size_read(file_inode(filp)))
		hdr->res.eof = true;

	dprintk("%s: read %ld bytes eof %d.\n", __func__,
			status > 0 ? status : 0, hdr->res.eof);
}

static void nfs_local_call_read(struct work_struct *work)
{
	struct nfs_local_kiocb *iocb =
		container_of(work, struct nfs_local_kiocb, work);
	struct file *filp = iocb->kiocb.ki_filp;
	const struct cred *save_cred;
	struct iov_iter iter;
	ssize_t status;

	save_cred = override_creds(filp->f_cred);

	nfs_local_iter_init(&iter, iocb, READ);

	status = filp->f_op->read_iter(&iocb->kiocb, &iter);
	WARN_ON_ONCE(status == -EIOCBQUEUED);

	nfs_local_read_done(iocb, status);
	nfs_local_pgio_release(iocb);

	revert_creds(save_cred);
}

static int
nfs_do_local_read(struct nfs_pgio_header *hdr,
		  struct nfsd_file *localio,
		  const struct rpc_call_ops *call_ops)
{
	struct nfs_local_kiocb *iocb;

	dprintk("%s: vfs_read count=%u pos=%llu\n",
		__func__, hdr->args.count, hdr->args.offset);

	iocb = nfs_local_iocb_alloc(hdr, localio, GFP_KERNEL);
	if (iocb == NULL)
		return -ENOMEM;

	nfs_local_pgio_init(hdr, call_ops);
	hdr->res.eof = false;

	INIT_WORK(&iocb->work, nfs_local_call_read);
	queue_work(nfslocaliod_workqueue, &iocb->work);

	return 0;
}

static void
nfs_copy_boot_verifier(struct nfs_write_verifier *verifier, struct inode *inode)
{
	struct nfs_client *clp = NFS_SERVER(inode)->nfs_client;
	u32 *verf = (u32 *)verifier->data;
	int seq = 0;

	do {
		read_seqbegin_or_lock(&clp->cl_boot_lock, &seq);
		verf[0] = (u32)clp->cl_nfssvc_boot.tv_sec;
		verf[1] = (u32)clp->cl_nfssvc_boot.tv_nsec;
	} while (need_seqretry(&clp->cl_boot_lock, seq));
	done_seqretry(&clp->cl_boot_lock, seq);
}

static void
nfs_reset_boot_verifier(struct inode *inode)
{
	struct nfs_client *clp = NFS_SERVER(inode)->nfs_client;

	write_seqlock(&clp->cl_boot_lock);
	ktime_get_real_ts64(&clp->cl_nfssvc_boot);
	write_sequnlock(&clp->cl_boot_lock);
}

static void
nfs_set_local_verifier(struct inode *inode,
		struct nfs_writeverf *verf,
		enum nfs3_stable_how how)
{
	nfs_copy_boot_verifier(&verf->verifier, inode);
	verf->committed = how;
}

/* Factored out from fs/nfsd/vfs.h:fh_getattr() */
static int __vfs_getattr(struct path *p, struct kstat *stat, int version)
{
	u32 request_mask = STATX_BASIC_STATS;

	if (version == 4)
		request_mask |= (STATX_BTIME | STATX_CHANGE_COOKIE);
	return vfs_getattr(p, stat, request_mask, AT_STATX_SYNC_AS_STAT);
}

/* Copied from fs/nfsd/nfsfh.c:nfsd4_change_attribute() */
static u64 __nfsd4_change_attribute(const struct kstat *stat,
				    const struct inode *inode)
{
	u64 chattr;

	if (stat->result_mask & STATX_CHANGE_COOKIE) {
		chattr = stat->change_cookie;
		if (S_ISREG(inode->i_mode) &&
		    !(stat->attributes & STATX_ATTR_CHANGE_MONOTONIC)) {
			chattr += (u64)stat->ctime.tv_sec << 30;
			chattr += stat->ctime.tv_nsec;
		}
	} else {
		chattr = time_to_chattr(&stat->ctime);
	}
	return chattr;
}

static void nfs_local_vfs_getattr(struct nfs_local_kiocb *iocb)
{
	struct kstat stat;
	struct file *filp = iocb->kiocb.ki_filp;
	struct nfs_pgio_header *hdr = iocb->hdr;
	struct nfs_fattr *fattr = hdr->res.fattr;
	int version = NFS_PROTO(hdr->inode)->version;

	if (unlikely(!fattr) || __vfs_getattr(&filp->f_path, &stat, version))
		return;

	fattr->valid = (NFS_ATTR_FATTR_FILEID |
			NFS_ATTR_FATTR_CHANGE |
			NFS_ATTR_FATTR_SIZE |
			NFS_ATTR_FATTR_ATIME |
			NFS_ATTR_FATTR_MTIME |
			NFS_ATTR_FATTR_CTIME |
			NFS_ATTR_FATTR_SPACE_USED);

	fattr->fileid = stat.ino;
	fattr->size = stat.size;
	fattr->atime = stat.atime;
	fattr->mtime = stat.mtime;
	fattr->ctime = stat.ctime;
	if (version == 4) {
		fattr->change_attr =
			__nfsd4_change_attribute(&stat, file_inode(filp));
	} else
		fattr->change_attr = nfs_timespec_to_change_attr(&fattr->ctime);
	fattr->du.nfs3.used = stat.blocks << 9;
}

static void
nfs_local_write_done(struct nfs_local_kiocb *iocb, long status)
{
	struct nfs_pgio_header *hdr = iocb->hdr;
	struct inode *inode = hdr->inode;

	dprintk("%s: wrote %ld bytes.\n", __func__, status > 0 ? status : 0);

	/* Handle short writes as if they are ENOSPC */
	if (status > 0 && status < hdr->args.count) {
		hdr->mds_offset += status;
		hdr->args.offset += status;
		hdr->args.pgbase += status;
		hdr->args.count -= status;
		nfs_set_pgio_error(hdr, -ENOSPC, hdr->args.offset);
		status = -ENOSPC;
	}
	if (status < 0)
		nfs_reset_boot_verifier(inode);
	else if (nfs_should_remove_suid(inode)) {
		/* Deal with the suid/sgid bit corner case */
		spin_lock(&inode->i_lock);
		nfs_set_cache_invalid(inode, NFS_INO_INVALID_MODE);
		spin_unlock(&inode->i_lock);
	}
	nfs_local_pgio_done(hdr, status);
}

static void nfs_local_call_write(struct work_struct *work)
{
	struct nfs_local_kiocb *iocb =
		container_of(work, struct nfs_local_kiocb, work);
	struct file *filp = iocb->kiocb.ki_filp;
	unsigned long old_flags = current->flags;
	const struct cred *save_cred;
	struct iov_iter iter;
	ssize_t status;

	current->flags |= PF_LOCAL_THROTTLE | PF_MEMALLOC_NOIO;
	save_cred = override_creds(filp->f_cred);

	nfs_local_iter_init(&iter, iocb, WRITE);

	file_start_write(filp);
	status = filp->f_op->write_iter(&iocb->kiocb, &iter);
	file_end_write(filp);
	WARN_ON_ONCE(status == -EIOCBQUEUED);

	nfs_local_write_done(iocb, status);
	nfs_local_vfs_getattr(iocb);
	nfs_local_pgio_release(iocb);

	revert_creds(save_cred);
	current->flags = old_flags;
}

static int
nfs_do_local_write(struct nfs_pgio_header *hdr,
		   struct nfsd_file *localio,
		   const struct rpc_call_ops *call_ops)
{
	struct nfs_local_kiocb *iocb;

	dprintk("%s: vfs_write count=%u pos=%llu %s\n",
		__func__, hdr->args.count, hdr->args.offset,
		(hdr->args.stable == NFS_UNSTABLE) ?  "unstable" : "stable");

	iocb = nfs_local_iocb_alloc(hdr, localio, GFP_NOIO);
	if (iocb == NULL)
		return -ENOMEM;

	switch (hdr->args.stable) {
	default:
		break;
	case NFS_DATA_SYNC:
		iocb->kiocb.ki_flags |= IOCB_DSYNC;
		break;
	case NFS_FILE_SYNC:
		iocb->kiocb.ki_flags |= IOCB_DSYNC|IOCB_SYNC;
	}
	nfs_local_pgio_init(hdr, call_ops);

	nfs_set_local_verifier(hdr->inode, hdr->res.verf, hdr->args.stable);

	INIT_WORK(&iocb->work, nfs_local_call_write);
	queue_work(nfslocaliod_workqueue, &iocb->work);

	return 0;
}

int nfs_local_doio(struct nfs_client *clp, struct nfsd_file *localio,
		   struct nfs_pgio_header *hdr,
		   const struct rpc_call_ops *call_ops)
{
	int status = 0;
	struct file *filp = nfs_to->nfsd_file_file(localio);

	if (!hdr->args.count)
		return 0;
	/* Don't support filesystems without read_iter/write_iter */
	if (!filp->f_op->read_iter || !filp->f_op->write_iter) {
		nfs_local_disable(clp);
		status = -EAGAIN;
		goto out;
	}

	switch (hdr->rw_mode) {
	case FMODE_READ:
		status = nfs_do_local_read(hdr, localio, call_ops);
		break;
	case FMODE_WRITE:
		status = nfs_do_local_write(hdr, localio, call_ops);
		break;
	default:
		dprintk("%s: invalid mode: %d\n", __func__,
			hdr->rw_mode);
		status = -EINVAL;
	}
out:
	if (status != 0) {
		nfs_to_nfsd_file_put_local(localio);
		hdr->task.tk_status = status;
		nfs_local_hdr_release(hdr, call_ops);
	}
	return status;
}

static void
nfs_local_init_commit(struct nfs_commit_data *data,
		const struct rpc_call_ops *call_ops)
{
	data->task.tk_ops = call_ops;
}

static int
nfs_local_run_commit(struct file *filp, struct nfs_commit_data *data)
{
	loff_t start = data->args.offset;
	loff_t end = LLONG_MAX;

	if (data->args.count > 0) {
		end = start + data->args.count - 1;
		if (end < start)
			end = LLONG_MAX;
	}

	dprintk("%s: commit %llu - %llu\n", __func__, start, end);
	return vfs_fsync_range(filp, start, end, 0);
}

static void
nfs_local_commit_done(struct nfs_commit_data *data, int status)
{
	if (status >= 0) {
		nfs_set_local_verifier(data->inode,
				data->res.verf,
				NFS_FILE_SYNC);
		data->res.op_status = NFS4_OK;
		data->task.tk_status = 0;
	} else {
		nfs_reset_boot_verifier(data->inode);
		data->res.op_status = nfs4_stat_to_errno(status);
		data->task.tk_status = status;
	}
}

static void
nfs_local_release_commit_data(struct nfsd_file *localio,
		struct nfs_commit_data *data,
		const struct rpc_call_ops *call_ops)
{
	nfs_to_nfsd_file_put_local(localio);
	call_ops->rpc_call_done(&data->task, data);
	call_ops->rpc_release(data);
}

static struct nfs_local_fsync_ctx *
nfs_local_fsync_ctx_alloc(struct nfs_commit_data *data,
			  struct nfsd_file *localio, gfp_t flags)
{
	struct nfs_local_fsync_ctx *ctx = kmalloc(sizeof(*ctx), flags);

	if (ctx != NULL) {
		ctx->localio = localio;
		ctx->data = data;
		INIT_WORK(&ctx->work, nfs_local_fsync_work);
		kref_init(&ctx->kref);
		ctx->done = NULL;
	}
	return ctx;
}

static void
nfs_local_fsync_ctx_kref_free(struct kref *kref)
{
	kfree(container_of(kref, struct nfs_local_fsync_ctx, kref));
}

static void
nfs_local_fsync_ctx_put(struct nfs_local_fsync_ctx *ctx)
{
	kref_put(&ctx->kref, nfs_local_fsync_ctx_kref_free);
}

static void
nfs_local_fsync_ctx_free(struct nfs_local_fsync_ctx *ctx)
{
	nfs_local_release_commit_data(ctx->localio, ctx->data,
				      ctx->data->task.tk_ops);
	nfs_local_fsync_ctx_put(ctx);
}

static void
nfs_local_fsync_work(struct work_struct *work)
{
	struct nfs_local_fsync_ctx *ctx;
	int status;

	ctx = container_of(work, struct nfs_local_fsync_ctx, work);

	status = nfs_local_run_commit(nfs_to->nfsd_file_file(ctx->localio),
				      ctx->data);
	nfs_local_commit_done(ctx->data, status);
	if (ctx->done != NULL)
		complete(ctx->done);
	nfs_local_fsync_ctx_free(ctx);
}

int nfs_local_commit(struct nfsd_file *localio,
		     struct nfs_commit_data *data,
		     const struct rpc_call_ops *call_ops, int how)
{
	struct nfs_local_fsync_ctx *ctx;

	ctx = nfs_local_fsync_ctx_alloc(data, localio, GFP_KERNEL);
	if (!ctx) {
		nfs_local_commit_done(data, -ENOMEM);
		nfs_local_release_commit_data(localio, data, call_ops);
		return -ENOMEM;
	}

	nfs_local_init_commit(data, call_ops);
	kref_get(&ctx->kref);
	if (how & FLUSH_SYNC) {
		DECLARE_COMPLETION_ONSTACK(done);
		ctx->done = &done;
		queue_work(nfsiod_workqueue, &ctx->work);
		wait_for_completion(&done);
	} else
		queue_work(nfsiod_workqueue, &ctx->work);
	nfs_local_fsync_ctx_put(ctx);
	return 0;
}
