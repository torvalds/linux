// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2015, 2017 Oracle.  All rights reserved.
 * Copyright (c) 2003-2007 Network Appliance, Inc. All rights reserved.
 */

/* Lightweight memory registration using Fast Registration Work
 * Requests (FRWR).
 *
 * FRWR features ordered asynchronous registration and deregistration
 * of arbitrarily sized memory regions. This is the fastest and safest
 * but most complex memory registration mode.
 */

/* Normal operation
 *
 * A Memory Region is prepared for RDMA READ or WRITE using a FAST_REG
 * Work Request (frwr_map). When the RDMA operation is finished, this
 * Memory Region is invalidated using a LOCAL_INV Work Request
 * (frwr_unmap_sync).
 *
 * Typically these Work Requests are not signaled, and neither are RDMA
 * SEND Work Requests (with the exception of signaling occasionally to
 * prevent provider work queue overflows). This greatly reduces HCA
 * interrupt workload.
 *
 * As an optimization, frwr_unmap marks MRs INVALID before the
 * LOCAL_INV WR is posted. If posting succeeds, the MR is placed on
 * rb_mrs immediately so that no work (like managing a linked list
 * under a spinlock) is needed in the completion upcall.
 *
 * But this means that frwr_map() can occasionally encounter an MR
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
 * When frwr_map encounters FLUSHED and VALID MRs, they are recovered
 * with ib_dereg_mr and then are re-initialized. Because MR recovery
 * allocates fresh resources, it is deferred to a workqueue, and the
 * recovered MRs are placed back on the rb_mrs list when recovery is
 * complete. frwr_map allocates another MR for the current RPC while
 * the broken MR is reset.
 *
 * To ensure that frwr_map doesn't encounter an MR that is marked
 * INVALID but that is about to be flushed due to a previous transport
 * disconnect, the transport connect worker attempts to drain all
 * pending send queue WRs before the transport is reconnected.
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

/* MRs are dynamically allocated, so simply clean up and release the MR.
 * A replacement MR will subsequently be allocated on demand.
 */
static void
frwr_mr_recycle_worker(struct work_struct *work)
{
	struct rpcrdma_mr *mr = container_of(work, struct rpcrdma_mr, mr_recycle);
	struct rpcrdma_xprt *r_xprt = mr->mr_xprt;

	trace_xprtrdma_mr_recycle(mr);

	if (mr->mr_dir != DMA_NONE) {
		trace_xprtrdma_mr_unmap(mr);
		ib_dma_unmap_sg(r_xprt->rx_ia.ri_id->device,
				mr->mr_sg, mr->mr_nents, mr->mr_dir);
		mr->mr_dir = DMA_NONE;
	}

	spin_lock(&r_xprt->rx_buf.rb_mrlock);
	list_del(&mr->mr_all);
	r_xprt->rx_stats.mrs_recycled++;
	spin_unlock(&r_xprt->rx_buf.rb_mrlock);

	frwr_release_mr(mr);
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

	frmr = ib_alloc_mr(ia->ri_pd, ia->ri_mrtype, depth);
	if (IS_ERR(frmr))
		goto out_mr_err;

	sg = kcalloc(depth, sizeof(*sg), GFP_KERNEL);
	if (!sg)
		goto out_list_err;

	mr->frwr.fr_mr = frmr;
	mr->frwr.fr_state = FRWR_IS_INVALID;
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
	dprintk("RPC:       %s: sg allocation failure\n",
		__func__);
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

	ia->ri_max_segs = max_t(unsigned int, 1, RPCRDMA_MAX_DATA_SEGS /
				ia->ri_max_frwr_depth);
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
 * frwr_wc_fastreg - Invoked by RDMA provider for a flushed FastReg WC
 * @cq:	completion queue (ignored)
 * @wc:	completed WR
 *
 */
static void
frwr_wc_fastreg(struct ib_cq *cq, struct ib_wc *wc)
{
	struct ib_cqe *cqe = wc->wr_cqe;
	struct rpcrdma_frwr *frwr =
			container_of(cqe, struct rpcrdma_frwr, fr_cqe);

	/* WARNING: Only wr_cqe and status are reliable at this point */
	if (wc->status != IB_WC_SUCCESS)
		frwr->fr_state = FRWR_FLUSHED_FR;
	trace_xprtrdma_wc_fastreg(wc, frwr);
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
	struct ib_cqe *cqe = wc->wr_cqe;
	struct rpcrdma_frwr *frwr = container_of(cqe, struct rpcrdma_frwr,
						 fr_cqe);

	/* WARNING: Only wr_cqe and status are reliable at this point */
	if (wc->status != IB_WC_SUCCESS)
		frwr->fr_state = FRWR_FLUSHED_LI;
	trace_xprtrdma_wc_li(wc, frwr);
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
	struct ib_cqe *cqe = wc->wr_cqe;
	struct rpcrdma_frwr *frwr = container_of(cqe, struct rpcrdma_frwr,
						 fr_cqe);

	/* WARNING: Only wr_cqe and status are reliable at this point */
	if (wc->status != IB_WC_SUCCESS)
		frwr->fr_state = FRWR_FLUSHED_LI;
	trace_xprtrdma_wc_li_wake(wc, frwr);
	complete(&frwr->fr_linv_done);
}

/**
 * frwr_map - Register a memory region
 * @r_xprt: controlling transport
 * @seg: memory region co-ordinates
 * @nsegs: number of segments remaining
 * @writing: true when RDMA Write will be used
 * @xid: XID of RPC using the registered memory
 * @out: initialized MR
 *
 * Prepare a REG_MR Work Request to register a memory region
 * for remote access via RDMA READ or RDMA WRITE.
 *
 * Returns the next segment or a negative errno pointer.
 * On success, the prepared MR is planted in @out.
 */
struct rpcrdma_mr_seg *frwr_map(struct rpcrdma_xprt *r_xprt,
				struct rpcrdma_mr_seg *seg,
				int nsegs, bool writing, __be32 xid,
				struct rpcrdma_mr **out)
{
	struct rpcrdma_ia *ia = &r_xprt->rx_ia;
	bool holes_ok = ia->ri_mrtype == IB_MR_TYPE_SG_GAPS;
	struct rpcrdma_frwr *frwr;
	struct rpcrdma_mr *mr;
	struct ib_mr *ibmr;
	struct ib_reg_wr *reg_wr;
	int i, n;
	u8 key;

	mr = NULL;
	do {
		if (mr)
			rpcrdma_mr_recycle(mr);
		mr = rpcrdma_mr_get(r_xprt);
		if (!mr)
			return ERR_PTR(-EAGAIN);
	} while (mr->frwr.fr_state != FRWR_IS_INVALID);
	frwr = &mr->frwr;
	frwr->fr_state = FRWR_IS_VALID;

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
		if (holes_ok)
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

	ibmr = frwr->fr_mr;
	n = ib_map_mr_sg(ibmr, mr->mr_sg, mr->mr_nents, NULL, PAGE_SIZE);
	if (unlikely(n != mr->mr_nents))
		goto out_mapmr_err;

	ibmr->iova &= 0x00000000ffffffff;
	ibmr->iova |= ((u64)be32_to_cpu(xid)) << 32;
	key = (u8)(ibmr->rkey & 0x000000FF);
	ib_update_fast_reg_key(ibmr, ++key);

	reg_wr = &frwr->fr_regwr;
	reg_wr->mr = ibmr;
	reg_wr->key = ibmr->rkey;
	reg_wr->access = writing ?
			 IB_ACCESS_REMOTE_WRITE | IB_ACCESS_LOCAL_WRITE :
			 IB_ACCESS_REMOTE_READ;

	mr->mr_handle = ibmr->rkey;
	mr->mr_length = ibmr->length;
	mr->mr_offset = ibmr->iova;
	trace_xprtrdma_mr_map(mr);

	*out = mr;
	return seg;

out_dmamap_err:
	mr->mr_dir = DMA_NONE;
	trace_xprtrdma_frwr_sgerr(mr, i);
	rpcrdma_mr_put(mr);
	return ERR_PTR(-EIO);

out_mapmr_err:
	trace_xprtrdma_frwr_maperr(mr, n);
	rpcrdma_mr_recycle(mr);
	return ERR_PTR(-EIO);
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
			mr->frwr.fr_state = FRWR_IS_INVALID;
			rpcrdma_mr_unmap_and_put(mr);
			break;	/* only one invalidated MR per RPC */
		}
}

