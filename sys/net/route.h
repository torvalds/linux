/*	$OpenBSD: route.h,v 1.218 2025/07/14 08:48:51 dlg Exp $	*/
/*	$NetBSD: route.h,v 1.9 1996/02/13 22:00:49 christos Exp $	*/

/*
 * Copyright (c) 1980, 1986, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	@(#)route.h	8.3 (Berkeley) 4/19/94
 */

#ifndef _NET_ROUTE_H_
#define _NET_ROUTE_H_

/*
 * Locks used to protect struct members in this file:
 *	I	immutable after creation
 *	N	net lock
 *	X	exclusive net lock, or shared net lock + kernel lock
 *	R	rtable lock
 *	r	per route entry mutex	rt_mtx
 *	L	arp/nd6/etc lock for updates, net lock for reads
 *	T	rttimer_mtx		route timer lists
 */

/*
 * Kernel resident routing tables.
 *
 * The routing tables are initialized when interface addresses
 * are set by making entries for all directly connected interfaces.
 */

#ifdef _KERNEL
/*
 * These numbers are used by reliable protocols for determining
 * retransmission behavior and are included in the routing structure.
 */
struct rt_kmetrics {
	u_int64_t	rmx_pksent;	/* packets sent using this route */
	int64_t		rmx_expire;	/* lifetime for route, e.g. redirect */
	u_int		rmx_locks;	/* Kernel must leave these values */
	u_int		rmx_mtu;	/* [a] MTU for this path */
};
#endif

/*
 * Huge version for userland compatibility.
 */
struct rt_metrics {
	u_int64_t	rmx_pksent;	/* packets sent using this route */
	int64_t		rmx_expire;	/* lifetime for route, e.g. redirect */
	u_int		rmx_locks;	/* Kernel must leave these values */
	u_int		rmx_mtu;	/* MTU for this path */
	u_int		rmx_refcnt;	/* # references hold */
	/* some apps may still need these no longer used metrics */
	u_int		rmx_hopcount;	/* max hops expected */
	u_int		rmx_recvpipe;	/* inbound delay-bandwidth product */
	u_int		rmx_sendpipe;	/* outbound delay-bandwidth product */
	u_int		rmx_ssthresh;	/* outbound gateway buffer limit */
	u_int		rmx_rtt;	/* estimated round trip time */
	u_int		rmx_rttvar;	/* estimated rtt variance */
	u_int		rmx_pad;
};

#ifdef _KERNEL
/*
 * rmx_rtt and rmx_rttvar are stored as microseconds;
 * RTTTOPRHZ(rtt) converts to a value suitable for use
 * by a protocol slowtimo counter.
 */
#define	RTM_RTTUNIT	1000000	/* units for rtt, rttvar, as units per sec */
#define	RTTTOPRHZ(r)	((r) / (RTM_RTTUNIT / PR_SLOWHZ))

#include <sys/mutex.h>
#include <sys/queue.h>
#include <net/rtable.h>

struct rttimer;

/*
 * We distinguish between routes to hosts and routes to networks,
 * preferring the former if available.  For each route we infer
 * the interface to use from the gateway address supplied when
 * the route was entered.  Routes that forward packets through
 * gateways are marked with RTF_GATEWAY so that the output routines
 * know to address the gateway rather than the ultimate destination.
 *
 * How the RT_gw union is used also depends on RTF_GATEWAY. With
 * RTF_GATEWAY set, rt_gwroute points at the rtentry for the rt_gateway
 * address. If RTF_GATEWAY is not set, rt_cachecnt contains the
 * number of RTF_GATEWAY rtentry structs with their rt_gwroute pointing
 * at this rtentry.
 */

struct rtentry {
	struct sockaddr	*rt_dest;	/* [I] destination */
	struct rtentry	*rt_next;	/* [R] next mpath entry to our dst */
	struct sockaddr	*rt_gateway;	/* [X] gateway address */
	struct ifaddr	*rt_ifa;	/* [N] interface addr to use */
	caddr_t		 rt_llinfo;	/* [L] pointer to link level info or
					   an MPLS structure */
	struct rtentry	*rt_gwroute;	/* [X] rtentry for rt_gateway */
	struct rtentry	*rt_parent;	/* [N] if cloned, parent rtentry */
	LIST_HEAD(, rttimer) rt_timer;  /* queue of timeouts for misc funcs */
	struct mutex	 rt_mtx;	/* lock members of this struct */
	struct refcnt	 rt_refcnt;	/* # held references */
	struct rt_kmetrics rt_rmx;	/* metrics used by rx'ing protocols */
	unsigned int	 rt_cachecnt;	/* [r] # gateway rtentry refs */
	unsigned int	 rt_ifidx;	/* [N] interface to use */
	unsigned int	 rt_flags;	/* [X] up/down?, host/net */
	int		 rt_plen;	/* [I] prefix length */
	uint16_t	 rt_labelid;	/* [N] route label ID */
	uint8_t		 rt_priority;	/* [N] routing priority to use */
};
#define	rt_use		rt_rmx.rmx_pksent
#define	rt_expire	rt_rmx.rmx_expire
#define	rt_locks	rt_rmx.rmx_locks
#define	rt_mtu		rt_rmx.rmx_mtu

