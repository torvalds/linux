/*
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
#include <linux/sunrpc/debug.h>
#include <linux/sunrpc/rpc_rdma.h>
#include <linux/spinlock.h>
#include <rdma/ib_verbs.h>
#include <rdma/rdma_cm.h>
#include <linux/sunrpc/svc_rdma.h>

#define RPCDBG_FACILITY	RPCDBG_SVCXPRT

static struct svc_xprt *svc_rdma_create(struct svc_serv *serv,
					struct sockaddr *sa, int salen,
					int flags);
static struct svc_xprt *svc_rdma_accept(struct svc_xprt *xprt);
static void svc_rdma_release_rqst(struct svc_rqst *);
static void dto_tasklet_func(unsigned long data);
static void svc_rdma_detach(struct svc_xprt *xprt);
static void svc_rdma_free(struct svc_xprt *xprt);
static int svc_rdma_has_wspace(struct svc_xprt *xprt);
static void rq_cq_reap(struct svcxprt_rdma *xprt);
static void sq_cq_reap(struct svcxprt_rdma *xprt);

DECLARE_TASKLET(dto_tasklet, dto_tasklet_func, 0UL);
static DEFINE_SPINLOCK(dto_lock);
static LIST_HEAD(dto_xprt_q);

static struct svc_xprt_ops svc_rdma_ops = {
	.xpo_create = svc_rdma_create,
	.xpo_recvfrom = svc_rdma_recvfrom,
	.xpo_sendto = svc_rdma_sendto,
	.xpo_release_rqst = svc_rdma_release_rqst,
	.xpo_detach = svc_rdma_detach,
	.xpo_free = svc_rdma_free,
	.xpo_prep_reply_hdr = svc_rdma_prep_reply_hdr,
	.xpo_has_wspace = svc_rdma_has_wspace,
	.xpo_accept = svc_rdma_accept,
};

struct svc_xprt_class svc_rdma_class = {
	.xcl_name = "rdma",
	.xcl_owner = THIS_MODULE,
	.xcl_ops = &svc_rdma_ops,
	.xcl_max_payload = RPCSVC_MAXPAYLOAD_TCP,
};

static int rdma_bump_context_cache(struct svcxprt_rdma *xprt)
{
	int target;
	int at_least_one = 0;
	struct svc_rdma_op_ctxt *ctxt;

	target = min(xprt->sc_ctxt_cnt + xprt->sc_ctxt_bump,
		     xprt->sc_ctxt_max);

	spin_lock_bh(&xprt->sc_ctxt_lock);
	while (xprt->sc_ctxt_cnt < target) {
		xprt->sc_ctxt_cnt++;
		spin_unlock_bh(&xprt->sc_ctxt_lock);

		ctxt = kmalloc(sizeof(*ctxt), GFP_KERNEL);

		spin_lock_bh(&xprt->sc_ctxt_lock);
		if (ctxt) {
			at_least_one = 1;
			ctxt->next = xprt->sc_ctxt_head;
			xprt->sc_ctxt_head = ctxt;
		} else {
			/* kmalloc failed...give up for now */
			xprt->sc_ctxt_cnt--;
			break;
		}
	}
	spin_unlock_bh(&xprt->sc_ctxt_lock);
	dprintk("svcrdma: sc_ctxt_max=%d, sc_ctxt_cnt=%d\n",
		xprt->sc_ctxt_max, xprt->sc_ctxt_cnt);
	return at_least_one;
}

struct svc_rdma_op_ctxt *svc_rdma_get_context(struct svcxprt_rdma *xprt)
{
	struct svc_rdma_op_ctxt *ctxt;

	while (1) {
		spin_lock_bh(&xprt->sc_ctxt_lock);
		if (unlikely(xprt->sc_ctxt_head == NULL)) {
			/* Try to bump my cache. */
			spin_unlock_bh(&xprt->sc_ctxt_lock);

			if (rdma_bump_context_cache(xprt))
				continue;

			printk(KERN_INFO "svcrdma: sleeping waiting for "
			       "context memory on xprt=%p\n",
			       xprt);
			schedule_timeout_uninterruptible(msecs_to_jiffies(500));
			continue;
		}
		ctxt = xprt->sc_ctxt_head;
		xprt->sc_ctxt_head = ctxt->next;
		spin_unlock_bh(&xprt->sc_ctxt_lock);
		ctxt->xprt = xprt;
		INIT_LIST_HEAD(&ctxt->dto_q);
		ctxt->count = 0;
		break;
	}
	return ctxt;
}

