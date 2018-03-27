/*
 * Copyright (c) 2014 Open Grid Computing, Inc. All rights reserved.
 * Copyright (c) 2005-2007 Network Appliance, Inc. All rights reserved.
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
 *
 * Author: Tom Tucker <tom@opengridcomputing.com>
 */

#include <linux/sunrpc/svc_xprt.h>
#include <linux/sunrpc/addr.h>
#include <linux/sunrpc/debug.h>
#include <linux/sunrpc/rpc_rdma.h>
#include <linux/interrupt.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/workqueue.h>
#include <rdma/ib_verbs.h>
#include <rdma/rdma_cm.h>
#include <rdma/rw.h>
#include <linux/sunrpc/svc_rdma.h>
#include <linux/export.h>
#include "xprt_rdma.h"

#define RPCDBG_FACILITY	RPCDBG_SVCXPRT

static int svc_rdma_post_recv(struct svcxprt_rdma *xprt);
static struct svcxprt_rdma *rdma_create_xprt(struct svc_serv *, int);
static struct svc_xprt *svc_rdma_create(struct svc_serv *serv,
					struct net *net,
					struct sockaddr *sa, int salen,
					int flags);
static struct svc_xprt *svc_rdma_accept(struct svc_xprt *xprt);
static void svc_rdma_release_rqst(struct svc_rqst *);
static void svc_rdma_detach(struct svc_xprt *xprt);
static void svc_rdma_free(struct svc_xprt *xprt);
static int svc_rdma_has_wspace(struct svc_xprt *xprt);
static void svc_rdma_secure_port(struct svc_rqst *);
static void svc_rdma_kill_temp_xprt(struct svc_xprt *);

static const struct svc_xprt_ops svc_rdma_ops = {
	.xpo_create = svc_rdma_create,
	.xpo_recvfrom = svc_rdma_recvfrom,
	.xpo_sendto = svc_rdma_sendto,
	.xpo_release_rqst = svc_rdma_release_rqst,
	.xpo_detach = svc_rdma_detach,
	.xpo_free = svc_rdma_free,
	.xpo_prep_reply_hdr = svc_rdma_prep_reply_hdr,
	.xpo_has_wspace = svc_rdma_has_wspace,
	.xpo_accept = svc_rdma_accept,
	.xpo_secure_port = svc_rdma_secure_port,
	.xpo_kill_temp_xprt = svc_rdma_kill_temp_xprt,
};

struct svc_xprt_class svc_rdma_class = {
	.xcl_name = "rdma",
	.xcl_owner = THIS_MODULE,
	.xcl_ops = &svc_rdma_ops,
	.xcl_max_payload = RPCSVC_MAXPAYLOAD_RDMA,
	.xcl_ident = XPRT_TRANSPORT_RDMA,
};

#if defined(CONFIG_SUNRPC_BACKCHANNEL)
static struct svc_xprt *svc_rdma_bc_create(struct svc_serv *, struct net *,
					   struct sockaddr *, int, int);
static void svc_rdma_bc_detach(struct svc_xprt *);
static void svc_rdma_bc_free(struct svc_xprt *);

static const struct svc_xprt_ops svc_rdma_bc_ops = {
	.xpo_create = svc_rdma_bc_create,
	.xpo_detach = svc_rdma_bc_detach,
	.xpo_free = svc_rdma_bc_free,
	.xpo_prep_reply_hdr = svc_rdma_prep_reply_hdr,
	.xpo_secure_port = svc_rdma_secure_port,
};

struct svc_xprt_class svc_rdma_bc_class = {
	.xcl_name = "rdma-bc",
	.xcl_owner = THIS_MODULE,
	.xcl_ops = &svc_rdma_bc_ops,
	.xcl_max_payload = (1024 - RPCRDMA_HDRLEN_MIN)
};

static struct svc_xprt *svc_rdma_bc_create(struct svc_serv *serv,
					   struct net *net,
					   struct sockaddr *sa, int salen,
					   int flags)
{
	struct svcxprt_rdma *cma_xprt;
	struct svc_xprt *xprt;

	cma_xprt = rdma_create_xprt(serv, 0);
	if (!cma_xprt)
		return ERR_PTR(-ENOMEM);
	xprt = &cma_xprt->sc_xprt;

	svc_xprt_init(net, &svc_rdma_bc_class, xprt, serv);
	set_bit(XPT_CONG_CTRL, &xprt->xpt_flags);
	serv->sv_bc_xprt = xprt;

	dprintk("svcrdma: %s(%p)\n", __func__, xprt);
	return xprt;
}

static void svc_rdma_bc_detach(struct svc_xprt *xprt)
{
	dprintk("svcrdma: %s(%p)\n", __func__, xprt);
}

static void svc_rdma_bc_free(struct svc_xprt *xprt)
{
	struct svcxprt_rdma *rdma =
		container_of(xprt, struct svcxprt_rdma, sc_xprt);

	dprintk("svcrdma: %s(%p)\n", __func__, xprt);
	if (xprt)
		kfree(rdma);
}
#endif	/* CONFIG_SUNRPC_BACKCHANNEL */

