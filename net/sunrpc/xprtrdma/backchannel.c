// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2015 Oracle.  All rights reserved.
 *
 * Support for backward direction RPCs on RPC/RDMA.
 */

#include <linux/sunrpc/xprt.h>
#include <linux/sunrpc/svc.h>
#include <linux/sunrpc/svc_xprt.h>
#include <linux/sunrpc/svc_rdma.h>

#include "xprt_rdma.h"
#include <trace/events/rpcrdma.h>

#if IS_ENABLED(CONFIG_SUNRPC_DEBUG)
# define RPCDBG_FACILITY	RPCDBG_TRANS
#endif

#undef RPCRDMA_BACKCHANNEL_DEBUG

static int rpcrdma_bc_setup_reqs(struct rpcrdma_xprt *r_xprt,
				 unsigned int count)
{
	struct rpc_xprt *xprt = &r_xprt->rx_xprt;
	struct rpcrdma_req *req;
	struct rpc_rqst *rqst;
	unsigned int i;

	for (i = 0; i < (count << 1); i++) {
		struct rpcrdma_regbuf *rb;
		size_t size;

		req = rpcrdma_create_req(r_xprt);
		if (IS_ERR(req))
			return PTR_ERR(req);
		rqst = &req->rl_slot;

		rqst->rq_xprt = xprt;
		INIT_LIST_HEAD(&rqst->rq_bc_list);
		__set_bit(RPC_BC_PA_IN_USE, &rqst->rq_bc_pa_state);
		spin_lock(&xprt->bc_pa_lock);
		list_add(&rqst->rq_bc_pa_list, &xprt->bc_pa_list);
		spin_unlock(&xprt->bc_pa_lock);

		size = r_xprt->rx_data.inline_rsize;
		rb = rpcrdma_alloc_regbuf(size, DMA_TO_DEVICE, GFP_KERNEL);
		if (IS_ERR(rb))
			goto out_fail;
		req->rl_sendbuf = rb;
		xdr_buf_init(&rqst->rq_snd_buf, rb->rg_base,
			     min_t(size_t, size, PAGE_SIZE));
	}
	return 0;

out_fail:
	rpcrdma_req_destroy(req);
	return -ENOMEM;
}

/**
 * xprt_rdma_bc_setup - Pre-allocate resources for handling backchannel requests
 * @xprt: transport associated with these backchannel resources
 * @reqs: number of concurrent incoming requests to expect
 *
 * Returns 0 on success; otherwise a negative errno
 */
int xprt_rdma_bc_setup(struct rpc_xprt *xprt, unsigned int reqs)
{
	struct rpcrdma_xprt *r_xprt = rpcx_to_rdmax(xprt);
	int rc;

	/* The backchannel reply path returns each rpc_rqst to the
	 * bc_pa_list _after_ the reply is sent. If the server is
	 * faster than the client, it can send another backward
	 * direction request before the rpc_rqst is returned to the
	 * list. The client rejects the request in this case.
	 *
	 * Twice as many rpc_rqsts are prepared to ensure there is
	 * always an rpc_rqst available as soon as a reply is sent.
	 */
	if (reqs > RPCRDMA_BACKWARD_WRS >> 1)
		goto out_err;

	rc = rpcrdma_bc_setup_reqs(r_xprt, reqs);
	if (rc)
		goto out_free;

	r_xprt->rx_buf.rb_bc_srv_max_requests = reqs;
	trace_xprtrdma_cb_setup(r_xprt, reqs);
	return 0;

out_free:
	xprt_rdma_bc_destroy(xprt, reqs);

out_err:
	pr_err("RPC:       %s: setup backchannel transport failed\n", __func__);
	return -ENOMEM;
}

/**
 * xprt_rdma_bc_maxpayload - Return maximum backchannel message size
 * @xprt: transport
 *
 * Returns maximum size, in bytes, of a backchannel message
 */
