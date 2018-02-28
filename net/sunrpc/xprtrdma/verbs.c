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

#include <asm-generic/barrier.h>
#include <asm/bitops.h>

#include <rdma/ib_cm.h>

#include "xprt_rdma.h"

/*
 * Globals/Macros
 */

#if IS_ENABLED(CONFIG_SUNRPC_DEBUG)
# define RPCDBG_FACILITY	RPCDBG_TRANS
#endif

/*
 * internal functions
 */
static void rpcrdma_mrs_create(struct rpcrdma_xprt *r_xprt);
static void rpcrdma_mrs_destroy(struct rpcrdma_buffer *buf);
static void rpcrdma_dma_unmap_regbuf(struct rpcrdma_regbuf *rb);

struct workqueue_struct *rpcrdma_receive_wq __read_mostly;

int
rpcrdma_alloc_wq(void)
{
	struct workqueue_struct *recv_wq;

	recv_wq = alloc_workqueue("xprtrdma_receive",
				  WQ_MEM_RECLAIM | WQ_HIGHPRI,
				  0);
	if (!recv_wq)
		return -ENOMEM;

	rpcrdma_receive_wq = recv_wq;
	return 0;
}

void
rpcrdma_destroy_wq(void)
{
	struct workqueue_struct *wq;

	if (rpcrdma_receive_wq) {
		wq = rpcrdma_receive_wq;
		rpcrdma_receive_wq = NULL;
		destroy_workqueue(wq);
	}
}

static void
rpcrdma_qp_async_error_upcall(struct ib_event *event, void *context)
{
	struct rpcrdma_ep *ep = context;
	struct rpcrdma_xprt *r_xprt = container_of(ep, struct rpcrdma_xprt,
						   rx_ep);

	trace_xprtrdma_qp_error(r_xprt, event);
	pr_err("rpcrdma: %s on device %s ep %p\n",
	       ib_event_msg(event->event), event->device->name, context);

	if (ep->rep_connected == 1) {
		ep->rep_connected = -EIO;
		rpcrdma_conn_func(ep);
		wake_up_all(&ep->rep_connect_wait);
	}
}

/**
 * rpcrdma_wc_send - Invoked by RDMA provider for each polled Send WC
 * @cq:	completion queue (ignored)
 * @wc:	completed WR
 *
 */
static void
rpcrdma_wc_send(struct ib_cq *cq, struct ib_wc *wc)
{
	struct ib_cqe *cqe = wc->wr_cqe;
	struct rpcrdma_sendctx *sc =
		container_of(cqe, struct rpcrdma_sendctx, sc_cqe);

	/* WARNING: Only wr_cqe and status are reliable at this point */
	trace_xprtrdma_wc_send(sc, wc);
	if (wc->status != IB_WC_SUCCESS && wc->status != IB_WC_WR_FLUSH_ERR)
		pr_err("rpcrdma: Send: %s (%u/0x%x)\n",
		       ib_wc_status_msg(wc->status),
		       wc->status, wc->vendor_err);

	rpcrdma_sendctx_put_locked(sc);
}

/**
 * rpcrdma_wc_receive - Invoked by RDMA provider for each polled Receive WC
 * @cq:	completion queue (ignored)
 * @wc:	completed WR
 *
 */
static void
rpcrdma_wc_receive(struct ib_cq *cq, struct ib_wc *wc)
{
	struct ib_cqe *cqe = wc->wr_cqe;
	struct rpcrdma_rep *rep = container_of(cqe, struct rpcrdma_rep,
					       rr_cqe);

	/* WARNING: Only wr_id and status are reliable at this point */
	trace_xprtrdma_wc_receive(rep, wc);
	if (wc->status != IB_WC_SUCCESS)
		goto out_fail;

	/* status == SUCCESS means all fields in wc are trustworthy */
	rpcrdma_set_xdrlen(&rep->rr_hdrbuf, wc->byte_len);
	rep->rr_wc_flags = wc->wc_flags;
	rep->rr_inv_rkey = wc->ex.invalidate_rkey;

	ib_dma_sync_single_for_cpu(rdmab_device(rep->rr_rdmabuf),
				   rdmab_addr(rep->rr_rdmabuf),
				   wc->byte_len, DMA_FROM_DEVICE);

out_schedule:
	rpcrdma_reply_handler(rep);
	return;

out_fail:
	if (wc->status != IB_WC_WR_FLUSH_ERR)
		pr_err("rpcrdma: Recv: %s (%u/0x%x)\n",
		       ib_wc_status_msg(wc->status),
		       wc->status, wc->vendor_err);
	rpcrdma_set_xdrlen(&rep->rr_hdrbuf, 0);
	goto out_schedule;
}

static void
rpcrdma_update_connect_private(struct rpcrdma_xprt *r_xprt,
			       struct rdma_conn_param *param)
{
	struct rpcrdma_create_data_internal *cdata = &r_xprt->rx_data;
	const struct rpcrdma_connect_private *pmsg = param->private_data;
	unsigned int rsize, wsize;

	/* Default settings for RPC-over-RDMA Version One */
	r_xprt->rx_ia.ri_implicit_roundup = xprt_rdma_pad_optimize;
	rsize = RPCRDMA_V1_DEF_INLINE_SIZE;
	wsize = RPCRDMA_V1_DEF_INLINE_SIZE;

	if (pmsg &&
	    pmsg->cp_magic == rpcrdma_cmp_magic &&
	    pmsg->cp_version == RPCRDMA_CMP_VERSION) {
		r_xprt->rx_ia.ri_implicit_roundup = true;
		rsize = rpcrdma_decode_buffer_size(pmsg->cp_send_size);
		wsize = rpcrdma_decode_buffer_size(pmsg->cp_recv_size);
	}

	if (rsize < cdata->inline_rsize)
		cdata->inline_rsize = rsize;
	if (wsize < cdata->inline_wsize)
		cdata->inline_wsize = wsize;
	dprintk("RPC:       %s: max send %u, max recv %u\n",
		__func__, cdata->inline_wsize, cdata->inline_rsize);
	rpcrdma_set_max_header_sizes(r_xprt);
}