void svc_rdma_put_context(struct svc_rdma_op_ctxt *ctxt, int free_pages)
{
	struct svcxprt_rdma *xprt;
	int i;

	BUG_ON(!ctxt);
	xprt = ctxt->xprt;
	if (free_pages)
		for (i = 0; i < ctxt->count; i++)
			put_page(ctxt->pages[i]);

	for (i = 0; i < ctxt->count; i++)
		dma_unmap_single(xprt->sc_cm_id->device->dma_device,
				 ctxt->sge[i].addr,
				 ctxt->sge[i].length,
				 ctxt->direction);
	spin_lock_bh(&xprt->sc_ctxt_lock);
	ctxt->next = xprt->sc_ctxt_head;
	xprt->sc_ctxt_head = ctxt;
	spin_unlock_bh(&xprt->sc_ctxt_lock);
}

/* ib_cq event handler */
static void cq_event_handler(struct ib_event *event, void *context)
{
	struct svc_xprt *xprt = context;
	dprintk("svcrdma: received CQ event id=%d, context=%p\n",
		event->event, context);
	set_bit(XPT_CLOSE, &xprt->xpt_flags);
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
		dprintk("svcrdma: QP event %d received for QP=%p\n",
			event->event, event->element.qp);
		break;
	/* These are considered fatal events */
	case IB_EVENT_PATH_MIG_ERR:
	case IB_EVENT_QP_FATAL:
	case IB_EVENT_QP_REQ_ERR:
	case IB_EVENT_QP_ACCESS_ERR:
	case IB_EVENT_DEVICE_FATAL:
	default:
		dprintk("svcrdma: QP ERROR event %d received for QP=%p, "
			"closing transport\n",
			event->event, event->element.qp);
		set_bit(XPT_CLOSE, &xprt->xpt_flags);
		break;
	}
}

/*
 * Data Transfer Operation Tasklet
 *
 * Walks a list of transports with I/O pending, removing entries as
 * they are added to the server's I/O pending list. Two bits indicate
 * if SQ, RQ, or both have I/O pending. The dto_lock is an irqsave
 * spinlock that serializes access to the transport list with the RQ
 * and SQ interrupt handlers.
 */
static void dto_tasklet_func(unsigned long data)
{
	struct svcxprt_rdma *xprt;
	unsigned long flags;

	spin_lock_irqsave(&dto_lock, flags);
	while (!list_empty(&dto_xprt_q)) {
		xprt = list_entry(dto_xprt_q.next,
				  struct svcxprt_rdma, sc_dto_q);
		list_del_init(&xprt->sc_dto_q);
		spin_unlock_irqrestore(&dto_lock, flags);

		if (test_and_clear_bit(RDMAXPRT_RQ_PENDING, &xprt->sc_flags)) {
			ib_req_notify_cq(xprt->sc_rq_cq, IB_CQ_NEXT_COMP);
			rq_cq_reap(xprt);
			set_bit(XPT_DATA, &xprt->sc_xprt.xpt_flags);
			/*
			 * If data arrived before established event,
			 * don't enqueue. This defers RPC I/O until the
			 * RDMA connection is complete.
			 */
			if (!test_bit(RDMAXPRT_CONN_PENDING, &xprt->sc_flags))
				svc_xprt_enqueue(&xprt->sc_xprt);
		}

		if (test_and_clear_bit(RDMAXPRT_SQ_PENDING, &xprt->sc_flags)) {
			ib_req_notify_cq(xprt->sc_sq_cq, IB_CQ_NEXT_COMP);
			sq_cq_reap(xprt);
		}

		svc_xprt_put(&xprt->sc_xprt);
		spin_lock_irqsave(&dto_lock, flags);
	}
	spin_unlock_irqrestore(&dto_lock, flags);
}

/*
 * Receive Queue Completion Handler
 *
 * Since an RQ completion handler is called on interrupt context, we
 * need to defer the handling of the I/O to a tasklet
 */
static void rq_comp_handler(struct ib_cq *cq, void *cq_context)
{
	struct svcxprt_rdma *xprt = cq_context;
	unsigned long flags;

	/*
	 * Set the bit regardless of whether or not it's on the list
	 * because it may be on the list already due to an SQ
	 * completion.
	*/
	set_bit(RDMAXPRT_RQ_PENDING, &xprt->sc_flags);

	/*
	 * If this transport is not already on the DTO transport queue,
	 * add it
	 */
	spin_lock_irqsave(&dto_lock, flags);
	if (list_empty(&xprt->sc_dto_q)) {
		svc_xprt_get(&xprt->sc_xprt);
		list_add_tail(&xprt->sc_dto_q, &dto_xprt_q);
	}
	spin_unlock_irqrestore(&dto_lock, flags);

	/* Tasklet does all the work to avoid irqsave locks. */
	tasklet_schedule(&dto_tasklet);
}

/*
 * rq_cq_reap - Process the RQ CQ.
 *
 * Take all completing WC off the CQE and enqueue the associated DTO
 * context on the dto_q for the transport.
 */