#endif /* _KERNEL */

/* bitmask values for rtm_flags */
#define	RTF_UP		0x1		/* route usable */
#define	RTF_GATEWAY	0x2		/* destination is a gateway */
#define	RTF_HOST	0x4		/* host entry (net otherwise) */
#define	RTF_REJECT	0x8		/* host or net unreachable */
#define	RTF_DYNAMIC	0x10		/* created dynamically (by redirect) */
#define	RTF_MODIFIED	0x20		/* modified dynamically (by redirect) */
#define RTF_DONE	0x40		/* message confirmed */
#define RTF_CLONING	0x100		/* generate new routes on use */
#define RTF_MULTICAST	0x200		/* route associated to a mcast addr. */
#define RTF_LLINFO	0x400		/* generated by ARP or ND */
#define RTF_STATIC	0x800		/* manually added */
#define RTF_BLACKHOLE	0x1000		/* just discard pkts (during updates) */
#define RTF_PROTO3	0x2000		/* protocol specific routing flag */
#define RTF_PROTO2	0x4000		/* protocol specific routing flag */
#define RTF_ANNOUNCE	RTF_PROTO2	/* announce L2 entry */
#define RTF_PROTO1	0x8000		/* protocol specific routing flag */
#define RTF_CLONED	0x10000		/* this is a cloned route */
#define RTF_CACHED	0x20000		/* cached by a RTF_GATEWAY entry */
#define RTF_MPATH	0x40000		/* multipath route or operation */
#define RTF_MPLS	0x100000	/* MPLS additional infos */
#define RTF_LOCAL	0x200000	/* route to a local address */
#define RTF_BROADCAST	0x400000	/* route associated to a bcast addr. */
#define RTF_CONNECTED	0x800000	/* interface route */
#define RTF_BFD		0x1000000	/* Link state controlled by BFD */

/* mask of RTF flags that are allowed to be modified by RTM_CHANGE */
#define RTF_FMASK	\
    (RTF_LLINFO | RTF_PROTO1 | RTF_PROTO2 | RTF_PROTO3 | RTF_BLACKHOLE | \
     RTF_REJECT | RTF_STATIC | RTF_MPLS | RTF_BFD)

/* Routing priorities used by the different routing protocols */
#define RTP_NONE	0	/* unset priority use sane default */
#define RTP_LOCAL	1	/* local address routes (must be the highest) */
#define RTP_CONNECTED	4	/* directly connected routes */
#define RTP_STATIC	8	/* static routes base priority */
#define RTP_EIGRP	28	/* EIGRP routes */
#define RTP_OSPF	32	/* OSPF routes */
#define RTP_ISIS	36	/* IS-IS routes */
#define RTP_RIP		40	/* RIP routes */
#define RTP_BGP		48	/* BGP routes */
#define RTP_DEFAULT	56	/* routes that have nothing set */
#define RTP_PROPOSAL_STATIC	57
#define RTP_PROPOSAL_DHCLIENT	58
#define RTP_PROPOSAL_SLAAC	59
#define RTP_PROPOSAL_UMB	60
#define RTP_PROPOSAL_PPP	61
#define RTP_PROPOSAL_SOLICIT	62	/* request reply of all RTM_PROPOSAL */
#define RTP_MAX		63	/* maximum priority */
#define RTP_ANY		64	/* any of the above */
#define RTP_MASK	0x7f
#define RTP_DOWN	0x80	/* route/link is down */

/*
 * Routing statistics.
 */
struct	rtstat {
	u_int32_t rts_badredirect;	/* bogus redirect calls */
	u_int32_t rts_dynamic;		/* routes created by redirects */
	u_int32_t rts_newgateway;	/* routes modified by redirects */
	u_int32_t rts_unreach;		/* lookups which failed */
	u_int32_t rts_wildcard;		/* lookups satisfied by a wildcard */
};

/*
 * Routing Table Info.
 */
struct rt_tableinfo {
	u_short rti_tableid;	/* routing table id */
	u_short rti_domainid;	/* routing domain id */
};

