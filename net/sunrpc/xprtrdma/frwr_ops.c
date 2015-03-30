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

/* Post a FAST_REG Work Request to register a memory region
 * for remote access via RDMA READ or RDMA WRITE.
 */
static int
frwr_op_map(struct rpcrdma_xprt *r_xprt, struct rpcrdma_mr_seg *seg,
	    int nsegs, bool writing)
{
	struct rpcrdma_ia *ia = &r_xprt->rx_ia;
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
		rpcrdma_map_one(ia, seg, writing);
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
		rpcrdma_unmap_one(ia, --seg);
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

	seg1->rl_mw->r.frmr.fr_state = FRMR_IS_INVALID;

	memset(&invalidate_wr, 0, sizeof(invalidate_wr));
	invalidate_wr.wr_id = (unsigned long)(void *)seg1->rl_mw;
	invalidate_wr.opcode = IB_WR_LOCAL_INV;
	invalidate_wr.ex.invalidate_rkey = seg1->rl_mw->r.frmr.fr_mr->rkey;
	DECR_CQCOUNT(&r_xprt->rx_ep);

	read_lock(&ia->ri_qplock);
	while (seg1->mr_nsegs--)
		rpcrdma_unmap_one(ia, seg++);
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

const struct rpcrdma_memreg_ops rpcrdma_frwr_memreg_ops = {
	.ro_map				= frwr_op_map,
	.ro_unmap			= frwr_op_unmap,
	.ro_maxpages			= frwr_op_maxpages,
	.ro_displayname			= "frwr",
};
