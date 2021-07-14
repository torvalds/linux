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
#include <linux/smp.h>

#include <linux/sunrpc/addr.h>
#include <linux/sunrpc/svc_rdma.h>

#include "xprt_rdma.h"
#include <trace/events/rpcrdma.h>

#if IS_ENABLED(CONFIG_SUNRPC_DEBUG)
# define RPCDBG_FACILITY	RPCDBG_TRANS
#endif

/*
 * tunables
 */

static unsigned int xprt_rdma_slot_table_entries = RPCRDMA_DEF_SLOT_TABLE;
unsigned int xprt_rdma_max_inline_read = RPCRDMA_DEF_INLINE;
unsigned int xprt_rdma_max_inline_write = RPCRDMA_DEF_INLINE;
unsigned int xprt_rdma_memreg_strategy		= RPCRDMA_FRWR;
int xprt_rdma_pad_optimize;

#if IS_ENABLED(CONFIG_SUNRPC_DEBUG)

static unsigned int min_slot_table_size = RPCRDMA_MIN_SLOT_TABLE;
static unsigned int max_slot_table_size = RPCRDMA_MAX_SLOT_TABLE;
static unsigned int min_inline_size = RPCRDMA_MIN_INLINE;
static unsigned int max_inline_size = RPCRDMA_MAX_INLINE;
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
		.extra1		= SYSCTL_ZERO,
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

/**
 * xprt_rdma_connect_worker - establish connection in the background
 * @work: worker thread context
 *
 * Requester holds the xprt's send lock to prevent activity on this
 * transport while a fresh connection is being established. RPC tasks
 * sleep on the xprt's pending queue waiting for connect to complete.
 */
static void
xprt_rdma_connect_worker(struct work_struct *work)
{
	struct rpcrdma_xprt *r_xprt = container_of(work, struct rpcrdma_xprt,
						   rx_connect_worker.work);
	struct rpc_xprt *xprt = &r_xprt->rx_xprt;
	int rc;

	rc = rpcrdma_xprt_connect(r_xprt);
	xprt_clear_connecting(xprt);
	if (!rc) {
		xprt->connect_cookie++;
		xprt->stat.connect_count++;
		xprt->stat.connect_time += (long)jiffies -
					   xprt->stat.connect_start;
		xprt_set_connected(xprt);
		rc = -EAGAIN;
	} else {
		/* Force a call to xprt_rdma_close to clean up */
		spin_lock(&xprt->transport_lock);
		set_bit(XPRT_CLOSE_WAIT, &xprt->state);
		spin_unlock(&xprt->transport_lock);
	}
	xprt_wake_pending_tasks(xprt, rc);
}

/**
 * xprt_rdma_inject_disconnect - inject a connection fault
 * @xprt: transport context
 *
 * If @xprt is connected, disconnect it to simulate spurious
 * connection loss. Caller must hold @xprt's send lock to
 * ensure that data structures and hardware resources are
 * stable during the rdma_disconnect() call.
 */
static void
xprt_rdma_inject_disconnect(struct rpc_xprt *xprt)
{
	struct rpcrdma_xprt *r_xprt = rpcx_to_rdmax(xprt);

	trace_xprtrdma_op_inject_dsc(r_xprt);
	rdma_disconnect(r_xprt->rx_ep->re_id);
}

/**
 * xprt_rdma_destroy - Full tear down of transport
 * @xprt: doomed transport context
 *
 * Caller guarantees there will be no more calls to us with
 * this @xprt.
 */
static void
xprt_rdma_destroy(struct rpc_xprt *xprt)
{
	struct rpcrdma_xprt *r_xprt = rpcx_to_rdmax(xprt);

	cancel_delayed_work_sync(&r_xprt->rx_connect_worker);

	rpcrdma_xprt_disconnect(r_xprt);
	rpcrdma_buffer_destroy(&r_xprt->rx_buf);

	xprt_rdma_free_addresses(xprt);
	xprt_free(xprt);

	module_put(THIS_MODULE);
}