static int
rpcrdma_conn_upcall(struct rdma_cm_id *id, struct rdma_cm_event *event)
{
	struct rpcrdma_xprt *xprt = id->context;
	struct rpcrdma_ia *ia = &xprt->rx_ia;
	struct rpcrdma_ep *ep = &xprt->rx_ep;
	int connstate = 0;

	trace_xprtrdma_conn_upcall(xprt, event);
	switch (event->event) {
	case RDMA_CM_EVENT_ADDR_RESOLVED:
	case RDMA_CM_EVENT_ROUTE_RESOLVED:
		ia->ri_async_rc = 0;
		complete(&ia->ri_done);
		break;
	case RDMA_CM_EVENT_ADDR_ERROR:
		ia->ri_async_rc = -EHOSTUNREACH;
		complete(&ia->ri_done);
		break;
	case RDMA_CM_EVENT_ROUTE_ERROR:
		ia->ri_async_rc = -ENETUNREACH;
		complete(&ia->ri_done);
		break;
	case RDMA_CM_EVENT_DEVICE_REMOVAL:
#if IS_ENABLED(CONFIG_SUNRPC_DEBUG)
		pr_info("rpcrdma: removing device %s for %s:%s\n",
			ia->ri_device->name,
			rpcrdma_addrstr(xprt), rpcrdma_portstr(xprt));
#endif
		set_bit(RPCRDMA_IAF_REMOVING, &ia->ri_flags);
		ep->rep_connected = -ENODEV;
		xprt_force_disconnect(&xprt->rx_xprt);
		wait_for_completion(&ia->ri_remove_done);

		ia->ri_id = NULL;
		ia->ri_pd = NULL;
		ia->ri_device = NULL;
		/* Return 1 to ensure the core destroys the id. */
		return 1;
	case RDMA_CM_EVENT_ESTABLISHED:
		++xprt->rx_xprt.connect_cookie;
		connstate = 1;
		rpcrdma_update_connect_private(xprt, &event->param.conn);
		goto connected;
	case RDMA_CM_EVENT_CONNECT_ERROR:
		connstate = -ENOTCONN;
		goto connected;
	case RDMA_CM_EVENT_UNREACHABLE:
		connstate = -ENETDOWN;
		goto connected;
	case RDMA_CM_EVENT_REJECTED:
		dprintk("rpcrdma: connection to %s:%s rejected: %s\n",
			rpcrdma_addrstr(xprt), rpcrdma_portstr(xprt),
			rdma_reject_msg(id, event->status));
		connstate = -ECONNREFUSED;
		if (event->status == IB_CM_REJ_STALE_CONN)
			connstate = -EAGAIN;
		goto connected;
	case RDMA_CM_EVENT_DISCONNECTED:
		++xprt->rx_xprt.connect_cookie;
		connstate = -ECONNABORTED;
connected:
		xprt->rx_buf.rb_credits = 1;
		ep->rep_connected = connstate;
		rpcrdma_conn_func(ep);
		wake_up_all(&ep->rep_connect_wait);
		/*FALLTHROUGH*/
	default:
		dprintk("RPC:       %s: %s:%s on %s/%s (ep 0x%p): %s\n",
			__func__,
			rpcrdma_addrstr(xprt), rpcrdma_portstr(xprt),
			ia->ri_device->name, ia->ri_ops->ro_displayname,
			ep, rdma_event_msg(event->event));
		break;
	}

	return 0;
}

static struct rdma_cm_id *
rpcrdma_create_id(struct rpcrdma_xprt *xprt, struct rpcrdma_ia *ia)
{
	unsigned long wtimeout = msecs_to_jiffies(RDMA_RESOLVE_TIMEOUT) + 1;
	struct rdma_cm_id *id;
	int rc;

	trace_xprtrdma_conn_start(xprt);

	init_completion(&ia->ri_done);
	init_completion(&ia->ri_remove_done);

	id = rdma_create_id(&init_net, rpcrdma_conn_upcall, xprt, RDMA_PS_TCP,
			    IB_QPT_RC);
	if (IS_ERR(id)) {
		rc = PTR_ERR(id);
		dprintk("RPC:       %s: rdma_create_id() failed %i\n",
			__func__, rc);
		return id;
	}

	ia->ri_async_rc = -ETIMEDOUT;
	rc = rdma_resolve_addr(id, NULL,
			       (struct sockaddr *)&xprt->rx_xprt.addr,
			       RDMA_RESOLVE_TIMEOUT);
	if (rc) {
		dprintk("RPC:       %s: rdma_resolve_addr() failed %i\n",
			__func__, rc);
		goto out;
	}
	rc = wait_for_completion_interruptible_timeout(&ia->ri_done, wtimeout);
	if (rc < 0) {
		trace_xprtrdma_conn_tout(xprt);
		goto out;
	}

	rc = ia->ri_async_rc;
	if (rc)
		goto out;

	ia->ri_async_rc = -ETIMEDOUT;
	rc = rdma_resolve_route(id, RDMA_RESOLVE_TIMEOUT);
	if (rc) {
		dprintk("RPC:       %s: rdma_resolve_route() failed %i\n",
			__func__, rc);
		goto out;
	}
	rc = wait_for_completion_interruptible_timeout(&ia->ri_done, wtimeout);
	if (rc < 0) {
		trace_xprtrdma_conn_tout(xprt);
		goto out;
	}
	rc = ia->ri_async_rc;
	if (rc)
		goto out;

	return id;

out:
	rdma_destroy_id(id);
	return ERR_PTR(rc);
}

/*
 * Exported functions.
 */

/**
 * rpcrdma_ia_open - Open and initialize an Interface Adapter.
 * @xprt: transport with IA to (re)initialize
 *
 * Returns 0 on success, negative errno if an appropriate
 * Interface Adapter could not be found and opened.
 */
int
rpcrdma_ia_open(struct rpcrdma_xprt *xprt)
{
	struct rpcrdma_ia *ia = &xprt->rx_ia;
	int rc;

	ia->ri_id = rpcrdma_create_id(xprt, ia);
	if (IS_ERR(ia->ri_id)) {
		rc = PTR_ERR(ia->ri_id);
		goto out_err;
	}
	ia->ri_device = ia->ri_id->device;

	ia->ri_pd = ib_alloc_pd(ia->ri_device, 0);
	if (IS_ERR(ia->ri_pd)) {
		rc = PTR_ERR(ia->ri_pd);
		pr_err("rpcrdma: ib_alloc_pd() returned %d\n", rc);
		goto out_err;
	}

	switch (xprt_rdma_memreg_strategy) {
	case RPCRDMA_FRWR:
		if (frwr_is_supported(ia)) {
			ia->ri_ops = &rpcrdma_frwr_memreg_ops;
			break;
		}
		/*FALLTHROUGH*/
	case RPCRDMA_MTHCAFMR:
		if (fmr_is_supported(ia)) {
			ia->ri_ops = &rpcrdma_fmr_memreg_ops;
			break;
		}
		/*FALLTHROUGH*/
	default:
		pr_err("rpcrdma: Device %s does not support memreg mode %d\n",
		       ia->ri_device->name, xprt_rdma_memreg_strategy);
		rc = -EINVAL;
		goto out_err;
	}

	return 0;

out_err:
	rpcrdma_ia_close(ia);
	return rc;
}