static struct svc_rdma_op_ctxt *alloc_ctxt(struct svcxprt_rdma *xprt,
					   gfp_t flags)
{
	struct svc_rdma_op_ctxt *ctxt;

	ctxt = kmalloc(sizeof(*ctxt), flags);
	if (ctxt) {
		ctxt->xprt = xprt;
		INIT_LIST_HEAD(&ctxt->list);
	}
	return ctxt;
}

static bool svc_rdma_prealloc_ctxts(struct svcxprt_rdma *xprt)
{
	unsigned int i;

	/* Each RPC/RDMA credit can consume one Receive and
	 * one Send WQE at the same time.
	 */
	i = xprt->sc_sq_depth + xprt->sc_rq_depth;

	while (i--) {
		struct svc_rdma_op_ctxt *ctxt;

		ctxt = alloc_ctxt(xprt, GFP_KERNEL);
		if (!ctxt) {
			dprintk("svcrdma: No memory for RDMA ctxt\n");
			return false;
		}
		list_add(&ctxt->list, &xprt->sc_ctxts);
	}
	return true;
}

struct svc_rdma_op_ctxt *svc_rdma_get_context(struct svcxprt_rdma *xprt)
{
	struct svc_rdma_op_ctxt *ctxt = NULL;

	spin_lock(&xprt->sc_ctxt_lock);
	xprt->sc_ctxt_used++;
	if (list_empty(&xprt->sc_ctxts))
		goto out_empty;

	ctxt = list_first_entry(&xprt->sc_ctxts,
				struct svc_rdma_op_ctxt, list);
	list_del(&ctxt->list);
	spin_unlock(&xprt->sc_ctxt_lock);

out:
	ctxt->count = 0;
	ctxt->mapped_sges = 0;
	return ctxt;

out_empty:
	/* Either pre-allocation missed the mark, or send
	 * queue accounting is broken.
	 */
	spin_unlock(&xprt->sc_ctxt_lock);

	ctxt = alloc_ctxt(xprt, GFP_NOIO);
	if (ctxt)
		goto out;

	spin_lock(&xprt->sc_ctxt_lock);
	xprt->sc_ctxt_used--;
	spin_unlock(&xprt->sc_ctxt_lock);
	WARN_ONCE(1, "svcrdma: empty RDMA ctxt list?\n");
	return NULL;
}

void svc_rdma_unmap_dma(struct svc_rdma_op_ctxt *ctxt)
{
	struct svcxprt_rdma *xprt = ctxt->xprt;
	struct ib_device *device = xprt->sc_cm_id->device;
	unsigned int i;

	for (i = 0; i < ctxt->mapped_sges; i++)
		ib_dma_unmap_page(device,
				  ctxt->sge[i].addr,
				  ctxt->sge[i].length,
				  ctxt->direction);
	ctxt->mapped_sges = 0;
}

void svc_rdma_put_context(struct svc_rdma_op_ctxt *ctxt, int free_pages)
{
	struct svcxprt_rdma *xprt = ctxt->xprt;
	int i;

	if (free_pages)
		for (i = 0; i < ctxt->count; i++)
			put_page(ctxt->pages[i]);

	spin_lock(&xprt->sc_ctxt_lock);
	xprt->sc_ctxt_used--;
	list_add(&ctxt->list, &xprt->sc_ctxts);
	spin_unlock(&xprt->sc_ctxt_lock);
}

static void svc_rdma_destroy_ctxts(struct svcxprt_rdma *xprt)
{
	while (!list_empty(&xprt->sc_ctxts)) {
		struct svc_rdma_op_ctxt *ctxt;

		ctxt = list_first_entry(&xprt->sc_ctxts,
					struct svc_rdma_op_ctxt, list);
		list_del(&ctxt->list);
		kfree(ctxt);
	}
}

/* QP event handler */
static void qp_event_handler(struct ib_event *event, void *context)
{
	struct svc_xprt *xprt = context;

	switch (event->event) {
	/* These are considered benign events */
	case IB_EVENT_PATH_MIG:
	case IB_EVENT_COMM_EST:
	case IB_EVENT_SQ_DRAINED:
	case IB_EVENT_QP_LAST_WQE_REACHED:
		dprintk("svcrdma: QP event %s (%d) received for QP=%p\n",
			ib_event_msg(event->event), event->event,
			event->element.qp);
		break;
	/* These are considered fatal events */
	case IB_EVENT_PATH_MIG_ERR:
	case IB_EVENT_QP_FATAL:
	case IB_EVENT_QP_REQ_ERR:
	case IB_EVENT_QP_ACCESS_ERR:
	case IB_EVENT_DEVICE_FATAL:
	default:
		dprintk("svcrdma: QP ERROR event %s (%d) received for QP=%p, "
			"closing transport\n",
			ib_event_msg(event->event), event->event,
			event->element.qp);
		set_bit(XPT_CLOSE, &xprt->xpt_flags);
		svc_xprt_enqueue(xprt);
		break;
	}
}

/**
 * svc_rdma_wc_receive - Invoked by RDMA provider for each polled Receive WC
 * @cq:        completion queue
 * @wc:        completed WR
 *
 */
