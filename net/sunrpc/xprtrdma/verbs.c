// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/*
 * Copyright (c) 2014-2017 Oracle.  All rights reserved.
 * Copyright (c) 2003-2007 Network Appliance, Inc. All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the BSD-type
 * license below:
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *      Redistributions of source code must retain the above copyright
 *      notice, this list of conditions and the following disclaimer.
 *
 *      Redistributions in binary form must reproduce the above
 *      copyright notice, this list of conditions and the following
 *      disclaimer in the documentation and/or other materials provided
 *      with the distribution.
 *
 *      Neither the name of the Network Appliance, Inc. nor the names of
 *      its contributors may be used to endorse or promote products
 *      derived from this software without specific prior written
 *      permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * verbs.c
 *
 * Encapsulates the major functions managing:
 *  o adapters
 *  o endpoints
 *  o connections
 *  o buffer memory
 */

#include <linux/interrupt.h>
#include <linux/slab.h>
#include <linux/sunrpc/addr.h>
#include <linux/sunrpc/svc_rdma.h>
#include <linux/log2.h>

#include <asm-generic/barrier.h>
#include <asm/bitops.h>

#include <rdma/ib_cm.h>

#include "xprt_rdma.h"
#include <trace/events/rpcrdma.h>

static int rpcrdma_sendctxs_create(struct rpcrdma_xprt *r_xprt);
static void rpcrdma_sendctxs_destroy(struct rpcrdma_xprt *r_xprt);
static void rpcrdma_sendctx_put_locked(struct rpcrdma_xprt *r_xprt,
				       struct rpcrdma_sendctx *sc);
static int rpcrdma_reqs_setup(struct rpcrdma_xprt *r_xprt);
static void rpcrdma_reqs_reset(struct rpcrdma_xprt *r_xprt);
static void rpcrdma_rep_destroy(struct rpcrdma_rep *rep);
static void rpcrdma_reps_unmap(struct rpcrdma_xprt *r_xprt);
static void rpcrdma_mrs_create(struct rpcrdma_xprt *r_xprt);
static void rpcrdma_mrs_destroy(struct rpcrdma_xprt *r_xprt);
static void rpcrdma_ep_get(struct rpcrdma_ep *ep);
static int rpcrdma_ep_put(struct rpcrdma_ep *ep);
static struct rpcrdma_regbuf *
rpcrdma_regbuf_alloc(size_t size, enum dma_data_direction direction,
		     gfp_t flags);
static void rpcrdma_regbuf_dma_unmap(struct rpcrdma_regbuf *rb);
static void rpcrdma_regbuf_free(struct rpcrdma_regbuf *rb);

/* Wait for outstanding transport work to finish. ib_drain_qp
 * handles the drains in the wrong order for us, so open code
 * them here.
 */
static void rpcrdma_xprt_drain(struct rpcrdma_xprt *r_xprt)
{
	struct rpcrdma_ep *ep = r_xprt->rx_ep;
	struct rdma_cm_id *id = ep->re_id;

	/* Wait for rpcrdma_post_recvs() to leave its critical
	 * section.
	 */
	if (atomic_inc_return(&ep->re_receiving) > 1)
		wait_for_completion(&ep->re_done);

	/* Flush Receives, then wait for deferred Reply work
	 * to complete.
	 */
	ib_drain_rq(id->qp);

	/* Deferred Reply processing might have scheduled
	 * local invalidations.
	 */
	ib_drain_sq(id->qp);

	rpcrdma_ep_put(ep);
}

/* Ensure xprt_force_disconnect() is invoked exactly once when a
 * connection is closed or lost. (The important thing is it needs
 * to be invoked "at least" once).
 */
void rpcrdma_force_disconnect(struct rpcrdma_ep *ep)
{
	if (atomic_add_unless(&ep->re_force_disconnect, 1, 1))
		xprt_force_disconnect(ep->re_xprt);
}

/**
 * rpcrdma_flush_disconnect - Disconnect on flushed completion
 * @r_xprt: transport to disconnect
 * @wc: work completion entry
 *
 * Must be called in process context.
 */
void rpcrdma_flush_disconnect(struct rpcrdma_xprt *r_xprt, struct ib_wc *wc)
{
	if (wc->status != IB_WC_SUCCESS)
		rpcrdma_force_disconnect(r_xprt->rx_ep);
}

/**
 * rpcrdma_wc_send - Invoked by RDMA provider for each polled Send WC
 * @cq:	completion queue
 * @wc:	WCE for a completed Send WR
 *
 */
static void rpcrdma_wc_send(struct ib_cq *cq, struct ib_wc *wc)
{
	struct ib_cqe *cqe = wc->wr_cqe;
	struct rpcrdma_sendctx *sc =
		container_of(cqe, struct rpcrdma_sendctx, sc_cqe);
	struct rpcrdma_xprt *r_xprt = cq->cq_context;

	/* WARNING: Only wr_cqe and status are reliable at this point */
	trace_xprtrdma_wc_send(wc, &sc->sc_cid);
	rpcrdma_sendctx_put_locked(r_xprt, sc);
	rpcrdma_flush_disconnect(r_xprt, wc);
}

/**
 * rpcrdma_wc_receive - Invoked by RDMA provider for each polled Receive WC
 * @cq:	completion queue
 * @wc:	WCE for a completed Receive WR
 *
 */
static void rpcrdma_wc_receive(struct ib_cq *cq, struct ib_wc *wc)
{
	struct ib_cqe *cqe = wc->wr_cqe;
	struct rpcrdma_rep *rep = container_of(cqe, struct rpcrdma_rep,
					       rr_cqe);
	struct rpcrdma_xprt *r_xprt = cq->cq_context;

	/* WARNING: Only wr_cqe and status are reliable at this point */
	trace_xprtrdma_wc_receive(wc, &rep->rr_cid);
	--r_xprt->rx_ep->re_receive_count;
	if (wc->status != IB_WC_SUCCESS)
		goto out_flushed;

	/* status == SUCCESS means all fields in wc are trustworthy */
	rpcrdma_set_xdrlen(&rep->rr_hdrbuf, wc->byte_len);
	rep->rr_wc_flags = wc->wc_flags;
	rep->rr_inv_rkey = wc->ex.invalidate_rkey;

	ib_dma_sync_single_for_cpu(rdmab_device(rep->rr_rdmabuf),
				   rdmab_addr(rep->rr_rdmabuf),
				   wc->byte_len, DMA_FROM_DEVICE);

	rpcrdma_reply_handler(rep);
	return;

out_flushed:
	rpcrdma_flush_disconnect(r_xprt, wc);
	rpcrdma_rep_put(&r_xprt->rx_buf, rep);
}

