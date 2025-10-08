/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _IPV6_H
#define _IPV6_H

#include <uapi/linux/ipv6.h>
#include <linux/cache.h>

#define ipv6_optlen(p)  (((p)->hdrlen+1) << 3)
#define ipv6_authlen(p) (((p)->hdrlen+2) << 2)
/*
 * This structure contains configuration options per IPv6 link.
 */
struct ipv6_devconf {
	/* RX & TX fastpath fields. */
	__cacheline_group_begin(ipv6_devconf_read_txrx);
	__s32		disable_ipv6;
	__s32		hop_limit;
	__s32		mtu6;
	__s32		forwarding;
	__s32		force_forwarding;
	__s32		disable_policy;
	__s32		proxy_ndp;
	__cacheline_group_end(ipv6_devconf_read_txrx);

	__s32		accept_ra;
	__s32		accept_redirects;
	__s32		autoconf;
	__s32		dad_transmits;
	__s32		rtr_solicits;
	__s32		rtr_solicit_interval;
	__s32		rtr_solicit_max_interval;
	__s32		rtr_solicit_delay;
	__s32		force_mld_version;
	__s32		mldv1_unsolicited_report_interval;
	__s32		mldv2_unsolicited_report_interval;
	__s32		use_tempaddr;
	__s32		temp_valid_lft;
	__s32		temp_prefered_lft;
	__s32		regen_min_advance;
	__s32		regen_max_retry;
	__s32		max_desync_factor;
	__s32		max_addresses;
	__s32		accept_ra_defrtr;
	__u32		ra_defrtr_metric;
	__s32		accept_ra_min_hop_limit;
	__s32		accept_ra_min_lft;
	__s32		accept_ra_pinfo;
	__s32		ignore_routes_with_linkdown;
#ifdef CONFIG_IPV6_ROUTER_PREF
	__s32		accept_ra_rtr_pref;
	__s32		rtr_probe_interval;
#ifdef CONFIG_IPV6_ROUTE_INFO
	__s32		accept_ra_rt_info_min_plen;
	__s32		accept_ra_rt_info_max_plen;
#endif
#endif
	__s32		accept_source_route;
	__s32		accept_ra_from_local;
#ifdef CONFIG_IPV6_OPTIMISTIC_DAD
	__s32		optimistic_dad;
	__s32		use_optimistic;
#endif
#ifdef CONFIG_IPV6_MROUTE
	atomic_t	mc_forwarding;
#endif
	__s32		drop_unicast_in_l2_multicast;
	__s32		accept_dad;
	__s32		force_tllao;
	__s32           ndisc_notify;
	__s32		suppress_frag_ndisc;
	__s32		accept_ra_mtu;
	__s32		drop_unsolicited_na;
	__s32		accept_untracked_na;
	struct ipv6_stable_secret {
		bool initialized;
		struct in6_addr secret;
	} stable_secret;
	__s32		use_oif_addrs_only;
	__s32		keep_addr_on_down;
	__s32		seg6_enabled;
#ifdef CONFIG_IPV6_SEG6_HMAC
	__s32		seg6_require_hmac;
#endif
	__u32		enhanced_dad;
	__u32		addr_gen_mode;
	__s32           ndisc_tclass;
	__s32		rpl_seg_enabled;
	__u32		ioam6_id;
	__u32		ioam6_id_wide;
	__u8		ioam6_enabled;
	__u8		ndisc_evict_nocarrier;
	__u8		ra_honor_pio_life;
	__u8		ra_honor_pio_pflag;

	struct ctl_table_header *sysctl_header;
};

struct ipv6_params {
	__s32 disable_ipv6;
	__s32 autoconf;
};
extern struct ipv6_params ipv6_defaults;
#include <linux/tcp.h>
#include <linux/udp.h>

#include <net/inet_sock.h>

static inline struct ipv6hdr *ipv6_hdr(const struct sk_buff *skb)
{
	return (struct ipv6hdr *)skb_network_header(skb);
}

static inline struct ipv6hdr *inner_ipv6_hdr(const struct sk_buff *skb)
{
	return (struct ipv6hdr *)skb_inner_network_header(skb);
}

