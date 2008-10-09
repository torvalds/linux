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

#include <linux/pci.h>	/* for Tavor hack below */

#include "xprt_rdma.h"

/*
 * Globals/Macros
 */

#ifdef RPC_DEBUG
# define RPCDBG_FACILITY	RPCDBG_TRANS
#endif

/*
 * internal functions
 */

/*
 * handle replies in tasklet context, using a single, global list
 * rdma tasklet function -- just turn around and call the func
 * for all replies on the list
 */

static DEFINE_SPINLOCK(rpcrdma_tk_lock_g);
static LIST_HEAD(rpcrdma_tasklets_g);

static void
rpcrdma_run_tasklet(unsigned long data)
{
	struct rpcrdma_rep *rep;
	void (*func)(struct rpcrdma_rep *);
	unsigned long flags;

	data = data;
	spin_lock_irqsave(&rpcrdma_tk_lock_g, flags);
	while (!list_empty(&rpcrdma_tasklets_g)) {
		rep = list_entry(rpcrdma_tasklets_g.next,
				 struct rpcrdma_rep, rr_list);
		list_del(&rep->rr_list);
		func = rep->rr_func;
		rep->rr_func = NULL;
		spin_unlock_irqrestore(&rpcrdma_tk_lock_g, flags);

		if (func)
			func(rep);
		else
			rpcrdma_recv_buffer_put(rep);

		spin_lock_irqsave(&rpcrdma_tk_lock_g, flags);
	}
	spin_unlock_irqrestore(&rpcrdma_tk_lock_g, flags);
}

static DECLARE_TASKLET(rpcrdma_tasklet_g, rpcrdma_run_tasklet, 0UL);

static inline void
rpcrdma_schedule_tasklet(struct rpcrdma_rep *rep)
{
	unsigned long flags;

	spin_lock_irqsave(&rpcrdma_tk_lock_g, flags);
	list_add_tail(&rep->rr_list, &rpcrdma_tasklets_g);
	spin_unlock_irqrestore(&rpcrdma_tk_lock_g, flags);
	tasklet_schedule(&rpcrdma_tasklet_g);
}

static void
rpcrdma_qp_async_error_upcall(struct ib_event *event, void *context)
{
	struct rpcrdma_ep *ep = context;

	dprintk("RPC:       %s: QP error %X on device %s ep %p\n",
		__func__, event->event, event->device->name, context);
	if (ep->rep_connected == 1) {
		ep->rep_connected = -EIO;
		ep->rep_func(ep);
		wake_up_all(&ep->rep_connect_wait);
	}
}

static void
rpcrdma_cq_async_error_upcall(struct ib_event *event, void *context)
{
	struct rpcrdma_ep *ep = context;

	dprintk("RPC:       %s: CQ error %X on device %s ep %p\n",
		__func__, event->event, event->device->name, context);
	if (ep->rep_connected == 1) {
		ep->rep_connected = -EIO;
		ep->rep_func(ep);
		wake_up_all(&ep->rep_connect_wait);
	}
}

static inline
void rpcrdma_event_process(struct ib_wc *wc)
{
	struct rpcrdma_rep *rep =
			(struct rpcrdma_rep *)(unsigned long) wc->wr_id;

	dprintk("RPC:       %s: event rep %p status %X opcode %X length %u\n",
		__func__, rep, wc->status, wc->opcode, wc->byte_len);

	if (!rep) /* send or bind completion that we don't care about */
		return;

	if (IB_WC_SUCCESS != wc->status) {
		dprintk("RPC:       %s: %s WC status %X, connection lost\n",
			__func__, (wc->opcode & IB_WC_RECV) ? "recv" : "send",
			 wc->status);
		rep->rr_len = ~0U;
		rpcrdma_schedule_tasklet(rep);
		return;
	}

	switch (wc->opcode) {
	case IB_WC_RECV:
		rep->rr_len = wc->byte_len;
		ib_dma_sync_single_for_cpu(
			rdmab_to_ia(rep->rr_buffer)->ri_id->device,
			rep->rr_iov.addr, rep->rr_len, DMA_FROM_DEVICE);
		/* Keep (only) the most recent credits, after check validity */
		if (rep->rr_len >= 16) {
			struct rpcrdma_msg *p =
					(struct rpcrdma_msg *) rep->rr_base;
			unsigned int credits = ntohl(p->rm_credit);
			if (credits == 0) {
				dprintk("RPC:       %s: server"
					" dropped credits to 0!\n", __func__);
				/* don't deadlock */
				credits = 1;
			} else if (credits > rep->rr_buffer->rb_max_requests) {
				dprintk("RPC:       %s: server"
					" over-crediting: %d (%d)\n",
					__func__, credits,
					rep->rr_buffer->rb_max_requests);
				credits = rep->rr_buffer->rb_max_requests;
			}
			atomic_set(&rep->rr_buffer->rb_credits, credits);
		}
		/* fall through */
	case IB_WC_BIND_MW:
		rpcrdma_schedule_tasklet(rep);
		break;
	default:
		dprintk("RPC:       %s: unexpected WC event %X\n",
			__func__, wc->opcode);
		break;
	}
}

static inline int
rpcrdma_cq_poll(struct ib_cq *cq)
{
	struct ib_wc wc;
	int rc;

	for (;;) {
		rc = ib_poll_cq(cq, 1, &wc);
		if (rc < 0) {
			dprintk("RPC:       %s: ib_poll_cq failed %i\n",
				__func__, rc);
			return rc;
		}
		if (rc == 0)
			break;

		rpcrdma_event_process(&wc);
	}

	return 0;
}

/*
 * rpcrdma_cq_event_upcall
 *
 * This upcall handles recv, send, bind and unbind events.
 * It is reentrant but processes single events in order to maintain
 * ordering of receives to keep server credits.
 *
 * It is the responsibility of the scheduled tasklet to return
 * recv buffers to the pool. NOTE: this affects synchronization of
 * connection shutdown. That is, the structures required for
 * the completion of the reply handler must remain intact until
 * all memory has been reclaimed.
 *
 * Note that send events are suppressed and do not result in an upcall.
 */
static void
rpcrdma_cq_event_upcall(struct ib_cq *cq, void *context)
{
	int rc;

	rc = rpcrdma_cq_poll(cq);
	if (rc)
		return;

	rc = ib_req_notify_cq(cq, IB_CQ_NEXT_COMP);
	if (rc) {
		dprintk("RPC:       %s: ib_req_notify_cq failed %i\n",
			__func__, rc);
		return;
	}

	rpcrdma_cq_poll(cq);
}

#ifdef RPC_DEBUG
static const char * const conn[] = {
	"address resolved",
	"address error",
	"route resolved",
	"route error",
	"connect request",
	"connect response",
	"connect error",
	"unreachable",
	"rejected",
	"established",
	"disconnected",
	"device removal"
};
#endif

static int
rpcrdma_conn_upcall(struct rdma_cm_id *id, struct rdma_cm_event *event)
{
	struct rpcrdma_xprt *xprt = id->context;
	struct rpcrdma_ia *ia = &xprt->rx_ia;
	struct rpcrdma_ep *ep = &xprt->rx_ep;
	struct sockaddr_in *addr = (struct sockaddr_in *) &ep->rep_remote_addr;
	struct ib_qp_attr attr;
	struct ib_qp_init_attr iattr;
	int connstate = 0;

	switch (event->event) {
	case RDMA_CM_EVENT_ADDR_RESOLVED:
	case RDMA_CM_EVENT_ROUTE_RESOLVED:
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
		ib_query_qp(ia->ri_id->qp, &attr,
			IB_QP_MAX_QP_RD_ATOMIC | IB_QP_MAX_DEST_RD_ATOMIC,
			&iattr);
		dprintk("RPC:       %s: %d responder resources"
			" (%d initiator)\n",
			__func__, attr.max_dest_rd_atomic, attr.max_rd_atomic);
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
		dprintk("RPC:       %s: %s: %u.%u.%u.%u:%u"
			" (ep 0x%p event 0x%x)\n",
			__func__,
			(event->event <= 11) ? conn[event->event] :
						"unknown connection error",
			NIPQUAD(addr->sin_addr.s_addr),
			ntohs(addr->sin_port),
			ep, event->event);
		atomic_set(&rpcx_to_rdmax(ep->rep_xprt)->rx_buf.rb_credits, 1);
		dprintk("RPC:       %s: %sconnected\n",
					__func__, connstate > 0 ? "" : "dis");
		ep->rep_connected = connstate;
		ep->rep_func(ep);
		wake_up_all(&ep->rep_connect_wait);
		break;
	default:
		ia->ri_async_rc = -EINVAL;
		dprintk("RPC:       %s: unexpected CM event %X\n",
			__func__, event->event);
		complete(&ia->ri_done);
		break;
	}

	return 0;
}

