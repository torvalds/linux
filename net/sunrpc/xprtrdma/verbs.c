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
	unsigned long flags;

	data = data;
	spin_lock_irqsave(&rpcrdma_tk_lock_g, flags);
	while (!list_empty(&rpcrdma_tasklets_g)) {
		rep = list_entry(rpcrdma_tasklets_g.next,
				 struct rpcrdma_rep, rr_list);
		list_del(&rep->rr_list);
		spin_unlock_irqrestore(&rpcrdma_tk_lock_g, flags);

		rpcrdma_reply_handler(rep);

		spin_lock_irqsave(&rpcrdma_tk_lock_g, flags);
	}
	spin_unlock_irqrestore(&rpcrdma_tk_lock_g, flags);
}

static DECLARE_TASKLET(rpcrdma_tasklet_g, rpcrdma_run_tasklet, 0UL);

static void
rpcrdma_schedule_tasklet(struct list_head *sched_list)
{
	unsigned long flags;

	spin_lock_irqsave(&rpcrdma_tk_lock_g, flags);
	list_splice_tail(sched_list, &rpcrdma_tasklets_g);
	spin_unlock_irqrestore(&rpcrdma_tk_lock_g, flags);
	tasklet_schedule(&rpcrdma_tasklet_g);
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

static void
rpcrdma_cq_async_error_upcall(struct ib_event *event, void *context)
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

static void
rpcrdma_sendcq_process_wc(struct ib_wc *wc)
{
	/* WARNING: Only wr_id and status are reliable at this point */
	if (wc->wr_id == RPCRDMA_IGNORE_COMPLETION) {
		if (wc->status != IB_WC_SUCCESS &&
		    wc->status != IB_WC_WR_FLUSH_ERR)
			pr_err("RPC:       %s: SEND: %s\n",
			       __func__, ib_wc_status_msg(wc->status));
	} else {
		struct rpcrdma_mw *r;

		r = (struct rpcrdma_mw *)(unsigned long)wc->wr_id;
		r->mw_sendcompletion(wc);
	}
}

static int
rpcrdma_sendcq_poll(struct ib_cq *cq, struct rpcrdma_ep *ep)
{
	struct ib_wc *wcs;
	int budget, count, rc;

	budget = RPCRDMA_WC_BUDGET / RPCRDMA_POLLSIZE;
	do {
		wcs = ep->rep_send_wcs;

		rc = ib_poll_cq(cq, RPCRDMA_POLLSIZE, wcs);
		if (rc <= 0)
			return rc;

		count = rc;
		while (count-- > 0)
			rpcrdma_sendcq_process_wc(wcs++);
	} while (rc == RPCRDMA_POLLSIZE && --budget);
	return 0;
}

/*
 * Handle send, fast_reg_mr, and local_inv completions.
 *
 * Send events are typically suppressed and thus do not result
 * in an upcall. Occasionally one is signaled, however. This
 * prevents the provider's completion queue from wrapping and
 * losing a completion.
 */
static void
rpcrdma_sendcq_upcall(struct ib_cq *cq, void *cq_context)
{
	struct rpcrdma_ep *ep = (struct rpcrdma_ep *)cq_context;
	int rc;

	rc = rpcrdma_sendcq_poll(cq, ep);
	if (rc) {
		dprintk("RPC:       %s: ib_poll_cq failed: %i\n",
			__func__, rc);
		return;
	}

	rc = ib_req_notify_cq(cq,
			IB_CQ_NEXT_COMP | IB_CQ_REPORT_MISSED_EVENTS);
	if (rc == 0)
		return;
	if (rc < 0) {
		dprintk("RPC:       %s: ib_req_notify_cq failed: %i\n",
			__func__, rc);
		return;
	}

	rpcrdma_sendcq_poll(cq, ep);
}

static void
rpcrdma_recvcq_process_wc(struct ib_wc *wc, struct list_head *sched_list)
{
	struct rpcrdma_rep *rep =
			(struct rpcrdma_rep *)(unsigned long)wc->wr_id;

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
	prefetch(rdmab_to_msg(rep->rr_rdmabuf));

out_schedule:
	list_add_tail(&rep->rr_list, sched_list);
	return;
out_fail:
	if (wc->status != IB_WC_WR_FLUSH_ERR)
		pr_err("RPC:       %s: rep %p: %s\n",
		       __func__, rep, ib_wc_status_msg(wc->status));
	rep->rr_len = ~0U;
	goto out_schedule;
}

static int
rpcrdma_recvcq_poll(struct ib_cq *cq, struct rpcrdma_ep *ep)
{
	struct list_head sched_list;
	struct ib_wc *wcs;
	int budget, count, rc;

	INIT_LIST_HEAD(&sched_list);
	budget = RPCRDMA_WC_BUDGET / RPCRDMA_POLLSIZE;
	do {
		wcs = ep->rep_recv_wcs;

		rc = ib_poll_cq(cq, RPCRDMA_POLLSIZE, wcs);
		if (rc <= 0)
			goto out_schedule;

		count = rc;
		while (count-- > 0)
			rpcrdma_recvcq_process_wc(wcs++, &sched_list);
	} while (rc == RPCRDMA_POLLSIZE && --budget);
	rc = 0;

out_schedule:
	rpcrdma_schedule_tasklet(&sched_list);
	return rc;
}

/*
 * Handle receive completions.
 *
 * It is reentrant but processes single events in order to maintain
 * ordering of receives to keep server credits.
 *
 * It is the responsibility of the scheduled tasklet to return
 * recv buffers to the pool. NOTE: this affects synchronization of
 * connection shutdown. That is, the structures required for
 * the completion of the reply handler must remain intact until
 * all memory has been reclaimed.
 */
static void
rpcrdma_recvcq_upcall(struct ib_cq *cq, void *cq_context)
{
	struct rpcrdma_ep *ep = (struct rpcrdma_ep *)cq_context;
	int rc;

	rc = rpcrdma_recvcq_poll(cq, ep);
	if (rc) {
		dprintk("RPC:       %s: ib_poll_cq failed: %i\n",
			__func__, rc);
		return;
	}

	rc = ib_req_notify_cq(cq,
			IB_CQ_NEXT_COMP | IB_CQ_REPORT_MISSED_EVENTS);
	if (rc == 0)
		return;
	if (rc < 0) {
		dprintk("RPC:       %s: ib_req_notify_cq failed: %i\n",
			__func__, rc);
		return;
	}

	rpcrdma_recvcq_poll(cq, ep);
}

static void
rpcrdma_flush_cqs(struct rpcrdma_ep *ep)
{
	struct ib_wc wc;
	LIST_HEAD(sched_list);

	while (ib_poll_cq(ep->rep_attr.recv_cq, 1, &wc) > 0)
		rpcrdma_recvcq_process_wc(&wc, &sched_list);
	if (!list_empty(&sched_list))
		rpcrdma_schedule_tasklet(&sched_list);
	while (ib_poll_cq(ep->rep_attr.send_cq, 1, &wc) > 0)
		rpcrdma_sendcq_process_wc(&wc);
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

	id = rdma_create_id(rpcrdma_conn_upcall, xprt, RDMA_PS_TCP, IB_QPT_RC);
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
	struct rpcrdma_ia *ia = &xprt->rx_ia;
	struct ib_device_attr *devattr = &ia->ri_devattr;
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

	rc = ib_query_device(ia->ri_device, devattr);
	if (rc) {
		dprintk("RPC:       %s: ib_query_device failed %d\n",
			__func__, rc);
		goto out3;
	}

	if (memreg == RPCRDMA_FRMR) {
		if (!(devattr->device_cap_flags & IB_DEVICE_MEM_MGT_EXTENSIONS) ||
		    (devattr->max_fast_reg_page_list_len == 0)) {
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
	struct ib_device_attr *devattr = &ia->ri_devattr;
	struct ib_cq *sendcq, *recvcq;
	struct ib_cq_init_attr cq_attr = {};
	int rc, err;

	if (devattr->max_sge < RPCRDMA_MAX_IOVS) {
		dprintk("RPC:       %s: insufficient sge's available\n",
			__func__);
		return -ENOMEM;
	}

	/* check provider's send/recv wr limits */
	if (cdata->max_requests > devattr->max_qp_wr)
		cdata->max_requests = devattr->max_qp_wr;

	ep->rep_attr.event_handler = rpcrdma_qp_async_error_upcall;
	ep->rep_attr.qp_context = ep;
	ep->rep_attr.srq = NULL;
	ep->rep_attr.cap.max_send_wr = cdata->max_requests;
	rc = ia->ri_ops->ro_open(ia, ep, cdata);
	if (rc)
		return rc;
	ep->rep_attr.cap.max_recv_wr = cdata->max_requests;
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
	if (ep->rep_cqinit > RPCRDMA_MAX_UNSIGNALED_SENDS)
		ep->rep_cqinit = RPCRDMA_MAX_UNSIGNALED_SENDS;
	else if (ep->rep_cqinit <= 2)
		ep->rep_cqinit = 0;
	INIT_CQCOUNT(ep);
	init_waitqueue_head(&ep->rep_connect_wait);
	INIT_DELAYED_WORK(&ep->rep_connect_worker, rpcrdma_connect_worker);

	cq_attr.cqe = ep->rep_attr.cap.max_send_wr + 1;
	sendcq = ib_create_cq(ia->ri_device, rpcrdma_sendcq_upcall,
			      rpcrdma_cq_async_error_upcall, ep, &cq_attr);
	if (IS_ERR(sendcq)) {
		rc = PTR_ERR(sendcq);
		dprintk("RPC:       %s: failed to create send CQ: %i\n",
			__func__, rc);
		goto out1;
	}

	rc = ib_req_notify_cq(sendcq, IB_CQ_NEXT_COMP);
	if (rc) {
		dprintk("RPC:       %s: ib_req_notify_cq failed: %i\n",
			__func__, rc);
		goto out2;
	}

	cq_attr.cqe = ep->rep_attr.cap.max_recv_wr + 1;
	recvcq = ib_create_cq(ia->ri_device, rpcrdma_recvcq_upcall,
			      rpcrdma_cq_async_error_upcall, ep, &cq_attr);
	if (IS_ERR(recvcq)) {
		rc = PTR_ERR(recvcq);
		dprintk("RPC:       %s: failed to create recv CQ: %i\n",
			__func__, rc);
		goto out2;
	}

	rc = ib_req_notify_cq(recvcq, IB_CQ_NEXT_COMP);
	if (rc) {
		dprintk("RPC:       %s: ib_req_notify_cq failed: %i\n",
			__func__, rc);
		ib_destroy_cq(recvcq);
		goto out2;
	}

	ep->rep_attr.send_cq = sendcq;
	ep->rep_attr.recv_cq = recvcq;

	/* Initialize cma parameters */

	/* RPC/RDMA does not use private data */
	ep->rep_remote_cma.private_data = NULL;
	ep->rep_remote_cma.private_data_len = 0;

	/* Client offers RDMA Read but does not initiate */
	ep->rep_remote_cma.initiator_depth = 0;
	if (devattr->max_qp_rd_atom > 32)	/* arbitrary but <= 255 */
		ep->rep_remote_cma.responder_resources = 32;
	else
		ep->rep_remote_cma.responder_resources =
						devattr->max_qp_rd_atom;

	ep->rep_remote_cma.retry_count = 7;
	ep->rep_remote_cma.flow_control = 0;
	ep->rep_remote_cma.rnr_retry_count = 0;

	return 0;

out2:
	err = ib_destroy_cq(sendcq);
	if (err)
		dprintk("RPC:       %s: ib_destroy_cq returned %i\n",
			__func__, err);
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

	if (ia->ri_id->qp)
		rpcrdma_ep_disconnect(ep, ia);

	rpcrdma_clean_cq(ep->rep_attr.recv_cq);
	rpcrdma_clean_cq(ep->rep_attr.send_cq);

	if (ia->ri_id->qp) {
		rdma_destroy_qp(ia->ri_id);
		ia->ri_id->qp = NULL;
	}

	rc = ib_destroy_cq(ep->rep_attr.recv_cq);
	if (rc)
		dprintk("RPC:       %s: ib_destroy_cq returned %i\n",
			__func__, rc);

	rc = ib_destroy_cq(ep->rep_attr.send_cq);
	if (rc)
		dprintk("RPC:       %s: ib_destroy_cq returned %i\n",
			__func__, rc);

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
		rpcrdma_flush_cqs(ep);

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
void
rpcrdma_ep_disconnect(struct rpcrdma_ep *ep, struct rpcrdma_ia *ia)
{
	int rc;

	rpcrdma_flush_cqs(ep);
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
}

static struct rpcrdma_req *
rpcrdma_create_req(struct rpcrdma_xprt *r_xprt)
{
	struct rpcrdma_req *req;

	req = kzalloc(sizeof(*req), GFP_KERNEL);
	if (req == NULL)
		return ERR_PTR(-ENOMEM);

	req->rl_buffer = &r_xprt->rx_buf;
	return req;
}

static struct rpcrdma_rep *
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
	rep->rr_rxprt = r_xprt;
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
	struct rpcrdma_create_data_internal *cdata = &r_xprt->rx_data;
	char *p;
	size_t len;
	int i, rc;

	buf->rb_max_requests = cdata->max_requests;
	spin_lock_init(&buf->rb_lock);

	/* Need to allocate:
	 *   1.  arrays for send and recv pointers
	 *   2.  arrays of struct rpcrdma_req to fill in pointers
	 *   3.  array of struct rpcrdma_rep for replies
	 * Send/recv buffers in req/rep need to be registered
	 */
	len = buf->rb_max_requests *
		(sizeof(struct rpcrdma_req *) + sizeof(struct rpcrdma_rep *));

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

	rc = ia->ri_ops->ro_init(r_xprt);
	if (rc)
		goto out;

	for (i = 0; i < buf->rb_max_requests; i++) {
		struct rpcrdma_req *req;
		struct rpcrdma_rep *rep;

		req = rpcrdma_create_req(r_xprt);
		if (IS_ERR(req)) {
			dprintk("RPC:       %s: request buffer %d alloc"
				" failed\n", __func__, i);
			rc = PTR_ERR(req);
			goto out;
		}
		buf->rb_send_bufs[i] = req;

		rep = rpcrdma_create_rep(r_xprt);
		if (IS_ERR(rep)) {
			dprintk("RPC:       %s: reply buffer %d alloc failed\n",
				__func__, i);
			rc = PTR_ERR(rep);
			goto out;
		}
		buf->rb_recv_bufs[i] = rep;
	}

	return 0;
out:
	rpcrdma_buffer_destroy(buf);
	return rc;
}

static void
rpcrdma_destroy_rep(struct rpcrdma_ia *ia, struct rpcrdma_rep *rep)
{
	if (!rep)
		return;

	rpcrdma_free_regbuf(ia, rep->rr_rdmabuf);
	kfree(rep);
}

static void
rpcrdma_destroy_req(struct rpcrdma_ia *ia, struct rpcrdma_req *req)
{
	if (!req)
		return;

	rpcrdma_free_regbuf(ia, req->rl_sendbuf);
	rpcrdma_free_regbuf(ia, req->rl_rdmabuf);
	kfree(req);
}

void
rpcrdma_buffer_destroy(struct rpcrdma_buffer *buf)
{
	struct rpcrdma_ia *ia = rdmab_to_ia(buf);
	int i;

	/* clean up in reverse order from create
	 *   1.  recv mr memory (mr free, then kfree)
	 *   2.  send mr memory (mr free, then kfree)
	 *   3.  MWs
	 */
	dprintk("RPC:       %s: entering\n", __func__);

	for (i = 0; i < buf->rb_max_requests; i++) {
		if (buf->rb_recv_bufs)
			rpcrdma_destroy_rep(ia, buf->rb_recv_bufs[i]);
		if (buf->rb_send_bufs)
			rpcrdma_destroy_req(ia, buf->rb_send_bufs[i]);
	}

	ia->ri_ops->ro_destroy(buf);

	kfree(buf->rb_pool);
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

static void
rpcrdma_buffer_put_sendbuf(struct rpcrdma_req *req, struct rpcrdma_buffer *buf)
{
	buf->rb_send_bufs[--buf->rb_send_index] = req;
	req->rl_niovs = 0;
	if (req->rl_reply) {
		buf->rb_recv_bufs[--buf->rb_recv_index] = req->rl_reply;
		req->rl_reply = NULL;
	}
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
	unsigned long flags;

	spin_lock_irqsave(&buffers->rb_lock, flags);
	rpcrdma_buffer_put_sendbuf(req, buffers);
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

	spin_lock_irqsave(&buffers->rb_lock, flags);
	if (buffers->rb_recv_index < buffers->rb_max_requests) {
		req->rl_reply = buffers->rb_recv_bufs[buffers->rb_recv_index];
		buffers->rb_recv_bufs[buffers->rb_recv_index++] = NULL;
	}
	spin_unlock_irqrestore(&buffers->rb_lock, flags);
}

/*
 * Put reply buffers back into pool when not attached to
 * request. This happens in error conditions.
 */
void
rpcrdma_recv_buffer_put(struct rpcrdma_rep *rep)
{
	struct rpcrdma_buffer *buffers = &rep->rr_rxprt->rx_buf;
	unsigned long flags;

	spin_lock_irqsave(&buffers->rb_lock, flags);
	buffers->rb_recv_bufs[--buffers->rb_recv_index] = rep;
	spin_unlock_irqrestore(&buffers->rb_lock, flags);
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
	send_wr.wr_id = RPCRDMA_IGNORE_COMPLETION;
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
	recv_wr.wr_id = (u64) (unsigned long) rep;
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

/* How many chunk list items fit within our inline buffers?
 */
unsigned int
rpcrdma_max_segments(struct rpcrdma_xprt *r_xprt)
{
	struct rpcrdma_create_data_internal *cdata = &r_xprt->rx_data;
	int bytes, segments;

	bytes = min_t(unsigned int, cdata->inline_wsize, cdata->inline_rsize);
	bytes -= RPCRDMA_HDRLEN_MIN;
	if (bytes < sizeof(struct rpcrdma_segment) * 2) {
		pr_warn("RPC:       %s: inline threshold too small\n",
			__func__);
		return 0;
	}

	segments = 1 << (fls(bytes / sizeof(struct rpcrdma_segment)) - 1);
	dprintk("RPC:       %s: max chunk list size = %d segments\n",
		__func__, segments);
	return segments;
}
