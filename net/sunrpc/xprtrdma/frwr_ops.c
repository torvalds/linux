// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2015, 2017 Oracle.  All rights reserved.
 * Copyright (c) 2003-2007 Network Appliance, Inc. All rights reserved.
 */

/* Lightweight memory registration using Fast Registration Work
 * Requests (FRWR).
 *
 * FRWR features ordered asynchronous registration and invalidation
 * of arbitrarily-sized memory regions. This is the fastest and safest
 * but most complex memory registration mode.
 */

/* Normal operation
 *
 * A Memory Region is prepared for RDMA Read or Write using a FAST_REG
 * Work Request (frwr_map). When the RDMA operation is finished, this
 * Memory Region is invalidated using a LOCAL_INV Work Request
 * (frwr_unmap_async and frwr_unmap_sync).
 *
 * Typically FAST_REG Work Requests are not signaled, and neither are
 * RDMA Send Work Requests (with the exception of signaling occasionally
 * to prevent provider work queue overflows). This greatly reduces HCA
 * interrupt workload.
 */

/* Transport recovery
 *
 * frwr_map and frwr_unmap_* cannot run at the same time the transport
 * connect worker is running. The connect worker holds the transport
 * send lock, just as ->send_request does. This prevents frwr_map and
 * the connect worker from running concurrently. When a connection is
 * closed, the Receive completion queue is drained before the allowing
 * the connect worker to get control. This prevents frwr_unmap and the
 * connect worker from running concurrently.
 *
 * When the underlying transport disconnects, MRs that are in flight
 * are flushed and are likely unusable. Thus all flushed MRs are
 * destroyed. New MRs are created on demand.
 */

#include <linux/sunrpc/rpc_rdma.h>
#include <linux/sunrpc/svc_rdma.h>

#include "xprt_rdma.h"
#include <trace/events/rpcrdma.h>

#if IS_ENABLED(CONFIG_SUNRPC_DEBUG)
# define RPCDBG_FACILITY	RPCDBG_TRANS
#endif

/**
 * frwr_is_supported - Check if device supports FRWR
 * @device: interface adapter to check
 *
 * Returns true if device supports FRWR, otherwise false
 */
bool frwr_is_supported(struct ib_device *device)
{
	struct ib_device_attr *attrs = &device->attrs;

	if (!(attrs->device_cap_flags & IB_DEVICE_MEM_MGT_EXTENSIONS))
		goto out_not_supported;
	if (attrs->max_fast_reg_page_list_len == 0)
		goto out_not_supported;
	return true;

out_not_supported:
	pr_info("rpcrdma: 'frwr' mode is not supported by device %s\n",
		device->name);
	return false;
}

/**
 * frwr_release_mr - Destroy one MR
 * @mr: MR allocated by frwr_init_mr
 *
 */
void frwr_release_mr(struct rpcrdma_mr *mr)
{
	int rc;

	rc = ib_dereg_mr(mr->frwr.fr_mr);
	if (rc)
		trace_xprtrdma_frwr_dereg(mr, rc);
	kfree(mr->mr_sg);
	kfree(mr);
}

static void frwr_mr_recycle(struct rpcrdma_xprt *r_xprt, struct rpcrdma_mr *mr)
{
	trace_xprtrdma_mr_recycle(mr);

	if (mr->mr_dir != DMA_NONE) {
		trace_xprtrdma_mr_unmap(mr);
		ib_dma_unmap_sg(r_xprt->rx_ia.ri_id->device,
				mr->mr_sg, mr->mr_nents, mr->mr_dir);
		mr->mr_dir = DMA_NONE;
	}

	spin_lock(&r_xprt->rx_buf.rb_lock);
	list_del(&mr->mr_all);
	r_xprt->rx_stats.mrs_recycled++;
	spin_unlock(&r_xprt->rx_buf.rb_lock);

	frwr_release_mr(mr);
}

/* MRs are dynamically allocated, so simply clean up and release the MR.
 * A replacement MR will subsequently be allocated on demand.
 */
static void
frwr_mr_recycle_worker(struct work_struct *work)
{
	struct rpcrdma_mr *mr = container_of(work, struct rpcrdma_mr,
					     mr_recycle);

	frwr_mr_recycle(mr->mr_xprt, mr);
}

/* frwr_recycle - Discard MRs
 * @req: request to reset
 *
 * Used after a reconnect. These MRs could be in flight, we can't
 * tell. Safe thing to do is release them.
 */