static void svc_rdma_wc_receive(struct ib_cq *cq, struct ib_wc *wc)
{
	struct svcxprt_rdma *xprt = cq->cq_context;
	struct ib_cqe *cqe = wc->wr_cqe;
	struct svc_rdma_op_ctxt *ctxt;

	/* WARNING: Only wc->wr_cqe and wc->status are reliable */
	ctxt = container_of(cqe, struct svc_rdma_op_ctxt, cqe);
	svc_rdma_unmap_dma(ctxt);

	if (wc->status != IB_WC_SUCCESS)
		goto flushed;

	/* All wc fields are now known to be valid */
	ctxt->byte_len = wc->byte_len;
	spin_lock(&xprt->sc_rq_dto_lock);
	list_add_tail(&ctxt->list, &xprt->sc_rq_dto_q);
	spin_unlock(&xprt->sc_rq_dto_lock);

	svc_rdma_post_recv(xprt);

	set_bit(XPT_DATA, &xprt->sc_xprt.xpt_flags);
	if (test_bit(RDMAXPRT_CONN_PENDING, &xprt->sc_flags))
		goto out;
	goto out_enqueue;

flushed:
	if (wc->status != IB_WC_WR_FLUSH_ERR)
		pr_err("svcrdma: Recv: %s (%u/0x%x)\n",
		       ib_wc_status_msg(wc->status),
		       wc->status, wc->vendor_err);
	set_bit(XPT_CLOSE, &xprt->sc_xprt.xpt_flags);
	svc_rdma_put_context(ctxt, 1);

out_enqueue:
	svc_xprt_enqueue(&xprt->sc_xprt);
out:
	svc_xprt_put(&xprt->sc_xprt);
}

/**
 * svc_rdma_wc_send - Invoked by RDMA provider for each polled Send WC
 * @cq:        completion queue
 * @wc:        completed WR
 *
 */
void svc_rdma_wc_send(struct ib_cq *cq, struct ib_wc *wc)
{
	struct svcxprt_rdma *xprt = cq->cq_context;
	struct ib_cqe *cqe = wc->wr_cqe;
	struct svc_rdma_op_ctxt *ctxt;

	atomic_inc(&xprt->sc_sq_avail);
	wake_up(&xprt->sc_send_wait);

	ctxt = container_of(cqe, struct svc_rdma_op_ctxt, cqe);
	svc_rdma_unmap_dma(ctxt);
	svc_rdma_put_context(ctxt, 1);

	if (unlikely(wc->status != IB_WC_SUCCESS)) {
		set_bit(XPT_CLOSE, &xprt->sc_xprt.xpt_flags);
		svc_xprt_enqueue(&xprt->sc_xprt);
		if (wc->status != IB_WC_WR_FLUSH_ERR)
			pr_err("svcrdma: Send: %s (%u/0x%x)\n",
			       ib_wc_status_msg(wc->status),
			       wc->status, wc->vendor_err);
	}

	svc_xprt_put(&xprt->sc_xprt);
}

static struct svcxprt_rdma *rdma_create_xprt(struct svc_serv *serv,
					     int listener)
{
	struct svcxprt_rdma *cma_xprt = kzalloc(sizeof *cma_xprt, GFP_KERNEL);

	if (!cma_xprt)
		return NULL;
	svc_xprt_init(&init_net, &svc_rdma_class, &cma_xprt->sc_xprt, serv);
	INIT_LIST_HEAD(&cma_xprt->sc_accept_q);
	INIT_LIST_HEAD(&cma_xprt->sc_rq_dto_q);
	INIT_LIST_HEAD(&cma_xprt->sc_read_complete_q);
	INIT_LIST_HEAD(&cma_xprt->sc_ctxts);
	INIT_LIST_HEAD(&cma_xprt->sc_rw_ctxts);
	init_waitqueue_head(&cma_xprt->sc_send_wait);

	spin_lock_init(&cma_xprt->sc_lock);
	spin_lock_init(&cma_xprt->sc_rq_dto_lock);
	spin_lock_init(&cma_xprt->sc_ctxt_lock);
	spin_lock_init(&cma_xprt->sc_rw_ctxt_lock);

	/*
	 * Note that this implies that the underlying transport support
	 * has some form of congestion control (see RFC 7530 section 3.1
	 * paragraph 2). For now, we assume that all supported RDMA
	 * transports are suitable here.
	 */
	set_bit(XPT_CONG_CTRL, &cma_xprt->sc_xprt.xpt_flags);

	if (listener)
		set_bit(XPT_LISTENER, &cma_xprt->sc_xprt.xpt_flags);

	return cma_xprt;
}

