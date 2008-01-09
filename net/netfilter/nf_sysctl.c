/* nf_sysctl.c	netfilter sysctl registration/unregistation
 *
 * Copyright (c) 2006 Patrick McHardy <kaber@trash.net>
 */
#include <linux/module.h>
#include <linux/sysctl.h>
#include <linux/string.h>
#include <linux/slab.h>

/* net/netfilter */
struct ctl_path nf_net_netfilter_sysctl_path[] = {
	{ .procname = "net", .ctl_name = CTL_NET, },
	{ .procname = "netfilter", .ctl_name = NET_NETFILTER, },
	{ }
};
EXPORT_SYMBOL_GPL(nf_net_netfilter_sysctl_path);

/* net/ipv4/netfilter */
struct ctl_path nf_net_ipv4_netfilter_sysctl_path[] = {
	{ .procname = "net", .ctl_name = CTL_NET, },
	{ .procname = "ipv4", .ctl_name = NET_IPV4, },
	{ .procname = "netfilter", .ctl_name = NET_IPV4_NETFILTER, },
	{ }
};
EXPORT_SYMBOL_GPL(nf_net_ipv4_netfilter_sysctl_path);
