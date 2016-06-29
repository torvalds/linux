/*
 * Copyright (c) 2015 Oracle.  All rights reserved.
 * Copyright (c) 2003-2007 Network Appliance, Inc. All rights reserved.
 */

/* Lightweight memory registration using Fast Memory Regions (FMR).
 * Referred to sometimes as MTHCAFMR mode.
 *
 * FMR uses synchronous memory registration and deregistration.
 * FMR registration is known to be fast, but FMR deregistration
 * can take tens of usecs to complete.
 */

/* Normal operation
 *
 * A Memory Region is prepared for RDMA READ or WRITE using the
 * ib_map_phys_fmr verb (fmr_op_map). When the RDMA operation is
 * finished, the Memory Region is unmapped using the ib_unmap_fmr
 * verb (fmr_op_unmap).
 */

#include "xprt_rdma.h"

#if IS_ENABLED(CONFIG_SUNRPC_DEBUG)
# define RPCDBG_FACILITY	RPCDBG_TRANS
#endif

/* Maximum scatter/gather per FMR */
#define RPCRDMA_MAX_FMR_SGES	(64)

/* Access mode of externally registered pages */
enum {
	RPCRDMA_FMR_ACCESS_FLAGS	= IB_ACCESS_REMOTE_WRITE |
					  IB_ACCESS_REMOTE_READ,
};

bool
fmr_is_supported(struct rpcrdma_ia *ia)
{
	if (!ia->ri_device->alloc_fmr) {
		pr_info("rpcrdma: 'fmr' mode is not supported by device %s\n",
			ia->ri_device->name);
		return false;
	}
	return true;
}

static int
__fmr_init(struct rpcrdma_mw *mw, struct ib_pd *pd)
{
	static struct ib_fmr_attr fmr_attr = {
		.max_pages	= RPCRDMA_MAX_FMR_SGES,
		.max_maps	= 1,
		.page_shift	= PAGE_SHIFT
	};

	mw->fmr.fm_physaddrs = kcalloc(RPCRDMA_MAX_FMR_SGES,
				       sizeof(u64), GFP_KERNEL);
	if (!mw->fmr.fm_physaddrs)
		goto out_free;

	mw->mw_sg = kcalloc(RPCRDMA_MAX_FMR_SGES,
			    sizeof(*mw->mw_sg), GFP_KERNEL);
	if (!mw->mw_sg)
		goto out_free;

	sg_init_table(mw->mw_sg, RPCRDMA_MAX_FMR_SGES);

	mw->fmr.fm_mr = ib_alloc_fmr(pd, RPCRDMA_FMR_ACCESS_FLAGS,
				     &fmr_attr);
	if (IS_ERR(mw->fmr.fm_mr))
		goto out_fmr_err;

	return 0;

out_fmr_err:
	dprintk("RPC:       %s: ib_alloc_fmr returned %ld\n", __func__,
		PTR_ERR(mw->fmr.fm_mr));

out_free:
	kfree(mw->mw_sg);
	kfree(mw->fmr.fm_physaddrs);
	return -ENOMEM;
}

static int
__fmr_unmap(struct rpcrdma_mw *mw)
{
	LIST_HEAD(l);
	int rc;

	list_add(&mw->fmr.fm_mr->list, &l);
	rc = ib_unmap_fmr(&l);
	list_del_init(&mw->fmr.fm_mr->list);
	return rc;
}

static void
__fmr_release(struct rpcrdma_mw *r)
{
	LIST_HEAD(unmap_list);
	int rc;

	kfree(r->fmr.fm_physaddrs);
	kfree(r->mw_sg);

	/* In case this one was left mapped, try to unmap it
	 * to prevent dealloc_fmr from failing with EBUSY
	 */
	rc = __fmr_unmap(r);
	if (rc)
		pr_err("rpcrdma: final ib_unmap_fmr for %p failed %i\n",
		       r, rc);

	rc = ib_dealloc_fmr(r->fmr.fm_mr);
	if (rc)
		pr_err("rpcrdma: final ib_dealloc_fmr for %p returned %i\n",
		       r, rc);
}

/* Reset of a single FMR.
 *
 * There's no recovery if this fails. The FMR is abandoned, but
 * remains in rb_all. It will be cleaned up when the transport is
 * destroyed.
 */
static void
fmr_op_recover_mr(struct rpcrdma_mw *mw)
{
	struct rpcrdma_xprt *r_xprt = mw->mw_xprt;
	int rc;

	/* ORDER: invalidate first */
	rc = __fmr_unmap(mw);

	/* ORDER: then DMA unmap */
	ib_dma_unmap_sg(r_xprt->rx_ia.ri_device,
			mw->mw_sg, mw->mw_nents, mw->mw_dir);
	if (rc) {
		pr_err("rpcrdma: FMR reset status %d, %p orphaned\n",
		       rc, mw);
		r_xprt->rx_stats.mrs_orphaned++;
		return;
	}

	rpcrdma_put_mw(r_xprt, mw);
	r_xprt->rx_stats.mrs_recovered++;
}

