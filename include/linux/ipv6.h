#ifndef _IPV6_H
#define _IPV6_H

#include <uapi/linux/ipv6.h>

#define ipv6_optlen(p)  (((p)->hdrlen+1) << 3)
/*
 * This structure contains configuration options per IPv6 link.
 */
struct ipv6_devconf {
	__s32		forwarding;
	__s32		hop_limit;
	__s32		mtu6;
	__s32		accept_ra;
	__s32		accept_redirects;
	__s32		autoconf;
	__s32		dad_transmits;
	__s32		rtr_solicits;
	__s32		rtr_solicit_interval;
	__s32		rtr_solicit_delay;
	__s32		force_mld_version;
#ifdef CONFIG_IPV6_PRIVACY
	__s32		use_tempaddr;
	__s32		temp_valid_lft;
	__s32		temp_prefered_lft;
	__s32		regen_max_retry;
	__s32		max_desync_factor;
#endif
	__s32		max_addresses;
	__s32		accept_ra_defrtr;
	__s32		accept_ra_pinfo;
#ifdef CONFIG_IPV6_ROUTER_PREF
	__s32		accept_ra_rtr_pref;
	__s32		rtr_probe_interval;
#ifdef CONFIG_IPV6_ROUTE_INFO
	__s32		accept_ra_rt_info_max_plen;
#endif
#endif
	__s32		proxy_ndp;
	__s32		accept_source_route;
#ifdef CONFIG_IPV6_OPTIMISTIC_DAD
	__s32		optimistic_dad;
#endif
#ifdef CONFIG_IPV6_MROUTE
	__s32		mc_forwarding;
#endif
	__s32		disable_ipv6;
	__s32		accept_dad;
	__s32		force_tllao;
	__s32           ndisc_notify;
	void		*sysctl;
};

struct ipv6_params {
	__s32 disable_ipv6;
	__s32 autoconf;
};
extern struct ipv6_params ipv6_defaults;
#include <linux/icmpv6.h>
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

/* 
   This structure contains results of exthdrs parsing
   as offsets from skb->nh.
 */

struct inet6_skb_parm {
	int			iif;
	__be16			ra;
	__u16			hop;
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

#define IP6SKB_XFRM_TRANSFORMED	1
#define IP6SKB_FORWARDED	2
#define IP6SKB_REROUTED		4
#define IP6SKB_ROUTERALERT	8
};

#define IP6CB(skb)	((struct inet6_skb_parm*)((skb)->cb))
#define IP6CBMTU(skb)	((struct ip6_mtuinfo *)((skb)->cb))

static inline int inet6_iif(const struct sk_buff *skb)
{
	return IP6CB(skb)->iif;
}

struct inet6_request_sock {
	struct in6_addr		loc_addr;
	struct in6_addr		rmt_addr;
	struct sk_buff		*pktopts;
	int			iif;
};

struct tcp6_request_sock {
	struct tcp_request_sock	  tcp6rsk_tcp;
	struct inet6_request_sock tcp6rsk_inet6;
};

struct ipv6_mc_socklist;
struct ipv6_ac_socklist;
struct ipv6_fl_socklist;

/**
 * struct ipv6_pinfo - ipv6 private area
 *
 * In the struct sock hierarchy (tcp6_sock, upd6_sock, etc)
 * this _must_ be the last member, so that inet6_sk_generic
 * is able to calculate its offset from the base struct sock
 * by using the struct proto->slab_obj_size member. -acme
 */
struct ipv6_pinfo {
	struct in6_addr 	saddr;
	struct in6_addr 	rcv_saddr;
	struct in6_addr		daddr;
	struct in6_pktinfo	sticky_pktinfo;
	const struct in6_addr		*daddr_cache;
#ifdef CONFIG_IPV6_SUBTREES
	const struct in6_addr		*saddr_cache;
#endif

	__be32			flow_label;
	__u32			frag_size;

