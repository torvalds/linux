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
					    r.frmr.fr_work);
	struct rpcrdma_xprt *r_xprt = r->r.frmr.fr_xprt;
	unsigned int depth = r_xprt->rx_ia.ri_max_frmr_depth;
	struct ib_pd *pd = r_xprt->rx_ia.ri_pd;

	if (ib_dereg_mr(r->r.frmr.fr_mr))
		goto out_fail;

	r->r.frmr.fr_mr = ib_alloc_mr(pd, IB_MR_TYPE_MEM_REG, depth);
	if (IS_ERR(r->r.frmr.fr_mr))
		goto out_fail;

	dprintk("RPC:       %s: recovered FRMR %p\n", __func__, r);
	r->r.frmr.fr_state = FRMR_IS_INVALID;
	rpcrdma_put_mw(r_xprt, r);
	return;

out_fail:
	pr_warn("RPC:       %s: FRMR %p unrecovered\n",
		__func__, r);
}

/* A broken MR was discovered in a context that can't sleep.
 * Defer recovery to the recovery worker.
 */
static void
__frwr_queue_recovery(struct rpcrdma_mw *r)
{
	INIT_WORK(&r->r.frmr.fr_work, __frwr_recovery_worker);
	queue_work(frwr_recovery_wq, &r->r.frmr.fr_work);
}

static int
__frwr_init(struct rpcrdma_mw *r, struct ib_pd *pd, struct ib_device *device,
	    unsigned int depth)
{
	struct rpcrdma_frmr *f = &r->r.frmr;
	int rc;

	f->fr_mr = ib_alloc_mr(pd, IB_MR_TYPE_MEM_REG, depth);
	if (IS_ERR(f->fr_mr))
		goto out_mr_err;
	f->fr_pgl = ib_alloc_fast_reg_page_list(device, depth);
	if (IS_ERR(f->fr_pgl))
		goto out_list_err;
	return 0;

out_mr_err:
	rc = PTR_ERR(f->fr_mr);
	dprintk("RPC:       %s: ib_alloc_mr status %i\n",
		__func__, rc);
	return rc;

out_list_err:
	rc = PTR_ERR(f->fr_pgl);
	dprintk("RPC:       %s: ib_alloc_fast_reg_page_list status %i\n",
		__func__, rc);
	ib_dereg_mr(f->fr_mr);
	return rc;
}

static void
__frwr_release(struct rpcrdma_mw *r)
{
	int rc;

	rc = ib_dereg_mr(r->r.frmr.fr_mr);
	if (rc)
		dprintk("RPC:       %s: ib_dereg_mr status %i\n",
			__func__, rc);
	ib_free_fast_reg_page_list(r->r.frmr.fr_pgl);
}

static int
frwr_op_open(struct rpcrdma_ia *ia, struct rpcrdma_ep *ep,
	     struct rpcrdma_create_data_internal *cdata)
{
	struct ib_device_attr *devattr = &ia->ri_devattr;
	int depth, delta;

	ia->ri_max_frmr_depth =
			min_t(unsigned int, RPCRDMA_MAX_DATA_SEGS,
			      devattr->max_fast_reg_page_list_len);
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
	if (ep->rep_attr.cap.max_send_wr > devattr->max_qp_wr) {
		cdata->max_requests = devattr->max_qp_wr / depth;
		if (!cdata->max_requests)
			return -EINVAL;
		ep->rep_attr.cap.max_send_wr = cdata->max_requests *
					       depth;
	}

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
		     rpcrdma_max_segments(r_xprt) * ia->ri_max_frmr_depth);
}

