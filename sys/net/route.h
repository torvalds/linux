/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
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
 *	@(#)route.h	8.4 (Berkeley) 1/9/95
 * $FreeBSD$
 */

#ifndef _NET_ROUTE_H_
#define _NET_ROUTE_H_

#include <sys/counter.h>
#include <net/vnet.h>

/*
 * Kernel resident routing tables.
 *
 * The routing tables are initialized when interface addresses
 * are set by making entries for all directly connected interfaces.
 */

/*
 * Struct route consiste of a destination address,
 * a route entry pointer, link-layer prepend data pointer along
 * with its length.
 */
struct route {
	struct	rtentry *ro_rt;
	struct	llentry *ro_lle;
	/*
	 * ro_prepend and ro_plen are only used for bpf to pass in a
	 * preformed header.  They are not cacheable.
	 */
	char		*ro_prepend;
	uint16_t	ro_plen;
	uint16_t	ro_flags;
	uint16_t	ro_mtu;	/* saved ro_rt mtu */
	uint16_t	spare;
	struct	sockaddr ro_dst;
};

#define	RT_L2_ME_BIT		2	/* dst L2 addr is our address */
#define	RT_MAY_LOOP_BIT		3	/* dst may require loop copy */
#define	RT_HAS_HEADER_BIT	4	/* mbuf already have its header prepended */

#define	RT_L2_ME		(1 << RT_L2_ME_BIT)		/* 0x0004 */
#define	RT_MAY_LOOP		(1 << RT_MAY_LOOP_BIT)		/* 0x0008 */
#define	RT_HAS_HEADER		(1 << RT_HAS_HEADER_BIT)	/* 0x0010 */

#define	RT_REJECT		0x0020		/* Destination is reject */
#define	RT_BLACKHOLE		0x0040		/* Destination is blackhole */
#define	RT_HAS_GW		0x0080		/* Destination has GW  */
#define	RT_LLE_CACHE		0x0100		/* Cache link layer  */

struct rt_metrics {
	u_long	rmx_locks;	/* Kernel must leave these values alone */
	u_long	rmx_mtu;	/* MTU for this path */
	u_long	rmx_hopcount;	/* max hops expected */
	u_long	rmx_expire;	/* lifetime for route, e.g. redirect */
	u_long	rmx_recvpipe;	/* inbound delay-bandwidth product */
	u_long	rmx_sendpipe;	/* outbound delay-bandwidth product */
	u_long	rmx_ssthresh;	/* outbound gateway buffer limit */
	u_long	rmx_rtt;	/* estimated round trip time */
	u_long	rmx_rttvar;	/* estimated rtt variance */
	u_long	rmx_pksent;	/* packets sent using this route */
	u_long	rmx_weight;	/* route weight */
	u_long	rmx_filler[3];	/* will be used for T/TCP later */
};

/*
 * rmx_rtt and rmx_rttvar are stored as microseconds;
 * RTTTOPRHZ(rtt) converts to a value suitable for use
 * by a protocol slowtimo counter.
 */
#define	RTM_RTTUNIT	1000000	/* units for rtt, rttvar, as units per sec */
#define	RTTTOPRHZ(r)	((r) / (RTM_RTTUNIT / PR_SLOWHZ))

/* lle state is exported in rmx_state rt_metrics field */
#define	rmx_state	rmx_weight

/*
 * Keep a generation count of routing table, incremented on route addition,
 * so we can invalidate caches.  This is accessed without a lock, as precision
 * is not required.
 */
typedef volatile u_int rt_gen_t;	/* tree generation (for adds) */
#define RT_GEN(fibnum, af)	rt_tables_get_gen(fibnum, af)

#define	RT_DEFAULT_FIB	0	/* Explicitly mark fib=0 restricted cases */
#define	RT_ALL_FIBS	-1	/* Announce event for every fib */
#ifdef _KERNEL
extern u_int rt_numfibs;	/* number of usable routing tables */
VNET_DECLARE(u_int, rt_add_addr_allfibs); /* Announce interfaces to all fibs */
#define	V_rt_add_addr_allfibs	VNET(rt_add_addr_allfibs)
#endif

/*
 * We distinguish between routes to hosts and routes to networks,
 * preferring the former if available.  For each route we infer
 * the interface to use from the gateway address supplied when
 * the route was entered.  Routes that forward packets through
 * gateways are marked so that the output routines know to address the
 * gateway rather than the ultimate destination.
 */
#ifndef RNF_NORMAL
#include <net/radix.h>
#ifdef RADIX_MPATH
#include <net/radix_mpath.h>
#endif
#endif

#if defined(_KERNEL)
struct rtentry {
	struct	radix_node rt_nodes[2];	/* tree glue, and other values */
	/*
	 * XXX struct rtentry must begin with a struct radix_node (or two!)
	 * because the code does some casts of a 'struct radix_node *'
	 * to a 'struct rtentry *'
	 */
#define	rt_key(r)	(*((struct sockaddr **)(&(r)->rt_nodes->rn_key)))
#define	rt_mask(r)	(*((struct sockaddr **)(&(r)->rt_nodes->rn_mask)))
	struct	sockaddr *rt_gateway;	/* value */
	struct	ifnet *rt_ifp;		/* the answer: interface to use */
	struct	ifaddr *rt_ifa;		/* the answer: interface address to use */
	int		rt_flags;	/* up/down?, host/net */
	int		rt_refcnt;	/* # held references */
	u_int		rt_fibnum;	/* which FIB */
	u_long		rt_mtu;		/* MTU for this path */
	u_long		rt_weight;	/* absolute weight */ 
	u_long		rt_expire;	/* lifetime for route, e.g. redirect */
#define	rt_endzero	rt_pksent
	counter_u64_t	rt_pksent;	/* packets sent using this route */
	struct mtx	rt_mtx;		/* mutex for routing entry */
	struct rtentry	*rt_chain;	/* pointer to next rtentry to delete */
};
#endif /* _KERNEL */

#define	RTF_UP		0x1		/* route usable */
#define	RTF_GATEWAY	0x2		/* destination is a gateway */
#define	RTF_HOST	0x4		/* host entry (net otherwise) */
#define	RTF_REJECT	0x8		/* host or net unreachable */
#define	RTF_DYNAMIC	0x10		/* created dynamically (by redirect) */
#define	RTF_MODIFIED	0x20		/* modified dynamically (by redirect) */
#define RTF_DONE	0x40		/* message confirmed */
/*			0x80		   unused, was RTF_DELCLONE */
/*			0x100		   unused, was RTF_CLONING */
#define RTF_XRESOLVE	0x200		/* external daemon resolves name */
#define RTF_LLINFO	0x400		/* DEPRECATED - exists ONLY for backward 
					   compatibility */
#define RTF_LLDATA	0x400		/* used by apps to add/del L2 entries */
#define RTF_STATIC	0x800		/* manually added */
#define RTF_BLACKHOLE	0x1000		/* just discard pkts (during updates) */
#define RTF_PROTO2	0x4000		/* protocol specific routing flag */
#define RTF_PROTO1	0x8000		/* protocol specific routing flag */
/*			0x10000		   unused, was RTF_PRCLONING */
/*			0x20000		   unused, was RTF_WASCLONED */
#define RTF_PROTO3	0x40000		/* protocol specific routing flag */
#define	RTF_FIXEDMTU	0x80000		/* MTU was explicitly specified */
#define RTF_PINNED	0x100000	/* route is immutable */
#define	RTF_LOCAL	0x200000 	/* route represents a local address */
#define	RTF_BROADCAST	0x400000	/* route represents a bcast address */
#define	RTF_MULTICAST	0x800000	/* route represents a mcast address */
					/* 0x8000000 and up unassigned */
#define	RTF_STICKY	 0x10000000	/* always route dst->src */

#define	RTF_RNH_LOCKED	 0x40000000	/* radix node head is locked */

#define	RTF_GWFLAG_COMPAT 0x80000000	/* a compatibility bit for interacting
					   with existing routing apps */

/* Mask of RTF flags that are allowed to be modified by RTM_CHANGE. */
#define RTF_FMASK	\
	(RTF_PROTO1 | RTF_PROTO2 | RTF_PROTO3 | RTF_BLACKHOLE | \
	 RTF_REJECT | RTF_STATIC | RTF_STICKY)

/*
 * fib_ nexthop API flags.
 */

/* Consumer-visible nexthop info flags */
#define	NHF_REJECT		0x0010	/* RTF_REJECT */
#define	NHF_BLACKHOLE		0x0020	/* RTF_BLACKHOLE */
#define	NHF_REDIRECT		0x0040	/* RTF_DYNAMIC|RTF_MODIFIED */
#define	NHF_DEFAULT		0x0080	/* Default route */
#define	NHF_BROADCAST		0x0100	/* RTF_BROADCAST */
#define	NHF_GATEWAY		0x0200	/* RTF_GATEWAY */

/* Nexthop request flags */
#define	NHR_IFAIF		0x01	/* Return ifa_ifp interface */
#define	NHR_REF			0x02	/* For future use */

/* Control plane route request flags */
#define	NHR_COPY		0x100	/* Copy rte data */

#ifdef _KERNEL
/* rte<>ro_flags translation */
static inline void
rt_update_ro_flags(struct route *ro)
{
	int rt_flags = ro->ro_rt->rt_flags;

	ro->ro_flags &= ~ (RT_REJECT|RT_BLACKHOLE|RT_HAS_GW);

	ro->ro_flags |= (rt_flags & RTF_REJECT) ? RT_REJECT : 0;
	ro->ro_flags |= (rt_flags & RTF_BLACKHOLE) ? RT_BLACKHOLE : 0;
	ro->ro_flags |= (rt_flags & RTF_GATEWAY) ? RT_HAS_GW : 0;
}
#endif

/*
 * Routing statistics.
 */
struct	rtstat {
	short	rts_badredirect;	/* bogus redirect calls */
	short	rts_dynamic;		/* routes created by redirects */
	short	rts_newgateway;		/* routes modified by redirects */
	short	rts_unreach;		/* lookups which failed */
	short	rts_wildcard;		/* lookups satisfied by a wildcard */
};
/*
 * Structures for routing messages.
 */
struct rt_msghdr {
	u_short	rtm_msglen;	/* to skip over non-understood messages */
	u_char	rtm_version;	/* future binary compatibility */
	u_char	rtm_type;	/* message type */
	u_short	rtm_index;	/* index for associated ifp */
	u_short _rtm_spare1;
	int	rtm_flags;	/* flags, incl. kern & message, e.g. DONE */
	int	rtm_addrs;	/* bitmask identifying sockaddrs in msg */
	pid_t	rtm_pid;	/* identify sender */
	int	rtm_seq;	/* for sender to identify action */
	int	rtm_errno;	/* why failed */
	int	rtm_fmask;	/* bitmask used in RTM_CHANGE message */
	u_long	rtm_inits;	/* which metrics we are initializing */
	struct	rt_metrics rtm_rmx; /* metrics themselves */
};

#define RTM_VERSION	5	/* Up the ante and ignore older versions */

/*
 * Message types.
 *
 * The format for each message is annotated below using the following
 * identifiers:
 *
 * (1) struct rt_msghdr
 * (2) struct ifa_msghdr
 * (3) struct if_msghdr
 * (4) struct ifma_msghdr
 * (5) struct if_announcemsghdr
 *
 */
#define	RTM_ADD		0x1	/* (1) Add Route */
#define	RTM_DELETE	0x2	/* (1) Delete Route */
#define	RTM_CHANGE	0x3	/* (1) Change Metrics or flags */
#define	RTM_GET		0x4	/* (1) Report Metrics */
#define	RTM_LOSING	0x5	/* (1) Kernel Suspects Partitioning */
#define	RTM_REDIRECT	0x6	/* (1) Told to use different route */
#define	RTM_MISS	0x7	/* (1) Lookup failed on this address */
#define	RTM_LOCK	0x8	/* (1) fix specified metrics */
		    /*	0x9  */
		    /*	0xa  */
#define	RTM_RESOLVE	0xb	/* (1) req to resolve dst to LL addr */
#define	RTM_NEWADDR	0xc	/* (2) address being added to iface */
#define	RTM_DELADDR	0xd	/* (2) address being removed from iface */
#define	RTM_IFINFO	0xe	/* (3) iface going up/down etc. */
#define	RTM_NEWMADDR	0xf	/* (4) mcast group membership being added to if */
#define	RTM_DELMADDR	0x10	/* (4) mcast group membership being deleted */
#define	RTM_IFANNOUNCE	0x11	/* (5) iface arrival/departure */
#define	RTM_IEEE80211	0x12	/* (5) IEEE80211 wireless event */

/*
 * Bitmask values for rtm_inits and rmx_locks.
 */
#define RTV_MTU		0x1	/* init or lock _mtu */
#define RTV_HOPCOUNT	0x2	/* init or lock _hopcount */
#define RTV_EXPIRE	0x4	/* init or lock _expire */
#define RTV_RPIPE	0x8	/* init or lock _recvpipe */
#define RTV_SPIPE	0x10	/* init or lock _sendpipe */
#define RTV_SSTHRESH	0x20	/* init or lock _ssthresh */
#define RTV_RTT		0x40	/* init or lock _rtt */
#define RTV_RTTVAR	0x80	/* init or lock _rttvar */
#define RTV_WEIGHT	0x100	/* init or lock _weight */

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
#define RTAX_MAX	8	/* size of array to allocate */

typedef int rt_filter_f_t(const struct rtentry *, void *);

struct rt_addrinfo {
	int	rti_addrs;			/* Route RTF_ flags */
	int	rti_flags;			/* Route RTF_ flags */
	struct	sockaddr *rti_info[RTAX_MAX];	/* Sockaddr data */
	struct	ifaddr *rti_ifa;		/* value of rt_ifa addr */
	struct	ifnet *rti_ifp;			/* route interface */
	rt_filter_f_t	*rti_filter;		/* filter function */
	void	*rti_filterdata;		/* filter paramenters */
	u_long	rti_mflags;			/* metrics RTV_ flags */
	u_long	rti_spare;			/* Will be used for fib */
	struct	rt_metrics *rti_rmx;		/* Pointer to route metrics */
};

/*
 * This macro returns the size of a struct sockaddr when passed
 * through a routing socket. Basically we round up sa_len to
 * a multiple of sizeof(long), with a minimum of sizeof(long).
 * The case sa_len == 0 should only apply to empty structures.
 */
#define SA_SIZE(sa)						\
    (  (((struct sockaddr *)(sa))->sa_len == 0) ?		\
	sizeof(long)		:				\
	1 + ( (((struct sockaddr *)(sa))->sa_len - 1) | (sizeof(long) - 1) ) )

#define	sa_equal(a, b) (	\
    (((const struct sockaddr *)(a))->sa_len == ((const struct sockaddr *)(b))->sa_len) && \
    (bcmp((a), (b), ((const struct sockaddr *)(b))->sa_len) == 0))