static inline struct ipv6hdr *ipipv6_hdr(const struct sk_buff *skb)
{
	return (struct ipv6hdr *)skb_transport_header(skb);
}

static inline unsigned int ipv6_transport_len(const struct sk_buff *skb)
{
	return ntohs(ipv6_hdr(skb)->payload_len) + sizeof(struct ipv6hdr) -
	       skb_network_header_len(skb);
}

/* 
   This structure contains results of exthdrs parsing
   as offsets from skb->nh.
 */

struct inet6_skb_parm {
	int			iif;
	__be16			ra;
	__u16			dst0;
	__u16			srcrt;
	__u16			dst1;
	__u16			lastopt;
	__u16			nhoff;
	__u16			flags;
#if defined(CONFIG_IPV6_MIP6) || defined(CONFIG_IPV6_MIP6_MODULE)
	__u16			dsthao;
#endif
	__u16			frag_max_size;
	__u16			srhoff;

#define IP6SKB_XFRM_TRANSFORMED	1
#define IP6SKB_FORWARDED	2
#define IP6SKB_REROUTED		4
#define IP6SKB_ROUTERALERT	8
#define IP6SKB_FRAGMENTED      16
#define IP6SKB_HOPBYHOP        32
#define IP6SKB_L3SLAVE         64
#define IP6SKB_JUMBOGRAM      128
#define IP6SKB_SEG6	      256
#define IP6SKB_FAKEJUMBO      512
#define IP6SKB_MULTIPATH      1024
#define IP6SKB_MCROUTE        2048
};

#if defined(CONFIG_NET_L3_MASTER_DEV)
static inline bool ipv6_l3mdev_skb(__u16 flags)
{
	return flags & IP6SKB_L3SLAVE;
}
#else
static inline bool ipv6_l3mdev_skb(__u16 flags)
{
	return false;
}
#endif

#define IP6CB(skb)	((struct inet6_skb_parm*)((skb)->cb))
#define IP6CBMTU(skb)	((struct ip6_mtuinfo *)((skb)->cb))

static inline int inet6_iif(const struct sk_buff *skb)
{
	bool l3_slave = ipv6_l3mdev_skb(IP6CB(skb)->flags);

	return l3_slave ? skb->skb_iif : IP6CB(skb)->iif;
}

static inline bool inet6_is_jumbogram(const struct sk_buff *skb)
{
	return !!(IP6CB(skb)->flags & IP6SKB_JUMBOGRAM);
}

/* can not be used in TCP layer after tcp_v6_fill_cb */
static inline int inet6_sdif(const struct sk_buff *skb)
{
#if IS_ENABLED(CONFIG_NET_L3_MASTER_DEV)
	if (skb && ipv6_l3mdev_skb(IP6CB(skb)->flags))
		return IP6CB(skb)->iif;
#endif
	return 0;
}

struct tcp6_request_sock {
	struct tcp_request_sock	  tcp6rsk_tcp;
};

struct ipv6_mc_socklist;
struct ipv6_ac_socklist;
struct ipv6_fl_socklist;

struct inet6_cork {
	struct ipv6_txoptions *opt;
	u8 hop_limit;
	u8 tclass;
	u8 dontfrag:1;
};

/* struct ipv6_pinfo - ipv6 private area */
struct ipv6_pinfo {
	struct in6_addr 	saddr;
	struct in6_pktinfo	sticky_pktinfo;
	const struct in6_addr		*daddr_cache;
#ifdef CONFIG_IPV6_SUBTREES
	const struct in6_addr		*saddr_cache;
#endif

	__be32			flow_label;
	__u32			frag_size;

	s16			hop_limit;
	u8			mcast_hops;

	int			ucast_oif;
	int			mcast_oif;

	/* pktoption flags */
	union {
		struct {
			__u16	srcrt:1,
				osrcrt:1,
			        rxinfo:1,
			        rxoinfo:1,
				rxhlim:1,
				rxohlim:1,
				hopopts:1,
				ohopopts:1,
				dstopts:1,
				odstopts:1,
                                rxflow:1,
				rxtclass:1,
				rxpmtu:1,
				rxorigdstaddr:1,
				recvfragsize:1;
				/* 1 bits hole */
		} bits;
		__u16		all;
	} rxopt;