static void rpcrdma_update_cm_private(struct rpcrdma_ep *ep,
				      struct rdma_conn_param *param)
{
	const struct rpcrdma_connect_private *pmsg = param->private_data;
	unsigned int rsize, wsize;

	/* Default settings for RPC-over-RDMA Version One */
	rsize = RPCRDMA_V1_DEF_INLINE_SIZE;
	wsize = RPCRDMA_V1_DEF_INLINE_SIZE;

	if (pmsg &&
	    pmsg->cp_magic == rpcrdma_cmp_magic &&
	    pmsg->cp_version == RPCRDMA_CMP_VERSION) {
		rsize = rpcrdma_decode_buffer_size(pmsg->cp_send_size);
		wsize = rpcrdma_decode_buffer_size(pmsg->cp_recv_size);
	}

	if (rsize < ep->re_inline_recv)
		ep->re_inline_recv = rsize;
	if (wsize < ep->re_inline_send)
		ep->re_inline_send = wsize;

	rpcrdma_set_max_header_sizes(ep);
}

/**
 * rpcrdma_cm_event_handler - Handle RDMA CM events
 * @id: rdma_cm_id on which an event has occurred
 * @event: details of the event
 *
 * Called with @id's mutex held. Returns 1 if caller should
 * destroy @id, otherwise 0.
 */
static int
rpcrdma_cm_event_handler(struct rdma_cm_id *id, struct rdma_cm_event *event)
{
	struct sockaddr *sap = (struct sockaddr *)&id->route.addr.dst_addr;
	struct rpcrdma_ep *ep = id->context;

	might_sleep();

	switch (event->event) {
	case RDMA_CM_EVENT_ADDR_RESOLVED:
	case RDMA_CM_EVENT_ROUTE_RESOLVED:
		ep->re_async_rc = 0;
		complete(&ep->re_done);
		return 0;
	case RDMA_CM_EVENT_ADDR_ERROR:
		ep->re_async_rc = -EPROTO;
		complete(&ep->re_done);
		return 0;
	case RDMA_CM_EVENT_ROUTE_ERROR:
		ep->re_async_rc = -ENETUNREACH;
		complete(&ep->re_done);
		return 0;
	case RDMA_CM_EVENT_DEVICE_REMOVAL:
		pr_info("rpcrdma: removing device %s for %pISpc\n",
			ep->re_id->device->name, sap);
		fallthrough;
	case RDMA_CM_EVENT_ADDR_CHANGE:
		ep->re_connect_status = -ENODEV;
		goto disconnected;
	case RDMA_CM_EVENT_ESTABLISHED:
		rpcrdma_ep_get(ep);
		ep->re_connect_status = 1;
		rpcrdma_update_cm_private(ep, &event->param.conn);
		trace_xprtrdma_inline_thresh(ep);
		wake_up_all(&ep->re_connect_wait);
		break;
	case RDMA_CM_EVENT_CONNECT_ERROR:
		ep->re_connect_status = -ENOTCONN;
		goto wake_connect_worker;
	case RDMA_CM_EVENT_UNREACHABLE:
		ep->re_connect_status = -ENETUNREACH;
		goto wake_connect_worker;
	case RDMA_CM_EVENT_REJECTED:
		ep->re_connect_status = -ECONNREFUSED;
		if (event->status == IB_CM_REJ_STALE_CONN)
			ep->re_connect_status = -ENOTCONN;
wake_connect_worker:
		wake_up_all(&ep->re_connect_wait);
		return 0;
	case RDMA_CM_EVENT_DISCONNECTED:
		ep->re_connect_status = -ECONNABORTED;
disconnected:
		rpcrdma_force_disconnect(ep);
		return rpcrdma_ep_put(ep);
	default:
		break;
	}

	return 0;
}

static struct rdma_cm_id *rpcrdma_create_id(struct rpcrdma_xprt *r_xprt,
					    struct rpcrdma_ep *ep)
{
	unsigned long wtimeout = msecs_to_jiffies(RDMA_RESOLVE_TIMEOUT) + 1;
	struct rpc_xprt *xprt = &r_xprt->rx_xprt;
	struct rdma_cm_id *id;
	int rc;

	init_completion(&ep->re_done);

	id = rdma_create_id(xprt->xprt_net, rpcrdma_cm_event_handler, ep,
			    RDMA_PS_TCP, IB_QPT_RC);
	if (IS_ERR(id))
		return id;

	ep->re_async_rc = -ETIMEDOUT;
	rc = rdma_resolve_addr(id, NULL, (struct sockaddr *)&xprt->addr,
			       RDMA_RESOLVE_TIMEOUT);
	if (rc)
		goto out;
	rc = wait_for_completion_interruptible_timeout(&ep->re_done, wtimeout);
	if (rc < 0)
		goto out;

	rc = ep->re_async_rc;
	if (rc)
		goto out;

	ep->re_async_rc = -ETIMEDOUT;
	rc = rdma_resolve_route(id, RDMA_RESOLVE_TIMEOUT);
	if (rc)
		goto out;
	rc = wait_for_completion_interruptible_timeout(&ep->re_done, wtimeout);
	if (rc < 0)
		goto out;
	rc = ep->re_async_rc;
	if (rc)
		goto out;

	return id;

out:
	rdma_destroy_id(id);
	return ERR_PTR(rc);
}

