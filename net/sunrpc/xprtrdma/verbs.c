/*
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
#include <linux/prefetch.h>
#include <linux/sunrpc/addr.h>
#include <asm/bitops.h>
#include <linux/module.h> /* try_module_get()/module_put() */

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

static struct workqueue_struct *rpcrdma_receive_wq;

int
rpcrdma_alloc_wq(void)
{
	struct workqueue_struct *recv_wq;

	recv_wq = alloc_workqueue("xprtrdma_receive",
				  WQ_MEM_RECLAIM | WQ_UNBOUND | WQ_HIGHPRI,
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

	pr_err("RPC:       %s: %s on device %s ep %p\n",
	       __func__, ib_event_msg(event->event),
		event->device->name, context);
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
	/* WARNING: Only wr_cqe and status are reliable at this point */
	if (wc->status != IB_WC_SUCCESS && wc->status != IB_WC_WR_FLUSH_ERR)
		pr_err("rpcrdma: Send: %s (%u/0x%x)\n",
		       ib_wc_status_msg(wc->status),
		       wc->status, wc->vendor_err);
}

static void
rpcrdma_receive_worker(struct work_struct *work)
{
	struct rpcrdma_rep *rep =
			container_of(work, struct rpcrdma_rep, rr_work);

	rpcrdma_reply_handler(rep);
}

/* Perform basic sanity checking to avoid using garbage
 * to update the credit grant value.
 */
static void
rpcrdma_update_granted_credits(struct rpcrdma_rep *rep)
{
	struct rpcrdma_msg *rmsgp = rdmab_to_msg(rep->rr_rdmabuf);
	struct rpcrdma_buffer *buffer = &rep->rr_rxprt->rx_buf;
	u32 credits;

	if (rep->rr_len < RPCRDMA_HDRLEN_ERR)
		return;

	credits = be32_to_cpu(rmsgp->rm_credit);
	if (credits == 0)
		credits = 1;	/* don't deadlock */
	else if (credits > buffer->rb_max_requests)
		credits = buffer->rb_max_requests;

	atomic_set(&buffer->rb_credits, credits);
}

/**
 * rpcrdma_receive_wc - Invoked by RDMA provider for each polled Receive WC
 * @cq:	completion queue (ignored)
 * @wc:	completed WR
 *
 */
static void
rpcrdma_receive_wc(struct ib_cq *cq, struct ib_wc *wc)
{
	struct ib_cqe *cqe = wc->wr_cqe;
	struct rpcrdma_rep *rep = container_of(cqe, struct rpcrdma_rep,
					       rr_cqe);

	/* WARNING: Only wr_id and status are reliable at this point */
	if (wc->status != IB_WC_SUCCESS)
		goto out_fail;

	/* status == SUCCESS means all fields in wc are trustworthy */
	if (wc->opcode != IB_WC_RECV)
		return;

	dprintk("RPC:       %s: rep %p opcode 'recv', length %u: success\n",
		__func__, rep, wc->byte_len);

	rep->rr_len = wc->byte_len;
	ib_dma_sync_single_for_cpu(rep->rr_device,
				   rdmab_addr(rep->rr_rdmabuf),
				   rep->rr_len, DMA_FROM_DEVICE);

	rpcrdma_update_granted_credits(rep);

out_schedule:
	queue_work(rpcrdma_receive_wq, &rep->rr_work);
	return;

out_fail:
	if (wc->status != IB_WC_WR_FLUSH_ERR)
		pr_err("rpcrdma: Recv: %s (%u/0x%x)\n",
		       ib_wc_status_msg(wc->status),
		       wc->status, wc->vendor_err);
	rep->rr_len = RPCRDMA_BAD_LEN;
	goto out_schedule;
}

static int
rpcrdma_conn_upcall(struct rdma_cm_id *id, struct rdma_cm_event *event)
{
	struct rpcrdma_xprt *xprt = id->context;
	struct rpcrdma_ia *ia = &xprt->rx_ia;
	struct rpcrdma_ep *ep = &xprt->rx_ep;
#if IS_ENABLED(CONFIG_SUNRPC_DEBUG)
	struct sockaddr *sap = (struct sockaddr *)&ep->rep_remote_addr;
#endif
	struct ib_qp_attr *attr = &ia->ri_qp_attr;
	struct ib_qp_init_attr *iattr = &ia->ri_qp_init_attr;
	int connstate = 0;

	switch (event->event) {
	case RDMA_CM_EVENT_ADDR_RESOLVED:
	case RDMA_CM_EVENT_ROUTE_RESOLVED:
		ia->ri_async_rc = 0;
		complete(&ia->ri_done);
		break;
	case RDMA_CM_EVENT_ADDR_ERROR:
		ia->ri_async_rc = -EHOSTUNREACH;
		dprintk("RPC:       %s: CM address resolution error, ep 0x%p\n",
			__func__, ep);
		complete(&ia->ri_done);
		break;
	case RDMA_CM_EVENT_ROUTE_ERROR:
		ia->ri_async_rc = -ENETUNREACH;
		dprintk("RPC:       %s: CM route resolution error, ep 0x%p\n",
			__func__, ep);
		complete(&ia->ri_done);
		break;
	case RDMA_CM_EVENT_ESTABLISHED:
		connstate = 1;
		ib_query_qp(ia->ri_id->qp, attr,
			    IB_QP_MAX_QP_RD_ATOMIC | IB_QP_MAX_DEST_RD_ATOMIC,
			    iattr);
		dprintk("RPC:       %s: %d responder resources"
			" (%d initiator)\n",
			__func__, attr->max_dest_rd_atomic,
			attr->max_rd_atomic);
		goto connected;
	case RDMA_CM_EVENT_CONNECT_ERROR:
		connstate = -ENOTCONN;
		goto connected;
	case RDMA_CM_EVENT_UNREACHABLE:
		connstate = -ENETDOWN;
		goto connected;
	case RDMA_CM_EVENT_REJECTED:
		connstate = -ECONNREFUSED;
		goto connected;
	case RDMA_CM_EVENT_DISCONNECTED:
		connstate = -ECONNABORTED;
		goto connected;
	case RDMA_CM_EVENT_DEVICE_REMOVAL:
		connstate = -ENODEV;
connected:
		dprintk("RPC:       %s: %sconnected\n",
					__func__, connstate > 0 ? "" : "dis");
		atomic_set(&xprt->rx_buf.rb_credits, 1);
		ep->rep_connected = connstate;
		rpcrdma_conn_func(ep);
		wake_up_all(&ep->rep_connect_wait);
		/*FALLTHROUGH*/
	default:
		dprintk("RPC:       %s: %pIS:%u (ep 0x%p): %s\n",
			__func__, sap, rpc_get_port(sap), ep,
			rdma_event_msg(event->event));
		break;
	}

#if IS_ENABLED(CONFIG_SUNRPC_DEBUG)
	if (connstate == 1) {
		int ird = attr->max_dest_rd_atomic;
		int tird = ep->rep_remote_cma.responder_resources;

		pr_info("rpcrdma: connection to %pIS:%u on %s, memreg '%s', %d credits, %d responders%s\n",
			sap, rpc_get_port(sap),
			ia->ri_device->name,
			ia->ri_ops->ro_displayname,
			xprt->rx_buf.rb_max_requests,
			ird, ird < 4 && ird < tird / 2 ? " (low!)" : "");
	} else if (connstate < 0) {
		pr_info("rpcrdma: connection to %pIS:%u closed (%d)\n",
			sap, rpc_get_port(sap), connstate);
	}
#endif

	return 0;
}

static void rpcrdma_destroy_id(struct rdma_cm_id *id)
{
	if (id) {
		module_put(id->device->owner);
		rdma_destroy_id(id);
	}
}

static struct rdma_cm_id *
rpcrdma_create_id(struct rpcrdma_xprt *xprt,
			struct rpcrdma_ia *ia, struct sockaddr *addr)
{
	struct rdma_cm_id *id;
	int rc;

	init_completion(&ia->ri_done);

	id = rdma_create_id(&init_net, rpcrdma_conn_upcall, xprt, RDMA_PS_TCP,
			    IB_QPT_RC);
	if (IS_ERR(id)) {
		rc = PTR_ERR(id);
		dprintk("RPC:       %s: rdma_create_id() failed %i\n",
			__func__, rc);
		return id;
	}

	ia->ri_async_rc = -ETIMEDOUT;
	rc = rdma_resolve_addr(id, NULL, addr, RDMA_RESOLVE_TIMEOUT);
	if (rc) {
		dprintk("RPC:       %s: rdma_resolve_addr() failed %i\n",
			__func__, rc);
		goto out;
	}
	wait_for_completion_interruptible_timeout(&ia->ri_done,
				msecs_to_jiffies(RDMA_RESOLVE_TIMEOUT) + 1);

	/* FIXME:
	 * Until xprtrdma supports DEVICE_REMOVAL, the provider must
	 * be pinned while there are active NFS/RDMA mounts to prevent
	 * hangs and crashes at umount time.
	 */
	if (!ia->ri_async_rc && !try_module_get(id->device->owner)) {
		dprintk("RPC:       %s: Failed to get device module\n",
			__func__);
		ia->ri_async_rc = -ENODEV;
	}
	rc = ia->ri_async_rc;
	if (rc)
		goto out;

	ia->ri_async_rc = -ETIMEDOUT;
	rc = rdma_resolve_route(id, RDMA_RESOLVE_TIMEOUT);
	if (rc) {
		dprintk("RPC:       %s: rdma_resolve_route() failed %i\n",
			__func__, rc);
		goto put;
	}
	wait_for_completion_interruptible_timeout(&ia->ri_done,
				msecs_to_jiffies(RDMA_RESOLVE_TIMEOUT) + 1);
	rc = ia->ri_async_rc;
	if (rc)
		goto put;

	return id;
put:
	module_put(id->device->owner);
out:
	rdma_destroy_id(id);
	return ERR_PTR(rc);
}

/*
 * Exported functions.
 */

/*
 * Open and initialize an Interface Adapter.
 *  o initializes fields of struct rpcrdma_ia, including
 *    interface and provider attributes and protection zone.
 */
int
rpcrdma_ia_open(struct rpcrdma_xprt *xprt, struct sockaddr *addr, int memreg)
{
	struct rpcrdma_ia *ia = &xprt->rx_ia;
	int rc;

	ia->ri_dma_mr = NULL;

	ia->ri_id = rpcrdma_create_id(xprt, ia, addr);
	if (IS_ERR(ia->ri_id)) {
		rc = PTR_ERR(ia->ri_id);
		goto out1;
	}
	ia->ri_device = ia->ri_id->device;

	ia->ri_pd = ib_alloc_pd(ia->ri_device);
	if (IS_ERR(ia->ri_pd)) {
		rc = PTR_ERR(ia->ri_pd);
		dprintk("RPC:       %s: ib_alloc_pd() failed %i\n",
			__func__, rc);
		goto out2;
	}

	if (memreg == RPCRDMA_FRMR) {
		if (!(ia->ri_device->attrs.device_cap_flags &
				IB_DEVICE_MEM_MGT_EXTENSIONS) ||
		    (ia->ri_device->attrs.max_fast_reg_page_list_len == 0)) {
			dprintk("RPC:       %s: FRMR registration "
				"not supported by HCA\n", __func__);
			memreg = RPCRDMA_MTHCAFMR;
		}
	}
	if (memreg == RPCRDMA_MTHCAFMR) {
		if (!ia->ri_device->alloc_fmr) {
			dprintk("RPC:       %s: MTHCAFMR registration "
				"not supported by HCA\n", __func__);
			rc = -EINVAL;
			goto out3;
		}
	}

	switch (memreg) {
	case RPCRDMA_FRMR:
		ia->ri_ops = &rpcrdma_frwr_memreg_ops;
		break;
	case RPCRDMA_ALLPHYSICAL:
		ia->ri_ops = &rpcrdma_physical_memreg_ops;
		break;
	case RPCRDMA_MTHCAFMR:
		ia->ri_ops = &rpcrdma_fmr_memreg_ops;
		break;
	default:
		printk(KERN_ERR "RPC: Unsupported memory "
				"registration mode: %d\n", memreg);
		rc = -ENOMEM;
		goto out3;
	}
	dprintk("RPC:       %s: memory registration strategy is '%s'\n",
		__func__, ia->ri_ops->ro_displayname);

	rwlock_init(&ia->ri_qplock);
	return 0;

out3:
	ib_dealloc_pd(ia->ri_pd);
	ia->ri_pd = NULL;
out2:
	rpcrdma_destroy_id(ia->ri_id);
	ia->ri_id = NULL;
out1:
	return rc;
}

/*
 * Clean up/close an IA.
 *   o if event handles and PD have been initialized, free them.
 *   o close the IA
 */
void
rpcrdma_ia_close(struct rpcrdma_ia *ia)
{
	dprintk("RPC:       %s: entering\n", __func__);
	if (ia->ri_id != NULL && !IS_ERR(ia->ri_id)) {
		if (ia->ri_id->qp)
			rdma_destroy_qp(ia->ri_id);
		rpcrdma_destroy_id(ia->ri_id);
		ia->ri_id = NULL;
	}

	/* If the pd is still busy, xprtrdma missed freeing a resource */
	if (ia->ri_pd && !IS_ERR(ia->ri_pd))
		ib_dealloc_pd(ia->ri_pd);
}

/*
 * Create unconnected endpoint.
 */
int
rpcrdma_ep_create(struct rpcrdma_ep *ep, struct rpcrdma_ia *ia,
				struct rpcrdma_create_data_internal *cdata)
{
	struct ib_cq *sendcq, *recvcq;
	unsigned int max_qp_wr;
	int rc;

	if (ia->ri_device->attrs.max_sge < RPCRDMA_MAX_IOVS) {
		dprintk("RPC:       %s: insufficient sge's available\n",
			__func__);
		return -ENOMEM;
	}

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
	ep->rep_attr.cap.max_send_sge = RPCRDMA_MAX_IOVS;
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
	ep->rep_cqinit = ep->rep_attr.cap.max_send_wr/2 - 1;
	if (ep->rep_cqinit <= 2)
		ep->rep_cqinit = 0;	/* always signal? */
	INIT_CQCOUNT(ep);
	init_waitqueue_head(&ep->rep_connect_wait);
	INIT_DELAYED_WORK(&ep->rep_connect_worker, rpcrdma_connect_worker);

	sendcq = ib_alloc_cq(ia->ri_device, NULL,
			     ep->rep_attr.cap.max_send_wr + 1,
			     0, IB_POLL_SOFTIRQ);
	if (IS_ERR(sendcq)) {
		rc = PTR_ERR(sendcq);
		dprintk("RPC:       %s: failed to create send CQ: %i\n",
			__func__, rc);
		goto out1;
	}

	recvcq = ib_alloc_cq(ia->ri_device, NULL,
			     ep->rep_attr.cap.max_recv_wr + 1,
			     0, IB_POLL_SOFTIRQ);
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

	/* RPC/RDMA does not use private data */
	ep->rep_remote_cma.private_data = NULL;
	ep->rep_remote_cma.private_data_len = 0;

	/* Client offers RDMA Read but does not initiate */
	ep->rep_remote_cma.initiator_depth = 0;
	if (ia->ri_device->attrs.max_qp_rd_atom > 32)	/* arbitrary but <= 255 */
		ep->rep_remote_cma.responder_resources = 32;
	else
		ep->rep_remote_cma.responder_resources =
						ia->ri_device->attrs.max_qp_rd_atom;

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
	if (ia->ri_dma_mr)
		ib_dereg_mr(ia->ri_dma_mr);
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
	int rc;

	dprintk("RPC:       %s: entering, connected is %d\n",
		__func__, ep->rep_connected);

	cancel_delayed_work_sync(&ep->rep_connect_worker);

	if (ia->ri_id->qp) {
		rpcrdma_ep_disconnect(ep, ia);
		rdma_destroy_qp(ia->ri_id);
		ia->ri_id->qp = NULL;
	}

	ib_free_cq(ep->rep_attr.recv_cq);
	ib_free_cq(ep->rep_attr.send_cq);

	if (ia->ri_dma_mr) {
		rc = ib_dereg_mr(ia->ri_dma_mr);
		dprintk("RPC:       %s: ib_dereg_mr returned %i\n",
			__func__, rc);
	}
}

/*
 * Connect unconnected endpoint.
 */
int
rpcrdma_ep_connect(struct rpcrdma_ep *ep, struct rpcrdma_ia *ia)
{
	struct rdma_cm_id *id, *old;
	int rc = 0;
	int retry_count = 0;

	if (ep->rep_connected != 0) {
		struct rpcrdma_xprt *xprt;
retry:
		dprintk("RPC:       %s: reconnecting...\n", __func__);

		rpcrdma_ep_disconnect(ep, ia);

		xprt = container_of(ia, struct rpcrdma_xprt, rx_ia);
		id = rpcrdma_create_id(xprt, ia,
				(struct sockaddr *)&xprt->rx_data.addr);
		if (IS_ERR(id)) {
			rc = -EHOSTUNREACH;
			goto out;
		}
		/* TEMP TEMP TEMP - fail if new device:
		 * Deregister/remarshal *all* requests!
		 * Close and recreate adapter, pd, etc!
		 * Re-determine all attributes still sane!
		 * More stuff I haven't thought of!
		 * Rrrgh!
		 */
		if (ia->ri_device != id->device) {
			printk("RPC:       %s: can't reconnect on "
				"different device!\n", __func__);
			rpcrdma_destroy_id(id);
			rc = -ENETUNREACH;
			goto out;
		}
		/* END TEMP */
		rc = rdma_create_qp(id, ia->ri_pd, &ep->rep_attr);
		if (rc) {
			dprintk("RPC:       %s: rdma_create_qp failed %i\n",
				__func__, rc);
			rpcrdma_destroy_id(id);
			rc = -ENETUNREACH;
			goto out;
		}

		write_lock(&ia->ri_qplock);
		old = ia->ri_id;
		ia->ri_id = id;
		write_unlock(&ia->ri_qplock);

		rdma_destroy_qp(old);
		rpcrdma_destroy_id(old);
	} else {
		dprintk("RPC:       %s: connecting...\n", __func__);
		rc = rdma_create_qp(ia->ri_id, ia->ri_pd, &ep->rep_attr);
		if (rc) {
			dprintk("RPC:       %s: rdma_create_qp failed %i\n",
				__func__, rc);
			/* do not update ep->rep_connected */
			return -ENETUNREACH;
		}
	}

	ep->rep_connected = 0;

	rc = rdma_connect(ia->ri_id, &ep->rep_remote_cma);
	if (rc) {
		dprintk("RPC:       %s: rdma_connect() failed with %i\n",
				__func__, rc);
		goto out;
	}

	wait_event_interruptible(ep->rep_connect_wait, ep->rep_connected != 0);

	/*
	 * Check state. A non-peer reject indicates no listener
	 * (ECONNREFUSED), which may be a transient state. All
	 * others indicate a transport condition which has already
	 * undergone a best-effort.
	 */
	if (ep->rep_connected == -ECONNREFUSED &&
	    ++retry_count <= RDMA_CONNECT_RETRY_MAX) {
		dprintk("RPC:       %s: non-peer_reject, retry\n", __func__);
		goto retry;
	}
	if (ep->rep_connected <= 0) {
		/* Sometimes, the only way to reliably connect to remote
		 * CMs is to use same nonzero values for ORD and IRD. */
		if (retry_count++ <= RDMA_CONNECT_RETRY_MAX + 1 &&
		    (ep->rep_remote_cma.responder_resources == 0 ||
		     ep->rep_remote_cma.initiator_depth !=
				ep->rep_remote_cma.responder_resources)) {
			if (ep->rep_remote_cma.responder_resources == 0)
				ep->rep_remote_cma.responder_resources = 1;
			ep->rep_remote_cma.initiator_depth =
				ep->rep_remote_cma.responder_resources;
			goto retry;
		}
		rc = ep->rep_connected;
	} else {
		struct rpcrdma_xprt *r_xprt;
		unsigned int extras;

		dprintk("RPC:       %s: connected\n", __func__);

		r_xprt = container_of(ia, struct rpcrdma_xprt, rx_ia);
		extras = r_xprt->rx_buf.rb_bc_srv_max_requests;

		if (extras) {
			rc = rpcrdma_ep_post_extra_recv(r_xprt, extras);
			if (rc) {
				pr_warn("%s: rpcrdma_ep_post_extra_recv: %i\n",
					__func__, rc);
				rc = 0;
			}
		}
	}

out:
	if (rc)
		ep->rep_connected = rc;
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
	if (!rc) {
		/* returns without wait if not connected */
		wait_event_interruptible(ep->rep_connect_wait,
							ep->rep_connected != 1);
		dprintk("RPC:       %s: after wait, %sconnected\n", __func__,
			(ep->rep_connected == 1) ? "still " : "dis");
	} else {
		dprintk("RPC:       %s: rdma_disconnect %i\n", __func__, rc);
		ep->rep_connected = rc;
	}

	ib_drain_qp(ia->ri_id->qp);
}

struct rpcrdma_req *
rpcrdma_create_req(struct rpcrdma_xprt *r_xprt)
{
	struct rpcrdma_buffer *buffer = &r_xprt->rx_buf;
	struct rpcrdma_req *req;

	req = kzalloc(sizeof(*req), GFP_KERNEL);
	if (req == NULL)
		return ERR_PTR(-ENOMEM);

	INIT_LIST_HEAD(&req->rl_free);
	spin_lock(&buffer->rb_reqslock);
	list_add(&req->rl_all, &buffer->rb_allreqs);
	spin_unlock(&buffer->rb_reqslock);
	req->rl_cqe.done = rpcrdma_wc_send;
	req->rl_buffer = &r_xprt->rx_buf;
	return req;
}

struct rpcrdma_rep *
rpcrdma_create_rep(struct rpcrdma_xprt *r_xprt)
{
	struct rpcrdma_create_data_internal *cdata = &r_xprt->rx_data;
	struct rpcrdma_ia *ia = &r_xprt->rx_ia;
	struct rpcrdma_rep *rep;
	int rc;

	rc = -ENOMEM;
	rep = kzalloc(sizeof(*rep), GFP_KERNEL);
	if (rep == NULL)
		goto out;

	rep->rr_rdmabuf = rpcrdma_alloc_regbuf(ia, cdata->inline_rsize,
					       GFP_KERNEL);
	if (IS_ERR(rep->rr_rdmabuf)) {
		rc = PTR_ERR(rep->rr_rdmabuf);
		goto out_free;
	}

	rep->rr_device = ia->ri_device;
	rep->rr_cqe.done = rpcrdma_receive_wc;
	rep->rr_rxprt = r_xprt;
	INIT_WORK(&rep->rr_work, rpcrdma_receive_worker);
	return rep;

out_free:
	kfree(rep);
out:
	return ERR_PTR(rc);
}

int
rpcrdma_buffer_create(struct rpcrdma_xprt *r_xprt)
{
	struct rpcrdma_buffer *buf = &r_xprt->rx_buf;
	struct rpcrdma_ia *ia = &r_xprt->rx_ia;
	int i, rc;

	buf->rb_max_requests = r_xprt->rx_data.max_requests;
	buf->rb_bc_srv_max_requests = 0;
	spin_lock_init(&buf->rb_lock);
	atomic_set(&buf->rb_credits, 1);

	rc = ia->ri_ops->ro_init(r_xprt);
	if (rc)
		goto out;

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
		req->rl_backchannel = false;
		list_add(&req->rl_free, &buf->rb_send_bufs);
	}

	INIT_LIST_HEAD(&buf->rb_recv_bufs);
	for (i = 0; i < buf->rb_max_requests + 2; i++) {
		struct rpcrdma_rep *rep;

		rep = rpcrdma_create_rep(r_xprt);
		if (IS_ERR(rep)) {
			dprintk("RPC:       %s: reply buffer %d alloc failed\n",
				__func__, i);
			rc = PTR_ERR(rep);
			goto out;
		}
		list_add(&rep->rr_list, &buf->rb_recv_bufs);
	}

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
			       struct rpcrdma_req, rl_free);
	list_del(&req->rl_free);
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
rpcrdma_destroy_rep(struct rpcrdma_ia *ia, struct rpcrdma_rep *rep)
{
	rpcrdma_free_regbuf(ia, rep->rr_rdmabuf);
	kfree(rep);
}