static void rq_cq_reap(struct svcxprt_rdma *xprt)
{
	int ret;
	struct ib_wc wc;
	struct svc_rdma_op_ctxt *ctxt = NULL;

	atomic_inc(&rdma_stat_rq_poll);

	spin_lock_bh(&xprt->sc_rq_dto_lock);
	while ((ret = ib_poll_cq(xprt->sc_rq_cq, 1, &wc)) > 0) {
		ctxt = (struct svc_rdma_op_ctxt *)(unsigned long)wc.wr_id;
		ctxt->wc_status = wc.status;
		ctxt->byte_len = wc.byte_len;
		if (wc.status != IB_WC_SUCCESS) {
			/* Close the transport */
			set_bit(XPT_CLOSE, &xprt->sc_xprt.xpt_flags);
			svc_rdma_put_context(ctxt, 1);
			continue;
		}
		list_add_tail(&ctxt->dto_q, &xprt->sc_rq_dto_q);
	}
	spin_unlock_bh(&xprt->sc_rq_dto_lock);

	if (ctxt)
		atomic_inc(&rdma_stat_rq_prod);
}

/*
 * Send Queue Completion Handler - potentially called on interrupt context.
 */
static void sq_cq_reap(struct svcxprt_rdma *xprt)
{
	struct svc_rdma_op_ctxt *ctxt = NULL;
	struct ib_wc wc;
	struct ib_cq *cq = xprt->sc_sq_cq;
	int ret;

	atomic_inc(&rdma_stat_sq_poll);
	while ((ret = ib_poll_cq(cq, 1, &wc)) > 0) {
		ctxt = (struct svc_rdma_op_ctxt *)(unsigned long)wc.wr_id;
		xprt = ctxt->xprt;

		if (wc.status != IB_WC_SUCCESS)
			/* Close the transport */
			set_bit(XPT_CLOSE, &xprt->sc_xprt.xpt_flags);

		/* Decrement used SQ WR count */
		atomic_dec(&xprt->sc_sq_count);
		wake_up(&xprt->sc_send_wait);

		switch (ctxt->wr_op) {
		case IB_WR_SEND:
		case IB_WR_RDMA_WRITE:
			svc_rdma_put_context(ctxt, 1);
			break;

		case IB_WR_RDMA_READ:
			if (test_bit(RDMACTXT_F_LAST_CTXT, &ctxt->flags)) {
				set_bit(XPT_DATA, &xprt->sc_xprt.xpt_flags);
				set_bit(RDMACTXT_F_READ_DONE, &ctxt->flags);
				spin_lock_bh(&xprt->sc_read_complete_lock);
				list_add_tail(&ctxt->dto_q,
					      &xprt->sc_read_complete_q);
				spin_unlock_bh(&xprt->sc_read_complete_lock);
				svc_xprt_enqueue(&xprt->sc_xprt);
			}
			break;

		default:
			printk(KERN_ERR "svcrdma: unexpected completion type, "
			       "opcode=%d, status=%d\n",
			       wc.opcode, wc.status);
			break;
		}
	}

	if (ctxt)
		atomic_inc(&rdma_stat_sq_prod);
}

static void sq_comp_handler(struct ib_cq *cq, void *cq_context)
{
	struct svcxprt_rdma *xprt = cq_context;
	unsigned long flags;

	/*
	 * Set the bit regardless of whether or not it's on the list
	 * because it may be on the list already due to an RQ
	 * completion.
	*/
	set_bit(RDMAXPRT_SQ_PENDING, &xprt->sc_flags);

	/*
	 * If this transport is not already on the DTO transport queue,
	 * add it
	 */
	spin_lock_irqsave(&dto_lock, flags);
	if (list_empty(&xprt->sc_dto_q)) {
		svc_xprt_get(&xprt->sc_xprt);
		list_add_tail(&xprt->sc_dto_q, &dto_xprt_q);
	}
	spin_unlock_irqrestore(&dto_lock, flags);

	/* Tasklet does all the work to avoid irqsave locks. */
	tasklet_schedule(&dto_tasklet);
}

static void create_context_cache(struct svcxprt_rdma *xprt,
				 int ctxt_count, int ctxt_bump, int ctxt_max)
{
	struct svc_rdma_op_ctxt *ctxt;
	int i;

	xprt->sc_ctxt_max = ctxt_max;
	xprt->sc_ctxt_bump = ctxt_bump;
	xprt->sc_ctxt_cnt = 0;
	xprt->sc_ctxt_head = NULL;
	for (i = 0; i < ctxt_count; i++) {
		ctxt = kmalloc(sizeof(*ctxt), GFP_KERNEL);
		if (ctxt) {
			ctxt->next = xprt->sc_ctxt_head;
			xprt->sc_ctxt_head = ctxt;
			xprt->sc_ctxt_cnt++;
		}
	}
}