static int
svc_rdma_post_recv(struct svcxprt_rdma *xprt)
{
	struct ib_recv_wr recv_wr, *bad_recv_wr;
	struct svc_rdma_op_ctxt *ctxt;
	struct page *page;
	dma_addr_t pa;
	int sge_no;
	int buflen;
	int ret;

	ctxt = svc_rdma_get_context(xprt);
	buflen = 0;
	ctxt->direction = DMA_FROM_DEVICE;
	ctxt->cqe.done = svc_rdma_wc_receive;
	for (sge_no = 0; buflen < xprt->sc_max_req_size; sge_no++) {
		if (sge_no >= xprt->sc_max_sge) {
			pr_err("svcrdma: Too many sges (%d)\n", sge_no);
			goto err_put_ctxt;
		}
		page = alloc_page(GFP_KERNEL);
		if (!page)
			goto err_put_ctxt;
		ctxt->pages[sge_no] = page;
		pa = ib_dma_map_page(xprt->sc_cm_id->device,
				     page, 0, PAGE_SIZE,
				     DMA_FROM_DEVICE);
		if (ib_dma_mapping_error(xprt->sc_cm_id->device, pa))
			goto err_put_ctxt;
		svc_rdma_count_mappings(xprt, ctxt);
		ctxt->sge[sge_no].addr = pa;
		ctxt->sge[sge_no].length = PAGE_SIZE;
		ctxt->sge[sge_no].lkey = xprt->sc_pd->local_dma_lkey;
		ctxt->count = sge_no + 1;
		buflen += PAGE_SIZE;
	}
	recv_wr.next = NULL;
	recv_wr.sg_list = &ctxt->sge[0];
	recv_wr.num_sge = ctxt->count;
	recv_wr.wr_cqe = &ctxt->cqe;

	svc_xprt_get(&xprt->sc_xprt);
	ret = ib_post_recv(xprt->sc_qp, &recv_wr, &bad_recv_wr);
	if (ret) {
		svc_rdma_unmap_dma(ctxt);
		svc_rdma_put_context(ctxt, 1);
		svc_xprt_put(&xprt->sc_xprt);
	}
	return ret;

 err_put_ctxt:
	svc_rdma_unmap_dma(ctxt);
	svc_rdma_put_context(ctxt, 1);
	return -ENOMEM;
}

static void
svc_rdma_parse_connect_private(struct svcxprt_rdma *newxprt,
			       struct rdma_conn_param *param)
{
	const struct rpcrdma_connect_private *pmsg = param->private_data;

	if (pmsg &&
	    pmsg->cp_magic == rpcrdma_cmp_magic &&
	    pmsg->cp_version == RPCRDMA_CMP_VERSION) {
		newxprt->sc_snd_w_inv = pmsg->cp_flags &
					RPCRDMA_CMP_F_SND_W_INV_OK;

		dprintk("svcrdma: client send_size %u, recv_size %u "
			"remote inv %ssupported\n",
			rpcrdma_decode_buffer_size(pmsg->cp_send_size),
			rpcrdma_decode_buffer_size(pmsg->cp_recv_size),
			newxprt->sc_snd_w_inv ? "" : "un");
	}
}

/*
 * This function handles the CONNECT_REQUEST event on a listening
 * endpoint. It is passed the cma_id for the _new_ connection. The context in
 * this cma_id is inherited from the listening cma_id and is the svc_xprt
 * structure for the listening endpoint.
 *
 * This function creates a new xprt for the new connection and enqueues it on
 * the accept queue for the listent xprt. When the listen thread is kicked, it
 * will call the recvfrom method on the listen xprt which will accept the new
 * connection.
 */
static void handle_connect_req(struct rdma_cm_id *new_cma_id,
			       struct rdma_conn_param *param)
{
	struct svcxprt_rdma *listen_xprt = new_cma_id->context;
	struct svcxprt_rdma *newxprt;
	struct sockaddr *sa;

	/* Create a new transport */
	newxprt = rdma_create_xprt(listen_xprt->sc_xprt.xpt_server, 0);
	if (!newxprt) {
		dprintk("svcrdma: failed to create new transport\n");
		return;
	}
	newxprt->sc_cm_id = new_cma_id;
	new_cma_id->context = newxprt;
	dprintk("svcrdma: Creating newxprt=%p, cm_id=%p, listenxprt=%p\n",
		newxprt, newxprt->sc_cm_id, listen_xprt);
	svc_rdma_parse_connect_private(newxprt, param);

	/* Save client advertised inbound read limit for use later in accept. */
	newxprt->sc_ord = param->initiator_depth;

	/* Set the local and remote addresses in the transport */
	sa = (struct sockaddr *)&newxprt->sc_cm_id->route.addr.dst_addr;
	svc_xprt_set_remote(&newxprt->sc_xprt, sa, svc_addr_len(sa));
	sa = (struct sockaddr *)&newxprt->sc_cm_id->route.addr.src_addr;
	svc_xprt_set_local(&newxprt->sc_xprt, sa, svc_addr_len(sa));

	/*
	 * Enqueue the new transport on the accept queue of the listening
	 * transport
	 */
	spin_lock_bh(&listen_xprt->sc_lock);
	list_add_tail(&newxprt->sc_accept_q, &listen_xprt->sc_accept_q);
	spin_unlock_bh(&listen_xprt->sc_lock);

	set_bit(XPT_CONN, &listen_xprt->sc_xprt.xpt_flags);
	svc_xprt_enqueue(&listen_xprt->sc_xprt);
}

/*
 * Handles events generated on the listening endpoint. These events will be
 * either be incoming connect requests or adapter removal  events.
 */
static int rdma_listen_handler(struct rdma_cm_id *cma_id,
			       struct rdma_cm_event *event)
{
	struct svcxprt_rdma *xprt = cma_id->context;
	int ret = 0;

