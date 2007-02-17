/*
 *  net/dccp/sysctl.c
 *
 *  An implementation of the DCCP protocol
 *  Arnaldo Carvalho de Melo <acme@mandriva.com>
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License v2
 *	as published by the Free Software Foundation.
 */

#include <linux/mm.h>
#include <linux/sysctl.h>
#include "dccp.h"
#include "feat.h"

#ifndef CONFIG_SYSCTL
#error This file should not be compiled without CONFIG_SYSCTL defined
#endif

static struct ctl_table dccp_default_table[] = {
	{
		.procname	= "seq_window",
		.data		= &sysctl_dccp_feat_sequence_window,
		.maxlen		= sizeof(sysctl_dccp_feat_sequence_window),
		.mode		= 0644,
		.proc_handler	= proc_dointvec,
	},
	{
		.procname	= "rx_ccid",
		.data		= &sysctl_dccp_feat_rx_ccid,
		.maxlen		= sizeof(sysctl_dccp_feat_rx_ccid),
		.mode		= 0644,
		.proc_handler	= proc_dointvec,
	},
	{
		.procname	= "tx_ccid",
		.data		= &sysctl_dccp_feat_tx_ccid,
		.maxlen		= sizeof(sysctl_dccp_feat_tx_ccid),
		.mode		= 0644,
		.proc_handler	= proc_dointvec,
	},
	{
		.procname	= "ack_ratio",
		.data		= &sysctl_dccp_feat_ack_ratio,
		.maxlen		= sizeof(sysctl_dccp_feat_ack_ratio),
		.mode		= 0644,
		.proc_handler	= proc_dointvec,
	},
	{
		.procname	= "send_ackvec",
		.data		= &sysctl_dccp_feat_send_ack_vector,
		.maxlen		= sizeof(sysctl_dccp_feat_send_ack_vector),
		.mode		= 0644,
		.proc_handler	= proc_dointvec,
	},
	{
		.procname	= "send_ndp",
		.data		= &sysctl_dccp_feat_send_ndp_count,
		.maxlen		= sizeof(sysctl_dccp_feat_send_ndp_count),
		.mode		= 0644,
		.proc_handler	= proc_dointvec,
	},
	{
		.procname	= "request_retries",
		.data		= &sysctl_dccp_request_retries,
		.maxlen		= sizeof(sysctl_dccp_request_retries),
		.mode		= 0644,
		.proc_handler	= proc_dointvec,
	},
	{
		.procname	= "retries1",
		.data		= &sysctl_dccp_retries1,
		.maxlen		= sizeof(sysctl_dccp_retries1),
		.mode		= 0644,
		.proc_handler	= proc_dointvec,
	},
	{
		.procname	= "retries2",
		.data		= &sysctl_dccp_retries2,
		.maxlen		= sizeof(sysctl_dccp_retries2),
		.mode		= 0644,
		.proc_handler	= proc_dointvec,
	},
	{
		.procname	= "tx_qlen",
		.data		= &sysctl_dccp_tx_qlen,
		.maxlen		= sizeof(sysctl_dccp_tx_qlen),
		.mode		= 0644,
		.proc_handler	= proc_dointvec,
	},

	{ .ctl_name = 0, }
};

static struct ctl_table dccp_table[] = {
	{
		.ctl_name	= NET_DCCP_DEFAULT,
		.procname	= "default",
		.mode		= 0555,
		.child		= dccp_default_table,
	},
	{ .ctl_name = 0, },
};

static struct ctl_table dccp_dir_table[] = {
	{
		.ctl_name	= NET_DCCP,
		.procname	= "dccp",
		.mode		= 0555,
		.child		= dccp_table,
	},
	{ .ctl_name = 0, },
};

static struct ctl_table dccp_root_table[] = {
	{
		.ctl_name	= CTL_NET,
		.procname	= "net",
		.mode		= 0555,
		.child		= dccp_dir_table,
	},
	{ .ctl_name = 0, },
};

static struct ctl_table_header *dccp_table_header;

int __init dccp_sysctl_init(void)
{
	dccp_table_header = register_sysctl_table(dccp_root_table);

	return dccp_table_header != NULL ? 0 : -ENOMEM;
}

void dccp_sysctl_exit(void)
{
	if (dccp_table_header != NULL) {
		unregister_sysctl_table(dccp_table_header);
		dccp_table_header = NULL;
	}
}