#ifdef _KERNEL

#define RT_LINK_IS_UP(ifp)	(!((ifp)->if_capabilities & IFCAP_LINKSTATE) \
				 || (ifp)->if_link_state == LINK_STATE_UP)

#define	RT_LOCK_INIT(_rt) \
	mtx_init(&(_rt)->rt_mtx, "rtentry", NULL, MTX_DEF | MTX_DUPOK | MTX_NEW)
#define	RT_LOCK(_rt)		mtx_lock(&(_rt)->rt_mtx)
#define	RT_UNLOCK(_rt)		mtx_unlock(&(_rt)->rt_mtx)
#define	RT_LOCK_DESTROY(_rt)	mtx_destroy(&(_rt)->rt_mtx)
#define	RT_LOCK_ASSERT(_rt)	mtx_assert(&(_rt)->rt_mtx, MA_OWNED)
#define	RT_UNLOCK_COND(_rt)	do {				\
	if (mtx_owned(&(_rt)->rt_mtx))				\
		mtx_unlock(&(_rt)->rt_mtx);			\
} while (0)

#define	RT_ADDREF(_rt)	do {					\
	RT_LOCK_ASSERT(_rt);					\
	KASSERT((_rt)->rt_refcnt >= 0,				\
		("negative refcnt %d", (_rt)->rt_refcnt));	\
	(_rt)->rt_refcnt++;					\
} while (0)