static struct rdma_cm_id *
rpcrdma_create_id(struct rpcrdma_xprt *xprt,
			struct rpcrdma_ia *ia, struct sockaddr *addr)
{
	struct rdma_cm_id *id;
	int rc;

	id = rdma_create_id(rpcrdma_conn_upcall, xprt, RDMA_PS_TCP);
	if (IS_ERR(id)) {
		rc = PTR_ERR(id);
		dprintk("RPC:       %s: rdma_create_id() failed %i\n",
			__func__, rc);
		return id;
	}

	ia->ri_async_rc = 0;
	rc = rdma_resolve_addr(id, NULL, addr, RDMA_RESOLVE_TIMEOUT);
	if (rc) {
		dprintk("RPC:       %s: rdma_resolve_addr() failed %i\n",
			__func__, rc);
		goto out;
	}
	wait_for_completion(&ia->ri_done);
	rc = ia->ri_async_rc;
	if (rc)
		goto out;

	ia->ri_async_rc = 0;
	rc = rdma_resolve_route(id, RDMA_RESOLVE_TIMEOUT);
	if (rc) {
		dprintk("RPC:       %s: rdma_resolve_route() failed %i\n",
			__func__, rc);
		goto out;
	}
	wait_for_completion(&ia->ri_done);
	rc = ia->ri_async_rc;
	if (rc)
		goto out;

	return id;

out:
	rdma_destroy_id(id);
	return ERR_PTR(rc);
}

/*
 * Drain any cq, prior to teardown.
 */
static void
rpcrdma_clean_cq(struct ib_cq *cq)
{
	struct ib_wc wc;
	int count = 0;

	while (1 == ib_poll_cq(cq, 1, &wc))
		++count;

	if (count)
		dprintk("RPC:       %s: flushed %d events (last 0x%x)\n",
			__func__, count, wc.opcode);
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
	int rc, mem_priv;
	struct ib_device_attr devattr;
	struct rpcrdma_ia *ia = &xprt->rx_ia;

	init_completion(&ia->ri_done);

	ia->ri_id = rpcrdma_create_id(xprt, ia, addr);
	if (IS_ERR(ia->ri_id)) {
		rc = PTR_ERR(ia->ri_id);
		goto out1;
	}

	ia->ri_pd = ib_alloc_pd(ia->ri_id->device);
	if (IS_ERR(ia->ri_pd)) {
		rc = PTR_ERR(ia->ri_pd);
		dprintk("RPC:       %s: ib_alloc_pd() failed %i\n",
			__func__, rc);
		goto out2;
	}

	/*
	 * Query the device to determine if the requested memory
	 * registration strategy is supported. If it isn't, set the
	 * strategy to a globally supported model.
	 */
	rc = ib_query_device(ia->ri_id->device, &devattr);
	if (rc) {
		dprintk("RPC:       %s: ib_query_device failed %d\n",
			__func__, rc);
		goto out2;
	}

	if (devattr.device_cap_flags & IB_DEVICE_LOCAL_DMA_LKEY) {
		ia->ri_have_dma_lkey = 1;
		ia->ri_dma_lkey = ia->ri_id->device->local_dma_lkey;
	}

	switch (memreg) {
	case RPCRDMA_MEMWINDOWS:
	case RPCRDMA_MEMWINDOWS_ASYNC:
		if (!(devattr.device_cap_flags & IB_DEVICE_MEM_WINDOW)) {
			dprintk("RPC:       %s: MEMWINDOWS registration "
				"specified but not supported by adapter, "
				"using slower RPCRDMA_REGISTER\n",
				__func__);
			memreg = RPCRDMA_REGISTER;
		}
		break;
	case RPCRDMA_MTHCAFMR:
		if (!ia->ri_id->device->alloc_fmr) {
#if RPCRDMA_PERSISTENT_REGISTRATION
			dprintk("RPC:       %s: MTHCAFMR registration "
				"specified but not supported by adapter, "
				"using riskier RPCRDMA_ALLPHYSICAL\n",
				__func__);
			memreg = RPCRDMA_ALLPHYSICAL;
#else
			dprintk("RPC:       %s: MTHCAFMR registration "
				"specified but not supported by adapter, "
				"using slower RPCRDMA_REGISTER\n",
				__func__);
			memreg = RPCRDMA_REGISTER;
#endif
		}
		break;
	case RPCRDMA_FRMR:
		/* Requires both frmr reg and local dma lkey */
		if ((devattr.device_cap_flags &
		     (IB_DEVICE_MEM_MGT_EXTENSIONS|IB_DEVICE_LOCAL_DMA_LKEY)) !=
		    (IB_DEVICE_MEM_MGT_EXTENSIONS|IB_DEVICE_LOCAL_DMA_LKEY)) {
#if RPCRDMA_PERSISTENT_REGISTRATION
			dprintk("RPC:       %s: FRMR registration "
				"specified but not supported by adapter, "
				"using riskier RPCRDMA_ALLPHYSICAL\n",
				__func__);
			memreg = RPCRDMA_ALLPHYSICAL;
#else
			dprintk("RPC:       %s: FRMR registration "
				"specified but not supported by adapter, "
				"using slower RPCRDMA_REGISTER\n",
				__func__);
			memreg = RPCRDMA_REGISTER;
#endif
		}
		break;
	}

	/*
	 * Optionally obtain an underlying physical identity mapping in
	 * order to do a memory window-based bind. This base registration
	 * is protected from remote access - that is enabled only by binding
	 * for the specific bytes targeted during each RPC operation, and
	 * revoked after the corresponding completion similar to a storage
	 * adapter.
	 */
	switch (memreg) {
	case RPCRDMA_BOUNCEBUFFERS:
	case RPCRDMA_REGISTER:
	case RPCRDMA_FRMR:
		break;
#if RPCRDMA_PERSISTENT_REGISTRATION
	case RPCRDMA_ALLPHYSICAL:
		mem_priv = IB_ACCESS_LOCAL_WRITE |
				IB_ACCESS_REMOTE_WRITE |
				IB_ACCESS_REMOTE_READ;
		goto register_setup;
#endif
	case RPCRDMA_MEMWINDOWS_ASYNC:
	case RPCRDMA_MEMWINDOWS:
		mem_priv = IB_ACCESS_LOCAL_WRITE |
				IB_ACCESS_MW_BIND;
		goto register_setup;
	case RPCRDMA_MTHCAFMR:
		if (ia->ri_have_dma_lkey)
			break;
		mem_priv = IB_ACCESS_LOCAL_WRITE;
	register_setup:
		ia->ri_bind_mem = ib_get_dma_mr(ia->ri_pd, mem_priv);
		if (IS_ERR(ia->ri_bind_mem)) {
			printk(KERN_ALERT "%s: ib_get_dma_mr for "
				"phys register failed with %lX\n\t"
				"Will continue with degraded performance\n",
				__func__, PTR_ERR(ia->ri_bind_mem));
			memreg = RPCRDMA_REGISTER;
			ia->ri_bind_mem = NULL;
		}
		break;
	default:
		printk(KERN_ERR "%s: invalid memory registration mode %d\n",
				__func__, memreg);
		rc = -EINVAL;
		goto out2;
	}
	dprintk("RPC:       %s: memory registration strategy is %d\n",
		__func__, memreg);

	/* Else will do memory reg/dereg for each chunk */
	ia->ri_memreg_strategy = memreg;

	return 0;
out2:
	rdma_destroy_id(ia->ri_id);
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
	int rc;

	dprintk("RPC:       %s: entering\n", __func__);
	if (ia->ri_bind_mem != NULL) {
		rc = ib_dereg_mr(ia->ri_bind_mem);
		dprintk("RPC:       %s: ib_dereg_mr returned %i\n",
			__func__, rc);
	}
	if (ia->ri_id != NULL && !IS_ERR(ia->ri_id)) {
		if (ia->ri_id->qp)
			rdma_destroy_qp(ia->ri_id);
		rdma_destroy_id(ia->ri_id);
		ia->ri_id = NULL;
	}
	if (ia->ri_pd != NULL && !IS_ERR(ia->ri_pd)) {
		rc = ib_dealloc_pd(ia->ri_pd);
		dprintk("RPC:       %s: ib_dealloc_pd returned %i\n",
			__func__, rc);
	}
}