/**
 * rpcrdma_ia_remove - Handle device driver unload
 * @ia: interface adapter being removed
 *
 * Divest transport H/W resources associated with this adapter,
 * but allow it to be restored later.
 */
void
rpcrdma_ia_remove(struct rpcrdma_ia *ia)
{
	struct rpcrdma_xprt *r_xprt = container_of(ia, struct rpcrdma_xprt,
						   rx_ia);
	struct rpcrdma_ep *ep = &r_xprt->rx_ep;
	struct rpcrdma_buffer *buf = &r_xprt->rx_buf;
	struct rpcrdma_req *req;
	struct rpcrdma_rep *rep;

	cancel_delayed_work_sync(&buf->rb_refresh_worker);

	/* This is similar to rpcrdma_ep_destroy, but:
	 * - Don't cancel the connect worker.
	 * - Don't call rpcrdma_ep_disconnect, which waits
	 *   for another conn upcall, which will deadlock.
	 * - rdma_disconnect is unneeded, the underlying
	 *   connection is already gone.
	 */
	if (ia->ri_id->qp) {
		ib_drain_qp(ia->ri_id->qp);
		rdma_destroy_qp(ia->ri_id);
		ia->ri_id->qp = NULL;
	}
	ib_free_cq(ep->rep_attr.recv_cq);
	ib_free_cq(ep->rep_attr.send_cq);

	/* The ULP is responsible for ensuring all DMA
	 * mappings and MRs are gone.
	 */
	list_for_each_entry(rep, &buf->rb_recv_bufs, rr_list)
		rpcrdma_dma_unmap_regbuf(rep->rr_rdmabuf);
	list_for_each_entry(req, &buf->rb_allreqs, rl_all) {
		rpcrdma_dma_unmap_regbuf(req->rl_rdmabuf);
		rpcrdma_dma_unmap_regbuf(req->rl_sendbuf);
		rpcrdma_dma_unmap_regbuf(req->rl_recvbuf);
	}
	rpcrdma_mrs_destroy(buf);

	/* Allow waiters to continue */
	complete(&ia->ri_remove_done);

	trace_xprtrdma_remove(r_xprt);
}

/**
 * rpcrdma_ia_close - Clean up/close an IA.
 * @ia: interface adapter to close
 *
 */
void
rpcrdma_ia_close(struct rpcrdma_ia *ia)
{
	if (ia->ri_id != NULL && !IS_ERR(ia->ri_id)) {
		if (ia->ri_id->qp)
			rdma_destroy_qp(ia->ri_id);
		rdma_destroy_id(ia->ri_id);
	}
	ia->ri_id = NULL;
	ia->ri_device = NULL;

	/* If the pd is still busy, xprtrdma missed freeing a resource */
	if (ia->ri_pd && !IS_ERR(ia->ri_pd))
		ib_dealloc_pd(ia->ri_pd);
	ia->ri_pd = NULL;
}

/*
 * Create unconnected endpoint.
 */
int
rpcrdma_ep_create(struct rpcrdma_ep *ep, struct rpcrdma_ia *ia,
		  struct rpcrdma_create_data_internal *cdata)
{
	struct rpcrdma_connect_private *pmsg = &ep->rep_cm_private;
	unsigned int max_qp_wr, max_sge;
	struct ib_cq *sendcq, *recvcq;
	int rc;

	max_sge = min_t(unsigned int, ia->ri_device->attrs.max_sge,
			RPCRDMA_MAX_SEND_SGES);
	if (max_sge < RPCRDMA_MIN_SEND_SGES) {
		pr_warn("rpcrdma: HCA provides only %d send SGEs\n", max_sge);
		return -ENOMEM;
	}
	ia->ri_max_send_sges = max_sge;

	if (ia->ri_device->attrs.max_qp_wr <= RPCRDMA_BACKWARD_WRS) {
		dprintk("RPC:       %s: insufficient wqe's available\n",
			__func__);
		return -ENOMEM;
	}
	max_qp_wr = ia->ri_device->attrs.max_qp_wr - RPCRDMA_BACKWARD_WRS - 1;

	/* check provider's send/recv wr limits */
	if (cdata->max_requests > max_qp_wr)
		cdata->max_requests = max_qp_wr;

	ep->rep_attr.event_handler = rpcrdma_qp_async_error_upcall;
	ep->rep_attr.qp_context = ep;
	ep->rep_attr.srq = NULL;
	ep->rep_attr.cap.max_send_wr = cdata->max_requests;
	ep->rep_attr.cap.max_send_wr += RPCRDMA_BACKWARD_WRS;
	ep->rep_attr.cap.max_send_wr += 1;	/* drain cqe */
	rc = ia->ri_ops->ro_open(ia, ep, cdata);
	if (rc)
		return rc;
	ep->rep_attr.cap.max_recv_wr = cdata->max_requests;
	ep->rep_attr.cap.max_recv_wr += RPCRDMA_BACKWARD_WRS;
	ep->rep_attr.cap.max_recv_wr += 1;	/* drain cqe */
	ep->rep_attr.cap.max_send_sge = max_sge;
	ep->rep_attr.cap.max_recv_sge = 1;
	ep->rep_attr.cap.max_inline_data = 0;
	ep->rep_attr.sq_sig_type = IB_SIGNAL_REQ_WR;
	ep->rep_attr.qp_type = IB_QPT_RC;
	ep->rep_attr.port_num = ~0;

	dprintk("RPC:       %s: requested max: dtos: send %d recv %d; "
		"iovs: send %d recv %d\n",
		__func__,
		ep->rep_attr.cap.max_send_wr,
		ep->rep_attr.cap.max_recv_wr,
		ep->rep_attr.cap.max_send_sge,
		ep->rep_attr.cap.max_recv_sge);

	/* set trigger for requesting send completion */
	ep->rep_send_batch = min_t(unsigned int, RPCRDMA_MAX_SEND_BATCH,
				   cdata->max_requests >> 2);
	ep->rep_send_count = ep->rep_send_batch;
	init_waitqueue_head(&ep->rep_connect_wait);
	INIT_DELAYED_WORK(&ep->rep_connect_worker, rpcrdma_connect_worker);

	sendcq = ib_alloc_cq(ia->ri_device, NULL,
			     ep->rep_attr.cap.max_send_wr + 1,
			     1, IB_POLL_WORKQUEUE);
	if (IS_ERR(sendcq)) {
		rc = PTR_ERR(sendcq);
		dprintk("RPC:       %s: failed to create send CQ: %i\n",
			__func__, rc);
		goto out1;
	}

