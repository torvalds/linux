// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/*
 * Copyright (c) 2014-2017 Oracle.  All rights reserved.
 * Copyright (c) 2003-2007 Network Appliance, Inc. All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the BSD-type
 * license below:
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *      Redistributions of source code must retain the above copyright
 *      notice, this list of conditions and the following disclaimer.
 *
 *      Redistributions in binary form must reproduce the above
 *      copyright notice, this list of conditions and the following
 *      disclaimer in the documentation and/or other materials provided
 *      with the distribution.
 *
 *      Neither the name of the Network Appliance, Inc. nor the names of
 *      its contributors may be used to endorse or promote products
 *      derived from this software without specific prior written
 *      permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * transport.c
 *
 * This file contains the top-level implementation of an RPC RDMA
 * transport.
 *
 * Naming convention: functions beginning with xprt_ are part of the
 * transport switch. All others are RPC RDMA internal.
 */

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/seq_file.h>
#include <linux/sunrpc/addr.h>

#include "xprt_rdma.h"

#if IS_ENABLED(CONFIG_SUNRPC_DEBUG)
# define RPCDBG_FACILITY	RPCDBG_TRANS
#endif

/*
 * tunables
 */

static unsigned int xprt_rdma_slot_table_entries = RPCRDMA_DEF_SLOT_TABLE;
unsigned int xprt_rdma_max_inline_read = RPCRDMA_DEF_INLINE;
static unsigned int xprt_rdma_max_inline_write = RPCRDMA_DEF_INLINE;
unsigned int xprt_rdma_memreg_strategy		= RPCRDMA_FRWR;
int xprt_rdma_pad_optimize;

#if IS_ENABLED(CONFIG_SUNRPC_DEBUG)

static unsigned int min_slot_table_size = RPCRDMA_MIN_SLOT_TABLE;
static unsigned int max_slot_table_size = RPCRDMA_MAX_SLOT_TABLE;
static unsigned int min_inline_size = RPCRDMA_MIN_INLINE;
static unsigned int max_inline_size = RPCRDMA_MAX_INLINE;
static unsigned int zero;
static unsigned int max_padding = PAGE_SIZE;
static unsigned int min_memreg = RPCRDMA_BOUNCEBUFFERS;
static unsigned int max_memreg = RPCRDMA_LAST - 1;
static unsigned int dummy;

static struct ctl_table_header *sunrpc_table_header;