static void destroy_context_cache(struct svc_rdma_op_ctxt *ctxt)
{
	struct svc_rdma_op_ctxt *next;
	if (!ctxt)
		return;

	do {
		next = ctxt->next;
		kfree(ctxt);
		ctxt = next;
	} while (next);
}

static struct svcxprt_rdma *rdma_create_xprt(struct svc_serv *serv,
					     int listener)
{
	struct svcxprt_rdma *cma_xprt = kzalloc(sizeof *cma_xprt, GFP_KERNEL);

	if (!cma_xprt)
		return NULL;
	svc_xprt_init(&svc_rdma_class, &cma_xprt->sc_xprt, serv);
	INIT_LIST_HEAD(&cma_xprt->sc_accept_q);
	INIT_LIST_HEAD(&cma_xprt->sc_dto_q);
	INIT_LIST_HEAD(&cma_xprt->sc_rq_dto_q);
	INIT_LIST_HEAD(&cma_xprt->sc_read_complete_q);
	init_waitqueue_head(&cma_xprt->sc_send_wait);

	spin_lock_init(&cma_xprt->sc_lock);
	spin_lock_init(&cma_xprt->sc_read_complete_lock);
	spin_lock_init(&cma_xprt->sc_ctxt_lock);
	spin_lock_init(&cma_xprt->sc_rq_dto_lock);

	cma_xprt->sc_ord = svcrdma_ord;

	cma_xprt->sc_max_req_size = svcrdma_max_req_size;
	cma_xprt->sc_max_requests = svcrdma_max_requests;
	cma_xprt->sc_sq_depth = svcrdma_max_requests * RPCRDMA_SQ_DEPTH_MULT;
	atomic_set(&cma_xprt->sc_sq_count, 0);

	if (!listener) {
		int reqs = cma_xprt->sc_max_requests;
		create_context_cache(cma_xprt,
				     reqs << 1, /* starting size */
				     reqs,	/* bump amount */
				     reqs +
				     cma_xprt->sc_sq_depth +
				     RPCRDMA_MAX_THREADS + 1); /* max */
		if (!cma_xprt->sc_ctxt_head) {
			kfree(cma_xprt);
			return NULL;
		}
		clear_bit(XPT_LISTENER, &cma_xprt->sc_xprt.xpt_flags);
	} else
		set_bit(XPT_LISTENER, &cma_xprt->sc_xprt.xpt_flags);

	return cma_xprt;
}

struct page *svc_rdma_get_page(void)
{
	struct page *page;

	while ((page = alloc_page(GFP_KERNEL)) == NULL) {
		/* If we can't get memory, wait a bit and try again */
		printk(KERN_INFO "svcrdma: out of memory...retrying in 1000 "
		       "jiffies.\n");
		schedule_timeout_uninterruptible(msecs_to_jiffies(1000));
	}
	return page;
}

int svc_rdma_post_recv(struct svcxprt_rdma *xprt)
{
	struct ib_recv_wr recv_wr, *bad_recv_wr;
	struct svc_rdma_op_ctxt *ctxt;
	struct page *page;
	unsigned long pa;
	int sge_no;
	int buflen;
	int ret;

	ctxt = svc_rdma_get_context(xprt);
	buflen = 0;
	ctxt->direction = DMA_FROM_DEVICE;
	for (sge_no = 0; buflen < xprt->sc_max_req_size; sge_no++) {
		BUG_ON(sge_no >= xprt->sc_max_sge);
		page = svc_rdma_get_page();
		ctxt->pages[sge_no] = page;
		pa = ib_dma_map_page(xprt->sc_cm_id->device,
				     page, 0, PAGE_SIZE,
				     DMA_FROM_DEVICE);
		ctxt->sge[sge_no].addr = pa;
		ctxt->sge[sge_no].length = PAGE_SIZE;
		ctxt->sge[sge_no].lkey = xprt->sc_phys_mr->lkey;
		buflen += PAGE_SIZE;
	}
	ctxt->count = sge_no;
	recv_wr.next = NULL;
	recv_wr.sg_list = &ctxt->sge[0];
	recv_wr.num_sge = ctxt->count;
	recv_wr.wr_id = (u64)(unsigned long)ctxt;

	ret = ib_post_recv(xprt->sc_qp, &recv_wr, &bad_recv_wr);
	return ret;
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
static void handle_connect_req(struct rdma_cm_id *new_cma_id)
{
	struct svcxprt_rdma *listen_xprt = new_cma_id->context;
	struct svcxprt_rdma *newxprt;

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

	/*
	 * Enqueue the new transport on the accept queue of the listening
	 * transport
	 */
	spin_lock_bh(&listen_xprt->sc_lock);
	list_add_tail(&newxprt->sc_accept_q, &listen_xprt->sc_accept_q);
	spin_unlock_bh(&listen_xprt->sc_lock);

	/*
	 * Can't use svc_xprt_received here because we are not on a
	 * rqstp thread
	*/
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
			"event=%d\n", cma_id, cma_id->context, event->event);
		handle_connect_req(cma_id);
		break;

	case RDMA_CM_EVENT_ESTABLISHED:
		/* Accept complete */
		dprintk("svcrdma: Connection completed on LISTEN xprt=%p, "
			"cm_id=%p\n", xprt, cma_id);
		break;

	case RDMA_CM_EVENT_DEVICE_REMOVAL:
		dprintk("svcrdma: Device removal xprt=%p, cm_id=%p\n",
			xprt, cma_id);
		if (xprt)
			set_bit(XPT_CLOSE, &xprt->sc_xprt.xpt_flags);
		break;

	default:
		dprintk("svcrdma: Unexpected event on listening endpoint %p, "
			"event=%d\n", cma_id, event->event);
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
		}
		break;
	case RDMA_CM_EVENT_DEVICE_REMOVAL:
		dprintk("svcrdma: Device removal cma_id=%p, xprt = %p, "
			"event=%d\n", cma_id, xprt, event->event);
		if (xprt) {
			set_bit(XPT_CLOSE, &xprt->xpt_flags);
			svc_xprt_enqueue(xprt);
		}
		break;
	default:
		dprintk("svcrdma: Unexpected event on DTO endpoint %p, "
			"event=%d\n", cma_id, event->event);
		break;
	}
	return 0;
}