	/*
	 * Packed in 16bits.
	 * Omit one shift by by putting the signed field at MSB.
	 */
#if defined(__BIG_ENDIAN_BITFIELD)
	__s16			hop_limit:9;
	__u16			__unused_1:7;
#else
	__u16			__unused_1:7;
	__s16			hop_limit:9;
#endif

#if defined(__BIG_ENDIAN_BITFIELD)
	/* Packed in 16bits. */
	__s16			mcast_hops:9;
	__u16			__unused_2:6,
				mc_loop:1;
#else
	__u16			mc_loop:1,
				__unused_2:6;
	__s16			mcast_hops:9;
#endif
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
				rxorigdstaddr:1;
				/* 2 bits hole */
		} bits;
		__u16		all;
	} rxopt;

	/* sockopt flags */
	__u16			recverr:1,
	                        sndflow:1,
				pmtudisc:2,
				ipv6only:1,
				srcprefs:3,	/* 001: prefer temporary address
						 * 010: prefer public address
						 * 100: prefer care-of address
						 */
				dontfrag:1;
	__u8			min_hopcount;
	__u8			tclass;
	__u8			rcv_tclass;

	__u32			dst_cookie;
	__u32			rx_dst_cookie;

	struct ipv6_mc_socklist	__rcu *ipv6_mc_list;
	struct ipv6_ac_socklist	*ipv6_ac_list;
	struct ipv6_fl_socklist __rcu *ipv6_fl_list;

	struct ipv6_txoptions	*opt;
	struct sk_buff		*pktoptions;
	struct sk_buff		*rxpmtu;
	struct {
		struct ipv6_txoptions *opt;
		u8 hop_limit;
		u8 tclass;
	} cork;
};

/* WARNING: don't change the layout of the members in {raw,udp,tcp}6_sock! */
struct raw6_sock {
	/* inet_sock has to be the first member of raw6_sock */
	struct inet_sock	inet;
	__u32			checksum;	/* perform checksum */
	__u32			offset;		/* checksum offset  */
	struct icmp6_filter	filter;
	__u32			ip6mr_table;
	/* ipv6_pinfo has to be the last member of raw6_sock, see inet6_sk_generic */
	struct ipv6_pinfo	inet6;
};

struct udp6_sock {
	struct udp_sock	  udp;
	/* ipv6_pinfo has to be the last member of udp6_sock, see inet6_sk_generic */
	struct ipv6_pinfo inet6;
};

struct tcp6_sock {
	struct tcp_sock	  tcp;
	/* ipv6_pinfo has to be the last member of tcp6_sock, see inet6_sk_generic */
	struct ipv6_pinfo inet6;
};

extern int inet6_sk_rebuild_header(struct sock *sk);

struct inet6_timewait_sock {
	struct in6_addr tw_v6_daddr;
	struct in6_addr	tw_v6_rcv_saddr;
};

struct tcp6_timewait_sock {
	struct tcp_timewait_sock   tcp6tw_tcp;
	struct inet6_timewait_sock tcp6tw_inet6;
};

static inline struct inet6_timewait_sock *inet6_twsk(const struct sock *sk)
{
	return (struct inet6_timewait_sock *)(((u8 *)sk) +
					      inet_twsk(sk)->tw_ipv6_offset);
}

#if IS_ENABLED(CONFIG_IPV6)
static inline struct ipv6_pinfo * inet6_sk(const struct sock *__sk)
{
	return inet_sk(__sk)->pinet6;
}

static inline struct inet6_request_sock *
			inet6_rsk(const struct request_sock *rsk)
{
	return (struct inet6_request_sock *)(((u8 *)rsk) +
					     inet_rsk(rsk)->inet6_rsk_offset);
}

static inline u32 inet6_rsk_offset(struct request_sock *rsk)
{
	return rsk->rsk_ops->obj_size - sizeof(struct inet6_request_sock);
}

