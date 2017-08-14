/*
 * Copyright (c) 2015 Oracle.  All rights reserved.
 * Copyright (c) 2003-2007 Network Appliance, Inc. All rights reserved.
 */

/* Lightweight memory registration using Fast Registration Work
 * Requests (FRWR). Also referred to sometimes as FRMR mode.
 *
 * FRWR features ordered asynchronous registration and deregistration
 * of arbitrarily sized memory regions. This is the fastest and safest
 * but most complex memory registration mode.
 */

/* Normal operation
 *
 * A Memory Region is prepared for RDMA READ or WRITE using a FAST_REG
 * Work Request (frmr_op_map). When the RDMA operation is finished, this
 * Memory Region is invalidated using a LOCAL_INV Work Request
 * (frmr_op_unmap).
 *
 * Typically these Work Requests are not signaled, and neither are RDMA
 * SEND Work Requests (with the exception of signaling occasionally to
 * prevent provider work queue overflows). This greatly reduces HCA
 * interrupt workload.
 *
 * As an optimization, frwr_op_unmap marks MRs INVALID before the
 * LOCAL_INV WR is posted. If posting succeeds, the MR is placed on
 * rb_mws immediately so that no work (like managing a linked list
 * under a spinlock) is needed in the completion upcall.
 *
 * But this means that frwr_op_map() can occasionally encounter an MR
 * that is INVALID but the LOCAL_INV WR has not completed. Work Queue
 * ordering prevents a subsequent FAST_REG WR from executing against
 * that MR while it is still being invalidated.
 */

/* Transport recovery
 *
 * ->op_map and the transport connect worker cannot run at the same
 * time, but ->op_unmap can fire while the transport connect worker
 * is running. Thus MR recovery is handled in ->op_map, to guarantee
 * that recovered MRs are owned by a sending RPC, and not one where
 * ->op_unmap could fire at the same time transport reconnect is
 * being done.
 *
 * When the underlying transport disconnects, MRs are left in one of
 * four states:
 *
 * INVALID:	The MR was not in use before the QP entered ERROR state.
 *
 * VALID:	The MR was registered before the QP entered ERROR state.
 *
 * FLUSHED_FR:	The MR was being registered when the QP entered ERROR
 *		state, and the pending WR was flushed.
 *
 * FLUSHED_LI:	The MR was being invalidated when the QP entered ERROR
 *		state, and the pending WR was flushed.
 *
 * When frwr_op_map encounters FLUSHED and VALID MRs, they are recovered
 * with ib_dereg_mr and then are re-initialized. Because MR recovery
 * allocates fresh resources, it is deferred to a workqueue, and the
 * recovered MRs are placed back on the rb_mws list when recovery is
 * complete. frwr_op_map allocates another MR for the current RPC while
 * the broken MR is reset.
 *
 * To ensure that frwr_op_map doesn't encounter an MR that is marked
 * INVALID but that is about to be flushed due to a previous transport
 * disconnect, the transport connect worker attempts to drain all
 * pending send queue WRs before the transport is reconnected.
 */

#include <linux/sunrpc/rpc_rdma.h>

#include "xprt_rdma.h"

#if IS_ENABLED(CONFIG_SUNRPC_DEBUG)
# define RPCDBG_FACILITY	RPCDBG_TRANS
#endif

bool
frwr_is_supported(struct rpcrdma_ia *ia)
{
	struct ib_device_attr *attrs = &ia->ri_device->attrs;

	if (!(attrs->device_cap_flags & IB_DEVICE_MEM_MGT_EXTENSIONS))
		goto out_not_supported;
	if (attrs->max_fast_reg_page_list_len == 0)
		goto out_not_supported;
	return true;

out_not_supported:
	pr_info("rpcrdma: 'frwr' mode is not supported by device %s\n",
		ia->ri_device->name);
	return false;
}

static int
frwr_op_init_mr(struct rpcrdma_ia *ia, struct rpcrdma_mw *r)
{
	unsigned int depth = ia->ri_max_frmr_depth;
	struct rpcrdma_frmr *f = &r->frmr;
	int rc;

	f->fr_mr = ib_alloc_mr(ia->ri_pd, ia->ri_mrtype, depth);
	if (IS_ERR(f->fr_mr))
		goto out_mr_err;

	r->mw_sg = kcalloc(depth, sizeof(*r->mw_sg), GFP_KERNEL);
	if (!r->mw_sg)
		goto out_list_err;

	sg_init_table(r->mw_sg, depth);
	init_completion(&f->fr_linv_done);
	return 0;

out_mr_err:
	rc = PTR_ERR(f->fr_mr);
	dprintk("RPC:       %s: ib_alloc_mr status %i\n",
		__func__, rc);
	return rc;

out_list_err:
	rc = -ENOMEM;
	dprintk("RPC:       %s: sg allocation failure\n",
		__func__);
	ib_dereg_mr(f->fr_mr);
	return rc;
}

