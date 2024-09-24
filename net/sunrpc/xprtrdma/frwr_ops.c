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
 * are flushed and are likely unusable. Thus all MRs are destroyed.
 * New MRs are created on demand.
 */

#include <linux/sunrpc/svc_rdma.h>

#include "xprt_rdma.h"
#include <trace/events/rpcrdma.h>

static void frwr_cid_init(struct rpcrdma_ep *ep,
			  struct rpcrdma_mr *mr)
{
	struct rpc_rdma_cid *cid = &mr->mr_cid;

	cid->ci_queue_id = ep->re_attr.send_cq->res.id;
	cid->ci_completion_id = mr->mr_ibmr->res.id;
}

static void frwr_mr_unmap(struct rpcrdma_mr *mr)
{
	if (mr->mr_device) {
		trace_xprtrdma_mr_unmap(mr);
		ib_dma_unmap_sg(mr->mr_device, mr->mr_sg, mr->mr_nents,
				mr->mr_dir);
		mr->mr_device = NULL;
	}
}

/**
 * frwr_mr_release - Destroy one MR
 * @mr: MR allocated by frwr_mr_init
 *
 */
void frwr_mr_release(struct rpcrdma_mr *mr)
{
	int rc;

	frwr_mr_unmap(mr);

	rc = ib_dereg_mr(mr->mr_ibmr);
	if (rc)
		trace_xprtrdma_frwr_dereg(mr, rc);
	kfree(mr->mr_sg);
	kfree(mr);
}

static void frwr_mr_put(struct rpcrdma_mr *mr)
{
	frwr_mr_unmap(mr);

	/* The MR is returned to the req's MR free list instead
	 * of to the xprt's MR free list. No spinlock is needed.
	 */
	rpcrdma_mr_push(mr, &mr->mr_req->rl_free_mrs);
}

/**
 * frwr_reset - Place MRs back on @req's free list
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
		frwr_mr_put(mr);
}

/**
 * frwr_mr_init - Initialize one MR
 * @r_xprt: controlling transport instance
 * @mr: generic MR to prepare for FRWR
 *
 * Returns zero if successful. Otherwise a negative errno
 * is returned.
 */
int frwr_mr_init(struct rpcrdma_xprt *r_xprt, struct rpcrdma_mr *mr)
{
	struct rpcrdma_ep *ep = r_xprt->rx_ep;
	unsigned int depth = ep->re_max_fr_depth;
	struct scatterlist *sg;
	struct ib_mr *frmr;

	sg = kcalloc_node(depth, sizeof(*sg), XPRTRDMA_GFP_FLAGS,
			  ibdev_to_node(ep->re_id->device));
	if (!sg)
		return -ENOMEM;

	frmr = ib_alloc_mr(ep->re_pd, ep->re_mrtype, depth);
	if (IS_ERR(frmr))
		goto out_mr_err;

	mr->mr_xprt = r_xprt;
	mr->mr_ibmr = frmr;
	mr->mr_device = NULL;
	INIT_LIST_HEAD(&mr->mr_list);
	init_completion(&mr->mr_linv_done);
	frwr_cid_init(ep, mr);

	sg_init_table(sg, depth);
	mr->mr_sg = sg;
	return 0;

out_mr_err:
	kfree(sg);
	trace_xprtrdma_frwr_alloc(mr, PTR_ERR(frmr));
	return PTR_ERR(frmr);
}

/**
 * frwr_query_device - Prepare a transport for use with FRWR
 * @ep: endpoint to fill in
 * @device: RDMA device to query
 *
 * On success, sets:
 *	ep->re_attr
 *	ep->re_max_requests
 *	ep->re_max_rdma_segs
 *	ep->re_max_fr_depth
 *	ep->re_mrtype
 *
 * Return values:
 *   On success, returns zero.
 *   %-EINVAL - the device does not support FRWR memory registration
 *   %-ENOMEM - the device is not sufficiently capable for NFS/RDMA
 */
