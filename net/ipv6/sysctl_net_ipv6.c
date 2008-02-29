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
#include <net/ndisc.h>
#include <net/ipv6.h>
#include <net/addrconf.h>
#include <net/inet_frag.h>

static ctl_table ipv6_table_template[] = {
	{
		.ctl_name	= NET_IPV6_ROUTE,
		.procname	= "route",
		.maxlen		= 0,
		.mode		= 0555,
		.child		= ipv6_route_table_template
	},
	{
		.ctl_name	= NET_IPV6_ICMP,
		.procname	= "icmp",
		.maxlen		= 0,
		.mode		= 0555,
		.child		= ipv6_icmp_table_template
	},
	{
		.ctl_name	= NET_IPV6_BINDV6ONLY,
		.procname	= "bindv6only",
		.data		= &init_net.ipv6.sysctl.bindv6only,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec
	},
	{
		.ctl_name	= NET_IPV6_MLD_MAX_MSF,
		.procname	= "mld_max_msf",
		.data		= &sysctl_mld_max_msf,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec
	},
	{ .ctl_name = 0 }
};

struct ctl_path net_ipv6_ctl_path[] = {
	{ .procname = "net", .ctl_name = CTL_NET, },
	{ .procname = "ipv6", .ctl_name = NET_IPV6, },
	{ },
};
EXPORT_SYMBOL_GPL(net_ipv6_ctl_path);

static int ipv6_sysctl_net_init(struct net *net)
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

	ipv6_route_table = ipv6_route_sysctl_init(net);
	if (!ipv6_route_table)
		goto out_ipv6_table;

	ipv6_icmp_table = ipv6_icmp_sysctl_init(net);
	if (!ipv6_icmp_table)
		goto out_ipv6_route_table;

	ipv6_route_table[0].data = &net->ipv6.sysctl.flush_delay;
	/* ipv6_route_table[1].data will be handled when we have
	   routes per namespace */
	ipv6_route_table[2].data = &net->ipv6.sysctl.ip6_rt_max_size;
	ipv6_route_table[3].data = &net->ipv6.sysctl.ip6_rt_gc_min_interval;
	ipv6_route_table[4].data = &net->ipv6.sysctl.ip6_rt_gc_timeout;
	ipv6_route_table[5].data = &net->ipv6.sysctl.ip6_rt_gc_interval;
	ipv6_route_table[6].data = &net->ipv6.sysctl.ip6_rt_gc_elasticity;
	ipv6_route_table[7].data = &net->ipv6.sysctl.ip6_rt_mtu_expires;
	ipv6_route_table[8].data = &net->ipv6.sysctl.ip6_rt_min_advmss;
	ipv6_table[0].child = ipv6_route_table;

	ipv6_icmp_table[0].data = &net->ipv6.sysctl.icmpv6_time;
	ipv6_table[1].child = ipv6_icmp_table;

	ipv6_table[2].data = &net->ipv6.sysctl.bindv6only;

	/* We don't want this value to be per namespace, it should be global
	   to all namespaces, so make it read-only when we are not in the
	   init network namespace */
	if (net != &init_net)
		ipv6_table[3].mode = 0444;

	net->ipv6.sysctl.table = register_net_sysctl_table(net, net_ipv6_ctl_path,
							   ipv6_table);
	if (!net->ipv6.sysctl.table)
		goto out_ipv6_icmp_table;

	err = 0;
out:
	return err;

out_ipv6_icmp_table:
	kfree(ipv6_icmp_table);
out_ipv6_route_table:
	kfree(ipv6_route_table);
out_ipv6_table:
	kfree(ipv6_table);
	goto out;
}

static void ipv6_sysctl_net_exit(struct net *net)
{
	struct ctl_table *ipv6_table;
	struct ctl_table *ipv6_route_table;
	struct ctl_table *ipv6_icmp_table;

	ipv6_table = net->ipv6.sysctl.table->ctl_table_arg;
	ipv6_route_table = ipv6_table[0].child;
	ipv6_icmp_table = ipv6_table[1].child;

	unregister_net_sysctl_table(net->ipv6.sysctl.table);

	kfree(ipv6_table);
	kfree(ipv6_route_table);
	kfree(ipv6_icmp_table);
}

static struct pernet_operations ipv6_sysctl_net_ops = {
	.init = ipv6_sysctl_net_init,
	.exit = ipv6_sysctl_net_exit,
};

int ipv6_sysctl_register(void)
{
	return register_pernet_subsys(&ipv6_sysctl_net_ops);
}

void ipv6_sysctl_unregister(void)
{
	unregister_pernet_subsys(&ipv6_sysctl_net_ops);
}