	recvcq = ib_alloc_cq(ia->ri_device, NULL,
			     ep->rep_attr.cap.max_recv_wr + 1,
			     0, IB_POLL_WORKQUEUE);
	if (IS_ERR(recvcq)) {
		rc = PTR_ERR(recvcq);
		dprintk("RPC:       %s: failed to create recv CQ: %i\n",
			__func__, rc);
		goto out2;
	}

	ep->rep_attr.send_cq = sendcq;
	ep->rep_attr.recv_cq = recvcq;

	/* Initialize cma parameters */
	memset(&ep->rep_remote_cma, 0, sizeof(ep->rep_remote_cma));

	/* Prepare RDMA-CM private message */
	pmsg->cp_magic = rpcrdma_cmp_magic;
	pmsg->cp_version = RPCRDMA_CMP_VERSION;
	pmsg->cp_flags |= ia->ri_ops->ro_send_w_inv_ok;
	pmsg->cp_send_size = rpcrdma_encode_buffer_size(cdata->inline_wsize);
	pmsg->cp_recv_size = rpcrdma_encode_buffer_size(cdata->inline_rsize);
	ep->rep_remote_cma.private_data = pmsg;
	ep->rep_remote_cma.private_data_len = sizeof(*pmsg);

	/* Client offers RDMA Read but does not initiate */
	ep->rep_remote_cma.initiator_depth = 0;
	ep->rep_remote_cma.responder_resources =
		min_t(int, U8_MAX, ia->ri_device->attrs.max_qp_rd_atom);

	/* Limit transport retries so client can detect server
	 * GID changes quickly. RPC layer handles re-establishing
	 * transport connection and retransmission.
	 */
	ep->rep_remote_cma.retry_count = 6;

	/* RPC-over-RDMA handles its own flow control. In addition,
	 * make all RNR NAKs visible so we know that RPC-over-RDMA
	 * flow control is working correctly (no NAKs should be seen).
	 */
	ep->rep_remote_cma.flow_control = 0;
	ep->rep_remote_cma.rnr_retry_count = 0;

	return 0;

out2:
	ib_free_cq(sendcq);
out1:
	return rc;
}

/*
 * rpcrdma_ep_destroy
 *
 * Disconnect and destroy endpoint. After this, the only
 * valid operations on the ep are to free it (if dynamically
 * allocated) or re-create it.
 */
void
rpcrdma_ep_destroy(struct rpcrdma_ep *ep, struct rpcrdma_ia *ia)
{
	cancel_delayed_work_sync(&ep->rep_connect_worker);

	if (ia->ri_id->qp) {
		rpcrdma_ep_disconnect(ep, ia);
		rdma_destroy_qp(ia->ri_id);
		ia->ri_id->qp = NULL;
	}

	ib_free_cq(ep->rep_attr.recv_cq);
	ib_free_cq(ep->rep_attr.send_cq);
}

/* Re-establish a connection after a device removal event.
 * Unlike a normal reconnection, a fresh PD and a new set
 * of MRs and buffers is needed.
 */
static int
rpcrdma_ep_recreate_xprt(struct rpcrdma_xprt *r_xprt,
			 struct rpcrdma_ep *ep, struct rpcrdma_ia *ia)
{
	int rc, err;

	trace_xprtrdma_reinsert(r_xprt);

	rc = -EHOSTUNREACH;
	if (rpcrdma_ia_open(r_xprt))
		goto out1;

	rc = -ENOMEM;
	err = rpcrdma_ep_create(ep, ia, &r_xprt->rx_data);
	if (err) {
		pr_err("rpcrdma: rpcrdma_ep_create returned %d\n", err);
		goto out2;
	}

	rc = -ENETUNREACH;
	err = rdma_create_qp(ia->ri_id, ia->ri_pd, &ep->rep_attr);
	if (err) {
		pr_err("rpcrdma: rdma_create_qp returned %d\n", err);
		goto out3;
	}

	rpcrdma_mrs_create(r_xprt);
	return 0;

out3:
	rpcrdma_ep_destroy(ep, ia);
out2:
	rpcrdma_ia_close(ia);
out1:
	return rc;
}

static int
rpcrdma_ep_reconnect(struct rpcrdma_xprt *r_xprt, struct rpcrdma_ep *ep,
		     struct rpcrdma_ia *ia)
{
	struct rdma_cm_id *id, *old;
	int err, rc;

	trace_xprtrdma_reconnect(r_xprt);

	rpcrdma_ep_disconnect(ep, ia);

	rc = -EHOSTUNREACH;
	id = rpcrdma_create_id(r_xprt, ia);
	if (IS_ERR(id))
		goto out;

	/* As long as the new ID points to the same device as the
	 * old ID, we can reuse the transport's existing PD and all
	 * previously allocated MRs. Also, the same device means
	 * the transport's previous DMA mappings are still valid.
	 *
	 * This is a sanity check only. There should be no way these
	 * point to two different devices here.
	 */
	old = id;
	rc = -ENETUNREACH;
	if (ia->ri_device != id->device) {
		pr_err("rpcrdma: can't reconnect on different device!\n");
		goto out_destroy;
	}

	err = rdma_create_qp(id, ia->ri_pd, &ep->rep_attr);
	if (err) {
		dprintk("RPC:       %s: rdma_create_qp returned %d\n",
			__func__, err);
		goto out_destroy;
	}

	/* Atomically replace the transport's ID and QP. */
	rc = 0;
	old = ia->ri_id;
	ia->ri_id = id;
	rdma_destroy_qp(old);

out_destroy:
	rdma_destroy_id(old);
out:
	return rc;
}

/*
 * Connect unconnected endpoint.
 */
int
rpcrdma_ep_connect(struct rpcrdma_ep *ep, struct rpcrdma_ia *ia)
{
	struct rpcrdma_xprt *r_xprt = container_of(ia, struct rpcrdma_xprt,
						   rx_ia);
	unsigned int extras;
	int rc;

retry:
	switch (ep->rep_connected) {
	case 0:
		dprintk("RPC:       %s: connecting...\n", __func__);
		rc = rdma_create_qp(ia->ri_id, ia->ri_pd, &ep->rep_attr);
		if (rc) {
			dprintk("RPC:       %s: rdma_create_qp failed %i\n",
				__func__, rc);
			rc = -ENETUNREACH;
			goto out_noupdate;
		}
		break;
	case -ENODEV:
		rc = rpcrdma_ep_recreate_xprt(r_xprt, ep, ia);
		if (rc)
			goto out_noupdate;
		break;
	default:
		rc = rpcrdma_ep_reconnect(r_xprt, ep, ia);
		if (rc)
			goto out;
	}

	ep->rep_connected = 0;

	rc = rdma_connect(ia->ri_id, &ep->rep_remote_cma);
	if (rc) {
		dprintk("RPC:       %s: rdma_connect() failed with %i\n",
				__func__, rc);
		goto out;
	}

	wait_event_interruptible(ep->rep_connect_wait, ep->rep_connected != 0);
	if (ep->rep_connected <= 0) {
		if (ep->rep_connected == -EAGAIN)
			goto retry;
		rc = ep->rep_connected;
		goto out;
	}

	dprintk("RPC:       %s: connected\n", __func__);
	extras = r_xprt->rx_buf.rb_bc_srv_max_requests;
	if (extras)
		rpcrdma_ep_post_extra_recv(r_xprt, extras);

out:
	if (rc)
		ep->rep_connected = rc;

out_noupdate:
	return rc;
}

