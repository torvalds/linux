// SPDX-License-Identifier: GPL-2.0-only
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
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

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
#include <linux/slab.h>
#include <linux/seq_file.h>
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
#define P9_RDMA_MAXSIZE		(1024*1024)	/* 1MB */

/**
 * struct p9_trans_rdma - RDMA transport instance
 *
 * @state: tracks the transport state machine for connection setup and tear down
 * @cm_id: The RDMA CM ID
 * @pd: Protection Domain pointer
 * @qp: Queue Pair pointer
 * @cq: Completion Queue pointer
 * @timeout: Number of uSecs to wait for connection management events
 * @privport: Whether a privileged port may be used
 * @port: The port to use
 * @sq_depth: The depth of the Send Queue
 * @sq_sem: Semaphore for the SQ
 * @rq_depth: The depth of the Receive Queue.
 * @rq_sem: Semaphore for the RQ
 * @excess_rc : Amount of posted Receive Contexts without a pending request.
 *		See rdma_request()
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
	long timeout;
	bool privport;
	u16 port;
	int sq_depth;
	struct semaphore sq_sem;
	int rq_depth;
	struct semaphore rq_sem;
	atomic_t excess_rc;
	struct sockaddr_in addr;
	spinlock_t req_lock;

	struct completion cm_done;
};

struct p9_rdma_req;

/**
 * struct p9_rdma_context - Keeps track of in-process WR
 *
 * @busa: Bus address to unmap when the WR completes
 * @req: Keeps track of requests (send)
 * @rc: Keepts track of replies (receive)
 */
struct p9_rdma_context {
	struct ib_cqe cqe;
	dma_addr_t busa;
	union {
		struct p9_req_t *req;
		struct p9_fcall rc;
	};
};

/**
 * struct p9_rdma_opts - Collection of mount options
 * @port: port of connection
 * @sq_depth: The requested depth of the SQ. This really doesn't need
 * to be any deeper than the number of threads used in the client
 * @rq_depth: The depth of the RQ. Should be greater than or equal to SQ depth
 * @timeout: Time to wait in msecs for CM events
 */
struct p9_rdma_opts {
	short port;
	bool privport;
	int sq_depth;
	int rq_depth;
	long timeout;
};

/*
 * Option Parsing (code inspired by NFS code)
 */
enum {
	/* Options that take integer arguments */
	Opt_port, Opt_rq_depth, Opt_sq_depth, Opt_timeout,
	/* Options that take no argument */
	Opt_privport,
	Opt_err,
};

static match_table_t tokens = {
	{Opt_port, "port=%u"},
	{Opt_sq_depth, "sq=%u"},
	{Opt_rq_depth, "rq=%u"},
	{Opt_timeout, "timeout=%u"},
	{Opt_privport, "privport"},
	{Opt_err, NULL},
};

