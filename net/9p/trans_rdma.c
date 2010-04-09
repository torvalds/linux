/*
 * linux/fs/9p/trans_rdma.c
 *
 * RDMA transport layer based on the trans_fd.c implementation.
 *
 *  Copyright (C) 2008 by Tom Tucker <tom@opengridcomputing.com>
 *  Copyright (C) 2006 by Russ Cox <rsc@swtch.com>
 *  Copyright (C) 2004-2005 by Latchesar Ionkov <lucho@ionkov.net>
 *  Copyright (C) 2004-2008 by Eric Van Hensbergen <ericvh@gmail.com>
 *  Copyright (C) 1997-2002 by Ron Minnich <rminnich@sarnoff.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2
 *  as published by the Free Software Foundation.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to:
 *  Free Software Foundation
 *  51 Franklin Street, Fifth Floor
 *  Boston, MA  02111-1301  USA
 *
 */

#include <linux/in.h>
#include <linux/module.h>
#include <linux/net.h>
#include <linux/ipv6.h>
#include <linux/kthread.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/un.h>
#include <linux/uaccess.h>
#include <linux/inet.h>
#include <linux/idr.h>
#include <linux/file.h>
#include <linux/parser.h>
#include <linux/semaphore.h>
#include <net/9p/9p.h>
#include <net/9p/client.h>
#include <net/9p/transport.h>
#include <rdma/ib_verbs.h>
#include <rdma/rdma_cm.h>

#define P9_PORT			5640
#define P9_RDMA_SQ_DEPTH	32
#define P9_RDMA_RQ_DEPTH	32
#define P9_RDMA_SEND_SGE	4
#define P9_RDMA_RECV_SGE	4
#define P9_RDMA_IRD		0
#define P9_RDMA_ORD		0
#define P9_RDMA_TIMEOUT		30000		/* 30 seconds */
#define P9_RDMA_MAXSIZE		(4*4096)	/* Min SGE is 4, so we can
						 * safely advertise a maxsize
						 * of 64k */

#define P9_RDMA_MAX_SGE (P9_RDMA_MAXSIZE >> PAGE_SHIFT)
/**
 * struct p9_trans_rdma - RDMA transport instance
 *
 * @state: tracks the transport state machine for connection setup and tear down
 * @cm_id: The RDMA CM ID
 * @pd: Protection Domain pointer
 * @qp: Queue Pair pointer
 * @cq: Completion Queue pointer
 * @dm_mr: DMA Memory Region pointer
 * @lkey: The local access only memory region key
 * @timeout: Number of uSecs to wait for connection management events
 * @sq_depth: The depth of the Send Queue
 * @sq_sem: Semaphore for the SQ
 * @rq_depth: The depth of the Receive Queue.
 * @rq_count: Count of requests in the Receive Queue.
 * @addr: The remote peer's address
 * @req_lock: Protects the active request list
 * @cm_done: Completion event for connection management tracking
 */
struct p9_trans_rdma {
	enum {
		P9_RDMA_INIT,
		P9_RDMA_ADDR_RESOLVED,
		P9_RDMA_ROUTE_RESOLVED,
		P9_RDMA_CONNECTED,
		P9_RDMA_FLUSHING,
		P9_RDMA_CLOSING,
		P9_RDMA_CLOSED,
	} state;
	struct rdma_cm_id *cm_id;
	struct ib_pd *pd;
	struct ib_qp *qp;
	struct ib_cq *cq;
	struct ib_mr *dma_mr;
	u32 lkey;
	long timeout;
	int sq_depth;
	struct semaphore sq_sem;
	int rq_depth;
	atomic_t rq_count;
	struct sockaddr_in addr;
	spinlock_t req_lock;

	struct completion cm_done;
};

/**
 * p9_rdma_context - Keeps track of in-process WR
 *
 * @wc_op: The original WR op for when the CQE completes in error.
 * @busa: Bus address to unmap when the WR completes
 * @req: Keeps track of requests (send)
 * @rc: Keepts track of replies (receive)
 */