/* 60 second timeout, no retries */
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
	struct rpc_xprt *xprt;
	struct rpcrdma_xprt *new_xprt;
	struct sockaddr *sap;
	int rc;

	if (args->addrlen > sizeof(xprt->addr))
		return ERR_PTR(-EBADF);

	if (!try_module_get(THIS_MODULE))
		return ERR_PTR(-EIO);

	xprt = xprt_alloc(args->net, sizeof(struct rpcrdma_xprt), 0,
			  xprt_rdma_slot_table_entries);
	if (!xprt) {
		module_put(THIS_MODULE);
		return ERR_PTR(-ENOMEM);
	}

	xprt->timeout = &xprt_rdma_default_timeout;
	xprt->connect_timeout = xprt->timeout->to_initval;
	xprt->max_reconnect_timeout = xprt->timeout->to_maxval;
	xprt->bind_timeout = RPCRDMA_BIND_TO;
	xprt->reestablish_timeout = RPCRDMA_INIT_REEST_TO;
	xprt->idle_timeout = RPCRDMA_IDLE_DISC_TO;

	xprt->resvport = 0;		/* privileged port not needed */
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

	new_xprt = rpcx_to_rdmax(xprt);
	rc = rpcrdma_buffer_create(new_xprt);
	if (rc) {
		xprt_rdma_free_addresses(xprt);
		xprt_free(xprt);
		module_put(THIS_MODULE);
		return ERR_PTR(rc);
	}

	INIT_DELAYED_WORK(&new_xprt->rx_connect_worker,
			  xprt_rdma_connect_worker);

	xprt->max_payload = RPCRDMA_MAX_DATA_SEGS << PAGE_SHIFT;

	return xprt;
}

/**
 * xprt_rdma_close - close a transport connection
 * @xprt: transport context
 *
 * Called during autoclose or device removal.
 *
 * Caller holds @xprt's send lock to prevent activity on this
 * transport while the connection is torn down.
 */
