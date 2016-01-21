/*
 * Copyright (c) 2015 Oracle.  All rights reserved.
 *
 * Support for backward direction RPCs on RPC/RDMA.
 */

#include <linux/module.h>
#include <linux/sunrpc/xprt.h>
#include <linux/sunrpc/svc.h>
#include <linux/sunrpc/svc_xprt.h>

#include "xprt_rdma.h"

#if IS_ENABLED(CONFIG_SUNRPC_DEBUG)
# define RPCDBG_FACILITY	RPCDBG_TRANS
#endif

#define RPCRDMA_BACKCHANNEL_DEBUG

static void rpcrdma_bc_free_rqst(struct rpcrdma_xprt *r_xprt,
				 struct rpc_rqst *rqst)
{
	struct rpcrdma_buffer *buf = &r_xprt->rx_buf;
	struct rpcrdma_req *req = rpcr_to_rdmar(rqst);

	spin_lock(&buf->rb_reqslock);
	list_del(&req->rl_all);
	spin_unlock(&buf->rb_reqslock);

	rpcrdma_destroy_req(&r_xprt->rx_ia, req);

	kfree(rqst);
}

static int rpcrdma_bc_setup_rqst(struct rpcrdma_xprt *r_xprt,
				 struct rpc_rqst *rqst)
{
	struct rpcrdma_ia *ia = &r_xprt->rx_ia;
	struct rpcrdma_regbuf *rb;
	struct rpcrdma_req *req;
	struct xdr_buf *buf;
	size_t size;

	req = rpcrdma_create_req(r_xprt);
	if (!req)
		return -ENOMEM;
	req->rl_backchannel = true;

	size = RPCRDMA_INLINE_WRITE_THRESHOLD(rqst);
	rb = rpcrdma_alloc_regbuf(ia, size, GFP_KERNEL);
	if (IS_ERR(rb))
		goto out_fail;
	req->rl_rdmabuf = rb;

	size += RPCRDMA_INLINE_READ_THRESHOLD(rqst);
	rb = rpcrdma_alloc_regbuf(ia, size, GFP_KERNEL);
	if (IS_ERR(rb))
		goto out_fail;
	rb->rg_owner = req;
	req->rl_sendbuf = rb;
	/* so that rpcr_to_rdmar works when receiving a request */
	rqst->rq_buffer = (void *)req->rl_sendbuf->rg_base;

	buf = &rqst->rq_snd_buf;
	buf->head[0].iov_base = rqst->rq_buffer;
	buf->head[0].iov_len = 0;
	buf->tail[0].iov_base = NULL;
	buf->tail[0].iov_len = 0;
	buf->page_len = 0;
	buf->len = 0;
	buf->buflen = size;

	return 0;

out_fail:
	rpcrdma_bc_free_rqst(r_xprt, rqst);
	return -ENOMEM;
}

/* Allocate and add receive buffers to the rpcrdma_buffer's
 * existing list of rep's. These are released when the
 * transport is destroyed.
 */
static int rpcrdma_bc_setup_reps(struct rpcrdma_xprt *r_xprt,
				 unsigned int count)
{
	struct rpcrdma_buffer *buffers = &r_xprt->rx_buf;
	struct rpcrdma_rep *rep;
	unsigned long flags;
	int rc = 0;

	while (count--) {
		rep = rpcrdma_create_rep(r_xprt);
		if (IS_ERR(rep)) {
			pr_err("RPC:       %s: reply buffer alloc failed\n",
			       __func__);
			rc = PTR_ERR(rep);
			break;
		}

		spin_lock_irqsave(&buffers->rb_lock, flags);
		list_add(&rep->rr_list, &buffers->rb_recv_bufs);
		spin_unlock_irqrestore(&buffers->rb_lock, flags);
	}

	return rc;
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
	struct rpcrdma_buffer *buffer = &r_xprt->rx_buf;
	struct rpc_rqst *rqst;
	unsigned int i;
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

	for (i = 0; i < (reqs << 1); i++) {
		rqst = kzalloc(sizeof(*rqst), GFP_KERNEL);
		if (!rqst) {
			pr_err("RPC:       %s: Failed to create bc rpc_rqst\n",
			       __func__);
			goto out_free;
		}

		rqst->rq_xprt = &r_xprt->rx_xprt;
		INIT_LIST_HEAD(&rqst->rq_list);
		INIT_LIST_HEAD(&rqst->rq_bc_list);

		if (rpcrdma_bc_setup_rqst(r_xprt, rqst))
			goto out_free;

		spin_lock_bh(&xprt->bc_pa_lock);
		list_add(&rqst->rq_bc_pa_list, &xprt->bc_pa_list);
		spin_unlock_bh(&xprt->bc_pa_lock);
	}

	rc = rpcrdma_bc_setup_reps(r_xprt, reqs);
	if (rc)
		goto out_free;

	rc = rpcrdma_ep_post_extra_recv(r_xprt, reqs);
	if (rc)
		goto out_free;

	buffer->rb_bc_srv_max_requests = reqs;
	request_module("svcrdma");

	return 0;

out_free:
	xprt_rdma_bc_destroy(xprt, reqs);

out_err:
	pr_err("RPC:       %s: setup backchannel transport failed\n", __func__);
	return -ENOMEM;
}