struct p9_rdma_req;
struct p9_rdma_context {
	enum ib_wc_opcode wc_op;
	dma_addr_t busa;
	union {
		struct p9_req_t *req;
		struct p9_fcall *rc;
	};
};

/**
 * p9_rdma_opts - Collection of mount options
 * @port: port of connection
 * @sq_depth: The requested depth of the SQ. This really doesn't need
 * to be any deeper than the number of threads used in the client
 * @rq_depth: The depth of the RQ. Should be greater than or equal to SQ depth
 * @timeout: Time to wait in msecs for CM events
 */
struct p9_rdma_opts {
	short port;
	int sq_depth;
	int rq_depth;
	long timeout;
};

/*
 * Option Parsing (code inspired by NFS code)
 */
enum {
	/* Options that take integer arguments */
	Opt_port, Opt_rq_depth, Opt_sq_depth, Opt_timeout, Opt_err,
};

static match_table_t tokens = {
	{Opt_port, "port=%u"},
	{Opt_sq_depth, "sq=%u"},
	{Opt_rq_depth, "rq=%u"},
	{Opt_timeout, "timeout=%u"},
	{Opt_err, NULL},
};

/**
 * parse_opts - parse mount options into rdma options structure
 * @params: options string passed from mount
 * @opts: rdma transport-specific structure to parse options into
 *
 * Returns 0 upon success, -ERRNO upon failure
 */
static int parse_opts(char *params, struct p9_rdma_opts *opts)
{
	char *p;
	substring_t args[MAX_OPT_ARGS];
	int option;
	char *options, *tmp_options;
	int ret;

	opts->port = P9_PORT;
	opts->sq_depth = P9_RDMA_SQ_DEPTH;
	opts->rq_depth = P9_RDMA_RQ_DEPTH;
	opts->timeout = P9_RDMA_TIMEOUT;

	if (!params)
		return 0;

	tmp_options = kstrdup(params, GFP_KERNEL);
	if (!tmp_options) {
		P9_DPRINTK(P9_DEBUG_ERROR,
			   "failed to allocate copy of option string\n");
		return -ENOMEM;
	}
	options = tmp_options;

	while ((p = strsep(&options, ",")) != NULL) {
		int token;
		int r;
		if (!*p)
			continue;
		token = match_token(p, tokens, args);
		r = match_int(&args[0], &option);
		if (r < 0) {
			P9_DPRINTK(P9_DEBUG_ERROR,
				   "integer field, but no integer?\n");
			ret = r;
			continue;
		}
		switch (token) {
		case Opt_port:
			opts->port = option;
			break;
		case Opt_sq_depth:
			opts->sq_depth = option;
			break;
		case Opt_rq_depth:
			opts->rq_depth = option;
			break;
		case Opt_timeout:
			opts->timeout = option;
			break;
		default:
			continue;
		}
	}
	/* RQ must be at least as large as the SQ */
	opts->rq_depth = max(opts->rq_depth, opts->sq_depth);
	kfree(tmp_options);
	return 0;
}

