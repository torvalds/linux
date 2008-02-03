/* -*- linux-c -*-
 * sysctl_net_core.c: sysctl interface to net core subsystem.
 *
 * Begun April 1, 1996, Mike Shaver.
 * Added /proc/sys/net/core directory entry (empty =) ). [MS]
 */

#include <linux/mm.h>
#include <linux/sysctl.h>
#include <linux/module.h>
#include <linux/socket.h>
#include <linux/netdevice.h>
#include <linux/init.h>
#include <net/sock.h>
#include <net/xfrm.h>

static struct ctl_table net_core_table[] = {
#ifdef CONFIG_NET
	{
		.ctl_name	= NET_CORE_WMEM_MAX,
		.procname	= "wmem_max",
		.data		= &sysctl_wmem_max,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec
	},
	{
		.ctl_name	= NET_CORE_RMEM_MAX,
		.procname	= "rmem_max",
		.data		= &sysctl_rmem_max,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec
	},
	{
		.ctl_name	= NET_CORE_WMEM_DEFAULT,
		.procname	= "wmem_default",
		.data		= &sysctl_wmem_default,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec
	},
	{
		.ctl_name	= NET_CORE_RMEM_DEFAULT,
		.procname	= "rmem_default",
		.data		= &sysctl_rmem_default,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec
	},
	{
		.ctl_name	= NET_CORE_DEV_WEIGHT,
		.procname	= "dev_weight",
		.data		= &weight_p,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec
	},
	{
		.ctl_name	= NET_CORE_MAX_BACKLOG,
		.procname	= "netdev_max_backlog",
		.data		= &netdev_max_backlog,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec
	},
	{
		.ctl_name	= NET_CORE_MSG_COST,
		.procname	= "message_cost",
		.data		= &net_msg_cost,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec_jiffies,
		.strategy	= &sysctl_jiffies,
	},
	{
		.ctl_name	= NET_CORE_MSG_BURST,
		.procname	= "message_burst",
		.data		= &net_msg_burst,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec,
	},
	{
		.ctl_name	= NET_CORE_OPTMEM_MAX,
		.procname	= "optmem_max",
		.data		= &sysctl_optmem_max,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec
	},
#ifdef CONFIG_XFRM
	{
		.ctl_name	= NET_CORE_AEVENT_ETIME,
		.procname	= "xfrm_aevent_etime",
		.data		= &sysctl_xfrm_aevent_etime,
		.maxlen		= sizeof(u32),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec
	},
	{
		.ctl_name	= NET_CORE_AEVENT_RSEQTH,
		.procname	= "xfrm_aevent_rseqth",
		.data		= &sysctl_xfrm_aevent_rseqth,
		.maxlen		= sizeof(u32),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec
	},
	{
		.ctl_name	= CTL_UNNUMBERED,
		.procname	= "xfrm_larval_drop",
		.data		= &sysctl_xfrm_larval_drop,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec
	},
	{
		.ctl_name	= CTL_UNNUMBERED,
		.procname	= "xfrm_acq_expires",
		.data		= &sysctl_xfrm_acq_expires,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec
	},
#endif /* CONFIG_XFRM */
#endif /* CONFIG_NET */
	{
		.ctl_name	= NET_CORE_SOMAXCONN,
		.procname	= "somaxconn",
		.data		= &init_net.sysctl_somaxconn,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec
	},
	{
		.ctl_name	= NET_CORE_BUDGET,
		.procname	= "netdev_budget",
		.data		= &netdev_budget,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec
	},
	{
		.ctl_name	= NET_CORE_WARNINGS,
		.procname	= "warnings",
		.data		= &net_msg_warn,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec
	},
	{ .ctl_name = 0 }
};

static __net_initdata struct ctl_path net_core_path[] = {
	{ .procname = "net", .ctl_name = CTL_NET, },
	{ .procname = "core", .ctl_name = NET_CORE, },
	{ },
};

static __net_init int sysctl_core_net_init(struct net *net)
{
	struct ctl_table *tbl, *tmp;

	net->sysctl_somaxconn = SOMAXCONN;

	tbl = net_core_table;
	if (net != &init_net) {
		tbl = kmemdup(tbl, sizeof(net_core_table), GFP_KERNEL);
		if (tbl == NULL)
			goto err_dup;

		for (tmp = tbl; tmp->procname; tmp++) {
			if (tmp->data >= (void *)&init_net &&
					tmp->data < (void *)(&init_net + 1))
				tmp->data += (char *)net - (char *)&init_net;
			else
				tmp->mode &= ~0222;
		}
	}

	net->sysctl_core_hdr = register_net_sysctl_table(net,
			net_core_path, tbl);
	if (net->sysctl_core_hdr == NULL)
		goto err_reg;

	return 0;

err_reg:
	if (tbl != net_core_table)
		kfree(tbl);
err_dup:
	return -ENOMEM;
}

static __net_exit void sysctl_core_net_exit(struct net *net)
{
	struct ctl_table *tbl;

	tbl = net->sysctl_core_hdr->ctl_table_arg;
	unregister_net_sysctl_table(net->sysctl_core_hdr);
	BUG_ON(tbl == net_core_table);
	kfree(tbl);
}

static __net_initdata struct pernet_operations sysctl_core_ops = {
	.init = sysctl_core_net_init,
	.exit = sysctl_core_net_exit,
};

static __init int sysctl_core_init(void)
{
	return register_pernet_subsys(&sysctl_core_ops);
}

__initcall(sysctl_core_init);
