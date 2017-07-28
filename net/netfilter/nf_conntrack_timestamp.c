/*
 * (C) 2010 Pablo Neira Ayuso <pablo@netfilter.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation (or any later at your option).
 */

#include <linux/netfilter.h>
#include <linux/slab.h>
#include <linux/kernel.h>
#include <linux/moduleparam.h>

#include <net/netfilter/nf_conntrack.h>
#include <net/netfilter/nf_conntrack_extend.h>
#include <net/netfilter/nf_conntrack_timestamp.h>

static bool nf_ct_tstamp __read_mostly;

module_param_named(tstamp, nf_ct_tstamp, bool, 0644);
MODULE_PARM_DESC(tstamp, "Enable connection tracking flow timestamping.");

#ifdef CONFIG_SYSCTL
static struct ctl_table tstamp_sysctl_table[] = {
	{
		.procname	= "nf_conntrack_timestamp",
		.data		= &init_net.ct.sysctl_tstamp,
		.maxlen		= sizeof(unsigned int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec,
	},
	{}
};
#endif /* CONFIG_SYSCTL */

static const struct nf_ct_ext_type tstamp_extend = {
	.len	= sizeof(struct nf_conn_tstamp),
	.align	= __alignof__(struct nf_conn_tstamp),
	.id	= NF_CT_EXT_TSTAMP,
};

#ifdef CONFIG_SYSCTL
static int nf_conntrack_tstamp_init_sysctl(struct net *net)
{
	struct ctl_table *table;

	table = kmemdup(tstamp_sysctl_table, sizeof(tstamp_sysctl_table),
			GFP_KERNEL);
	if (!table)
		goto out;

	table[0].data = &net->ct.sysctl_tstamp;

	/* Don't export sysctls to unprivileged users */
	if (net->user_ns != &init_user_ns)
		table[0].procname = NULL;

	net->ct.tstamp_sysctl_header = register_net_sysctl(net,	"net/netfilter",
							   table);
	if (!net->ct.tstamp_sysctl_header) {
		printk(KERN_ERR "nf_ct_tstamp: can't register to sysctl.\n");
		goto out_register;
	}
	return 0;

out_register:
	kfree(table);
out:
	return -ENOMEM;
}

static void nf_conntrack_tstamp_fini_sysctl(struct net *net)
{
	struct ctl_table *table;

	table = net->ct.tstamp_sysctl_header->ctl_table_arg;
	unregister_net_sysctl_table(net->ct.tstamp_sysctl_header);
	kfree(table);
}
#else
static int nf_conntrack_tstamp_init_sysctl(struct net *net)
{
	return 0;
}

static void nf_conntrack_tstamp_fini_sysctl(struct net *net)
{
}
#endif

int nf_conntrack_tstamp_pernet_init(struct net *net)
{
	net->ct.sysctl_tstamp = nf_ct_tstamp;
	return nf_conntrack_tstamp_init_sysctl(net);
}

void nf_conntrack_tstamp_pernet_fini(struct net *net)
{
	nf_conntrack_tstamp_fini_sysctl(net);
}

int nf_conntrack_tstamp_init(void)
{
	int ret;
	ret = nf_ct_extend_register(&tstamp_extend);
	if (ret < 0)
		pr_err("nf_ct_tstamp: Unable to register extension\n");
	return ret;
}

void nf_conntrack_tstamp_fini(void)
{
	nf_ct_extend_unregister(&tstamp_extend);
}
