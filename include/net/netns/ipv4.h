/*
 * ipv4 in net namespaces
 */

#ifndef __NETNS_IPV4_H__
#define __NETNS_IPV4_H__

#include <linux/uidgid.h>
#include <net/inet_frag.h>

struct tcpm_hash_bucket;
struct ctl_table_header;
struct ipv4_devconf;
struct fib_rules_ops;
struct hlist_head;
struct fib_table;
struct sock;
struct local_ports {
	seqlock_t	lock;
	int		range[2];
};

struct ping_group_range {
	seqlock_t	lock;
	kgid_t		range[2];
};

struct netns_ipv4 {
#ifdef CONFIG_SYSCTL
	struct ctl_table_header	*forw_hdr;
	struct ctl_table_header	*frags_hdr;
	struct ctl_table_header	*ipv4_hdr;
	struct ctl_table_header *route_hdr;
	struct ctl_table_header *xfrm4_hdr;
#endif
	struct ipv4_devconf	*devconf_all;
	struct ipv4_devconf	*devconf_dflt;
#ifdef CONFIG_IP_MULTIPLE_TABLES
	struct fib_rules_ops	*rules_ops;
	bool			fib_has_custom_rules;
	struct fib_table	*fib_local;
	struct fib_table	*fib_main;
	struct fib_table	*fib_default;
#endif
#ifdef CONFIG_IP_ROUTE_CLASSID
	int			fib_num_tclassid_users;
#endif
	struct hlist_head	*fib_table_hash;
	struct sock		*fibnl;

	struct sock		**icmp_sk;
	struct inet_peer_base	*peers;
	struct tcpm_hash_bucket	*tcp_metrics_hash;
	unsigned int		tcp_metrics_hash_log;
	struct sock  * __percpu	*tcp_sk;
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
#endif

	int sysctl_icmp_echo_ignore_all;
	int sysctl_icmp_echo_ignore_broadcasts;
	int sysctl_icmp_ignore_bogus_error_responses;
	int sysctl_icmp_ratelimit;
	int sysctl_icmp_ratemask;
	int sysctl_icmp_errors_use_inbound_ifaddr;

	struct local_ports ip_local_ports;

	int sysctl_tcp_ecn;
	int sysctl_ip_no_pmtu_disc;
	int sysctl_ip_fwd_use_pmtu;
	int sysctl_ip_nonlocal_bind;

	int sysctl_fwmark_reflect;
	int sysctl_tcp_fwmark_accept;

	struct ping_group_range ping_group_range;

	atomic_t dev_addr_genid;

#ifdef CONFIG_SYSCTL
	unsigned long *sysctl_local_reserved_ports;
#endif

#ifdef CONFIG_IP_MROUTE
#ifndef CONFIG_IP_MROUTE_MULTIPLE_TABLES
	struct mr_table		*mrt;
#else
	struct list_head	mr_tables;
	struct fib_rules_ops	*mr_rules_ops;
#endif
#endif
	atomic_t	rt_genid;
};
#endif