static void
frwr_op_release_mr(struct rpcrdma_mw *r)
{
	int rc;

	/* Ensure MW is not on any rl_registered list */
	if (!list_empty(&r->mw_list))
		list_del(&r->mw_list);

	rc = ib_dereg_mr(r->frmr.fr_mr);
	if (rc)
		pr_err("rpcrdma: final ib_dereg_mr for %p returned %i\n",
		       r, rc);
	kfree(r->mw_sg);
	kfree(r);
}

static int
__frwr_reset_mr(struct rpcrdma_ia *ia, struct rpcrdma_mw *r)
{
	struct rpcrdma_frmr *f = &r->frmr;
	int rc;

	rc = ib_dereg_mr(f->fr_mr);
	if (rc) {
		pr_warn("rpcrdma: ib_dereg_mr status %d, frwr %p orphaned\n",
			rc, r);
		return rc;
	}

	f->fr_mr = ib_alloc_mr(ia->ri_pd, ia->ri_mrtype,
			       ia->ri_max_frmr_depth);
	if (IS_ERR(f->fr_mr)) {
		pr_warn("rpcrdma: ib_alloc_mr status %ld, frwr %p orphaned\n",
			PTR_ERR(f->fr_mr), r);
		return PTR_ERR(f->fr_mr);
	}

	dprintk("RPC:       %s: recovered FRMR %p\n", __func__, f);
	f->fr_state = FRMR_IS_INVALID;
	return 0;
}

/* Reset of a single FRMR. Generate a fresh rkey by replacing the MR.
 */
static void
frwr_op_recover_mr(struct rpcrdma_mw *mw)
{
	enum rpcrdma_frmr_state state = mw->frmr.fr_state;
	struct rpcrdma_xprt *r_xprt = mw->mw_xprt;
	struct rpcrdma_ia *ia = &r_xprt->rx_ia;
	int rc;

	rc = __frwr_reset_mr(ia, mw);
	if (state != FRMR_FLUSHED_LI)
		ib_dma_unmap_sg(ia->ri_device,
				mw->mw_sg, mw->mw_nents, mw->mw_dir);
	if (rc)
		goto out_release;

	rpcrdma_put_mw(r_xprt, mw);
	r_xprt->rx_stats.mrs_recovered++;
	return;

out_release:
	pr_err("rpcrdma: FRMR reset failed %d, %p release\n", rc, mw);
	r_xprt->rx_stats.mrs_orphaned++;

	spin_lock(&r_xprt->rx_buf.rb_mwlock);
	list_del(&mw->mw_all);
	spin_unlock(&r_xprt->rx_buf.rb_mwlock);

	frwr_op_release_mr(mw);
}

static int
frwr_op_open(struct rpcrdma_ia *ia, struct rpcrdma_ep *ep,
	     struct rpcrdma_create_data_internal *cdata)
{
	struct ib_device_attr *attrs = &ia->ri_device->attrs;
	int depth, delta;

	ia->ri_mrtype = IB_MR_TYPE_MEM_REG;
	if (attrs->device_cap_flags & IB_DEVICE_SG_GAPS_REG)
		ia->ri_mrtype = IB_MR_TYPE_SG_GAPS;

	ia->ri_max_frmr_depth =
			min_t(unsigned int, RPCRDMA_MAX_DATA_SEGS,
			      attrs->max_fast_reg_page_list_len);
	dprintk("RPC:       %s: device's max FR page list len = %u\n",
		__func__, ia->ri_max_frmr_depth);

	/* Add room for frmr register and invalidate WRs.
	 * 1. FRMR reg WR for head
	 * 2. FRMR invalidate WR for head
	 * 3. N FRMR reg WRs for pagelist
	 * 4. N FRMR invalidate WRs for pagelist
	 * 5. FRMR reg WR for tail
	 * 6. FRMR invalidate WR for tail
	 * 7. The RDMA_SEND WR
	 */
	depth = 7;

