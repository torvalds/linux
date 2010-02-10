/*
 * ipv4 in net namespaces
 */

#ifndef __NETNS_IPV4_H__
#define __NETNS_IPV4_H__

#include <net/inet_frag.h>

struct ctl_table_header;
struct ipv4_devconf;
struct fib_rules_ops;
struct hlist_head;
struct sock;

struct netns_ipv4 {
#ifdef CONFIG_SYSCTL
	struct ctl_table_header	*forw_hdr;
	struct ctl_table_header	*frags_hdr;
	struct ctl_table_header	*ipv4_hdr;
	struct ctl_table_header *route_hdr;
#endif
	struct ipv4_devconf	*devconf_all;
	struct ipv4_devconf	*devconf_dflt;
#ifdef CONFIG_IP_MULTIPLE_TABLES
	struct fib_rules_ops	*rules_ops;
#endif
	struct hlist_head	*fib_table_hash;
	struct sock		*fibnl;

	struct sock		**icmp_sk;
	struct sock		*tcp_sock;

	struct netns_frags	frags;
#ifdef CONFIG_NETFILTER
	struct xt_table		*iptable_filter;
	struct xt_table		*iptable_mangle;
	struct xt_table		*iptable_raw;
	struct xt_table		*arptable_filter;
#ifdef CONFIG_SECURITY
	struct xt_table		*iptable_security;
#endif
	struct xt_table		*nat_table;
	struct hlist_head	*nat_bysource;
	unsigned int		nat_htable_size;
	int			nat_vmalloced;
#endif

	int sysctl_icmp_echo_ignore_all;
	int sysctl_icmp_echo_ignore_broadcasts;
	int sysctl_icmp_ignore_bogus_error_responses;
	int sysctl_icmp_ratelimit;
	int sysctl_icmp_ratemask;
	int sysctl_icmp_errors_use_inbound_ifaddr;
	int sysctl_rt_cache_rebuild_count;
	int current_rt_cache_rebuild_count;

	struct timer_list rt_secret_timer;
	atomic_t rt_genid;

#ifdef CONFIG_IP_MROUTE
	struct sock		*mroute_sk;
	struct mfc_cache	**mfc_cache_array;
	struct vif_device	*vif_table;
	int			maxvif;
	atomic_t		cache_resolve_queue_len;
	int			mroute_do_assert;
	int			mroute_do_pim;
#if defined(CONFIG_IP_PIMSM_V1) || defined(CONFIG_IP_PIMSM_V2)
	int			mroute_reg_vif_num;
#endif
#endif
};
#endif
