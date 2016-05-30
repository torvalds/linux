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
 * three states:
 *
 * INVALID:	The MR was not in use before the QP entered ERROR state.
 *		(Or, the LOCAL_INV WR has not completed or flushed yet).
 *
 * STALE:	The MR was being registered or unregistered when the QP
 *		entered ERROR state, and the pending WR was flushed.
 *
 * VALID:	The MR was registered before the QP entered ERROR state.
 *
 * When frwr_op_map encounters STALE and VALID MRs, they are recovered
 * with ib_dereg_mr and then are re-initialized. Beause MR recovery
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

#include "xprt_rdma.h"

#if IS_ENABLED(CONFIG_SUNRPC_DEBUG)
# define RPCDBG_FACILITY	RPCDBG_TRANS
#endif

static struct workqueue_struct *frwr_recovery_wq;

#define FRWR_RECOVERY_WQ_FLAGS		(WQ_UNBOUND | WQ_MEM_RECLAIM)

int
frwr_alloc_recovery_wq(void)
{
	frwr_recovery_wq = alloc_workqueue("frwr_recovery",
					   FRWR_RECOVERY_WQ_FLAGS, 0);
	return !frwr_recovery_wq ? -ENOMEM : 0;
}

void
frwr_destroy_recovery_wq(void)
{
	struct workqueue_struct *wq;

	if (!frwr_recovery_wq)
		return;

	wq = frwr_recovery_wq;
	frwr_recovery_wq = NULL;
	destroy_workqueue(wq);
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

	f->fr_mr = ib_alloc_mr(ia->ri_pd, IB_MR_TYPE_MEM_REG,
			       ia->ri_max_frmr_depth);
	if (IS_ERR(f->fr_mr)) {
		pr_warn("rpcrdma: ib_alloc_mr status %ld, frwr %p orphaned\n",
			PTR_ERR(f->fr_mr), r);
		return PTR_ERR(f->fr_mr);
	}

	dprintk("RPC:       %s: recovered FRMR %p\n", __func__, r);
	f->fr_state = FRMR_IS_INVALID;
	return 0;
}

static void
__frwr_reset_and_unmap(struct rpcrdma_xprt *r_xprt, struct rpcrdma_mw *mw)
{
	struct rpcrdma_ia *ia = &r_xprt->rx_ia;
	struct rpcrdma_frmr *f = &mw->frmr;
	int rc;

	rc = __frwr_reset_mr(ia, mw);
	ib_dma_unmap_sg(ia->ri_device, f->fr_sg, f->fr_nents, f->fr_dir);
	if (rc)
		return;

	rpcrdma_put_mw(r_xprt, mw);
}

/* Deferred reset of a single FRMR. Generate a fresh rkey by
 * replacing the MR.
 *
 * There's no recovery if this fails. The FRMR is abandoned, but
 * remains in rb_all. It will be cleaned up when the transport is
 * destroyed.
 */
static void
__frwr_recovery_worker(struct work_struct *work)
{
	struct rpcrdma_mw *r = container_of(work, struct rpcrdma_mw,
					    mw_work);

	__frwr_reset_and_unmap(r->mw_xprt, r);
	return;
}

/* A broken MR was discovered in a context that can't sleep.
 * Defer recovery to the recovery worker.
 */
static void
__frwr_queue_recovery(struct rpcrdma_mw *r)
{
	INIT_WORK(&r->mw_work, __frwr_recovery_worker);
	queue_work(frwr_recovery_wq, &r->mw_work);
}

