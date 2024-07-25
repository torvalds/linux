// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/*
 * Copyright (c) 2015-2018 Oracle.  All rights reserved.
 * Copyright (c) 2005-2006 Network Appliance, Inc. All rights reserved.
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
 *
 * Author: Tom Tucker <tom@opengridcomputing.com>
 */

#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/sysctl.h>
#include <linux/workqueue.h>
#include <linux/sunrpc/clnt.h>
#include <linux/sunrpc/sched.h>
#include <linux/sunrpc/svc_rdma.h>

#define RPCDBG_FACILITY	RPCDBG_SVCXPRT

/* RPC/RDMA parameters */
unsigned int svcrdma_ord = 16;	/* historical default */
static unsigned int min_ord = 1;
static unsigned int max_ord = 255;
unsigned int svcrdma_max_requests = RPCRDMA_MAX_REQUESTS;
unsigned int svcrdma_max_bc_requests = RPCRDMA_MAX_BC_REQUESTS;
static unsigned int min_max_requests = 4;
static unsigned int max_max_requests = 16384;
unsigned int svcrdma_max_req_size = RPCRDMA_DEF_INLINE_THRESH;
static unsigned int min_max_inline = RPCRDMA_DEF_INLINE_THRESH;
static unsigned int max_max_inline = RPCRDMA_MAX_INLINE_THRESH;
static unsigned int svcrdma_stat_unused;
static unsigned int zero;

struct percpu_counter svcrdma_stat_read;
struct percpu_counter svcrdma_stat_recv;
struct percpu_counter svcrdma_stat_sq_starve;
struct percpu_counter svcrdma_stat_write;

enum {
	SVCRDMA_COUNTER_BUFSIZ	= sizeof(unsigned long long),
};

static int svcrdma_counter_handler(const struct ctl_table *table, int write,
				   void *buffer, size_t *lenp, loff_t *ppos)
{
	struct percpu_counter *stat = (struct percpu_counter *)table->data;
	char tmp[SVCRDMA_COUNTER_BUFSIZ + 1];
	int len;

	if (write) {
		percpu_counter_set(stat, 0);
		return 0;
	}

	len = snprintf(tmp, SVCRDMA_COUNTER_BUFSIZ, "%lld\n",
		       percpu_counter_sum_positive(stat));
	if (len >= SVCRDMA_COUNTER_BUFSIZ)
		return -EFAULT;
	len = strlen(tmp);
	if (*ppos > len) {
		*lenp = 0;
		return 0;
	}
	len -= *ppos;
	if (len > *lenp)
		len = *lenp;
	if (len)
		memcpy(buffer, tmp, len);
	*lenp = len;
	*ppos += len;

	return 0;
}