/*
 * Structures for routing messages.
 */
struct rt_msghdr {
	u_short	rtm_msglen;	/* to skip over non-understood messages */
	u_char	rtm_version;	/* future binary compatibility */
	u_char	rtm_type;	/* message type */
	u_short	rtm_hdrlen;	/* sizeof(rt_msghdr) to skip over the header */
	u_short	rtm_index;	/* index for associated ifp */
	u_short rtm_tableid;	/* routing table id */
	u_char	rtm_priority;	/* routing priority */
	u_char	rtm_mpls;	/* MPLS additional infos */
	int	rtm_addrs;	/* bitmask identifying sockaddrs in msg */
	int	rtm_flags;	/* flags, incl. kern & message, e.g. DONE */
	int	rtm_fmask;	/* bitmask used in RTM_CHANGE message */
	pid_t	rtm_pid;	/* identify sender */
	int	rtm_seq;	/* for sender to identify action */
	int	rtm_errno;	/* why failed */
	u_int	rtm_inits;	/* which metrics we are initializing */
	struct	rt_metrics rtm_rmx; /* metrics themselves */
};
/* overload no longer used field */
#define rtm_use	rtm_rmx.rmx_pksent

#define RTM_VERSION	5	/* Up the ante and ignore older versions */

#define RTM_MAXSIZE	2048	/* Maximum size of an accepted route msg */

/* values for rtm_type */
#define RTM_ADD		0x1	/* Add Route */
#define RTM_DELETE	0x2	/* Delete Route */
#define RTM_CHANGE	0x3	/* Change Metrics or flags */
#define RTM_GET		0x4	/* Report Metrics */
#define RTM_LOSING	0x5	/* Kernel Suspects Partitioning */
#define RTM_REDIRECT	0x6	/* Told to use different route */
#define RTM_MISS	0x7	/* Lookup failed on this address */
#define RTM_RESOLVE	0xb	/* req to resolve dst to LL addr */
#define RTM_NEWADDR	0xc	/* address being added to iface */
#define RTM_DELADDR	0xd	/* address being removed from iface */
#define RTM_IFINFO	0xe	/* iface going up/down etc. */
#define RTM_IFANNOUNCE	0xf	/* iface arrival/departure */
#define RTM_DESYNC	0x10	/* route socket buffer overflow */
#define RTM_INVALIDATE	0x11	/* Invalidate cache of L2 route */
#define RTM_BFD		0x12	/* bidirectional forwarding detection */
#define RTM_PROPOSAL	0x13	/* proposal for resolvd(8) */
#define RTM_CHGADDRATTR	0x14	/* address attribute change */
#define RTM_80211INFO	0x15	/* 80211 iface change */
#define RTM_SOURCE	0x16	/* set source address */

#define RTV_MTU		0x1	/* init or lock _mtu */
#define RTV_HOPCOUNT	0x2	/* init or lock _hopcount */
#define RTV_EXPIRE	0x4	/* init or lock _expire */
#define RTV_RPIPE	0x8	/* init or lock _recvpipe */
#define RTV_SPIPE	0x10	/* init or lock _sendpipe */
#define RTV_SSTHRESH	0x20	/* init or lock _ssthresh */
#define RTV_RTT		0x40	/* init or lock _rtt */
#define RTV_RTTVAR	0x80	/* init or lock _rttvar */

/*
 * Bitmask values for rtm_addrs.
 */
#define RTA_DST		0x1	/* destination sockaddr present */
#define RTA_GATEWAY	0x2	/* gateway sockaddr present */
#define RTA_NETMASK	0x4	/* netmask sockaddr present */
#define RTA_GENMASK	0x8	/* cloning mask sockaddr present */
#define RTA_IFP		0x10	/* interface name sockaddr present */
#define RTA_IFA		0x20	/* interface addr sockaddr present */
#define RTA_AUTHOR	0x40	/* sockaddr for author of redirect */
#define RTA_BRD		0x80	/* for NEWADDR, broadcast or p-p dest addr */
#define RTA_SRC		0x100	/* source sockaddr present */
#define RTA_SRCMASK	0x200	/* source netmask present */
#define RTA_LABEL	0x400	/* route label present */
#define RTA_BFD		0x800	/* bfd present */
#define RTA_DNS		0x1000	/* DNS Servers sockaddr present */
#define RTA_STATIC	0x2000	/* RFC 3442 encoded static routes present */
#define RTA_SEARCH	0x4000	/* RFC 3397 encoded search path present */

/*
 * Index offsets for sockaddr array for alternate internal encoding.
 */
