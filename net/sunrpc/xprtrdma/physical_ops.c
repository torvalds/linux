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

/* PHYSICAL memory registration conveys one page per chunk segment.
 */
static size_t
physical_op_maxpages(struct rpcrdma_xprt *r_xprt)
{
	return min_t(unsigned int, RPCRDMA_MAX_DATA_SEGS,
		     rpcrdma_max_segments(r_xprt));
}

const struct rpcrdma_memreg_ops rpcrdma_physical_memreg_ops = {
	.ro_maxpages			= physical_op_maxpages,
	.ro_displayname			= "physical",
};
