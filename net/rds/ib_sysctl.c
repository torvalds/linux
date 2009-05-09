/*
 * Copyright (c) 2006 Oracle.  All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 */
#include <linux/kernel.h>
#include <linux/sysctl.h>
#include <linux/proc_fs.h>

#include "ib.h"

static struct ctl_table_header *rds_ib_sysctl_hdr;

unsigned long rds_ib_sysctl_max_send_wr = RDS_IB_DEFAULT_SEND_WR;
unsigned long rds_ib_sysctl_max_recv_wr = RDS_IB_DEFAULT_RECV_WR;
unsigned long rds_ib_sysctl_max_recv_allocation = (128 * 1024 * 1024) / RDS_FRAG_SIZE;
static unsigned long rds_ib_sysctl_max_wr_min = 1;
/* hardware will fail CQ creation long before this */
static unsigned long rds_ib_sysctl_max_wr_max = (u32)~0;

unsigned long rds_ib_sysctl_max_unsig_wrs = 16;
static unsigned long rds_ib_sysctl_max_unsig_wr_min = 1;
static unsigned long rds_ib_sysctl_max_unsig_wr_max = 64;

unsigned long rds_ib_sysctl_max_unsig_bytes = (16 << 20);
static unsigned long rds_ib_sysctl_max_unsig_bytes_min = 1;
static unsigned long rds_ib_sysctl_max_unsig_bytes_max = ~0UL;

unsigned int rds_ib_sysctl_flow_control = 1;

ctl_table rds_ib_sysctl_table[] = {
	{
		.ctl_name       = CTL_UNNUMBERED,
		.procname       = "max_send_wr",
		.data		= &rds_ib_sysctl_max_send_wr,
		.maxlen         = sizeof(unsigned long),
		.mode           = 0644,
		.proc_handler   = &proc_doulongvec_minmax,
		.extra1		= &rds_ib_sysctl_max_wr_min,
		.extra2		= &rds_ib_sysctl_max_wr_max,
	},
	{
		.ctl_name       = CTL_UNNUMBERED,
		.procname       = "max_recv_wr",
		.data		= &rds_ib_sysctl_max_recv_wr,
		.maxlen         = sizeof(unsigned long),
		.mode           = 0644,
		.proc_handler   = &proc_doulongvec_minmax,
		.extra1		= &rds_ib_sysctl_max_wr_min,
		.extra2		= &rds_ib_sysctl_max_wr_max,
	},
	{
		.ctl_name       = CTL_UNNUMBERED,
		.procname       = "max_unsignaled_wr",
		.data		= &rds_ib_sysctl_max_unsig_wrs,
		.maxlen         = sizeof(unsigned long),
		.mode           = 0644,
		.proc_handler   = &proc_doulongvec_minmax,
		.extra1		= &rds_ib_sysctl_max_unsig_wr_min,
		.extra2		= &rds_ib_sysctl_max_unsig_wr_max,
	},
	{
		.ctl_name       = CTL_UNNUMBERED,
		.procname       = "max_unsignaled_bytes",
		.data		= &rds_ib_sysctl_max_unsig_bytes,
		.maxlen         = sizeof(unsigned long),
		.mode           = 0644,
		.proc_handler   = &proc_doulongvec_minmax,
		.extra1		= &rds_ib_sysctl_max_unsig_bytes_min,
		.extra2		= &rds_ib_sysctl_max_unsig_bytes_max,
	},
	{
		.ctl_name       = CTL_UNNUMBERED,
		.procname       = "max_recv_allocation",
		.data		= &rds_ib_sysctl_max_recv_allocation,
		.maxlen         = sizeof(unsigned long),
		.mode           = 0644,
		.proc_handler   = &proc_doulongvec_minmax,
	},
	{
		.ctl_name	= CTL_UNNUMBERED,
		.procname	= "flow_control",
		.data		= &rds_ib_sysctl_flow_control,
		.maxlen		= sizeof(rds_ib_sysctl_flow_control),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec,
	},
	{ .ctl_name = 0}
};

static struct ctl_path rds_ib_sysctl_path[] = {
	{ .procname = "net", .ctl_name = CTL_NET, },
	{ .procname = "rds", .ctl_name = CTL_UNNUMBERED, },
	{ .procname = "ib", .ctl_name = CTL_UNNUMBERED, },
	{ }
};

void rds_ib_sysctl_exit(void)
{
	if (rds_ib_sysctl_hdr)
		unregister_sysctl_table(rds_ib_sysctl_hdr);
}

int __init rds_ib_sysctl_init(void)
{
	rds_ib_sysctl_hdr = register_sysctl_paths(rds_ib_sysctl_path, rds_ib_sysctl_table);
	if (rds_ib_sysctl_hdr == NULL)
		return -ENOMEM;
	return 0;
}