	switch (event->event) {
	case RDMA_CM_EVENT_CONNECT_REQUEST:
		dprintk("svcrdma: Connect request on cma_id=%p, xprt = %p, "
			"event = %s (%d)\n", cma_id, cma_id->context,
			rdma_event_msg(event->event), event->event);
		handle_connect_req(cma_id, &event->param.conn);
		break;

	case RDMA_CM_EVENT_ESTABLISHED:
		/* Accept complete */
		dprintk("svcrdma: Connection completed on LISTEN xprt=%p, "
			"cm_id=%p\n", xprt, cma_id);
		break;

	case RDMA_CM_EVENT_DEVICE_REMOVAL:
		dprintk("svcrdma: Device removal xprt=%p, cm_id=%p\n",
			xprt, cma_id);
		if (xprt) {
			set_bit(XPT_CLOSE, &xprt->sc_xprt.xpt_flags);
			svc_xprt_enqueue(&xprt->sc_xprt);
		}
		break;

	default:
		dprintk("svcrdma: Unexpected event on listening endpoint %p, "
			"event = %s (%d)\n", cma_id,
			rdma_event_msg(event->event), event->event);
		break;
	}

	return ret;
}

static int rdma_cma_handler(struct rdma_cm_id *cma_id,
			    struct rdma_cm_event *event)
{
	struct svc_xprt *xprt = cma_id->context;
	struct svcxprt_rdma *rdma =
		container_of(xprt, struct svcxprt_rdma, sc_xprt);
	switch (event->event) {
	case RDMA_CM_EVENT_ESTABLISHED:
		/* Accept complete */
		svc_xprt_get(xprt);
		dprintk("svcrdma: Connection completed on DTO xprt=%p, "
			"cm_id=%p\n", xprt, cma_id);
		clear_bit(RDMAXPRT_CONN_PENDING, &rdma->sc_flags);
		svc_xprt_enqueue(xprt);
		break;
	case RDMA_CM_EVENT_DISCONNECTED:
		dprintk("svcrdma: Disconnect on DTO xprt=%p, cm_id=%p\n",
			xprt, cma_id);
		if (xprt) {
			set_bit(XPT_CLOSE, &xprt->xpt_flags);
			svc_xprt_enqueue(xprt);
			svc_xprt_put(xprt);
		}
		break;
	case RDMA_CM_EVENT_DEVICE_REMOVAL:
		dprintk("svcrdma: Device removal cma_id=%p, xprt = %p, "
			"event = %s (%d)\n", cma_id, xprt,
			rdma_event_msg(event->event), event->event);
		if (xprt) {
			set_bit(XPT_CLOSE, &xprt->xpt_flags);
			svc_xprt_enqueue(xprt);
			svc_xprt_put(xprt);
		}
		break;
	default:
		dprintk("svcrdma: Unexpected event on DTO endpoint %p, "
			"event = %s (%d)\n", cma_id,
			rdma_event_msg(event->event), event->event);
		break;
	}
	return 0;
}

/*
 * Create a listening RDMA service endpoint.
 */
static struct svc_xprt *svc_rdma_create(struct svc_serv *serv,
					struct net *net,
					struct sockaddr *sa, int salen,
					int flags)
{
	struct rdma_cm_id *listen_id;
	struct svcxprt_rdma *cma_xprt;
	int ret;

	dprintk("svcrdma: Creating RDMA socket\n");
	if ((sa->sa_family != AF_INET) && (sa->sa_family != AF_INET6)) {
		dprintk("svcrdma: Address family %d is not supported.\n", sa->sa_family);
		return ERR_PTR(-EAFNOSUPPORT);
	}
	cma_xprt = rdma_create_xprt(serv, 1);
	if (!cma_xprt)
		return ERR_PTR(-ENOMEM);

	listen_id = rdma_create_id(&init_net, rdma_listen_handler, cma_xprt,
				   RDMA_PS_TCP, IB_QPT_RC);
	if (IS_ERR(listen_id)) {
		ret = PTR_ERR(listen_id);
		dprintk("svcrdma: rdma_create_id failed = %d\n", ret);
		goto err0;
	}

	/* Allow both IPv4 and IPv6 sockets to bind a single port
	 * at the same time.
	 */
#if IS_ENABLED(CONFIG_IPV6)
	ret = rdma_set_afonly(listen_id, 1);
	if (ret) {
		dprintk("svcrdma: rdma_set_afonly failed = %d\n", ret);
		goto err1;
	}
#endif
	ret = rdma_bind_addr(listen_id, sa);
	if (ret) {
		dprintk("svcrdma: rdma_bind_addr failed = %d\n", ret);
		goto err1;
	}
	cma_xprt->sc_cm_id = listen_id;

	ret = rdma_listen(listen_id, RPCRDMA_LISTEN_BACKLOG);
	if (ret) {
		dprintk("svcrdma: rdma_listen failed = %d\n", ret);
		goto err1;
	}

	/*
	 * We need to use the address from the cm_id in case the
	 * caller specified 0 for the port number.
	 */
	sa = (struct sockaddr *)&cma_xprt->sc_cm_id->route.addr.src_addr;
	svc_xprt_set_local(&cma_xprt->sc_xprt, sa, salen);

	return &cma_xprt->sc_xprt;

 err1:
	rdma_destroy_id(listen_id);
 err0:
	kfree(cma_xprt);
	return ERR_PTR(ret);
}

/*
 * This is the xpo_recvfrom function for listening endpoints. Its
 * purpose is to accept incoming connections. The CMA callback handler
 * has already created a new transport and attached it to the new CMA
 * ID.
 *
 * There is a queue of pending connections hung on the listening
 * transport. This queue contains the new svc_xprt structure. This
 * function takes svc_xprt structures off the accept_q and completes
 * the connection.
 */
