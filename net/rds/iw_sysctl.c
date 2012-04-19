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

#include "iw.h"

static struct ctl_table_header *rds_iw_sysctl_hdr;

unsigned long rds_iw_sysctl_max_send_wr = RDS_IW_DEFAULT_SEND_WR;
unsigned long rds_iw_sysctl_max_recv_wr = RDS_IW_DEFAULT_RECV_WR;
unsigned long rds_iw_sysctl_max_recv_allocation = (128 * 1024 * 1024) / RDS_FRAG_SIZE;
static unsigned long rds_iw_sysctl_max_wr_min = 1;
/* hardware will fail CQ creation long before this */
static unsigned long rds_iw_sysctl_max_wr_max = (u32)~0;

unsigned long rds_iw_sysctl_max_unsig_wrs = 16;
static unsigned long rds_iw_sysctl_max_unsig_wr_min = 1;
static unsigned long rds_iw_sysctl_max_unsig_wr_max = 64;

unsigned long rds_iw_sysctl_max_unsig_bytes = (16 << 20);
static unsigned long rds_iw_sysctl_max_unsig_bytes_min = 1;
static unsigned long rds_iw_sysctl_max_unsig_bytes_max = ~0UL;

unsigned int rds_iw_sysctl_flow_control = 1;

static ctl_table rds_iw_sysctl_table[] = {
	{
		.procname       = "max_send_wr",
		.data		= &rds_iw_sysctl_max_send_wr,
		.maxlen         = sizeof(unsigned long),
		.mode           = 0644,
		.proc_handler   = proc_doulongvec_minmax,
		.extra1		= &rds_iw_sysctl_max_wr_min,
		.extra2		= &rds_iw_sysctl_max_wr_max,
	},
	{
		.procname       = "max_recv_wr",
		.data		= &rds_iw_sysctl_max_recv_wr,
		.maxlen         = sizeof(unsigned long),
		.mode           = 0644,
		.proc_handler   = proc_doulongvec_minmax,
		.extra1		= &rds_iw_sysctl_max_wr_min,
		.extra2		= &rds_iw_sysctl_max_wr_max,
	},
	{
		.procname       = "max_unsignaled_wr",
		.data		= &rds_iw_sysctl_max_unsig_wrs,
		.maxlen         = sizeof(unsigned long),
		.mode           = 0644,
		.proc_handler   = proc_doulongvec_minmax,
		.extra1		= &rds_iw_sysctl_max_unsig_wr_min,
		.extra2		= &rds_iw_sysctl_max_unsig_wr_max,
	},
	{
		.procname       = "max_unsignaled_bytes",
		.data		= &rds_iw_sysctl_max_unsig_bytes,
		.maxlen         = sizeof(unsigned long),
		.mode           = 0644,
		.proc_handler   = proc_doulongvec_minmax,
		.extra1		= &rds_iw_sysctl_max_unsig_bytes_min,
		.extra2		= &rds_iw_sysctl_max_unsig_bytes_max,
	},
	{
		.procname       = "max_recv_allocation",
		.data		= &rds_iw_sysctl_max_recv_allocation,
		.maxlen         = sizeof(unsigned long),
		.mode           = 0644,
		.proc_handler   = proc_doulongvec_minmax,
	},
	{
		.procname	= "flow_control",
		.data		= &rds_iw_sysctl_flow_control,
		.maxlen		= sizeof(rds_iw_sysctl_flow_control),
		.mode		= 0644,
		.proc_handler	= proc_dointvec,
	},
	{ }
};

static struct ctl_path rds_iw_sysctl_path[] = {
	{ .procname = "net", },
	{ .procname = "rds", },
	{ .procname = "iw", },
	{ }
};

void rds_iw_sysctl_exit(void)
{
	if (rds_iw_sysctl_hdr)
		unregister_net_sysctl_table(rds_iw_sysctl_hdr);
}

int rds_iw_sysctl_init(void)
{
	rds_iw_sysctl_hdr = register_net_sysctl_table(&init_net, rds_iw_sysctl_path, rds_iw_sysctl_table);
	if (!rds_iw_sysctl_hdr)
		return -ENOMEM;
	return 0;
}