/*
 * Create unconnected endpoint.
 */
int
rpcrdma_ep_create(struct rpcrdma_ep *ep, struct rpcrdma_ia *ia,
				struct rpcrdma_create_data_internal *cdata)
{
	struct ib_device_attr devattr;
	int rc, err;

	rc = ib_query_device(ia->ri_id->device, &devattr);
	if (rc) {
		dprintk("RPC:       %s: ib_query_device failed %d\n",
			__func__, rc);
		return rc;
	}

	/* check provider's send/recv wr limits */
	if (cdata->max_requests > devattr.max_qp_wr)
		cdata->max_requests = devattr.max_qp_wr;

	ep->rep_attr.event_handler = rpcrdma_qp_async_error_upcall;
	ep->rep_attr.qp_context = ep;
	/* send_cq and recv_cq initialized below */
	ep->rep_attr.srq = NULL;
	ep->rep_attr.cap.max_send_wr = cdata->max_requests;
	switch (ia->ri_memreg_strategy) {
	case RPCRDMA_FRMR:
		/* Add room for frmr register and invalidate WRs */
		ep->rep_attr.cap.max_send_wr *= 3;
		if (ep->rep_attr.cap.max_send_wr > devattr.max_qp_wr)
			return -EINVAL;
		break;
	case RPCRDMA_MEMWINDOWS_ASYNC:
	case RPCRDMA_MEMWINDOWS:
		/* Add room for mw_binds+unbinds - overkill! */
		ep->rep_attr.cap.max_send_wr++;
		ep->rep_attr.cap.max_send_wr *= (2 * RPCRDMA_MAX_SEGS);
		if (ep->rep_attr.cap.max_send_wr > devattr.max_qp_wr)
			return -EINVAL;
		break;
	default:
		break;
	}
	ep->rep_attr.cap.max_recv_wr = cdata->max_requests;
	ep->rep_attr.cap.max_send_sge = (cdata->padding ? 4 : 2);
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
	ep->rep_cqinit = ep->rep_attr.cap.max_send_wr/2 /*  - 1*/;
	switch (ia->ri_memreg_strategy) {
	case RPCRDMA_MEMWINDOWS_ASYNC:
	case RPCRDMA_MEMWINDOWS:
		ep->rep_cqinit -= RPCRDMA_MAX_SEGS;
		break;
	default:
		break;
	}
	if (ep->rep_cqinit <= 2)
		ep->rep_cqinit = 0;
	INIT_CQCOUNT(ep);
	ep->rep_ia = ia;
	init_waitqueue_head(&ep->rep_connect_wait);

	/*
	 * Create a single cq for receive dto and mw_bind (only ever
	 * care about unbind, really). Send completions are suppressed.
	 * Use single threaded tasklet upcalls to maintain ordering.
	 */
	ep->rep_cq = ib_create_cq(ia->ri_id->device, rpcrdma_cq_event_upcall,
				  rpcrdma_cq_async_error_upcall, NULL,
				  ep->rep_attr.cap.max_recv_wr +
				  ep->rep_attr.cap.max_send_wr + 1, 0);
	if (IS_ERR(ep->rep_cq)) {
		rc = PTR_ERR(ep->rep_cq);
		dprintk("RPC:       %s: ib_create_cq failed: %i\n",
			__func__, rc);
		goto out1;
	}

	rc = ib_req_notify_cq(ep->rep_cq, IB_CQ_NEXT_COMP);
	if (rc) {
		dprintk("RPC:       %s: ib_req_notify_cq failed: %i\n",
			__func__, rc);
		goto out2;
	}

	ep->rep_attr.send_cq = ep->rep_cq;
	ep->rep_attr.recv_cq = ep->rep_cq;

	/* Initialize cma parameters */

	/* RPC/RDMA does not use private data */
	ep->rep_remote_cma.private_data = NULL;
	ep->rep_remote_cma.private_data_len = 0;

	/* Client offers RDMA Read but does not initiate */
	ep->rep_remote_cma.initiator_depth = 0;
	if (ia->ri_memreg_strategy == RPCRDMA_BOUNCEBUFFERS)
		ep->rep_remote_cma.responder_resources = 0;
	else if (devattr.max_qp_rd_atom > 32)	/* arbitrary but <= 255 */
		ep->rep_remote_cma.responder_resources = 32;
	else
		ep->rep_remote_cma.responder_resources = devattr.max_qp_rd_atom;

	ep->rep_remote_cma.retry_count = 7;
	ep->rep_remote_cma.flow_control = 0;
	ep->rep_remote_cma.rnr_retry_count = 0;

	return 0;

out2:
	err = ib_destroy_cq(ep->rep_cq);
	if (err)
		dprintk("RPC:       %s: ib_destroy_cq returned %i\n",
			__func__, err);
out1:
	return rc;
}

/*
 * rpcrdma_ep_destroy
 *
 * Disconnect and destroy endpoint. After this, the only
 * valid operations on the ep are to free it (if dynamically
 * allocated) or re-create it.
 *
 * The caller's error handling must be sure to not leak the endpoint
 * if this function fails.
 */
int
rpcrdma_ep_destroy(struct rpcrdma_ep *ep, struct rpcrdma_ia *ia)
{
	int rc;

	dprintk("RPC:       %s: entering, connected is %d\n",
		__func__, ep->rep_connected);

	if (ia->ri_id->qp) {
		rc = rpcrdma_ep_disconnect(ep, ia);
		if (rc)
			dprintk("RPC:       %s: rpcrdma_ep_disconnect"
				" returned %i\n", __func__, rc);
		rdma_destroy_qp(ia->ri_id);
		ia->ri_id->qp = NULL;
	}

	/* padding - could be done in rpcrdma_buffer_destroy... */
	if (ep->rep_pad_mr) {
		rpcrdma_deregister_internal(ia, ep->rep_pad_mr, &ep->rep_pad);
		ep->rep_pad_mr = NULL;
	}

	rpcrdma_clean_cq(ep->rep_cq);
	rc = ib_destroy_cq(ep->rep_cq);
	if (rc)
		dprintk("RPC:       %s: ib_destroy_cq returned %i\n",
			__func__, rc);

	return rc;
}

/*
 * Connect unconnected endpoint.
 */