static inline struct request_sock *inet6_reqsk_alloc(struct request_sock_ops *ops)
{
	struct request_sock *req = reqsk_alloc(ops);

	if (req != NULL) {
		inet_rsk(req)->inet6_rsk_offset = inet6_rsk_offset(req);
		inet6_rsk(req)->pktopts = NULL;
	}

	return req;
}

static inline struct raw6_sock *raw6_sk(const struct sock *sk)
{
	return (struct raw6_sock *)sk;
}

static inline void inet_sk_copy_descendant(struct sock *sk_to,
					   const struct sock *sk_from)
{
	int ancestor_size = sizeof(struct inet_sock);

	if (sk_from->sk_family == PF_INET6)
		ancestor_size += sizeof(struct ipv6_pinfo);

	__inet_sk_copy_descendant(sk_to, sk_from, ancestor_size);
}

#define __ipv6_only_sock(sk)	(inet6_sk(sk)->ipv6only)
#define ipv6_only_sock(sk)	((sk)->sk_family == PF_INET6 && __ipv6_only_sock(sk))

static inline u16 inet6_tw_offset(const struct proto *prot)
{
	return prot->twsk_prot->twsk_obj_size -
			sizeof(struct inet6_timewait_sock);
}

static inline struct in6_addr *__inet6_rcv_saddr(const struct sock *sk)
{
	return likely(sk->sk_state != TCP_TIME_WAIT) ?
		&inet6_sk(sk)->rcv_saddr : &inet6_twsk(sk)->tw_v6_rcv_saddr;
}

static inline struct in6_addr *inet6_rcv_saddr(const struct sock *sk)
{
	return sk->sk_family == AF_INET6 ? __inet6_rcv_saddr(sk) : NULL;
}

static inline int inet_v6_ipv6only(const struct sock *sk)
{
	return likely(sk->sk_state != TCP_TIME_WAIT) ?
		ipv6_only_sock(sk) : inet_twsk(sk)->tw_ipv6only;
}
#else
#define __ipv6_only_sock(sk)	0
#define ipv6_only_sock(sk)	0

static inline struct ipv6_pinfo * inet6_sk(const struct sock *__sk)
{
	return NULL;
}

static inline struct inet6_request_sock *
			inet6_rsk(const struct request_sock *rsk)
{
	return NULL;
}

static inline struct raw6_sock *raw6_sk(const struct sock *sk)
{
	return NULL;
}

#define __inet6_rcv_saddr(__sk)	NULL
#define inet6_rcv_saddr(__sk)	NULL
#define tcp_twsk_ipv6only(__sk)		0
#define inet_v6_ipv6only(__sk)		0
#endif /* IS_ENABLED(CONFIG_IPV6) */

#define INET6_MATCH(__sk, __net, __saddr, __daddr, __ports, __dif)	\
	((inet_sk(__sk)->inet_portpair == (__ports))		&&	\
	 ((__sk)->sk_family == AF_INET6)			&&	\
	 ipv6_addr_equal(&inet6_sk(__sk)->daddr, (__saddr))	&&	\
	 ipv6_addr_equal(&inet6_sk(__sk)->rcv_saddr, (__daddr))	&&	\
	 (!(__sk)->sk_bound_dev_if	||				\
	   ((__sk)->sk_bound_dev_if == (__dif))) 		&&	\
	 net_eq(sock_net(__sk), (__net)))

#define INET6_TW_MATCH(__sk, __net, __saddr, __daddr, __ports, __dif)	   \
	((inet_twsk(__sk)->tw_portpair == (__ports))			&& \
	 ((__sk)->sk_family == AF_INET6)				&& \
	 ipv6_addr_equal(&inet6_twsk(__sk)->tw_v6_daddr, (__saddr))	&& \
	 ipv6_addr_equal(&inet6_twsk(__sk)->tw_v6_rcv_saddr, (__daddr)) && \
	 (!(__sk)->sk_bound_dev_if	||				   \
	  ((__sk)->sk_bound_dev_if == (__dif)))				&& \
	 net_eq(sock_net(__sk), (__net)))

#endif /* _IPV6_H */