static struct svc_xprt *svc_rdma_accept(struct svc_xprt *xprt)
{
	struct svcxprt_rdma *listen_rdma;
	struct svcxprt_rdma *newxprt = NULL;
	struct rdma_conn_param conn_param;
	struct rpcrdma_connect_private pmsg;
	struct ib_qp_init_attr qp_attr;
	struct ib_device *dev;
	struct sockaddr *sap;
	unsigned int i, ctxts;
	int ret = 0;

	listen_rdma = container_of(xprt, struct svcxprt_rdma, sc_xprt);
	clear_bit(XPT_CONN, &xprt->xpt_flags);
	/* Get the next entry off the accept list */
	spin_lock_bh(&listen_rdma->sc_lock);
	if (!list_empty(&listen_rdma->sc_accept_q)) {
		newxprt = list_entry(listen_rdma->sc_accept_q.next,
				     struct svcxprt_rdma, sc_accept_q);
		list_del_init(&newxprt->sc_accept_q);
	}
	if (!list_empty(&listen_rdma->sc_accept_q))
		set_bit(XPT_CONN, &listen_rdma->sc_xprt.xpt_flags);
	spin_unlock_bh(&listen_rdma->sc_lock);
	if (!newxprt)
		return NULL;

	dprintk("svcrdma: newxprt from accept queue = %p, cm_id=%p\n",
		newxprt, newxprt->sc_cm_id);

	dev = newxprt->sc_cm_id->device;
	newxprt->sc_port_num = newxprt->sc_cm_id->port_num;

	/* Qualify the transport resource defaults with the
	 * capabilities of this particular device */
	newxprt->sc_max_sge = min((size_t)dev->attrs.max_sge,
				  (size_t)RPCSVC_MAXPAGES);
	newxprt->sc_max_req_size = svcrdma_max_req_size;
	newxprt->sc_max_requests = svcrdma_max_requests;
	newxprt->sc_max_bc_requests = svcrdma_max_bc_requests;
	newxprt->sc_rq_depth = newxprt->sc_max_requests +
			       newxprt->sc_max_bc_requests;
	if (newxprt->sc_rq_depth > dev->attrs.max_qp_wr) {
		pr_warn("svcrdma: reducing receive depth to %d\n",
			dev->attrs.max_qp_wr);
		newxprt->sc_rq_depth = dev->attrs.max_qp_wr;
		newxprt->sc_max_requests = newxprt->sc_rq_depth - 2;
		newxprt->sc_max_bc_requests = 2;
	}
	newxprt->sc_fc_credits = cpu_to_be32(newxprt->sc_max_requests);
	ctxts = rdma_rw_mr_factor(dev, newxprt->sc_port_num, RPCSVC_MAXPAGES);
	ctxts *= newxprt->sc_max_requests;
	newxprt->sc_sq_depth = newxprt->sc_rq_depth + ctxts;
	if (newxprt->sc_sq_depth > dev->attrs.max_qp_wr) {
		pr_warn("svcrdma: reducing send depth to %d\n",
			dev->attrs.max_qp_wr);
		newxprt->sc_sq_depth = dev->attrs.max_qp_wr;
	}
	atomic_set(&newxprt->sc_sq_avail, newxprt->sc_sq_depth);

	if (!svc_rdma_prealloc_ctxts(newxprt))
		goto errout;

	newxprt->sc_pd = ib_alloc_pd(dev, 0);
	if (IS_ERR(newxprt->sc_pd)) {
		dprintk("svcrdma: error creating PD for connect request\n");
		goto errout;
	}
	newxprt->sc_sq_cq = ib_alloc_cq(dev, newxprt, newxprt->sc_sq_depth,
					0, IB_POLL_WORKQUEUE);
	if (IS_ERR(newxprt->sc_sq_cq)) {
		dprintk("svcrdma: error creating SQ CQ for connect request\n");
		goto errout;
	}
	newxprt->sc_rq_cq = ib_alloc_cq(dev, newxprt, newxprt->sc_rq_depth,
					0, IB_POLL_WORKQUEUE);
	if (IS_ERR(newxprt->sc_rq_cq)) {
		dprintk("svcrdma: error creating RQ CQ for connect request\n");
		goto errout;
	}

	memset(&qp_attr, 0, sizeof qp_attr);
	qp_attr.event_handler = qp_event_handler;
	qp_attr.qp_context = &newxprt->sc_xprt;
	qp_attr.port_num = newxprt->sc_port_num;
	qp_attr.cap.max_rdma_ctxs = ctxts;
	qp_attr.cap.max_send_wr = newxprt->sc_sq_depth - ctxts;
	qp_attr.cap.max_recv_wr = newxprt->sc_rq_depth;
	qp_attr.cap.max_send_sge = newxprt->sc_max_sge;
	qp_attr.cap.max_recv_sge = newxprt->sc_max_sge;
	qp_attr.sq_sig_type = IB_SIGNAL_REQ_WR;
	qp_attr.qp_type = IB_QPT_RC;
	qp_attr.send_cq = newxprt->sc_sq_cq;
	qp_attr.recv_cq = newxprt->sc_rq_cq;
	dprintk("svcrdma: newxprt->sc_cm_id=%p, newxprt->sc_pd=%p\n",
		newxprt->sc_cm_id, newxprt->sc_pd);
	dprintk("    cap.max_send_wr = %d, cap.max_recv_wr = %d\n",
		qp_attr.cap.max_send_wr, qp_attr.cap.max_recv_wr);
	dprintk("    cap.max_send_sge = %d, cap.max_recv_sge = %d\n",
		qp_attr.cap.max_send_sge, qp_attr.cap.max_recv_sge);

