// SPDX-License-Identifier: GPL-2.0
/*
 * sysctl_net_ipv6.c: sysctl interface to net IPV6 subsystem.
 *
 * Changes:
 * YOSHIFUJI Hideaki @USAGI:	added icmp sysctl table.
 */

#include <linux/mm.h>
#include <linux/sysctl.h>
#include <linux/in6.h>
#include <linux/ipv6.h>
#include <linux/slab.h>
#include <linux/export.h>
#include <net/ndisc.h>
#include <net/ipv6.h>
#include <net/addrconf.h>
#include <net/inet_frag.h>
#include <net/netevent.h>
#ifdef CONFIG_NETLABEL
#include <net/calipso.h>
#endif

static int zero;
static int one = 1;
static int auto_flowlabels_min;
static int auto_flowlabels_max = IP6_AUTO_FLOW_LABEL_MAX;

static int proc_rt6_multipath_hash_policy(struct ctl_table *table, int write,
					  void __user *buffer, size_t *lenp,
					  loff_t *ppos)
{
	struct net *net;
	int ret;

	net = container_of(table->data, struct net,
			   ipv6.sysctl.multipath_hash_policy);
	ret = proc_dointvec_minmax(table, write, buffer, lenp, ppos);
	if (write && ret == 0)
		call_netevent_notifiers(NETEVENT_IPV6_MPATH_HASH_UPDATE, net);

	return ret;
}

static struct ctl_table ipv6_table_template[] = {
	{
		.procname	= "bindv6only",
		.data		= &init_net.ipv6.sysctl.bindv6only,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec
	},
	{
		.procname	= "anycast_src_echo_reply",
		.data		= &init_net.ipv6.sysctl.anycast_src_echo_reply,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec
	},
	{
		.procname	= "flowlabel_consistency",
		.data		= &init_net.ipv6.sysctl.flowlabel_consistency,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec
	},
	{
		.procname	= "auto_flowlabels",
		.data		= &init_net.ipv6.sysctl.auto_flowlabels,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_minmax,
		.extra1		= &auto_flowlabels_min,
		.extra2		= &auto_flowlabels_max
	},
	{
		.procname	= "fwmark_reflect",
		.data		= &init_net.ipv6.sysctl.fwmark_reflect,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec
	},
	{
		.procname	= "idgen_retries",
		.data		= &init_net.ipv6.sysctl.idgen_retries,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec,
	},
	{
		.procname	= "idgen_delay",
		.data		= &init_net.ipv6.sysctl.idgen_delay,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_jiffies,
	},
	{
		.procname	= "flowlabel_state_ranges",
		.data		= &init_net.ipv6.sysctl.flowlabel_state_ranges,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec
	},
	{
		.procname	= "ip_nonlocal_bind",
		.data		= &init_net.ipv6.sysctl.ip_nonlocal_bind,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec
	},
	{
		.procname	= "flowlabel_reflect",
		.data		= &init_net.ipv6.sysctl.flowlabel_reflect,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec,
	},
	{
		.procname	= "max_dst_opts_number",
		.data		= &init_net.ipv6.sysctl.max_dst_opts_cnt,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec
	},
	{
		.procname	= "max_hbh_opts_number",
		.data		= &init_net.ipv6.sysctl.max_hbh_opts_cnt,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec
	},
	{
		.procname	= "max_dst_opts_length",
		.data		= &init_net.ipv6.sysctl.max_dst_opts_len,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec
	},
	{
		.procname	= "max_hbh_length",
		.data		= &init_net.ipv6.sysctl.max_hbh_opts_len,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec
	},
	{
		.procname	= "fib_multipath_hash_policy",
		.data		= &init_net.ipv6.sysctl.multipath_hash_policy,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler   = proc_rt6_multipath_hash_policy,
		.extra1		= &zero,
		.extra2		= &one,
	},
	{ }
};

static struct ctl_table ipv6_rotable[] = {
	{
		.procname	= "mld_max_msf",
		.data		= &sysctl_mld_max_msf,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec
	},
	{
		.procname	= "mld_qrv",
		.data		= &sysctl_mld_qrv,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_minmax,
		.extra1		= &one
	},
#ifdef CONFIG_NETLABEL
	{
		.procname	= "calipso_cache_enable",
		.data		= &calipso_cache_enabled,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec,
	},
	{
		.procname	= "calipso_cache_bucket_size",
		.data		= &calipso_cache_bucketsize,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec,
	},
#endif /* CONFIG_NETLABEL */
	{ }
};

