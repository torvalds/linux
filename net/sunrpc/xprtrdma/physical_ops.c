/*
 * Copyright (c) 2015 Oracle.  All rights reserved.
 * Copyright (c) 2003-2007 Network Appliance, Inc. All rights reserved.
 */

/* No-op chunk preparation. All client memory is pre-registered.
 * Sometimes referred to as ALLPHYSICAL mode.
 *
 * Physical registration is simple because all client memory is
 * pre-registered and never deregistered. This mode is good for
 * adapter bring up, but is considered not safe: the server is
 * trusted not to abuse its access to client memory not involved
 * in RDMA I/O.
 */

#include "xprt_rdma.h"

#if IS_ENABLED(CONFIG_SUNRPC_DEBUG)
# define RPCDBG_FACILITY	RPCDBG_TRANS
#endif

static int
physical_op_open(struct rpcrdma_ia *ia, struct rpcrdma_ep *ep,
		 struct rpcrdma_create_data_internal *cdata)
{
	struct ib_mr *mr;

	/* Obtain an rkey to use for RPC data payloads.
	 */
	mr = ib_get_dma_mr(ia->ri_pd,
			   IB_ACCESS_LOCAL_WRITE |
			   IB_ACCESS_REMOTE_WRITE |
			   IB_ACCESS_REMOTE_READ);
	if (IS_ERR(mr)) {
		pr_err("%s: ib_get_dma_mr for failed with %lX\n",
		       __func__, PTR_ERR(mr));
		return -ENOMEM;
	}

	ia->ri_dma_mr = mr;
	return 0;
}

/* PHYSICAL memory registration conveys one page per chunk segment.
 */
static size_t
physical_op_maxpages(struct rpcrdma_xprt *r_xprt)
{
	return min_t(unsigned int, RPCRDMA_MAX_DATA_SEGS,
		     rpcrdma_max_segments(r_xprt));
}

static int
physical_op_init(struct rpcrdma_xprt *r_xprt)
{
	return 0;
}

/* The client's physical memory is already exposed for
 * remote access via RDMA READ or RDMA WRITE.
 */
static int
physical_op_map(struct rpcrdma_xprt *r_xprt, struct rpcrdma_mr_seg *seg,
		int nsegs, bool writing)
{
	struct rpcrdma_ia *ia = &r_xprt->rx_ia;

	rpcrdma_map_one(ia->ri_device, seg, rpcrdma_data_dir(writing));
	seg->mr_rkey = ia->ri_dma_mr->rkey;
	seg->mr_base = seg->mr_dma;
	return 1;
}

/* Unmap a memory region, but leave it registered.
 */
static int
physical_op_unmap(struct rpcrdma_xprt *r_xprt, struct rpcrdma_mr_seg *seg)
{
	struct rpcrdma_ia *ia = &r_xprt->rx_ia;

	rpcrdma_unmap_one(ia->ri_device, seg);
	return 1;
}

/* DMA unmap all memory regions that were mapped for "req".
 */
static void
physical_op_unmap_sync(struct rpcrdma_xprt *r_xprt, struct rpcrdma_req *req)
{
	struct ib_device *device = r_xprt->rx_ia.ri_device;
	unsigned int i;

	for (i = 0; req->rl_nchunks; --req->rl_nchunks)
		rpcrdma_unmap_one(device, &req->rl_segments[i++]);
}

static void
physical_op_destroy(struct rpcrdma_buffer *buf)
{
}

const struct rpcrdma_memreg_ops rpcrdma_physical_memreg_ops = {
	.ro_map				= physical_op_map,
	.ro_unmap_sync			= physical_op_unmap_sync,
	.ro_unmap			= physical_op_unmap,
	.ro_open			= physical_op_open,
	.ro_maxpages			= physical_op_maxpages,
	.ro_init			= physical_op_init,
	.ro_destroy			= physical_op_destroy,
	.ro_displayname			= "physical",
};