static int
p9_cm_event_handler(struct rdma_cm_id *id, struct rdma_cm_event *event)
{
	struct p9_client *c = id->context;
	struct p9_trans_rdma *rdma = c->trans;
	switch (event->event) {
	case RDMA_CM_EVENT_ADDR_RESOLVED:
		BUG_ON(rdma->state != P9_RDMA_INIT);
		rdma->state = P9_RDMA_ADDR_RESOLVED;
		break;

	case RDMA_CM_EVENT_ROUTE_RESOLVED:
		BUG_ON(rdma->state != P9_RDMA_ADDR_RESOLVED);
		rdma->state = P9_RDMA_ROUTE_RESOLVED;
		break;

	case RDMA_CM_EVENT_ESTABLISHED:
		BUG_ON(rdma->state != P9_RDMA_ROUTE_RESOLVED);
		rdma->state = P9_RDMA_CONNECTED;
		break;

	case RDMA_CM_EVENT_DISCONNECTED:
		if (rdma)
			rdma->state = P9_RDMA_CLOSED;
		if (c)
			c->status = Disconnected;
		break;

	case RDMA_CM_EVENT_TIMEWAIT_EXIT:
		break;

	case RDMA_CM_EVENT_ADDR_CHANGE:
	case RDMA_CM_EVENT_ROUTE_ERROR:
	case RDMA_CM_EVENT_DEVICE_REMOVAL:
	case RDMA_CM_EVENT_MULTICAST_JOIN:
	case RDMA_CM_EVENT_MULTICAST_ERROR:
	case RDMA_CM_EVENT_REJECTED:
	case RDMA_CM_EVENT_CONNECT_REQUEST:
	case RDMA_CM_EVENT_CONNECT_RESPONSE:
	case RDMA_CM_EVENT_CONNECT_ERROR:
	case RDMA_CM_EVENT_ADDR_ERROR:
	case RDMA_CM_EVENT_UNREACHABLE:
		c->status = Disconnected;
		rdma_disconnect(rdma->cm_id);
		break;
	default:
		BUG();
	}
	complete(&rdma->cm_done);
	return 0;
}

static void
handle_recv(struct p9_client *client, struct p9_trans_rdma *rdma,
	    struct p9_rdma_context *c, enum ib_wc_status status, u32 byte_len)
{
	struct p9_req_t *req;
	int err = 0;
	int16_t tag;

	req = NULL;
	ib_dma_unmap_single(rdma->cm_id->device, c->busa, client->msize,
							 DMA_FROM_DEVICE);

	if (status != IB_WC_SUCCESS)
		goto err_out;

	err = p9_parse_header(c->rc, NULL, NULL, &tag, 1);
	if (err)
		goto err_out;

	req = p9_tag_lookup(client, tag);
	if (!req)
		goto err_out;

	req->rc = c->rc;
	req->status = REQ_STATUS_RCVD;
	p9_client_cb(client, req);

	return;

 err_out:
	P9_DPRINTK(P9_DEBUG_ERROR, "req %p err %d status %d\n",
		   req, err, status);
	rdma->state = P9_RDMA_FLUSHING;
	client->status = Disconnected;
	return;
}

static void
handle_send(struct p9_client *client, struct p9_trans_rdma *rdma,
	    struct p9_rdma_context *c, enum ib_wc_status status, u32 byte_len)
{
	ib_dma_unmap_single(rdma->cm_id->device,
			    c->busa, c->req->tc->size,
			    DMA_TO_DEVICE);
}

static void qp_event_handler(struct ib_event *event, void *context)
{
	P9_DPRINTK(P9_DEBUG_ERROR, "QP event %d context %p\n", event->event,
								context);
}

static void cq_comp_handler(struct ib_cq *cq, void *cq_context)
{
	struct p9_client *client = cq_context;
	struct p9_trans_rdma *rdma = client->trans;
	int ret;
	struct ib_wc wc;

	ib_req_notify_cq(rdma->cq, IB_CQ_NEXT_COMP);
	while ((ret = ib_poll_cq(cq, 1, &wc)) > 0) {
		struct p9_rdma_context *c = (void *) (unsigned long) wc.wr_id;

		switch (c->wc_op) {
		case IB_WC_RECV:
			atomic_dec(&rdma->rq_count);
			handle_recv(client, rdma, c, wc.status, wc.byte_len);
			break;

		case IB_WC_SEND:
			handle_send(client, rdma, c, wc.status, wc.byte_len);
			up(&rdma->sq_sem);
			break;

		default:
			printk(KERN_ERR "9prdma: unexpected completion type, "
			       "c->wc_op=%d, wc.opcode=%d, status=%d\n",
			       c->wc_op, wc.opcode, wc.status);
			break;
		}
		kfree(c);
	}
}

static void cq_event_handler(struct ib_event *e, void *v)
{
	P9_DPRINTK(P9_DEBUG_ERROR, "CQ event %d context %p\n", e->event, v);
}