	/* sockopt flags */
	__u8			srcprefs;	/* 001: prefer temporary address
						 * 010: prefer public address
						 * 100: prefer care-of address
						 */
	__u8			pmtudisc;
	__u8			min_hopcount;
	__u8			tclass;
	__be32			rcv_flowinfo;

	__u32			dst_cookie;

	struct ipv6_mc_socklist	__rcu *ipv6_mc_list;
	struct ipv6_ac_socklist	*ipv6_ac_list;
	struct ipv6_fl_socklist __rcu *ipv6_fl_list;

	struct ipv6_txoptions __rcu	*opt;
	struct sk_buff		*pktoptions;
	struct sk_buff		*rxpmtu;
	struct inet6_cork	cork;
};

/* We currently use available bits from inet_sk(sk)->inet_flags,
 * this could change in the future.
 */
#define inet6_test_bit(nr, sk)			\
	test_bit(INET_FLAGS_##nr, &inet_sk(sk)->inet_flags)
#define inet6_set_bit(nr, sk)			\
	set_bit(INET_FLAGS_##nr, &inet_sk(sk)->inet_flags)
#define inet6_clear_bit(nr, sk)			\
	clear_bit(INET_FLAGS_##nr, &inet_sk(sk)->inet_flags)
#define inet6_assign_bit(nr, sk, val)		\
	assign_bit(INET_FLAGS_##nr, &inet_sk(sk)->inet_flags, val)

/* WARNING: don't change the layout of the members in {raw,udp,tcp}6_sock! */
struct raw6_sock {
	/* inet_sock has to be the first member of raw6_sock */
	struct inet_sock	inet;
	__u32			checksum;	/* perform checksum */
	__u32			offset;		/* checksum offset  */
	struct icmp6_filter	filter;
	__u32			ip6mr_table;

	struct ipv6_pinfo	inet6;
};

struct udp6_sock {
	struct udp_sock	  udp;

	struct ipv6_pinfo inet6;
};

struct tcp6_sock {
	struct tcp_sock	  tcp;

	struct ipv6_pinfo inet6;
};

extern int inet6_sk_rebuild_header(struct sock *sk);

struct tcp6_timewait_sock {
	struct tcp_timewait_sock   tcp6tw_tcp;
};

#if IS_ENABLED(CONFIG_IPV6)
bool ipv6_mod_enabled(void);

static inline struct ipv6_pinfo *inet6_sk(const struct sock *__sk)
{
	return sk_fullsock(__sk) ? inet_sk(__sk)->pinet6 : NULL;
}

#define raw6_sk(ptr) container_of_const(ptr, struct raw6_sock, inet.sk)

#define ipv6_only_sock(sk)	(sk->sk_ipv6only)
#define ipv6_sk_rxinfo(sk)	((sk)->sk_family == PF_INET6 && \
				 inet6_sk(sk)->rxopt.bits.rxinfo)

static inline const struct in6_addr *inet6_rcv_saddr(const struct sock *sk)
{
	if (sk->sk_family == AF_INET6)
		return &sk->sk_v6_rcv_saddr;
	return NULL;
}

static inline int inet_v6_ipv6only(const struct sock *sk)
{
	/* ipv6only field is at same position for timewait and other sockets */
	return ipv6_only_sock(sk);
}
#else
#define ipv6_only_sock(sk)	0
#define ipv6_sk_rxinfo(sk)	0

static inline bool ipv6_mod_enabled(void)
{
	return false;
}

static inline struct ipv6_pinfo * inet6_sk(const struct sock *__sk)
{
	return NULL;
}

static inline struct raw6_sock *raw6_sk(const struct sock *sk)
{
	return NULL;
}

#define inet6_rcv_saddr(__sk)	NULL
#define inet_v6_ipv6only(__sk)		0
#endif /* IS_ENABLED(CONFIG_IPV6) */
#endif /* _IPV6_H */