/**
 * xprt_rdma_bc_up - Create transport endpoint for backchannel service
 * @serv: server endpoint
 * @net: network namespace
 *
 * The "xprt" is an implied argument: it supplies the name of the
 * backchannel transport class.
 *
 * Returns zero on success, negative errno on failure
 */
int xprt_rdma_bc_up(struct svc_serv *serv, struct net *net)
{
	int ret;

	ret = svc_create_xprt(serv, "rdma-bc", net, PF_INET, 0, 0);
	if (ret < 0)
		return ret;
	return 0;
}

/**
 * rpcrdma_bc_marshal_reply - Send backwards direction reply
 * @rqst: buffer containing RPC reply data
 *
 * Returns zero on success.
 */
int rpcrdma_bc_marshal_reply(struct rpc_rqst *rqst)
{
	struct rpc_xprt *xprt = rqst->rq_xprt;
	struct rpcrdma_xprt *r_xprt = rpcx_to_rdmax(xprt);
	struct rpcrdma_req *req = rpcr_to_rdmar(rqst);
	struct rpcrdma_msg *headerp;
	size_t rpclen;

	headerp = rdmab_to_msg(req->rl_rdmabuf);
	headerp->rm_xid = rqst->rq_xid;
	headerp->rm_vers = rpcrdma_version;
	headerp->rm_credit =
			cpu_to_be32(r_xprt->rx_buf.rb_bc_srv_max_requests);
	headerp->rm_type = rdma_msg;
	headerp->rm_body.rm_chunks[0] = xdr_zero;
	headerp->rm_body.rm_chunks[1] = xdr_zero;
	headerp->rm_body.rm_chunks[2] = xdr_zero;

	rpclen = rqst->rq_svec[0].iov_len;

	pr_info("RPC:       %s: rpclen %zd headerp 0x%p lkey 0x%x\n",
		__func__, rpclen, headerp, rdmab_lkey(req->rl_rdmabuf));
	pr_info("RPC:       %s: RPC/RDMA: %*ph\n",
		__func__, (int)RPCRDMA_HDRLEN_MIN, headerp);
	pr_info("RPC:       %s:      RPC: %*ph\n",
		__func__, (int)rpclen, rqst->rq_svec[0].iov_base);

	req->rl_send_iov[0].addr = rdmab_addr(req->rl_rdmabuf);
	req->rl_send_iov[0].length = RPCRDMA_HDRLEN_MIN;
	req->rl_send_iov[0].lkey = rdmab_lkey(req->rl_rdmabuf);

	req->rl_send_iov[1].addr = rdmab_addr(req->rl_sendbuf);
	req->rl_send_iov[1].length = rpclen;
	req->rl_send_iov[1].lkey = rdmab_lkey(req->rl_sendbuf);

	req->rl_niovs = 2;
	return 0;
}

/**
 * xprt_rdma_bc_destroy - Release resources for handling backchannel requests
 * @xprt: transport associated with these backchannel resources
 * @reqs: number of incoming requests to destroy; ignored
 */