static int
fmr_op_open(struct rpcrdma_ia *ia, struct rpcrdma_ep *ep,
	    struct rpcrdma_create_data_internal *cdata)
{
	rpcrdma_set_max_header_sizes(ia, cdata, max_t(unsigned int, 1,
						      RPCRDMA_MAX_DATA_SEGS /
						      RPCRDMA_MAX_FMR_SGES));
	return 0;
}

/* FMR mode conveys up to 64 pages of payload per chunk segment.
 */
static size_t
fmr_op_maxpages(struct rpcrdma_xprt *r_xprt)
{
	return min_t(unsigned int, RPCRDMA_MAX_DATA_SEGS,
		     RPCRDMA_MAX_HDR_SEGS * RPCRDMA_MAX_FMR_SGES);
}

static int
fmr_op_init(struct rpcrdma_xprt *r_xprt)
{
	struct rpcrdma_buffer *buf = &r_xprt->rx_buf;
	struct ib_pd *pd = r_xprt->rx_ia.ri_pd;
	struct rpcrdma_mw *r;
	int i, rc;

	spin_lock_init(&buf->rb_mwlock);
	INIT_LIST_HEAD(&buf->rb_mws);
	INIT_LIST_HEAD(&buf->rb_all);

	i = max_t(int, RPCRDMA_MAX_DATA_SEGS / RPCRDMA_MAX_FMR_SGES, 1);
	i += 2;				/* head + tail */
	i *= buf->rb_max_requests;	/* one set for each RPC slot */
	dprintk("RPC:       %s: initalizing %d FMRs\n", __func__, i);

	while (i--) {
		r = kzalloc(sizeof(*r), GFP_KERNEL);
		if (!r)
			return -ENOMEM;

		rc = __fmr_init(r, pd);
		if (rc) {
			kfree(r);
			return rc;
		}

		r->mw_xprt = r_xprt;
		list_add(&r->mw_list, &buf->rb_mws);
		list_add(&r->mw_all, &buf->rb_all);
	}
	return 0;
}

/* Use the ib_map_phys_fmr() verb to register a memory region
 * for remote access via RDMA READ or RDMA WRITE.
 */
static int
fmr_op_map(struct rpcrdma_xprt *r_xprt, struct rpcrdma_mr_seg *seg,
	   int nsegs, bool writing)
{
	struct rpcrdma_mr_seg *seg1 = seg;
	int len, pageoff, i, rc;
	struct rpcrdma_mw *mw;
	u64 *dma_pages;

	mw = seg1->rl_mw;
	seg1->rl_mw = NULL;
	if (mw)
		rpcrdma_defer_mr_recovery(mw);
	mw = rpcrdma_get_mw(r_xprt);
	if (!mw)
		return -ENOMEM;

	pageoff = offset_in_page(seg1->mr_offset);
	seg1->mr_offset -= pageoff;	/* start of page */
	seg1->mr_len += pageoff;
	len = -pageoff;
	if (nsegs > RPCRDMA_MAX_FMR_SGES)
		nsegs = RPCRDMA_MAX_FMR_SGES;
	for (i = 0; i < nsegs;) {
		if (seg->mr_page)
			sg_set_page(&mw->mw_sg[i],
				    seg->mr_page,
				    seg->mr_len,
				    offset_in_page(seg->mr_offset));
		else
			sg_set_buf(&mw->mw_sg[i], seg->mr_offset,
				   seg->mr_len);
		len += seg->mr_len;
		++seg;
		++i;
		/* Check for holes */
		if ((i < nsegs && offset_in_page(seg->mr_offset)) ||
		    offset_in_page((seg-1)->mr_offset + (seg-1)->mr_len))
			break;
	}
	mw->mw_nents = i;
	mw->mw_dir = rpcrdma_data_dir(writing);

	if (!ib_dma_map_sg(r_xprt->rx_ia.ri_device,
			   mw->mw_sg, mw->mw_nents, mw->mw_dir))
		goto out_dmamap_err;

	for (i = 0, dma_pages = mw->fmr.fm_physaddrs; i < mw->mw_nents; i++)
		dma_pages[i] = sg_dma_address(&mw->mw_sg[i]);
	rc = ib_map_phys_fmr(mw->fmr.fm_mr, dma_pages, mw->mw_nents,
			     dma_pages[0]);
	if (rc)
		goto out_maperr;

	seg1->rl_mw = mw;
	seg1->mr_rkey = mw->fmr.fm_mr->rkey;
	seg1->mr_base = dma_pages[0] + pageoff;
	seg1->mr_nsegs = mw->mw_nents;
	seg1->mr_len = len;
	return mw->mw_nents;

out_dmamap_err:
	pr_err("rpcrdma: failed to dma map sg %p sg_nents %u\n",
	       mw->mw_sg, mw->mw_nents);
	rpcrdma_defer_mr_recovery(mw);
	return -ENOMEM;

out_maperr:
	pr_err("rpcrdma: ib_map_phys_fmr %u@0x%llx+%i (%d) status %i\n",
	       len, (unsigned long long)dma_pages[0],
	       pageoff, mw->mw_nents, rc);
	rpcrdma_defer_mr_recovery(mw);
	return rc;
}