	ret = rdma_create_qp(newxprt->sc_cm_id, newxprt->sc_pd, &qp_attr);
	if (ret) {
		dprintk("svcrdma: failed to create QP, ret=%d\n", ret);
		goto errout;
	}
	newxprt->sc_qp = newxprt->sc_cm_id->qp;

	if (!(dev->attrs.device_cap_flags & IB_DEVICE_MEM_MGT_EXTENSIONS))
		newxprt->sc_snd_w_inv = false;
	if (!rdma_protocol_iwarp(dev, newxprt->sc_port_num) &&
	    !rdma_ib_or_roce(dev, newxprt->sc_port_num))
		goto errout;

	/* Post receive buffers */
	for (i = 0; i < newxprt->sc_max_requests; i++) {
		ret = svc_rdma_post_recv(newxprt);
		if (ret) {
			dprintk("svcrdma: failure posting receive buffers\n");
			goto errout;
		}
	}

	/* Swap out the handler */
	newxprt->sc_cm_id->event_handler = rdma_cma_handler;

	/* Construct RDMA-CM private message */
	pmsg.cp_magic = rpcrdma_cmp_magic;
	pmsg.cp_version = RPCRDMA_CMP_VERSION;
	pmsg.cp_flags = 0;
	pmsg.cp_send_size = pmsg.cp_recv_size =
		rpcrdma_encode_buffer_size(newxprt->sc_max_req_size);

	/* Accept Connection */
	set_bit(RDMAXPRT_CONN_PENDING, &newxprt->sc_flags);
	memset(&conn_param, 0, sizeof conn_param);
	conn_param.responder_resources = 0;
	conn_param.initiator_depth = min_t(int, newxprt->sc_ord,
					   dev->attrs.max_qp_init_rd_atom);
	if (!conn_param.initiator_depth) {
		dprintk("svcrdma: invalid ORD setting\n");
		ret = -EINVAL;
		goto errout;
	}
	conn_param.private_data = &pmsg;
	conn_param.private_data_len = sizeof(pmsg);
	ret = rdma_accept(newxprt->sc_cm_id, &conn_param);
	if (ret)
		goto errout;

	dprintk("svcrdma: new connection %p accepted:\n", newxprt);
	sap = (struct sockaddr *)&newxprt->sc_cm_id->route.addr.src_addr;
	dprintk("    local address   : %pIS:%u\n", sap, rpc_get_port(sap));
	sap = (struct sockaddr *)&newxprt->sc_cm_id->route.addr.dst_addr;
	dprintk("    remote address  : %pIS:%u\n", sap, rpc_get_port(sap));
	dprintk("    max_sge         : %d\n", newxprt->sc_max_sge);
	dprintk("    sq_depth        : %d\n", newxprt->sc_sq_depth);
	dprintk("    rdma_rw_ctxs    : %d\n", ctxts);
	dprintk("    max_requests    : %d\n", newxprt->sc_max_requests);
	dprintk("    ord             : %d\n", conn_param.initiator_depth);

	return &newxprt->sc_xprt;

 errout:
	dprintk("svcrdma: failure accepting new connection rc=%d.\n", ret);
	/* Take a reference in case the DTO handler runs */
	svc_xprt_get(&newxprt->sc_xprt);
	if (newxprt->sc_qp && !IS_ERR(newxprt->sc_qp))
		ib_destroy_qp(newxprt->sc_qp);
	rdma_destroy_id(newxprt->sc_cm_id);
	/* This call to put will destroy the transport */
	svc_xprt_put(&newxprt->sc_xprt);
	return NULL;
}

static void svc_rdma_release_rqst(struct svc_rqst *rqstp)
{
}

/*
 * When connected, an svc_xprt has at least two references:
 *
 * - A reference held by the cm_id between the ESTABLISHED and
 *   DISCONNECTED events. If the remote peer disconnected first, this
 *   reference could be gone.
 *
 * - A reference held by the svc_recv code that called this function
 *   as part of close processing.
 *
 * At a minimum one references should still be held.
 */
static void svc_rdma_detach(struct svc_xprt *xprt)
{
	struct svcxprt_rdma *rdma =
		container_of(xprt, struct svcxprt_rdma, sc_xprt);
	dprintk("svc: svc_rdma_detach(%p)\n", xprt);

	/* Disconnect and flush posted WQE */
	rdma_disconnect(rdma->sc_cm_id);
}