/*
 * Create a listening RDMA service endpoint.
 */
static struct svc_xprt *svc_rdma_create(struct svc_serv *serv,
					struct sockaddr *sa, int salen,
					int flags)
{
	struct rdma_cm_id *listen_id;
	struct svcxprt_rdma *cma_xprt;
	struct svc_xprt *xprt;
	int ret;

	dprintk("svcrdma: Creating RDMA socket\n");

	cma_xprt = rdma_create_xprt(serv, 1);
	if (!cma_xprt)
		return ERR_PTR(ENOMEM);
	xprt = &cma_xprt->sc_xprt;

	listen_id = rdma_create_id(rdma_listen_handler, cma_xprt, RDMA_PS_TCP);
	if (IS_ERR(listen_id)) {
		svc_xprt_put(&cma_xprt->sc_xprt);
		dprintk("svcrdma: rdma_create_id failed = %ld\n",
			PTR_ERR(listen_id));
		return (void *)listen_id;
	}
	ret = rdma_bind_addr(listen_id, sa);
	if (ret) {
		rdma_destroy_id(listen_id);
		svc_xprt_put(&cma_xprt->sc_xprt);
		dprintk("svcrdma: rdma_bind_addr failed = %d\n", ret);
		return ERR_PTR(ret);
	}
	cma_xprt->sc_cm_id = listen_id;

	ret = rdma_listen(listen_id, RPCRDMA_LISTEN_BACKLOG);
	if (ret) {
		rdma_destroy_id(listen_id);
		svc_xprt_put(&cma_xprt->sc_xprt);
		dprintk("svcrdma: rdma_listen failed = %d\n", ret);
		return ERR_PTR(ret);
	}

	/*
	 * We need to use the address from the cm_id in case the
	 * caller specified 0 for the port number.
	 */
	sa = (struct sockaddr *)&cma_xprt->sc_cm_id->route.addr.src_addr;
	svc_xprt_set_local(&cma_xprt->sc_xprt, sa, salen);

	return &cma_xprt->sc_xprt;
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
	struct ib_qp_init_attr qp_attr;
	struct ib_device_attr devattr;
	struct sockaddr *sa;
	int ret;
	int i;

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

	ret = ib_query_device(newxprt->sc_cm_id->device, &devattr);
	if (ret) {
		dprintk("svcrdma: could not query device attributes on "
			"device %p, rc=%d\n", newxprt->sc_cm_id->device, ret);
		goto errout;
	}

	/* Qualify the transport resource defaults with the
	 * capabilities of this particular device */
	newxprt->sc_max_sge = min((size_t)devattr.max_sge,
				  (size_t)RPCSVC_MAXPAGES);
	newxprt->sc_max_requests = min((size_t)devattr.max_qp_wr,
				   (size_t)svcrdma_max_requests);
	newxprt->sc_sq_depth = RPCRDMA_SQ_DEPTH_MULT * newxprt->sc_max_requests;

	newxprt->sc_ord =  min((size_t)devattr.max_qp_rd_atom,
			       (size_t)svcrdma_ord);

	newxprt->sc_pd = ib_alloc_pd(newxprt->sc_cm_id->device);
	if (IS_ERR(newxprt->sc_pd)) {
		dprintk("svcrdma: error creating PD for connect request\n");
		goto errout;
	}
	newxprt->sc_sq_cq = ib_create_cq(newxprt->sc_cm_id->device,
					 sq_comp_handler,
					 cq_event_handler,
					 newxprt,
					 newxprt->sc_sq_depth,
					 0);
	if (IS_ERR(newxprt->sc_sq_cq)) {
		dprintk("svcrdma: error creating SQ CQ for connect request\n");
		goto errout;
	}
	newxprt->sc_rq_cq = ib_create_cq(newxprt->sc_cm_id->device,
					 rq_comp_handler,
					 cq_event_handler,
					 newxprt,
					 newxprt->sc_max_requests,
					 0);
	if (IS_ERR(newxprt->sc_rq_cq)) {
		dprintk("svcrdma: error creating RQ CQ for connect request\n");
		goto errout;
	}

	memset(&qp_attr, 0, sizeof qp_attr);
	qp_attr.event_handler = qp_event_handler;
	qp_attr.qp_context = &newxprt->sc_xprt;
	qp_attr.cap.max_send_wr = newxprt->sc_sq_depth;
	qp_attr.cap.max_recv_wr = newxprt->sc_max_requests;
	qp_attr.cap.max_send_sge = newxprt->sc_max_sge;
	qp_attr.cap.max_recv_sge = newxprt->sc_max_sge;
	qp_attr.sq_sig_type = IB_SIGNAL_REQ_WR;
	qp_attr.qp_type = IB_QPT_RC;
	qp_attr.send_cq = newxprt->sc_sq_cq;
	qp_attr.recv_cq = newxprt->sc_rq_cq;
	dprintk("svcrdma: newxprt->sc_cm_id=%p, newxprt->sc_pd=%p\n"
		"    cm_id->device=%p, sc_pd->device=%p\n"
		"    cap.max_send_wr = %d\n"
		"    cap.max_recv_wr = %d\n"
		"    cap.max_send_sge = %d\n"
		"    cap.max_recv_sge = %d\n",
		newxprt->sc_cm_id, newxprt->sc_pd,
		newxprt->sc_cm_id->device, newxprt->sc_pd->device,
		qp_attr.cap.max_send_wr,
		qp_attr.cap.max_recv_wr,
		qp_attr.cap.max_send_sge,
		qp_attr.cap.max_recv_sge);

	ret = rdma_create_qp(newxprt->sc_cm_id, newxprt->sc_pd, &qp_attr);
	if (ret) {
		/*
		 * XXX: This is a hack. We need a xx_request_qp interface
		 * that will adjust the qp_attr's with a best-effort
		 * number
		 */
		qp_attr.cap.max_send_sge -= 2;
		qp_attr.cap.max_recv_sge -= 2;
		ret = rdma_create_qp(newxprt->sc_cm_id, newxprt->sc_pd,
				     &qp_attr);
		if (ret) {
			dprintk("svcrdma: failed to create QP, ret=%d\n", ret);
			goto errout;
		}
		newxprt->sc_max_sge = qp_attr.cap.max_send_sge;
		newxprt->sc_max_sge = qp_attr.cap.max_recv_sge;
		newxprt->sc_sq_depth = qp_attr.cap.max_send_wr;
		newxprt->sc_max_requests = qp_attr.cap.max_recv_wr;
	}
	svc_xprt_get(&newxprt->sc_xprt);
	newxprt->sc_qp = newxprt->sc_cm_id->qp;

	/* Register all of physical memory */
	newxprt->sc_phys_mr = ib_get_dma_mr(newxprt->sc_pd,
					    IB_ACCESS_LOCAL_WRITE |
					    IB_ACCESS_REMOTE_WRITE);
	if (IS_ERR(newxprt->sc_phys_mr)) {
		dprintk("svcrdma: Failed to create DMA MR ret=%d\n", ret);
		goto errout;
	}

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

	/* Accept Connection */
	set_bit(RDMAXPRT_CONN_PENDING, &newxprt->sc_flags);
	memset(&conn_param, 0, sizeof conn_param);
	conn_param.responder_resources = 0;
	conn_param.initiator_depth = newxprt->sc_ord;
	ret = rdma_accept(newxprt->sc_cm_id, &conn_param);
	if (ret) {
		dprintk("svcrdma: failed to accept new connection, ret=%d\n",
		       ret);
		goto errout;
	}

	dprintk("svcrdma: new connection %p accepted with the following "
		"attributes:\n"
		"    local_ip        : %d.%d.%d.%d\n"
		"    local_port	     : %d\n"
		"    remote_ip       : %d.%d.%d.%d\n"
		"    remote_port     : %d\n"
		"    max_sge         : %d\n"
		"    sq_depth        : %d\n"
		"    max_requests    : %d\n"
		"    ord             : %d\n",
		newxprt,
		NIPQUAD(((struct sockaddr_in *)&newxprt->sc_cm_id->
			 route.addr.src_addr)->sin_addr.s_addr),
		ntohs(((struct sockaddr_in *)&newxprt->sc_cm_id->
		       route.addr.src_addr)->sin_port),
		NIPQUAD(((struct sockaddr_in *)&newxprt->sc_cm_id->
			 route.addr.dst_addr)->sin_addr.s_addr),
		ntohs(((struct sockaddr_in *)&newxprt->sc_cm_id->
		       route.addr.dst_addr)->sin_port),
		newxprt->sc_max_sge,
		newxprt->sc_sq_depth,
		newxprt->sc_max_requests,
		newxprt->sc_ord);

	/* Set the local and remote addresses in the transport */
	sa = (struct sockaddr *)&newxprt->sc_cm_id->route.addr.dst_addr;
	svc_xprt_set_remote(&newxprt->sc_xprt, sa, svc_addr_len(sa));
	sa = (struct sockaddr *)&newxprt->sc_cm_id->route.addr.src_addr;
	svc_xprt_set_local(&newxprt->sc_xprt, sa, svc_addr_len(sa));

	ib_req_notify_cq(newxprt->sc_sq_cq, IB_CQ_NEXT_COMP);
	ib_req_notify_cq(newxprt->sc_rq_cq, IB_CQ_NEXT_COMP);
	return &newxprt->sc_xprt;

 errout:
	dprintk("svcrdma: failure accepting new connection rc=%d.\n", ret);
	/* Take a reference in case the DTO handler runs */
	svc_xprt_get(&newxprt->sc_xprt);
	if (newxprt->sc_qp && !IS_ERR(newxprt->sc_qp)) {
		ib_destroy_qp(newxprt->sc_qp);
		svc_xprt_put(&newxprt->sc_xprt);
	}
	rdma_destroy_id(newxprt->sc_cm_id);
	/* This call to put will destroy the transport */
	svc_xprt_put(&newxprt->sc_xprt);
	return NULL;
}