void
rpcrdma_destroy_req(struct rpcrdma_ia *ia, struct rpcrdma_req *req)
{
	rpcrdma_free_regbuf(ia, req->rl_sendbuf);
	rpcrdma_free_regbuf(ia, req->rl_rdmabuf);
	kfree(req);
}

void
rpcrdma_buffer_destroy(struct rpcrdma_buffer *buf)
{
	struct rpcrdma_ia *ia = rdmab_to_ia(buf);

	while (!list_empty(&buf->rb_recv_bufs)) {
		struct rpcrdma_rep *rep;

		rep = rpcrdma_buffer_get_rep_locked(buf);
		rpcrdma_destroy_rep(ia, rep);
	}

	spin_lock(&buf->rb_reqslock);
	while (!list_empty(&buf->rb_allreqs)) {
		struct rpcrdma_req *req;

		req = list_first_entry(&buf->rb_allreqs,
				       struct rpcrdma_req, rl_all);
		list_del(&req->rl_all);

		spin_unlock(&buf->rb_reqslock);
		rpcrdma_destroy_req(ia, req);
		spin_lock(&buf->rb_reqslock);
	}
	spin_unlock(&buf->rb_reqslock);

	ia->ri_ops->ro_destroy(buf);
}

struct rpcrdma_mw *
rpcrdma_get_mw(struct rpcrdma_xprt *r_xprt)
{
	struct rpcrdma_buffer *buf = &r_xprt->rx_buf;
	struct rpcrdma_mw *mw = NULL;

	spin_lock(&buf->rb_mwlock);
	if (!list_empty(&buf->rb_mws)) {
		mw = list_first_entry(&buf->rb_mws,
				      struct rpcrdma_mw, mw_list);
		list_del_init(&mw->mw_list);
	}
	spin_unlock(&buf->rb_mwlock);

	if (!mw)
		pr_err("RPC:       %s: no MWs available\n", __func__);
	return mw;
}