int frwr_query_device(struct rpcrdma_ep *ep, const struct ib_device *device)
{
	const struct ib_device_attr *attrs = &device->attrs;
	int max_qp_wr, depth, delta;
	unsigned int max_sge;

	if (!(attrs->device_cap_flags & IB_DEVICE_MEM_MGT_EXTENSIONS) ||
	    attrs->max_fast_reg_page_list_len == 0) {
		pr_err("rpcrdma: 'frwr' mode is not supported by device %s\n",
		       device->name);
		return -EINVAL;
	}

	max_sge = min_t(unsigned int, attrs->max_send_sge,
			RPCRDMA_MAX_SEND_SGES);
	if (max_sge < RPCRDMA_MIN_SEND_SGES) {
		pr_err("rpcrdma: HCA provides only %u send SGEs\n", max_sge);
		return -ENOMEM;
	}
	ep->re_attr.cap.max_send_sge = max_sge;
	ep->re_attr.cap.max_recv_sge = 1;

	ep->re_mrtype = IB_MR_TYPE_MEM_REG;
	if (attrs->kernel_cap_flags & IBK_SG_GAPS_REG)
		ep->re_mrtype = IB_MR_TYPE_SG_GAPS;

	/* Quirk: Some devices advertise a large max_fast_reg_page_list_len
	 * capability, but perform optimally when the MRs are not larger
	 * than a page.
	 */
	if (attrs->max_sge_rd > RPCRDMA_MAX_HDR_SEGS)
		ep->re_max_fr_depth = attrs->max_sge_rd;
	else
		ep->re_max_fr_depth = attrs->max_fast_reg_page_list_len;
	if (ep->re_max_fr_depth > RPCRDMA_MAX_DATA_SEGS)
		ep->re_max_fr_depth = RPCRDMA_MAX_DATA_SEGS;

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
	if (ep->re_max_fr_depth < RPCRDMA_MAX_DATA_SEGS) {
		delta = RPCRDMA_MAX_DATA_SEGS - ep->re_max_fr_depth;
		do {
			depth += 2; /* FRWR reg + invalidate */
			delta -= ep->re_max_fr_depth;
		} while (delta > 0);
	}

	max_qp_wr = attrs->max_qp_wr;
	max_qp_wr -= RPCRDMA_BACKWARD_WRS;
	max_qp_wr -= 1;
	if (max_qp_wr < RPCRDMA_MIN_SLOT_TABLE)
		return -ENOMEM;
	if (ep->re_max_requests > max_qp_wr)
		ep->re_max_requests = max_qp_wr;
	ep->re_attr.cap.max_send_wr = ep->re_max_requests * depth;
	if (ep->re_attr.cap.max_send_wr > max_qp_wr) {
		ep->re_max_requests = max_qp_wr / depth;
		if (!ep->re_max_requests)
			return -ENOMEM;
		ep->re_attr.cap.max_send_wr = ep->re_max_requests * depth;
	}
	ep->re_attr.cap.max_send_wr += RPCRDMA_BACKWARD_WRS;
	ep->re_attr.cap.max_send_wr += 1; /* for ib_drain_sq */
	ep->re_attr.cap.max_recv_wr = ep->re_max_requests;
	ep->re_attr.cap.max_recv_wr += RPCRDMA_BACKWARD_WRS;
	ep->re_attr.cap.max_recv_wr += RPCRDMA_MAX_RECV_BATCH;
	ep->re_attr.cap.max_recv_wr += 1; /* for ib_drain_rq */

	ep->re_max_rdma_segs =
		DIV_ROUND_UP(RPCRDMA_MAX_DATA_SEGS, ep->re_max_fr_depth);
	/* Reply chunks require segments for head and tail buffers */
	ep->re_max_rdma_segs += 2;
	if (ep->re_max_rdma_segs > RPCRDMA_MAX_HDR_SEGS)
		ep->re_max_rdma_segs = RPCRDMA_MAX_HDR_SEGS;

	/* Ensure the underlying device is capable of conveying the
	 * largest r/wsize NFS will ask for. This guarantees that
	 * failing over from one RDMA device to another will not
	 * break NFS I/O.
	 */
	if ((ep->re_max_rdma_segs * ep->re_max_fr_depth) < RPCRDMA_MAX_SEGS)
		return -ENOMEM;

	return 0;
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
	struct rpcrdma_ep *ep = r_xprt->rx_ep;
	struct ib_reg_wr *reg_wr;
	int i, n, dma_nents;
	struct ib_mr *ibmr;
	u8 key;

	if (nsegs > ep->re_max_fr_depth)
		nsegs = ep->re_max_fr_depth;
	for (i = 0; i < nsegs;) {
		sg_set_page(&mr->mr_sg[i], seg->mr_page,
			    seg->mr_len, seg->mr_offset);

		++seg;
		++i;
		if (ep->re_mrtype == IB_MR_TYPE_SG_GAPS)
			continue;
		if ((i < nsegs && seg->mr_offset) ||
		    offset_in_page((seg-1)->mr_offset + (seg-1)->mr_len))
			break;
	}
	mr->mr_dir = rpcrdma_data_dir(writing);
	mr->mr_nents = i;

	dma_nents = ib_dma_map_sg(ep->re_id->device, mr->mr_sg, mr->mr_nents,
				  mr->mr_dir);
	if (!dma_nents)
		goto out_dmamap_err;
	mr->mr_device = ep->re_id->device;

	ibmr = mr->mr_ibmr;
	n = ib_map_mr_sg(ibmr, mr->mr_sg, dma_nents, NULL, PAGE_SIZE);
	if (n != dma_nents)
		goto out_mapmr_err;

	ibmr->iova &= 0x00000000ffffffff;
	ibmr->iova |= ((u64)be32_to_cpu(xid)) << 32;
	key = (u8)(ibmr->rkey & 0x000000FF);
	ib_update_fast_reg_key(ibmr, ++key);

	reg_wr = &mr->mr_regwr;
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
	trace_xprtrdma_frwr_sgerr(mr, i);
	return ERR_PTR(-EIO);

out_mapmr_err:
	trace_xprtrdma_frwr_maperr(mr, n);
	return ERR_PTR(-EIO);
}