void frwr_recycle(struct rpcrdma_req *req)
{
	struct rpcrdma_mr *mr;

	while ((mr = rpcrdma_mr_pop(&req->rl_registered)))
		frwr_mr_recycle(mr->mr_xprt, mr);
}

/* frwr_reset - Place MRs back on the free list
 * @req: request to reset
 *
 * Used after a failed marshal. For FRWR, this means the MRs
 * don't have to be fully released and recreated.
 *
 * NB: This is safe only as long as none of @req's MRs are
 * involved with an ongoing asynchronous FAST_REG or LOCAL_INV
 * Work Request.
 */
void frwr_reset(struct rpcrdma_req *req)
{
	struct rpcrdma_mr *mr;

	while ((mr = rpcrdma_mr_pop(&req->rl_registered)))
		rpcrdma_mr_put(mr);
}

/**
 * frwr_init_mr - Initialize one MR
 * @ia: interface adapter
 * @mr: generic MR to prepare for FRWR
 *
 * Returns zero if successful. Otherwise a negative errno
 * is returned.
 */
int frwr_init_mr(struct rpcrdma_ia *ia, struct rpcrdma_mr *mr)
{
	unsigned int depth = ia->ri_max_frwr_depth;
	struct scatterlist *sg;
	struct ib_mr *frmr;
	int rc;

	/* NB: ib_alloc_mr and device drivers typically allocate
	 *     memory with GFP_KERNEL.
	 */
	frmr = ib_alloc_mr(ia->ri_pd, ia->ri_mrtype, depth);
	if (IS_ERR(frmr))
		goto out_mr_err;

	sg = kcalloc(depth, sizeof(*sg), GFP_NOFS);
	if (!sg)
		goto out_list_err;

	mr->frwr.fr_mr = frmr;
	mr->mr_dir = DMA_NONE;
	INIT_LIST_HEAD(&mr->mr_list);
	INIT_WORK(&mr->mr_recycle, frwr_mr_recycle_worker);
	init_completion(&mr->frwr.fr_linv_done);

	sg_init_table(sg, depth);
	mr->mr_sg = sg;
	return 0;

out_mr_err:
	rc = PTR_ERR(frmr);
	trace_xprtrdma_frwr_alloc(mr, rc);
	return rc;

out_list_err:
	ib_dereg_mr(frmr);
	return -ENOMEM;
}

/**
 * frwr_open - Prepare an endpoint for use with FRWR
 * @ia: interface adapter this endpoint will use
 * @ep: endpoint to prepare
 *
 * On success, sets:
 *	ep->rep_attr.cap.max_send_wr
 *	ep->rep_attr.cap.max_recv_wr
 *	ep->rep_max_requests
 *	ia->ri_max_segs
 *
 * And these FRWR-related fields:
 *	ia->ri_max_frwr_depth
 *	ia->ri_mrtype
 *
 * On failure, a negative errno is returned.
 */