#define RTAX_DST	0	/* destination sockaddr present */
#define RTAX_GATEWAY	1	/* gateway sockaddr present */
#define RTAX_NETMASK	2	/* netmask sockaddr present */
#define RTAX_GENMASK	3	/* cloning mask sockaddr present */
#define RTAX_IFP	4	/* interface name sockaddr present */
#define RTAX_IFA	5	/* interface addr sockaddr present */
#define RTAX_AUTHOR	6	/* sockaddr for author of redirect */
#define RTAX_BRD	7	/* for NEWADDR, broadcast or p-p dest addr */
#define RTAX_SRC	8	/* source sockaddr present */
#define RTAX_SRCMASK	9	/* source netmask present */
#define RTAX_LABEL	10	/* route label present */
#define RTAX_BFD	11	/* bfd present */
#define RTAX_DNS	12	/* DNS Server(s) sockaddr present */
#define RTAX_STATIC	13	/* RFC 3442 encoded static routes present */
#define RTAX_SEARCH	14	/* RFC 3397 encoded search path present */
#define RTAX_MAX	15	/* size of array to allocate */

/*
 * setsockopt defines used for the filtering.
 */
#define ROUTE_MSGFILTER	1	/* bitmask to specify which types should be
				   sent to the client. */
#define ROUTE_TABLEFILTER 2	/* change routing table the socket is listening
				   on, RTABLE_ANY listens on all tables. */
#define ROUTE_PRIOFILTER 3	/* only pass updates with a priority higher or
				   equal (actual value lower) to the specified
				   priority. */
#define ROUTE_FLAGFILTER 4	/* do not pass updates for routes with flags
				   in this bitmask. */

#define ROUTE_FILTER(m)	(1 << (m))
#define RTABLE_ANY	0xffffffff

#define	RTLABEL_LEN	32

struct sockaddr_rtlabel {
	u_int8_t	sr_len;			/* total length */
	sa_family_t	sr_family;		/* address family */
	char		sr_label[RTLABEL_LEN];
};

#define	RTDNS_LEN	128

struct sockaddr_rtdns {
	u_int8_t	sr_len;			/* total length */
	sa_family_t	sr_family;		/* address family */
	char		sr_dns[RTDNS_LEN];
};

#ifdef _KERNEL

static inline struct sockaddr *
srtdnstosa(struct sockaddr_rtdns *sdns)
{
	return ((struct sockaddr *)(sdns));
}

#endif

#define	RTSTATIC_LEN	128

struct sockaddr_rtstatic {
	u_int8_t	sr_len;			/* total length */
	sa_family_t	sr_family;		/* address family */
	char		sr_static[RTSTATIC_LEN];
};

#define	RTSEARCH_LEN	128

struct sockaddr_rtsearch {
	u_int8_t	sr_len;			/* total length */
	sa_family_t	sr_family;		/* address family */
	char		sr_search[RTSEARCH_LEN];
};

struct rt_addrinfo {
	int	rti_addrs;
	const	struct sockaddr *rti_info[RTAX_MAX];
	int	rti_flags;
	struct	ifaddr *rti_ifa;
	struct	rt_msghdr *rti_rtm;
	u_char	rti_mpls;
};

#ifdef __BSD_VISIBLE

#include <netinet/in.h>

/*
 * A route consists of a destination address and a reference
 * to a routing entry.  These are often held by protocols
 * in their control blocks, e.g. inpcb.
 */
struct route {
	struct	rtentry *ro_rt;
	u_long		 ro_generation;
	u_long		 ro_tableid;	/* u_long because of alignment */
	union {
		struct	sockaddr	ro_dstsa;
		struct	sockaddr_in	ro_dstsin;
		struct	sockaddr_in6	ro_dstsin6;
	};
	union {
		struct	in_addr		ro_srcin;
		struct	in6_addr	ro_srcin6;
	};
};

#endif /* __BSD_VISIBLE */

#ifdef _KERNEL

#include <sys/percpu.h>

enum rtstat_counters {
	rts_badredirect,	/* bogus redirect calls */
	rts_dynamic,		/* routes created by redirects */
	rts_newgateway,		/* routes modified by redirects */
	rts_unreach,		/* lookups which failed */
	rts_wildcard,		/* lookups satisfied by a wildcard */

	rts_ncounters
};

static inline void
rtstat_inc(enum rtstat_counters c)
{
	extern struct cpumem *rtcounters;

	counters_inc(rtcounters, c);
}

/*
 * This structure, and the prototypes for the rt_timer_{init,remove_all,
 * add,timer} functions all used with the kind permission of BSDI.
 * These allow functions to be called for routes at specific times.
 */