static void rpcrdma_ep_destroy(struct kref *kref)
{
	struct rpcrdma_ep *ep = container_of(kref, struct rpcrdma_ep, re_kref);

	if (ep->re_id->qp) {
		rdma_destroy_qp(ep->re_id);
		ep->re_id->qp = NULL;
	}

	if (ep->re_attr.recv_cq)
		ib_free_cq(ep->re_attr.recv_cq);
	ep->re_attr.recv_cq = NULL;
	if (ep->re_attr.send_cq)
		ib_free_cq(ep->re_attr.send_cq);
	ep->re_attr.send_cq = NULL;

	if (ep->re_pd)
		ib_dealloc_pd(ep->re_pd);
	ep->re_pd = NULL;

	kfree(ep);
	module_put(THIS_MODULE);
}

static noinline void rpcrdma_ep_get(struct rpcrdma_ep *ep)
{
	kref_get(&ep->re_kref);
}

/* Returns:
 *     %0 if @ep still has a positive kref count, or
 *     %1 if @ep was destroyed successfully.
 */
static noinline int rpcrdma_ep_put(struct rpcrdma_ep *ep)
{
	return kref_put(&ep->re_kref, rpcrdma_ep_destroy);
}

static int rpcrdma_ep_create(struct rpcrdma_xprt *r_xprt)
{
	struct rpcrdma_connect_private *pmsg;
	struct ib_device *device;
	struct rdma_cm_id *id;
	struct rpcrdma_ep *ep;
	int rc;

	ep = kzalloc(sizeof(*ep), GFP_NOFS);
	if (!ep)
		return -ENOTCONN;
	ep->re_xprt = &r_xprt->rx_xprt;
	kref_init(&ep->re_kref);

	id = rpcrdma_create_id(r_xprt, ep);
	if (IS_ERR(id)) {
		kfree(ep);
		return PTR_ERR(id);
	}
	__module_get(THIS_MODULE);
	device = id->device;
	ep->re_id = id;
	reinit_completion(&ep->re_done);

	ep->re_max_requests = r_xprt->rx_xprt.max_reqs;
	ep->re_inline_send = xprt_rdma_max_inline_write;
	ep->re_inline_recv = xprt_rdma_max_inline_read;
	rc = frwr_query_device(ep, device);
	if (rc)
		goto out_destroy;

	r_xprt->rx_buf.rb_max_requests = cpu_to_be32(ep->re_max_requests);

	ep->re_attr.srq = NULL;
	ep->re_attr.cap.max_inline_data = 0;
	ep->re_attr.sq_sig_type = IB_SIGNAL_REQ_WR;
	ep->re_attr.qp_type = IB_QPT_RC;
	ep->re_attr.port_num = ~0;

	ep->re_send_batch = ep->re_max_requests >> 3;
	ep->re_send_count = ep->re_send_batch;
	init_waitqueue_head(&ep->re_connect_wait);

	ep->re_attr.send_cq = ib_alloc_cq_any(device, r_xprt,
					      ep->re_attr.cap.max_send_wr,
					      IB_POLL_WORKQUEUE);
	if (IS_ERR(ep->re_attr.send_cq)) {
		rc = PTR_ERR(ep->re_attr.send_cq);
		ep->re_attr.send_cq = NULL;
		goto out_destroy;
	}

	ep->re_attr.recv_cq = ib_alloc_cq_any(device, r_xprt,
					      ep->re_attr.cap.max_recv_wr,
					      IB_POLL_WORKQUEUE);
	if (IS_ERR(ep->re_attr.recv_cq)) {
		rc = PTR_ERR(ep->re_attr.recv_cq);
		ep->re_attr.recv_cq = NULL;
		goto out_destroy;
	}
	ep->re_receive_count = 0;

	/* Initialize cma parameters */
	memset(&ep->re_remote_cma, 0, sizeof(ep->re_remote_cma));

	/* Prepare RDMA-CM private message */
	pmsg = &ep->re_cm_private;
	pmsg->cp_magic = rpcrdma_cmp_magic;
	pmsg->cp_version = RPCRDMA_CMP_VERSION;
	pmsg->cp_flags |= RPCRDMA_CMP_F_SND_W_INV_OK;
	pmsg->cp_send_size = rpcrdma_encode_buffer_size(ep->re_inline_send);
	pmsg->cp_recv_size = rpcrdma_encode_buffer_size(ep->re_inline_recv);
	ep->re_remote_cma.private_data = pmsg;
	ep->re_remote_cma.private_data_len = sizeof(*pmsg);

	/* Client offers RDMA Read but does not initiate */
	ep->re_remote_cma.initiator_depth = 0;
	ep->re_remote_cma.responder_resources =
		min_t(int, U8_MAX, device->attrs.max_qp_rd_atom);

	/* Limit transport retries so client can detect server
	 * GID changes quickly. RPC layer handles re-establishing
	 * transport connection and retransmission.
	 */
	ep->re_remote_cma.retry_count = 6;

	/* RPC-over-RDMA handles its own flow control. In addition,
	 * make all RNR NAKs visible so we know that RPC-over-RDMA
	 * flow control is working correctly (no NAKs should be seen).
	 */
	ep->re_remote_cma.flow_control = 0;
	ep->re_remote_cma.rnr_retry_count = 0;

	ep->re_pd = ib_alloc_pd(device, 0);
	if (IS_ERR(ep->re_pd)) {
		rc = PTR_ERR(ep->re_pd);
		ep->re_pd = NULL;
		goto out_destroy;
	}

	rc = rdma_create_qp(id, ep->re_pd, &ep->re_attr);
	if (rc)
		goto out_destroy;

	r_xprt->rx_ep = ep;
	return 0;

out_destroy:
	rpcrdma_ep_put(ep);
	rdma_destroy_id(id);
	return rc;
}

/**
 * rpcrdma_xprt_connect - Connect an unconnected transport
 * @r_xprt: controlling transport instance
 *
 * Returns 0 on success or a negative errno.
 */