int frwr_open(struct rpcrdma_ia *ia, struct rpcrdma_ep *ep)
{
	struct ib_device_attr *attrs = &ia->ri_id->device->attrs;
	int max_qp_wr, depth, delta;

	ia->ri_mrtype = IB_MR_TYPE_MEM_REG;
	if (attrs->device_cap_flags & IB_DEVICE_SG_GAPS_REG)
		ia->ri_mrtype = IB_MR_TYPE_SG_GAPS;

	/* Quirk: Some devices advertise a large max_fast_reg_page_list_len
	 * capability, but perform optimally when the MRs are not larger
	 * than a page.
	 */
	if (attrs->max_sge_rd > 1)
		ia->ri_max_frwr_depth = attrs->max_sge_rd;
	else
		ia->ri_max_frwr_depth = attrs->max_fast_reg_page_list_len;
	if (ia->ri_max_frwr_depth > RPCRDMA_MAX_DATA_SEGS)
		ia->ri_max_frwr_depth = RPCRDMA_MAX_DATA_SEGS;
	dprintk("RPC:       %s: max FR page list depth = %u\n",
		__func__, ia->ri_max_frwr_depth);

	/* Add room for frwr register and invalidate WRs.
	 * 1. FRWR reg WR for head
	 * 2. FRWR invalidate WR for head
	 * 3. N FRWR reg WRs for pagelist
	 * 4. N FRWR invalidate WRs for pagelist
	 * 5. FRWR reg WR for tail
	 * 6. FRWR invalidate WR for tail
	 * 7. The RDMA_SEND WR
	 */
	depth = 7;

	/* Calculate N if the device max FRWR depth is smaller than
	 * RPCRDMA_MAX_DATA_SEGS.
	 */
	if (ia->ri_max_frwr_depth < RPCRDMA_MAX_DATA_SEGS) {
		delta = RPCRDMA_MAX_DATA_SEGS - ia->ri_max_frwr_depth;
		do {
			depth += 2; /* FRWR reg + invalidate */
			delta -= ia->ri_max_frwr_depth;
		} while (delta > 0);
	}

	max_qp_wr = ia->ri_id->device->attrs.max_qp_wr;
	max_qp_wr -= RPCRDMA_BACKWARD_WRS;
	max_qp_wr -= 1;
	if (max_qp_wr < RPCRDMA_MIN_SLOT_TABLE)
		return -ENOMEM;
	if (ep->rep_max_requests > max_qp_wr)
		ep->rep_max_requests = max_qp_wr;
	ep->rep_attr.cap.max_send_wr = ep->rep_max_requests * depth;
	if (ep->rep_attr.cap.max_send_wr > max_qp_wr) {
		ep->rep_max_requests = max_qp_wr / depth;
		if (!ep->rep_max_requests)
			return -EINVAL;
		ep->rep_attr.cap.max_send_wr = ep->rep_max_requests * depth;
	}
	ep->rep_attr.cap.max_send_wr += RPCRDMA_BACKWARD_WRS;
	ep->rep_attr.cap.max_send_wr += 1; /* for ib_drain_sq */
	ep->rep_attr.cap.max_recv_wr = ep->rep_max_requests;
	ep->rep_attr.cap.max_recv_wr += RPCRDMA_BACKWARD_WRS;
	ep->rep_attr.cap.max_recv_wr += 1; /* for ib_drain_rq */

	ia->ri_max_segs =
		DIV_ROUND_UP(RPCRDMA_MAX_DATA_SEGS, ia->ri_max_frwr_depth);
	/* Reply chunks require segments for head and tail buffers */
	ia->ri_max_segs += 2;
	if (ia->ri_max_segs > RPCRDMA_MAX_HDR_SEGS)
		ia->ri_max_segs = RPCRDMA_MAX_HDR_SEGS;
	return 0;
}

/**
 * frwr_maxpages - Compute size of largest payload
 * @r_xprt: transport
 *
 * Returns maximum size of an RPC message, in pages.
 *
 * FRWR mode conveys a list of pages per chunk segment. The
 * maximum length of that list is the FRWR page list depth.
 */
size_t frwr_maxpages(struct rpcrdma_xprt *r_xprt)
{
	struct rpcrdma_ia *ia = &r_xprt->rx_ia;

	return min_t(unsigned int, RPCRDMA_MAX_DATA_SEGS,
		     (ia->ri_max_segs - 2) * ia->ri_max_frwr_depth);
}

/**
 * frwr_map - Register a memory region
 * @r_xprt: controlling transport
 * @seg: memory region co-ordinates
 * @nsegs: number of segments remaining
 * @writing: true when RDMA Write will be used
 * @xid: XID of RPC using the registered memory
 * @mr: MR to fill in
 *
 * Prepare a REG_MR Work Request to register a memory region
 * for remote access via RDMA READ or RDMA WRITE.
 *
 * Returns the next segment or a negative errno pointer.
 * On success, @mr is filled in.
 */
struct rpcrdma_mr_seg *frwr_map(struct rpcrdma_xprt *r_xprt,
				struct rpcrdma_mr_seg *seg,
				int nsegs, bool writing, __be32 xid,
				struct rpcrdma_mr *mr)
{
	struct rpcrdma_ia *ia = &r_xprt->rx_ia;
	struct ib_reg_wr *reg_wr;
	struct ib_mr *ibmr;
	int i, n;
	u8 key;

	if (nsegs > ia->ri_max_frwr_depth)
		nsegs = ia->ri_max_frwr_depth;
	for (i = 0; i < nsegs;) {
		if (seg->mr_page)
			sg_set_page(&mr->mr_sg[i],
				    seg->mr_page,
				    seg->mr_len,
				    offset_in_page(seg->mr_offset));
		else
			sg_set_buf(&mr->mr_sg[i], seg->mr_offset,
				   seg->mr_len);

		++seg;
		++i;
		if (ia->ri_mrtype == IB_MR_TYPE_SG_GAPS)
			continue;
		if ((i < nsegs && offset_in_page(seg->mr_offset)) ||
		    offset_in_page((seg-1)->mr_offset + (seg-1)->mr_len))
			break;
	}
	mr->mr_dir = rpcrdma_data_dir(writing);

