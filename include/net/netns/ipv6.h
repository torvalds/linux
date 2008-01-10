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
#endif
	struct inet_frags_ctl frags;
	int bindv6only;
};

struct netns_ipv6 {
	struct netns_sysctl_ipv6 sysctl;
};
#endif
