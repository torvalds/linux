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

#ifdef CONFIG_SYSCTL

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

static struct ctl_table_header *ipv6_sysctl_header;

static ctl_table ipv6_net_table[] = {
	{
		.ctl_name	= NET_IPV6,
		.procname	= "ipv6",
		.mode		= 0555,
		.child		= ipv6_table
	},
	{ .ctl_name = 0 }
};

static ctl_table ipv6_root_table[] = {
	{
		.ctl_name	= CTL_NET,
		.procname	= "net",
		.mode		= 0555,
		.child		= ipv6_net_table
	},
	{ .ctl_name = 0 }
};

void ipv6_sysctl_register(void)
{
	ipv6_sysctl_header = register_sysctl_table(ipv6_root_table);
}

void ipv6_sysctl_unregister(void)
{
	unregister_sysctl_table(ipv6_sysctl_header);
}

#endif /* CONFIG_SYSCTL */