static int __net_init ipv6_sysctl_net_init(struct net *net)
{
	struct ctl_table *ipv6_table;
	struct ctl_table *ipv6_route_table;
	struct ctl_table *ipv6_icmp_table;
	int err;

	err = -ENOMEM;
	ipv6_table = kmemdup(ipv6_table_template, sizeof(ipv6_table_template),
			     GFP_KERNEL);
	if (!ipv6_table)
		goto out;
	ipv6_table[0].data = &net->ipv6.sysctl.bindv6only;
	ipv6_table[1].data = &net->ipv6.sysctl.anycast_src_echo_reply;
	ipv6_table[2].data = &net->ipv6.sysctl.flowlabel_consistency;
	ipv6_table[3].data = &net->ipv6.sysctl.auto_flowlabels;
	ipv6_table[4].data = &net->ipv6.sysctl.fwmark_reflect;
	ipv6_table[5].data = &net->ipv6.sysctl.idgen_retries;
	ipv6_table[6].data = &net->ipv6.sysctl.idgen_delay;
	ipv6_table[7].data = &net->ipv6.sysctl.flowlabel_state_ranges;
	ipv6_table[8].data = &net->ipv6.sysctl.ip_nonlocal_bind;
	ipv6_table[9].data = &net->ipv6.sysctl.flowlabel_reflect;
	ipv6_table[10].data = &net->ipv6.sysctl.max_dst_opts_cnt;
	ipv6_table[11].data = &net->ipv6.sysctl.max_hbh_opts_cnt;
	ipv6_table[12].data = &net->ipv6.sysctl.max_dst_opts_len;
	ipv6_table[13].data = &net->ipv6.sysctl.max_hbh_opts_len;
	ipv6_table[14].data = &net->ipv6.sysctl.multipath_hash_policy,

	ipv6_route_table = ipv6_route_sysctl_init(net);
	if (!ipv6_route_table)
		goto out_ipv6_table;

	ipv6_icmp_table = ipv6_icmp_sysctl_init(net);
	if (!ipv6_icmp_table)
		goto out_ipv6_route_table;

	net->ipv6.sysctl.hdr = register_net_sysctl(net, "net/ipv6", ipv6_table);
	if (!net->ipv6.sysctl.hdr)
		goto out_ipv6_icmp_table;

	net->ipv6.sysctl.route_hdr =
		register_net_sysctl(net, "net/ipv6/route", ipv6_route_table);
	if (!net->ipv6.sysctl.route_hdr)
		goto out_unregister_ipv6_table;

	net->ipv6.sysctl.icmp_hdr =
		register_net_sysctl(net, "net/ipv6/icmp", ipv6_icmp_table);
	if (!net->ipv6.sysctl.icmp_hdr)
		goto out_unregister_route_table;

	err = 0;
out:
	return err;
out_unregister_route_table:
	unregister_net_sysctl_table(net->ipv6.sysctl.route_hdr);
out_unregister_ipv6_table:
	unregister_net_sysctl_table(net->ipv6.sysctl.hdr);
out_ipv6_icmp_table:
	kfree(ipv6_icmp_table);
out_ipv6_route_table:
	kfree(ipv6_route_table);
out_ipv6_table:
	kfree(ipv6_table);
	goto out;
}

static void __net_exit ipv6_sysctl_net_exit(struct net *net)
{
	struct ctl_table *ipv6_table;
	struct ctl_table *ipv6_route_table;
	struct ctl_table *ipv6_icmp_table;

	ipv6_table = net->ipv6.sysctl.hdr->ctl_table_arg;
	ipv6_route_table = net->ipv6.sysctl.route_hdr->ctl_table_arg;
	ipv6_icmp_table = net->ipv6.sysctl.icmp_hdr->ctl_table_arg;

	unregister_net_sysctl_table(net->ipv6.sysctl.icmp_hdr);
	unregister_net_sysctl_table(net->ipv6.sysctl.route_hdr);
	unregister_net_sysctl_table(net->ipv6.sysctl.hdr);

	kfree(ipv6_table);
	kfree(ipv6_route_table);
	kfree(ipv6_icmp_table);
}

static struct pernet_operations ipv6_sysctl_net_ops = {
	.init = ipv6_sysctl_net_init,
	.exit = ipv6_sysctl_net_exit,
};

static struct ctl_table_header *ip6_header;

int ipv6_sysctl_register(void)
{
	int err = -ENOMEM;

	ip6_header = register_net_sysctl(&init_net, "net/ipv6", ipv6_rotable);
	if (!ip6_header)
		goto out;

	err = register_pernet_subsys(&ipv6_sysctl_net_ops);
	if (err)
		goto err_pernet;
out:
	return err;

err_pernet:
	unregister_net_sysctl_table(ip6_header);
	goto out;
}

void ipv6_sysctl_unregister(void)
{
	unregister_net_sysctl_table(ip6_header);
	unregister_pernet_subsys(&ipv6_sysctl_net_ops);
}