	mr->mr_nents =
		ib_dma_map_sg(ia->ri_id->device, mr->mr_sg, i, mr->mr_dir);
	if (!mr->mr_nents)
		goto out_dmamap_err;

	ibmr = mr->frwr.fr_mr;
	n = ib_map_mr_sg(ibmr, mr->mr_sg, mr->mr_nents, NULL, PAGE_SIZE);
	if (unlikely(n != mr->mr_nents))
		goto out_mapmr_err;

	ibmr->iova &= 0x00000000ffffffff;
	ibmr->iova |= ((u64)be32_to_cpu(xid)) << 32;
	key = (u8)(ibmr->rkey & 0x000000FF);
	ib_update_fast_reg_key(ibmr, ++key);

	reg_wr = &mr->frwr.fr_regwr;
	reg_wr->mr = ibmr;
	reg_wr->key = ibmr->rkey;
	reg_wr->access = writing ?
			 IB_ACCESS_REMOTE_WRITE | IB_ACCESS_LOCAL_WRITE :
			 IB_ACCESS_REMOTE_READ;

	mr->mr_handle = ibmr->rkey;
	mr->mr_length = ibmr->length;
	mr->mr_offset = ibmr->iova;
	trace_xprtrdma_mr_map(mr);

	return seg;

out_dmamap_err:
	mr->mr_dir = DMA_NONE;
	trace_xprtrdma_frwr_sgerr(mr, i);
	return ERR_PTR(-EIO);

out_mapmr_err:
	trace_xprtrdma_frwr_maperr(mr, n);
	return ERR_PTR(-EIO);
}

/**
 * frwr_wc_fastreg - Invoked by RDMA provider for a flushed FastReg WC
 * @cq:	completion queue (ignored)
 * @wc:	completed WR
 *
 */
static void frwr_wc_fastreg(struct ib_cq *cq, struct ib_wc *wc)
{
	struct ib_cqe *cqe = wc->wr_cqe;
	struct rpcrdma_frwr *frwr =
		container_of(cqe, struct rpcrdma_frwr, fr_cqe);

	/* WARNING: Only wr_cqe and status are reliable at this point */
	trace_xprtrdma_wc_fastreg(wc, frwr);
	/* The MR will get recycled when the associated req is retransmitted */
}

/**
 * frwr_send - post Send WR containing the RPC Call message
 * @ia: interface adapter
 * @req: Prepared RPC Call
 *
 * For FRWR, chain any FastReg WRs to the Send WR. Only a
 * single ib_post_send call is needed to register memory
 * and then post the Send WR.
 *
 * Returns the result of ib_post_send.
 */
int frwr_send(struct rpcrdma_ia *ia, struct rpcrdma_req *req)
{
	struct ib_send_wr *post_wr;
	struct rpcrdma_mr *mr;

	post_wr = &req->rl_sendctx->sc_wr;
	list_for_each_entry(mr, &req->rl_registered, mr_list) {
		struct rpcrdma_frwr *frwr;

		frwr = &mr->frwr;

		frwr->fr_cqe.done = frwr_wc_fastreg;
		frwr->fr_regwr.wr.next = post_wr;
		frwr->fr_regwr.wr.wr_cqe = &frwr->fr_cqe;
		frwr->fr_regwr.wr.num_sge = 0;
		frwr->fr_regwr.wr.opcode = IB_WR_REG_MR;
		frwr->fr_regwr.wr.send_flags = 0;

		post_wr = &frwr->fr_regwr.wr;
	}

	/* If ib_post_send fails, the next ->send_request for
	 * @req will queue these MRs for recovery.
	 */
	return ib_post_send(ia->ri_id->qp, post_wr, NULL);
}

/**
 * frwr_reminv - handle a remotely invalidated mr on the @mrs list
 * @rep: Received reply
 * @mrs: list of MRs to check
 *
 */
void frwr_reminv(struct rpcrdma_rep *rep, struct list_head *mrs)
{
	struct rpcrdma_mr *mr;

	list_for_each_entry(mr, mrs, mr_list)
		if (mr->mr_handle == rep->rr_inv_rkey) {
			list_del_init(&mr->mr_list);
			trace_xprtrdma_mr_remoteinv(mr);
			rpcrdma_mr_put(mr);
			break;	/* only one invalidated MR per RPC */
		}
}