int
rpcrdma_ep_connect(struct rpcrdma_ep *ep, struct rpcrdma_ia *ia)
{
	struct rdma_cm_id *id;
	int rc = 0;
	int retry_count = 0;
	int reconnect = (ep->rep_connected != 0);

	if (reconnect) {
		struct rpcrdma_xprt *xprt;
retry:
		rc = rpcrdma_ep_disconnect(ep, ia);
		if (rc && rc != -ENOTCONN)
			dprintk("RPC:       %s: rpcrdma_ep_disconnect"
				" status %i\n", __func__, rc);
		rpcrdma_clean_cq(ep->rep_cq);

		xprt = container_of(ia, struct rpcrdma_xprt, rx_ia);
		id = rpcrdma_create_id(xprt, ia,
				(struct sockaddr *)&xprt->rx_data.addr);
		if (IS_ERR(id)) {
			rc = PTR_ERR(id);
			goto out;
		}
		/* TEMP TEMP TEMP - fail if new device:
		 * Deregister/remarshal *all* requests!
		 * Close and recreate adapter, pd, etc!
		 * Re-determine all attributes still sane!
		 * More stuff I haven't thought of!
		 * Rrrgh!
		 */
		if (ia->ri_id->device != id->device) {
			printk("RPC:       %s: can't reconnect on "
				"different device!\n", __func__);
			rdma_destroy_id(id);
			rc = -ENETDOWN;
			goto out;
		}
		/* END TEMP */
		rdma_destroy_id(ia->ri_id);
		ia->ri_id = id;
	}

	rc = rdma_create_qp(ia->ri_id, ia->ri_pd, &ep->rep_attr);
	if (rc) {
		dprintk("RPC:       %s: rdma_create_qp failed %i\n",
			__func__, rc);
		goto out;
	}

/* XXX Tavor device performs badly with 2K MTU! */
if (strnicmp(ia->ri_id->device->dma_device->bus->name, "pci", 3) == 0) {
	struct pci_dev *pcid = to_pci_dev(ia->ri_id->device->dma_device);
	if (pcid->device == PCI_DEVICE_ID_MELLANOX_TAVOR &&
	    (pcid->vendor == PCI_VENDOR_ID_MELLANOX ||
	     pcid->vendor == PCI_VENDOR_ID_TOPSPIN)) {
		struct ib_qp_attr attr = {
			.path_mtu = IB_MTU_1024
		};
		rc = ib_modify_qp(ia->ri_id->qp, &attr, IB_QP_PATH_MTU);
	}
}

	ep->rep_connected = 0;

	rc = rdma_connect(ia->ri_id, &ep->rep_remote_cma);
	if (rc) {
		dprintk("RPC:       %s: rdma_connect() failed with %i\n",
				__func__, rc);
		goto out;
	}

	if (reconnect)
		return 0;

	wait_event_interruptible(ep->rep_connect_wait, ep->rep_connected != 0);

	/*
	 * Check state. A non-peer reject indicates no listener
	 * (ECONNREFUSED), which may be a transient state. All
	 * others indicate a transport condition which has already
	 * undergone a best-effort.
	 */
	if (ep->rep_connected == -ECONNREFUSED
	    && ++retry_count <= RDMA_CONNECT_RETRY_MAX) {
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
		dprintk("RPC:       %s: connected\n", __func__);
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
int
rpcrdma_ep_disconnect(struct rpcrdma_ep *ep, struct rpcrdma_ia *ia)
{
	int rc;

	rpcrdma_clean_cq(ep->rep_cq);
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
	return rc;
}

/*
 * Initialize buffer memory
 */
int
rpcrdma_buffer_create(struct rpcrdma_buffer *buf, struct rpcrdma_ep *ep,
	struct rpcrdma_ia *ia, struct rpcrdma_create_data_internal *cdata)
{
	char *p;
	size_t len;
	int i, rc;
	struct rpcrdma_mw *r;

	buf->rb_max_requests = cdata->max_requests;
	spin_lock_init(&buf->rb_lock);
	atomic_set(&buf->rb_credits, 1);

	/* Need to allocate:
	 *   1.  arrays for send and recv pointers
	 *   2.  arrays of struct rpcrdma_req to fill in pointers
	 *   3.  array of struct rpcrdma_rep for replies
	 *   4.  padding, if any
	 *   5.  mw's, fmr's or frmr's, if any
	 * Send/recv buffers in req/rep need to be registered
	 */

	len = buf->rb_max_requests *
		(sizeof(struct rpcrdma_req *) + sizeof(struct rpcrdma_rep *));
	len += cdata->padding;
	switch (ia->ri_memreg_strategy) {
	case RPCRDMA_FRMR:
		len += buf->rb_max_requests * RPCRDMA_MAX_SEGS *
				sizeof(struct rpcrdma_mw);
		break;
	case RPCRDMA_MTHCAFMR:
		/* TBD we are perhaps overallocating here */
		len += (buf->rb_max_requests + 1) * RPCRDMA_MAX_SEGS *
				sizeof(struct rpcrdma_mw);
		break;
	case RPCRDMA_MEMWINDOWS_ASYNC:
	case RPCRDMA_MEMWINDOWS:
		len += (buf->rb_max_requests + 1) * RPCRDMA_MAX_SEGS *
				sizeof(struct rpcrdma_mw);
		break;
	default:
		break;
	}

	/* allocate 1, 4 and 5 in one shot */
	p = kzalloc(len, GFP_KERNEL);
	if (p == NULL) {
		dprintk("RPC:       %s: req_t/rep_t/pad kzalloc(%zd) failed\n",
			__func__, len);
		rc = -ENOMEM;
		goto out;
	}
	buf->rb_pool = p;	/* for freeing it later */

	buf->rb_send_bufs = (struct rpcrdma_req **) p;
	p = (char *) &buf->rb_send_bufs[buf->rb_max_requests];
	buf->rb_recv_bufs = (struct rpcrdma_rep **) p;
	p = (char *) &buf->rb_recv_bufs[buf->rb_max_requests];

	/*
	 * Register the zeroed pad buffer, if any.
	 */
	if (cdata->padding) {
		rc = rpcrdma_register_internal(ia, p, cdata->padding,
					    &ep->rep_pad_mr, &ep->rep_pad);
		if (rc)
			goto out;
	}
	p += cdata->padding;

	/*
	 * Allocate the fmr's, or mw's for mw_bind chunk registration.
	 * We "cycle" the mw's in order to minimize rkey reuse,
	 * and also reduce unbind-to-bind collision.
	 */
	INIT_LIST_HEAD(&buf->rb_mws);
	r = (struct rpcrdma_mw *)p;
	switch (ia->ri_memreg_strategy) {
	case RPCRDMA_FRMR:
		for (i = buf->rb_max_requests * RPCRDMA_MAX_SEGS; i; i--) {
			r->r.frmr.fr_mr = ib_alloc_fast_reg_mr(ia->ri_pd,
							 RPCRDMA_MAX_SEGS);
			if (IS_ERR(r->r.frmr.fr_mr)) {
				rc = PTR_ERR(r->r.frmr.fr_mr);
				dprintk("RPC:       %s: ib_alloc_fast_reg_mr"
					" failed %i\n", __func__, rc);
				goto out;
			}
			r->r.frmr.fr_pgl =
				ib_alloc_fast_reg_page_list(ia->ri_id->device,
							    RPCRDMA_MAX_SEGS);
			if (IS_ERR(r->r.frmr.fr_pgl)) {
				rc = PTR_ERR(r->r.frmr.fr_pgl);
				dprintk("RPC:       %s: "
					"ib_alloc_fast_reg_page_list "
					"failed %i\n", __func__, rc);
				goto out;
			}
			list_add(&r->mw_list, &buf->rb_mws);
			++r;
		}
		break;
	case RPCRDMA_MTHCAFMR:
		/* TBD we are perhaps overallocating here */
		for (i = (buf->rb_max_requests+1) * RPCRDMA_MAX_SEGS; i; i--) {
			static struct ib_fmr_attr fa =
				{ RPCRDMA_MAX_DATA_SEGS, 1, PAGE_SHIFT };
			r->r.fmr = ib_alloc_fmr(ia->ri_pd,
				IB_ACCESS_REMOTE_WRITE | IB_ACCESS_REMOTE_READ,
				&fa);
			if (IS_ERR(r->r.fmr)) {
				rc = PTR_ERR(r->r.fmr);
				dprintk("RPC:       %s: ib_alloc_fmr"
					" failed %i\n", __func__, rc);
				goto out;
			}
			list_add(&r->mw_list, &buf->rb_mws);
			++r;
		}
		break;
	case RPCRDMA_MEMWINDOWS_ASYNC:
	case RPCRDMA_MEMWINDOWS:
		/* Allocate one extra request's worth, for full cycling */
		for (i = (buf->rb_max_requests+1) * RPCRDMA_MAX_SEGS; i; i--) {
			r->r.mw = ib_alloc_mw(ia->ri_pd);
			if (IS_ERR(r->r.mw)) {
				rc = PTR_ERR(r->r.mw);
				dprintk("RPC:       %s: ib_alloc_mw"
					" failed %i\n", __func__, rc);
				goto out;
			}
			list_add(&r->mw_list, &buf->rb_mws);
			++r;
		}
		break;
	default:
		break;
	}

	/*
	 * Allocate/init the request/reply buffers. Doing this
	 * using kmalloc for now -- one for each buf.
	 */
	for (i = 0; i < buf->rb_max_requests; i++) {
		struct rpcrdma_req *req;
		struct rpcrdma_rep *rep;

		len = cdata->inline_wsize + sizeof(struct rpcrdma_req);
		/* RPC layer requests *double* size + 1K RPC_SLACK_SPACE! */
		/* Typical ~2400b, so rounding up saves work later */
		if (len < 4096)
			len = 4096;
		req = kmalloc(len, GFP_KERNEL);
		if (req == NULL) {
			dprintk("RPC:       %s: request buffer %d alloc"
				" failed\n", __func__, i);
			rc = -ENOMEM;
			goto out;
		}
		memset(req, 0, sizeof(struct rpcrdma_req));
		buf->rb_send_bufs[i] = req;
		buf->rb_send_bufs[i]->rl_buffer = buf;

		rc = rpcrdma_register_internal(ia, req->rl_base,
				len - offsetof(struct rpcrdma_req, rl_base),
				&buf->rb_send_bufs[i]->rl_handle,
				&buf->rb_send_bufs[i]->rl_iov);
		if (rc)
			goto out;

		buf->rb_send_bufs[i]->rl_size = len-sizeof(struct rpcrdma_req);

		len = cdata->inline_rsize + sizeof(struct rpcrdma_rep);
		rep = kmalloc(len, GFP_KERNEL);
		if (rep == NULL) {
			dprintk("RPC:       %s: reply buffer %d alloc failed\n",
				__func__, i);
			rc = -ENOMEM;
			goto out;
		}
		memset(rep, 0, sizeof(struct rpcrdma_rep));
		buf->rb_recv_bufs[i] = rep;
		buf->rb_recv_bufs[i]->rr_buffer = buf;
		init_waitqueue_head(&rep->rr_unbind);

		rc = rpcrdma_register_internal(ia, rep->rr_base,
				len - offsetof(struct rpcrdma_rep, rr_base),
				&buf->rb_recv_bufs[i]->rr_handle,
				&buf->rb_recv_bufs[i]->rr_iov);
		if (rc)
			goto out;

	}
	dprintk("RPC:       %s: max_requests %d\n",
		__func__, buf->rb_max_requests);
	/* done */
	return 0;
out:
	rpcrdma_buffer_destroy(buf);
	return rc;
}

/*
 * Unregister and destroy buffer memory. Need to deal with
 * partial initialization, so it's callable from failed create.
 * Must be called before destroying endpoint, as registrations
 * reference it.
 */
void
rpcrdma_buffer_destroy(struct rpcrdma_buffer *buf)
{
	int rc, i;
	struct rpcrdma_ia *ia = rdmab_to_ia(buf);
	struct rpcrdma_mw *r;

	/* clean up in reverse order from create
	 *   1.  recv mr memory (mr free, then kfree)
	 *   1a. bind mw memory
	 *   2.  send mr memory (mr free, then kfree)
	 *   3.  padding (if any) [moved to rpcrdma_ep_destroy]
	 *   4.  arrays
	 */
	dprintk("RPC:       %s: entering\n", __func__);

	for (i = 0; i < buf->rb_max_requests; i++) {
		if (buf->rb_recv_bufs && buf->rb_recv_bufs[i]) {
			rpcrdma_deregister_internal(ia,
					buf->rb_recv_bufs[i]->rr_handle,
					&buf->rb_recv_bufs[i]->rr_iov);
			kfree(buf->rb_recv_bufs[i]);
		}
		if (buf->rb_send_bufs && buf->rb_send_bufs[i]) {
			while (!list_empty(&buf->rb_mws)) {
				r = list_entry(buf->rb_mws.next,
					struct rpcrdma_mw, mw_list);
				list_del(&r->mw_list);
				switch (ia->ri_memreg_strategy) {
				case RPCRDMA_FRMR:
					rc = ib_dereg_mr(r->r.frmr.fr_mr);
					if (rc)
						dprintk("RPC:       %s:"
							" ib_dereg_mr"
							" failed %i\n",
							__func__, rc);
					ib_free_fast_reg_page_list(r->r.frmr.fr_pgl);
					break;
				case RPCRDMA_MTHCAFMR:
					rc = ib_dealloc_fmr(r->r.fmr);
					if (rc)
						dprintk("RPC:       %s:"
							" ib_dealloc_fmr"
							" failed %i\n",
							__func__, rc);
					break;
				case RPCRDMA_MEMWINDOWS_ASYNC:
				case RPCRDMA_MEMWINDOWS:
					rc = ib_dealloc_mw(r->r.mw);
					if (rc)
						dprintk("RPC:       %s:"
							" ib_dealloc_mw"
							" failed %i\n",
							__func__, rc);
					break;
				default:
					break;
				}
			}
			rpcrdma_deregister_internal(ia,
					buf->rb_send_bufs[i]->rl_handle,
					&buf->rb_send_bufs[i]->rl_iov);
			kfree(buf->rb_send_bufs[i]);
		}
	}

	kfree(buf->rb_pool);
}

/*
 * Get a set of request/reply buffers.
 *
 * Reply buffer (if needed) is attached to send buffer upon return.
 * Rule:
 *    rb_send_index and rb_recv_index MUST always be pointing to the
 *    *next* available buffer (non-NULL). They are incremented after
 *    removing buffers, and decremented *before* returning them.
 */
struct rpcrdma_req *
rpcrdma_buffer_get(struct rpcrdma_buffer *buffers)
{
	struct rpcrdma_req *req;
	unsigned long flags;
	int i;
	struct rpcrdma_mw *r;

	spin_lock_irqsave(&buffers->rb_lock, flags);
	if (buffers->rb_send_index == buffers->rb_max_requests) {
		spin_unlock_irqrestore(&buffers->rb_lock, flags);
		dprintk("RPC:       %s: out of request buffers\n", __func__);
		return ((struct rpcrdma_req *)NULL);
	}

	req = buffers->rb_send_bufs[buffers->rb_send_index];
	if (buffers->rb_send_index < buffers->rb_recv_index) {
		dprintk("RPC:       %s: %d extra receives outstanding (ok)\n",
			__func__,
			buffers->rb_recv_index - buffers->rb_send_index);
		req->rl_reply = NULL;
	} else {
		req->rl_reply = buffers->rb_recv_bufs[buffers->rb_recv_index];
		buffers->rb_recv_bufs[buffers->rb_recv_index++] = NULL;
	}
	buffers->rb_send_bufs[buffers->rb_send_index++] = NULL;
	if (!list_empty(&buffers->rb_mws)) {
		i = RPCRDMA_MAX_SEGS - 1;
		do {
			r = list_entry(buffers->rb_mws.next,
					struct rpcrdma_mw, mw_list);
			list_del(&r->mw_list);
			req->rl_segments[i].mr_chunk.rl_mw = r;
		} while (--i >= 0);
	}
	spin_unlock_irqrestore(&buffers->rb_lock, flags);
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
	struct rpcrdma_ia *ia = rdmab_to_ia(buffers);
	int i;
	unsigned long flags;

	BUG_ON(req->rl_nchunks != 0);
	spin_lock_irqsave(&buffers->rb_lock, flags);
	buffers->rb_send_bufs[--buffers->rb_send_index] = req;
	req->rl_niovs = 0;
	if (req->rl_reply) {
		buffers->rb_recv_bufs[--buffers->rb_recv_index] = req->rl_reply;
		init_waitqueue_head(&req->rl_reply->rr_unbind);
		req->rl_reply->rr_func = NULL;
		req->rl_reply = NULL;
	}
	switch (ia->ri_memreg_strategy) {
	case RPCRDMA_FRMR:
	case RPCRDMA_MTHCAFMR:
	case RPCRDMA_MEMWINDOWS_ASYNC:
	case RPCRDMA_MEMWINDOWS:
		/*
		 * Cycle mw's back in reverse order, and "spin" them.
		 * This delays and scrambles reuse as much as possible.
		 */
		i = 1;
		do {
			struct rpcrdma_mw **mw;
			mw = &req->rl_segments[i].mr_chunk.rl_mw;
			list_add_tail(&(*mw)->mw_list, &buffers->rb_mws);
			*mw = NULL;
		} while (++i < RPCRDMA_MAX_SEGS);
		list_add_tail(&req->rl_segments[0].mr_chunk.rl_mw->mw_list,
					&buffers->rb_mws);
		req->rl_segments[0].mr_chunk.rl_mw = NULL;
		break;
	default:
		break;
	}
	spin_unlock_irqrestore(&buffers->rb_lock, flags);
}

/*
 * Recover reply buffers from pool.
 * This happens when recovering from error conditions.
 * Post-increment counter/array index.
 */
void
rpcrdma_recv_buffer_get(struct rpcrdma_req *req)
{
	struct rpcrdma_buffer *buffers = req->rl_buffer;
	unsigned long flags;

	if (req->rl_iov.length == 0)	/* special case xprt_rdma_allocate() */
		buffers = ((struct rpcrdma_req *) buffers)->rl_buffer;
	spin_lock_irqsave(&buffers->rb_lock, flags);
	if (buffers->rb_recv_index < buffers->rb_max_requests) {
		req->rl_reply = buffers->rb_recv_bufs[buffers->rb_recv_index];
		buffers->rb_recv_bufs[buffers->rb_recv_index++] = NULL;
	}
	spin_unlock_irqrestore(&buffers->rb_lock, flags);
}

/*
 * Put reply buffers back into pool when not attached to
 * request. This happens in error conditions, and when
 * aborting unbinds. Pre-decrement counter/array index.
 */
void
rpcrdma_recv_buffer_put(struct rpcrdma_rep *rep)
{
	struct rpcrdma_buffer *buffers = rep->rr_buffer;
	unsigned long flags;

	rep->rr_func = NULL;
	spin_lock_irqsave(&buffers->rb_lock, flags);
	buffers->rb_recv_bufs[--buffers->rb_recv_index] = rep;
	spin_unlock_irqrestore(&buffers->rb_lock, flags);
}

/*
 * Wrappers for internal-use kmalloc memory registration, used by buffer code.
 */

int
rpcrdma_register_internal(struct rpcrdma_ia *ia, void *va, int len,
				struct ib_mr **mrp, struct ib_sge *iov)
{
	struct ib_phys_buf ipb;
	struct ib_mr *mr;
	int rc;

	/*
	 * All memory passed here was kmalloc'ed, therefore phys-contiguous.
	 */
	iov->addr = ib_dma_map_single(ia->ri_id->device,
			va, len, DMA_BIDIRECTIONAL);
	iov->length = len;

	if (ia->ri_have_dma_lkey) {
		*mrp = NULL;
		iov->lkey = ia->ri_dma_lkey;
		return 0;
	} else if (ia->ri_bind_mem != NULL) {
		*mrp = NULL;
		iov->lkey = ia->ri_bind_mem->lkey;
		return 0;
	}

	ipb.addr = iov->addr;
	ipb.size = iov->length;
	mr = ib_reg_phys_mr(ia->ri_pd, &ipb, 1,
			IB_ACCESS_LOCAL_WRITE, &iov->addr);

	dprintk("RPC:       %s: phys convert: 0x%llx "
			"registered 0x%llx length %d\n",
			__func__, (unsigned long long)ipb.addr,
			(unsigned long long)iov->addr, len);

	if (IS_ERR(mr)) {
		*mrp = NULL;
		rc = PTR_ERR(mr);
		dprintk("RPC:       %s: failed with %i\n", __func__, rc);
	} else {
		*mrp = mr;
		iov->lkey = mr->lkey;
		rc = 0;
	}

	return rc;
}

int
rpcrdma_deregister_internal(struct rpcrdma_ia *ia,
				struct ib_mr *mr, struct ib_sge *iov)
{
	int rc;

	ib_dma_unmap_single(ia->ri_id->device,
			iov->addr, iov->length, DMA_BIDIRECTIONAL);

	if (NULL == mr)
		return 0;

	rc = ib_dereg_mr(mr);
	if (rc)
		dprintk("RPC:       %s: ib_dereg_mr failed %i\n", __func__, rc);
	return rc;
}

/*
 * Wrappers for chunk registration, shared by read/write chunk code.
 */

static void
rpcrdma_map_one(struct rpcrdma_ia *ia, struct rpcrdma_mr_seg *seg, int writing)
{
	seg->mr_dir = writing ? DMA_FROM_DEVICE : DMA_TO_DEVICE;
	seg->mr_dmalen = seg->mr_len;
	if (seg->mr_page)
		seg->mr_dma = ib_dma_map_page(ia->ri_id->device,
				seg->mr_page, offset_in_page(seg->mr_offset),
				seg->mr_dmalen, seg->mr_dir);
	else
		seg->mr_dma = ib_dma_map_single(ia->ri_id->device,
				seg->mr_offset,
				seg->mr_dmalen, seg->mr_dir);
}

static void
rpcrdma_unmap_one(struct rpcrdma_ia *ia, struct rpcrdma_mr_seg *seg)
{
	if (seg->mr_page)
		ib_dma_unmap_page(ia->ri_id->device,
				seg->mr_dma, seg->mr_dmalen, seg->mr_dir);
	else
		ib_dma_unmap_single(ia->ri_id->device,
				seg->mr_dma, seg->mr_dmalen, seg->mr_dir);
}

static int
rpcrdma_register_frmr_external(struct rpcrdma_mr_seg *seg,
			int *nsegs, int writing, struct rpcrdma_ia *ia,
			struct rpcrdma_xprt *r_xprt)
{
	struct rpcrdma_mr_seg *seg1 = seg;
	struct ib_send_wr frmr_wr, *bad_wr;
	u8 key;
	int len, pageoff;
	int i, rc;

	pageoff = offset_in_page(seg1->mr_offset);
	seg1->mr_offset -= pageoff;	/* start of page */
	seg1->mr_len += pageoff;
	len = -pageoff;
	if (*nsegs > RPCRDMA_MAX_DATA_SEGS)
		*nsegs = RPCRDMA_MAX_DATA_SEGS;
	for (i = 0; i < *nsegs;) {
		rpcrdma_map_one(ia, seg, writing);
		seg1->mr_chunk.rl_mw->r.frmr.fr_pgl->page_list[i] = seg->mr_dma;
		len += seg->mr_len;
		++seg;
		++i;
		/* Check for holes */
		if ((i < *nsegs && offset_in_page(seg->mr_offset)) ||
		    offset_in_page((seg-1)->mr_offset + (seg-1)->mr_len))
			break;
	}
	dprintk("RPC:       %s: Using frmr %p to map %d segments\n",
		__func__, seg1->mr_chunk.rl_mw, i);

	/* Bump the key */
	key = (u8)(seg1->mr_chunk.rl_mw->r.frmr.fr_mr->rkey & 0x000000FF);
	ib_update_fast_reg_key(seg1->mr_chunk.rl_mw->r.frmr.fr_mr, ++key);

	/* Prepare FRMR WR */
	memset(&frmr_wr, 0, sizeof frmr_wr);
	frmr_wr.opcode = IB_WR_FAST_REG_MR;
	frmr_wr.send_flags = 0;			/* unsignaled */
	frmr_wr.wr.fast_reg.iova_start = (unsigned long)seg1->mr_dma;
	frmr_wr.wr.fast_reg.page_list = seg1->mr_chunk.rl_mw->r.frmr.fr_pgl;
	frmr_wr.wr.fast_reg.page_list_len = i;
	frmr_wr.wr.fast_reg.page_shift = PAGE_SHIFT;
	frmr_wr.wr.fast_reg.length = i << PAGE_SHIFT;
	frmr_wr.wr.fast_reg.access_flags = (writing ?
				IB_ACCESS_REMOTE_WRITE : IB_ACCESS_REMOTE_READ);
	frmr_wr.wr.fast_reg.rkey = seg1->mr_chunk.rl_mw->r.frmr.fr_mr->rkey;
	DECR_CQCOUNT(&r_xprt->rx_ep);

	rc = ib_post_send(ia->ri_id->qp, &frmr_wr, &bad_wr);

	if (rc) {
		dprintk("RPC:       %s: failed ib_post_send for register,"
			" status %i\n", __func__, rc);
		while (i--)
			rpcrdma_unmap_one(ia, --seg);
	} else {
		seg1->mr_rkey = seg1->mr_chunk.rl_mw->r.frmr.fr_mr->rkey;
		seg1->mr_base = seg1->mr_dma + pageoff;
		seg1->mr_nsegs = i;
		seg1->mr_len = len;
	}
	*nsegs = i;
	return rc;
}

static int
rpcrdma_deregister_frmr_external(struct rpcrdma_mr_seg *seg,
			struct rpcrdma_ia *ia, struct rpcrdma_xprt *r_xprt)
{
	struct rpcrdma_mr_seg *seg1 = seg;
	struct ib_send_wr invalidate_wr, *bad_wr;
	int rc;

	while (seg1->mr_nsegs--)
		rpcrdma_unmap_one(ia, seg++);

	memset(&invalidate_wr, 0, sizeof invalidate_wr);
	invalidate_wr.opcode = IB_WR_LOCAL_INV;
	invalidate_wr.send_flags = 0;			/* unsignaled */
	invalidate_wr.ex.invalidate_rkey = seg1->mr_chunk.rl_mw->r.frmr.fr_mr->rkey;
	DECR_CQCOUNT(&r_xprt->rx_ep);

	rc = ib_post_send(ia->ri_id->qp, &invalidate_wr, &bad_wr);
	if (rc)
		dprintk("RPC:       %s: failed ib_post_send for invalidate,"
			" status %i\n", __func__, rc);
	return rc;
}

static int
rpcrdma_register_fmr_external(struct rpcrdma_mr_seg *seg,
			int *nsegs, int writing, struct rpcrdma_ia *ia)
{
	struct rpcrdma_mr_seg *seg1 = seg;
	u64 physaddrs[RPCRDMA_MAX_DATA_SEGS];
	int len, pageoff, i, rc;

	pageoff = offset_in_page(seg1->mr_offset);
	seg1->mr_offset -= pageoff;	/* start of page */
	seg1->mr_len += pageoff;
	len = -pageoff;
	if (*nsegs > RPCRDMA_MAX_DATA_SEGS)
		*nsegs = RPCRDMA_MAX_DATA_SEGS;
	for (i = 0; i < *nsegs;) {
		rpcrdma_map_one(ia, seg, writing);
		physaddrs[i] = seg->mr_dma;
		len += seg->mr_len;
		++seg;
		++i;
		/* Check for holes */
		if ((i < *nsegs && offset_in_page(seg->mr_offset)) ||
		    offset_in_page((seg-1)->mr_offset + (seg-1)->mr_len))
			break;
	}
	rc = ib_map_phys_fmr(seg1->mr_chunk.rl_mw->r.fmr,
				physaddrs, i, seg1->mr_dma);
	if (rc) {
		dprintk("RPC:       %s: failed ib_map_phys_fmr "
			"%u@0x%llx+%i (%d)... status %i\n", __func__,
			len, (unsigned long long)seg1->mr_dma,
			pageoff, i, rc);
		while (i--)
			rpcrdma_unmap_one(ia, --seg);
	} else {
		seg1->mr_rkey = seg1->mr_chunk.rl_mw->r.fmr->rkey;
		seg1->mr_base = seg1->mr_dma + pageoff;
		seg1->mr_nsegs = i;
		seg1->mr_len = len;
	}
	*nsegs = i;
	return rc;
}

static int
rpcrdma_deregister_fmr_external(struct rpcrdma_mr_seg *seg,
			struct rpcrdma_ia *ia)
{
	struct rpcrdma_mr_seg *seg1 = seg;
	LIST_HEAD(l);
	int rc;

	list_add(&seg1->mr_chunk.rl_mw->r.fmr->list, &l);
	rc = ib_unmap_fmr(&l);
	while (seg1->mr_nsegs--)
		rpcrdma_unmap_one(ia, seg++);
	if (rc)
		dprintk("RPC:       %s: failed ib_unmap_fmr,"
			" status %i\n", __func__, rc);
	return rc;
}

static int
rpcrdma_register_memwin_external(struct rpcrdma_mr_seg *seg,
			int *nsegs, int writing, struct rpcrdma_ia *ia,
			struct rpcrdma_xprt *r_xprt)
{
	int mem_priv = (writing ? IB_ACCESS_REMOTE_WRITE :
				  IB_ACCESS_REMOTE_READ);
	struct ib_mw_bind param;
	int rc;

	*nsegs = 1;
	rpcrdma_map_one(ia, seg, writing);
	param.mr = ia->ri_bind_mem;
	param.wr_id = 0ULL;	/* no send cookie */
	param.addr = seg->mr_dma;
	param.length = seg->mr_len;
	param.send_flags = 0;
	param.mw_access_flags = mem_priv;

	DECR_CQCOUNT(&r_xprt->rx_ep);
	rc = ib_bind_mw(ia->ri_id->qp, seg->mr_chunk.rl_mw->r.mw, &param);
	if (rc) {
		dprintk("RPC:       %s: failed ib_bind_mw "
			"%u@0x%llx status %i\n",
			__func__, seg->mr_len,
			(unsigned long long)seg->mr_dma, rc);
		rpcrdma_unmap_one(ia, seg);
	} else {
		seg->mr_rkey = seg->mr_chunk.rl_mw->r.mw->rkey;
		seg->mr_base = param.addr;
		seg->mr_nsegs = 1;
	}
	return rc;
}

static int
rpcrdma_deregister_memwin_external(struct rpcrdma_mr_seg *seg,
			struct rpcrdma_ia *ia,
			struct rpcrdma_xprt *r_xprt, void **r)
{
	struct ib_mw_bind param;
	LIST_HEAD(l);
	int rc;

	BUG_ON(seg->mr_nsegs != 1);
	param.mr = ia->ri_bind_mem;
	param.addr = 0ULL;	/* unbind */
	param.length = 0;
	param.mw_access_flags = 0;
	if (*r) {
		param.wr_id = (u64) (unsigned long) *r;
		param.send_flags = IB_SEND_SIGNALED;
		INIT_CQCOUNT(&r_xprt->rx_ep);
	} else {
		param.wr_id = 0ULL;
		param.send_flags = 0;
		DECR_CQCOUNT(&r_xprt->rx_ep);
	}
	rc = ib_bind_mw(ia->ri_id->qp, seg->mr_chunk.rl_mw->r.mw, &param);
	rpcrdma_unmap_one(ia, seg);
	if (rc)
		dprintk("RPC:       %s: failed ib_(un)bind_mw,"
			" status %i\n", __func__, rc);
	else
		*r = NULL;	/* will upcall on completion */
	return rc;
}

static int
rpcrdma_register_default_external(struct rpcrdma_mr_seg *seg,
			int *nsegs, int writing, struct rpcrdma_ia *ia)
{
	int mem_priv = (writing ? IB_ACCESS_REMOTE_WRITE :
				  IB_ACCESS_REMOTE_READ);
	struct rpcrdma_mr_seg *seg1 = seg;
	struct ib_phys_buf ipb[RPCRDMA_MAX_DATA_SEGS];
	int len, i, rc = 0;

	if (*nsegs > RPCRDMA_MAX_DATA_SEGS)
		*nsegs = RPCRDMA_MAX_DATA_SEGS;
	for (len = 0, i = 0; i < *nsegs;) {
		rpcrdma_map_one(ia, seg, writing);
		ipb[i].addr = seg->mr_dma;
		ipb[i].size = seg->mr_len;
		len += seg->mr_len;
		++seg;
		++i;
		/* Check for holes */
		if ((i < *nsegs && offset_in_page(seg->mr_offset)) ||
		    offset_in_page((seg-1)->mr_offset+(seg-1)->mr_len))
			break;
	}
	seg1->mr_base = seg1->mr_dma;
	seg1->mr_chunk.rl_mr = ib_reg_phys_mr(ia->ri_pd,
				ipb, i, mem_priv, &seg1->mr_base);
	if (IS_ERR(seg1->mr_chunk.rl_mr)) {
		rc = PTR_ERR(seg1->mr_chunk.rl_mr);
		dprintk("RPC:       %s: failed ib_reg_phys_mr "
			"%u@0x%llx (%d)... status %i\n",
			__func__, len,
			(unsigned long long)seg1->mr_dma, i, rc);
		while (i--)
			rpcrdma_unmap_one(ia, --seg);
	} else {
		seg1->mr_rkey = seg1->mr_chunk.rl_mr->rkey;
		seg1->mr_nsegs = i;
		seg1->mr_len = len;
	}
	*nsegs = i;
	return rc;
}

static int
rpcrdma_deregister_default_external(struct rpcrdma_mr_seg *seg,
			struct rpcrdma_ia *ia)
{
	struct rpcrdma_mr_seg *seg1 = seg;
	int rc;

	rc = ib_dereg_mr(seg1->mr_chunk.rl_mr);
	seg1->mr_chunk.rl_mr = NULL;
	while (seg1->mr_nsegs--)
		rpcrdma_unmap_one(ia, seg++);
	if (rc)
		dprintk("RPC:       %s: failed ib_dereg_mr,"
			" status %i\n", __func__, rc);
	return rc;
}

int
rpcrdma_register_external(struct rpcrdma_mr_seg *seg,
			int nsegs, int writing, struct rpcrdma_xprt *r_xprt)
{
	struct rpcrdma_ia *ia = &r_xprt->rx_ia;
	int rc = 0;

	switch (ia->ri_memreg_strategy) {

#if RPCRDMA_PERSISTENT_REGISTRATION
	case RPCRDMA_ALLPHYSICAL:
		rpcrdma_map_one(ia, seg, writing);
		seg->mr_rkey = ia->ri_bind_mem->rkey;
		seg->mr_base = seg->mr_dma;
		seg->mr_nsegs = 1;
		nsegs = 1;
		break;
#endif

	/* Registration using frmr registration */
	case RPCRDMA_FRMR:
		rc = rpcrdma_register_frmr_external(seg, &nsegs, writing, ia, r_xprt);
		break;

	/* Registration using fmr memory registration */
	case RPCRDMA_MTHCAFMR:
		rc = rpcrdma_register_fmr_external(seg, &nsegs, writing, ia);
		break;

	/* Registration using memory windows */
	case RPCRDMA_MEMWINDOWS_ASYNC:
	case RPCRDMA_MEMWINDOWS:
		rc = rpcrdma_register_memwin_external(seg, &nsegs, writing, ia, r_xprt);
		break;

	/* Default registration each time */
	default:
		rc = rpcrdma_register_default_external(seg, &nsegs, writing, ia);
		break;
	}
	if (rc)
		return -1;

	return nsegs;
}

int
rpcrdma_deregister_external(struct rpcrdma_mr_seg *seg,
		struct rpcrdma_xprt *r_xprt, void *r)
{
	struct rpcrdma_ia *ia = &r_xprt->rx_ia;
	int nsegs = seg->mr_nsegs, rc;

	switch (ia->ri_memreg_strategy) {

#if RPCRDMA_PERSISTENT_REGISTRATION
	case RPCRDMA_ALLPHYSICAL:
		BUG_ON(nsegs != 1);
		rpcrdma_unmap_one(ia, seg);
		rc = 0;
		break;
#endif

	case RPCRDMA_FRMR:
		rc = rpcrdma_deregister_frmr_external(seg, ia, r_xprt);
		break;

	case RPCRDMA_MTHCAFMR:
		rc = rpcrdma_deregister_fmr_external(seg, ia);
		break;

	case RPCRDMA_MEMWINDOWS_ASYNC:
	case RPCRDMA_MEMWINDOWS:
		rc = rpcrdma_deregister_memwin_external(seg, ia, r_xprt, &r);
		break;

	default:
		rc = rpcrdma_deregister_default_external(seg, ia);
		break;
	}
	if (r) {
		struct rpcrdma_rep *rep = r;
		void (*func)(struct rpcrdma_rep *) = rep->rr_func;
		rep->rr_func = NULL;
		func(rep);	/* dereg done, callback now */
	}
	return nsegs;
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
	struct ib_send_wr send_wr, *send_wr_fail;
	struct rpcrdma_rep *rep = req->rl_reply;
	int rc;

	if (rep) {
		rc = rpcrdma_ep_post_recv(ia, ep, rep);
		if (rc)
			goto out;
		req->rl_reply = NULL;
	}

	send_wr.next = NULL;
	send_wr.wr_id = 0ULL;	/* no send cookie */
	send_wr.sg_list = req->rl_send_iov;
	send_wr.num_sge = req->rl_niovs;
	send_wr.opcode = IB_WR_SEND;
	if (send_wr.num_sge == 4)	/* no need to sync any pad (constant) */
		ib_dma_sync_single_for_device(ia->ri_id->device,
			req->rl_send_iov[3].addr, req->rl_send_iov[3].length,
			DMA_TO_DEVICE);
	ib_dma_sync_single_for_device(ia->ri_id->device,
		req->rl_send_iov[1].addr, req->rl_send_iov[1].length,
		DMA_TO_DEVICE);
	ib_dma_sync_single_for_device(ia->ri_id->device,
		req->rl_send_iov[0].addr, req->rl_send_iov[0].length,
		DMA_TO_DEVICE);

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
	recv_wr.wr_id = (u64) (unsigned long) rep;
	recv_wr.sg_list = &rep->rr_iov;
	recv_wr.num_sge = 1;

	ib_dma_sync_single_for_cpu(ia->ri_id->device,
		rep->rr_iov.addr, rep->rr_iov.length, DMA_BIDIRECTIONAL);

	DECR_CQCOUNT(ep);
	rc = ib_post_recv(ia->ri_id->qp, &recv_wr, &recv_wr_fail);

	if (rc)
		dprintk("RPC:       %s: ib_post_recv returned %i\n", __func__,
			rc);
	return rc;
}