/* If FAST_REG or LOCAL_INV failed, indicate the frmr needs to be reset. */
static void
frwr_sendcompletion(struct ib_wc *wc)
{
	struct rpcrdma_mw *r;

	if (likely(wc->status == IB_WC_SUCCESS))
		return;

	/* WARNING: Only wr_id and status are reliable at this point */
	r = (struct rpcrdma_mw *)(unsigned long)wc->wr_id;
	pr_warn("RPC:       %s: frmr %p flushed, status %s (%d)\n",
		__func__, r, ib_wc_status_msg(wc->status), wc->status);
	r->r.frmr.fr_state = FRMR_IS_STALE;
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

		list_add(&r->mw_list, &buf->rb_mws);
		list_add(&r->mw_all, &buf->rb_all);
		r->mw_sendcompletion = frwr_sendcompletion;
		r->r.frmr.fr_xprt = r_xprt;
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
	struct ib_send_wr fastreg_wr, *bad_wr;
	u8 key;
	int len, pageoff;
	int i, rc;
	int seg_len;
	u64 pa;
	int page_no;

	mw = seg1->rl_mw;
	seg1->rl_mw = NULL;
	do {
		if (mw)
			__frwr_queue_recovery(mw);
		mw = rpcrdma_get_mw(r_xprt);
		if (!mw)
			return -ENOMEM;
	} while (mw->r.frmr.fr_state != FRMR_IS_INVALID);
	frmr = &mw->r.frmr;
	frmr->fr_state = FRMR_IS_VALID;

	pageoff = offset_in_page(seg1->mr_offset);
	seg1->mr_offset -= pageoff;	/* start of page */
	seg1->mr_len += pageoff;
	len = -pageoff;
	if (nsegs > ia->ri_max_frmr_depth)
		nsegs = ia->ri_max_frmr_depth;

	for (page_no = i = 0; i < nsegs;) {
		rpcrdma_map_one(device, seg, direction);
		pa = seg->mr_dma;
		for (seg_len = seg->mr_len; seg_len > 0; seg_len -= PAGE_SIZE) {
			frmr->fr_pgl->page_list[page_no++] = pa;
			pa += PAGE_SIZE;
		}
		len += seg->mr_len;
		++seg;
		++i;
		/* Check for holes */
		if ((i < nsegs && offset_in_page(seg->mr_offset)) ||
		    offset_in_page((seg-1)->mr_offset + (seg-1)->mr_len))
			break;
	}
	dprintk("RPC:       %s: Using frmr %p to map %d segments (%d bytes)\n",
		__func__, mw, i, len);

	memset(&fastreg_wr, 0, sizeof(fastreg_wr));
	fastreg_wr.wr_id = (unsigned long)(void *)mw;
	fastreg_wr.opcode = IB_WR_FAST_REG_MR;
	fastreg_wr.wr.fast_reg.iova_start = seg1->mr_dma + pageoff;
	fastreg_wr.wr.fast_reg.page_list = frmr->fr_pgl;
	fastreg_wr.wr.fast_reg.page_shift = PAGE_SHIFT;
	fastreg_wr.wr.fast_reg.page_list_len = page_no;
	fastreg_wr.wr.fast_reg.length = len;
	fastreg_wr.wr.fast_reg.access_flags = writing ?
				IB_ACCESS_REMOTE_WRITE | IB_ACCESS_LOCAL_WRITE :
				IB_ACCESS_REMOTE_READ;
	mr = frmr->fr_mr;
	key = (u8)(mr->rkey & 0x000000FF);
	ib_update_fast_reg_key(mr, ++key);
	fastreg_wr.wr.fast_reg.rkey = mr->rkey;

	DECR_CQCOUNT(&r_xprt->rx_ep);
	rc = ib_post_send(ia->ri_id->qp, &fastreg_wr, &bad_wr);
	if (rc)
		goto out_senderr;

	seg1->rl_mw = mw;
	seg1->mr_rkey = mr->rkey;
	seg1->mr_base = seg1->mr_dma + pageoff;
	seg1->mr_nsegs = i;
	seg1->mr_len = len;
	return i;

out_senderr:
	dprintk("RPC:       %s: ib_post_send status %i\n", __func__, rc);
	while (i--)
		rpcrdma_unmap_one(device, --seg);
	__frwr_queue_recovery(mw);
	return rc;
}

/* Post a LOCAL_INV Work Request to prevent further remote access
 * via RDMA READ or RDMA WRITE.
 */
static int
frwr_op_unmap(struct rpcrdma_xprt *r_xprt, struct rpcrdma_mr_seg *seg)
{
	struct rpcrdma_mr_seg *seg1 = seg;
	struct rpcrdma_ia *ia = &r_xprt->rx_ia;
	struct rpcrdma_mw *mw = seg1->rl_mw;
	struct ib_send_wr invalidate_wr, *bad_wr;
	int rc, nsegs = seg->mr_nsegs;

	dprintk("RPC:       %s: FRMR %p\n", __func__, mw);

	seg1->rl_mw = NULL;
	mw->r.frmr.fr_state = FRMR_IS_INVALID;

	memset(&invalidate_wr, 0, sizeof(invalidate_wr));
	invalidate_wr.wr_id = (unsigned long)(void *)mw;
	invalidate_wr.opcode = IB_WR_LOCAL_INV;
	invalidate_wr.ex.invalidate_rkey = mw->r.frmr.fr_mr->rkey;
	DECR_CQCOUNT(&r_xprt->rx_ep);

	while (seg1->mr_nsegs--)
		rpcrdma_unmap_one(ia->ri_device, seg++);
	read_lock(&ia->ri_qplock);
	rc = ib_post_send(ia->ri_id->qp, &invalidate_wr, &bad_wr);
	read_unlock(&ia->ri_qplock);
	if (rc)
		goto out_err;

	rpcrdma_put_mw(r_xprt, mw);
	return nsegs;

out_err:
	dprintk("RPC:       %s: ib_post_send status %i\n", __func__, rc);
	__frwr_queue_recovery(mw);
	return nsegs;
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
	.ro_unmap			= frwr_op_unmap,
	.ro_open			= frwr_op_open,
	.ro_maxpages			= frwr_op_maxpages,
	.ro_init			= frwr_op_init,
	.ro_destroy			= frwr_op_destroy,
	.ro_displayname			= "frwr",
};
