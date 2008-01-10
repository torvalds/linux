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

static ctl_table ipv6_table[] = {
	{
		.ctl_name	= NET_IPV6_ROUTE,
		.procname	= "route",
		.maxlen		= 0,
		.mode		= 0555,
		.child		= ipv6_route_table
	},
	{
		.ctl_name	= NET_IPV6_ICMP,
		.procname	= "icmp",
		.maxlen		= 0,
		.mode		= 0555,
		.child		= ipv6_icmp_table
	},
	{
		.ctl_name	= NET_IPV6_BINDV6ONLY,
		.procname	= "bindv6only",
		.data		= &sysctl_ipv6_bindv6only,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec
	},
	{
		.ctl_name	= NET_IPV6_IP6FRAG_HIGH_THRESH,
		.procname	= "ip6frag_high_thresh",
		.data		= &ip6_frags_ctl.high_thresh,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec
	},
	{
		.ctl_name	= NET_IPV6_IP6FRAG_LOW_THRESH,
		.procname	= "ip6frag_low_thresh",
		.data		= &ip6_frags_ctl.low_thresh,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec
	},
	{
		.ctl_name	= NET_IPV6_IP6FRAG_TIME,
		.procname	= "ip6frag_time",
		.data		= &ip6_frags_ctl.timeout,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec_jiffies,
		.strategy	= &sysctl_jiffies,
	},
	{
		.ctl_name	= NET_IPV6_IP6FRAG_SECRET_INTERVAL,
		.procname	= "ip6frag_secret_interval",
		.data		= &ip6_frags_ctl.secret_interval,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec_jiffies,
		.strategy	= &sysctl_jiffies
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

static struct ctl_table_header *ipv6_sysctl_header;

static int ipv6_sysctl_net_init(struct net *net)
{
	ipv6_sysctl_header = register_net_sysctl_table(net, net_ipv6_ctl_path,
						       ipv6_table);
	if (!ipv6_sysctl_header)
		return -ENOMEM;

	return 0;

}

static void ipv6_sysctl_net_exit(struct net *net)
{
	unregister_net_sysctl_table(ipv6_sysctl_header);
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