int rpcrdma_xprt_connect(struct rpcrdma_xprt *r_xprt)
{
	struct rpc_xprt *xprt = &r_xprt->rx_xprt;
	struct rpcrdma_ep *ep;
	int rc;

	rc = rpcrdma_ep_create(r_xprt);
	if (rc)
		return rc;
	ep = r_xprt->rx_ep;

	xprt_clear_connected(xprt);
	rpcrdma_reset_cwnd(r_xprt);

	/* Bump the ep's reference count while there are
	 * outstanding Receives.
	 */
	rpcrdma_ep_get(ep);
	rpcrdma_post_recvs(r_xprt, 1, true);

	rc = rdma_connect(ep->re_id, &ep->re_remote_cma);
	if (rc)
		goto out;

	if (xprt->reestablish_timeout < RPCRDMA_INIT_REEST_TO)
		xprt->reestablish_timeout = RPCRDMA_INIT_REEST_TO;
	wait_event_interruptible(ep->re_connect_wait,
				 ep->re_connect_status != 0);
	if (ep->re_connect_status <= 0) {
		rc = ep->re_connect_status;
		goto out;
	}

	rc = rpcrdma_sendctxs_create(r_xprt);
	if (rc) {
		rc = -ENOTCONN;
		goto out;
	}

	rc = rpcrdma_reqs_setup(r_xprt);
	if (rc) {
		rc = -ENOTCONN;
		goto out;
	}
	rpcrdma_mrs_create(r_xprt);
	frwr_wp_create(r_xprt);

out:
	trace_xprtrdma_connect(r_xprt, rc);
	return rc;
}

/**
 * rpcrdma_xprt_disconnect - Disconnect underlying transport
 * @r_xprt: controlling transport instance
 *
 * Caller serializes. Either the transport send lock is held,
 * or we're being called to destroy the transport.
 *
 * On return, @r_xprt is completely divested of all hardware
 * resources and prepared for the next ->connect operation.
 */
void rpcrdma_xprt_disconnect(struct rpcrdma_xprt *r_xprt)
{
	struct rpcrdma_ep *ep = r_xprt->rx_ep;
	struct rdma_cm_id *id;
	int rc;

	if (!ep)
		return;

	id = ep->re_id;
	rc = rdma_disconnect(id);
	trace_xprtrdma_disconnect(r_xprt, rc);

	rpcrdma_xprt_drain(r_xprt);
	rpcrdma_reps_unmap(r_xprt);
	rpcrdma_reqs_reset(r_xprt);
	rpcrdma_mrs_destroy(r_xprt);
	rpcrdma_sendctxs_destroy(r_xprt);

	if (rpcrdma_ep_put(ep))
		rdma_destroy_id(id);

	r_xprt->rx_ep = NULL;
}

/* Fixed-size circular FIFO queue. This implementation is wait-free and
 * lock-free.
 *
 * Consumer is the code path that posts Sends. This path dequeues a
 * sendctx for use by a Send operation. Multiple consumer threads
 * are serialized by the RPC transport lock, which allows only one
 * ->send_request call at a time.
 *
 * Producer is the code path that handles Send completions. This path
 * enqueues a sendctx that has been completed. Multiple producer
 * threads are serialized by the ib_poll_cq() function.
 */

/* rpcrdma_sendctxs_destroy() assumes caller has already quiesced
 * queue activity, and rpcrdma_xprt_drain has flushed all remaining
 * Send requests.
 */
static void rpcrdma_sendctxs_destroy(struct rpcrdma_xprt *r_xprt)
{
	struct rpcrdma_buffer *buf = &r_xprt->rx_buf;
	unsigned long i;

	if (!buf->rb_sc_ctxs)
		return;
	for (i = 0; i <= buf->rb_sc_last; i++)
		kfree(buf->rb_sc_ctxs[i]);
	kfree(buf->rb_sc_ctxs);
	buf->rb_sc_ctxs = NULL;
}

static struct rpcrdma_sendctx *rpcrdma_sendctx_create(struct rpcrdma_ep *ep)
{
	struct rpcrdma_sendctx *sc;

	sc = kzalloc(struct_size(sc, sc_sges, ep->re_attr.cap.max_send_sge),
		     GFP_KERNEL);
	if (!sc)
		return NULL;

	sc->sc_cqe.done = rpcrdma_wc_send;
	sc->sc_cid.ci_queue_id = ep->re_attr.send_cq->res.id;
	sc->sc_cid.ci_completion_id =
		atomic_inc_return(&ep->re_completion_ids);
	return sc;
}

static int rpcrdma_sendctxs_create(struct rpcrdma_xprt *r_xprt)
{
	struct rpcrdma_buffer *buf = &r_xprt->rx_buf;
	struct rpcrdma_sendctx *sc;
	unsigned long i;

	/* Maximum number of concurrent outstanding Send WRs. Capping
	 * the circular queue size stops Send Queue overflow by causing
	 * the ->send_request call to fail temporarily before too many
	 * Sends are posted.
	 */
	i = r_xprt->rx_ep->re_max_requests + RPCRDMA_MAX_BC_REQUESTS;
	buf->rb_sc_ctxs = kcalloc(i, sizeof(sc), GFP_KERNEL);
	if (!buf->rb_sc_ctxs)
		return -ENOMEM;

	buf->rb_sc_last = i - 1;
	for (i = 0; i <= buf->rb_sc_last; i++) {
		sc = rpcrdma_sendctx_create(r_xprt->rx_ep);
		if (!sc)
			return -ENOMEM;

		buf->rb_sc_ctxs[i] = sc;
	}

	buf->rb_sc_head = 0;
	buf->rb_sc_tail = 0;
	return 0;
}

/* The sendctx queue is not guaranteed to have a size that is a
 * power of two, thus the helpers in circ_buf.h cannot be used.
 * The other option is to use modulus (%), which can be expensive.
 */
static unsigned long rpcrdma_sendctx_next(struct rpcrdma_buffer *buf,
					  unsigned long item)
{
	return likely(item < buf->rb_sc_last) ? item + 1 : 0;
}

/**
 * rpcrdma_sendctx_get_locked - Acquire a send context
 * @r_xprt: controlling transport instance
 *
 * Returns pointer to a free send completion context; or NULL if
 * the queue is empty.
 *
 * Usage: Called to acquire an SGE array before preparing a Send WR.
 *
 * The caller serializes calls to this function (per transport), and
 * provides an effective memory barrier that flushes the new value
 * of rb_sc_head.
 */
struct rpcrdma_sendctx *rpcrdma_sendctx_get_locked(struct rpcrdma_xprt *r_xprt)
{
	struct rpcrdma_buffer *buf = &r_xprt->rx_buf;
	struct rpcrdma_sendctx *sc;
	unsigned long next_head;