void xprt_rdma_bc_destroy(struct rpc_xprt *xprt, unsigned int reqs)
{
	struct rpcrdma_xprt *r_xprt = rpcx_to_rdmax(xprt);
	struct rpc_rqst *rqst, *tmp;

	spin_lock_bh(&xprt->bc_pa_lock);
	list_for_each_entry_safe(rqst, tmp, &xprt->bc_pa_list, rq_bc_pa_list) {
		list_del(&rqst->rq_bc_pa_list);
		spin_unlock_bh(&xprt->bc_pa_lock);

		rpcrdma_bc_free_rqst(r_xprt, rqst);

		spin_lock_bh(&xprt->bc_pa_lock);
	}
	spin_unlock_bh(&xprt->bc_pa_lock);
}

/**
 * xprt_rdma_bc_free_rqst - Release a backchannel rqst
 * @rqst: request to release
 */
void xprt_rdma_bc_free_rqst(struct rpc_rqst *rqst)
{
	struct rpc_xprt *xprt = rqst->rq_xprt;

	smp_mb__before_atomic();
	WARN_ON_ONCE(!test_bit(RPC_BC_PA_IN_USE, &rqst->rq_bc_pa_state));
	clear_bit(RPC_BC_PA_IN_USE, &rqst->rq_bc_pa_state);
	smp_mb__after_atomic();

	spin_lock_bh(&xprt->bc_pa_lock);
	list_add_tail(&rqst->rq_bc_pa_list, &xprt->bc_pa_list);
	spin_unlock_bh(&xprt->bc_pa_lock);
}

/**
 * rpcrdma_bc_receive_call - Handle a backward direction call
 * @xprt: transport receiving the call
 * @rep: receive buffer containing the call
 *
 * Called in the RPC reply handler, which runs in a tasklet.
 * Be quick about it.
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
	struct rpcrdma_msg *headerp;
	struct svc_serv *bc_serv;
	struct rpcrdma_req *req;
	struct rpc_rqst *rqst;
	struct xdr_buf *buf;
	size_t size;
	__be32 *p;

	headerp = rdmab_to_msg(rep->rr_rdmabuf);
#ifdef RPCRDMA_BACKCHANNEL_DEBUG
	pr_info("RPC:       %s: callback XID %08x, length=%u\n",
		__func__, be32_to_cpu(headerp->rm_xid), rep->rr_len);
	pr_info("RPC:       %s: %*ph\n", __func__, rep->rr_len, headerp);
#endif

	/* Sanity check:
	 * Need at least enough bytes for RPC/RDMA header, as code
	 * here references the header fields by array offset. Also,
	 * backward calls are always inline, so ensure there
	 * are some bytes beyond the RPC/RDMA header.
	 */
	if (rep->rr_len < RPCRDMA_HDRLEN_MIN + 24)
		goto out_short;
	p = (__be32 *)((unsigned char *)headerp + RPCRDMA_HDRLEN_MIN);
	size = rep->rr_len - RPCRDMA_HDRLEN_MIN;

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
#ifdef RPCRDMA_BACKCHANNEL_DEBUG
	pr_info("RPC:       %s: using rqst %p\n", __func__, rqst);
#endif

	/* Prepare rqst */
	rqst->rq_reply_bytes_recvd = 0;
	rqst->rq_bytes_sent = 0;
	rqst->rq_xid = headerp->rm_xid;
	set_bit(RPC_BC_PA_IN_USE, &rqst->rq_bc_pa_state);

	buf = &rqst->rq_rcv_buf;
	memset(buf, 0, sizeof(*buf));
	buf->head[0].iov_base = p;
	buf->head[0].iov_len = size;
	buf->len = size;

	/* The receive buffer has to be hooked to the rpcrdma_req
	 * so that it can be reposted after the server is done
	 * parsing it but just before sending the backward
	 * direction reply.
	 */
	req = rpcr_to_rdmar(rqst);
#ifdef RPCRDMA_BACKCHANNEL_DEBUG
	pr_info("RPC:       %s: attaching rep %p to req %p\n",
		__func__, rep, req);
#endif
	req->rl_reply = rep;

	/* Defeat the retransmit detection logic in send_request */
	req->rl_connect_cookie = 0;

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
	xprt_disconnect_done(xprt);
	/* This receive buffer gets reposted automatically
	 * when the connection is re-established.
	 */
	return;

out_short:
	pr_warn("RPC/RDMA short backward direction call\n");

	if (rpcrdma_ep_post_recv(&r_xprt->rx_ia, &r_xprt->rx_ep, rep))
		xprt_disconnect_done(xprt);
	else
		pr_warn("RPC:       %s: reposting rep %p\n",
			__func__, rep);
}
