/* SPDX-License-Identifier: GPL-2.0 */
/*
 * ipv4 in net namespaces
 */

#ifndef __NETNS_IPV4_H__
#define __NETNS_IPV4_H__

#include <linux/uidgid.h>
#include <net/inet_frag.h>
#include <linux/rcupdate.h>

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
	bool		warned;
};

struct ping_group_range {
	seqlock_t	lock;
	kgid_t		range[2];
};

struct inet_hashinfo;

struct inet_timewait_death_row {
	atomic_t		tw_count;

	struct inet_hashinfo 	*hashinfo ____cacheline_aligned_in_smp;
	int			sysctl_max_tw_buckets;
};

struct tcp_fastopen_context;

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
	struct ip_ra_chain __rcu *ra_chain;
	struct mutex		ra_mutex;
#ifdef CONFIG_IP_MULTIPLE_TABLES
	struct fib_rules_ops	*rules_ops;
	bool			fib_has_custom_rules;
	unsigned int		fib_rules_require_fldissect;
	struct fib_table __rcu	*fib_main;
	struct fib_table __rcu	*fib_default;
#endif
	bool			fib_has_custom_local_routes;
#ifdef CONFIG_IP_ROUTE_CLASSID
	int			fib_num_tclassid_users;
#endif
	struct hlist_head	*fib_table_hash;
	bool			fib_offload_disabled;
	struct sock		*fibnl;

	struct sock  * __percpu	*icmp_sk;
	struct sock		*mc_autojoin_sk;

	struct inet_peer_base	*peers;
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
	int sysctl_tcp_ecn_fallback;

	int sysctl_ip_default_ttl;
	int sysctl_ip_no_pmtu_disc;
	int sysctl_ip_fwd_use_pmtu;
	int sysctl_ip_fwd_update_priority;
	int sysctl_ip_nonlocal_bind;
	/* Shall we try to damage output packets if routing dev changes? */
	int sysctl_ip_dynaddr;
	int sysctl_ip_early_demux;
	int sysctl_tcp_early_demux;
	int sysctl_udp_early_demux;

	int sysctl_fwmark_reflect;
	int sysctl_tcp_fwmark_accept;
#ifdef CONFIG_NET_L3_MASTER_DEV
	int sysctl_tcp_l3mdev_accept;
#endif
	int sysctl_tcp_mtu_probing;
	int sysctl_tcp_base_mss;
	int sysctl_tcp_probe_threshold;
	u32 sysctl_tcp_probe_interval;

	int sysctl_tcp_keepalive_time;
	int sysctl_tcp_keepalive_probes;
	int sysctl_tcp_keepalive_intvl;

	int sysctl_tcp_syn_retries;
	int sysctl_tcp_synack_retries;
	int sysctl_tcp_syncookies;
	int sysctl_tcp_reordering;
	int sysctl_tcp_retries1;
	int sysctl_tcp_retries2;
	int sysctl_tcp_orphan_retries;
	int sysctl_tcp_fin_timeout;
	unsigned int sysctl_tcp_notsent_lowat;
	int sysctl_tcp_tw_reuse;
	int sysctl_tcp_sack;
	int sysctl_tcp_window_scaling;
	int sysctl_tcp_timestamps;
	int sysctl_tcp_early_retrans;
	int sysctl_tcp_recovery;
	int sysctl_tcp_thin_linear_timeouts;
	int sysctl_tcp_slow_start_after_idle;
	int sysctl_tcp_retrans_collapse;
	int sysctl_tcp_stdurg;
	int sysctl_tcp_rfc1337;
	int sysctl_tcp_abort_on_overflow;
	int sysctl_tcp_fack;
	int sysctl_tcp_max_reordering;
	int sysctl_tcp_dsack;
	int sysctl_tcp_app_win;
	int sysctl_tcp_adv_win_scale;
	int sysctl_tcp_frto;
	int sysctl_tcp_nometrics_save;
	int sysctl_tcp_moderate_rcvbuf;
	int sysctl_tcp_tso_win_divisor;
	int sysctl_tcp_workaround_signed_windows;
	int sysctl_tcp_limit_output_bytes;
	int sysctl_tcp_challenge_ack_limit;
	int sysctl_tcp_min_tso_segs;
	int sysctl_tcp_min_rtt_wlen;
	int sysctl_tcp_autocorking;
	int sysctl_tcp_invalid_ratelimit;
	int sysctl_tcp_pacing_ss_ratio;
	int sysctl_tcp_pacing_ca_ratio;
	int sysctl_tcp_wmem[3];
	int sysctl_tcp_rmem[3];
	int sysctl_tcp_comp_sack_nr;
	unsigned long sysctl_tcp_comp_sack_delay_ns;
	struct inet_timewait_death_row tcp_death_row;
	int sysctl_max_syn_backlog;
	int sysctl_tcp_fastopen;
	const struct tcp_congestion_ops __rcu  *tcp_congestion_control;
	struct tcp_fastopen_context __rcu *tcp_fastopen_ctx;
	spinlock_t tcp_fastopen_ctx_lock;
	unsigned int sysctl_tcp_fastopen_blackhole_timeout;
	atomic_t tfo_active_disable_times;
	unsigned long tfo_active_disable_stamp;

	int sysctl_udp_wmem_min;
	int sysctl_udp_rmem_min;

#ifdef CONFIG_NET_L3_MASTER_DEV
	int sysctl_udp_l3mdev_accept;
#endif

	int sysctl_igmp_max_memberships;
	int sysctl_igmp_max_msf;
	int sysctl_igmp_llm_reports;
	int sysctl_igmp_qrv;

	struct ping_group_range ping_group_range;

	atomic_t dev_addr_genid;

#ifdef CONFIG_SYSCTL
	unsigned long *sysctl_local_reserved_ports;
	int sysctl_ip_prot_sock;
#endif

#ifdef CONFIG_IP_MROUTE
#ifndef CONFIG_IP_MROUTE_MULTIPLE_TABLES
	struct mr_table		*mrt;
#else
	struct list_head	mr_tables;
	struct fib_rules_ops	*mr_rules_ops;
#endif
#endif
#ifdef CONFIG_IP_ROUTE_MULTIPATH
	int sysctl_fib_multipath_use_neigh;
	int sysctl_fib_multipath_hash_policy;
#endif

	struct fib_notifier_ops	*notifier_ops;
	unsigned int	fib_seq;	/* protected by rtnl_mutex */

	struct fib_notifier_ops	*ipmr_notifier_ops;
	unsigned int	ipmr_seq;	/* protected by rtnl_mutex */

	atomic_t	rt_genid;
};
#endif
