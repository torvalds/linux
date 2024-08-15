// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/*
 * Copyright (c) 2015, 2017 Oracle.  All rights reserved.
 */

/* rpcrdma.ko module initialization
 */

#include <linux/types.h>
#include <linux/compiler.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/sunrpc/svc_rdma.h>

#include <asm/swab.h>

#include "xprt_rdma.h"

#define CREATE_TRACE_POINTS
#include <trace/events/rpcrdma.h>

MODULE_AUTHOR("Open Grid Computing and Network Appliance, Inc.");
MODULE_DESCRIPTION("RPC/RDMA Transport");
MODULE_LICENSE("Dual BSD/GPL");
MODULE_ALIAS("svcrdma");
MODULE_ALIAS("xprtrdma");
MODULE_ALIAS("rpcrdma6");

static void __exit rpc_rdma_cleanup(void)
{
	xprt_rdma_cleanup();
	svc_rdma_cleanup();
}

static int __init rpc_rdma_init(void)
{
	int rc;

	rc = svc_rdma_init();
	if (rc)
		goto out;

	rc = xprt_rdma_init();
	if (rc)
		svc_rdma_cleanup();

out:
	return rc;
}

module_init(rpc_rdma_init);
module_exit(rpc_rdma_cleanup);