	next_head = rpcrdma_sendctx_next(buf, buf->rb_sc_head);

	if (next_head == READ_ONCE(buf->rb_sc_tail))
		goto out_emptyq;

	/* ORDER: item must be accessed _before_ head is updated */
	sc = buf->rb_sc_ctxs[next_head];

	/* Releasing the lock in the caller acts as a memory
	 * barrier that flushes rb_sc_head.
	 */
	buf->rb_sc_head = next_head;

	return sc;

out_emptyq:
	/* The queue is "empty" if there have not been enough Send
	 * completions recently. This is a sign the Send Queue is
	 * backing up. Cause the caller to pause and try again.
	 */
	xprt_wait_for_buffer_space(&r_xprt->rx_xprt);
	r_xprt->rx_stats.empty_sendctx_q++;
	return NULL;
}

/**
 * rpcrdma_sendctx_put_locked - Release a send context
 * @r_xprt: controlling transport instance
 * @sc: send context to release
 *
 * Usage: Called from Send completion to return a sendctxt
 * to the queue.
 *
 * The caller serializes calls to this function (per transport).
 */
static void rpcrdma_sendctx_put_locked(struct rpcrdma_xprt *r_xprt,
				       struct rpcrdma_sendctx *sc)
{
	struct rpcrdma_buffer *buf = &r_xprt->rx_buf;
	unsigned long next_tail;

	/* Unmap SGEs of previously completed but unsignaled
	 * Sends by walking up the queue until @sc is found.
	 */
	next_tail = buf->rb_sc_tail;
	do {
		next_tail = rpcrdma_sendctx_next(buf, next_tail);

		/* ORDER: item must be accessed _before_ tail is updated */
		rpcrdma_sendctx_unmap(buf->rb_sc_ctxs[next_tail]);

	} while (buf->rb_sc_ctxs[next_tail] != sc);

	/* Paired with READ_ONCE */
	smp_store_release(&buf->rb_sc_tail, next_tail);

	xprt_write_space(&r_xprt->rx_xprt);
}

static void
rpcrdma_mrs_create(struct rpcrdma_xprt *r_xprt)
{
	struct rpcrdma_buffer *buf = &r_xprt->rx_buf;
	struct rpcrdma_ep *ep = r_xprt->rx_ep;
	unsigned int count;

	for (count = 0; count < ep->re_max_rdma_segs; count++) {
		struct rpcrdma_mr *mr;
		int rc;

		mr = kzalloc(sizeof(*mr), GFP_NOFS);
		if (!mr)
			break;

		rc = frwr_mr_init(r_xprt, mr);
		if (rc) {
			kfree(mr);
			break;
		}

		spin_lock(&buf->rb_lock);
		rpcrdma_mr_push(mr, &buf->rb_mrs);
		list_add(&mr->mr_all, &buf->rb_all_mrs);
		spin_unlock(&buf->rb_lock);
	}

	r_xprt->rx_stats.mrs_allocated += count;
	trace_xprtrdma_createmrs(r_xprt, count);
}

static void
rpcrdma_mr_refresh_worker(struct work_struct *work)
{
	struct rpcrdma_buffer *buf = container_of(work, struct rpcrdma_buffer,
						  rb_refresh_worker);
	struct rpcrdma_xprt *r_xprt = container_of(buf, struct rpcrdma_xprt,
						   rx_buf);

	rpcrdma_mrs_create(r_xprt);
	xprt_write_space(&r_xprt->rx_xprt);
}

/**
 * rpcrdma_mrs_refresh - Wake the MR refresh worker
 * @r_xprt: controlling transport instance
 *
 */
void rpcrdma_mrs_refresh(struct rpcrdma_xprt *r_xprt)
{
	struct rpcrdma_buffer *buf = &r_xprt->rx_buf;
	struct rpcrdma_ep *ep = r_xprt->rx_ep;

	/* If there is no underlying connection, it's no use
	 * to wake the refresh worker.
	 */
	if (ep->re_connect_status == 1) {
		/* The work is scheduled on a WQ_MEM_RECLAIM
		 * workqueue in order to prevent MR allocation
		 * from recursing into NFS during direct reclaim.
		 */
		queue_work(xprtiod_workqueue, &buf->rb_refresh_worker);
	}
}

/**
 * rpcrdma_req_create - Allocate an rpcrdma_req object
 * @r_xprt: controlling r_xprt
 * @size: initial size, in bytes, of send and receive buffers
 * @flags: GFP flags passed to memory allocators
 *
 * Returns an allocated and fully initialized rpcrdma_req or NULL.
 */
struct rpcrdma_req *rpcrdma_req_create(struct rpcrdma_xprt *r_xprt, size_t size,
				       gfp_t flags)
{
	struct rpcrdma_buffer *buffer = &r_xprt->rx_buf;
	struct rpcrdma_req *req;

	req = kzalloc(sizeof(*req), flags);
	if (req == NULL)
		goto out1;

	req->rl_sendbuf = rpcrdma_regbuf_alloc(size, DMA_TO_DEVICE, flags);
	if (!req->rl_sendbuf)
		goto out2;

	req->rl_recvbuf = rpcrdma_regbuf_alloc(size, DMA_NONE, flags);
	if (!req->rl_recvbuf)
		goto out3;

	INIT_LIST_HEAD(&req->rl_free_mrs);
	INIT_LIST_HEAD(&req->rl_registered);
	spin_lock(&buffer->rb_lock);
	list_add(&req->rl_all, &buffer->rb_allreqs);
	spin_unlock(&buffer->rb_lock);
	return req;

out3:
	kfree(req->rl_sendbuf);
out2:
	kfree(req);
out1:
	return NULL;
}

/**
 * rpcrdma_req_setup - Per-connection instance setup of an rpcrdma_req object
 * @r_xprt: controlling transport instance
 * @req: rpcrdma_req object to set up
 *
 * Returns zero on success, and a negative errno on failure.
 */