/**
 * frwr_wc_fastreg - Invoked by RDMA provider for a flushed FastReg WC
 * @cq: completion queue
 * @wc: WCE for a completed FastReg WR
 *
 * Each flushed MR gets destroyed after the QP has drained.
 */
static void frwr_wc_fastreg(struct ib_cq *cq, struct ib_wc *wc)
{
	struct ib_cqe *cqe = wc->wr_cqe;
	struct rpcrdma_mr *mr = container_of(cqe, struct rpcrdma_mr, mr_cqe);

	/* WARNING: Only wr_cqe and status are reliable at this point */
	trace_xprtrdma_wc_fastreg(wc, &mr->mr_cid);

	rpcrdma_flush_disconnect(cq->cq_context, wc);
}

/**
 * frwr_send - post Send WRs containing the RPC Call message
 * @r_xprt: controlling transport instance
 * @req: prepared RPC Call
 *
 * For FRWR, chain any FastReg WRs to the Send WR. Only a
 * single ib_post_send call is needed to register memory
 * and then post the Send WR.
 *
 * Returns the return code from ib_post_send.
 *
 * Caller must hold the transport send lock to ensure that the
 * pointers to the transport's rdma_cm_id and QP are stable.
 */
int frwr_send(struct rpcrdma_xprt *r_xprt, struct rpcrdma_req *req)
{
	struct ib_send_wr *post_wr, *send_wr = &req->rl_wr;
	struct rpcrdma_ep *ep = r_xprt->rx_ep;
	struct rpcrdma_mr *mr;
	unsigned int num_wrs;
	int ret;

	num_wrs = 1;
	post_wr = send_wr;
	list_for_each_entry(mr, &req->rl_registered, mr_list) {
		trace_xprtrdma_mr_fastreg(mr);

		mr->mr_cqe.done = frwr_wc_fastreg;
		mr->mr_regwr.wr.next = post_wr;
		mr->mr_regwr.wr.wr_cqe = &mr->mr_cqe;
		mr->mr_regwr.wr.num_sge = 0;
		mr->mr_regwr.wr.opcode = IB_WR_REG_MR;
		mr->mr_regwr.wr.send_flags = 0;
		post_wr = &mr->mr_regwr.wr;
		++num_wrs;
	}

	if ((kref_read(&req->rl_kref) > 1) || num_wrs > ep->re_send_count) {
		send_wr->send_flags |= IB_SEND_SIGNALED;
		ep->re_send_count = min_t(unsigned int, ep->re_send_batch,
					  num_wrs - ep->re_send_count);
	} else {
		send_wr->send_flags &= ~IB_SEND_SIGNALED;
		ep->re_send_count -= num_wrs;
	}

	trace_xprtrdma_post_send(req);
	ret = ib_post_send(ep->re_id->qp, post_wr, NULL);
	if (ret)
		trace_xprtrdma_post_send_err(r_xprt, req, ret);
	return ret;
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
			trace_xprtrdma_mr_reminv(mr);
			frwr_mr_put(mr);
			break;	/* only one invalidated MR per RPC */
		}
}