static void rdma_destroy_trans(struct p9_trans_rdma *rdma)
{
	if (!rdma)
		return;

	if (rdma->dma_mr && !IS_ERR(rdma->dma_mr))
		ib_dereg_mr(rdma->dma_mr);

	if (rdma->qp && !IS_ERR(rdma->qp))
		ib_destroy_qp(rdma->qp);

	if (rdma->pd && !IS_ERR(rdma->pd))
		ib_dealloc_pd(rdma->pd);

	if (rdma->cq && !IS_ERR(rdma->cq))
		ib_destroy_cq(rdma->cq);

	if (rdma->cm_id && !IS_ERR(rdma->cm_id))
		rdma_destroy_id(rdma->cm_id);

	kfree(rdma);
}

static int
post_recv(struct p9_client *client, struct p9_rdma_context *c)
{
	struct p9_trans_rdma *rdma = client->trans;
	struct ib_recv_wr wr, *bad_wr;
	struct ib_sge sge;

	c->busa = ib_dma_map_single(rdma->cm_id->device,
				    c->rc->sdata, client->msize,
				    DMA_FROM_DEVICE);
	if (ib_dma_mapping_error(rdma->cm_id->device, c->busa))
		goto error;

	sge.addr = c->busa;
	sge.length = client->msize;
	sge.lkey = rdma->lkey;

	wr.next = NULL;
	c->wc_op = IB_WC_RECV;
	wr.wr_id = (unsigned long) c;
	wr.sg_list = &sge;
	wr.num_sge = 1;
	return ib_post_recv(rdma->qp, &wr, &bad_wr);

 error:
	P9_DPRINTK(P9_DEBUG_ERROR, "EIO\n");
	return -EIO;
}

static int rdma_request(struct p9_client *client, struct p9_req_t *req)
{
	struct p9_trans_rdma *rdma = client->trans;
	struct ib_send_wr wr, *bad_wr;
	struct ib_sge sge;
	int err = 0;
	unsigned long flags;
	struct p9_rdma_context *c = NULL;
	struct p9_rdma_context *rpl_context = NULL;

	/* Allocate an fcall for the reply */
	rpl_context = kmalloc(sizeof *rpl_context, GFP_KERNEL);
	if (!rpl_context)
		goto err_close;

	/*
	 * If the request has a buffer, steal it, otherwise
	 * allocate a new one.  Typically, requests should already
	 * have receive buffers allocated and just swap them around
	 */
	if (!req->rc) {
		req->rc = kmalloc(sizeof(struct p9_fcall)+client->msize,
								GFP_KERNEL);
		if (req->rc) {
			req->rc->sdata = (char *) req->rc +
						sizeof(struct p9_fcall);
			req->rc->capacity = client->msize;
		}
	}
	rpl_context->rc = req->rc;
	if (!rpl_context->rc) {
		kfree(rpl_context);
		goto err_close;
	}

	/*
	 * Post a receive buffer for this request. We need to ensure
	 * there is a reply buffer available for every outstanding
	 * request. A flushed request can result in no reply for an
	 * outstanding request, so we must keep a count to avoid
	 * overflowing the RQ.
	 */
	if (atomic_inc_return(&rdma->rq_count) <= rdma->rq_depth) {
		err = post_recv(client, rpl_context);
		if (err) {
			kfree(rpl_context->rc);
			kfree(rpl_context);
			goto err_close;
		}
	} else
		atomic_dec(&rdma->rq_count);

	/* remove posted receive buffer from request structure */
	req->rc = NULL;

	/* Post the request */
	c = kmalloc(sizeof *c, GFP_KERNEL);
	if (!c)
		goto err_close;
	c->req = req;

	c->busa = ib_dma_map_single(rdma->cm_id->device,
				    c->req->tc->sdata, c->req->tc->size,
				    DMA_TO_DEVICE);
	if (ib_dma_mapping_error(rdma->cm_id->device, c->busa))
		goto error;

	sge.addr = c->busa;
	sge.length = c->req->tc->size;
	sge.lkey = rdma->lkey;

	wr.next = NULL;
	c->wc_op = IB_WC_SEND;
	wr.wr_id = (unsigned long) c;
	wr.opcode = IB_WR_SEND;
	wr.send_flags = IB_SEND_SIGNALED;
	wr.sg_list = &sge;
	wr.num_sge = 1;

	if (down_interruptible(&rdma->sq_sem))
		goto error;

	return ib_post_send(rdma->qp, &wr, &bad_wr);

 error:
	P9_DPRINTK(P9_DEBUG_ERROR, "EIO\n");
	return -EIO;

 err_close:
	spin_lock_irqsave(&rdma->req_lock, flags);
	if (rdma->state < P9_RDMA_CLOSING) {
		rdma->state = P9_RDMA_CLOSING;
		spin_unlock_irqrestore(&rdma->req_lock, flags);
		rdma_disconnect(rdma->cm_id);
	} else
		spin_unlock_irqrestore(&rdma->req_lock, flags);
	return err;
}

