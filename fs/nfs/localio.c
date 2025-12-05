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

#define NFSLOCAL_MAX_IOS	3

struct nfs_local_kiocb {
	struct kiocb		kiocb;
	struct bio_vec		*bvec;
	struct nfs_pgio_header	*hdr;
	struct work_struct	work;
	void (*aio_complete_work)(struct work_struct *);
	struct nfsd_file	*localio;
	/* Begin mostly DIO-specific members */
	size_t                  end_len;
	short int		end_iter_index;
	atomic_t		n_iters;
	bool			iter_is_dio_aligned[NFSLOCAL_MAX_IOS];
	struct iov_iter		iters[NFSLOCAL_MAX_IOS] ____cacheline_aligned;
	/* End mostly DIO-specific members */
};

struct nfs_local_fsync_ctx {
	struct nfsd_file	*localio;
	struct nfs_commit_data	*data;
	struct work_struct	work;
	struct completion	*done;
};

static bool localio_enabled __read_mostly = true;
module_param(localio_enabled, bool, 0644);

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

	if (nfs_client_is_local(clp))
		return;

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
	int status = 0;
	struct nfsd_file *localio;

	localio = nfs_open_local_fh(&clp->cl_uuid, clp->cl_rpcclient,
				    cred, fh, nfl, pnf, mode);
	if (IS_ERR(localio)) {
		status = PTR_ERR(localio);
		switch (status) {
		case -ENOMEM:
		case -ENXIO:
		case -ENOENT:
			/* Revalidate localio */
			nfs_localio_disable_client(clp);
			nfs_local_probe(clp);
		}
	}
	trace_nfs_local_open_fh(fh, mode, status);
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

	iocb = kzalloc(sizeof(*iocb), flags);
	if (iocb == NULL)
		return NULL;

	iocb->bvec = kmalloc_array(hdr->page_array.npages,
				   sizeof(struct bio_vec), flags);
	if (iocb->bvec == NULL) {
		kfree(iocb);
		return NULL;
	}

	init_sync_kiocb(&iocb->kiocb, file);

	iocb->hdr = hdr;
	iocb->kiocb.ki_pos = hdr->args.offset;
	iocb->kiocb.ki_flags &= ~IOCB_APPEND;
	iocb->kiocb.ki_complete = NULL;
	iocb->aio_complete_work = NULL;

	iocb->end_iter_index = -1;

	return iocb;
}

static bool
nfs_is_local_dio_possible(struct nfs_local_kiocb *iocb, int rw,
			  size_t len, struct nfs_local_dio *local_dio)
{
	struct nfs_pgio_header *hdr = iocb->hdr;
	loff_t offset = hdr->args.offset;
	u32 nf_dio_mem_align, nf_dio_offset_align, nf_dio_read_offset_align;
	loff_t start_end, orig_end, middle_end;

	nfs_to->nfsd_file_dio_alignment(iocb->localio, &nf_dio_mem_align,
			&nf_dio_offset_align, &nf_dio_read_offset_align);
	if (rw == ITER_DEST)
		nf_dio_offset_align = nf_dio_read_offset_align;

	if (unlikely(!nf_dio_mem_align || !nf_dio_offset_align))
		return false;
	if (unlikely(nf_dio_offset_align > PAGE_SIZE))
		return false;
	if (unlikely(len < nf_dio_offset_align))
		return false;

	local_dio->mem_align = nf_dio_mem_align;
	local_dio->offset_align = nf_dio_offset_align;

	start_end = round_up(offset, nf_dio_offset_align);
	orig_end = offset + len;
	middle_end = round_down(orig_end, nf_dio_offset_align);

	local_dio->middle_offset = start_end;
	local_dio->end_offset = middle_end;

	local_dio->start_len = start_end - offset;
	local_dio->middle_len = middle_end - start_end;
	local_dio->end_len = orig_end - middle_end;

	if (rw == ITER_DEST)
		trace_nfs_local_dio_read(hdr->inode, offset, len, local_dio);
	else
		trace_nfs_local_dio_write(hdr->inode, offset, len, local_dio);
	return true;
}