#define	RT_REMREF(_rt)	do {					\
	RT_LOCK_ASSERT(_rt);					\
	KASSERT((_rt)->rt_refcnt > 0,				\
		("bogus refcnt %d", (_rt)->rt_refcnt));	\
	(_rt)->rt_refcnt--;					\
} while (0)

#define	RTFREE_LOCKED(_rt) do {					\
	if ((_rt)->rt_refcnt <= 1)				\
		rtfree(_rt);					\
	else {							\
		RT_REMREF(_rt);					\
		RT_UNLOCK(_rt);					\
	}							\
	/* guard against invalid refs */			\
	_rt = 0;						\
} while (0)

#define	RTFREE(_rt) do {					\
	RT_LOCK(_rt);						\
	RTFREE_LOCKED(_rt);					\
} while (0)

#define	RO_RTFREE(_ro) do {					\
	if ((_ro)->ro_rt)					\
		RTFREE((_ro)->ro_rt);				\
} while (0)

#define	RO_INVALIDATE_CACHE(ro) do {					\
		RO_RTFREE(ro);						\
		if ((ro)->ro_lle != NULL) {				\
			LLE_FREE((ro)->ro_lle);				\
			(ro)->ro_lle = NULL;				\
		}							\
	} while (0)

/*
 * Validate a cached route based on a supplied cookie.  If there is an
 * out-of-date cache, simply free it.  Update the generation number
 * for the new allocation
 */
