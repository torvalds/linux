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
	void (*aio_complete_work)(struct work_struct *);
	struct nfsd_file	*localio;
};

struct nfs_local_fsync_ctx {
	struct nfsd_file	*localio;
	struct nfs_commit_data	*data;
	struct work_struct	work;
	struct completion	*done;
};

static bool localio_enabled __read_mostly = true;
module_param(localio_enabled, bool, 0644);

static bool localio_O_DIRECT_semantics __read_mostly = false;
module_param(localio_O_DIRECT_semantics, bool, 0644);
MODULE_PARM_DESC(localio_O_DIRECT_semantics,
		 "LOCALIO will use O_DIRECT semantics to filesystem.");

static inline bool nfs_client_is_local(const struct nfs_client *clp)
{
	return !!rcu_access_pointer(clp->cl_uuid.net);
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
	if (status || !rcu_access_pointer(clp->cl_uuid.net) || !clp->cl_uuid.dom)
		return false;

	return true;
}

/*
 * nfs_local_probe - probe local i/o support for an nfs_server and nfs_client
 * - called after alloc_client and init_client (so cl_rpcclient exists)
 * - this function is idempotent, it can be called for old or new clients
 */
static void nfs_local_probe(struct nfs_client *clp)
{
	/* Disallow localio if disabled via sysfs or AUTH_SYS isn't used */
	if (!localio_enabled ||
	    clp->cl_rpcclient->cl_auth->au_flavor != RPC_AUTH_UNIX) {
		nfs_localio_disable_client(clp);
		return;
	}

	if (nfs_client_is_local(clp)) {
		/* If already enabled, disable and re-enable */
		nfs_localio_disable_client(clp);
	}

	if (!nfs_uuid_begin(&clp->cl_uuid))
		return;
	if (nfs_server_uuid_is_local(clp))
		nfs_localio_enable_client(clp);
	nfs_uuid_end(&clp->cl_uuid);
}

void nfs_local_probe_async_work(struct work_struct *work)
{
	struct nfs_client *clp =
		container_of(work, struct nfs_client, cl_local_probe_work);

	if (!refcount_inc_not_zero(&clp->cl_count))
		return;
	nfs_local_probe(clp);
	nfs_put_client(clp);
}

void nfs_local_probe_async(struct nfs_client *clp)
{
	queue_work(nfsiod_workqueue, &clp->cl_local_probe_work);
}
EXPORT_SYMBOL_GPL(nfs_local_probe_async);

static inline void nfs_local_file_put(struct nfsd_file *localio)
{
	/* nfs_to_nfsd_file_put_local() expects an __rcu pointer
	 * but we have a __kernel pointer.  It is always safe
	 * to cast a __kernel pointer to an __rcu pointer
	 * because the cast only weakens what is known about the pointer.
	 */
	struct nfsd_file __rcu *nf = (struct nfsd_file __rcu*) localio;

	nfs_to_nfsd_file_put_local(&nf);
}

/*
 * __nfs_local_open_fh - open a local filehandle in terms of nfsd_file.
 *
 * Returns a pointer to a struct nfsd_file or ERR_PTR.
 * Caller must release returned nfsd_file with nfs_to_nfsd_file_put_local().
 */
static struct nfsd_file *
__nfs_local_open_fh(struct nfs_client *clp, const struct cred *cred,
		    struct nfs_fh *fh, struct nfs_file_localio *nfl,
		    struct nfsd_file __rcu **pnf,
		    const fmode_t mode)
{
	struct nfsd_file *localio;

	localio = nfs_open_local_fh(&clp->cl_uuid, clp->cl_rpcclient,
				    cred, fh, nfl, pnf, mode);
	if (IS_ERR(localio)) {
		int status = PTR_ERR(localio);
		trace_nfs_local_open_fh(fh, mode, status);
		switch (status) {
		case -ENOMEM:
		case -ENXIO:
		case -ENOENT:
			/* Revalidate localio, will disable if unsupported */
			nfs_local_probe(clp);
		}
	}
	return localio;
}

/*
 * nfs_local_open_fh - open a local filehandle in terms of nfsd_file.
 * First checking if the open nfsd_file is already cached, otherwise
 * must __nfs_local_open_fh and insert the nfsd_file in nfs_file_localio.
 *
 * Returns a pointer to a struct nfsd_file or NULL.
 */