/*
 * Post an RQ WQE to the RQ when the rqst is being released. This
 * effectively returns an RQ credit to the client. The rq_xprt_ctxt
 * will be null if the request is deferred due to an RDMA_READ or the
 * transport had no data ready (EAGAIN). Note that an RPC deferred in
 * svc_process will still return the credit, this is because the data
 * is copied and no longer consume a WQE/WC.
 */
static void svc_rdma_release_rqst(struct svc_rqst *rqstp)
{
	int err;
	struct svcxprt_rdma *rdma =
		container_of(rqstp->rq_xprt, struct svcxprt_rdma, sc_xprt);
	if (rqstp->rq_xprt_ctxt) {
		BUG_ON(rqstp->rq_xprt_ctxt != rdma);
		err = svc_rdma_post_recv(rdma);
		if (err)
			dprintk("svcrdma: failed to post an RQ WQE error=%d\n",
				err);
	}
	rqstp->rq_xprt_ctxt = NULL;
}

/*
 * When connected, an svc_xprt has at least three references:
 *
 * - A reference held by the QP. We still hold that here because this
 *   code deletes the QP and puts the reference.
 *
 * - A reference held by the cm_id between the ESTABLISHED and
 *   DISCONNECTED events. If the remote peer disconnected first, this
 *   reference could be gone.
 *
 * - A reference held by the svc_recv code that called this function
 *   as part of close processing.
 *
 * At a minimum two references should still be held.
 */