void
rpcrdma_put_mw(struct rpcrdma_xprt *r_xprt, struct rpcrdma_mw *mw)
{
	struct rpcrdma_buffer *buf = &r_xprt->rx_buf;

	spin_lock(&buf->rb_mwlock);
	list_add_tail(&mw->mw_list, &buf->rb_mws);
	spin_unlock(&buf->rb_mwlock);
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
	req = rpcrdma_buffer_get_req_locked(buffers);
	if (list_empty(&buffers->rb_recv_bufs))
		goto out_repbuf;
	req->rl_reply = rpcrdma_buffer_get_rep_locked(buffers);
	spin_unlock(&buffers->rb_lock);
	return req;

out_reqbuf:
	spin_unlock(&buffers->rb_lock);
	pr_warn("RPC:       %s: out of request buffers\n", __func__);
	return NULL;
out_repbuf:
	spin_unlock(&buffers->rb_lock);
	pr_warn("RPC:       %s: out of reply buffers\n", __func__);
	req->rl_reply = NULL;
	return req;
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

	req->rl_niovs = 0;
	req->rl_reply = NULL;

	spin_lock(&buffers->rb_lock);
	list_add_tail(&req->rl_free, &buffers->rb_send_bufs);
	if (rep)
		list_add_tail(&rep->rr_list, &buffers->rb_recv_bufs);
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
	if (!list_empty(&buffers->rb_recv_bufs))
		req->rl_reply = rpcrdma_buffer_get_rep_locked(buffers);
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
	list_add_tail(&rep->rr_list, &buffers->rb_recv_bufs);
	spin_unlock(&buffers->rb_lock);
}

