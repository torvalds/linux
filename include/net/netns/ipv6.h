/*
 * ipv6 in net namespaces
 */

#include <net/inet_frag.h>

#ifndef __NETNS_IPV6_H__
#define __NETNS_IPV6_H__

struct ctl_table_header;

struct netns_sysctl_ipv6 {
#ifdef CONFIG_SYSCTL
	struct ctl_table_header *table;
	struct ctl_table_header *frags_hdr;
#endif
	int bindv6only;
	int flush_delay;
	int ip6_rt_max_size;
	int ip6_rt_gc_min_interval;
	int ip6_rt_gc_timeout;
	int ip6_rt_gc_interval;
	int ip6_rt_gc_elasticity;
	int ip6_rt_mtu_expires;
	int ip6_rt_min_advmss;
	int icmpv6_time;
};

struct netns_ipv6 {
	struct netns_sysctl_ipv6 sysctl;
	struct ipv6_devconf	*devconf_all;
	struct ipv6_devconf	*devconf_dflt;
	struct netns_frags	frags;
#ifdef CONFIG_NETFILTER
	struct xt_table		*ip6table_filter;
	struct xt_table		*ip6table_mangle;
	struct xt_table		*ip6table_raw;
#endif
};
#endif