static void svc_rdma_detach(struct svc_xprt *xprt)
{
	struct svcxprt_rdma *rdma =
		container_of(xprt, struct svcxprt_rdma, sc_xprt);
	dprintk("svc: svc_rdma_detach(%p)\n", xprt);

	/* Disconnect and flush posted WQE */
	rdma_disconnect(rdma->sc_cm_id);

	/* Destroy the QP if present (not a listener) */
	if (rdma->sc_qp && !IS_ERR(rdma->sc_qp)) {
		ib_destroy_qp(rdma->sc_qp);
		svc_xprt_put(xprt);
	}

	/* Destroy the CM ID */
	rdma_destroy_id(rdma->sc_cm_id);
}

static void svc_rdma_free(struct svc_xprt *xprt)
{
	struct svcxprt_rdma *rdma = (struct svcxprt_rdma *)xprt;
	dprintk("svcrdma: svc_rdma_free(%p)\n", rdma);
	/* We should only be called from kref_put */
	BUG_ON(atomic_read(&xprt->xpt_ref.refcount) != 0);
	if (rdma->sc_sq_cq && !IS_ERR(rdma->sc_sq_cq))
		ib_destroy_cq(rdma->sc_sq_cq);

	if (rdma->sc_rq_cq && !IS_ERR(rdma->sc_rq_cq))
		ib_destroy_cq(rdma->sc_rq_cq);

	if (rdma->sc_phys_mr && !IS_ERR(rdma->sc_phys_mr))
		ib_dereg_mr(rdma->sc_phys_mr);

	if (rdma->sc_pd && !IS_ERR(rdma->sc_pd))
		ib_dealloc_pd(rdma->sc_pd);

	destroy_context_cache(rdma->sc_ctxt_head);
	kfree(rdma);
}