static struct ctl_table xr_tunables_table[] = {
	{
		.procname	= "rdma_slot_table_entries",
		.data		= &xprt_rdma_slot_table_entries,
		.maxlen		= sizeof(unsigned int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_minmax,
		.extra1		= &min_slot_table_size,
		.extra2		= &max_slot_table_size
	},
	{
		.procname	= "rdma_max_inline_read",
		.data		= &xprt_rdma_max_inline_read,
		.maxlen		= sizeof(unsigned int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_minmax,
		.extra1		= &min_inline_size,
		.extra2		= &max_inline_size,
	},
	{
		.procname	= "rdma_max_inline_write",
		.data		= &xprt_rdma_max_inline_write,
		.maxlen		= sizeof(unsigned int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_minmax,
		.extra1		= &min_inline_size,
		.extra2		= &max_inline_size,
	},
	{
		.procname	= "rdma_inline_write_padding",
		.data		= &dummy,
		.maxlen		= sizeof(unsigned int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_minmax,
		.extra1		= &zero,
		.extra2		= &max_padding,
	},
	{
		.procname	= "rdma_memreg_strategy",
		.data		= &xprt_rdma_memreg_strategy,
		.maxlen		= sizeof(unsigned int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_minmax,
		.extra1		= &min_memreg,
		.extra2		= &max_memreg,
	},
	{
		.procname	= "rdma_pad_optimize",
		.data		= &xprt_rdma_pad_optimize,
		.maxlen		= sizeof(unsigned int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec,
	},
	{ },
};

static struct ctl_table sunrpc_table[] = {
	{
		.procname	= "sunrpc",
		.mode		= 0555,
		.child		= xr_tunables_table
	},
	{ },
};

#endif

static const struct rpc_xprt_ops xprt_rdma_procs;

static void
xprt_rdma_format_addresses4(struct rpc_xprt *xprt, struct sockaddr *sap)
{
	struct sockaddr_in *sin = (struct sockaddr_in *)sap;
	char buf[20];

	snprintf(buf, sizeof(buf), "%08x", ntohl(sin->sin_addr.s_addr));
	xprt->address_strings[RPC_DISPLAY_HEX_ADDR] = kstrdup(buf, GFP_KERNEL);

	xprt->address_strings[RPC_DISPLAY_NETID] = RPCBIND_NETID_RDMA;
}

static void
xprt_rdma_format_addresses6(struct rpc_xprt *xprt, struct sockaddr *sap)
{
	struct sockaddr_in6 *sin6 = (struct sockaddr_in6 *)sap;
	char buf[40];

	snprintf(buf, sizeof(buf), "%pi6", &sin6->sin6_addr);
	xprt->address_strings[RPC_DISPLAY_HEX_ADDR] = kstrdup(buf, GFP_KERNEL);

	xprt->address_strings[RPC_DISPLAY_NETID] = RPCBIND_NETID_RDMA6;
}

void
xprt_rdma_format_addresses(struct rpc_xprt *xprt, struct sockaddr *sap)
{
	char buf[128];

	switch (sap->sa_family) {
	case AF_INET:
		xprt_rdma_format_addresses4(xprt, sap);
		break;
	case AF_INET6:
		xprt_rdma_format_addresses6(xprt, sap);
		break;
	default:
		pr_err("rpcrdma: Unrecognized address family\n");
		return;
	}

	(void)rpc_ntop(sap, buf, sizeof(buf));
	xprt->address_strings[RPC_DISPLAY_ADDR] = kstrdup(buf, GFP_KERNEL);

	snprintf(buf, sizeof(buf), "%u", rpc_get_port(sap));
	xprt->address_strings[RPC_DISPLAY_PORT] = kstrdup(buf, GFP_KERNEL);

	snprintf(buf, sizeof(buf), "%4hx", rpc_get_port(sap));
	xprt->address_strings[RPC_DISPLAY_HEX_PORT] = kstrdup(buf, GFP_KERNEL);

	xprt->address_strings[RPC_DISPLAY_PROTO] = "rdma";
}

void
xprt_rdma_free_addresses(struct rpc_xprt *xprt)
{
	unsigned int i;

	for (i = 0; i < RPC_DISPLAY_MAX; i++)
		switch (i) {
		case RPC_DISPLAY_PROTO:
		case RPC_DISPLAY_NETID:
			continue;
		default:
			kfree(xprt->address_strings[i]);
		}
}

void
rpcrdma_conn_func(struct rpcrdma_ep *ep)
{
	schedule_delayed_work(&ep->rep_connect_worker, 0);
}

void
rpcrdma_connect_worker(struct work_struct *work)
{
	struct rpcrdma_ep *ep =
		container_of(work, struct rpcrdma_ep, rep_connect_worker.work);
	struct rpcrdma_xprt *r_xprt =
		container_of(ep, struct rpcrdma_xprt, rx_ep);
	struct rpc_xprt *xprt = &r_xprt->rx_xprt;

	spin_lock_bh(&xprt->transport_lock);
	if (ep->rep_connected > 0) {
		if (!xprt_test_and_set_connected(xprt))
			xprt_wake_pending_tasks(xprt, 0);
	} else {
		if (xprt_test_and_clear_connected(xprt))
			xprt_wake_pending_tasks(xprt, -ENOTCONN);
	}
	spin_unlock_bh(&xprt->transport_lock);
}

static void
xprt_rdma_connect_worker(struct work_struct *work)
{
	struct rpcrdma_xprt *r_xprt = container_of(work, struct rpcrdma_xprt,
						   rx_connect_worker.work);
	struct rpc_xprt *xprt = &r_xprt->rx_xprt;
	int rc = 0;

	xprt_clear_connected(xprt);

	rc = rpcrdma_ep_connect(&r_xprt->rx_ep, &r_xprt->rx_ia);
	if (rc)
		xprt_wake_pending_tasks(xprt, rc);

	xprt_clear_connecting(xprt);
}

static void
xprt_rdma_inject_disconnect(struct rpc_xprt *xprt)
{
	struct rpcrdma_xprt *r_xprt = container_of(xprt, struct rpcrdma_xprt,
						   rx_xprt);

	trace_xprtrdma_inject_dsc(r_xprt);
	rdma_disconnect(r_xprt->rx_ia.ri_id);
}

/*
 * xprt_rdma_destroy
 *
 * Destroy the xprt.
 * Free all memory associated with the object, including its own.
 * NOTE: none of the *destroy methods free memory for their top-level
 * objects, even though they may have allocated it (they do free
 * private memory). It's up to the caller to handle it. In this
 * case (RDMA transport), all structure memory is inlined with the
 * struct rpcrdma_xprt.
 */
static void
xprt_rdma_destroy(struct rpc_xprt *xprt)
{
	struct rpcrdma_xprt *r_xprt = rpcx_to_rdmax(xprt);

	trace_xprtrdma_destroy(r_xprt);

	cancel_delayed_work_sync(&r_xprt->rx_connect_worker);

	xprt_clear_connected(xprt);

	rpcrdma_ep_destroy(&r_xprt->rx_ep, &r_xprt->rx_ia);
	rpcrdma_buffer_destroy(&r_xprt->rx_buf);
	rpcrdma_ia_close(&r_xprt->rx_ia);

	xprt_rdma_free_addresses(xprt);
	xprt_free(xprt);

	module_put(THIS_MODULE);
}

static const struct rpc_timeout xprt_rdma_default_timeout = {
	.to_initval = 60 * HZ,
	.to_maxval = 60 * HZ,
};

/**
 * xprt_setup_rdma - Set up transport to use RDMA
 *
 * @args: rpc transport arguments
 */
static struct rpc_xprt *
xprt_setup_rdma(struct xprt_create *args)
{
	struct rpcrdma_create_data_internal cdata;
	struct rpc_xprt *xprt;
	struct rpcrdma_xprt *new_xprt;
	struct rpcrdma_ep *new_ep;
	struct sockaddr *sap;
	int rc;

	if (args->addrlen > sizeof(xprt->addr)) {
		dprintk("RPC:       %s: address too large\n", __func__);
		return ERR_PTR(-EBADF);
	}

	xprt = xprt_alloc(args->net, sizeof(struct rpcrdma_xprt),
			xprt_rdma_slot_table_entries,
			xprt_rdma_slot_table_entries);
	if (xprt == NULL) {
		dprintk("RPC:       %s: couldn't allocate rpcrdma_xprt\n",
			__func__);
		return ERR_PTR(-ENOMEM);
	}

	/* 60 second timeout, no retries */
	xprt->timeout = &xprt_rdma_default_timeout;
	xprt->bind_timeout = RPCRDMA_BIND_TO;
	xprt->reestablish_timeout = RPCRDMA_INIT_REEST_TO;
	xprt->idle_timeout = RPCRDMA_IDLE_DISC_TO;

	xprt->resvport = 0;		/* privileged port not needed */
	xprt->tsh_size = 0;		/* RPC-RDMA handles framing */
	xprt->ops = &xprt_rdma_procs;

	/*
	 * Set up RDMA-specific connect data.
	 */
	sap = args->dstaddr;

	/* Ensure xprt->addr holds valid server TCP (not RDMA)
	 * address, for any side protocols which peek at it */
	xprt->prot = IPPROTO_TCP;
	xprt->addrlen = args->addrlen;
	memcpy(&xprt->addr, sap, xprt->addrlen);

	if (rpc_get_port(sap))
		xprt_set_bound(xprt);
	xprt_rdma_format_addresses(xprt, sap);

	cdata.max_requests = xprt->max_reqs;

	cdata.rsize = RPCRDMA_MAX_SEGS * PAGE_SIZE; /* RDMA write max */
	cdata.wsize = RPCRDMA_MAX_SEGS * PAGE_SIZE; /* RDMA read max */

	cdata.inline_wsize = xprt_rdma_max_inline_write;
	if (cdata.inline_wsize > cdata.wsize)
		cdata.inline_wsize = cdata.wsize;

	cdata.inline_rsize = xprt_rdma_max_inline_read;
	if (cdata.inline_rsize > cdata.rsize)
		cdata.inline_rsize = cdata.rsize;

	/*
	 * Create new transport instance, which includes initialized
	 *  o ia
	 *  o endpoint
	 *  o buffers
	 */

	new_xprt = rpcx_to_rdmax(xprt);

	rc = rpcrdma_ia_open(new_xprt);
	if (rc)
		goto out1;

	/*
	 * initialize and create ep
	 */
	new_xprt->rx_data = cdata;
	new_ep = &new_xprt->rx_ep;

	rc = rpcrdma_ep_create(&new_xprt->rx_ep,
				&new_xprt->rx_ia, &new_xprt->rx_data);
	if (rc)
		goto out2;

	rc = rpcrdma_buffer_create(new_xprt);
	if (rc)
		goto out3;

	INIT_DELAYED_WORK(&new_xprt->rx_connect_worker,
			  xprt_rdma_connect_worker);

	xprt->max_payload = new_xprt->rx_ia.ri_ops->ro_maxpages(new_xprt);
	if (xprt->max_payload == 0)
		goto out4;
	xprt->max_payload <<= PAGE_SHIFT;
	dprintk("RPC:       %s: transport data payload maximum: %zu bytes\n",
		__func__, xprt->max_payload);

	if (!try_module_get(THIS_MODULE))
		goto out4;

	dprintk("RPC:       %s: %s:%s\n", __func__,
		xprt->address_strings[RPC_DISPLAY_ADDR],
		xprt->address_strings[RPC_DISPLAY_PORT]);
	trace_xprtrdma_create(new_xprt);
	return xprt;

out4:
	rpcrdma_buffer_destroy(&new_xprt->rx_buf);
	rc = -ENODEV;
out3:
	rpcrdma_ep_destroy(new_ep, &new_xprt->rx_ia);
out2:
	rpcrdma_ia_close(&new_xprt->rx_ia);
out1:
	trace_xprtrdma_destroy(new_xprt);
	xprt_rdma_free_addresses(xprt);
	xprt_free(xprt);
	return ERR_PTR(rc);
}

/**
 * xprt_rdma_close - Close down RDMA connection
 * @xprt: generic transport to be closed
 *
 * Called during transport shutdown reconnect, or device
 * removal. Caller holds the transport's write lock.
 */
static void
xprt_rdma_close(struct rpc_xprt *xprt)
{
	struct rpcrdma_xprt *r_xprt = rpcx_to_rdmax(xprt);
	struct rpcrdma_ep *ep = &r_xprt->rx_ep;
	struct rpcrdma_ia *ia = &r_xprt->rx_ia;

	dprintk("RPC:       %s: closing xprt %p\n", __func__, xprt);

	if (test_and_clear_bit(RPCRDMA_IAF_REMOVING, &ia->ri_flags)) {
		xprt_clear_connected(xprt);
		rpcrdma_ia_remove(ia);
		return;
	}
	if (ep->rep_connected == -ENODEV)
		return;
	if (ep->rep_connected > 0)
		xprt->reestablish_timeout = 0;
	xprt_disconnect_done(xprt);
	rpcrdma_ep_disconnect(ep, ia);
}

/**
 * xprt_rdma_set_port - update server port with rpcbind result
 * @xprt: controlling RPC transport
 * @port: new port value
 *
 * Transport connect status is unchanged.
 */
static void
xprt_rdma_set_port(struct rpc_xprt *xprt, u16 port)
{
	struct sockaddr *sap = (struct sockaddr *)&xprt->addr;
	char buf[8];

	dprintk("RPC:       %s: setting port for xprt %p (%s:%s) to %u\n",
		__func__, xprt,
		xprt->address_strings[RPC_DISPLAY_ADDR],
		xprt->address_strings[RPC_DISPLAY_PORT],
		port);

	rpc_set_port(sap, port);

	kfree(xprt->address_strings[RPC_DISPLAY_PORT]);
	snprintf(buf, sizeof(buf), "%u", port);
	xprt->address_strings[RPC_DISPLAY_PORT] = kstrdup(buf, GFP_KERNEL);

	kfree(xprt->address_strings[RPC_DISPLAY_HEX_PORT]);
	snprintf(buf, sizeof(buf), "%4hx", port);
	xprt->address_strings[RPC_DISPLAY_HEX_PORT] = kstrdup(buf, GFP_KERNEL);
}

/**
 * xprt_rdma_timer - invoked when an RPC times out
 * @xprt: controlling RPC transport
 * @task: RPC task that timed out
 *
 * Invoked when the transport is still connected, but an RPC
 * retransmit timeout occurs.
 *
 * Since RDMA connections don't have a keep-alive, forcibly
 * disconnect and retry to connect. This drives full
 * detection of the network path, and retransmissions of
 * all pending RPCs.
 */
static void
xprt_rdma_timer(struct rpc_xprt *xprt, struct rpc_task *task)
{
	xprt_force_disconnect(xprt);
}

static void
xprt_rdma_connect(struct rpc_xprt *xprt, struct rpc_task *task)
{
	struct rpcrdma_xprt *r_xprt = rpcx_to_rdmax(xprt);

	if (r_xprt->rx_ep.rep_connected != 0) {
		/* Reconnect */
		schedule_delayed_work(&r_xprt->rx_connect_worker,
				      xprt->reestablish_timeout);
		xprt->reestablish_timeout <<= 1;
		if (xprt->reestablish_timeout > RPCRDMA_MAX_REEST_TO)
			xprt->reestablish_timeout = RPCRDMA_MAX_REEST_TO;
		else if (xprt->reestablish_timeout < RPCRDMA_INIT_REEST_TO)
			xprt->reestablish_timeout = RPCRDMA_INIT_REEST_TO;
	} else {
		schedule_delayed_work(&r_xprt->rx_connect_worker, 0);
		if (!RPC_IS_ASYNC(task))
			flush_delayed_work(&r_xprt->rx_connect_worker);
	}
}

/**
 * xprt_rdma_alloc_slot - allocate an rpc_rqst
 * @xprt: controlling RPC transport
 * @task: RPC task requesting a fresh rpc_rqst
 *
 * tk_status values:
 *	%0 if task->tk_rqstp points to a fresh rpc_rqst
 *	%-EAGAIN if no rpc_rqst is available; queued on backlog
 */
static void
xprt_rdma_alloc_slot(struct rpc_xprt *xprt, struct rpc_task *task)
{
	struct rpc_rqst *rqst;

	spin_lock(&xprt->reserve_lock);
	if (list_empty(&xprt->free))
		goto out_sleep;
	rqst = list_first_entry(&xprt->free, struct rpc_rqst, rq_list);
	list_del(&rqst->rq_list);
	spin_unlock(&xprt->reserve_lock);

	task->tk_rqstp = rqst;
	task->tk_status = 0;
	return;

out_sleep:
	rpc_sleep_on(&xprt->backlog, task, NULL);
	spin_unlock(&xprt->reserve_lock);
	task->tk_status = -EAGAIN;
}

/**
 * xprt_rdma_free_slot - release an rpc_rqst
 * @xprt: controlling RPC transport
 * @rqst: rpc_rqst to release
 *
 */
static void
xprt_rdma_free_slot(struct rpc_xprt *xprt, struct rpc_rqst *rqst)
{
	memset(rqst, 0, sizeof(*rqst));

	spin_lock(&xprt->reserve_lock);
	list_add(&rqst->rq_list, &xprt->free);
	rpc_wake_up_next(&xprt->backlog);
	spin_unlock(&xprt->reserve_lock);
}

static bool
rpcrdma_get_sendbuf(struct rpcrdma_xprt *r_xprt, struct rpcrdma_req *req,
		    size_t size, gfp_t flags)
{
	struct rpcrdma_regbuf *rb;

	if (req->rl_sendbuf && rdmab_length(req->rl_sendbuf) >= size)
		return true;

	rb = rpcrdma_alloc_regbuf(size, DMA_TO_DEVICE, flags);
	if (IS_ERR(rb))
		return false;

	rpcrdma_free_regbuf(req->rl_sendbuf);
	r_xprt->rx_stats.hardway_register_count += size;
	req->rl_sendbuf = rb;
	return true;
}

/* The rq_rcv_buf is used only if a Reply chunk is necessary.
 * The decision to use a Reply chunk is made later in
 * rpcrdma_marshal_req. This buffer is registered at that time.
 *
 * Otherwise, the associated RPC Reply arrives in a separate
 * Receive buffer, arbitrarily chosen by the HCA. The buffer
 * allocated here for the RPC Reply is not utilized in that
 * case. See rpcrdma_inline_fixup.
 *
 * A regbuf is used here to remember the buffer size.
 */
static bool
rpcrdma_get_recvbuf(struct rpcrdma_xprt *r_xprt, struct rpcrdma_req *req,
		    size_t size, gfp_t flags)
{
	struct rpcrdma_regbuf *rb;

	if (req->rl_recvbuf && rdmab_length(req->rl_recvbuf) >= size)
		return true;

	rb = rpcrdma_alloc_regbuf(size, DMA_NONE, flags);
	if (IS_ERR(rb))
		return false;

	rpcrdma_free_regbuf(req->rl_recvbuf);
	r_xprt->rx_stats.hardway_register_count += size;
	req->rl_recvbuf = rb;
	return true;
}

/**
 * xprt_rdma_allocate - allocate transport resources for an RPC
 * @task: RPC task
 *
 * Return values:
 *        0:	Success; rq_buffer points to RPC buffer to use
 *   ENOMEM:	Out of memory, call again later
 *      EIO:	A permanent error occurred, do not retry
 *
 * The RDMA allocate/free functions need the task structure as a place
 * to hide the struct rpcrdma_req, which is necessary for the actual
 * send/recv sequence.
 *
 * xprt_rdma_allocate provides buffers that are already mapped for
 * DMA, and a local DMA lkey is provided for each.
 */
static int
xprt_rdma_allocate(struct rpc_task *task)
{
	struct rpc_rqst *rqst = task->tk_rqstp;
	struct rpcrdma_xprt *r_xprt = rpcx_to_rdmax(rqst->rq_xprt);
	struct rpcrdma_req *req;
	gfp_t flags;

	req = rpcrdma_buffer_get(&r_xprt->rx_buf);
	if (req == NULL)
		goto out_get;

	flags = RPCRDMA_DEF_GFP;
	if (RPC_IS_SWAPPER(task))
		flags = __GFP_MEMALLOC | GFP_NOWAIT | __GFP_NOWARN;

	if (!rpcrdma_get_sendbuf(r_xprt, req, rqst->rq_callsize, flags))
		goto out_fail;
	if (!rpcrdma_get_recvbuf(r_xprt, req, rqst->rq_rcvsize, flags))
		goto out_fail;

	rpcrdma_set_xprtdata(rqst, req);
	rqst->rq_buffer = req->rl_sendbuf->rg_base;
	rqst->rq_rbuffer = req->rl_recvbuf->rg_base;
	trace_xprtrdma_allocate(task, req);
	return 0;

out_fail:
	rpcrdma_buffer_put(req);
out_get:
	trace_xprtrdma_allocate(task, NULL);
	return -ENOMEM;
}

/**
 * xprt_rdma_free - release resources allocated by xprt_rdma_allocate
 * @task: RPC task
 *
 * Caller guarantees rqst->rq_buffer is non-NULL.
 */
static void
xprt_rdma_free(struct rpc_task *task)
{
	struct rpc_rqst *rqst = task->tk_rqstp;
	struct rpcrdma_xprt *r_xprt = rpcx_to_rdmax(rqst->rq_xprt);
	struct rpcrdma_req *req = rpcr_to_rdmar(rqst);

	if (test_bit(RPCRDMA_REQ_F_PENDING, &req->rl_flags))
		rpcrdma_release_rqst(r_xprt, req);
	trace_xprtrdma_rpc_done(task, req);
	rpcrdma_buffer_put(req);
}

/**
 * xprt_rdma_send_request - marshal and send an RPC request
 * @task: RPC task with an RPC message in rq_snd_buf
 *
 * Caller holds the transport's write lock.
 *
 * Returns:
 *	%0 if the RPC message has been sent
 *	%-ENOTCONN if the caller should reconnect and call again
 *	%-EAGAIN if the caller should call again
 *	%-ENOBUFS if the caller should call again after a delay
 *	%-EIO if a permanent error occurred and the request was not
 *		sent. Do not try to send this message again.
 */
static int
xprt_rdma_send_request(struct rpc_task *task)
{
	struct rpc_rqst *rqst = task->tk_rqstp;
	struct rpc_xprt *xprt = rqst->rq_xprt;
	struct rpcrdma_req *req = rpcr_to_rdmar(rqst);
	struct rpcrdma_xprt *r_xprt = rpcx_to_rdmax(xprt);
	int rc = 0;

#if defined(CONFIG_SUNRPC_BACKCHANNEL)
	if (unlikely(!rqst->rq_buffer))
		return xprt_rdma_bc_send_reply(rqst);
#endif	/* CONFIG_SUNRPC_BACKCHANNEL */

	if (!xprt_connected(xprt))
		goto drop_connection;

	rc = rpcrdma_marshal_req(r_xprt, rqst);
	if (rc < 0)
		goto failed_marshal;

	if (req->rl_reply == NULL) 		/* e.g. reconnection */
		rpcrdma_recv_buffer_get(req);

	/* Must suppress retransmit to maintain credits */
	if (rqst->rq_connect_cookie == xprt->connect_cookie)
		goto drop_connection;
	rqst->rq_xtime = ktime_get();

	__set_bit(RPCRDMA_REQ_F_PENDING, &req->rl_flags);
	if (rpcrdma_ep_post(&r_xprt->rx_ia, &r_xprt->rx_ep, req))
		goto drop_connection;

	rqst->rq_xmit_bytes_sent += rqst->rq_snd_buf.len;
	rqst->rq_bytes_sent = 0;

	/* An RPC with no reply will throw off credit accounting,
	 * so drop the connection to reset the credit grant.
	 */
	if (!rpc_reply_expected(task))
		goto drop_connection;
	return 0;

failed_marshal:
	if (rc != -ENOTCONN)
		return rc;
drop_connection:
	xprt_disconnect_done(xprt);
	return -ENOTCONN;	/* implies disconnect */
}

void xprt_rdma_print_stats(struct rpc_xprt *xprt, struct seq_file *seq)
{
	struct rpcrdma_xprt *r_xprt = rpcx_to_rdmax(xprt);
	long idle_time = 0;

	if (xprt_connected(xprt))
		idle_time = (long)(jiffies - xprt->last_used) / HZ;

	seq_puts(seq, "\txprt:\trdma ");
	seq_printf(seq, "%u %lu %lu %lu %ld %lu %lu %lu %llu %llu ",
		   0,	/* need a local port? */
		   xprt->stat.bind_count,
		   xprt->stat.connect_count,
		   xprt->stat.connect_time,
		   idle_time,
		   xprt->stat.sends,
		   xprt->stat.recvs,
		   xprt->stat.bad_xids,
		   xprt->stat.req_u,
		   xprt->stat.bklog_u);
	seq_printf(seq, "%lu %lu %lu %llu %llu %llu %llu %lu %lu %lu %lu ",
		   r_xprt->rx_stats.read_chunk_count,
		   r_xprt->rx_stats.write_chunk_count,
		   r_xprt->rx_stats.reply_chunk_count,
		   r_xprt->rx_stats.total_rdma_request,
		   r_xprt->rx_stats.total_rdma_reply,
		   r_xprt->rx_stats.pullup_copy_count,
		   r_xprt->rx_stats.fixup_copy_count,
		   r_xprt->rx_stats.hardway_register_count,
		   r_xprt->rx_stats.failed_marshal_count,
		   r_xprt->rx_stats.bad_reply_count,
		   r_xprt->rx_stats.nomsg_call_count);
	seq_printf(seq, "%lu %lu %lu %lu %lu %lu\n",
		   r_xprt->rx_stats.mrs_recovered,
		   r_xprt->rx_stats.mrs_orphaned,
		   r_xprt->rx_stats.mrs_allocated,
		   r_xprt->rx_stats.local_inv_needed,
		   r_xprt->rx_stats.empty_sendctx_q,
		   r_xprt->rx_stats.reply_waits_for_send);
}

static int
xprt_rdma_enable_swap(struct rpc_xprt *xprt)
{
	return 0;
}

static void
xprt_rdma_disable_swap(struct rpc_xprt *xprt)
{
}

/*
 * Plumbing for rpc transport switch and kernel module
 */

static const struct rpc_xprt_ops xprt_rdma_procs = {
	.reserve_xprt		= xprt_reserve_xprt_cong,
	.release_xprt		= xprt_release_xprt_cong, /* sunrpc/xprt.c */
	.alloc_slot		= xprt_rdma_alloc_slot,
	.free_slot		= xprt_rdma_free_slot,
	.release_request	= xprt_release_rqst_cong,       /* ditto */
	.set_retrans_timeout	= xprt_set_retrans_timeout_def, /* ditto */
	.timer			= xprt_rdma_timer,
	.rpcbind		= rpcb_getport_async,	/* sunrpc/rpcb_clnt.c */
	.set_port		= xprt_rdma_set_port,
	.connect		= xprt_rdma_connect,
	.buf_alloc		= xprt_rdma_allocate,
	.buf_free		= xprt_rdma_free,
	.send_request		= xprt_rdma_send_request,
	.close			= xprt_rdma_close,
	.destroy		= xprt_rdma_destroy,
	.print_stats		= xprt_rdma_print_stats,
	.enable_swap		= xprt_rdma_enable_swap,
	.disable_swap		= xprt_rdma_disable_swap,
	.inject_disconnect	= xprt_rdma_inject_disconnect,
#if defined(CONFIG_SUNRPC_BACKCHANNEL)
	.bc_setup		= xprt_rdma_bc_setup,
	.bc_up			= xprt_rdma_bc_up,
	.bc_maxpayload		= xprt_rdma_bc_maxpayload,
	.bc_free_rqst		= xprt_rdma_bc_free_rqst,
	.bc_destroy		= xprt_rdma_bc_destroy,
#endif
};

static struct xprt_class xprt_rdma = {
	.list			= LIST_HEAD_INIT(xprt_rdma.list),
	.name			= "rdma",
	.owner			= THIS_MODULE,
	.ident			= XPRT_TRANSPORT_RDMA,
	.setup			= xprt_setup_rdma,
};

void xprt_rdma_cleanup(void)
{
	int rc;

	dprintk("RPCRDMA Module Removed, deregister RPC RDMA transport\n");
#if IS_ENABLED(CONFIG_SUNRPC_DEBUG)
	if (sunrpc_table_header) {
		unregister_sysctl_table(sunrpc_table_header);
		sunrpc_table_header = NULL;
	}
#endif
	rc = xprt_unregister_transport(&xprt_rdma);
	if (rc)
		dprintk("RPC:       %s: xprt_unregister returned %i\n",
			__func__, rc);

	rpcrdma_destroy_wq();

	rc = xprt_unregister_transport(&xprt_rdma_bc);
	if (rc)
		dprintk("RPC:       %s: xprt_unregister(bc) returned %i\n",
			__func__, rc);
}

int xprt_rdma_init(void)
{
	int rc;

	rc = rpcrdma_alloc_wq();
	if (rc)
		return rc;

	rc = xprt_register_transport(&xprt_rdma);
	if (rc) {
		rpcrdma_destroy_wq();
		return rc;
	}

	rc = xprt_register_transport(&xprt_rdma_bc);
	if (rc) {
		xprt_unregister_transport(&xprt_rdma);
		rpcrdma_destroy_wq();
		return rc;
	}

	dprintk("RPCRDMA Module Init, register RPC RDMA transport\n");

	dprintk("Defaults:\n");
	dprintk("\tSlots %d\n"
		"\tMaxInlineRead %d\n\tMaxInlineWrite %d\n",
		xprt_rdma_slot_table_entries,
		xprt_rdma_max_inline_read, xprt_rdma_max_inline_write);
	dprintk("\tPadding 0\n\tMemreg %d\n", xprt_rdma_memreg_strategy);

#if IS_ENABLED(CONFIG_SUNRPC_DEBUG)
	if (!sunrpc_table_header)
		sunrpc_table_header = register_sysctl_table(sunrpc_table);
#endif
	return 0;
}