/**
 * frwr_unmap_sync - invalidate memory regions that were registered for @req
 * @r_xprt: controlling transport
 * @mrs: list of MRs to process
 *
 * Sleeps until it is safe for the host CPU to access the
 * previously mapped memory regions.
 *
 * Caller ensures that @mrs is not empty before the call. This
 * function empties the list.
 */
void frwr_unmap_sync(struct rpcrdma_xprt *r_xprt, struct list_head *mrs)
{
	struct ib_send_wr *first, **prev, *last;
	const struct ib_send_wr *bad_wr;
	struct rpcrdma_ia *ia = &r_xprt->rx_ia;
	struct rpcrdma_frwr *frwr;
	struct rpcrdma_mr *mr;
	int count, rc;

	/* ORDER: Invalidate all of the MRs first
	 *
	 * Chain the LOCAL_INV Work Requests and post them with
	 * a single ib_post_send() call.
	 */
	frwr = NULL;
	count = 0;
	prev = &first;
	list_for_each_entry(mr, mrs, mr_list) {
		mr->frwr.fr_state = FRWR_IS_INVALID;

		frwr = &mr->frwr;
		trace_xprtrdma_mr_localinv(mr);

		frwr->fr_cqe.done = frwr_wc_localinv;
		last = &frwr->fr_invwr;
		memset(last, 0, sizeof(*last));
		last->wr_cqe = &frwr->fr_cqe;
		last->opcode = IB_WR_LOCAL_INV;
		last->ex.invalidate_rkey = mr->mr_handle;
		count++;

		*prev = last;
		prev = &last->next;
	}
	if (!frwr)
		goto unmap;

	/* Strong send queue ordering guarantees that when the
	 * last WR in the chain completes, all WRs in the chain
	 * are complete.
	 */
	last->send_flags = IB_SEND_SIGNALED;
	frwr->fr_cqe.done = frwr_wc_localinv_wake;
	reinit_completion(&frwr->fr_linv_done);

	/* Transport disconnect drains the receive CQ before it
	 * replaces the QP. The RPC reply handler won't call us
	 * unless ri_id->qp is a valid pointer.
	 */
	r_xprt->rx_stats.local_inv_needed++;
	bad_wr = NULL;
	rc = ib_post_send(ia->ri_id->qp, first, &bad_wr);
	if (bad_wr != first)
		wait_for_completion(&frwr->fr_linv_done);
	if (rc)
		goto out_release;

	/* ORDER: Now DMA unmap all of the MRs, and return
	 * them to the free MR list.
	 */
unmap:
	while (!list_empty(mrs)) {
		mr = rpcrdma_mr_pop(mrs);
		rpcrdma_mr_unmap_and_put(mr);
	}
	return;

out_release:
	pr_err("rpcrdma: FRWR invalidate ib_post_send returned %i\n", rc);

	/* Unmap and release the MRs in the LOCAL_INV WRs that did not
	 * get posted.
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