struct nfsd_file *
nfs_local_open_fh(struct nfs_client *clp, const struct cred *cred,
		  struct nfs_fh *fh, struct nfs_file_localio *nfl,
		  const fmode_t mode)
{
	struct nfsd_file *nf, __rcu **pnf;

	if (!nfs_server_is_local(clp))
		return NULL;
	if (mode & ~(FMODE_READ | FMODE_WRITE))
		return NULL;

	if (mode & FMODE_WRITE)
		pnf = &nfl->rw_file;
	else
		pnf = &nfl->ro_file;

	nf = __nfs_local_open_fh(clp, cred, fh, nfl, pnf, mode);
	if (IS_ERR(nf))
		return NULL;
	return nf;
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
		     struct file *file, gfp_t flags)
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

	if (localio_O_DIRECT_semantics &&
	    test_bit(NFS_IOHDR_ODIRECT, &hdr->flags)) {
		iocb->kiocb.ki_filp = file;
		iocb->kiocb.ki_flags = IOCB_DIRECT;
	} else
		init_sync_kiocb(&iocb->kiocb, file);

	iocb->kiocb.ki_pos = hdr->args.offset;
	iocb->hdr = hdr;
	iocb->kiocb.ki_flags &= ~IOCB_APPEND;
	iocb->aio_complete_work = NULL;

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
		hdr->res.op_status = nfs_localio_errno_to_nfs4_stat(status);
		hdr->task.tk_status = status;
	}
}

static void
nfs_local_pgio_release(struct nfs_local_kiocb *iocb)
{
	struct nfs_pgio_header *hdr = iocb->hdr;

	nfs_local_file_put(iocb->localio);
	nfs_local_iocb_free(iocb);
	nfs_local_hdr_release(hdr, hdr->task.tk_ops);
}

/*
 * Complete the I/O from iocb->kiocb.ki_complete()
 *
 * Note that this function can be called from a bottom half context,
 * hence we need to queue the rpc_call_done() etc to a workqueue
 */
static inline void nfs_local_pgio_aio_complete(struct nfs_local_kiocb *iocb)
{
	INIT_WORK(&iocb->work, iocb->aio_complete_work);
	queue_work(nfsiod_workqueue, &iocb->work);
}

static void
nfs_local_read_done(struct nfs_local_kiocb *iocb, long status)
{
	struct nfs_pgio_header *hdr = iocb->hdr;
	struct file *filp = iocb->kiocb.ki_filp;

	nfs_local_pgio_done(hdr, status);

	/*
	 * Must clear replen otherwise NFSv3 data corruption will occur
	 * if/when switching from LOCALIO back to using normal RPC.
	 */
	hdr->res.replen = 0;

	if (hdr->res.count != hdr->args.count ||
	    hdr->args.offset + hdr->res.count >= i_size_read(file_inode(filp)))
		hdr->res.eof = true;

	dprintk("%s: read %ld bytes eof %d.\n", __func__,
			status > 0 ? status : 0, hdr->res.eof);
}

static void nfs_local_read_aio_complete_work(struct work_struct *work)
{
	struct nfs_local_kiocb *iocb =
		container_of(work, struct nfs_local_kiocb, work);

	nfs_local_pgio_release(iocb);
}

static void nfs_local_read_aio_complete(struct kiocb *kiocb, long ret)
{
	struct nfs_local_kiocb *iocb =
		container_of(kiocb, struct nfs_local_kiocb, kiocb);

	nfs_local_read_done(iocb, ret);
	nfs_local_pgio_aio_complete(iocb); /* Calls nfs_local_read_aio_complete_work */
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
	if (status != -EIOCBQUEUED) {
		nfs_local_read_done(iocb, status);
		nfs_local_pgio_release(iocb);
	}

	revert_creds(save_cred);
}

static int
nfs_do_local_read(struct nfs_pgio_header *hdr,
		  struct nfsd_file *localio,
		  const struct rpc_call_ops *call_ops)
{
	struct nfs_local_kiocb *iocb;
	struct file *file = nfs_to->nfsd_file_file(localio);

	/* Don't support filesystems without read_iter */
	if (!file->f_op->read_iter)
		return -EAGAIN;

	dprintk("%s: vfs_read count=%u pos=%llu\n",
		__func__, hdr->args.count, hdr->args.offset);

	iocb = nfs_local_iocb_alloc(hdr, file, GFP_KERNEL);
	if (iocb == NULL)
		return -ENOMEM;
	iocb->localio = localio;

	nfs_local_pgio_init(hdr, call_ops);
	hdr->res.eof = false;

	if (iocb->kiocb.ki_flags & IOCB_DIRECT) {
		iocb->kiocb.ki_complete = nfs_local_read_aio_complete;
		iocb->aio_complete_work = nfs_local_read_aio_complete_work;
	}

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

	nfs_local_pgio_done(hdr, status);
}