static void rdma_close(struct p9_client *client)
{
	struct p9_trans_rdma *rdma;

	if (!client)
		return;

	rdma = client->trans;
	if (!rdma)
		return;

	client->status = Disconnected;
	rdma_disconnect(rdma->cm_id);
	rdma_destroy_trans(rdma);
}

/**
 * alloc_rdma - Allocate and initialize the rdma transport structure
 * @opts: Mount options structure
 */
static struct p9_trans_rdma *alloc_rdma(struct p9_rdma_opts *opts)
{
	struct p9_trans_rdma *rdma;

	rdma = kzalloc(sizeof(struct p9_trans_rdma), GFP_KERNEL);
	if (!rdma)
		return NULL;

	rdma->sq_depth = opts->sq_depth;
	rdma->rq_depth = opts->rq_depth;
	rdma->timeout = opts->timeout;
	spin_lock_init(&rdma->req_lock);
	init_completion(&rdma->cm_done);
	sema_init(&rdma->sq_sem, rdma->sq_depth);
	atomic_set(&rdma->rq_count, 0);

	return rdma;
}

/* its not clear to me we can do anything after send has been posted */
static int rdma_cancel(struct p9_client *client, struct p9_req_t *req)
{
	return 1;
}

/**
 * trans_create_rdma - Transport method for creating atransport instance
 * @client: client instance
 * @addr: IP address string
 * @args: Mount options string
 */