#define RT_VALIDATE(ro, cookiep, fibnum) do {				\
	rt_gen_t cookie = RT_GEN(fibnum, (ro)->ro_dst.sa_family);	\
	if (*(cookiep) != cookie) {					\
		RO_INVALIDATE_CACHE(ro);				\
		*(cookiep) = cookie;					\
	}								\
} while (0)

struct ifmultiaddr;
struct rib_head;

void	 rt_ieee80211msg(struct ifnet *, int, void *, size_t);
void	 rt_ifannouncemsg(struct ifnet *, int);
void	 rt_ifmsg(struct ifnet *);
void	 rt_missmsg(int, struct rt_addrinfo *, int, int);
void	 rt_missmsg_fib(int, struct rt_addrinfo *, int, int, int);
void	 rt_newaddrmsg(int, struct ifaddr *, int, struct rtentry *);
void	 rt_newaddrmsg_fib(int, struct ifaddr *, int, struct rtentry *, int);
int	 rt_addrmsg(int, struct ifaddr *, int);
int	 rt_routemsg(int, struct ifnet *ifp, int, struct rtentry *, int);
void	 rt_newmaddrmsg(int, struct ifmultiaddr *);
int	 rt_setgate(struct rtentry *, struct sockaddr *, struct sockaddr *);
void 	 rt_maskedcopy(struct sockaddr *, struct sockaddr *, struct sockaddr *);
struct rib_head *rt_table_init(int);
void	rt_table_destroy(struct rib_head *);
u_int	rt_tables_get_gen(int table, int fam);

