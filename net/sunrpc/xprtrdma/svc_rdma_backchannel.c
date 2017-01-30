/*
 * Copyright (c) 2015 Oracle.  All rights reserved.
 *
 * Support for backward direction RPCs on RPC/RDMA (server-side).
 */

#include <linux/sunrpc/svc_rdma.h>
#include "xprt_rdma.h"

#define RPCDBG_FACILITY	RPCDBG_SVCXPRT

#undef SVCRDMA_BACKCHANNEL_DEBUG

int svc_rdma_handle_bc_reply(struct rpc_xprt *xprt, struct rpcrdma_msg *rmsgp,
			     struct xdr_buf *rcvbuf)
{
	struct rpcrdma_xprt *r_xprt = rpcx_to_rdmax(xprt);
	struct kvec *dst, *src = &rcvbuf->head[0];
	struct rpc_rqst *req;
	unsigned long cwnd;
	u32 credits;
	size_t len;
	__be32 xid;
	__be32 *p;
	int ret;

	p = (__be32 *)src->iov_base;
	len = src->iov_len;
	xid = rmsgp->rm_xid;

#ifdef SVCRDMA_BACKCHANNEL_DEBUG
	pr_info("%s: xid=%08x, length=%zu\n",
		__func__, be32_to_cpu(xid), len);
	pr_info("%s: RPC/RDMA: %*ph\n",
		__func__, (int)RPCRDMA_HDRLEN_MIN, rmsgp);
	pr_info("%s:      RPC: %*ph\n",
		__func__, (int)len, p);
#endif

	ret = -EAGAIN;
	if (src->iov_len < 24)
		goto out_shortreply;

	spin_lock_bh(&xprt->transport_lock);
	req = xprt_lookup_rqst(xprt, xid);
	if (!req)
		goto out_notfound;

	dst = &req->rq_private_buf.head[0];
	memcpy(&req->rq_private_buf, &req->rq_rcv_buf, sizeof(struct xdr_buf));
	if (dst->iov_len < len)
		goto out_unlock;
	memcpy(dst->iov_base, p, len);

	credits = be32_to_cpu(rmsgp->rm_credit);
	if (credits == 0)
		credits = 1;	/* don't deadlock */
	else if (credits > r_xprt->rx_buf.rb_bc_max_requests)
		credits = r_xprt->rx_buf.rb_bc_max_requests;

	cwnd = xprt->cwnd;
	xprt->cwnd = credits << RPC_CWNDSHIFT;
	if (xprt->cwnd > cwnd)
		xprt_release_rqst_cong(req->rq_task);

	ret = 0;
	xprt_complete_rqst(req->rq_task, rcvbuf->len);
	rcvbuf->len = 0;

out_unlock:
	spin_unlock_bh(&xprt->transport_lock);
out:
	return ret;

out_shortreply:
	dprintk("svcrdma: short bc reply: xprt=%p, len=%zu\n",
		xprt, src->iov_len);
	goto out;

out_notfound:
	dprintk("svcrdma: unrecognized bc reply: xprt=%p, xid=%08x\n",
		xprt, be32_to_cpu(xid));

	goto out_unlock;
}

/* Send a backwards direction RPC call.
 *
 * Caller holds the connection's mutex and has already marshaled
 * the RPC/RDMA request.
 *
 * This is similar to svc_rdma_reply, but takes an rpc_rqst
 * instead, does not support chunks, and avoids blocking memory
 * allocation.
 *
 * XXX: There is still an opportunity to block in svc_rdma_send()
 * if there are no SQ entries to post the Send. This may occur if
 * the adapter has a small maximum SQ depth.
 */
static int svc_rdma_bc_sendto(struct svcxprt_rdma *rdma,
			      struct rpc_rqst *rqst)
{
	struct xdr_buf *sndbuf = &rqst->rq_snd_buf;
	struct svc_rdma_op_ctxt *ctxt;
	struct svc_rdma_req_map *vec;
	struct ib_send_wr send_wr;
	int ret;

	vec = svc_rdma_get_req_map(rdma);
	ret = svc_rdma_map_xdr(rdma, sndbuf, vec, false);
	if (ret)
		goto out_err;

	ret = svc_rdma_repost_recv(rdma, GFP_NOIO);
	if (ret)
		goto out_err;

	ctxt = svc_rdma_get_context(rdma);
	ctxt->pages[0] = virt_to_page(rqst->rq_buffer);
	ctxt->count = 1;