int rpcrdma_req_setup(struct rpcrdma_xprt *r_xprt, struct rpcrdma_req *req)
{
	struct rpcrdma_regbuf *rb;
	size_t maxhdrsize;

	/* Compute maximum header buffer size in bytes */
	maxhdrsize = rpcrdma_fixed_maxsz + 3 +
		     r_xprt->rx_ep->re_max_rdma_segs * rpcrdma_readchunk_maxsz;
	maxhdrsize *= sizeof(__be32);
	rb = rpcrdma_regbuf_alloc(__roundup_pow_of_two(maxhdrsize),
				  DMA_TO_DEVICE, GFP_KERNEL);
	if (!rb)
		goto out;

	if (!__rpcrdma_regbuf_dma_map(r_xprt, rb))
		goto out_free;

	req->rl_rdmabuf = rb;
	xdr_buf_init(&req->rl_hdrbuf, rdmab_data(rb), rdmab_length(rb));
	return 0;

out_free:
	rpcrdma_regbuf_free(rb);
out:
	return -ENOMEM;
}

/* ASSUMPTION: the rb_allreqs list is stable for the duration,
 * and thus can be walked without holding rb_lock. Eg. the
 * caller is holding the transport send lock to exclude
 * device removal or disconnection.
 */
static int rpcrdma_reqs_setup(struct rpcrdma_xprt *r_xprt)
{
	struct rpcrdma_buffer *buf = &r_xprt->rx_buf;
	struct rpcrdma_req *req;
	int rc;

	list_for_each_entry(req, &buf->rb_allreqs, rl_all) {
		rc = rpcrdma_req_setup(r_xprt, req);
		if (rc)
			return rc;
	}
	return 0;
}

static void rpcrdma_req_reset(struct rpcrdma_req *req)
{
	/* Credits are valid for only one connection */
	req->rl_slot.rq_cong = 0;

	rpcrdma_regbuf_free(req->rl_rdmabuf);
	req->rl_rdmabuf = NULL;

	rpcrdma_regbuf_dma_unmap(req->rl_sendbuf);
	rpcrdma_regbuf_dma_unmap(req->rl_recvbuf);

	frwr_reset(req);
}

/* ASSUMPTION: the rb_allreqs list is stable for the duration,
 * and thus can be walked without holding rb_lock. Eg. the
 * caller is holding the transport send lock to exclude
 * device removal or disconnection.
 */
static void rpcrdma_reqs_reset(struct rpcrdma_xprt *r_xprt)
{
	struct rpcrdma_buffer *buf = &r_xprt->rx_buf;
	struct rpcrdma_req *req;

	list_for_each_entry(req, &buf->rb_allreqs, rl_all)
		rpcrdma_req_reset(req);
}

static noinline
struct rpcrdma_rep *rpcrdma_rep_create(struct rpcrdma_xprt *r_xprt,
				       bool temp)
{
	struct rpcrdma_buffer *buf = &r_xprt->rx_buf;
	struct rpcrdma_rep *rep;

	rep = kzalloc(sizeof(*rep), GFP_KERNEL);
	if (rep == NULL)
		goto out;

	rep->rr_rdmabuf = rpcrdma_regbuf_alloc(r_xprt->rx_ep->re_inline_recv,
					       DMA_FROM_DEVICE, GFP_KERNEL);
	if (!rep->rr_rdmabuf)
		goto out_free;

	if (!rpcrdma_regbuf_dma_map(r_xprt, rep->rr_rdmabuf))
		goto out_free_regbuf;

	rep->rr_cid.ci_completion_id =
		atomic_inc_return(&r_xprt->rx_ep->re_completion_ids);

	xdr_buf_init(&rep->rr_hdrbuf, rdmab_data(rep->rr_rdmabuf),
		     rdmab_length(rep->rr_rdmabuf));
	rep->rr_cqe.done = rpcrdma_wc_receive;
	rep->rr_rxprt = r_xprt;
	rep->rr_recv_wr.next = NULL;
	rep->rr_recv_wr.wr_cqe = &rep->rr_cqe;
	rep->rr_recv_wr.sg_list = &rep->rr_rdmabuf->rg_iov;
	rep->rr_recv_wr.num_sge = 1;
	rep->rr_temp = temp;

	spin_lock(&buf->rb_lock);
	list_add(&rep->rr_all, &buf->rb_all_reps);
	spin_unlock(&buf->rb_lock);
	return rep;

out_free_regbuf:
	rpcrdma_regbuf_free(rep->rr_rdmabuf);
out_free:
	kfree(rep);
out:
	return NULL;
}

static void rpcrdma_rep_free(struct rpcrdma_rep *rep)
{
	rpcrdma_regbuf_free(rep->rr_rdmabuf);
	kfree(rep);
}

static void rpcrdma_rep_destroy(struct rpcrdma_rep *rep)
{
	struct rpcrdma_buffer *buf = &rep->rr_rxprt->rx_buf;

	spin_lock(&buf->rb_lock);
	list_del(&rep->rr_all);
	spin_unlock(&buf->rb_lock);

	rpcrdma_rep_free(rep);
}

static struct rpcrdma_rep *rpcrdma_rep_get_locked(struct rpcrdma_buffer *buf)
{
	struct llist_node *node;

	/* Calls to llist_del_first are required to be serialized */
	node = llist_del_first(&buf->rb_free_reps);
	if (!node)
		return NULL;
	return llist_entry(node, struct rpcrdma_rep, rr_node);
}

/**
 * rpcrdma_rep_put - Release rpcrdma_rep back to free list
 * @buf: buffer pool
 * @rep: rep to release
 *
 */
void rpcrdma_rep_put(struct rpcrdma_buffer *buf, struct rpcrdma_rep *rep)
{
	llist_add(&rep->rr_node, &buf->rb_free_reps);
}

/* Caller must ensure the QP is quiescent (RQ is drained) before
 * invoking this function, to guarantee rb_all_reps is not
 * changing.
 */
static void rpcrdma_reps_unmap(struct rpcrdma_xprt *r_xprt)
{
	struct rpcrdma_buffer *buf = &r_xprt->rx_buf;
	struct rpcrdma_rep *rep;

	list_for_each_entry(rep, &buf->rb_all_reps, rr_all) {
		rpcrdma_regbuf_dma_unmap(rep->rr_rdmabuf);
		rep->rr_temp = true;	/* Mark this rep for destruction */
	}
}