static void frwr_mr_done(struct ib_wc *wc, struct rpcrdma_mr *mr)
{
	if (likely(wc->status == IB_WC_SUCCESS))
		frwr_mr_put(mr);
}

/**
 * frwr_wc_localinv - Invoked by RDMA provider for a LOCAL_INV WC
 * @cq: completion queue
 * @wc: WCE for a completed LocalInv WR
 *
 */
static void frwr_wc_localinv(struct ib_cq *cq, struct ib_wc *wc)
{
	struct ib_cqe *cqe = wc->wr_cqe;
	struct rpcrdma_mr *mr = container_of(cqe, struct rpcrdma_mr, mr_cqe);

	/* WARNING: Only wr_cqe and status are reliable at this point */
	trace_xprtrdma_wc_li(wc, &mr->mr_cid);
	frwr_mr_done(wc, mr);

	rpcrdma_flush_disconnect(cq->cq_context, wc);
}

/**
 * frwr_wc_localinv_wake - Invoked by RDMA provider for a LOCAL_INV WC
 * @cq: completion queue
 * @wc: WCE for a completed LocalInv WR
 *
 * Awaken anyone waiting for an MR to finish being fenced.
 */
static void frwr_wc_localinv_wake(struct ib_cq *cq, struct ib_wc *wc)
{
	struct ib_cqe *cqe = wc->wr_cqe;
	struct rpcrdma_mr *mr = container_of(cqe, struct rpcrdma_mr, mr_cqe);

	/* WARNING: Only wr_cqe and status are reliable at this point */
	trace_xprtrdma_wc_li_wake(wc, &mr->mr_cid);
	frwr_mr_done(wc, mr);
	complete(&mr->mr_linv_done);

	rpcrdma_flush_disconnect(cq->cq_context, wc);
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
	struct rpcrdma_ep *ep = r_xprt->rx_ep;
	const struct ib_send_wr *bad_wr;
	struct rpcrdma_mr *mr;
	int rc;

	/* ORDER: Invalidate all of the MRs first
	 *
	 * Chain the LOCAL_INV Work Requests and post them with
	 * a single ib_post_send() call.
	 */
	prev = &first;
	mr = rpcrdma_mr_pop(&req->rl_registered);
	do {
		trace_xprtrdma_mr_localinv(mr);
		r_xprt->rx_stats.local_inv_needed++;

		last = &mr->mr_invwr;
		last->next = NULL;
		last->wr_cqe = &mr->mr_cqe;
		last->sg_list = NULL;
		last->num_sge = 0;
		last->opcode = IB_WR_LOCAL_INV;
		last->send_flags = IB_SEND_SIGNALED;
		last->ex.invalidate_rkey = mr->mr_handle;

		last->wr_cqe->done = frwr_wc_localinv;

		*prev = last;
		prev = &last->next;
	} while ((mr = rpcrdma_mr_pop(&req->rl_registered)));

	mr = container_of(last, struct rpcrdma_mr, mr_invwr);

	/* Strong send queue ordering guarantees that when the
	 * last WR in the chain completes, all WRs in the chain
	 * are complete.
	 */
	last->wr_cqe->done = frwr_wc_localinv_wake;
	reinit_completion(&mr->mr_linv_done);

	/* Transport disconnect drains the receive CQ before it
	 * replaces the QP. The RPC reply handler won't call us
	 * unless re_id->qp is a valid pointer.
	 */
	bad_wr = NULL;
	rc = ib_post_send(ep->re_id->qp, first, &bad_wr);

	/* The final LOCAL_INV WR in the chain is supposed to
	 * do the wake. If it was never posted, the wake will
	 * not happen, so don't wait in that case.
	 */
	if (bad_wr != first)
		wait_for_completion(&mr->mr_linv_done);
	if (!rc)
		return;

	/* On error, the MRs get destroyed once the QP has drained. */
	trace_xprtrdma_post_linv_err(req, rc);

	/* Force a connection loss to ensure complete recovery.
	 */
	rpcrdma_force_disconnect(ep);
}

/**
 * frwr_wc_localinv_done - Invoked by RDMA provider for a signaled LOCAL_INV WC
 * @cq:	completion queue
 * @wc:	WCE for a completed LocalInv WR
 *
 */
