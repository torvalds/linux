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
#include <linux/bpf.h>
#include <net/net_namespace.h>

#include "smc.h"
#include "smc_core.h"
#include "smc_llc.h"
#include "smc_sysctl.h"
#include "smc_hs_bpf.h"

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
static unsigned int smcr_max_wr_min = 2;
static unsigned int smcr_max_wr_max = 2048;

#if IS_ENABLED(CONFIG_SMC_HS_CTRL_BPF)
static int smc_net_replace_smc_hs_ctrl(struct net *net, const char *name)
{
	struct smc_hs_ctrl *ctrl = NULL;

	rcu_read_lock();
	/* null or empty name ask to clear current ctrl */
	if (name && name[0]) {
		ctrl = smc_hs_ctrl_find_by_name(name);
		if (!ctrl) {
			rcu_read_unlock();
			return -EINVAL;
		}
		/* no change, just return */
		if (ctrl == rcu_dereference(net->smc.hs_ctrl)) {
			rcu_read_unlock();
			return 0;
		}
		if (!bpf_try_module_get(ctrl, ctrl->owner)) {
			rcu_read_unlock();
			return -EBUSY;
		}
	}
	/* xhcg old ctrl with the new one atomically */
	ctrl = unrcu_pointer(xchg(&net->smc.hs_ctrl, RCU_INITIALIZER(ctrl)));
	/* release old ctrl */
	if (ctrl)
		bpf_module_put(ctrl, ctrl->owner);

	rcu_read_unlock();
	return 0;
}

static int proc_smc_hs_ctrl(const struct ctl_table *ctl, int write,
			    void *buffer, size_t *lenp, loff_t *ppos)
{
	struct net *net = container_of(ctl->data, struct net, smc.hs_ctrl);
	char val[SMC_HS_CTRL_NAME_MAX];
	const struct ctl_table tbl = {
		.data = val,
		.maxlen = SMC_HS_CTRL_NAME_MAX,
	};
	struct smc_hs_ctrl *ctrl;
	int ret;

	rcu_read_lock();
	ctrl = rcu_dereference(net->smc.hs_ctrl);
	if (ctrl)
		memcpy(val, ctrl->name, sizeof(ctrl->name));
	else
		val[0] = '\0';
	rcu_read_unlock();

	ret = proc_dostring(&tbl, write, buffer, lenp, ppos);
	if (ret)
		return ret;

	if (write)
		ret = smc_net_replace_smc_hs_ctrl(net, val);
	return ret;
}
#endif /* CONFIG_SMC_HS_CTRL_BPF */

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
	{
		.procname	= "limit_smc_hs",
		.data		= &init_net.smc.limit_smc_hs,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_minmax,
		.extra1		= SYSCTL_ZERO,
		.extra2		= SYSCTL_ONE,
	},
	{
		.procname	= "smcr_max_send_wr",
		.data		= &init_net.smc.sysctl_smcr_max_send_wr,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_minmax,
		.extra1		= &smcr_max_wr_min,
		.extra2		= &smcr_max_wr_max,
	},
	{
		.procname	= "smcr_max_recv_wr",
		.data		= &init_net.smc.sysctl_smcr_max_recv_wr,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_minmax,
		.extra1		= &smcr_max_wr_min,
		.extra2		= &smcr_max_wr_max,
	},
#if IS_ENABLED(CONFIG_SMC_HS_CTRL_BPF)
	{
		.procname	= "hs_ctrl",
		.data		= &init_net.smc.hs_ctrl,
		.mode		= 0644,
		.maxlen		= SMC_HS_CTRL_NAME_MAX,
		.proc_handler	= proc_smc_hs_ctrl,
	},
#endif /* CONFIG_SMC_HS_CTRL_BPF */
};

int __net_init smc_sysctl_net_init(struct net *net)
{
	size_t table_size = ARRAY_SIZE(smc_table);
	struct ctl_table *table;

	table = smc_table;
	if (!net_eq(net, &init_net)) {
		int i;
#if IS_ENABLED(CONFIG_SMC_HS_CTRL_BPF)
		struct smc_hs_ctrl *ctrl;

		rcu_read_lock();
		ctrl = rcu_dereference(init_net.smc.hs_ctrl);
		if (ctrl && ctrl->flags & SMC_HS_CTRL_FLAG_INHERITABLE &&
		    bpf_try_module_get(ctrl, ctrl->owner))
			rcu_assign_pointer(net->smc.hs_ctrl, ctrl);
		rcu_read_unlock();
#endif /* CONFIG_SMC_HS_CTRL_BPF */

		table = kmemdup(table, sizeof(smc_table), GFP_KERNEL);
		if (!table)
			goto err_alloc;

		for (i = 0; i < table_size; i++)
			table[i].data += (void *)net - (void *)&init_net;
	}

	net->smc.smc_hdr = register_net_sysctl_sz(net, "net/smc", table,
						  table_size);
	if (!net->smc.smc_hdr)
		goto err_reg;

	net->smc.sysctl_autocorking_size = SMC_AUTOCORKING_DEFAULT_SIZE;
	net->smc.sysctl_smcr_buf_type = SMCR_PHYS_CONT_BUFS;
	net->smc.sysctl_smcr_testlink_time = SMC_LLC_TESTLINK_DEFAULT_TIME;
	WRITE_ONCE(net->smc.sysctl_wmem, net_smc_wmem_init);
	WRITE_ONCE(net->smc.sysctl_rmem, net_smc_rmem_init);
	net->smc.sysctl_max_links_per_lgr = SMC_LINKS_PER_LGR_MAX_PREFER;
	net->smc.sysctl_max_conns_per_lgr = SMC_CONN_PER_LGR_PREFER;
	net->smc.sysctl_smcr_max_send_wr = SMCR_MAX_SEND_WR_DEF;
	net->smc.sysctl_smcr_max_recv_wr = SMCR_MAX_RECV_WR_DEF;
	/* disable handshake limitation by default */
	net->smc.limit_smc_hs = 0;

	return 0;

err_reg:
	if (!net_eq(net, &init_net))
		kfree(table);
err_alloc:
#if IS_ENABLED(CONFIG_SMC_HS_CTRL_BPF)
	smc_net_replace_smc_hs_ctrl(net, NULL);
#endif /* CONFIG_SMC_HS_CTRL_BPF */
	return -ENOMEM;
}

void __net_exit smc_sysctl_net_exit(struct net *net)
{
	const struct ctl_table *table;

	table = net->smc.smc_hdr->ctl_table_arg;
	unregister_net_sysctl_table(net->smc.smc_hdr);
#if IS_ENABLED(CONFIG_SMC_HS_CTRL_BPF)
	smc_net_replace_smc_hs_ctrl(net, NULL);
#endif /* CONFIG_SMC_HS_CTRL_BPF */

	if (!net_eq(net, &init_net))
		kfree(table);
}