	/* Calculate N if the device max FRMR depth is smaller than
	 * RPCRDMA_MAX_DATA_SEGS.
	 */
	if (ia->ri_max_frmr_depth < RPCRDMA_MAX_DATA_SEGS) {
		delta = RPCRDMA_MAX_DATA_SEGS - ia->ri_max_frmr_depth;
		do {
			depth += 2; /* FRMR reg + invalidate */
			delta -= ia->ri_max_frmr_depth;
		} while (delta > 0);
	}

	ep->rep_attr.cap.max_send_wr *= depth;
	if (ep->rep_attr.cap.max_send_wr > attrs->max_qp_wr) {
		cdata->max_requests = attrs->max_qp_wr / depth;
		if (!cdata->max_requests)
			return -EINVAL;
		ep->rep_attr.cap.max_send_wr = cdata->max_requests *
					       depth;
	}

	ia->ri_max_segs = max_t(unsigned int, 1, RPCRDMA_MAX_DATA_SEGS /
				ia->ri_max_frmr_depth);
	return 0;
}

/* FRWR mode conveys a list of pages per chunk segment. The
 * maximum length of that list is the FRWR page list depth.
 */
static size_t
frwr_op_maxpages(struct rpcrdma_xprt *r_xprt)
{
	struct rpcrdma_ia *ia = &r_xprt->rx_ia;

	return min_t(unsigned int, RPCRDMA_MAX_DATA_SEGS,
		     RPCRDMA_MAX_HDR_SEGS * ia->ri_max_frmr_depth);
}

static void
__frwr_sendcompletion_flush(struct ib_wc *wc, const char *wr)
{
	if (wc->status != IB_WC_WR_FLUSH_ERR)
		pr_err("rpcrdma: %s: %s (%u/0x%x)\n",
		       wr, ib_wc_status_msg(wc->status),
		       wc->status, wc->vendor_err);
}

/**
 * frwr_wc_fastreg - Invoked by RDMA provider for a flushed FastReg WC
 * @cq:	completion queue (ignored)
 * @wc:	completed WR
 *
 */
static void
frwr_wc_fastreg(struct ib_cq *cq, struct ib_wc *wc)
{
	struct rpcrdma_frmr *frmr;
	struct ib_cqe *cqe;

	/* WARNING: Only wr_cqe and status are reliable at this point */
	if (wc->status != IB_WC_SUCCESS) {
		cqe = wc->wr_cqe;
		frmr = container_of(cqe, struct rpcrdma_frmr, fr_cqe);
		frmr->fr_state = FRMR_FLUSHED_FR;
		__frwr_sendcompletion_flush(wc, "fastreg");
	}
}

/**
 * frwr_wc_localinv - Invoked by RDMA provider for a flushed LocalInv WC
 * @cq:	completion queue (ignored)
 * @wc:	completed WR
 *
 */
static void
frwr_wc_localinv(struct ib_cq *cq, struct ib_wc *wc)
{
	struct rpcrdma_frmr *frmr;
	struct ib_cqe *cqe;

	/* WARNING: Only wr_cqe and status are reliable at this point */
	if (wc->status != IB_WC_SUCCESS) {
		cqe = wc->wr_cqe;
		frmr = container_of(cqe, struct rpcrdma_frmr, fr_cqe);
		frmr->fr_state = FRMR_FLUSHED_LI;
		__frwr_sendcompletion_flush(wc, "localinv");
	}
}

/**
 * frwr_wc_localinv_wake - Invoked by RDMA provider for a signaled LocalInv WC
 * @cq:	completion queue (ignored)
 * @wc:	completed WR
 *
 * Awaken anyone waiting for an MR to finish being fenced.
 */
static void
frwr_wc_localinv_wake(struct ib_cq *cq, struct ib_wc *wc)
{
	struct rpcrdma_frmr *frmr;
	struct ib_cqe *cqe;

	/* WARNING: Only wr_cqe and status are reliable at this point */
	cqe = wc->wr_cqe;
	frmr = container_of(cqe, struct rpcrdma_frmr, fr_cqe);
	if (wc->status != IB_WC_SUCCESS) {
		frmr->fr_state = FRMR_FLUSHED_LI;
		__frwr_sendcompletion_flush(wc, "localinv");
	}
	complete(&frmr->fr_linv_done);
}

/* Post a REG_MR Work Request to register a memory region
 * for remote access via RDMA READ or RDMA WRITE.
 */
