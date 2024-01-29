// SPDX-License-Identifier: GPL-2.0
/*
 *  Shared Memory Communications over RDMA (SMC-R) and RoCE
 *
 *  smc_sysctl.c: sysctl interface to SMC subsystem.
 *
 *  Copyright (c) 2022, Alibaba Inc.
 *
 *  Author: Tony Lu <tonylu@linux.alibaba.com>
 *
 */

#include <linux/init.h>
#include <linux/sysctl.h>
#include <net/net_namespace.h>

#include "smc.h"
#include "smc_core.h"
#include "smc_llc.h"
#include "smc_sysctl.h"

static int min_sndbuf = SMC_BUF_MIN_SIZE;
static int min_rcvbuf = SMC_BUF_MIN_SIZE;
static int max_sndbuf = INT_MAX / 2;
static int max_rcvbuf = INT_MAX / 2;
static const int net_smc_wmem_init = (64 * 1024);
static const int net_smc_rmem_init = (64 * 1024);
static int links_per_lgr_min = SMC_LINKS_ADD_LNK_MIN;
static int links_per_lgr_max = SMC_LINKS_ADD_LNK_MAX;
static int conns_per_lgr_min = SMC_CONN_PER_LGR_MIN;
static int conns_per_lgr_max = SMC_CONN_PER_LGR_MAX;

static struct ctl_table smc_table[] = {
	{
		.procname       = "autocorking_size",
		.data           = &init_net.smc.sysctl_autocorking_size,
		.maxlen         = sizeof(unsigned int),
		.mode           = 0644,
		.proc_handler	= proc_douintvec,
	},
	{
		.procname	= "smcr_buf_type",
		.data		= &init_net.smc.sysctl_smcr_buf_type,
		.maxlen		= sizeof(unsigned int),
		.mode		= 0644,
		.proc_handler	= proc_douintvec_minmax,
		.extra1		= SYSCTL_ZERO,
		.extra2		= SYSCTL_TWO,
	},
	{
		.procname	= "smcr_testlink_time",
		.data		= &init_net.smc.sysctl_smcr_testlink_time,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_jiffies,
	},
	{
		.procname	= "wmem",
		.data		= &init_net.smc.sysctl_wmem,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_minmax,
		.extra1		= &min_sndbuf,
		.extra2		= &max_sndbuf,
	},
	{
		.procname	= "rmem",
		.data		= &init_net.smc.sysctl_rmem,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_minmax,
		.extra1		= &min_rcvbuf,
		.extra2		= &max_rcvbuf,
	},
	{
		.procname	= "smcr_max_links_per_lgr",
		.data		= &init_net.smc.sysctl_max_links_per_lgr,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_minmax,
		.extra1		= &links_per_lgr_min,
		.extra2		= &links_per_lgr_max,
	},
	{
		.procname	= "smcr_max_conns_per_lgr",
		.data		= &init_net.smc.sysctl_max_conns_per_lgr,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_minmax,
		.extra1		= &conns_per_lgr_min,
		.extra2		= &conns_per_lgr_max,
	},
	{  }
};

int __net_init smc_sysctl_net_init(struct net *net)
{
	struct ctl_table *table;

	table = smc_table;
	if (!net_eq(net, &init_net)) {
		int i;

		table = kmemdup(table, sizeof(smc_table), GFP_KERNEL);
		if (!table)
			goto err_alloc;

		for (i = 0; i < ARRAY_SIZE(smc_table) - 1; i++)
			table[i].data += (void *)net - (void *)&init_net;
	}

	net->smc.smc_hdr = register_net_sysctl_sz(net, "net/smc", table,
						  ARRAY_SIZE(smc_table));
	if (!net->smc.smc_hdr)
		goto err_reg;

	net->smc.sysctl_autocorking_size = SMC_AUTOCORKING_DEFAULT_SIZE;
	net->smc.sysctl_smcr_buf_type = SMCR_PHYS_CONT_BUFS;
	net->smc.sysctl_smcr_testlink_time = SMC_LLC_TESTLINK_DEFAULT_TIME;
	WRITE_ONCE(net->smc.sysctl_wmem, net_smc_wmem_init);
	WRITE_ONCE(net->smc.sysctl_rmem, net_smc_rmem_init);
	net->smc.sysctl_max_links_per_lgr = SMC_LINKS_PER_LGR_MAX_PREFER;
	net->smc.sysctl_max_conns_per_lgr = SMC_CONN_PER_LGR_PREFER;

	return 0;

err_reg:
	if (!net_eq(net, &init_net))
		kfree(table);
err_alloc:
	return -ENOMEM;
}

void __net_exit smc_sysctl_net_exit(struct net *net)
{
	struct ctl_table *table;

	table = net->smc.smc_hdr->ctl_table_arg;
	unregister_net_sysctl_table(net->smc.smc_hdr);
	if (!net_eq(net, &init_net))
		kfree(table);
}