static void rpcrdma_reps_destroy(struct rpcrdma_buffer *buf)
{
	struct rpcrdma_rep *rep;

	spin_lock(&buf->rb_lock);
	while ((rep = list_first_entry_or_null(&buf->rb_all_reps,
					       struct rpcrdma_rep,
					       rr_all)) != NULL) {
		list_del(&rep->rr_all);
		spin_unlock(&buf->rb_lock);

		rpcrdma_rep_free(rep);

		spin_lock(&buf->rb_lock);
	}
	spin_unlock(&buf->rb_lock);
}

/**
 * rpcrdma_buffer_create - Create initial set of req/rep objects
 * @r_xprt: transport instance to (re)initialize
 *
 * Returns zero on success, otherwise a negative errno.
 */
int rpcrdma_buffer_create(struct rpcrdma_xprt *r_xprt)
{
	struct rpcrdma_buffer *buf = &r_xprt->rx_buf;
	int i, rc;

	buf->rb_bc_srv_max_requests = 0;
	spin_lock_init(&buf->rb_lock);
	INIT_LIST_HEAD(&buf->rb_mrs);
	INIT_LIST_HEAD(&buf->rb_all_mrs);
	INIT_WORK(&buf->rb_refresh_worker, rpcrdma_mr_refresh_worker);

	INIT_LIST_HEAD(&buf->rb_send_bufs);
	INIT_LIST_HEAD(&buf->rb_allreqs);
	INIT_LIST_HEAD(&buf->rb_all_reps);

	rc = -ENOMEM;
	for (i = 0; i < r_xprt->rx_xprt.max_reqs; i++) {
		struct rpcrdma_req *req;

		req = rpcrdma_req_create(r_xprt, RPCRDMA_V1_DEF_INLINE_SIZE * 2,
					 GFP_KERNEL);
		if (!req)
			goto out;
		list_add(&req->rl_list, &buf->rb_send_bufs);
	}

	init_llist_head(&buf->rb_free_reps);

	return 0;
out:
	rpcrdma_buffer_destroy(buf);
	return rc;
}

/**
 * rpcrdma_req_destroy - Destroy an rpcrdma_req object
 * @req: unused object to be destroyed
 *
 * Relies on caller holding the transport send lock to protect
 * removing req->rl_all from buf->rb_all_reqs safely.
 */
void rpcrdma_req_destroy(struct rpcrdma_req *req)
{
	struct rpcrdma_mr *mr;

	list_del(&req->rl_all);

	while ((mr = rpcrdma_mr_pop(&req->rl_free_mrs))) {
		struct rpcrdma_buffer *buf = &mr->mr_xprt->rx_buf;

		spin_lock(&buf->rb_lock);
		list_del(&mr->mr_all);
		spin_unlock(&buf->rb_lock);

		frwr_mr_release(mr);
	}

	rpcrdma_regbuf_free(req->rl_recvbuf);
	rpcrdma_regbuf_free(req->rl_sendbuf);
	rpcrdma_regbuf_free(req->rl_rdmabuf);
	kfree(req);
}

/**
 * rpcrdma_mrs_destroy - Release all of a transport's MRs
 * @r_xprt: controlling transport instance
 *
 * Relies on caller holding the transport send lock to protect
 * removing mr->mr_list from req->rl_free_mrs safely.
 */
static void rpcrdma_mrs_destroy(struct rpcrdma_xprt *r_xprt)
{
	struct rpcrdma_buffer *buf = &r_xprt->rx_buf;
	struct rpcrdma_mr *mr;

	cancel_work_sync(&buf->rb_refresh_worker);

	spin_lock(&buf->rb_lock);
	while ((mr = list_first_entry_or_null(&buf->rb_all_mrs,
					      struct rpcrdma_mr,
					      mr_all)) != NULL) {
		list_del(&mr->mr_list);
		list_del(&mr->mr_all);
		spin_unlock(&buf->rb_lock);

		frwr_mr_release(mr);

		spin_lock(&buf->rb_lock);
	}
	spin_unlock(&buf->rb_lock);
}

/**
 * rpcrdma_buffer_destroy - Release all hw resources
 * @buf: root control block for resources
 *
 * ORDERING: relies on a prior rpcrdma_xprt_drain :
 * - No more Send or Receive completions can occur
 * - All MRs, reps, and reqs are returned to their free lists
 */
void
rpcrdma_buffer_destroy(struct rpcrdma_buffer *buf)
{
	rpcrdma_reps_destroy(buf);

	while (!list_empty(&buf->rb_send_bufs)) {
		struct rpcrdma_req *req;

		req = list_first_entry(&buf->rb_send_bufs,
				       struct rpcrdma_req, rl_list);
		list_del(&req->rl_list);
		rpcrdma_req_destroy(req);
	}
}

/**
 * rpcrdma_mr_get - Allocate an rpcrdma_mr object
 * @r_xprt: controlling transport
 *
 * Returns an initialized rpcrdma_mr or NULL if no free
 * rpcrdma_mr objects are available.
 */
struct rpcrdma_mr *
rpcrdma_mr_get(struct rpcrdma_xprt *r_xprt)
{
	struct rpcrdma_buffer *buf = &r_xprt->rx_buf;
	struct rpcrdma_mr *mr;

	spin_lock(&buf->rb_lock);
	mr = rpcrdma_mr_pop(&buf->rb_mrs);
	spin_unlock(&buf->rb_lock);
	return mr;
}

/**
 * rpcrdma_reply_put - Put reply buffers back into pool
 * @buffers: buffer pool
 * @req: object to return
 *
 */
void rpcrdma_reply_put(struct rpcrdma_buffer *buffers, struct rpcrdma_req *req)
{
	if (req->rl_reply) {
		rpcrdma_rep_put(buffers, req->rl_reply);
		req->rl_reply = NULL;
	}
}

/**
 * rpcrdma_buffer_get - Get a request buffer
 * @buffers: Buffer pool from which to obtain a buffer
 *
 * Returns a fresh rpcrdma_req, or NULL if none are available.
 */
struct rpcrdma_req *
rpcrdma_buffer_get(struct rpcrdma_buffer *buffers)
{
	struct rpcrdma_req *req;

	spin_lock(&buffers->rb_lock);
	req = list_first_entry_or_null(&buffers->rb_send_bufs,
				       struct rpcrdma_req, rl_list);
	if (req)
		list_del_init(&req->rl_list);
	spin_unlock(&buffers->rb_lock);
	return req;
}

