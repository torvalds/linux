/*
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
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/seq_file.h>
#include <linux/sunrpc/addr.h>

#include "xprt_rdma.h"

#ifdef RPC_DEBUG
# define RPCDBG_FACILITY	RPCDBG_TRANS
#endif

MODULE_LICENSE("Dual BSD/GPL");

MODULE_DESCRIPTION("RPC/RDMA Transport for Linux kernel NFS");
MODULE_AUTHOR("Network Appliance, Inc.");

/*
 * tunables
 */

static unsigned int xprt_rdma_slot_table_entries = RPCRDMA_DEF_SLOT_TABLE;
static unsigned int xprt_rdma_max_inline_read = RPCRDMA_DEF_INLINE;
static unsigned int xprt_rdma_max_inline_write = RPCRDMA_DEF_INLINE;
static unsigned int xprt_rdma_inline_write_padding;
static unsigned int xprt_rdma_memreg_strategy = RPCRDMA_FRMR;
                int xprt_rdma_pad_optimize = 0;

#ifdef RPC_DEBUG

static unsigned int min_slot_table_size = RPCRDMA_MIN_SLOT_TABLE;
static unsigned int max_slot_table_size = RPCRDMA_MAX_SLOT_TABLE;
static unsigned int zero;
static unsigned int max_padding = PAGE_SIZE;
static unsigned int min_memreg = RPCRDMA_BOUNCEBUFFERS;
static unsigned int max_memreg = RPCRDMA_LAST - 1;

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
		.proc_handler	= proc_dointvec,
	},
	{
		.procname	= "rdma_max_inline_write",
		.data		= &xprt_rdma_max_inline_write,
		.maxlen		= sizeof(unsigned int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec,
	},
	{
		.procname	= "rdma_inline_write_padding",
		.data		= &xprt_rdma_inline_write_padding,
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

#define RPCRDMA_BIND_TO		(60U * HZ)
#define RPCRDMA_INIT_REEST_TO	(5U * HZ)
#define RPCRDMA_MAX_REEST_TO	(30U * HZ)
#define RPCRDMA_IDLE_DISC_TO	(5U * 60 * HZ)

static struct rpc_xprt_ops xprt_rdma_procs;	/* forward reference */

static void
xprt_rdma_format_addresses(struct rpc_xprt *xprt)
{
	struct sockaddr *sap = (struct sockaddr *)
					&rpcx_to_rdmad(xprt).addr;
	struct sockaddr_in *sin = (struct sockaddr_in *)sap;
	char buf[64];

	(void)rpc_ntop(sap, buf, sizeof(buf));
	xprt->address_strings[RPC_DISPLAY_ADDR] = kstrdup(buf, GFP_KERNEL);

	snprintf(buf, sizeof(buf), "%u", rpc_get_port(sap));
	xprt->address_strings[RPC_DISPLAY_PORT] = kstrdup(buf, GFP_KERNEL);

	xprt->address_strings[RPC_DISPLAY_PROTO] = "rdma";

	snprintf(buf, sizeof(buf), "%08x", ntohl(sin->sin_addr.s_addr));
	xprt->address_strings[RPC_DISPLAY_HEX_ADDR] = kstrdup(buf, GFP_KERNEL);

	snprintf(buf, sizeof(buf), "%4hx", rpc_get_port(sap));
	xprt->address_strings[RPC_DISPLAY_HEX_PORT] = kstrdup(buf, GFP_KERNEL);

	/* netid */
	xprt->address_strings[RPC_DISPLAY_NETID] = "rdma";
}

static void
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

static void
xprt_rdma_connect_worker(struct work_struct *work)
{
	struct rpcrdma_xprt *r_xprt =
		container_of(work, struct rpcrdma_xprt, rdma_connect.work);
	struct rpc_xprt *xprt = &r_xprt->xprt;
	int rc = 0;

	current->flags |= PF_FSTRANS;
	xprt_clear_connected(xprt);

	dprintk("RPC:       %s: %sconnect\n", __func__,
			r_xprt->rx_ep.rep_connected != 0 ? "re" : "");
	rc = rpcrdma_ep_connect(&r_xprt->rx_ep, &r_xprt->rx_ia);
	if (rc)
		xprt_wake_pending_tasks(xprt, rc);

	dprintk("RPC:       %s: exit\n", __func__);
	xprt_clear_connecting(xprt);
	current->flags &= ~PF_FSTRANS;
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

	dprintk("RPC:       %s: called\n", __func__);

	cancel_delayed_work_sync(&r_xprt->rdma_connect);

	xprt_clear_connected(xprt);

	rpcrdma_buffer_destroy(&r_xprt->rx_buf);
	rpcrdma_ep_destroy(&r_xprt->rx_ep, &r_xprt->rx_ia);
	rpcrdma_ia_close(&r_xprt->rx_ia);

	xprt_rdma_free_addresses(xprt);

	xprt_free(xprt);

	dprintk("RPC:       %s: returning\n", __func__);

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
	struct sockaddr_in *sin;
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

	/* Put server RDMA address in local cdata */
	memcpy(&cdata.addr, args->dstaddr, args->addrlen);

	/* Ensure xprt->addr holds valid server TCP (not RDMA)
	 * address, for any side protocols which peek at it */
	xprt->prot = IPPROTO_TCP;
	xprt->addrlen = args->addrlen;
	memcpy(&xprt->addr, &cdata.addr, xprt->addrlen);

	sin = (struct sockaddr_in *)&cdata.addr;
	if (ntohs(sin->sin_port) != 0)
		xprt_set_bound(xprt);

	dprintk("RPC:       %s: %pI4:%u\n",
		__func__, &sin->sin_addr.s_addr, ntohs(sin->sin_port));

	/* Set max requests */
	cdata.max_requests = xprt->max_reqs;

	/* Set some length limits */
	cdata.rsize = RPCRDMA_MAX_SEGS * PAGE_SIZE; /* RDMA write max */
	cdata.wsize = RPCRDMA_MAX_SEGS * PAGE_SIZE; /* RDMA read max */

	cdata.inline_wsize = xprt_rdma_max_inline_write;
	if (cdata.inline_wsize > cdata.wsize)
		cdata.inline_wsize = cdata.wsize;

	cdata.inline_rsize = xprt_rdma_max_inline_read;
	if (cdata.inline_rsize > cdata.rsize)
		cdata.inline_rsize = cdata.rsize;

	cdata.padding = xprt_rdma_inline_write_padding;

	/*
	 * Create new transport instance, which includes initialized
	 *  o ia
	 *  o endpoint
	 *  o buffers
	 */

	new_xprt = rpcx_to_rdmax(xprt);

	rc = rpcrdma_ia_open(new_xprt, (struct sockaddr *) &cdata.addr,
				xprt_rdma_memreg_strategy);
	if (rc)
		goto out1;

	/*
	 * initialize and create ep
	 */
	new_xprt->rx_data = cdata;
	new_ep = &new_xprt->rx_ep;
	new_ep->rep_remote_addr = cdata.addr;

	rc = rpcrdma_ep_create(&new_xprt->rx_ep,
				&new_xprt->rx_ia, &new_xprt->rx_data);
	if (rc)
		goto out2;

	/*
	 * Allocate pre-registered send and receive buffers for headers and
	 * any inline data. Also specify any padding which will be provided
	 * from a preregistered zero buffer.
	 */
	rc = rpcrdma_buffer_create(&new_xprt->rx_buf, new_ep, &new_xprt->rx_ia,
				&new_xprt->rx_data);
	if (rc)
		goto out3;

	/*
	 * Register a callback for connection events. This is necessary because
	 * connection loss notification is async. We also catch connection loss
	 * when reaping receives.
	 */
	INIT_DELAYED_WORK(&new_xprt->rdma_connect, xprt_rdma_connect_worker);
	new_ep->rep_func = rpcrdma_conn_func;
	new_ep->rep_xprt = xprt;

	xprt_rdma_format_addresses(xprt);
	xprt->max_payload = rpcrdma_max_payload(new_xprt);
	dprintk("RPC:       %s: transport data payload maximum: %zu bytes\n",
		__func__, xprt->max_payload);

	if (!try_module_get(THIS_MODULE))
		goto out4;

	return xprt;

out4:
	xprt_rdma_free_addresses(xprt);
	rc = -EINVAL;
out3:
	rpcrdma_ep_destroy(new_ep, &new_xprt->rx_ia);
out2:
	rpcrdma_ia_close(&new_xprt->rx_ia);
out1:
	xprt_free(xprt);
	return ERR_PTR(rc);
}

/*
 * Close a connection, during shutdown or timeout/reconnect
 */
static void
xprt_rdma_close(struct rpc_xprt *xprt)
{
	struct rpcrdma_xprt *r_xprt = rpcx_to_rdmax(xprt);

	dprintk("RPC:       %s: closing\n", __func__);
	if (r_xprt->rx_ep.rep_connected > 0)
		xprt->reestablish_timeout = 0;
	xprt_disconnect_done(xprt);
	(void) rpcrdma_ep_disconnect(&r_xprt->rx_ep, &r_xprt->rx_ia);
}

static void
xprt_rdma_set_port(struct rpc_xprt *xprt, u16 port)
{
	struct sockaddr_in *sap;

	sap = (struct sockaddr_in *)&xprt->addr;
	sap->sin_port = htons(port);
	sap = (struct sockaddr_in *)&rpcx_to_rdmad(xprt).addr;
	sap->sin_port = htons(port);
	dprintk("RPC:       %s: %u\n", __func__, port);
}

static void
xprt_rdma_connect(struct rpc_xprt *xprt, struct rpc_task *task)
{
	struct rpcrdma_xprt *r_xprt = rpcx_to_rdmax(xprt);

	if (r_xprt->rx_ep.rep_connected != 0) {
		/* Reconnect */
		schedule_delayed_work(&r_xprt->rdma_connect,
			xprt->reestablish_timeout);
		xprt->reestablish_timeout <<= 1;
		if (xprt->reestablish_timeout > RPCRDMA_MAX_REEST_TO)
			xprt->reestablish_timeout = RPCRDMA_MAX_REEST_TO;
		else if (xprt->reestablish_timeout < RPCRDMA_INIT_REEST_TO)
			xprt->reestablish_timeout = RPCRDMA_INIT_REEST_TO;
	} else {
		schedule_delayed_work(&r_xprt->rdma_connect, 0);
		if (!RPC_IS_ASYNC(task))
			flush_delayed_work(&r_xprt->rdma_connect);
	}
}

/*
 * The RDMA allocate/free functions need the task structure as a place
 * to hide the struct rpcrdma_req, which is necessary for the actual send/recv
 * sequence. For this reason, the recv buffers are attached to send
 * buffers for portions of the RPC. Note that the RPC layer allocates
 * both send and receive buffers in the same call. We may register
 * the receive buffer portion when using reply chunks.
 */
static void *
xprt_rdma_allocate(struct rpc_task *task, size_t size)
{
	struct rpc_xprt *xprt = task->tk_rqstp->rq_xprt;
	struct rpcrdma_req *req, *nreq;

	req = rpcrdma_buffer_get(&rpcx_to_rdmax(xprt)->rx_buf);
	if (req == NULL)
		return NULL;

	if (size > req->rl_size) {
		dprintk("RPC:       %s: size %zd too large for buffer[%zd]: "
			"prog %d vers %d proc %d\n",
			__func__, size, req->rl_size,
			task->tk_client->cl_prog, task->tk_client->cl_vers,
			task->tk_msg.rpc_proc->p_proc);
		/*
		 * Outgoing length shortage. Our inline write max must have
		 * been configured to perform direct i/o.
		 *
		 * This is therefore a large metadata operation, and the
		 * allocate call was made on the maximum possible message,
		 * e.g. containing long filename(s) or symlink data. In
		 * fact, while these metadata operations *might* carry
		 * large outgoing payloads, they rarely *do*. However, we
		 * have to commit to the request here, so reallocate and
		 * register it now. The data path will never require this
		 * reallocation.
		 *
		 * If the allocation or registration fails, the RPC framework
		 * will (doggedly) retry.
		 */
		if (task->tk_flags & RPC_TASK_SWAPPER)
			nreq = kmalloc(sizeof *req + size, GFP_ATOMIC);
		else
			nreq = kmalloc(sizeof *req + size, GFP_NOFS);
		if (nreq == NULL)
			goto outfail;

		if (rpcrdma_register_internal(&rpcx_to_rdmax(xprt)->rx_ia,
				nreq->rl_base, size + sizeof(struct rpcrdma_req)
				- offsetof(struct rpcrdma_req, rl_base),
				&nreq->rl_handle, &nreq->rl_iov)) {
			kfree(nreq);
			goto outfail;
		}
		rpcx_to_rdmax(xprt)->rx_stats.hardway_register_count += size;
		nreq->rl_size = size;
		nreq->rl_niovs = 0;
		nreq->rl_nchunks = 0;
		nreq->rl_buffer = (struct rpcrdma_buffer *)req;
		nreq->rl_reply = req->rl_reply;
		memcpy(nreq->rl_segments,
			req->rl_segments, sizeof nreq->rl_segments);
		/* flag the swap with an unused field */
		nreq->rl_iov.length = 0;
		req->rl_reply = NULL;
		req = nreq;
	}
	dprintk("RPC:       %s: size %zd, request 0x%p\n", __func__, size, req);
	req->rl_connect_cookie = 0;	/* our reserved value */
	return req->rl_xdr_buf;

outfail:
	rpcrdma_buffer_put(req);
	rpcx_to_rdmax(xprt)->rx_stats.failed_marshal_count++;
	return NULL;
}

/*
 * This function returns all RDMA resources to the pool.
 */
static void
xprt_rdma_free(void *buffer)
{
	struct rpcrdma_req *req;
	struct rpcrdma_xprt *r_xprt;
	struct rpcrdma_rep *rep;
	int i;

	if (buffer == NULL)
		return;

	req = container_of(buffer, struct rpcrdma_req, rl_xdr_buf[0]);
	if (req->rl_iov.length == 0) {	/* see allocate above */
		r_xprt = container_of(((struct rpcrdma_req *) req->rl_buffer)->rl_buffer,
				      struct rpcrdma_xprt, rx_buf);
	} else
		r_xprt = container_of(req->rl_buffer, struct rpcrdma_xprt, rx_buf);
	rep = req->rl_reply;

	dprintk("RPC:       %s: called on 0x%p%s\n",
		__func__, rep, (rep && rep->rr_func) ? " (with waiter)" : "");

	/*
	 * Finish the deregistration.  The process is considered
	 * complete when the rr_func vector becomes NULL - this
	 * was put in place during rpcrdma_reply_handler() - the wait
	 * call below will not block if the dereg is "done". If
	 * interrupted, our framework will clean up.
	 */
	for (i = 0; req->rl_nchunks;) {
		--req->rl_nchunks;
		i += rpcrdma_deregister_external(
			&req->rl_segments[i], r_xprt);
	}

	if (req->rl_iov.length == 0) {	/* see allocate above */
		struct rpcrdma_req *oreq = (struct rpcrdma_req *)req->rl_buffer;
		oreq->rl_reply = req->rl_reply;
		(void) rpcrdma_deregister_internal(&r_xprt->rx_ia,
						   req->rl_handle,
						   &req->rl_iov);
		kfree(req);
		req = oreq;
	}

	/* Put back request+reply buffers */
	rpcrdma_buffer_put(req);
}

/*
 * send_request invokes the meat of RPC RDMA. It must do the following:
 *  1.  Marshal the RPC request into an RPC RDMA request, which means
 *	putting a header in front of data, and creating IOVs for RDMA
 *	from those in the request.
 *  2.  In marshaling, detect opportunities for RDMA, and use them.
 *  3.  Post a recv message to set up asynch completion, then send
 *	the request (rpcrdma_ep_post).
 *  4.  No partial sends are possible in the RPC-RDMA protocol (as in UDP).
 */

static int
xprt_rdma_send_request(struct rpc_task *task)
{
	struct rpc_rqst *rqst = task->tk_rqstp;
	struct rpc_xprt *xprt = rqst->rq_xprt;
	struct rpcrdma_req *req = rpcr_to_rdmar(rqst);
	struct rpcrdma_xprt *r_xprt = rpcx_to_rdmax(xprt);
	int rc = 0;

	if (req->rl_niovs == 0)
		rc = rpcrdma_marshal_req(rqst);
	else if (r_xprt->rx_ia.ri_memreg_strategy == RPCRDMA_FRMR)
		rc = rpcrdma_marshal_chunks(rqst, 0);
	if (rc < 0)
		goto failed_marshal;

	if (req->rl_reply == NULL) 		/* e.g. reconnection */
		rpcrdma_recv_buffer_get(req);

	if (req->rl_reply) {
		req->rl_reply->rr_func = rpcrdma_reply_handler;
		/* this need only be done once, but... */
		req->rl_reply->rr_xprt = xprt;
	}

	/* Must suppress retransmit to maintain credits */
	if (req->rl_connect_cookie == xprt->connect_cookie)
		goto drop_connection;
	req->rl_connect_cookie = xprt->connect_cookie;

	if (rpcrdma_ep_post(&r_xprt->rx_ia, &r_xprt->rx_ep, req))
		goto drop_connection;

	rqst->rq_xmit_bytes_sent += rqst->rq_snd_buf.len;
	rqst->rq_bytes_sent = 0;
	return 0;

failed_marshal:
	r_xprt->rx_stats.failed_marshal_count++;
	dprintk("RPC:       %s: rpcrdma_marshal_req failed, status %i\n",
		__func__, rc);
	if (rc == -EIO)
		return -EIO;
drop_connection:
	xprt_disconnect_done(xprt);
	return -ENOTCONN;	/* implies disconnect */
}

static void xprt_rdma_print_stats(struct rpc_xprt *xprt, struct seq_file *seq)
{
	struct rpcrdma_xprt *r_xprt = rpcx_to_rdmax(xprt);
	long idle_time = 0;

	if (xprt_connected(xprt))
		idle_time = (long)(jiffies - xprt->last_used) / HZ;

	seq_printf(seq,
	  "\txprt:\trdma %u %lu %lu %lu %ld %lu %lu %lu %Lu %Lu "
	  "%lu %lu %lu %Lu %Lu %Lu %Lu %lu %lu %lu\n",

	   0,	/* need a local port? */
	   xprt->stat.bind_count,
	   xprt->stat.connect_count,
	   xprt->stat.connect_time,
	   idle_time,
	   xprt->stat.sends,
	   xprt->stat.recvs,
	   xprt->stat.bad_xids,
	   xprt->stat.req_u,
	   xprt->stat.bklog_u,

	   r_xprt->rx_stats.read_chunk_count,
	   r_xprt->rx_stats.write_chunk_count,
	   r_xprt->rx_stats.reply_chunk_count,
	   r_xprt->rx_stats.total_rdma_request,
	   r_xprt->rx_stats.total_rdma_reply,
	   r_xprt->rx_stats.pullup_copy_count,
	   r_xprt->rx_stats.fixup_copy_count,
	   r_xprt->rx_stats.hardway_register_count,
	   r_xprt->rx_stats.failed_marshal_count,
	   r_xprt->rx_stats.bad_reply_count);
}

/*
 * Plumbing for rpc transport switch and kernel module
 */

static struct rpc_xprt_ops xprt_rdma_procs = {
	.reserve_xprt		= xprt_reserve_xprt_cong,
	.release_xprt		= xprt_release_xprt_cong, /* sunrpc/xprt.c */
	.alloc_slot		= xprt_alloc_slot,
	.release_request	= xprt_release_rqst_cong,       /* ditto */
	.set_retrans_timeout	= xprt_set_retrans_timeout_def, /* ditto */
	.rpcbind		= rpcb_getport_async,	/* sunrpc/rpcb_clnt.c */
	.set_port		= xprt_rdma_set_port,
	.connect		= xprt_rdma_connect,
	.buf_alloc		= xprt_rdma_allocate,
	.buf_free		= xprt_rdma_free,
	.send_request		= xprt_rdma_send_request,
	.close			= xprt_rdma_close,
	.destroy		= xprt_rdma_destroy,
	.print_stats		= xprt_rdma_print_stats
};

static struct xprt_class xprt_rdma = {
	.list			= LIST_HEAD_INIT(xprt_rdma.list),
	.name			= "rdma",
	.owner			= THIS_MODULE,
	.ident			= XPRT_TRANSPORT_RDMA,
	.setup			= xprt_setup_rdma,
};

static void __exit xprt_rdma_cleanup(void)
{
	int rc;

	dprintk("RPCRDMA Module Removed, deregister RPC RDMA transport\n");
#ifdef RPC_DEBUG
	if (sunrpc_table_header) {
		unregister_sysctl_table(sunrpc_table_header);
		sunrpc_table_header = NULL;
	}
#endif
	rc = xprt_unregister_transport(&xprt_rdma);
	if (rc)
		dprintk("RPC:       %s: xprt_unregister returned %i\n",
			__func__, rc);
}

static int __init xprt_rdma_init(void)
{
	int rc;

	rc = xprt_register_transport(&xprt_rdma);

	if (rc)
		return rc;

	dprintk("RPCRDMA Module Init, register RPC RDMA transport\n");

	dprintk("Defaults:\n");
	dprintk("\tSlots %d\n"
		"\tMaxInlineRead %d\n\tMaxInlineWrite %d\n",
		xprt_rdma_slot_table_entries,
		xprt_rdma_max_inline_read, xprt_rdma_max_inline_write);
	dprintk("\tPadding %d\n\tMemreg %d\n",
		xprt_rdma_inline_write_padding, xprt_rdma_memreg_strategy);

#ifdef RPC_DEBUG
	if (!sunrpc_table_header)
		sunrpc_table_header = register_sysctl_table(sunrpc_table);
#endif
	return 0;
}

module_init(xprt_rdma_init);
module_exit(xprt_rdma_cleanup);
