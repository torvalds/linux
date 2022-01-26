/* SPDX-License-Identifier: GPL-2.0 */
/*
 * ipv4 in net namespaces
 */

#ifndef __NETNS_IPV4_H__
#define __NETNS_IPV4_H__

#include <linux/uidgid.h>
#include <net/inet_frag.h>
#include <linux/rcupdate.h>
#include <linux/siphash.h>

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
	refcount_t		tw_refcount;

	struct inet_hashinfo 	*hashinfo ____cacheline_aligned_in_smp;
	int			sysctl_max_tw_buckets;
};

struct tcp_fastopen_context;

struct netns_ipv4 {
	struct inet_timewait_death_row *tcp_death_row;

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
	struct fib_table __rcu	*fib_main;
	struct fib_table __rcu	*fib_default;
	unsigned int		fib_rules_require_fldissect;
	bool			fib_has_custom_rules;
#endif
	bool			fib_has_custom_local_routes;
	bool			fib_offload_disabled;
#ifdef CONFIG_IP_ROUTE_CLASSID
	atomic_t		fib_num_tclassid_users;
#endif
	struct hlist_head	*fib_table_hash;
	struct sock		*fibnl;

	struct sock		*mc_autojoin_sk;

	struct inet_peer_base	*peers;
	struct fqdir		*fqdir;

	u8 sysctl_icmp_echo_ignore_all;
	u8 sysctl_icmp_echo_enable_probe;
	u8 sysctl_icmp_echo_ignore_broadcasts;
	u8 sysctl_icmp_ignore_bogus_error_responses;
	u8 sysctl_icmp_errors_use_inbound_ifaddr;
	int sysctl_icmp_ratelimit;
	int sysctl_icmp_ratemask;

	u32 ip_rt_min_pmtu;
	int ip_rt_mtu_expires;
	int ip_rt_min_advmss;

	struct local_ports ip_local_ports;

	u8 sysctl_tcp_ecn;
	u8 sysctl_tcp_ecn_fallback;

	u8 sysctl_ip_default_ttl;
	u8 sysctl_ip_no_pmtu_disc;
	u8 sysctl_ip_fwd_use_pmtu;
	u8 sysctl_ip_fwd_update_priority;
	u8 sysctl_ip_nonlocal_bind;
	u8 sysctl_ip_autobind_reuse;
	/* Shall we try to damage output packets if routing dev changes? */
	u8 sysctl_ip_dynaddr;
	u8 sysctl_ip_early_demux;
#ifdef CONFIG_NET_L3_MASTER_DEV
	u8 sysctl_raw_l3mdev_accept;
#endif
	u8 sysctl_tcp_early_demux;
	u8 sysctl_udp_early_demux;

	u8 sysctl_nexthop_compat_mode;

	u8 sysctl_fwmark_reflect;
	u8 sysctl_tcp_fwmark_accept;
#ifdef CONFIG_NET_L3_MASTER_DEV
	u8 sysctl_tcp_l3mdev_accept;
#endif
	u8 sysctl_tcp_mtu_probing;
	int sysctl_tcp_mtu_probe_floor;
	int sysctl_tcp_base_mss;
	int sysctl_tcp_min_snd_mss;
	int sysctl_tcp_probe_threshold;
	u32 sysctl_tcp_probe_interval;

	int sysctl_tcp_keepalive_time;
	int sysctl_tcp_keepalive_intvl;
	u8 sysctl_tcp_keepalive_probes;

	u8 sysctl_tcp_syn_retries;
	u8 sysctl_tcp_synack_retries;
	u8 sysctl_tcp_syncookies;
	u8 sysctl_tcp_migrate_req;
	int sysctl_tcp_reordering;
	u8 sysctl_tcp_retries1;
	u8 sysctl_tcp_retries2;
	u8 sysctl_tcp_orphan_retries;
	u8 sysctl_tcp_tw_reuse;
	int sysctl_tcp_fin_timeout;
	unsigned int sysctl_tcp_notsent_lowat;
	u8 sysctl_tcp_sack;
	u8 sysctl_tcp_window_scaling;
	u8 sysctl_tcp_timestamps;
	u8 sysctl_tcp_early_retrans;
	u8 sysctl_tcp_recovery;
	u8 sysctl_tcp_thin_linear_timeouts;
	u8 sysctl_tcp_slow_start_after_idle;
	u8 sysctl_tcp_retrans_collapse;
	u8 sysctl_tcp_stdurg;
	u8 sysctl_tcp_rfc1337;
	u8 sysctl_tcp_abort_on_overflow;
	u8 sysctl_tcp_fack; /* obsolete */
	int sysctl_tcp_max_reordering;
	int sysctl_tcp_adv_win_scale;
	u8 sysctl_tcp_dsack;
	u8 sysctl_tcp_app_win;
	u8 sysctl_tcp_frto;
	u8 sysctl_tcp_nometrics_save;
	u8 sysctl_tcp_no_ssthresh_metrics_save;
	u8 sysctl_tcp_moderate_rcvbuf;
	u8 sysctl_tcp_tso_win_divisor;
	u8 sysctl_tcp_workaround_signed_windows;
	int sysctl_tcp_limit_output_bytes;
	int sysctl_tcp_challenge_ack_limit;
	int sysctl_tcp_min_rtt_wlen;
	u8 sysctl_tcp_min_tso_segs;
	u8 sysctl_tcp_autocorking;
	u8 sysctl_tcp_reflect_tos;
	u8 sysctl_tcp_comp_sack_nr;
	int sysctl_tcp_invalid_ratelimit;
	int sysctl_tcp_pacing_ss_ratio;
	int sysctl_tcp_pacing_ca_ratio;
	int sysctl_tcp_wmem[3];
	int sysctl_tcp_rmem[3];
	unsigned long sysctl_tcp_comp_sack_delay_ns;
	unsigned long sysctl_tcp_comp_sack_slack_ns;
	int sysctl_max_syn_backlog;
	int sysctl_tcp_fastopen;
	const struct tcp_congestion_ops __rcu  *tcp_congestion_control;
	struct tcp_fastopen_context __rcu *tcp_fastopen_ctx;
	unsigned int sysctl_tcp_fastopen_blackhole_timeout;
	atomic_t tfo_active_disable_times;
	unsigned long tfo_active_disable_stamp;

	int sysctl_udp_wmem_min;
	int sysctl_udp_rmem_min;

	u8 sysctl_fib_notify_on_flag_change;

#ifdef CONFIG_NET_L3_MASTER_DEV
	u8 sysctl_udp_l3mdev_accept;
#endif

	u8 sysctl_igmp_llm_reports;
	int sysctl_igmp_max_memberships;
	int sysctl_igmp_max_msf;
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
	u32 sysctl_fib_multipath_hash_fields;
	u8 sysctl_fib_multipath_use_neigh;
	u8 sysctl_fib_multipath_hash_policy;
#endif

	struct fib_notifier_ops	*notifier_ops;
	unsigned int	fib_seq;	/* protected by rtnl_mutex */

	struct fib_notifier_ops	*ipmr_notifier_ops;
	unsigned int	ipmr_seq;	/* protected by rtnl_mutex */

	atomic_t	rt_genid;
	siphash_key_t	ip_id_key;
};
#endif
