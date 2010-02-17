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

#include "rds.h"

static struct ctl_table_header *rds_sysctl_reg_table;

static unsigned long rds_sysctl_reconnect_min = 1;
static unsigned long rds_sysctl_reconnect_max = ~0UL;

unsigned long rds_sysctl_reconnect_min_jiffies;
unsigned long rds_sysctl_reconnect_max_jiffies = HZ;

unsigned int  rds_sysctl_max_unacked_packets = 8;
unsigned int  rds_sysctl_max_unacked_bytes = (16 << 20);

unsigned int rds_sysctl_ping_enable = 1;

static ctl_table rds_sysctl_rds_table[] = {
	{
		.procname       = "reconnect_min_delay_ms",
		.data		= &rds_sysctl_reconnect_min_jiffies,
		.maxlen         = sizeof(unsigned long),
		.mode           = 0644,
		.proc_handler   = proc_doulongvec_ms_jiffies_minmax,
		.extra1		= &rds_sysctl_reconnect_min,
		.extra2		= &rds_sysctl_reconnect_max_jiffies,
	},
	{
		.procname       = "reconnect_max_delay_ms",
		.data		= &rds_sysctl_reconnect_max_jiffies,
		.maxlen         = sizeof(unsigned long),
		.mode           = 0644,
		.proc_handler   = proc_doulongvec_ms_jiffies_minmax,
		.extra1		= &rds_sysctl_reconnect_min_jiffies,
		.extra2		= &rds_sysctl_reconnect_max,
	},
	{
		.procname	= "max_unacked_packets",
		.data		= &rds_sysctl_max_unacked_packets,
		.maxlen         = sizeof(unsigned long),
		.mode           = 0644,
		.proc_handler   = proc_dointvec,
	},
	{
		.procname	= "max_unacked_bytes",
		.data		= &rds_sysctl_max_unacked_bytes,
		.maxlen         = sizeof(unsigned long),
		.mode           = 0644,
		.proc_handler   = proc_dointvec,
	},
	{
		.procname	= "ping_enable",
		.data		= &rds_sysctl_ping_enable,
		.maxlen         = sizeof(int),
		.mode           = 0644,
		.proc_handler   = proc_dointvec,
	},
	{ }
};

static struct ctl_path rds_sysctl_path[] = {
	{ .procname = "net", },
	{ .procname = "rds", },
	{ }
};


void rds_sysctl_exit(void)
{
	if (rds_sysctl_reg_table)
		unregister_sysctl_table(rds_sysctl_reg_table);
}

int __init rds_sysctl_init(void)
{
	rds_sysctl_reconnect_min = msecs_to_jiffies(1);
	rds_sysctl_reconnect_min_jiffies = rds_sysctl_reconnect_min;

	rds_sysctl_reg_table = register_sysctl_paths(rds_sysctl_path, rds_sysctl_rds_table);
	if (rds_sysctl_reg_table == NULL)
		return -ENOMEM;
	return 0;
}