static void frwr_wc_localinv_done(struct ib_cq *cq, struct ib_wc *wc)
{
	struct ib_cqe *cqe = wc->wr_cqe;
	struct rpcrdma_mr *mr = container_of(cqe, struct rpcrdma_mr, mr_cqe);
	struct rpcrdma_rep *rep;

	/* WARNING: Only wr_cqe and status are reliable at this point */
	trace_xprtrdma_wc_li_done(wc, &mr->mr_cid);

	/* Ensure that @rep is generated before the MR is released */
	rep = mr->mr_req->rl_reply;
	smp_rmb();

	if (wc->status != IB_WC_SUCCESS) {
		if (rep)
			rpcrdma_unpin_rqst(rep);
		rpcrdma_flush_disconnect(cq->cq_context, wc);
		return;
	}
	frwr_mr_put(mr);
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
	struct rpcrdma_ep *ep = r_xprt->rx_ep;
	struct rpcrdma_mr *mr;
	int rc;

	/* Chain the LOCAL_INV Work Requests and post them with
	 * a single ib_post_send() call.
	 */
	prev = &first;
	mr = rpcrdma_mr_pop(&req->rl_registered);
	do {
		trace_xprtrdma_mr_localinv(mr);
		r_xprt->rx_stats.local_inv_needed++;

		last = &mr->mr_invwr;
		last->next = NULL;
		last->wr_cqe = &mr->mr_cqe;
		last->sg_list = NULL;
		last->num_sge = 0;
		last->opcode = IB_WR_LOCAL_INV;
		last->send_flags = IB_SEND_SIGNALED;
		last->ex.invalidate_rkey = mr->mr_handle;

		last->wr_cqe->done = frwr_wc_localinv;

		*prev = last;
		prev = &last->next;
	} while ((mr = rpcrdma_mr_pop(&req->rl_registered)));

	/* Strong send queue ordering guarantees that when the
	 * last WR in the chain completes, all WRs in the chain
	 * are complete. The last completion will wake up the
	 * RPC waiter.
	 */
	last->wr_cqe->done = frwr_wc_localinv_done;

	/* Transport disconnect drains the receive CQ before it
	 * replaces the QP. The RPC reply handler won't call us
	 * unless re_id->qp is a valid pointer.
	 */
	rc = ib_post_send(ep->re_id->qp, first, NULL);
	if (!rc)
		return;

	/* On error, the MRs get destroyed once the QP has drained. */
	trace_xprtrdma_post_linv_err(req, rc);

	/* The final LOCAL_INV WR in the chain is supposed to
	 * do the wake. If it was never posted, the wake does
	 * not happen. Unpin the rqst in preparation for its
	 * retransmission.
	 */
	rpcrdma_unpin_rqst(req->rl_reply);

	/* Force a connection loss to ensure complete recovery.
	 */
	rpcrdma_force_disconnect(ep);
}

/**
 * frwr_wp_create - Create an MR for padding Write chunks
 * @r_xprt: transport resources to use
 *
 * Return 0 on success, negative errno on failure.
 */
int frwr_wp_create(struct rpcrdma_xprt *r_xprt)
{
	struct rpcrdma_ep *ep = r_xprt->rx_ep;
	struct rpcrdma_mr_seg seg;
	struct rpcrdma_mr *mr;

	mr = rpcrdma_mr_get(r_xprt);
	if (!mr)
		return -EAGAIN;
	mr->mr_req = NULL;
	ep->re_write_pad_mr = mr;

	seg.mr_len = XDR_UNIT;
	seg.mr_page = virt_to_page(ep->re_write_pad);
	seg.mr_offset = offset_in_page(ep->re_write_pad);
	if (IS_ERR(frwr_map(r_xprt, &seg, 1, true, xdr_zero, mr)))
		return -EIO;
	trace_xprtrdma_mr_fastreg(mr);

	mr->mr_cqe.done = frwr_wc_fastreg;
	mr->mr_regwr.wr.next = NULL;
	mr->mr_regwr.wr.wr_cqe = &mr->mr_cqe;
	mr->mr_regwr.wr.num_sge = 0;
	mr->mr_regwr.wr.opcode = IB_WR_REG_MR;
	mr->mr_regwr.wr.send_flags = 0;

	return ib_post_send(ep->re_id->qp, &mr->mr_regwr.wr, NULL);
}