static struct rpcrdma_mr_seg *
frwr_op_map(struct rpcrdma_xprt *r_xprt, struct rpcrdma_mr_seg *seg,
	    int nsegs, bool writing, struct rpcrdma_mw **out)
{
	struct rpcrdma_ia *ia = &r_xprt->rx_ia;
	bool holes_ok = ia->ri_mrtype == IB_MR_TYPE_SG_GAPS;
	struct rpcrdma_mw *mw;
	struct rpcrdma_frmr *frmr;
	struct ib_mr *mr;
	struct ib_reg_wr *reg_wr;
	struct ib_send_wr *bad_wr;
	int rc, i, n;
	u8 key;

	mw = NULL;
	do {
		if (mw)
			rpcrdma_defer_mr_recovery(mw);
		mw = rpcrdma_get_mw(r_xprt);
		if (!mw)
			return ERR_PTR(-ENOBUFS);
	} while (mw->frmr.fr_state != FRMR_IS_INVALID);
	frmr = &mw->frmr;
	frmr->fr_state = FRMR_IS_VALID;
	mr = frmr->fr_mr;
	reg_wr = &frmr->fr_regwr;

	if (nsegs > ia->ri_max_frmr_depth)
		nsegs = ia->ri_max_frmr_depth;
	for (i = 0; i < nsegs;) {
		if (seg->mr_page)
			sg_set_page(&mw->mw_sg[i],
				    seg->mr_page,
				    seg->mr_len,
				    offset_in_page(seg->mr_offset));
		else
			sg_set_buf(&mw->mw_sg[i], seg->mr_offset,
				   seg->mr_len);

		++seg;
		++i;
		if (holes_ok)
			continue;
		if ((i < nsegs && offset_in_page(seg->mr_offset)) ||
		    offset_in_page((seg-1)->mr_offset + (seg-1)->mr_len))
			break;
	}
	mw->mw_dir = rpcrdma_data_dir(writing);

	mw->mw_nents = ib_dma_map_sg(ia->ri_device, mw->mw_sg, i, mw->mw_dir);
	if (!mw->mw_nents)
		goto out_dmamap_err;

	n = ib_map_mr_sg(mr, mw->mw_sg, mw->mw_nents, NULL, PAGE_SIZE);
	if (unlikely(n != mw->mw_nents))
		goto out_mapmr_err;

	dprintk("RPC:       %s: Using frmr %p to map %u segments (%u bytes)\n",
		__func__, frmr, mw->mw_nents, mr->length);

	key = (u8)(mr->rkey & 0x000000FF);
	ib_update_fast_reg_key(mr, ++key);

	reg_wr->wr.next = NULL;
	reg_wr->wr.opcode = IB_WR_REG_MR;
	frmr->fr_cqe.done = frwr_wc_fastreg;
	reg_wr->wr.wr_cqe = &frmr->fr_cqe;
	reg_wr->wr.num_sge = 0;
	reg_wr->wr.send_flags = 0;
	reg_wr->mr = mr;
	reg_wr->key = mr->rkey;
	reg_wr->access = writing ?
			 IB_ACCESS_REMOTE_WRITE | IB_ACCESS_LOCAL_WRITE :
			 IB_ACCESS_REMOTE_READ;

	rpcrdma_set_signaled(&r_xprt->rx_ep, &reg_wr->wr);
	rc = ib_post_send(ia->ri_id->qp, &reg_wr->wr, &bad_wr);
	if (rc)
		goto out_senderr;

	mw->mw_handle = mr->rkey;
	mw->mw_length = mr->length;
	mw->mw_offset = mr->iova;

	*out = mw;
	return seg;

out_dmamap_err:
	pr_err("rpcrdma: failed to DMA map sg %p sg_nents %d\n",
	       mw->mw_sg, i);
	frmr->fr_state = FRMR_IS_INVALID;
	rpcrdma_put_mw(r_xprt, mw);
	return ERR_PTR(-EIO);

out_mapmr_err:
	pr_err("rpcrdma: failed to map mr %p (%d/%d)\n",
	       frmr->fr_mr, n, mw->mw_nents);
	rpcrdma_defer_mr_recovery(mw);
	return ERR_PTR(-EIO);

out_senderr:
	pr_err("rpcrdma: FRMR registration ib_post_send returned %i\n", rc);
	rpcrdma_defer_mr_recovery(mw);
	return ERR_PTR(-ENOTCONN);
}

/* Invalidate all memory regions that were registered for "req".
 *
 * Sleeps until it is safe for the host CPU to access the
 * previously mapped memory regions.
 *
 * Caller ensures that @mws is not empty before the call. This
 * function empties the list.
 */