/*
 * Wrappers for internal-use kmalloc memory registration, used by buffer code.
 */

void
rpcrdma_mapping_error(struct rpcrdma_mr_seg *seg)
{
	dprintk("RPC:       map_one: offset %p iova %llx len %zu\n",
		seg->mr_offset,
		(unsigned long long)seg->mr_dma, seg->mr_dmalen);
}

/**
 * rpcrdma_alloc_regbuf - kmalloc and register memory for SEND/RECV buffers
 * @ia: controlling rpcrdma_ia
 * @size: size of buffer to be allocated, in bytes
 * @flags: GFP flags
 *
 * Returns pointer to private header of an area of internally
 * registered memory, or an ERR_PTR. The registered buffer follows
 * the end of the private header.
 *
 * xprtrdma uses a regbuf for posting an outgoing RDMA SEND, or for
 * receiving the payload of RDMA RECV operations. regbufs are not
 * used for RDMA READ/WRITE operations, thus are registered only for
 * LOCAL access.
 */
struct rpcrdma_regbuf *
rpcrdma_alloc_regbuf(struct rpcrdma_ia *ia, size_t size, gfp_t flags)
{
	struct rpcrdma_regbuf *rb;
	struct ib_sge *iov;

	rb = kmalloc(sizeof(*rb) + size, flags);
	if (rb == NULL)
		goto out;

	iov = &rb->rg_iov;
	iov->addr = ib_dma_map_single(ia->ri_device,
				      (void *)rb->rg_base, size,
				      DMA_BIDIRECTIONAL);
	if (ib_dma_mapping_error(ia->ri_device, iov->addr))
		goto out_free;

	iov->length = size;
	iov->lkey = ia->ri_pd->local_dma_lkey;
	rb->rg_size = size;
	rb->rg_owner = NULL;
	return rb;

out_free:
	kfree(rb);
out:
	return ERR_PTR(-ENOMEM);
}