/* Invalidate all memory regions that were registered for "req".
 *
 * Sleeps until it is safe for the host CPU to access the
 * previously mapped memory regions.
 */
static void
fmr_op_unmap_sync(struct rpcrdma_xprt *r_xprt, struct rpcrdma_req *req)
{
	struct rpcrdma_mr_seg *seg;
	unsigned int i, nchunks;
	struct rpcrdma_mw *mw;
	LIST_HEAD(unmap_list);
	int rc;

	dprintk("RPC:       %s: req %p\n", __func__, req);

	/* ORDER: Invalidate all of the req's MRs first
	 *
	 * ib_unmap_fmr() is slow, so use a single call instead
	 * of one call per mapped FMR.
	 */
	for (i = 0, nchunks = req->rl_nchunks; nchunks; nchunks--) {
		seg = &req->rl_segments[i];
		mw = seg->rl_mw;

		list_add_tail(&mw->fmr.fm_mr->list, &unmap_list);

		i += seg->mr_nsegs;
	}
	rc = ib_unmap_fmr(&unmap_list);
	if (rc)
		goto out_reset;

	/* ORDER: Now DMA unmap all of the req's MRs, and return
	 * them to the free MW list.
	 */
	for (i = 0, nchunks = req->rl_nchunks; nchunks; nchunks--) {
		seg = &req->rl_segments[i];
		mw = seg->rl_mw;

		list_del_init(&mw->fmr.fm_mr->list);
		ib_dma_unmap_sg(r_xprt->rx_ia.ri_device,
				mw->mw_sg, mw->mw_nents, mw->mw_dir);
		rpcrdma_put_mw(r_xprt, mw);

		i += seg->mr_nsegs;
		seg->mr_nsegs = 0;
		seg->rl_mw = NULL;
	}

	req->rl_nchunks = 0;
	return;

out_reset:
	pr_err("rpcrdma: ib_unmap_fmr failed (%i)\n", rc);

	for (i = 0, nchunks = req->rl_nchunks; nchunks; nchunks--) {
		seg = &req->rl_segments[i];
		mw = seg->rl_mw;

		list_del_init(&mw->fmr.fm_mr->list);
		fmr_op_recover_mr(mw);

		i += seg->mr_nsegs;
	}
}

/* Use a slow, safe mechanism to invalidate all memory regions
 * that were registered for "req".
 */
static void
fmr_op_unmap_safe(struct rpcrdma_xprt *r_xprt, struct rpcrdma_req *req,
		  bool sync)
{
	struct rpcrdma_mr_seg *seg;
	struct rpcrdma_mw *mw;
	unsigned int i;

	for (i = 0; req->rl_nchunks; req->rl_nchunks--) {
		seg = &req->rl_segments[i];
		mw = seg->rl_mw;

		if (sync)
			fmr_op_recover_mr(mw);
		else
			rpcrdma_defer_mr_recovery(mw);

		i += seg->mr_nsegs;
		seg->mr_nsegs = 0;
		seg->rl_mw = NULL;
	}
}

static void
fmr_op_destroy(struct rpcrdma_buffer *buf)
{
	struct rpcrdma_mw *r;

	while (!list_empty(&buf->rb_all)) {
		r = list_entry(buf->rb_all.next, struct rpcrdma_mw, mw_all);
		list_del(&r->mw_all);
		__fmr_release(r);
		kfree(r);
	}
}

const struct rpcrdma_memreg_ops rpcrdma_fmr_memreg_ops = {
	.ro_map				= fmr_op_map,
	.ro_unmap_sync			= fmr_op_unmap_sync,
	.ro_unmap_safe			= fmr_op_unmap_safe,
	.ro_recover_mr			= fmr_op_recover_mr,
	.ro_open			= fmr_op_open,
	.ro_maxpages			= fmr_op_maxpages,
	.ro_init			= fmr_op_init,
	.ro_destroy			= fmr_op_destroy,
	.ro_displayname			= "fmr",
};