void xprt_rdma_close(struct rpc_xprt *xprt)
{
	struct rpcrdma_xprt *r_xprt = rpcx_to_rdmax(xprt);

	rpcrdma_xprt_disconnect(r_xprt);

	xprt->reestablish_timeout = 0;
	++xprt->connect_cookie;
	xprt_disconnect_done(xprt);
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

/**
 * xprt_rdma_set_connect_timeout - set timeouts for establishing a connection
 * @xprt: controlling transport instance
 * @connect_timeout: reconnect timeout after client disconnects
 * @reconnect_timeout: reconnect timeout after server disconnects
 *
 */
static void xprt_rdma_set_connect_timeout(struct rpc_xprt *xprt,
					  unsigned long connect_timeout,
					  unsigned long reconnect_timeout)
{
	struct rpcrdma_xprt *r_xprt = rpcx_to_rdmax(xprt);

	trace_xprtrdma_op_set_cto(r_xprt, connect_timeout, reconnect_timeout);

	spin_lock(&xprt->transport_lock);

	if (connect_timeout < xprt->connect_timeout) {
		struct rpc_timeout to;
		unsigned long initval;

		to = *xprt->timeout;
		initval = connect_timeout;
		if (initval < RPCRDMA_INIT_REEST_TO << 1)
			initval = RPCRDMA_INIT_REEST_TO << 1;
		to.to_initval = initval;
		to.to_maxval = initval;
		r_xprt->rx_timeout = to;
		xprt->timeout = &r_xprt->rx_timeout;
		xprt->connect_timeout = connect_timeout;
	}

	if (reconnect_timeout < xprt->max_reconnect_timeout)
		xprt->max_reconnect_timeout = reconnect_timeout;

	spin_unlock(&xprt->transport_lock);
}

/**
 * xprt_rdma_connect - schedule an attempt to reconnect
 * @xprt: transport state
 * @task: RPC scheduler context (unused)
 *
 */
static void
xprt_rdma_connect(struct rpc_xprt *xprt, struct rpc_task *task)
{
	struct rpcrdma_xprt *r_xprt = rpcx_to_rdmax(xprt);
	struct rpcrdma_ep *ep = r_xprt->rx_ep;
	unsigned long delay;

	delay = 0;
	if (ep && ep->re_connect_status != 0) {
		delay = xprt_reconnect_delay(xprt);
		xprt_reconnect_backoff(xprt, RPCRDMA_INIT_REEST_TO);
	}
	trace_xprtrdma_op_connect(r_xprt, delay);
	queue_delayed_work(xprtiod_workqueue, &r_xprt->rx_connect_worker,
			   delay);
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
	struct rpcrdma_xprt *r_xprt = rpcx_to_rdmax(xprt);
	struct rpcrdma_req *req;

	req = rpcrdma_buffer_get(&r_xprt->rx_buf);
	if (!req)
		goto out_sleep;
	task->tk_rqstp = &req->rl_slot;
	task->tk_status = 0;
	return;

out_sleep:
	task->tk_status = -EAGAIN;
	xprt_add_backlog(xprt, task);
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
	struct rpcrdma_xprt *r_xprt =
		container_of(xprt, struct rpcrdma_xprt, rx_xprt);

	rpcrdma_reply_put(&r_xprt->rx_buf, rpcr_to_rdmar(rqst));
	if (!xprt_wake_up_backlog(xprt, rqst)) {
		memset(rqst, 0, sizeof(*rqst));
		rpcrdma_buffer_put(&r_xprt->rx_buf, rpcr_to_rdmar(rqst));
	}
}

static bool rpcrdma_check_regbuf(struct rpcrdma_xprt *r_xprt,
				 struct rpcrdma_regbuf *rb, size_t size,
				 gfp_t flags)
{
	if (unlikely(rdmab_length(rb) < size)) {
		if (!rpcrdma_regbuf_realloc(rb, size, flags))
			return false;
		r_xprt->rx_stats.hardway_register_count += size;
	}
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
 */
static int
xprt_rdma_allocate(struct rpc_task *task)
{
	struct rpc_rqst *rqst = task->tk_rqstp;
	struct rpcrdma_xprt *r_xprt = rpcx_to_rdmax(rqst->rq_xprt);
	struct rpcrdma_req *req = rpcr_to_rdmar(rqst);
	gfp_t flags;

	flags = RPCRDMA_DEF_GFP;
	if (RPC_IS_SWAPPER(task))
		flags = __GFP_MEMALLOC | GFP_NOWAIT | __GFP_NOWARN;

	if (!rpcrdma_check_regbuf(r_xprt, req->rl_sendbuf, rqst->rq_callsize,
				  flags))
		goto out_fail;
	if (!rpcrdma_check_regbuf(r_xprt, req->rl_recvbuf, rqst->rq_rcvsize,
				  flags))
		goto out_fail;

	rqst->rq_buffer = rdmab_data(req->rl_sendbuf);
	rqst->rq_rbuffer = rdmab_data(req->rl_recvbuf);
	return 0;

out_fail:
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
	struct rpcrdma_req *req = rpcr_to_rdmar(rqst);

	if (unlikely(!list_empty(&req->rl_registered))) {
		trace_xprtrdma_mrs_zap(task);
		frwr_unmap_sync(rpcx_to_rdmax(rqst->rq_xprt), req);
	}

	/* XXX: If the RPC is completing because of a signal and
	 * not because a reply was received, we ought to ensure
	 * that the Send completion has fired, so that memory
	 * involved with the Send is not still visible to the NIC.
	 */
}

/**
 * xprt_rdma_send_request - marshal and send an RPC request
 * @rqst: RPC message in rq_snd_buf
 *
 * Caller holds the transport's write lock.
 *
 * Returns:
 *	%0 if the RPC message has been sent
 *	%-ENOTCONN if the caller should reconnect and call again
 *	%-EAGAIN if the caller should call again
 *	%-ENOBUFS if the caller should call again after a delay
 *	%-EMSGSIZE if encoding ran out of buffer space. The request
 *		was not sent. Do not try to send this message again.
 *	%-EIO if an I/O error occurred. The request was not sent.
 *		Do not try to send this message again.
 */
static int
xprt_rdma_send_request(struct rpc_rqst *rqst)
{
	struct rpc_xprt *xprt = rqst->rq_xprt;
	struct rpcrdma_req *req = rpcr_to_rdmar(rqst);
	struct rpcrdma_xprt *r_xprt = rpcx_to_rdmax(xprt);
	int rc = 0;

#if defined(CONFIG_SUNRPC_BACKCHANNEL)
	if (unlikely(!rqst->rq_buffer))
		return xprt_rdma_bc_send_reply(rqst);
#endif	/* CONFIG_SUNRPC_BACKCHANNEL */

	if (!xprt_connected(xprt))
		return -ENOTCONN;

	if (!xprt_request_get_cong(xprt, rqst))
		return -EBADSLT;

	rc = rpcrdma_marshal_req(r_xprt, rqst);
	if (rc < 0)
		goto failed_marshal;

	/* Must suppress retransmit to maintain credits */
	if (rqst->rq_connect_cookie == xprt->connect_cookie)
		goto drop_connection;
	rqst->rq_xtime = ktime_get();

	if (rpcrdma_post_sends(r_xprt, req))
		goto drop_connection;

	rqst->rq_xmit_bytes_sent += rqst->rq_snd_buf.len;

	/* An RPC with no reply will throw off credit accounting,
	 * so drop the connection to reset the credit grant.
	 */
	if (!rpc_reply_expected(rqst->rq_task))
		goto drop_connection;
	return 0;

failed_marshal:
	if (rc != -ENOTCONN)
		return rc;
drop_connection:
	xprt_rdma_close(xprt);
	return -ENOTCONN;
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
		   xprt->stat.connect_time / HZ,
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
		   r_xprt->rx_stats.mrs_recycled,
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
	.wait_for_reply_request	= xprt_wait_for_reply_request_def, /* ditto */
	.timer			= xprt_rdma_timer,
	.rpcbind		= rpcb_getport_async,	/* sunrpc/rpcb_clnt.c */
	.set_port		= xprt_rdma_set_port,
	.connect		= xprt_rdma_connect,
	.buf_alloc		= xprt_rdma_allocate,
	.buf_free		= xprt_rdma_free,
	.send_request		= xprt_rdma_send_request,
	.close			= xprt_rdma_close,
	.destroy		= xprt_rdma_destroy,
	.set_connect_timeout	= xprt_rdma_set_connect_timeout,
	.print_stats		= xprt_rdma_print_stats,
	.enable_swap		= xprt_rdma_enable_swap,
	.disable_swap		= xprt_rdma_disable_swap,
	.inject_disconnect	= xprt_rdma_inject_disconnect,
#if defined(CONFIG_SUNRPC_BACKCHANNEL)
	.bc_setup		= xprt_rdma_bc_setup,
	.bc_maxpayload		= xprt_rdma_bc_maxpayload,
	.bc_num_slots		= xprt_rdma_bc_max_slots,
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
	.netid			= { "rdma", "rdma6", "" },
};

void xprt_rdma_cleanup(void)
{
#if IS_ENABLED(CONFIG_SUNRPC_DEBUG)
	if (sunrpc_table_header) {
		unregister_sysctl_table(sunrpc_table_header);
		sunrpc_table_header = NULL;
	}
#endif

	xprt_unregister_transport(&xprt_rdma);
	xprt_unregister_transport(&xprt_rdma_bc);
}

int xprt_rdma_init(void)
{
	int rc;

	rc = xprt_register_transport(&xprt_rdma);
	if (rc)
		return rc;

	rc = xprt_register_transport(&xprt_rdma_bc);
	if (rc) {
		xprt_unregister_transport(&xprt_rdma);
		return rc;
	}

#if IS_ENABLED(CONFIG_SUNRPC_DEBUG)
	if (!sunrpc_table_header)
		sunrpc_table_header = register_sysctl_table(sunrpc_table);
#endif
	return 0;
}