/**
 * rpcrdma_free_regbuf - deregister and free registered buffer
 * @ia: controlling rpcrdma_ia
 * @rb: regbuf to be deregistered and freed
 */
void
rpcrdma_free_regbuf(struct rpcrdma_ia *ia, struct rpcrdma_regbuf *rb)
{
	struct ib_sge *iov;

	if (!rb)
		return;

	iov = &rb->rg_iov;
	ib_dma_unmap_single(ia->ri_device,
			    iov->addr, iov->length, DMA_BIDIRECTIONAL);
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
	struct ib_device *device = ia->ri_device;
	struct ib_send_wr send_wr, *send_wr_fail;
	struct rpcrdma_rep *rep = req->rl_reply;
	struct ib_sge *iov = req->rl_send_iov;
	int i, rc;

	if (rep) {
		rc = rpcrdma_ep_post_recv(ia, ep, rep);
		if (rc)
			goto out;
		req->rl_reply = NULL;
	}

	send_wr.next = NULL;
	send_wr.wr_cqe = &req->rl_cqe;
	send_wr.sg_list = iov;
	send_wr.num_sge = req->rl_niovs;
	send_wr.opcode = IB_WR_SEND;

	for (i = 0; i < send_wr.num_sge; i++)
		ib_dma_sync_single_for_device(device, iov[i].addr,
					      iov[i].length, DMA_TO_DEVICE);
	dprintk("RPC:       %s: posting %d s/g entries\n",
		__func__, send_wr.num_sge);

	if (DECR_CQCOUNT(ep) > 0)
		send_wr.send_flags = 0;
	else { /* Provider must take a send completion every now and then */
		INIT_CQCOUNT(ep);
		send_wr.send_flags = IB_SEND_SIGNALED;
	}

	rc = ib_post_send(ia->ri_id->qp, &send_wr, &send_wr_fail);
	if (rc)
		dprintk("RPC:       %s: ib_post_send returned %i\n", __func__,
			rc);
out:
	return rc;
}

