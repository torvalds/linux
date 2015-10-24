/*
 * Copyright (c) 2015 Oracle.  All rights reserved.
 *
 * Support for backward direction RPCs on RPC/RDMA.
 */

#include <linux/module.h>

#include "xprt_rdma.h"

#if IS_ENABLED(CONFIG_SUNRPC_DEBUG)
# define RPCDBG_FACILITY	RPCDBG_TRANS
#endif

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

	pr_err("RPC:       %s: setup backchannel transport failed\n", __func__);
	return -ENOMEM;
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