struct rttimer_queue {
	TAILQ_HEAD(, rttimer)		rtq_head;	/* [T] */
	LIST_ENTRY(rttimer_queue)	rtq_link;	/* [T] */
	void				(*rtq_func)	/* [I] callback */
					    (struct rtentry *, u_int);
	unsigned long			rtq_count;	/* [T] */
	int				rtq_timeout;	/* [T] */
};

const char	*rtlabel_id2name_locked(u_int16_t);
const char	*rtlabel_id2name(u_int16_t, char *, size_t);
u_int16_t	 rtlabel_name2id(const char *);
struct sockaddr	*rtlabel_id2sa(u_int16_t, struct sockaddr_rtlabel *);
void		 rtlabel_unref(u_int16_t);

/*
 * Values for additional argument to rtalloc()
 */
#define	RT_RESOLVE	1

extern struct rtstat rtstat;
extern u_long rtgeneration;

struct mbuf;
struct socket;
struct ifnet;
struct sockaddr_in6;
struct if_ieee80211_data;
struct bfd_config;

void	 route_init(void);
int	 route_cache(struct route *, const struct in_addr *,
	    const struct in_addr *, u_int);
struct rtentry *route_mpath(struct route *, const struct in_addr *,
	    const struct in_addr *, u_int);
int	 route6_cache(struct route *, const struct in6_addr *,
	    const struct in6_addr *, u_int);
struct rtentry *route6_mpath(struct route *, const struct in6_addr *,
	    const struct in6_addr *, u_int);
void	 rtm_ifchg(struct ifnet *);
void	 rtm_ifannounce(struct ifnet *, int);
void	 rtm_bfd(struct bfd_config *);
void	 rtm_80211info(struct ifnet *, struct if_ieee80211_data *);
void	 rt_maskedcopy(struct sockaddr *,
	    struct sockaddr *, struct sockaddr *);
struct sockaddr *rt_plen2mask(const struct rtentry *, struct sockaddr_in6 *);
void	 rtm_send(struct rtentry *, int, int, unsigned int);
void	 rtm_addr(int, struct ifaddr *);
void	 rtm_miss(int, struct rt_addrinfo *, int, uint8_t, u_int, int, u_int);
void	 rtm_proposal(struct ifnet *, struct rt_addrinfo *, int, uint8_t);
int	 rt_setgate(struct rtentry *, const struct sockaddr *, u_int);
struct rtentry *rt_getll(struct rtentry *);

void		rt_timer_init(void);
int		rt_timer_add(struct rtentry *,
		    struct rttimer_queue *, u_int);
void		rt_timer_remove_all(struct rtentry *);
time_t		rt_timer_get_expire(const struct rtentry *);
void		rt_timer_queue_init(struct rttimer_queue *, int,
		    void(*)(struct rtentry *, u_int));
void		rt_timer_queue_change(struct rttimer_queue *, int);
void		rt_timer_queue_flush(struct rttimer_queue *);
unsigned long	rt_timer_queue_count(struct rttimer_queue *);
void		rt_timer_timer(void *);

int	 rt_mpls_set(struct rtentry *, const struct sockaddr *, uint8_t);
void	 rt_mpls_clear(struct rtentry *);

int	 rtisvalid(struct rtentry *);
int	 rt_hash(struct rtentry *, const struct sockaddr *, uint32_t *);
struct	 rtentry *rtalloc_mpath(const struct sockaddr *, uint32_t *, u_int);
struct	 rtentry *rtalloc(const struct sockaddr *, int, unsigned int);
void	 rtref(struct rtentry *);
void	 rtfree(struct rtentry *);

int	 rt_ifa_add(struct ifaddr *, int, struct sockaddr *, unsigned int);
int	 rt_ifa_del(struct ifaddr *, int, struct sockaddr *, unsigned int);
void	 rt_ifa_purge(struct ifaddr *);
int	 rt_ifa_addlocal(struct ifaddr *);
int	 rt_ifa_dellocal(struct ifaddr *);
void	 rtredirect(struct sockaddr *, struct sockaddr *, struct sockaddr *,
	    struct rtentry **, unsigned int);
int	 rtrequest(int, struct rt_addrinfo *, u_int8_t, struct rtentry **,
	    u_int);
int	 rtrequest_delete(struct rt_addrinfo *, u_int8_t, struct ifnet *,
	    struct rtentry **, u_int);
int	 rt_if_track(struct ifnet *);
int	 rt_if_linkstate_change(struct rtentry *, void *, u_int);
int	 rtdeletemsg(struct rtentry *, struct ifnet *, u_int);
#endif /* _KERNEL */

#endif /* _NET_ROUTE_H_ */