static int
rdma_create_trans(struct p9_client *client, const char *addr, char *args)
{
	int err;
	struct p9_rdma_opts opts;
	struct p9_trans_rdma *rdma;
	struct rdma_conn_param conn_param;
	struct ib_qp_init_attr qp_attr;
	struct ib_device_attr devattr;

	/* Parse the transport specific mount options */
	err = parse_opts(args, &opts);
	if (err < 0)
		return err;

	/* Create and initialize the RDMA transport structure */
	rdma = alloc_rdma(&opts);
	if (!rdma)
		return -ENOMEM;

	/* Create the RDMA CM ID */
	rdma->cm_id = rdma_create_id(p9_cm_event_handler, client, RDMA_PS_TCP);
	if (IS_ERR(rdma->cm_id))
		goto error;

	/* Associate the client with the transport */
	client->trans = rdma;

	/* Resolve the server's address */
	rdma->addr.sin_family = AF_INET;
	rdma->addr.sin_addr.s_addr = in_aton(addr);
	rdma->addr.sin_port = htons(opts.port);
	err = rdma_resolve_addr(rdma->cm_id, NULL,
				(struct sockaddr *)&rdma->addr,
				rdma->timeout);
	if (err)
		goto error;
	err = wait_for_completion_interruptible(&rdma->cm_done);
	if (err || (rdma->state != P9_RDMA_ADDR_RESOLVED))
		goto error;

	/* Resolve the route to the server */
	err = rdma_resolve_route(rdma->cm_id, rdma->timeout);
	if (err)
		goto error;
	err = wait_for_completion_interruptible(&rdma->cm_done);
	if (err || (rdma->state != P9_RDMA_ROUTE_RESOLVED))
		goto error;

	/* Query the device attributes */
	err = ib_query_device(rdma->cm_id->device, &devattr);
	if (err)
		goto error;

	/* Create the Completion Queue */
	rdma->cq = ib_create_cq(rdma->cm_id->device, cq_comp_handler,
				cq_event_handler, client,
				opts.sq_depth + opts.rq_depth + 1, 0);
	if (IS_ERR(rdma->cq))
		goto error;
	ib_req_notify_cq(rdma->cq, IB_CQ_NEXT_COMP);

	/* Create the Protection Domain */
	rdma->pd = ib_alloc_pd(rdma->cm_id->device);
	if (IS_ERR(rdma->pd))
		goto error;

	/* Cache the DMA lkey in the transport */
	rdma->dma_mr = NULL;
	if (devattr.device_cap_flags & IB_DEVICE_LOCAL_DMA_LKEY)
		rdma->lkey = rdma->cm_id->device->local_dma_lkey;
	else {
		rdma->dma_mr = ib_get_dma_mr(rdma->pd, IB_ACCESS_LOCAL_WRITE);
		if (IS_ERR(rdma->dma_mr))
			goto error;
		rdma->lkey = rdma->dma_mr->lkey;
	}

	/* Create the Queue Pair */
	memset(&qp_attr, 0, sizeof qp_attr);
	qp_attr.event_handler = qp_event_handler;
	qp_attr.qp_context = client;
	qp_attr.cap.max_send_wr = opts.sq_depth;
	qp_attr.cap.max_recv_wr = opts.rq_depth;
	qp_attr.cap.max_send_sge = P9_RDMA_SEND_SGE;
	qp_attr.cap.max_recv_sge = P9_RDMA_RECV_SGE;
	qp_attr.sq_sig_type = IB_SIGNAL_REQ_WR;
	qp_attr.qp_type = IB_QPT_RC;
	qp_attr.send_cq = rdma->cq;
	qp_attr.recv_cq = rdma->cq;
	err = rdma_create_qp(rdma->cm_id, rdma->pd, &qp_attr);
	if (err)
		goto error;
	rdma->qp = rdma->cm_id->qp;

	/* Request a connection */
	memset(&conn_param, 0, sizeof(conn_param));
	conn_param.private_data = NULL;
	conn_param.private_data_len = 0;
	conn_param.responder_resources = P9_RDMA_IRD;
	conn_param.initiator_depth = P9_RDMA_ORD;
	err = rdma_connect(rdma->cm_id, &conn_param);
	if (err)
		goto error;
	err = wait_for_completion_interruptible(&rdma->cm_done);
	if (err || (rdma->state != P9_RDMA_CONNECTED))
		goto error;

	client->status = Connected;

	return 0;

error:
	rdma_destroy_trans(rdma);
	return -ENOTCONN;
}

static struct p9_trans_module p9_rdma_trans = {
	.name = "rdma",
	.maxsize = P9_RDMA_MAXSIZE,
	.def = 0,
	.owner = THIS_MODULE,
	.create = rdma_create_trans,
	.close = rdma_close,
	.request = rdma_request,
	.cancel = rdma_cancel,
};

/**
 * p9_trans_rdma_init - Register the 9P RDMA transport driver
 */
static int __init p9_trans_rdma_init(void)
{
	v9fs_register_trans(&p9_rdma_trans);
	return 0;
}

static void __exit p9_trans_rdma_exit(void)
{
	v9fs_unregister_trans(&p9_rdma_trans);
}

module_init(p9_trans_rdma_init);
module_exit(p9_trans_rdma_exit);

MODULE_AUTHOR("Tom Tucker <tom@opengridcomputing.com>");
MODULE_DESCRIPTION("RDMA Transport for 9P");
MODULE_LICENSE("Dual BSD/GPL");