static int
__frwr_init(struct rpcrdma_mw *r, struct ib_pd *pd, struct ib_device *device,
	    unsigned int depth)
{
	struct rpcrdma_frmr *f = &r->frmr;
	int rc;

	f->fr_mr = ib_alloc_mr(pd, IB_MR_TYPE_MEM_REG, depth);
	if (IS_ERR(f->fr_mr))
		goto out_mr_err;

	f->fr_sg = kcalloc(depth, sizeof(*f->fr_sg), GFP_KERNEL);
	if (!f->fr_sg)
		goto out_list_err;

	sg_init_table(f->fr_sg, depth);

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
__frwr_release(struct rpcrdma_mw *r)
{
	int rc;

	rc = ib_dereg_mr(r->frmr.fr_mr);
	if (rc)
		dprintk("RPC:       %s: ib_dereg_mr status %i\n",
			__func__, rc);
	kfree(r->frmr.fr_sg);
}

static int
frwr_op_open(struct rpcrdma_ia *ia, struct rpcrdma_ep *ep,
	     struct rpcrdma_create_data_internal *cdata)
{
	int depth, delta;

	ia->ri_max_frmr_depth =
			min_t(unsigned int, RPCRDMA_MAX_DATA_SEGS,
			      ia->ri_device->attrs.max_fast_reg_page_list_len);
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
	if (ep->rep_attr.cap.max_send_wr > ia->ri_device->attrs.max_qp_wr) {
		cdata->max_requests = ia->ri_device->attrs.max_qp_wr / depth;
		if (!cdata->max_requests)
			return -EINVAL;
		ep->rep_attr.cap.max_send_wr = cdata->max_requests *
					       depth;
	}

	rpcrdma_set_max_header_sizes(ia, cdata, max_t(unsigned int, 1,
						      RPCRDMA_MAX_DATA_SEGS /
						      ia->ri_max_frmr_depth));
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
__frwr_sendcompletion_flush(struct ib_wc *wc, struct rpcrdma_frmr *frmr,
			    const char *wr)
{
	frmr->fr_state = FRMR_IS_STALE;
	if (wc->status != IB_WC_WR_FLUSH_ERR)
		pr_err("rpcrdma: %s: %s (%u/0x%x)\n",
		       wr, ib_wc_status_msg(wc->status),
		       wc->status, wc->vendor_err);
}

/**
 * frwr_wc_fastreg - Invoked by RDMA provider for each polled FastReg WC
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
		__frwr_sendcompletion_flush(wc, frmr, "fastreg");
	}
}

/**
 * frwr_wc_localinv - Invoked by RDMA provider for each polled LocalInv WC
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
		__frwr_sendcompletion_flush(wc, frmr, "localinv");
	}
}

/**
 * frwr_wc_localinv - Invoked by RDMA provider for each polled LocalInv WC
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
	if (wc->status != IB_WC_SUCCESS)
		__frwr_sendcompletion_flush(wc, frmr, "localinv");
	complete_all(&frmr->fr_linv_done);
}

static int
frwr_op_init(struct rpcrdma_xprt *r_xprt)
{
	struct rpcrdma_buffer *buf = &r_xprt->rx_buf;
	struct ib_device *device = r_xprt->rx_ia.ri_device;
	unsigned int depth = r_xprt->rx_ia.ri_max_frmr_depth;
	struct ib_pd *pd = r_xprt->rx_ia.ri_pd;
	int i;

	spin_lock_init(&buf->rb_mwlock);
	INIT_LIST_HEAD(&buf->rb_mws);
	INIT_LIST_HEAD(&buf->rb_all);

	i = max_t(int, RPCRDMA_MAX_DATA_SEGS / depth, 1);
	i += 2;				/* head + tail */
	i *= buf->rb_max_requests;	/* one set for each RPC slot */
	dprintk("RPC:       %s: initalizing %d FRMRs\n", __func__, i);

	while (i--) {
		struct rpcrdma_mw *r;
		int rc;

		r = kzalloc(sizeof(*r), GFP_KERNEL);
		if (!r)
			return -ENOMEM;

		rc = __frwr_init(r, pd, device, depth);
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

/* Post a FAST_REG Work Request to register a memory region
 * for remote access via RDMA READ or RDMA WRITE.
 */
static int
frwr_op_map(struct rpcrdma_xprt *r_xprt, struct rpcrdma_mr_seg *seg,
	    int nsegs, bool writing)
{
	struct rpcrdma_ia *ia = &r_xprt->rx_ia;
	struct ib_device *device = ia->ri_device;
	enum dma_data_direction direction = rpcrdma_data_dir(writing);
	struct rpcrdma_mr_seg *seg1 = seg;
	struct rpcrdma_mw *mw;
	struct rpcrdma_frmr *frmr;
	struct ib_mr *mr;
	struct ib_reg_wr *reg_wr;
	struct ib_send_wr *bad_wr;
	int rc, i, n, dma_nents;
	u8 key;

	mw = seg1->rl_mw;
	seg1->rl_mw = NULL;
	do {
		if (mw)
			__frwr_queue_recovery(mw);
		mw = rpcrdma_get_mw(r_xprt);
		if (!mw)
			return -ENOMEM;
	} while (mw->frmr.fr_state != FRMR_IS_INVALID);
	frmr = &mw->frmr;
	frmr->fr_state = FRMR_IS_VALID;
	mr = frmr->fr_mr;
	reg_wr = &frmr->fr_regwr;

	if (nsegs > ia->ri_max_frmr_depth)
		nsegs = ia->ri_max_frmr_depth;

	for (i = 0; i < nsegs;) {
		if (seg->mr_page)
			sg_set_page(&frmr->fr_sg[i],
				    seg->mr_page,
				    seg->mr_len,
				    offset_in_page(seg->mr_offset));
		else
			sg_set_buf(&frmr->fr_sg[i], seg->mr_offset,
				   seg->mr_len);

		++seg;
		++i;

		/* Check for holes */
		if ((i < nsegs && offset_in_page(seg->mr_offset)) ||
		    offset_in_page((seg-1)->mr_offset + (seg-1)->mr_len))
			break;
	}
	frmr->fr_nents = i;
	frmr->fr_dir = direction;

	dma_nents = ib_dma_map_sg(device, frmr->fr_sg, frmr->fr_nents, direction);
	if (!dma_nents) {
		pr_err("RPC:       %s: failed to dma map sg %p sg_nents %u\n",
		       __func__, frmr->fr_sg, frmr->fr_nents);
		return -ENOMEM;
	}

	n = ib_map_mr_sg(mr, frmr->fr_sg, frmr->fr_nents, NULL, PAGE_SIZE);
	if (unlikely(n != frmr->fr_nents)) {
		pr_err("RPC:       %s: failed to map mr %p (%u/%u)\n",
		       __func__, frmr->fr_mr, n, frmr->fr_nents);
		rc = n < 0 ? n : -EINVAL;
		goto out_senderr;
	}

	dprintk("RPC:       %s: Using frmr %p to map %u segments (%u bytes)\n",
		__func__, mw, frmr->fr_nents, mr->length);

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

	DECR_CQCOUNT(&r_xprt->rx_ep);
	rc = ib_post_send(ia->ri_id->qp, &reg_wr->wr, &bad_wr);
	if (rc)
		goto out_senderr;

	seg1->rl_mw = mw;
	seg1->mr_rkey = mr->rkey;
	seg1->mr_base = mr->iova;
	seg1->mr_nsegs = frmr->fr_nents;
	seg1->mr_len = mr->length;

	return frmr->fr_nents;

out_senderr:
	dprintk("RPC:       %s: ib_post_send status %i\n", __func__, rc);
	__frwr_queue_recovery(mw);
	return rc;
}

static struct ib_send_wr *
__frwr_prepare_linv_wr(struct rpcrdma_mr_seg *seg)
{
	struct rpcrdma_mw *mw = seg->rl_mw;
	struct rpcrdma_frmr *f = &mw->frmr;
	struct ib_send_wr *invalidate_wr;

	f->fr_state = FRMR_IS_INVALID;
	invalidate_wr = &f->fr_invwr;

	memset(invalidate_wr, 0, sizeof(*invalidate_wr));
	f->fr_cqe.done = frwr_wc_localinv;
	invalidate_wr->wr_cqe = &f->fr_cqe;
	invalidate_wr->opcode = IB_WR_LOCAL_INV;
	invalidate_wr->ex.invalidate_rkey = f->fr_mr->rkey;

	return invalidate_wr;
}

/* Invalidate all memory regions that were registered for "req".
 *
 * Sleeps until it is safe for the host CPU to access the
 * previously mapped memory regions.
 */
static void
frwr_op_unmap_sync(struct rpcrdma_xprt *r_xprt, struct rpcrdma_req *req)
{
	struct ib_send_wr *invalidate_wrs, *pos, *prev, *bad_wr;
	struct rpcrdma_ia *ia = &r_xprt->rx_ia;
	struct rpcrdma_mr_seg *seg;
	unsigned int i, nchunks;
	struct rpcrdma_frmr *f;
	struct rpcrdma_mw *mw;
	int rc;

	dprintk("RPC:       %s: req %p\n", __func__, req);

	/* ORDER: Invalidate all of the req's MRs first
	 *
	 * Chain the LOCAL_INV Work Requests and post them with
	 * a single ib_post_send() call.
	 */
	invalidate_wrs = pos = prev = NULL;
	seg = NULL;
	for (i = 0, nchunks = req->rl_nchunks; nchunks; nchunks--) {
		seg = &req->rl_segments[i];

		pos = __frwr_prepare_linv_wr(seg);

		if (!invalidate_wrs)
			invalidate_wrs = pos;
		else
			prev->next = pos;
		prev = pos;

		i += seg->mr_nsegs;
	}
	f = &seg->rl_mw->frmr;

	/* Strong send queue ordering guarantees that when the
	 * last WR in the chain completes, all WRs in the chain
	 * are complete.
	 */
	f->fr_invwr.send_flags = IB_SEND_SIGNALED;
	f->fr_cqe.done = frwr_wc_localinv_wake;
	reinit_completion(&f->fr_linv_done);
	INIT_CQCOUNT(&r_xprt->rx_ep);

	/* Transport disconnect drains the receive CQ before it
	 * replaces the QP. The RPC reply handler won't call us
	 * unless ri_id->qp is a valid pointer.
	 */
	rc = ib_post_send(ia->ri_id->qp, invalidate_wrs, &bad_wr);
	if (rc)
		goto reset_mrs;

	wait_for_completion(&f->fr_linv_done);

	/* ORDER: Now DMA unmap all of the req's MRs, and return
	 * them to the free MW list.
	 */
unmap:
	for (i = 0, nchunks = req->rl_nchunks; nchunks; nchunks--) {
		seg = &req->rl_segments[i];
		mw = seg->rl_mw;
		seg->rl_mw = NULL;

		ib_dma_unmap_sg(ia->ri_device, f->fr_sg, f->fr_nents,
				f->fr_dir);
		rpcrdma_put_mw(r_xprt, mw);

		i += seg->mr_nsegs;
		seg->mr_nsegs = 0;
	}

	req->rl_nchunks = 0;
	return;

reset_mrs:
	pr_warn("%s: ib_post_send failed %i\n", __func__, rc);

	/* Find and reset the MRs in the LOCAL_INV WRs that did not
	 * get posted. This is synchronous, and slow.
	 */
	for (i = 0, nchunks = req->rl_nchunks; nchunks; nchunks--) {
		seg = &req->rl_segments[i];
		mw = seg->rl_mw;
		f = &mw->frmr;

		if (mw->frmr.fr_mr->rkey == bad_wr->ex.invalidate_rkey) {
			__frwr_reset_mr(ia, mw);
			bad_wr = bad_wr->next;
		}

		i += seg->mr_nsegs;
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
	struct rpcrdma_mr_seg *seg;
	struct rpcrdma_mw *mw;
	unsigned int i;

	for (i = 0; req->rl_nchunks; req->rl_nchunks--) {
		seg = &req->rl_segments[i];
		mw = seg->rl_mw;

		if (sync)
			__frwr_reset_and_unmap(r_xprt, mw);
		else
			__frwr_queue_recovery(mw);

		i += seg->mr_nsegs;
		seg->mr_nsegs = 0;
		seg->rl_mw = NULL;
	}
}

static void
frwr_op_destroy(struct rpcrdma_buffer *buf)
{
	struct rpcrdma_mw *r;

	/* Ensure stale MWs for "buf" are no longer in flight */
	flush_workqueue(frwr_recovery_wq);

	while (!list_empty(&buf->rb_all)) {
		r = list_entry(buf->rb_all.next, struct rpcrdma_mw, mw_all);
		list_del(&r->mw_all);
		__frwr_release(r);
		kfree(r);
	}
}

const struct rpcrdma_memreg_ops rpcrdma_frwr_memreg_ops = {
	.ro_map				= frwr_op_map,
	.ro_unmap_sync			= frwr_op_unmap_sync,
	.ro_unmap_safe			= frwr_op_unmap_safe,
	.ro_open			= frwr_op_open,
	.ro_maxpages			= frwr_op_maxpages,
	.ro_init			= frwr_op_init,
	.ro_destroy			= frwr_op_destroy,
	.ro_displayname			= "frwr",
};