int	rtsock_addrmsg(int, struct ifaddr *, int);
int	rtsock_routemsg(int, struct ifnet *ifp, int, struct rtentry *, int);

/*
 * Note the following locking behavior:
 *
 *    rtalloc1() returns a locked rtentry
 *
 *    rtfree() and RTFREE_LOCKED() require a locked rtentry
 *
 *    RTFREE() uses an unlocked entry.
 */

void	 rtfree(struct rtentry *);
void	rt_updatemtu(struct ifnet *);

typedef int rt_walktree_f_t(struct rtentry *, void *);
typedef void rt_setwarg_t(struct rib_head *, uint32_t, int, void *);
void	rt_foreach_fib_walk(int af, rt_setwarg_t *, rt_walktree_f_t *, void *);
void	rt_foreach_fib_walk_del(int af, rt_filter_f_t *filter_f, void *arg);
void	rt_flushifroutes_af(struct ifnet *, int);
void	rt_flushifroutes(struct ifnet *ifp);

/* XXX MRT COMPAT VERSIONS THAT SET UNIVERSE to 0 */
/* Thes are used by old code not yet converted to use multiple FIBS */
struct rtentry *rtalloc1(struct sockaddr *, int, u_long);
int	 rtinit(struct ifaddr *, int, int);

/* XXX MRT NEW VERSIONS THAT USE FIBs
 * For now the protocol indepedent versions are the same as the AF_INET ones
 * but this will change.. 
 */
int	 rt_getifa_fib(struct rt_addrinfo *, u_int fibnum);
void	 rtalloc_ign_fib(struct route *ro, u_long ignflags, u_int fibnum);
struct rtentry *rtalloc1_fib(struct sockaddr *, int, u_long, u_int);
int	 rtioctl_fib(u_long, caddr_t, u_int);
void	 rtredirect_fib(struct sockaddr *, struct sockaddr *,
	    struct sockaddr *, int, struct sockaddr *, u_int);
int	 rtrequest_fib(int, struct sockaddr *,
	    struct sockaddr *, struct sockaddr *, int, struct rtentry **, u_int);
int	 rtrequest1_fib(int, struct rt_addrinfo *, struct rtentry **, u_int);
int	rib_lookup_info(uint32_t, const struct sockaddr *, uint32_t, uint32_t,
	    struct rt_addrinfo *);
void	rib_free_info(struct rt_addrinfo *info);

#endif

#endif