static struct ctl_table_header *svcrdma_table_header;
static struct ctl_table svcrdma_parm_table[] = {
	{
		.procname	= "max_requests",
		.data		= &svcrdma_max_requests,
		.maxlen		= sizeof(unsigned int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_minmax,
		.extra1		= &min_max_requests,
		.extra2		= &max_max_requests
	},
	{
		.procname	= "max_req_size",
		.data		= &svcrdma_max_req_size,
		.maxlen		= sizeof(unsigned int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_minmax,
		.extra1		= &min_max_inline,
		.extra2		= &max_max_inline
	},
	{
		.procname	= "max_outbound_read_requests",
		.data		= &svcrdma_ord,
		.maxlen		= sizeof(unsigned int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_minmax,
		.extra1		= &min_ord,
		.extra2		= &max_ord,
	},

	{
		.procname	= "rdma_stat_read",
		.data		= &svcrdma_stat_read,
		.maxlen		= SVCRDMA_COUNTER_BUFSIZ,
		.mode		= 0644,
		.proc_handler	= svcrdma_counter_handler,
	},
	{
		.procname	= "rdma_stat_recv",
		.data		= &svcrdma_stat_recv,
		.maxlen		= SVCRDMA_COUNTER_BUFSIZ,
		.mode		= 0644,
		.proc_handler	= svcrdma_counter_handler,
	},
	{
		.procname	= "rdma_stat_write",
		.data		= &svcrdma_stat_write,
		.maxlen		= SVCRDMA_COUNTER_BUFSIZ,
		.mode		= 0644,
		.proc_handler	= svcrdma_counter_handler,
	},
	{
		.procname	= "rdma_stat_sq_starve",
		.data		= &svcrdma_stat_sq_starve,
		.maxlen		= SVCRDMA_COUNTER_BUFSIZ,
		.mode		= 0644,
		.proc_handler	= svcrdma_counter_handler,
	},
	{
		.procname	= "rdma_stat_rq_starve",
		.data		= &svcrdma_stat_unused,
		.maxlen		= sizeof(unsigned int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_minmax,
		.extra1		= &zero,
		.extra2		= &zero,
	},
	{
		.procname	= "rdma_stat_rq_poll",
		.data		= &svcrdma_stat_unused,
		.maxlen		= sizeof(unsigned int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_minmax,
		.extra1		= &zero,
		.extra2		= &zero,
	},
	{
		.procname	= "rdma_stat_rq_prod",
		.data		= &svcrdma_stat_unused,
		.maxlen		= sizeof(unsigned int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_minmax,
		.extra1		= &zero,
		.extra2		= &zero,
	},
	{
		.procname	= "rdma_stat_sq_poll",
		.data		= &svcrdma_stat_unused,
		.maxlen		= sizeof(unsigned int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_minmax,
		.extra1		= &zero,
		.extra2		= &zero,
	},
	{
		.procname	= "rdma_stat_sq_prod",
		.data		= &svcrdma_stat_unused,
		.maxlen		= sizeof(unsigned int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_minmax,
		.extra1		= &zero,
		.extra2		= &zero,
	},
};

static void svc_rdma_proc_cleanup(void)
{
	if (!svcrdma_table_header)
		return;
	unregister_sysctl_table(svcrdma_table_header);
	svcrdma_table_header = NULL;

	percpu_counter_destroy(&svcrdma_stat_write);
	percpu_counter_destroy(&svcrdma_stat_sq_starve);
	percpu_counter_destroy(&svcrdma_stat_recv);
	percpu_counter_destroy(&svcrdma_stat_read);
}

static int svc_rdma_proc_init(void)
{
	int rc;

	if (svcrdma_table_header)
		return 0;

	rc = percpu_counter_init(&svcrdma_stat_read, 0, GFP_KERNEL);
	if (rc)
		goto out_err;
	rc = percpu_counter_init(&svcrdma_stat_recv, 0, GFP_KERNEL);
	if (rc)
		goto out_err;
	rc = percpu_counter_init(&svcrdma_stat_sq_starve, 0, GFP_KERNEL);
	if (rc)
		goto out_err;
	rc = percpu_counter_init(&svcrdma_stat_write, 0, GFP_KERNEL);
	if (rc)
		goto out_err;

	svcrdma_table_header = register_sysctl("sunrpc/svc_rdma",
					       svcrdma_parm_table);
	return 0;

out_err:
	percpu_counter_destroy(&svcrdma_stat_sq_starve);
	percpu_counter_destroy(&svcrdma_stat_recv);
	percpu_counter_destroy(&svcrdma_stat_read);
	return rc;
}

struct workqueue_struct *svcrdma_wq;

void svc_rdma_cleanup(void)
{
	svc_unreg_xprt_class(&svc_rdma_class);
	svc_rdma_proc_cleanup();
	if (svcrdma_wq) {
		struct workqueue_struct *wq = svcrdma_wq;

		svcrdma_wq = NULL;
		destroy_workqueue(wq);
	}

	dprintk("SVCRDMA Module Removed, deregister RPC RDMA transport\n");
}

int svc_rdma_init(void)
{
	struct workqueue_struct *wq;
	int rc;

	wq = alloc_workqueue("svcrdma", WQ_UNBOUND, 0);
	if (!wq)
		return -ENOMEM;

	rc = svc_rdma_proc_init();
	if (rc) {
		destroy_workqueue(wq);
		return rc;
	}

	svcrdma_wq = wq;
	svc_reg_xprt_class(&svc_rdma_class);

	dprintk("SVCRDMA Module Init, register RPC RDMA transport\n");
	dprintk("\tsvcrdma_ord      : %d\n", svcrdma_ord);
	dprintk("\tmax_requests     : %u\n", svcrdma_max_requests);
	dprintk("\tmax_bc_requests  : %u\n", svcrdma_max_bc_requests);
	dprintk("\tmax_inline       : %d\n", svcrdma_max_req_size);
	return 0;
}