	ctxt->direction = DMA_TO_DEVICE;
	ctxt->sge[0].lkey = rdma->sc_pd->local_dma_lkey;
	ctxt->sge[0].length = sndbuf->len;
	ctxt->sge[0].addr =
	    ib_dma_map_page(rdma->sc_cm_id->device, ctxt->pages[0], 0,
			    sndbuf->len, DMA_TO_DEVICE);
	if (ib_dma_mapping_error(rdma->sc_cm_id->device, ctxt->sge[0].addr)) {
		ret = -EIO;
		goto out_unmap;
	}
	svc_rdma_count_mappings(rdma, ctxt);

	memset(&send_wr, 0, sizeof(send_wr));
	ctxt->cqe.done = svc_rdma_wc_send;
	send_wr.wr_cqe = &ctxt->cqe;
	send_wr.sg_list = ctxt->sge;
	send_wr.num_sge = 1;
	send_wr.opcode = IB_WR_SEND;
	send_wr.send_flags = IB_SEND_SIGNALED;

	ret = svc_rdma_send(rdma, &send_wr);
	if (ret) {
		ret = -EIO;
		goto out_unmap;
	}

out_err:
	svc_rdma_put_req_map(rdma, vec);
	dprintk("svcrdma: %s returns %d\n", __func__, ret);
	return ret;

out_unmap:
	svc_rdma_unmap_dma(ctxt);
	svc_rdma_put_context(ctxt, 1);
	goto out_err;
}

/* Server-side transport endpoint wants a whole page for its send
 * buffer. The client RPC code constructs the RPC header in this
 * buffer before it invokes ->send_request.
 */
static int
xprt_rdma_bc_allocate(struct rpc_task *task)
{
	struct rpc_rqst *rqst = task->tk_rqstp;
	size_t size = rqst->rq_callsize;
	struct page *page;

	if (size > PAGE_SIZE) {
		WARN_ONCE(1, "svcrdma: large bc buffer request (size %zu)\n",
			  size);
		return -EINVAL;
	}

	/* svc_rdma_sendto releases this page */
	page = alloc_page(RPCRDMA_DEF_GFP);
	if (!page)
		return -ENOMEM;
	rqst->rq_buffer = page_address(page);

	rqst->rq_rbuffer = kmalloc(rqst->rq_rcvsize, RPCRDMA_DEF_GFP);
	if (!rqst->rq_rbuffer) {
		put_page(page);
		return -ENOMEM;
	}
	return 0;
}

static void
xprt_rdma_bc_free(struct rpc_task *task)
{
	struct rpc_rqst *rqst = task->tk_rqstp;

	kfree(rqst->rq_rbuffer);
}

static int
rpcrdma_bc_send_request(struct svcxprt_rdma *rdma, struct rpc_rqst *rqst)
{
	struct rpc_xprt *xprt = rqst->rq_xprt;
	struct rpcrdma_xprt *r_xprt = rpcx_to_rdmax(xprt);
	struct rpcrdma_msg *headerp = (struct rpcrdma_msg *)rqst->rq_buffer;
	int rc;

	/* Space in the send buffer for an RPC/RDMA header is reserved
	 * via xprt->tsh_size.
	 */
	headerp->rm_xid = rqst->rq_xid;
	headerp->rm_vers = rpcrdma_version;
	headerp->rm_credit = cpu_to_be32(r_xprt->rx_buf.rb_bc_max_requests);
	headerp->rm_type = rdma_msg;
	headerp->rm_body.rm_chunks[0] = xdr_zero;
	headerp->rm_body.rm_chunks[1] = xdr_zero;
	headerp->rm_body.rm_chunks[2] = xdr_zero;

#ifdef SVCRDMA_BACKCHANNEL_DEBUG
	pr_info("%s: %*ph\n", __func__, 64, rqst->rq_buffer);
#endif

	rc = svc_rdma_bc_sendto(rdma, rqst);
	if (rc)
		goto drop_connection;
	return rc;

drop_connection:
	dprintk("svcrdma: failed to send bc call\n");
	xprt_disconnect_done(xprt);
	return -ENOTCONN;
}

/* Send an RPC call on the passive end of a transport
 * connection.
 */
static int
xprt_rdma_bc_send_request(struct rpc_task *task)
{
	struct rpc_rqst *rqst = task->tk_rqstp;
	struct svc_xprt *sxprt = rqst->rq_xprt->bc_xprt;
	struct svcxprt_rdma *rdma;
	int ret;

	dprintk("svcrdma: sending bc call with xid: %08x\n",
		be32_to_cpu(rqst->rq_xid));

	if (!mutex_trylock(&sxprt->xpt_mutex)) {
		rpc_sleep_on(&sxprt->xpt_bc_pending, task, NULL);
		if (!mutex_trylock(&sxprt->xpt_mutex))
			return -EAGAIN;
		rpc_wake_up_queued_task(&sxprt->xpt_bc_pending, task);
	}

	ret = -ENOTCONN;
	rdma = container_of(sxprt, struct svcxprt_rdma, sc_xprt);
	if (!test_bit(XPT_DEAD, &sxprt->xpt_flags))
		ret = rpcrdma_bc_send_request(rdma, rqst);

	mutex_unlock(&sxprt->xpt_mutex);

	if (ret < 0)
		return ret;
	return 0;
}

