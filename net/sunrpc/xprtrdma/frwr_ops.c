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

#include "xprt_rdma.h"

#if IS_ENABLED(CONFIG_SUNRPC_DEBUG)
# define RPCDBG_FACILITY	RPCDBG_TRANS
#endif

static int
__frwr_init(struct rpcrdma_mw *r, struct ib_pd *pd, struct ib_device *device,
	    unsigned int depth)
{
	struct rpcrdma_frmr *f = &r->r.frmr;
	int rc;

	f->fr_mr = ib_alloc_fast_reg_mr(pd, depth);
	if (IS_ERR(f->fr_mr))
		goto out_mr_err;
	f->fr_pgl = ib_alloc_fast_reg_page_list(device, depth);
	if (IS_ERR(f->fr_pgl))
		goto out_list_err;
	return 0;

out_mr_err:
	rc = PTR_ERR(f->fr_mr);
	dprintk("RPC:       %s: ib_alloc_fast_reg_mr status %i\n",
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
	dprintk("RPC:       %s: frmr %p (stale), status %d\n",
		__func__, r, wc->status);
	r->r.frmr.fr_state = FRMR_IS_STALE;
}

static int
frwr_op_init(struct rpcrdma_xprt *r_xprt)
{
	struct rpcrdma_buffer *buf = &r_xprt->rx_buf;
	struct ib_device *device = r_xprt->rx_ia.ri_id->device;
	unsigned int depth = r_xprt->rx_ia.ri_max_frmr_depth;
	struct ib_pd *pd = r_xprt->rx_ia.ri_pd;
	int i;

	INIT_LIST_HEAD(&buf->rb_mws);
	INIT_LIST_HEAD(&buf->rb_all);

	i = (buf->rb_max_requests + 1) * RPCRDMA_MAX_SEGS;
	dprintk("RPC:       %s: initializing %d FRMRs\n", __func__, i);

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
	struct ib_device *device = ia->ri_id->device;
	enum dma_data_direction direction = rpcrdma_data_dir(writing);
	struct rpcrdma_mr_seg *seg1 = seg;
	struct rpcrdma_mw *mw = seg1->rl_mw;
	struct rpcrdma_frmr *frmr = &mw->r.frmr;
	struct ib_mr *mr = frmr->fr_mr;
	struct ib_send_wr fastreg_wr, *bad_wr;
	u8 key;
	int len, pageoff;
	int i, rc;
	int seg_len;
	u64 pa;
	int page_no;

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

	frmr->fr_state = FRMR_IS_VALID;

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
	key = (u8)(mr->rkey & 0x000000FF);
	ib_update_fast_reg_key(mr, ++key);
	fastreg_wr.wr.fast_reg.rkey = mr->rkey;

	DECR_CQCOUNT(&r_xprt->rx_ep);
	rc = ib_post_send(ia->ri_id->qp, &fastreg_wr, &bad_wr);
	if (rc)
		goto out_senderr;

	seg1->mr_rkey = mr->rkey;
	seg1->mr_base = seg1->mr_dma + pageoff;
	seg1->mr_nsegs = i;
	seg1->mr_len = len;
	return i;

out_senderr:
	dprintk("RPC:       %s: ib_post_send status %i\n", __func__, rc);
	ib_update_fast_reg_key(mr, --key);
	frmr->fr_state = FRMR_IS_INVALID;
	while (i--)
		rpcrdma_unmap_one(device, --seg);
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
	struct ib_send_wr invalidate_wr, *bad_wr;
	int rc, nsegs = seg->mr_nsegs;
	struct ib_device *device;

	seg1->rl_mw->r.frmr.fr_state = FRMR_IS_INVALID;

	memset(&invalidate_wr, 0, sizeof(invalidate_wr));
	invalidate_wr.wr_id = (unsigned long)(void *)seg1->rl_mw;
	invalidate_wr.opcode = IB_WR_LOCAL_INV;
	invalidate_wr.ex.invalidate_rkey = seg1->rl_mw->r.frmr.fr_mr->rkey;
	DECR_CQCOUNT(&r_xprt->rx_ep);

	read_lock(&ia->ri_qplock);
	device = ia->ri_id->device;
	while (seg1->mr_nsegs--)
		rpcrdma_unmap_one(device, seg++);
	rc = ib_post_send(ia->ri_id->qp, &invalidate_wr, &bad_wr);
	read_unlock(&ia->ri_qplock);
	if (rc)
		goto out_err;
	return nsegs;

out_err:
	/* Force rpcrdma_buffer_get() to retry */
	seg1->rl_mw->r.frmr.fr_state = FRMR_IS_STALE;
	dprintk("RPC:       %s: ib_post_send status %i\n", __func__, rc);
	return nsegs;
}

/* After a disconnect, a flushed FAST_REG_MR can leave an FRMR in
 * an unusable state. Find FRMRs in this state and dereg / reg
 * each.  FRMRs that are VALID and attached to an rpcrdma_req are
 * also torn down.
 *
 * This gives all in-use FRMRs a fresh rkey and leaves them INVALID.
 *
 * This is invoked only in the transport connect worker in order
 * to serialize with rpcrdma_register_frmr_external().
 */
static void
frwr_op_reset(struct rpcrdma_xprt *r_xprt)
{
	struct rpcrdma_buffer *buf = &r_xprt->rx_buf;
	struct ib_device *device = r_xprt->rx_ia.ri_id->device;
	unsigned int depth = r_xprt->rx_ia.ri_max_frmr_depth;
	struct ib_pd *pd = r_xprt->rx_ia.ri_pd;
	struct rpcrdma_mw *r;
	int rc;

	list_for_each_entry(r, &buf->rb_all, mw_all) {
		if (r->r.frmr.fr_state == FRMR_IS_INVALID)
			continue;

		__frwr_release(r);
		rc = __frwr_init(r, pd, device, depth);
		if (rc) {
			dprintk("RPC:       %s: mw %p left %s\n",
				__func__, r,
				(r->r.frmr.fr_state == FRMR_IS_STALE ?
					"stale" : "valid"));
			continue;
		}

		r->r.frmr.fr_state = FRMR_IS_INVALID;
	}
}

static void
frwr_op_destroy(struct rpcrdma_buffer *buf)
{
	struct rpcrdma_mw *r;

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
	.ro_reset			= frwr_op_reset,
	.ro_destroy			= frwr_op_destroy,
	.ro_displayname			= "frwr",
};