static int svc_rdma_has_wspace(struct svc_xprt *xprt)
{
	struct svcxprt_rdma *rdma =
		container_of(xprt, struct svcxprt_rdma, sc_xprt);

	/*
	 * If there are fewer SQ WR available than required to send a
	 * simple response, return false.
	 */
	if ((rdma->sc_sq_depth - atomic_read(&rdma->sc_sq_count) < 3))
		return 0;

	/*
	 * ...or there are already waiters on the SQ,
	 * return false.
	 */
	if (waitqueue_active(&rdma->sc_send_wait))
		return 0;

	/* Otherwise return true. */
	return 1;
}

int svc_rdma_send(struct svcxprt_rdma *xprt, struct ib_send_wr *wr)
{
	struct ib_send_wr *bad_wr;
	int ret;

	if (test_bit(XPT_CLOSE, &xprt->sc_xprt.xpt_flags))
		return 0;

	BUG_ON(wr->send_flags != IB_SEND_SIGNALED);
	BUG_ON(((struct svc_rdma_op_ctxt *)(unsigned long)wr->wr_id)->wr_op !=
		wr->opcode);
	/* If the SQ is full, wait until an SQ entry is available */
	while (1) {
		spin_lock_bh(&xprt->sc_lock);
		if (xprt->sc_sq_depth == atomic_read(&xprt->sc_sq_count)) {
			spin_unlock_bh(&xprt->sc_lock);
			atomic_inc(&rdma_stat_sq_starve);
			/* See if we can reap some SQ WR */
			sq_cq_reap(xprt);

			/* Wait until SQ WR available if SQ still full */
			wait_event(xprt->sc_send_wait,
				   atomic_read(&xprt->sc_sq_count) <
				   xprt->sc_sq_depth);
			if (test_bit(XPT_CLOSE, &xprt->sc_xprt.xpt_flags))
				return 0;
			continue;
		}
		/* Bumped used SQ WR count and post */
		ret = ib_post_send(xprt->sc_qp, wr, &bad_wr);
		if (!ret)
			atomic_inc(&xprt->sc_sq_count);
		else
			dprintk("svcrdma: failed to post SQ WR rc=%d, "
			       "sc_sq_count=%d, sc_sq_depth=%d\n",
			       ret, atomic_read(&xprt->sc_sq_count),
			       xprt->sc_sq_depth);
		spin_unlock_bh(&xprt->sc_lock);
		break;
	}
	return ret;
}

int svc_rdma_send_error(struct svcxprt_rdma *xprt, struct rpcrdma_msg *rmsgp,
			enum rpcrdma_errcode err)
{
	struct ib_send_wr err_wr;
	struct ib_sge sge;
	struct page *p;
	struct svc_rdma_op_ctxt *ctxt;
	u32 *va;
	int length;
	int ret;

	p = svc_rdma_get_page();
	va = page_address(p);

	/* XDR encode error */
	length = svc_rdma_xdr_encode_error(xprt, rmsgp, err, va);

	/* Prepare SGE for local address */
	sge.addr = ib_dma_map_page(xprt->sc_cm_id->device,
				   p, 0, PAGE_SIZE, DMA_FROM_DEVICE);
	sge.lkey = xprt->sc_phys_mr->lkey;
	sge.length = length;

	ctxt = svc_rdma_get_context(xprt);
	ctxt->count = 1;
	ctxt->pages[0] = p;

	/* Prepare SEND WR */
	memset(&err_wr, 0, sizeof err_wr);
	ctxt->wr_op = IB_WR_SEND;
	err_wr.wr_id = (unsigned long)ctxt;
	err_wr.sg_list = &sge;
	err_wr.num_sge = 1;
	err_wr.opcode = IB_WR_SEND;
	err_wr.send_flags = IB_SEND_SIGNALED;

	/* Post It */
	ret = svc_rdma_send(xprt, &err_wr);
	if (ret) {
		dprintk("svcrdma: Error posting send = %d\n", ret);
		svc_rdma_put_context(ctxt, 1);
	}

	return ret;
}