static void
xprt_rdma_bc_close(struct rpc_xprt *xprt)
{
	dprintk("svcrdma: %s: xprt %p\n", __func__, xprt);
}

static void
xprt_rdma_bc_put(struct rpc_xprt *xprt)
{
	dprintk("svcrdma: %s: xprt %p\n", __func__, xprt);

	xprt_free(xprt);
	module_put(THIS_MODULE);
}

static struct rpc_xprt_ops xprt_rdma_bc_procs = {
	.reserve_xprt		= xprt_reserve_xprt_cong,
	.release_xprt		= xprt_release_xprt_cong,
	.alloc_slot		= xprt_alloc_slot,
	.release_request	= xprt_release_rqst_cong,
	.buf_alloc		= xprt_rdma_bc_allocate,
	.buf_free		= xprt_rdma_bc_free,
	.send_request		= xprt_rdma_bc_send_request,
	.set_retrans_timeout	= xprt_set_retrans_timeout_def,
	.close			= xprt_rdma_bc_close,
	.destroy		= xprt_rdma_bc_put,
	.print_stats		= xprt_rdma_print_stats
};

static const struct rpc_timeout xprt_rdma_bc_timeout = {
	.to_initval = 60 * HZ,
	.to_maxval = 60 * HZ,
};

/* It shouldn't matter if the number of backchannel session slots
 * doesn't match the number of RPC/RDMA credits. That just means
 * one or the other will have extra slots that aren't used.
 */
static struct rpc_xprt *
xprt_setup_rdma_bc(struct xprt_create *args)
{
	struct rpc_xprt *xprt;
	struct rpcrdma_xprt *new_xprt;

	if (args->addrlen > sizeof(xprt->addr)) {
		dprintk("RPC:       %s: address too large\n", __func__);
		return ERR_PTR(-EBADF);
	}

	xprt = xprt_alloc(args->net, sizeof(*new_xprt),
			  RPCRDMA_MAX_BC_REQUESTS,
			  RPCRDMA_MAX_BC_REQUESTS);
	if (!xprt) {
		dprintk("RPC:       %s: couldn't allocate rpc_xprt\n",
			__func__);
		return ERR_PTR(-ENOMEM);
	}

	xprt->timeout = &xprt_rdma_bc_timeout;
	xprt_set_bound(xprt);
	xprt_set_connected(xprt);
	xprt->bind_timeout = RPCRDMA_BIND_TO;
	xprt->reestablish_timeout = RPCRDMA_INIT_REEST_TO;
	xprt->idle_timeout = RPCRDMA_IDLE_DISC_TO;

	xprt->prot = XPRT_TRANSPORT_BC_RDMA;
	xprt->tsh_size = RPCRDMA_HDRLEN_MIN / sizeof(__be32);
	xprt->ops = &xprt_rdma_bc_procs;

	memcpy(&xprt->addr, args->dstaddr, args->addrlen);
	xprt->addrlen = args->addrlen;
	xprt_rdma_format_addresses(xprt, (struct sockaddr *)&xprt->addr);
	xprt->resvport = 0;

	xprt->max_payload = xprt_rdma_max_inline_read;

	new_xprt = rpcx_to_rdmax(xprt);
	new_xprt->rx_buf.rb_bc_max_requests = xprt->max_reqs;

	xprt_get(xprt);
	args->bc_xprt->xpt_bc_xprt = xprt;
	xprt->bc_xprt = args->bc_xprt;

	if (!try_module_get(THIS_MODULE))
		goto out_fail;

	/* Final put for backchannel xprt is in __svc_rdma_free */
	xprt_get(xprt);
	return xprt;

out_fail:
	xprt_rdma_free_addresses(xprt);
	args->bc_xprt->xpt_bc_xprt = NULL;
	args->bc_xprt->xpt_bc_xps = NULL;
	xprt_put(xprt);
	xprt_free(xprt);
	return ERR_PTR(-EINVAL);
}

struct xprt_class xprt_rdma_bc = {
	.list			= LIST_HEAD_INIT(xprt_rdma_bc.list),
	.name			= "rdma backchannel",
	.owner			= THIS_MODULE,
	.ident			= XPRT_TRANSPORT_BC_RDMA,
	.setup			= xprt_setup_rdma_bc,
};
