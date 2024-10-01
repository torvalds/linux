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
#include <linux/sunrpc/rdma_rn.h>

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
	rpcrdma_ib_client_unregister();
}

static int __init rpc_rdma_init(void)
{
	int rc;

	rc = rpcrdma_ib_client_register();
	if (rc)
		goto out_rc;

	rc = svc_rdma_init();
	if (rc)
		goto out_ib_client;

	rc = xprt_rdma_init();
	if (rc)
		goto out_svc_rdma;

	return 0;

out_svc_rdma:
	svc_rdma_cleanup();
out_ib_client:
	rpcrdma_ib_client_unregister();
out_rc:
	return rc;
}

module_init(rpc_rdma_init);
module_exit(rpc_rdma_cleanup);