/*
 * rpcrdma_ep_disconnect
 *
 * This is separate from destroy to facilitate the ability
 * to reconnect without recreating the endpoint.
 *
 * This call is not reentrant, and must not be made in parallel
 * on the same endpoint.
 */
void
rpcrdma_ep_disconnect(struct rpcrdma_ep *ep, struct rpcrdma_ia *ia)
{
	int rc;

	rc = rdma_disconnect(ia->ri_id);
	if (!rc)
		/* returns without wait if not connected */
		wait_event_interruptible(ep->rep_connect_wait,
							ep->rep_connected != 1);
	else
		ep->rep_connected = rc;
	trace_xprtrdma_disconnect(container_of(ep, struct rpcrdma_xprt,
					       rx_ep), rc);

	ib_drain_qp(ia->ri_id->qp);
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
 * queue activity, and ib_drain_qp has flushed all remaining Send
 * requests.
 */
static void rpcrdma_sendctxs_destroy(struct rpcrdma_buffer *buf)
{
	unsigned long i;

	for (i = 0; i <= buf->rb_sc_last; i++)
		kfree(buf->rb_sc_ctxs[i]);
	kfree(buf->rb_sc_ctxs);
}

static struct rpcrdma_sendctx *rpcrdma_sendctx_create(struct rpcrdma_ia *ia)
{
	struct rpcrdma_sendctx *sc;

	sc = kzalloc(sizeof(*sc) +
		     ia->ri_max_send_sges * sizeof(struct ib_sge),
		     GFP_KERNEL);
	if (!sc)
		return NULL;

	sc->sc_wr.wr_cqe = &sc->sc_cqe;
	sc->sc_wr.sg_list = sc->sc_sges;
	sc->sc_wr.opcode = IB_WR_SEND;
	sc->sc_cqe.done = rpcrdma_wc_send;
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
	i = buf->rb_max_requests + RPCRDMA_MAX_BC_REQUESTS;
	dprintk("RPC:       %s: allocating %lu send_ctxs\n", __func__, i);
	buf->rb_sc_ctxs = kcalloc(i, sizeof(sc), GFP_KERNEL);
	if (!buf->rb_sc_ctxs)
		return -ENOMEM;

	buf->rb_sc_last = i - 1;
	for (i = 0; i <= buf->rb_sc_last; i++) {
		sc = rpcrdma_sendctx_create(&r_xprt->rx_ia);
		if (!sc)
			goto out_destroy;

		sc->sc_xprt = r_xprt;
		buf->rb_sc_ctxs[i] = sc;
	}

	return 0;

out_destroy:
	rpcrdma_sendctxs_destroy(buf);
	return -ENOMEM;
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
 * @buf: transport buffers from which to acquire an unused context
 *
 * Returns pointer to a free send completion context; or NULL if
 * the queue is empty.
 *
 * Usage: Called to acquire an SGE array before preparing a Send WR.
 *
 * The caller serializes calls to this function (per rpcrdma_buffer),
 * and provides an effective memory barrier that flushes the new value
 * of rb_sc_head.
 */
struct rpcrdma_sendctx *rpcrdma_sendctx_get_locked(struct rpcrdma_buffer *buf)
{
	struct rpcrdma_xprt *r_xprt;
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
	dprintk("RPC:       %s: empty sendctx queue\n", __func__);
	r_xprt = container_of(buf, struct rpcrdma_xprt, rx_buf);
	r_xprt->rx_stats.empty_sendctx_q++;
	return NULL;
}

/**
 * rpcrdma_sendctx_put_locked - Release a send context
 * @sc: send context to release
 *
 * Usage: Called from Send completion to return a sendctxt
 * to the queue.
 *
 * The caller serializes calls to this function (per rpcrdma_buffer).
 */
void rpcrdma_sendctx_put_locked(struct rpcrdma_sendctx *sc)
{
	struct rpcrdma_buffer *buf = &sc->sc_xprt->rx_buf;
	unsigned long next_tail;

	/* Unmap SGEs of previously completed by unsignaled
	 * Sends by walking up the queue until @sc is found.
	 */
	next_tail = buf->rb_sc_tail;
	do {
		next_tail = rpcrdma_sendctx_next(buf, next_tail);

		/* ORDER: item must be accessed _before_ tail is updated */
		rpcrdma_unmap_sendctx(buf->rb_sc_ctxs[next_tail]);

	} while (buf->rb_sc_ctxs[next_tail] != sc);

	/* Paired with READ_ONCE */
	smp_store_release(&buf->rb_sc_tail, next_tail);
}

static void
rpcrdma_mr_recovery_worker(struct work_struct *work)
{
	struct rpcrdma_buffer *buf = container_of(work, struct rpcrdma_buffer,
						  rb_recovery_worker.work);
	struct rpcrdma_mr *mr;

	spin_lock(&buf->rb_recovery_lock);
	while (!list_empty(&buf->rb_stale_mrs)) {
		mr = rpcrdma_mr_pop(&buf->rb_stale_mrs);
		spin_unlock(&buf->rb_recovery_lock);

		trace_xprtrdma_recover_mr(mr);
		mr->mr_xprt->rx_ia.ri_ops->ro_recover_mr(mr);

		spin_lock(&buf->rb_recovery_lock);
	}
	spin_unlock(&buf->rb_recovery_lock);
}

void
rpcrdma_mr_defer_recovery(struct rpcrdma_mr *mr)
{
	struct rpcrdma_xprt *r_xprt = mr->mr_xprt;
	struct rpcrdma_buffer *buf = &r_xprt->rx_buf;

	spin_lock(&buf->rb_recovery_lock);
	rpcrdma_mr_push(mr, &buf->rb_stale_mrs);
	spin_unlock(&buf->rb_recovery_lock);

	schedule_delayed_work(&buf->rb_recovery_worker, 0);
}

static void
rpcrdma_mrs_create(struct rpcrdma_xprt *r_xprt)
{
	struct rpcrdma_buffer *buf = &r_xprt->rx_buf;
	struct rpcrdma_ia *ia = &r_xprt->rx_ia;
	unsigned int count;
	LIST_HEAD(free);
	LIST_HEAD(all);

	for (count = 0; count < 3; count++) {
		struct rpcrdma_mr *mr;
		int rc;

		mr = kzalloc(sizeof(*mr), GFP_KERNEL);
		if (!mr)
			break;

		rc = ia->ri_ops->ro_init_mr(ia, mr);
		if (rc) {
			kfree(mr);
			break;
		}

		mr->mr_xprt = r_xprt;

		list_add(&mr->mr_list, &free);
		list_add(&mr->mr_all, &all);
	}

	spin_lock(&buf->rb_mrlock);
	list_splice(&free, &buf->rb_mrs);
	list_splice(&all, &buf->rb_all);
	r_xprt->rx_stats.mrs_allocated += count;
	spin_unlock(&buf->rb_mrlock);
	trace_xprtrdma_createmrs(r_xprt, count);

	xprt_write_space(&r_xprt->rx_xprt);
}

static void
rpcrdma_mr_refresh_worker(struct work_struct *work)
{
	struct rpcrdma_buffer *buf = container_of(work, struct rpcrdma_buffer,
						  rb_refresh_worker.work);
	struct rpcrdma_xprt *r_xprt = container_of(buf, struct rpcrdma_xprt,
						   rx_buf);

	rpcrdma_mrs_create(r_xprt);
}

struct rpcrdma_req *
rpcrdma_create_req(struct rpcrdma_xprt *r_xprt)
{
	struct rpcrdma_buffer *buffer = &r_xprt->rx_buf;
	struct rpcrdma_req *req;

	req = kzalloc(sizeof(*req), GFP_KERNEL);
	if (req == NULL)
		return ERR_PTR(-ENOMEM);

	spin_lock(&buffer->rb_reqslock);
	list_add(&req->rl_all, &buffer->rb_allreqs);
	spin_unlock(&buffer->rb_reqslock);
	req->rl_buffer = &r_xprt->rx_buf;
	INIT_LIST_HEAD(&req->rl_registered);
	return req;
}

/**
 * rpcrdma_create_rep - Allocate an rpcrdma_rep object
 * @r_xprt: controlling transport
 *
 * Returns 0 on success or a negative errno on failure.
 */
int
rpcrdma_create_rep(struct rpcrdma_xprt *r_xprt)
{
	struct rpcrdma_create_data_internal *cdata = &r_xprt->rx_data;
	struct rpcrdma_buffer *buf = &r_xprt->rx_buf;
	struct rpcrdma_rep *rep;
	int rc;

	rc = -ENOMEM;
	rep = kzalloc(sizeof(*rep), GFP_KERNEL);
	if (rep == NULL)
		goto out;

	rep->rr_rdmabuf = rpcrdma_alloc_regbuf(cdata->inline_rsize,
					       DMA_FROM_DEVICE, GFP_KERNEL);
	if (IS_ERR(rep->rr_rdmabuf)) {
		rc = PTR_ERR(rep->rr_rdmabuf);
		goto out_free;
	}
	xdr_buf_init(&rep->rr_hdrbuf, rep->rr_rdmabuf->rg_base,
		     rdmab_length(rep->rr_rdmabuf));

	rep->rr_cqe.done = rpcrdma_wc_receive;
	rep->rr_rxprt = r_xprt;
	INIT_WORK(&rep->rr_work, rpcrdma_deferred_completion);
	rep->rr_recv_wr.next = NULL;
	rep->rr_recv_wr.wr_cqe = &rep->rr_cqe;
	rep->rr_recv_wr.sg_list = &rep->rr_rdmabuf->rg_iov;
	rep->rr_recv_wr.num_sge = 1;

	spin_lock(&buf->rb_lock);
	list_add(&rep->rr_list, &buf->rb_recv_bufs);
	spin_unlock(&buf->rb_lock);
	return 0;

out_free:
	kfree(rep);
out:
	dprintk("RPC:       %s: reply buffer %d alloc failed\n",
		__func__, rc);
	return rc;
}

int
rpcrdma_buffer_create(struct rpcrdma_xprt *r_xprt)
{
	struct rpcrdma_buffer *buf = &r_xprt->rx_buf;
	int i, rc;

	buf->rb_max_requests = r_xprt->rx_data.max_requests;
	buf->rb_bc_srv_max_requests = 0;
	spin_lock_init(&buf->rb_mrlock);
	spin_lock_init(&buf->rb_lock);
	spin_lock_init(&buf->rb_recovery_lock);
	INIT_LIST_HEAD(&buf->rb_mrs);
	INIT_LIST_HEAD(&buf->rb_all);
	INIT_LIST_HEAD(&buf->rb_stale_mrs);
	INIT_DELAYED_WORK(&buf->rb_refresh_worker,
			  rpcrdma_mr_refresh_worker);
	INIT_DELAYED_WORK(&buf->rb_recovery_worker,
			  rpcrdma_mr_recovery_worker);

	rpcrdma_mrs_create(r_xprt);

	INIT_LIST_HEAD(&buf->rb_send_bufs);
	INIT_LIST_HEAD(&buf->rb_allreqs);
	spin_lock_init(&buf->rb_reqslock);
	for (i = 0; i < buf->rb_max_requests; i++) {
		struct rpcrdma_req *req;

		req = rpcrdma_create_req(r_xprt);
		if (IS_ERR(req)) {
			dprintk("RPC:       %s: request buffer %d alloc"
				" failed\n", __func__, i);
			rc = PTR_ERR(req);
			goto out;
		}
		list_add(&req->rl_list, &buf->rb_send_bufs);
	}

	INIT_LIST_HEAD(&buf->rb_recv_bufs);
	for (i = 0; i <= buf->rb_max_requests; i++) {
		rc = rpcrdma_create_rep(r_xprt);
		if (rc)
			goto out;
	}

	rc = rpcrdma_sendctxs_create(r_xprt);
	if (rc)
		goto out;

	return 0;
out:
	rpcrdma_buffer_destroy(buf);
	return rc;
}

static struct rpcrdma_req *
rpcrdma_buffer_get_req_locked(struct rpcrdma_buffer *buf)
{
	struct rpcrdma_req *req;

	req = list_first_entry(&buf->rb_send_bufs,
			       struct rpcrdma_req, rl_list);
	list_del_init(&req->rl_list);
	return req;
}

static struct rpcrdma_rep *
rpcrdma_buffer_get_rep_locked(struct rpcrdma_buffer *buf)
{
	struct rpcrdma_rep *rep;

	rep = list_first_entry(&buf->rb_recv_bufs,
			       struct rpcrdma_rep, rr_list);
	list_del(&rep->rr_list);
	return rep;
}

static void
rpcrdma_destroy_rep(struct rpcrdma_rep *rep)
{
	rpcrdma_free_regbuf(rep->rr_rdmabuf);
	kfree(rep);
}

void
rpcrdma_destroy_req(struct rpcrdma_req *req)
{
	rpcrdma_free_regbuf(req->rl_recvbuf);
	rpcrdma_free_regbuf(req->rl_sendbuf);
	rpcrdma_free_regbuf(req->rl_rdmabuf);
	kfree(req);
}

static void
rpcrdma_mrs_destroy(struct rpcrdma_buffer *buf)
{
	struct rpcrdma_xprt *r_xprt = container_of(buf, struct rpcrdma_xprt,
						   rx_buf);
	struct rpcrdma_ia *ia = rdmab_to_ia(buf);
	struct rpcrdma_mr *mr;
	unsigned int count;

	count = 0;
	spin_lock(&buf->rb_mrlock);
	while (!list_empty(&buf->rb_all)) {
		mr = list_entry(buf->rb_all.next, struct rpcrdma_mr, mr_all);
		list_del(&mr->mr_all);

		spin_unlock(&buf->rb_mrlock);
		ia->ri_ops->ro_release_mr(mr);
		count++;
		spin_lock(&buf->rb_mrlock);
	}
	spin_unlock(&buf->rb_mrlock);
	r_xprt->rx_stats.mrs_allocated = 0;

	dprintk("RPC:       %s: released %u MRs\n", __func__, count);
}

void
rpcrdma_buffer_destroy(struct rpcrdma_buffer *buf)
{
	cancel_delayed_work_sync(&buf->rb_recovery_worker);
	cancel_delayed_work_sync(&buf->rb_refresh_worker);

	rpcrdma_sendctxs_destroy(buf);

	while (!list_empty(&buf->rb_recv_bufs)) {
		struct rpcrdma_rep *rep;

		rep = rpcrdma_buffer_get_rep_locked(buf);
		rpcrdma_destroy_rep(rep);
	}
	buf->rb_send_count = 0;

	spin_lock(&buf->rb_reqslock);
	while (!list_empty(&buf->rb_allreqs)) {
		struct rpcrdma_req *req;

		req = list_first_entry(&buf->rb_allreqs,
				       struct rpcrdma_req, rl_all);
		list_del(&req->rl_all);

		spin_unlock(&buf->rb_reqslock);
		rpcrdma_destroy_req(req);
		spin_lock(&buf->rb_reqslock);
	}
	spin_unlock(&buf->rb_reqslock);
	buf->rb_recv_count = 0;

	rpcrdma_mrs_destroy(buf);
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
	struct rpcrdma_mr *mr = NULL;

	spin_lock(&buf->rb_mrlock);
	if (!list_empty(&buf->rb_mrs))
		mr = rpcrdma_mr_pop(&buf->rb_mrs);
	spin_unlock(&buf->rb_mrlock);

	if (!mr)
		goto out_nomrs;
	return mr;

out_nomrs:
	trace_xprtrdma_nomrs(r_xprt);
	if (r_xprt->rx_ep.rep_connected != -ENODEV)
		schedule_delayed_work(&buf->rb_refresh_worker, 0);

	/* Allow the reply handler and refresh worker to run */
	cond_resched();

	return NULL;
}

static void
__rpcrdma_mr_put(struct rpcrdma_buffer *buf, struct rpcrdma_mr *mr)
{
	spin_lock(&buf->rb_mrlock);
	rpcrdma_mr_push(mr, &buf->rb_mrs);
	spin_unlock(&buf->rb_mrlock);
}

/**
 * rpcrdma_mr_put - Release an rpcrdma_mr object
 * @mr: object to release
 *
 */
void
rpcrdma_mr_put(struct rpcrdma_mr *mr)
{
	__rpcrdma_mr_put(&mr->mr_xprt->rx_buf, mr);
}

/**
 * rpcrdma_mr_unmap_and_put - DMA unmap an MR and release it
 * @mr: object to release
 *
 */
void
rpcrdma_mr_unmap_and_put(struct rpcrdma_mr *mr)
{
	struct rpcrdma_xprt *r_xprt = mr->mr_xprt;

	trace_xprtrdma_dma_unmap(mr);
	ib_dma_unmap_sg(r_xprt->rx_ia.ri_device,
			mr->mr_sg, mr->mr_nents, mr->mr_dir);
	__rpcrdma_mr_put(&r_xprt->rx_buf, mr);
}

static struct rpcrdma_rep *
rpcrdma_buffer_get_rep(struct rpcrdma_buffer *buffers)
{
	/* If an RPC previously completed without a reply (say, a
	 * credential problem or a soft timeout occurs) then hold off
	 * on supplying more Receive buffers until the number of new
	 * pending RPCs catches up to the number of posted Receives.
	 */
	if (unlikely(buffers->rb_send_count < buffers->rb_recv_count))
		return NULL;

	if (unlikely(list_empty(&buffers->rb_recv_bufs)))
		return NULL;
	buffers->rb_recv_count++;
	return rpcrdma_buffer_get_rep_locked(buffers);
}

/*
 * Get a set of request/reply buffers.
 *
 * Reply buffer (if available) is attached to send buffer upon return.
 */
struct rpcrdma_req *
rpcrdma_buffer_get(struct rpcrdma_buffer *buffers)
{
	struct rpcrdma_req *req;

	spin_lock(&buffers->rb_lock);
	if (list_empty(&buffers->rb_send_bufs))
		goto out_reqbuf;
	buffers->rb_send_count++;
	req = rpcrdma_buffer_get_req_locked(buffers);
	req->rl_reply = rpcrdma_buffer_get_rep(buffers);
	spin_unlock(&buffers->rb_lock);

	return req;

out_reqbuf:
	spin_unlock(&buffers->rb_lock);
	return NULL;
}

/*
 * Put request/reply buffers back into pool.
 * Pre-decrement counter/array index.
 */
void
rpcrdma_buffer_put(struct rpcrdma_req *req)
{
	struct rpcrdma_buffer *buffers = req->rl_buffer;
	struct rpcrdma_rep *rep = req->rl_reply;

	req->rl_reply = NULL;

	spin_lock(&buffers->rb_lock);
	buffers->rb_send_count--;
	list_add_tail(&req->rl_list, &buffers->rb_send_bufs);
	if (rep) {
		buffers->rb_recv_count--;
		list_add_tail(&rep->rr_list, &buffers->rb_recv_bufs);
	}
	spin_unlock(&buffers->rb_lock);
}

/*
 * Recover reply buffers from pool.
 * This happens when recovering from disconnect.
 */
void
rpcrdma_recv_buffer_get(struct rpcrdma_req *req)
{
	struct rpcrdma_buffer *buffers = req->rl_buffer;

	spin_lock(&buffers->rb_lock);
	req->rl_reply = rpcrdma_buffer_get_rep(buffers);
	spin_unlock(&buffers->rb_lock);
}

/*
 * Put reply buffers back into pool when not attached to
 * request. This happens in error conditions.
 */
void
rpcrdma_recv_buffer_put(struct rpcrdma_rep *rep)
{
	struct rpcrdma_buffer *buffers = &rep->rr_rxprt->rx_buf;

	spin_lock(&buffers->rb_lock);
	buffers->rb_recv_count--;
	list_add_tail(&rep->rr_list, &buffers->rb_recv_bufs);
	spin_unlock(&buffers->rb_lock);
}

/**
 * rpcrdma_alloc_regbuf - allocate and DMA-map memory for SEND/RECV buffers
 * @size: size of buffer to be allocated, in bytes
 * @direction: direction of data movement
 * @flags: GFP flags
 *
 * Returns an ERR_PTR, or a pointer to a regbuf, a buffer that
 * can be persistently DMA-mapped for I/O.
 *
 * xprtrdma uses a regbuf for posting an outgoing RDMA SEND, or for
 * receiving the payload of RDMA RECV operations. During Long Calls
 * or Replies they may be registered externally via ro_map.
 */
struct rpcrdma_regbuf *
rpcrdma_alloc_regbuf(size_t size, enum dma_data_direction direction,
		     gfp_t flags)
{
	struct rpcrdma_regbuf *rb;

	rb = kmalloc(sizeof(*rb) + size, flags);
	if (rb == NULL)
		return ERR_PTR(-ENOMEM);

	rb->rg_device = NULL;
	rb->rg_direction = direction;
	rb->rg_iov.length = size;

	return rb;
}

/**
 * __rpcrdma_map_regbuf - DMA-map a regbuf
 * @ia: controlling rpcrdma_ia
 * @rb: regbuf to be mapped
 */
bool
__rpcrdma_dma_map_regbuf(struct rpcrdma_ia *ia, struct rpcrdma_regbuf *rb)
{
	struct ib_device *device = ia->ri_device;

	if (rb->rg_direction == DMA_NONE)
		return false;

	rb->rg_iov.addr = ib_dma_map_single(device,
					    (void *)rb->rg_base,
					    rdmab_length(rb),
					    rb->rg_direction);
	if (ib_dma_mapping_error(device, rdmab_addr(rb)))
		return false;

	rb->rg_device = device;
	rb->rg_iov.lkey = ia->ri_pd->local_dma_lkey;
	return true;
}

static void
rpcrdma_dma_unmap_regbuf(struct rpcrdma_regbuf *rb)
{
	if (!rb)
		return;

	if (!rpcrdma_regbuf_is_mapped(rb))
		return;

	ib_dma_unmap_single(rb->rg_device, rdmab_addr(rb),
			    rdmab_length(rb), rb->rg_direction);
	rb->rg_device = NULL;
}

/**
 * rpcrdma_free_regbuf - deregister and free registered buffer
 * @rb: regbuf to be deregistered and freed
 */
void
rpcrdma_free_regbuf(struct rpcrdma_regbuf *rb)
{
	rpcrdma_dma_unmap_regbuf(rb);
	kfree(rb);
}

/*
 * Prepost any receive buffer, then post send.
 *
 * Receive buffer is donated to hardware, reclaimed upon recv completion.
 */
int
rpcrdma_ep_post(struct rpcrdma_ia *ia,
		struct rpcrdma_ep *ep,
		struct rpcrdma_req *req)
{
	struct ib_send_wr *send_wr = &req->rl_sendctx->sc_wr;
	int rc;

	if (req->rl_reply) {
		rc = rpcrdma_ep_post_recv(ia, req->rl_reply);
		if (rc)
			return rc;
		req->rl_reply = NULL;
	}

	if (!ep->rep_send_count ||
	    test_bit(RPCRDMA_REQ_F_TX_RESOURCES, &req->rl_flags)) {
		send_wr->send_flags |= IB_SEND_SIGNALED;
		ep->rep_send_count = ep->rep_send_batch;
	} else {
		send_wr->send_flags &= ~IB_SEND_SIGNALED;
		--ep->rep_send_count;
	}

	rc = ia->ri_ops->ro_send(ia, req);
	trace_xprtrdma_post_send(req, rc);
	if (rc)
		return -ENOTCONN;
	return 0;
}

int
rpcrdma_ep_post_recv(struct rpcrdma_ia *ia,
		     struct rpcrdma_rep *rep)
{
	struct ib_recv_wr *recv_wr_fail;
	int rc;

	if (!rpcrdma_dma_map_regbuf(ia, rep->rr_rdmabuf))
		goto out_map;
	rc = ib_post_recv(ia->ri_id->qp, &rep->rr_recv_wr, &recv_wr_fail);
	trace_xprtrdma_post_recv(rep, rc);
	if (rc)
		return -ENOTCONN;
	return 0;

out_map:
	pr_err("rpcrdma: failed to DMA map the Receive buffer\n");
	return -EIO;
}

/**
 * rpcrdma_ep_post_extra_recv - Post buffers for incoming backchannel requests
 * @r_xprt: transport associated with these backchannel resources
 * @count: minimum number of incoming requests expected
 *
 * Returns zero if all requested buffers were posted, or a negative errno.
 */
int
rpcrdma_ep_post_extra_recv(struct rpcrdma_xprt *r_xprt, unsigned int count)
{
	struct rpcrdma_buffer *buffers = &r_xprt->rx_buf;
	struct rpcrdma_ia *ia = &r_xprt->rx_ia;
	struct rpcrdma_rep *rep;
	int rc;

	while (count--) {
		spin_lock(&buffers->rb_lock);
		if (list_empty(&buffers->rb_recv_bufs))
			goto out_reqbuf;
		rep = rpcrdma_buffer_get_rep_locked(buffers);
		spin_unlock(&buffers->rb_lock);

		rc = rpcrdma_ep_post_recv(ia, rep);
		if (rc)
			goto out_rc;
	}

	return 0;

out_reqbuf:
	spin_unlock(&buffers->rb_lock);
	trace_xprtrdma_noreps(r_xprt);
	return -ENOMEM;

out_rc:
	rpcrdma_recv_buffer_put(rep);
	return rc;
}