size_t xprt_rdma_bc_maxpayload(struct rpc_xprt *xprt)
{
	struct rpcrdma_xprt *r_xprt = rpcx_to_rdmax(xprt);
	struct rpcrdma_create_data_internal *cdata = &r_xprt->rx_data;
	size_t maxmsg;

	maxmsg = min_t(unsigned int, cdata->inline_rsize, cdata->inline_wsize);
	maxmsg = min_t(unsigned int, maxmsg, PAGE_SIZE);
	return maxmsg - RPCRDMA_HDRLEN_MIN;
}

static int rpcrdma_bc_marshal_reply(struct rpc_rqst *rqst)
{
	struct rpcrdma_xprt *r_xprt = rpcx_to_rdmax(rqst->rq_xprt);
	struct rpcrdma_req *req = rpcr_to_rdmar(rqst);
	__be32 *p;

	rpcrdma_set_xdrlen(&req->rl_hdrbuf, 0);
	xdr_init_encode(&req->rl_stream, &req->rl_hdrbuf,
			req->rl_rdmabuf->rg_base, rqst);

	p = xdr_reserve_space(&req->rl_stream, 28);
	if (unlikely(!p))
		return -EIO;
	*p++ = rqst->rq_xid;
	*p++ = rpcrdma_version;
	*p++ = cpu_to_be32(r_xprt->rx_buf.rb_bc_srv_max_requests);
	*p++ = rdma_msg;
	*p++ = xdr_zero;
	*p++ = xdr_zero;
	*p = xdr_zero;

	if (rpcrdma_prepare_send_sges(r_xprt, req, RPCRDMA_HDRLEN_MIN,
				      &rqst->rq_snd_buf, rpcrdma_noch))
		return -EIO;

	trace_xprtrdma_cb_reply(rqst);
	return 0;
}

/**
 * xprt_rdma_bc_send_reply - marshal and send a backchannel reply
 * @rqst: RPC rqst with a backchannel RPC reply in rq_snd_buf
 *
 * Caller holds the transport's write lock.
 *
 * Returns:
 *	%0 if the RPC message has been sent
 *	%-ENOTCONN if the caller should reconnect and call again
 *	%-EIO if a permanent error occurred and the request was not
 *		sent. Do not try to send this message again.
 */
int xprt_rdma_bc_send_reply(struct rpc_rqst *rqst)
{
	struct rpc_xprt *xprt = rqst->rq_xprt;
	struct rpcrdma_xprt *r_xprt = rpcx_to_rdmax(xprt);
	struct rpcrdma_req *req = rpcr_to_rdmar(rqst);
	int rc;

	if (!xprt_connected(xprt))
		return -ENOTCONN;

	if (!xprt_request_get_cong(xprt, rqst))
		return -EBADSLT;

	rc = rpcrdma_bc_marshal_reply(rqst);
	if (rc < 0)
		goto failed_marshal;

	if (rpcrdma_ep_post(&r_xprt->rx_ia, &r_xprt->rx_ep, req))
		goto drop_connection;
	return 0;

failed_marshal:
	if (rc != -ENOTCONN)
		return rc;
drop_connection:
	xprt_rdma_close(xprt);
	return -ENOTCONN;
}

/**
 * xprt_rdma_bc_destroy - Release resources for handling backchannel requests
 * @xprt: transport associated with these backchannel resources
 * @reqs: number of incoming requests to destroy; ignored
 */
void xprt_rdma_bc_destroy(struct rpc_xprt *xprt, unsigned int reqs)
{
	struct rpc_rqst *rqst, *tmp;

	spin_lock(&xprt->bc_pa_lock);
	list_for_each_entry_safe(rqst, tmp, &xprt->bc_pa_list, rq_bc_pa_list) {
		list_del(&rqst->rq_bc_pa_list);
		spin_unlock(&xprt->bc_pa_lock);

		rpcrdma_req_destroy(rpcr_to_rdmar(rqst));

		spin_lock(&xprt->bc_pa_lock);
	}
	spin_unlock(&xprt->bc_pa_lock);
}