static int p9_rdma_show_options(struct seq_file *m, struct p9_client *clnt)
{
	struct p9_trans_rdma *rdma = clnt->trans;

	if (rdma->port != P9_PORT)
		seq_printf(m, ",port=%u", rdma->port);
	if (rdma->sq_depth != P9_RDMA_SQ_DEPTH)
		seq_printf(m, ",sq=%u", rdma->sq_depth);
	if (rdma->rq_depth != P9_RDMA_RQ_DEPTH)
		seq_printf(m, ",rq=%u", rdma->rq_depth);
	if (rdma->timeout != P9_RDMA_TIMEOUT)
		seq_printf(m, ",timeout=%lu", rdma->timeout);
	if (rdma->privport)
		seq_puts(m, ",privport");
	return 0;
}

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

	opts->port = P9_PORT;
	opts->sq_depth = P9_RDMA_SQ_DEPTH;
	opts->rq_depth = P9_RDMA_RQ_DEPTH;
	opts->timeout = P9_RDMA_TIMEOUT;
	opts->privport = false;

	if (!params)
		return 0;

	tmp_options = kstrdup(params, GFP_KERNEL);
	if (!tmp_options) {
		p9_debug(P9_DEBUG_ERROR,
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
		if ((token != Opt_err) && (token != Opt_privport)) {
			r = match_int(&args[0], &option);
			if (r < 0) {
				p9_debug(P9_DEBUG_ERROR,
					 "integer field, but no integer?\n");
				continue;
			}
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
		case Opt_privport:
			opts->privport = true;
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
recv_done(struct ib_cq *cq, struct ib_wc *wc)
{
	struct p9_client *client = cq->cq_context;
	struct p9_trans_rdma *rdma = client->trans;
	struct p9_rdma_context *c =
		container_of(wc->wr_cqe, struct p9_rdma_context, cqe);
	struct p9_req_t *req;
	int err = 0;
	int16_t tag;

	req = NULL;
	ib_dma_unmap_single(rdma->cm_id->device, c->busa, client->msize,
							 DMA_FROM_DEVICE);

	if (wc->status != IB_WC_SUCCESS)
		goto err_out;

	c->rc.size = wc->byte_len;
	err = p9_parse_header(&c->rc, NULL, NULL, &tag, 1);
	if (err)
		goto err_out;

	req = p9_tag_lookup(client, tag);
	if (!req)
		goto err_out;

	/* Check that we have not yet received a reply for this request.
	 */
	if (unlikely(req->rc.sdata)) {
		pr_err("Duplicate reply for request %d", tag);
		goto err_out;
	}

	req->rc.size = c->rc.size;
	req->rc.sdata = c->rc.sdata;
	p9_client_cb(client, req, REQ_STATUS_RCVD);

 out:
	up(&rdma->rq_sem);
	kfree(c);
	return;

 err_out:
	p9_debug(P9_DEBUG_ERROR, "req %p err %d status %d\n",
			req, err, wc->status);
	rdma->state = P9_RDMA_FLUSHING;
	client->status = Disconnected;
	goto out;
}

static void
send_done(struct ib_cq *cq, struct ib_wc *wc)
{
	struct p9_client *client = cq->cq_context;
	struct p9_trans_rdma *rdma = client->trans;
	struct p9_rdma_context *c =
		container_of(wc->wr_cqe, struct p9_rdma_context, cqe);

	ib_dma_unmap_single(rdma->cm_id->device,
			    c->busa, c->req->tc.size,
			    DMA_TO_DEVICE);
	up(&rdma->sq_sem);
	p9_req_put(c->req);
	kfree(c);
}

static void qp_event_handler(struct ib_event *event, void *context)
{
	p9_debug(P9_DEBUG_ERROR, "QP event %d context %p\n",
		 event->event, context);
}

static void rdma_destroy_trans(struct p9_trans_rdma *rdma)
{
	if (!rdma)
		return;

	if (rdma->qp && !IS_ERR(rdma->qp))
		ib_destroy_qp(rdma->qp);

	if (rdma->pd && !IS_ERR(rdma->pd))
		ib_dealloc_pd(rdma->pd);

	if (rdma->cq && !IS_ERR(rdma->cq))
		ib_free_cq(rdma->cq);

	if (rdma->cm_id && !IS_ERR(rdma->cm_id))
		rdma_destroy_id(rdma->cm_id);

	kfree(rdma);
}

static int
post_recv(struct p9_client *client, struct p9_rdma_context *c)
{
	struct p9_trans_rdma *rdma = client->trans;
	struct ib_recv_wr wr;
	struct ib_sge sge;
	int ret;

	c->busa = ib_dma_map_single(rdma->cm_id->device,
				    c->rc.sdata, client->msize,
				    DMA_FROM_DEVICE);
	if (ib_dma_mapping_error(rdma->cm_id->device, c->busa))
		goto error;

	c->cqe.done = recv_done;

	sge.addr = c->busa;
	sge.length = client->msize;
	sge.lkey = rdma->pd->local_dma_lkey;

	wr.next = NULL;
	wr.wr_cqe = &c->cqe;
	wr.sg_list = &sge;
	wr.num_sge = 1;

	ret = ib_post_recv(rdma->qp, &wr, NULL);
	if (ret)
		ib_dma_unmap_single(rdma->cm_id->device, c->busa,
				    client->msize, DMA_FROM_DEVICE);
	return ret;

 error:
	p9_debug(P9_DEBUG_ERROR, "EIO\n");
	return -EIO;
}

static int rdma_request(struct p9_client *client, struct p9_req_t *req)
{
	struct p9_trans_rdma *rdma = client->trans;
	struct ib_send_wr wr;
	struct ib_sge sge;
	int err = 0;
	unsigned long flags;
	struct p9_rdma_context *c = NULL;
	struct p9_rdma_context *rpl_context = NULL;

	/* When an error occurs between posting the recv and the send,
	 * there will be a receive context posted without a pending request.
	 * Since there is no way to "un-post" it, we remember it and skip
	 * post_recv() for the next request.
	 * So here,
	 * see if we are this `next request' and need to absorb an excess rc.
	 * If yes, then drop and free our own, and do not recv_post().
	 **/
	if (unlikely(atomic_read(&rdma->excess_rc) > 0)) {
		if ((atomic_sub_return(1, &rdma->excess_rc) >= 0)) {
			/* Got one! */
			p9_fcall_fini(&req->rc);
			req->rc.sdata = NULL;
			goto dont_need_post_recv;
		} else {
			/* We raced and lost. */
			atomic_inc(&rdma->excess_rc);
		}
	}

	/* Allocate an fcall for the reply */
	rpl_context = kmalloc(sizeof *rpl_context, GFP_NOFS);
	if (!rpl_context) {
		err = -ENOMEM;
		goto recv_error;
	}
	rpl_context->rc.sdata = req->rc.sdata;

	/*
	 * Post a receive buffer for this request. We need to ensure
	 * there is a reply buffer available for every outstanding
	 * request. A flushed request can result in no reply for an
	 * outstanding request, so we must keep a count to avoid
	 * overflowing the RQ.
	 */
	if (down_interruptible(&rdma->rq_sem)) {
		err = -EINTR;
		goto recv_error;
	}

	err = post_recv(client, rpl_context);
	if (err) {
		p9_debug(P9_DEBUG_ERROR, "POST RECV failed: %d\n", err);
		goto recv_error;
	}
	/* remove posted receive buffer from request structure */
	req->rc.sdata = NULL;

dont_need_post_recv:
	/* Post the request */
	c = kmalloc(sizeof *c, GFP_NOFS);
	if (!c) {
		err = -ENOMEM;
		goto send_error;
	}
	c->req = req;

	c->busa = ib_dma_map_single(rdma->cm_id->device,
				    c->req->tc.sdata, c->req->tc.size,
				    DMA_TO_DEVICE);
	if (ib_dma_mapping_error(rdma->cm_id->device, c->busa)) {
		err = -EIO;
		goto send_error;
	}

	c->cqe.done = send_done;

	sge.addr = c->busa;
	sge.length = c->req->tc.size;
	sge.lkey = rdma->pd->local_dma_lkey;

	wr.next = NULL;
	wr.wr_cqe = &c->cqe;
	wr.opcode = IB_WR_SEND;
	wr.send_flags = IB_SEND_SIGNALED;
	wr.sg_list = &sge;
	wr.num_sge = 1;

	if (down_interruptible(&rdma->sq_sem)) {
		err = -EINTR;
		goto dma_unmap;
	}

	/* Mark request as `sent' *before* we actually send it,
	 * because doing if after could erase the REQ_STATUS_RCVD
	 * status in case of a very fast reply.
	 */
	req->status = REQ_STATUS_SENT;
	err = ib_post_send(rdma->qp, &wr, NULL);
	if (err)
		goto dma_unmap;

	/* Success */
	return 0;

dma_unmap:
	ib_dma_unmap_single(rdma->cm_id->device, c->busa,
			    c->req->tc.size, DMA_TO_DEVICE);
 /* Handle errors that happened during or while preparing the send: */
 send_error:
	req->status = REQ_STATUS_ERROR;
	kfree(c);
	p9_debug(P9_DEBUG_ERROR, "Error %d in rdma_request()\n", err);

	/* Ach.
	 *  We did recv_post(), but not send. We have one recv_post in excess.
	 */
	atomic_inc(&rdma->excess_rc);
	return err;

 /* Handle errors that happened during or while preparing post_recv(): */
 recv_error:
	kfree(rpl_context);
	spin_lock_irqsave(&rdma->req_lock, flags);
	if (err != -EINTR && rdma->state < P9_RDMA_CLOSING) {
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

	rdma->port = opts->port;
	rdma->privport = opts->privport;
	rdma->sq_depth = opts->sq_depth;
	rdma->rq_depth = opts->rq_depth;
	rdma->timeout = opts->timeout;
	spin_lock_init(&rdma->req_lock);
	init_completion(&rdma->cm_done);
	sema_init(&rdma->sq_sem, rdma->sq_depth);
	sema_init(&rdma->rq_sem, rdma->rq_depth);
	atomic_set(&rdma->excess_rc, 0);

	return rdma;
}

static int rdma_cancel(struct p9_client *client, struct p9_req_t *req)
{
	/* Nothing to do here.
	 * We will take care of it (if we have to) in rdma_cancelled()
	 */
	return 1;
}

/* A request has been fully flushed without a reply.
 * That means we have posted one buffer in excess.
 */
static int rdma_cancelled(struct p9_client *client, struct p9_req_t *req)
{
	struct p9_trans_rdma *rdma = client->trans;
	atomic_inc(&rdma->excess_rc);
	return 0;
}

static int p9_rdma_bind_privport(struct p9_trans_rdma *rdma)
{
	struct sockaddr_in cl = {
		.sin_family = AF_INET,
		.sin_addr.s_addr = htonl(INADDR_ANY),
	};
	int port, err = -EINVAL;

	for (port = P9_DEF_MAX_RESVPORT; port >= P9_DEF_MIN_RESVPORT; port--) {
		cl.sin_port = htons((ushort)port);
		err = rdma_bind_addr(rdma->cm_id, (struct sockaddr *)&cl);
		if (err != -EADDRINUSE)
			break;
	}
	return err;
}

/**
 * rdma_create_trans - Transport method for creating a transport instance
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

	if (addr == NULL)
		return -EINVAL;

	/* Parse the transport specific mount options */
	err = parse_opts(args, &opts);
	if (err < 0)
		return err;

	/* Create and initialize the RDMA transport structure */
	rdma = alloc_rdma(&opts);
	if (!rdma)
		return -ENOMEM;

	/* Create the RDMA CM ID */
	rdma->cm_id = rdma_create_id(&init_net, p9_cm_event_handler, client,
				     RDMA_PS_TCP, IB_QPT_RC);
	if (IS_ERR(rdma->cm_id))
		goto error;

	/* Associate the client with the transport */
	client->trans = rdma;

	/* Bind to a privileged port if we need to */
	if (opts.privport) {
		err = p9_rdma_bind_privport(rdma);
		if (err < 0) {
			pr_err("%s (%d): problem binding to privport: %d\n",
			       __func__, task_pid_nr(current), -err);
			goto error;
		}
	}

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

	/* Create the Completion Queue */
	rdma->cq = ib_alloc_cq_any(rdma->cm_id->device, client,
				   opts.sq_depth + opts.rq_depth + 1,
				   IB_POLL_SOFTIRQ);
	if (IS_ERR(rdma->cq))
		goto error;

	/* Create the Protection Domain */
	rdma->pd = ib_alloc_pd(rdma->cm_id->device, 0);
	if (IS_ERR(rdma->pd))
		goto error;

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
	.cancelled = rdma_cancelled,
	.show_options = p9_rdma_show_options,
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