static void __frwr_release_mr(struct ib_wc *wc, struct rpcrdma_mr *mr)
{
	if (wc->status != IB_WC_SUCCESS)
		rpcrdma_mr_recycle(mr);
	else
		rpcrdma_mr_put(mr);
}

/**
 * frwr_wc_localinv - Invoked by RDMA provider for a LOCAL_INV WC
 * @cq:	completion queue (ignored)
 * @wc:	completed WR
 *
 */
static void frwr_wc_localinv(struct ib_cq *cq, struct ib_wc *wc)
{
	struct ib_cqe *cqe = wc->wr_cqe;
	struct rpcrdma_frwr *frwr =
		container_of(cqe, struct rpcrdma_frwr, fr_cqe);
	struct rpcrdma_mr *mr = container_of(frwr, struct rpcrdma_mr, frwr);

	/* WARNING: Only wr_cqe and status are reliable at this point */
	trace_xprtrdma_wc_li(wc, frwr);
	__frwr_release_mr(wc, mr);
}

/**
 * frwr_wc_localinv_wake - Invoked by RDMA provider for a LOCAL_INV WC
 * @cq:	completion queue (ignored)
 * @wc:	completed WR
 *
 * Awaken anyone waiting for an MR to finish being fenced.
 */
static void frwr_wc_localinv_wake(struct ib_cq *cq, struct ib_wc *wc)
{
	struct ib_cqe *cqe = wc->wr_cqe;
	struct rpcrdma_frwr *frwr =
		container_of(cqe, struct rpcrdma_frwr, fr_cqe);
	struct rpcrdma_mr *mr = container_of(frwr, struct rpcrdma_mr, frwr);

	/* WARNING: Only wr_cqe and status are reliable at this point */
	trace_xprtrdma_wc_li_wake(wc, frwr);
	__frwr_release_mr(wc, mr);
	complete(&frwr->fr_linv_done);
}

/**
 * frwr_unmap_sync - invalidate memory regions that were registered for @req
 * @r_xprt: controlling transport instance
 * @req: rpcrdma_req with a non-empty list of MRs to process
 *
 * Sleeps until it is safe for the host CPU to access the previously mapped
 * memory regions. This guarantees that registered MRs are properly fenced
 * from the server before the RPC consumer accesses the data in them. It
 * also ensures proper Send flow control: waking the next RPC waits until
 * this RPC has relinquished all its Send Queue entries.
 */
void frwr_unmap_sync(struct rpcrdma_xprt *r_xprt, struct rpcrdma_req *req)
{
	struct ib_send_wr *first, **prev, *last;
	const struct ib_send_wr *bad_wr;
	struct rpcrdma_frwr *frwr;
	struct rpcrdma_mr *mr;
	int rc;

	/* ORDER: Invalidate all of the MRs first
	 *
	 * Chain the LOCAL_INV Work Requests and post them with
	 * a single ib_post_send() call.
	 */
	frwr = NULL;
	prev = &first;
	while ((mr = rpcrdma_mr_pop(&req->rl_registered))) {

		trace_xprtrdma_mr_localinv(mr);
		r_xprt->rx_stats.local_inv_needed++;

		frwr = &mr->frwr;
		frwr->fr_cqe.done = frwr_wc_localinv;
		last = &frwr->fr_invwr;
		last->next = NULL;
		last->wr_cqe = &frwr->fr_cqe;
		last->sg_list = NULL;
		last->num_sge = 0;
		last->opcode = IB_WR_LOCAL_INV;
		last->send_flags = IB_SEND_SIGNALED;
		last->ex.invalidate_rkey = mr->mr_handle;

		*prev = last;
		prev = &last->next;
	}

	/* Strong send queue ordering guarantees that when the
	 * last WR in the chain completes, all WRs in the chain
	 * are complete.
	 */
	frwr->fr_cqe.done = frwr_wc_localinv_wake;
	reinit_completion(&frwr->fr_linv_done);

	/* Transport disconnect drains the receive CQ before it
	 * replaces the QP. The RPC reply handler won't call us
	 * unless ri_id->qp is a valid pointer.
	 */
	bad_wr = NULL;
	rc = ib_post_send(r_xprt->rx_ia.ri_id->qp, first, &bad_wr);
	trace_xprtrdma_post_send(req, rc);

	/* The final LOCAL_INV WR in the chain is supposed to
	 * do the wake. If it was never posted, the wake will
	 * not happen, so don't wait in that case.
	 */
	if (bad_wr != first)
		wait_for_completion(&frwr->fr_linv_done);
	if (!rc)
		return;

	/* Recycle MRs in the LOCAL_INV chain that did not get posted.
	 */
	while (bad_wr) {
		frwr = container_of(bad_wr, struct rpcrdma_frwr,
				    fr_invwr);
		mr = container_of(frwr, struct rpcrdma_mr, frwr);
		bad_wr = bad_wr->next;

		list_del_init(&mr->mr_list);
		rpcrdma_mr_recycle(mr);
	}
}