static void
frwr_op_unmap_sync(struct rpcrdma_xprt *r_xprt, struct list_head *mws)
{
	struct ib_send_wr *first, **prev, *last, *bad_wr;
	struct rpcrdma_ia *ia = &r_xprt->rx_ia;
	struct rpcrdma_frmr *f;
	struct rpcrdma_mw *mw;
	int count, rc;

	/* ORDER: Invalidate all of the MRs first
	 *
	 * Chain the LOCAL_INV Work Requests and post them with
	 * a single ib_post_send() call.
	 */
	f = NULL;
	count = 0;
	prev = &first;
	list_for_each_entry(mw, mws, mw_list) {
		mw->frmr.fr_state = FRMR_IS_INVALID;

		if (mw->mw_flags & RPCRDMA_MW_F_RI)
			continue;

		f = &mw->frmr;
		dprintk("RPC:       %s: invalidating frmr %p\n",
			__func__, f);

		f->fr_cqe.done = frwr_wc_localinv;
		last = &f->fr_invwr;
		memset(last, 0, sizeof(*last));
		last->wr_cqe = &f->fr_cqe;
		last->opcode = IB_WR_LOCAL_INV;
		last->ex.invalidate_rkey = mw->mw_handle;
		count++;

		*prev = last;
		prev = &last->next;
	}
	if (!f)
		goto unmap;

	/* Strong send queue ordering guarantees that when the
	 * last WR in the chain completes, all WRs in the chain
	 * are complete.
	 */
	last->send_flags = IB_SEND_SIGNALED;
	f->fr_cqe.done = frwr_wc_localinv_wake;
	reinit_completion(&f->fr_linv_done);

	/* Initialize CQ count, since there is always a signaled
	 * WR being posted here.  The new cqcount depends on how
	 * many SQEs are about to be consumed.
	 */
	rpcrdma_init_cqcount(&r_xprt->rx_ep, count);

	/* Transport disconnect drains the receive CQ before it
	 * replaces the QP. The RPC reply handler won't call us
	 * unless ri_id->qp is a valid pointer.
	 */
	r_xprt->rx_stats.local_inv_needed++;
	bad_wr = NULL;
	rc = ib_post_send(ia->ri_id->qp, first, &bad_wr);
	if (bad_wr != first)
		wait_for_completion(&f->fr_linv_done);
	if (rc)
		goto reset_mrs;

	/* ORDER: Now DMA unmap all of the MRs, and return
	 * them to the free MW list.
	 */
unmap:
	while (!list_empty(mws)) {
		mw = rpcrdma_pop_mw(mws);
		dprintk("RPC:       %s: DMA unmapping frmr %p\n",
			__func__, &mw->frmr);
		ib_dma_unmap_sg(ia->ri_device,
				mw->mw_sg, mw->mw_nents, mw->mw_dir);
		rpcrdma_put_mw(r_xprt, mw);
	}
	return;

reset_mrs:
	pr_err("rpcrdma: FRMR invalidate ib_post_send returned %i\n", rc);

	/* Find and reset the MRs in the LOCAL_INV WRs that did not
	 * get posted.
	 */
	rpcrdma_init_cqcount(&r_xprt->rx_ep, -count);
	while (bad_wr) {
		f = container_of(bad_wr, struct rpcrdma_frmr,
				 fr_invwr);
		mw = container_of(f, struct rpcrdma_mw, frmr);

		__frwr_reset_mr(ia, mw);

		bad_wr = bad_wr->next;
	}
	goto unmap;
}

/* Use a slow, safe mechanism to invalidate all memory regions
 * that were registered for "req".
 */
static void
frwr_op_unmap_safe(struct rpcrdma_xprt *r_xprt, struct rpcrdma_req *req,
		   bool sync)
{
	struct rpcrdma_mw *mw;

	while (!list_empty(&req->rl_registered)) {
		mw = rpcrdma_pop_mw(&req->rl_registered);
		if (sync)
			frwr_op_recover_mr(mw);
		else
			rpcrdma_defer_mr_recovery(mw);
	}
}

const struct rpcrdma_memreg_ops rpcrdma_frwr_memreg_ops = {
	.ro_map				= frwr_op_map,
	.ro_unmap_sync			= frwr_op_unmap_sync,
	.ro_unmap_safe			= frwr_op_unmap_safe,
	.ro_recover_mr			= frwr_op_recover_mr,
	.ro_open			= frwr_op_open,
	.ro_maxpages			= frwr_op_maxpages,
	.ro_init_mr			= frwr_op_init_mr,
	.ro_release_mr			= frwr_op_release_mr,
	.ro_displayname			= "frwr",
	.ro_send_w_inv_ok		= RPCRDMA_CMP_F_SND_W_INV_OK,
};