static bool nfs_iov_iter_aligned_bvec(const struct iov_iter *i,
		unsigned int addr_mask, unsigned int len_mask)
{
	const struct bio_vec *bvec = i->bvec;
	size_t skip = i->iov_offset;
	size_t size = i->count;

	if (size & len_mask)
		return false;
	do {
		size_t len = bvec->bv_len;

		if (len > size)
			len = size;
		if ((unsigned long)(bvec->bv_offset + skip) & addr_mask)
			return false;
		bvec++;
		size -= len;
		skip = 0;
	} while (size);

	return true;
}

static void
nfs_local_iter_setup(struct iov_iter *iter, int rw, struct bio_vec *bvec,
		     unsigned int nvecs, unsigned long total,
		     size_t start, size_t len)
{
	iov_iter_bvec(iter, rw, bvec, nvecs, total);
	if (start)
		iov_iter_advance(iter, start);
	iov_iter_truncate(iter, len);
}

/*
 * Setup as many as 3 iov_iter based on extents described by @local_dio.
 * Returns the number of iov_iter that were setup.
 */
static int
nfs_local_iters_setup_dio(struct nfs_local_kiocb *iocb, int rw,
			  unsigned int nvecs, unsigned long total,
			  struct nfs_local_dio *local_dio)
{
	int n_iters = 0;
	struct iov_iter *iters = iocb->iters;

	/* Setup misaligned start? */
	if (local_dio->start_len) {
		nfs_local_iter_setup(&iters[n_iters], rw, iocb->bvec,
				     nvecs, total, 0, local_dio->start_len);
		++n_iters;
	}

	/*
	 * Setup DIO-aligned middle, if there is no misaligned end (below)
	 * then AIO completion is used, see nfs_local_call_{read,write}
	 */
	nfs_local_iter_setup(&iters[n_iters], rw, iocb->bvec, nvecs,
			     total, local_dio->start_len, local_dio->middle_len);

	iocb->iter_is_dio_aligned[n_iters] =
		nfs_iov_iter_aligned_bvec(&iters[n_iters],
			local_dio->mem_align-1, local_dio->offset_align-1);

	if (unlikely(!iocb->iter_is_dio_aligned[n_iters])) {
		trace_nfs_local_dio_misaligned(iocb->hdr->inode,
			local_dio->start_len, local_dio->middle_len, local_dio);
		return 0; /* no DIO-aligned IO possible */
	}
	iocb->end_iter_index = n_iters;
	++n_iters;

	/* Setup misaligned end? */
	if (local_dio->end_len) {
		nfs_local_iter_setup(&iters[n_iters], rw, iocb->bvec,
				     nvecs, total, local_dio->start_len +
				     local_dio->middle_len, local_dio->end_len);
		iocb->end_iter_index = n_iters;
		++n_iters;
	}

	atomic_set(&iocb->n_iters, n_iters);
	return n_iters;
}