/**
 * rpcrdma_buffer_put - Put request/reply buffers back into pool
 * @buffers: buffer pool
 * @req: object to return
 *
 */
void rpcrdma_buffer_put(struct rpcrdma_buffer *buffers, struct rpcrdma_req *req)
{
	rpcrdma_reply_put(buffers, req);

	spin_lock(&buffers->rb_lock);
	list_add(&req->rl_list, &buffers->rb_send_bufs);
	spin_unlock(&buffers->rb_lock);
}

/* Returns a pointer to a rpcrdma_regbuf object, or NULL.
 *
 * xprtrdma uses a regbuf for posting an outgoing RDMA SEND, or for
 * receiving the payload of RDMA RECV operations. During Long Calls
 * or Replies they may be registered externally via frwr_map.
 */
static struct rpcrdma_regbuf *
rpcrdma_regbuf_alloc(size_t size, enum dma_data_direction direction,
		     gfp_t flags)
{
	struct rpcrdma_regbuf *rb;

	rb = kmalloc(sizeof(*rb), flags);
	if (!rb)
		return NULL;
	rb->rg_data = kmalloc(size, flags);
	if (!rb->rg_data) {
		kfree(rb);
		return NULL;
	}

	rb->rg_device = NULL;
	rb->rg_direction = direction;
	rb->rg_iov.length = size;
	return rb;
}

/**
 * rpcrdma_regbuf_realloc - re-allocate a SEND/RECV buffer
 * @rb: regbuf to reallocate
 * @size: size of buffer to be allocated, in bytes
 * @flags: GFP flags
 *
 * Returns true if reallocation was successful. If false is
 * returned, @rb is left untouched.
 */
bool rpcrdma_regbuf_realloc(struct rpcrdma_regbuf *rb, size_t size, gfp_t flags)
{
	void *buf;

	buf = kmalloc(size, flags);
	if (!buf)
		return false;

	rpcrdma_regbuf_dma_unmap(rb);
	kfree(rb->rg_data);

	rb->rg_data = buf;
	rb->rg_iov.length = size;
	return true;
}

/**
 * __rpcrdma_regbuf_dma_map - DMA-map a regbuf
 * @r_xprt: controlling transport instance
 * @rb: regbuf to be mapped
 *
 * Returns true if the buffer is now DMA mapped to @r_xprt's device
 */
bool __rpcrdma_regbuf_dma_map(struct rpcrdma_xprt *r_xprt,
			      struct rpcrdma_regbuf *rb)
{
	struct ib_device *device = r_xprt->rx_ep->re_id->device;

	if (rb->rg_direction == DMA_NONE)
		return false;

	rb->rg_iov.addr = ib_dma_map_single(device, rdmab_data(rb),
					    rdmab_length(rb), rb->rg_direction);
	if (ib_dma_mapping_error(device, rdmab_addr(rb))) {
		trace_xprtrdma_dma_maperr(rdmab_addr(rb));
		return false;
	}

	rb->rg_device = device;
	rb->rg_iov.lkey = r_xprt->rx_ep->re_pd->local_dma_lkey;
	return true;
}

static void rpcrdma_regbuf_dma_unmap(struct rpcrdma_regbuf *rb)
{
	if (!rb)
		return;

	if (!rpcrdma_regbuf_is_mapped(rb))
		return;

	ib_dma_unmap_single(rb->rg_device, rdmab_addr(rb), rdmab_length(rb),
			    rb->rg_direction);
	rb->rg_device = NULL;
}

static void rpcrdma_regbuf_free(struct rpcrdma_regbuf *rb)
{
	rpcrdma_regbuf_dma_unmap(rb);
	if (rb)
		kfree(rb->rg_data);
	kfree(rb);
}

/**
 * rpcrdma_post_recvs - Refill the Receive Queue
 * @r_xprt: controlling transport instance
 * @needed: current credit grant
 * @temp: mark Receive buffers to be deleted after one use
 *
 */
void rpcrdma_post_recvs(struct rpcrdma_xprt *r_xprt, int needed, bool temp)
{
	struct rpcrdma_buffer *buf = &r_xprt->rx_buf;
	struct rpcrdma_ep *ep = r_xprt->rx_ep;
	struct ib_recv_wr *wr, *bad_wr;
	struct rpcrdma_rep *rep;
	int count, rc;

	rc = 0;
	count = 0;

	if (likely(ep->re_receive_count > needed))
		goto out;
	needed -= ep->re_receive_count;
	if (!temp)
		needed += RPCRDMA_MAX_RECV_BATCH;

	if (atomic_inc_return(&ep->re_receiving) > 1)
		goto out;

	/* fast path: all needed reps can be found on the free list */
	wr = NULL;
	while (needed) {
		rep = rpcrdma_rep_get_locked(buf);
		if (rep && rep->rr_temp) {
			rpcrdma_rep_destroy(rep);
			continue;
		}
		if (!rep)
			rep = rpcrdma_rep_create(r_xprt, temp);
		if (!rep)
			break;

		rep->rr_cid.ci_queue_id = ep->re_attr.recv_cq->res.id;
		trace_xprtrdma_post_recv(rep);
		rep->rr_recv_wr.next = wr;
		wr = &rep->rr_recv_wr;
		--needed;
		++count;
	}
	if (!wr)
		goto out;

	rc = ib_post_recv(ep->re_id->qp, wr,
			  (const struct ib_recv_wr **)&bad_wr);
	if (rc) {
		trace_xprtrdma_post_recvs_err(r_xprt, rc);
		for (wr = bad_wr; wr;) {
			struct rpcrdma_rep *rep;

			rep = container_of(wr, struct rpcrdma_rep, rr_recv_wr);
			wr = wr->next;
			rpcrdma_rep_put(buf, rep);
			--count;
		}
	}
	if (atomic_dec_return(&ep->re_receiving) > 0)
		complete(&ep->re_done);

out:
	trace_xprtrdma_post_recvs(r_xprt, count);
	ep->re_receive_count += count;
	return;
}