static void __svc_rdma_free(struct work_struct *work)
{
	struct svcxprt_rdma *rdma =
		container_of(work, struct svcxprt_rdma, sc_work);
	struct svc_xprt *xprt = &rdma->sc_xprt;

	dprintk("svcrdma: %s(%p)\n", __func__, rdma);

	if (rdma->sc_qp && !IS_ERR(rdma->sc_qp))
		ib_drain_qp(rdma->sc_qp);

	/* We should only be called from kref_put */
	if (kref_read(&xprt->xpt_ref) != 0)
		pr_err("svcrdma: sc_xprt still in use? (%d)\n",
		       kref_read(&xprt->xpt_ref));

	while (!list_empty(&rdma->sc_read_complete_q)) {
		struct svc_rdma_op_ctxt *ctxt;
		ctxt = list_first_entry(&rdma->sc_read_complete_q,
					struct svc_rdma_op_ctxt, list);
		list_del(&ctxt->list);
		svc_rdma_put_context(ctxt, 1);
	}
	while (!list_empty(&rdma->sc_rq_dto_q)) {
		struct svc_rdma_op_ctxt *ctxt;
		ctxt = list_first_entry(&rdma->sc_rq_dto_q,
					struct svc_rdma_op_ctxt, list);
		list_del(&ctxt->list);
		svc_rdma_put_context(ctxt, 1);
	}

	/* Warn if we leaked a resource or under-referenced */
	if (rdma->sc_ctxt_used != 0)
		pr_err("svcrdma: ctxt still in use? (%d)\n",
		       rdma->sc_ctxt_used);

	/* Final put of backchannel client transport */
	if (xprt->xpt_bc_xprt) {
		xprt_put(xprt->xpt_bc_xprt);
		xprt->xpt_bc_xprt = NULL;
	}

	svc_rdma_destroy_rw_ctxts(rdma);
	svc_rdma_destroy_ctxts(rdma);

	/* Destroy the QP if present (not a listener) */
	if (rdma->sc_qp && !IS_ERR(rdma->sc_qp))
		ib_destroy_qp(rdma->sc_qp);

	if (rdma->sc_sq_cq && !IS_ERR(rdma->sc_sq_cq))
		ib_free_cq(rdma->sc_sq_cq);

	if (rdma->sc_rq_cq && !IS_ERR(rdma->sc_rq_cq))
		ib_free_cq(rdma->sc_rq_cq);

	if (rdma->sc_pd && !IS_ERR(rdma->sc_pd))
		ib_dealloc_pd(rdma->sc_pd);

	/* Destroy the CM ID */
	rdma_destroy_id(rdma->sc_cm_id);

	kfree(rdma);
}

static void svc_rdma_free(struct svc_xprt *xprt)
{
	struct svcxprt_rdma *rdma =
		container_of(xprt, struct svcxprt_rdma, sc_xprt);
	INIT_WORK(&rdma->sc_work, __svc_rdma_free);
	queue_work(svc_rdma_wq, &rdma->sc_work);
}

static int svc_rdma_has_wspace(struct svc_xprt *xprt)
{
	struct svcxprt_rdma *rdma =
		container_of(xprt, struct svcxprt_rdma, sc_xprt);

	/*
	 * If there are already waiters on the SQ,
	 * return false.
	 */
	if (waitqueue_active(&rdma->sc_send_wait))
		return 0;

	/* Otherwise return true. */
	return 1;
}

static void svc_rdma_secure_port(struct svc_rqst *rqstp)
{
	set_bit(RQ_SECURE, &rqstp->rq_flags);
}

static void svc_rdma_kill_temp_xprt(struct svc_xprt *xprt)
{
}

int svc_rdma_send(struct svcxprt_rdma *xprt, struct ib_send_wr *wr)
{
	struct ib_send_wr *bad_wr, *n_wr;
	int wr_count;
	int i;
	int ret;

	if (test_bit(XPT_CLOSE, &xprt->sc_xprt.xpt_flags))
		return -ENOTCONN;

	wr_count = 1;
	for (n_wr = wr->next; n_wr; n_wr = n_wr->next)
		wr_count++;

	/* If the SQ is full, wait until an SQ entry is available */
	while (1) {
		if ((atomic_sub_return(wr_count, &xprt->sc_sq_avail) < 0)) {
			atomic_inc(&rdma_stat_sq_starve);

			/* Wait until SQ WR available if SQ still full */
			atomic_add(wr_count, &xprt->sc_sq_avail);
			wait_event(xprt->sc_send_wait,
				   atomic_read(&xprt->sc_sq_avail) > wr_count);
			if (test_bit(XPT_CLOSE, &xprt->sc_xprt.xpt_flags))
				return -ENOTCONN;
			continue;
		}
		/* Take a transport ref for each WR posted */
		for (i = 0; i < wr_count; i++)
			svc_xprt_get(&xprt->sc_xprt);

		/* Bump used SQ WR count and post */
		ret = ib_post_send(xprt->sc_qp, wr, &bad_wr);
		if (ret) {
			set_bit(XPT_CLOSE, &xprt->sc_xprt.xpt_flags);
			for (i = 0; i < wr_count; i ++)
				svc_xprt_put(&xprt->sc_xprt);
			dprintk("svcrdma: failed to post SQ WR rc=%d\n", ret);
			dprintk("    sc_sq_avail=%d, sc_sq_depth=%d\n",
				atomic_read(&xprt->sc_sq_avail),
				xprt->sc_sq_depth);
			wake_up(&xprt->sc_send_wait);
		}
		break;
	}
	return ret;
}
