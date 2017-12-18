/* SPDX-License-Identifier: GPL-2.0 */
/*
 * ipv6 in net namespaces
 */

#include <net/inet_frag.h>

#ifndef __NETNS_IPV6_H__
#define __NETNS_IPV6_H__
#include <net/dst_ops.h>

struct ctl_table_header;

struct netns_sysctl_ipv6 {
#ifdef CONFIG_SYSCTL
	struct ctl_table_header *hdr;
	struct ctl_table_header *route_hdr;
	struct ctl_table_header *icmp_hdr;
	struct ctl_table_header *frags_hdr;
	struct ctl_table_header *xfrm6_hdr;
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
	int flowlabel_consistency;
	int auto_flowlabels;
	int icmpv6_time;
	int anycast_src_echo_reply;
	int ip_nonlocal_bind;
	int fwmark_reflect;
	int idgen_retries;
	int idgen_delay;
	int flowlabel_state_ranges;
	int flowlabel_reflect;
	int max_dst_opts_cnt;
	int max_hbh_opts_cnt;
	int max_dst_opts_len;
	int max_hbh_opts_len;
};

struct netns_ipv6 {
	struct netns_sysctl_ipv6 sysctl;
	struct ipv6_devconf	*devconf_all;
	struct ipv6_devconf	*devconf_dflt;
	struct inet_peer_base	*peers;
	struct netns_frags	frags;
#ifdef CONFIG_NETFILTER
	struct xt_table		*ip6table_filter;
	struct xt_table		*ip6table_mangle;
	struct xt_table		*ip6table_raw;
#ifdef CONFIG_SECURITY
	struct xt_table		*ip6table_security;
#endif
	struct xt_table		*ip6table_nat;
#endif
	struct rt6_info         *ip6_null_entry;
	struct rt6_statistics   *rt6_stats;
	struct timer_list       ip6_fib_timer;
	struct hlist_head       *fib_table_hash;
	struct fib6_table       *fib6_main_tbl;
	struct list_head	fib6_walkers;
	struct dst_ops		ip6_dst_ops;
	rwlock_t		fib6_walker_lock;
	spinlock_t		fib6_gc_lock;
	unsigned int		 ip6_rt_gc_expire;
	unsigned long		 ip6_rt_last_gc;
#ifdef CONFIG_IPV6_MULTIPLE_TABLES
	bool			 fib6_has_custom_rules;
	struct rt6_info         *ip6_prohibit_entry;
	struct rt6_info         *ip6_blk_hole_entry;
	struct fib6_table       *fib6_local_tbl;
	struct fib_rules_ops    *fib6_rules_ops;
#endif
	struct sock		**icmp_sk;
	struct sock             *ndisc_sk;
	struct sock             *tcp_sk;
	struct sock             *igmp_sk;
	struct sock		*mc_autojoin_sk;
#ifdef CONFIG_IPV6_MROUTE
#ifndef CONFIG_IPV6_MROUTE_MULTIPLE_TABLES
	struct mr6_table	*mrt6;
#else
	struct list_head	mr6_tables;
	struct fib_rules_ops	*mr6_rules_ops;
#endif
#endif
	atomic_t		dev_addr_genid;
	atomic_t		fib6_sernum;
	struct seg6_pernet_data *seg6_data;
	struct fib_notifier_ops	*notifier_ops;
	struct {
		struct hlist_head head;
		spinlock_t	lock;
		u32		seq;
	} ip6addrlbl_table;
};

#if IS_ENABLED(CONFIG_NF_DEFRAG_IPV6)
struct netns_nf_frag {
	struct netns_sysctl_ipv6 sysctl;
	struct netns_frags	frags;
};
#endif

#endif