/*
 * (Re)post a receive buffer.
 */
int
rpcrdma_ep_post_recv(struct rpcrdma_ia *ia,
		     struct rpcrdma_ep *ep,
		     struct rpcrdma_rep *rep)
{
	struct ib_recv_wr recv_wr, *recv_wr_fail;
	int rc;

	recv_wr.next = NULL;
	recv_wr.wr_cqe = &rep->rr_cqe;
	recv_wr.sg_list = &rep->rr_rdmabuf->rg_iov;
	recv_wr.num_sge = 1;

	ib_dma_sync_single_for_cpu(ia->ri_device,
				   rdmab_addr(rep->rr_rdmabuf),
				   rdmab_length(rep->rr_rdmabuf),
				   DMA_BIDIRECTIONAL);

	rc = ib_post_recv(ia->ri_id->qp, &recv_wr, &recv_wr_fail);

	if (rc)
		dprintk("RPC:       %s: ib_post_recv returned %i\n", __func__,
			rc);
	return rc;
}

/**
 * rpcrdma_ep_post_extra_recv - Post buffers for incoming backchannel requests
 * @r_xprt: transport associated with these backchannel resources
 * @min_reqs: minimum number of incoming requests expected
 *
 * Returns zero if all requested buffers were posted, or a negative errno.
 */
int
rpcrdma_ep_post_extra_recv(struct rpcrdma_xprt *r_xprt, unsigned int count)
{
	struct rpcrdma_buffer *buffers = &r_xprt->rx_buf;
	struct rpcrdma_ia *ia = &r_xprt->rx_ia;
	struct rpcrdma_ep *ep = &r_xprt->rx_ep;
	struct rpcrdma_rep *rep;
	int rc;

	while (count--) {
		spin_lock(&buffers->rb_lock);
		if (list_empty(&buffers->rb_recv_bufs))
			goto out_reqbuf;
		rep = rpcrdma_buffer_get_rep_locked(buffers);
		spin_unlock(&buffers->rb_lock);

		rc = rpcrdma_ep_post_recv(ia, ep, rep);
		if (rc)
			goto out_rc;
	}

	return 0;

out_reqbuf:
	spin_unlock(&buffers->rb_lock);
	pr_warn("%s: no extra receive buffers\n", __func__);
	return -ENOMEM;

out_rc:
	rpcrdma_recv_buffer_put(rep);
	return rc;
}