static noinline_for_stack void
nfs_local_iters_init(struct nfs_local_kiocb *iocb, int rw)
{
	struct nfs_pgio_header *hdr = iocb->hdr;
	struct page **pagevec = hdr->page_array.pagevec;
	unsigned long v, total;
	unsigned int base;
	size_t len;

	v = 0;
	total = hdr->args.count;
	base = hdr->args.pgbase;
	while (total && v < hdr->page_array.npages) {
		len = min_t(size_t, total, PAGE_SIZE - base);
		bvec_set_page(&iocb->bvec[v], *pagevec, len, base);
		total -= len;
		++pagevec;
		++v;
		base = 0;
	}
	len = hdr->args.count - total;

	/*
	 * For each iocb, iocb->n_iters is always at least 1 and we always
	 * end io after first nfs_local_pgio_done call unless misaligned DIO.
	 */
	atomic_set(&iocb->n_iters, 1);

	if (test_bit(NFS_IOHDR_ODIRECT, &hdr->flags)) {
		struct nfs_local_dio local_dio;

		if (nfs_is_local_dio_possible(iocb, rw, len, &local_dio) &&
		    nfs_local_iters_setup_dio(iocb, rw, v, len, &local_dio) != 0) {
			/* Ensure DIO WRITE's IO on stable storage upon completion */
			if (rw == ITER_SOURCE)
				iocb->kiocb.ki_flags |= IOCB_DSYNC|IOCB_SYNC;
			return; /* is DIO-aligned */
		}
	}

	/* Use buffered IO */
	iov_iter_bvec(&iocb->iters[0], rw, iocb->bvec, v, len);
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

static bool
nfs_local_pgio_done(struct nfs_local_kiocb *iocb, long status, bool force)
{
	struct nfs_pgio_header *hdr = iocb->hdr;

	/* Must handle partial completions */
	if (status >= 0) {
		hdr->res.count += status;
		/* @hdr was initialized to 0 (zeroed during allocation) */
		if (hdr->task.tk_status == 0)
			hdr->res.op_status = NFS4_OK;
	} else {
		hdr->res.op_status = nfs_localio_errno_to_nfs4_stat(status);
		hdr->task.tk_status = status;
	}

	if (force)
		return true;

	BUG_ON(atomic_read(&iocb->n_iters) <= 0);
	return atomic_dec_and_test(&iocb->n_iters);
}

static void
nfs_local_iocb_release(struct nfs_local_kiocb *iocb)
{
	nfs_local_file_put(iocb->localio);
	nfs_local_iocb_free(iocb);
}

static void
nfs_local_pgio_release(struct nfs_local_kiocb *iocb)
{
	struct nfs_pgio_header *hdr = iocb->hdr;

	nfs_local_iocb_release(iocb);
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

static void nfs_local_read_done(struct nfs_local_kiocb *iocb)
{
	struct nfs_pgio_header *hdr = iocb->hdr;
	struct file *filp = iocb->kiocb.ki_filp;
	long status = hdr->task.tk_status;

	if ((iocb->kiocb.ki_flags & IOCB_DIRECT) && status == -EINVAL) {
		/* Underlying FS will return -EINVAL if misaligned DIO is attempted. */
		pr_info_ratelimited("nfs: Unexpected direct I/O read alignment failure\n");
	}

	/*
	 * Must clear replen otherwise NFSv3 data corruption will occur
	 * if/when switching from LOCALIO back to using normal RPC.
	 */
	hdr->res.replen = 0;

	/* nfs_readpage_result() handles short read */

	if (hdr->args.offset + hdr->res.count >= i_size_read(file_inode(filp)))
		hdr->res.eof = true;

	dprintk("%s: read %ld bytes eof %d.\n", __func__,
			status > 0 ? status : 0, hdr->res.eof);
}

static inline void nfs_local_read_iocb_done(struct nfs_local_kiocb *iocb)
{
	nfs_local_read_done(iocb);
	nfs_local_pgio_release(iocb);
}

static void nfs_local_read_aio_complete_work(struct work_struct *work)
{
	struct nfs_local_kiocb *iocb =
		container_of(work, struct nfs_local_kiocb, work);

	nfs_local_read_iocb_done(iocb);
}

static void nfs_local_read_aio_complete(struct kiocb *kiocb, long ret)
{
	struct nfs_local_kiocb *iocb =
		container_of(kiocb, struct nfs_local_kiocb, kiocb);

	/* AIO completion of DIO read should always be last to complete */
	if (unlikely(!nfs_local_pgio_done(iocb, ret, false)))
		return;

	nfs_local_pgio_aio_complete(iocb); /* Calls nfs_local_read_aio_complete_work */
}

static void nfs_local_call_read(struct work_struct *work)
{
	struct nfs_local_kiocb *iocb =
		container_of(work, struct nfs_local_kiocb, work);
	struct file *filp = iocb->kiocb.ki_filp;
	const struct cred *save_cred;
	bool force_done = false;
	ssize_t status;
	int n_iters;

	save_cred = override_creds(filp->f_cred);

	n_iters = atomic_read(&iocb->n_iters);
	for (int i = 0; i < n_iters ; i++) {
		if (iocb->iter_is_dio_aligned[i]) {
			iocb->kiocb.ki_flags |= IOCB_DIRECT;
			/* Only use AIO completion if DIO-aligned segment is last */
			if (i == iocb->end_iter_index) {
				iocb->kiocb.ki_complete = nfs_local_read_aio_complete;
				iocb->aio_complete_work = nfs_local_read_aio_complete_work;
			}
		} else
			iocb->kiocb.ki_flags &= ~IOCB_DIRECT;

		status = filp->f_op->read_iter(&iocb->kiocb, &iocb->iters[i]);
		if (status != -EIOCBQUEUED) {
			if (unlikely(status >= 0 && status < iocb->iters[i].count))
				force_done = true; /* Partial read */
			if (nfs_local_pgio_done(iocb, status, force_done)) {
				nfs_local_read_iocb_done(iocb);
				break;
			}
		}
	}

	revert_creds(save_cred);
}

static int
nfs_local_do_read(struct nfs_local_kiocb *iocb,
		  const struct rpc_call_ops *call_ops)
{
	struct nfs_pgio_header *hdr = iocb->hdr;

	dprintk("%s: vfs_read count=%u pos=%llu\n",
		__func__, hdr->args.count, hdr->args.offset);

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
	unsigned int seq;

	do {
		seq = read_seqbegin(&clp->cl_boot_lock);
		verf[0] = (u32)clp->cl_nfssvc_boot.tv_sec;
		verf[1] = (u32)clp->cl_nfssvc_boot.tv_nsec;
	} while (read_seqretry(&clp->cl_boot_lock, seq));
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
static int __vfs_getattr(const struct path *p, struct kstat *stat, int version)
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

static void nfs_local_write_done(struct nfs_local_kiocb *iocb)
{
	struct nfs_pgio_header *hdr = iocb->hdr;
	long status = hdr->task.tk_status;

	dprintk("%s: wrote %ld bytes.\n", __func__, status > 0 ? status : 0);

	if ((iocb->kiocb.ki_flags & IOCB_DIRECT) && status == -EINVAL) {
		/* Underlying FS will return -EINVAL if misaligned DIO is attempted. */
		pr_info_ratelimited("nfs: Unexpected direct I/O write alignment failure\n");
	}

	/* Handle short writes as if they are ENOSPC */
	status = hdr->res.count;
	if (status > 0 && status < hdr->args.count) {
		hdr->mds_offset += status;
		hdr->args.offset += status;
		hdr->args.pgbase += status;
		hdr->args.count -= status;
		nfs_set_pgio_error(hdr, -ENOSPC, hdr->args.offset);
		status = -ENOSPC;
		/* record -ENOSPC in terms of nfs_local_pgio_done */
		(void) nfs_local_pgio_done(iocb, status, true);
	}
	if (hdr->task.tk_status < 0)
		nfs_reset_boot_verifier(hdr->inode);
}

static inline void nfs_local_write_iocb_done(struct nfs_local_kiocb *iocb)
{
	nfs_local_write_done(iocb);
	nfs_local_vfs_getattr(iocb);
	nfs_local_pgio_release(iocb);
}

static void nfs_local_write_aio_complete_work(struct work_struct *work)
{
	struct nfs_local_kiocb *iocb =
		container_of(work, struct nfs_local_kiocb, work);

	nfs_local_write_iocb_done(iocb);
}

static void nfs_local_write_aio_complete(struct kiocb *kiocb, long ret)
{
	struct nfs_local_kiocb *iocb =
		container_of(kiocb, struct nfs_local_kiocb, kiocb);

	/* AIO completion of DIO write should always be last to complete */
	if (unlikely(!nfs_local_pgio_done(iocb, ret, false)))
		return;

	nfs_local_pgio_aio_complete(iocb); /* Calls nfs_local_write_aio_complete_work */
}

static void nfs_local_call_write(struct work_struct *work)
{
	struct nfs_local_kiocb *iocb =
		container_of(work, struct nfs_local_kiocb, work);
	struct file *filp = iocb->kiocb.ki_filp;
	unsigned long old_flags = current->flags;
	const struct cred *save_cred;
	bool force_done = false;
	ssize_t status;
	int n_iters;

	current->flags |= PF_LOCAL_THROTTLE | PF_MEMALLOC_NOIO;
	save_cred = override_creds(filp->f_cred);

	file_start_write(filp);
	n_iters = atomic_read(&iocb->n_iters);
	for (int i = 0; i < n_iters ; i++) {
		if (iocb->iter_is_dio_aligned[i]) {
			iocb->kiocb.ki_flags |= IOCB_DIRECT;
			/* Only use AIO completion if DIO-aligned segment is last */
			if (i == iocb->end_iter_index) {
				iocb->kiocb.ki_complete = nfs_local_write_aio_complete;
				iocb->aio_complete_work = nfs_local_write_aio_complete_work;
			}
		} else
			iocb->kiocb.ki_flags &= ~IOCB_DIRECT;

		status = filp->f_op->write_iter(&iocb->kiocb, &iocb->iters[i]);
		if (status != -EIOCBQUEUED) {
			if (unlikely(status >= 0 && status < iocb->iters[i].count))
				force_done = true; /* Partial write */
			if (nfs_local_pgio_done(iocb, status, force_done)) {
				nfs_local_write_iocb_done(iocb);
				break;
			}
		}
	}
	file_end_write(filp);

	revert_creds(save_cred);
	current->flags = old_flags;
}

static int
nfs_local_do_write(struct nfs_local_kiocb *iocb,
		   const struct rpc_call_ops *call_ops)
{
	struct nfs_pgio_header *hdr = iocb->hdr;

	dprintk("%s: vfs_write count=%u pos=%llu %s\n",
		__func__, hdr->args.count, hdr->args.offset,
		(hdr->args.stable == NFS_UNSTABLE) ?  "unstable" : "stable");

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

static struct nfs_local_kiocb *
nfs_local_iocb_init(struct nfs_pgio_header *hdr, struct nfsd_file *localio)
{
	struct file *file = nfs_to->nfsd_file_file(localio);
	struct nfs_local_kiocb *iocb;
	gfp_t gfp_mask;
	int rw;

	if (hdr->rw_mode & FMODE_READ) {
		if (!file->f_op->read_iter)
			return ERR_PTR(-EOPNOTSUPP);
		gfp_mask = GFP_KERNEL;
		rw = ITER_DEST;
	} else {
		if (!file->f_op->write_iter)
			return ERR_PTR(-EOPNOTSUPP);
		gfp_mask = GFP_NOIO;
		rw = ITER_SOURCE;
	}

	iocb = nfs_local_iocb_alloc(hdr, file, gfp_mask);
	if (iocb == NULL)
		return ERR_PTR(-ENOMEM);
	iocb->hdr = hdr;
	iocb->localio = localio;

	nfs_local_iters_init(iocb, rw);

	return iocb;
}

int nfs_local_doio(struct nfs_client *clp, struct nfsd_file *localio,
		   struct nfs_pgio_header *hdr,
		   const struct rpc_call_ops *call_ops)
{
	struct nfs_local_kiocb *iocb;
	int status = 0;

	if (!hdr->args.count)
		return 0;

	iocb = nfs_local_iocb_init(hdr, localio);
	if (IS_ERR(iocb))
		return PTR_ERR(iocb);

	switch (hdr->rw_mode) {
	case FMODE_READ:
		status = nfs_local_do_read(iocb, call_ops);
		break;
	case FMODE_WRITE:
		status = nfs_local_do_write(iocb, call_ops);
		break;
	default:
		dprintk("%s: invalid mode: %d\n", __func__,
			hdr->rw_mode);
		status = -EOPNOTSUPP;
	}

	if (status != 0) {
		if (status == -EAGAIN)
			nfs_localio_disable_client(clp);
		nfs_local_iocb_release(iocb);
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