/**
 * frwr_wc_localinv_done - Invoked by RDMA provider for a signaled LOCAL_INV WC
 * @cq:	completion queue (ignored)
 * @wc:	completed WR
 *
 */
static void frwr_wc_localinv_done(struct ib_cq *cq, struct ib_wc *wc)
{
	struct ib_cqe *cqe = wc->wr_cqe;
	struct rpcrdma_frwr *frwr =
		container_of(cqe, struct rpcrdma_frwr, fr_cqe);
	struct rpcrdma_mr *mr = container_of(frwr, struct rpcrdma_mr, frwr);
	struct rpcrdma_rep *rep = mr->mr_req->rl_reply;

	/* WARNING: Only wr_cqe and status are reliable at this point */
	trace_xprtrdma_wc_li_done(wc, frwr);
	__frwr_release_mr(wc, mr);

	/* Ensure @rep is generated before __frwr_release_mr */
	smp_rmb();
	rpcrdma_complete_rqst(rep);
}

/**
 * frwr_unmap_async - invalidate memory regions that were registered for @req
 * @r_xprt: controlling transport instance
 * @req: rpcrdma_req with a non-empty list of MRs to process
 *
 * This guarantees that registered MRs are properly fenced from the
 * server before the RPC consumer accesses the data in them. It also
 * ensures proper Send flow control: waking the next RPC waits until
 * this RPC has relinquished all its Send Queue entries.
 */
void frwr_unmap_async(struct rpcrdma_xprt *r_xprt, struct rpcrdma_req *req)
{
	struct ib_send_wr *first, *last, **prev;
	const struct ib_send_wr *bad_wr;
	struct rpcrdma_frwr *frwr;
	struct rpcrdma_mr *mr;
	int rc;

	/* Chain the LOCAL_INV Work Requests and post them with
	 * a single ib_post_send() call.
	 */
	frwr = NULL;
	prev = &first;
	while ((mr = rpcrdma_mr_pop(&req->rl_registered))) {

		trace_xprtrdma_mr_localinv(mr);
		r_xprt->rx_stats.local_inv_needed++;

		frwr = &mr->frwr;
		frwr->fr_cqe.done = frwr_wc_localinv;
		last = &frwr->fr_invwr;
		last->next = NULL;
		last->wr_cqe = &frwr->fr_cqe;
		last->sg_list = NULL;
		last->num_sge = 0;
		last->opcode = IB_WR_LOCAL_INV;
		last->send_flags = IB_SEND_SIGNALED;
		last->ex.invalidate_rkey = mr->mr_handle;

		*prev = last;
		prev = &last->next;
	}

	/* Strong send queue ordering guarantees that when the
	 * last WR in the chain completes, all WRs in the chain
	 * are complete. The last completion will wake up the
	 * RPC waiter.
	 */
	frwr->fr_cqe.done = frwr_wc_localinv_done;

	/* Transport disconnect drains the receive CQ before it
	 * replaces the QP. The RPC reply handler won't call us
	 * unless ri_id->qp is a valid pointer.
	 */
	bad_wr = NULL;
	rc = ib_post_send(r_xprt->rx_ia.ri_id->qp, first, &bad_wr);
	trace_xprtrdma_post_send(req, rc);
	if (!rc)
		return;

	/* Recycle MRs in the LOCAL_INV chain that did not get posted.
	 */
	while (bad_wr) {
		frwr = container_of(bad_wr, struct rpcrdma_frwr, fr_invwr);
		mr = container_of(frwr, struct rpcrdma_mr, frwr);
		bad_wr = bad_wr->next;

		rpcrdma_mr_recycle(mr);
	}

	/* The final LOCAL_INV WR in the chain is supposed to
	 * do the wake. If it was never posted, the wake will
	 * not happen, so wake here in that case.
	 */
	rpcrdma_complete_rqst(req->rl_reply);
}