/**
 * xprt_rdma_bc_free_rqst - Release a backchannel rqst
 * @rqst: request to release
 */
void xprt_rdma_bc_free_rqst(struct rpc_rqst *rqst)
{
	struct rpcrdma_req *req = rpcr_to_rdmar(rqst);
	struct rpc_xprt *xprt = rqst->rq_xprt;

	rpcrdma_recv_buffer_put(req->rl_reply);
	req->rl_reply = NULL;

	spin_lock(&xprt->bc_pa_lock);
	list_add_tail(&rqst->rq_bc_pa_list, &xprt->bc_pa_list);
	spin_unlock(&xprt->bc_pa_lock);
}

/**
 * rpcrdma_bc_receive_call - Handle a backward direction call
 * @r_xprt: transport receiving the call
 * @rep: receive buffer containing the call
 *
 * Operational assumptions:
 *    o Backchannel credits are ignored, just as the NFS server
 *      forechannel currently does
 *    o The ULP manages a replay cache (eg, NFSv4.1 sessions).
 *      No replay detection is done at the transport level
 */
void rpcrdma_bc_receive_call(struct rpcrdma_xprt *r_xprt,
			     struct rpcrdma_rep *rep)
{
	struct rpc_xprt *xprt = &r_xprt->rx_xprt;
	struct svc_serv *bc_serv;
	struct rpcrdma_req *req;
	struct rpc_rqst *rqst;
	struct xdr_buf *buf;
	size_t size;
	__be32 *p;

	p = xdr_inline_decode(&rep->rr_stream, 0);
	size = xdr_stream_remaining(&rep->rr_stream);

#ifdef RPCRDMA_BACKCHANNEL_DEBUG
	pr_info("RPC:       %s: callback XID %08x, length=%u\n",
		__func__, be32_to_cpup(p), size);
	pr_info("RPC:       %s: %*ph\n", __func__, size, p);
#endif

	/* Grab a free bc rqst */
	spin_lock(&xprt->bc_pa_lock);
	if (list_empty(&xprt->bc_pa_list)) {
		spin_unlock(&xprt->bc_pa_lock);
		goto out_overflow;
	}
	rqst = list_first_entry(&xprt->bc_pa_list,
				struct rpc_rqst, rq_bc_pa_list);
	list_del(&rqst->rq_bc_pa_list);
	spin_unlock(&xprt->bc_pa_lock);

	/* Prepare rqst */
	rqst->rq_reply_bytes_recvd = 0;
	rqst->rq_xid = *p;

	rqst->rq_private_buf.len = size;

	buf = &rqst->rq_rcv_buf;
	memset(buf, 0, sizeof(*buf));
	buf->head[0].iov_base = p;
	buf->head[0].iov_len = size;
	buf->len = size;

	/* The receive buffer has to be hooked to the rpcrdma_req
	 * so that it is not released while the req is pointing
	 * to its buffer, and so that it can be reposted after
	 * the Upper Layer is done decoding it.
	 */
	req = rpcr_to_rdmar(rqst);
	req->rl_reply = rep;
	trace_xprtrdma_cb_call(rqst);

	/* Queue rqst for ULP's callback service */
	bc_serv = xprt->bc_serv;
	spin_lock(&bc_serv->sv_cb_lock);
	list_add(&rqst->rq_bc_list, &bc_serv->sv_cb_list);
	spin_unlock(&bc_serv->sv_cb_lock);

	wake_up(&bc_serv->sv_cb_waitq);

	r_xprt->rx_stats.bcall_count++;
	return;

out_overflow:
	pr_warn("RPC/RDMA backchannel overflow\n");
	xprt_force_disconnect(xprt);
	/* This receive buffer gets reposted automatically
	 * when the connection is re-established.
	 */
	return;
}