static void nfs_local_write_aio_complete_work(struct work_struct *work)
{
	struct nfs_local_kiocb *iocb =
		container_of(work, struct nfs_local_kiocb, work);

	nfs_local_vfs_getattr(iocb);
	nfs_local_pgio_release(iocb);
}

static void nfs_local_write_aio_complete(struct kiocb *kiocb, long ret)
{
	struct nfs_local_kiocb *iocb =
		container_of(kiocb, struct nfs_local_kiocb, kiocb);

	nfs_local_write_done(iocb, ret);
	nfs_local_pgio_aio_complete(iocb); /* Calls nfs_local_write_aio_complete_work */
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
	if (status != -EIOCBQUEUED) {
		nfs_local_write_done(iocb, status);
		nfs_local_vfs_getattr(iocb);
		nfs_local_pgio_release(iocb);
	}

	revert_creds(save_cred);
	current->flags = old_flags;
}

static int
nfs_do_local_write(struct nfs_pgio_header *hdr,
		   struct nfsd_file *localio,
		   const struct rpc_call_ops *call_ops)
{
	struct nfs_local_kiocb *iocb;
	struct file *file = nfs_to->nfsd_file_file(localio);

	/* Don't support filesystems without write_iter */
	if (!file->f_op->write_iter)
		return -EAGAIN;

	dprintk("%s: vfs_write count=%u pos=%llu %s\n",
		__func__, hdr->args.count, hdr->args.offset,
		(hdr->args.stable == NFS_UNSTABLE) ?  "unstable" : "stable");

	iocb = nfs_local_iocb_alloc(hdr, file, GFP_NOIO);
	if (iocb == NULL)
		return -ENOMEM;
	iocb->localio = localio;

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

	if (iocb->kiocb.ki_flags & IOCB_DIRECT) {
		iocb->kiocb.ki_complete = nfs_local_write_aio_complete;
		iocb->aio_complete_work = nfs_local_write_aio_complete_work;
	}

	INIT_WORK(&iocb->work, nfs_local_call_write);
	queue_work(nfslocaliod_workqueue, &iocb->work);

	return 0;
}

int nfs_local_doio(struct nfs_client *clp, struct nfsd_file *localio,
		   struct nfs_pgio_header *hdr,
		   const struct rpc_call_ops *call_ops)
{
	int status = 0;

	if (!hdr->args.count)
		return 0;

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

	if (status != 0) {
		if (status == -EAGAIN)
			nfs_localio_disable_client(clp);
		nfs_local_file_put(localio);
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
		data->res.op_status = nfs_localio_errno_to_nfs4_stat(status);
		data->task.tk_status = status;
	}
}

static void
nfs_local_release_commit_data(struct nfsd_file *localio,
		struct nfs_commit_data *data,
		const struct rpc_call_ops *call_ops)
{
	nfs_local_file_put(localio);
	call_ops->rpc_call_done(&data->task, data);
	call_ops->rpc_release(data);
}

static void
nfs_local_fsync_ctx_free(struct nfs_local_fsync_ctx *ctx)
{
	nfs_local_release_commit_data(ctx->localio, ctx->data,
				      ctx->data->task.tk_ops);
	kfree(ctx);
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

static struct nfs_local_fsync_ctx *
nfs_local_fsync_ctx_alloc(struct nfs_commit_data *data,
			  struct nfsd_file *localio, gfp_t flags)
{
	struct nfs_local_fsync_ctx *ctx = kmalloc(sizeof(*ctx), flags);

	if (ctx != NULL) {
		ctx->localio = localio;
		ctx->data = data;
		INIT_WORK(&ctx->work, nfs_local_fsync_work);
		ctx->done = NULL;
	}
	return ctx;
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

	if (how & FLUSH_SYNC) {
		DECLARE_COMPLETION_ONSTACK(done);
		ctx->done = &done;
		queue_work(nfsiod_workqueue, &ctx->work);
		wait_for_completion(&done);
	} else
		queue_work(nfsiod_workqueue, &ctx->work);

	return 0;
}
