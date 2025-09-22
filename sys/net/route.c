/*	$OpenBSD: route.c,v 1.450 2025/09/15 13:51:24 bluhm Exp $	*/
/*	$NetBSD: route.c,v 1.14 1996/02/13 22:00:46 christos Exp $	*/

/*
 * Copyright (C) 1995, 1996, 1997, and 1998 WIDE Project.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the project nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE PROJECT AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE PROJECT OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 * Copyright (c) 1980, 1986, 1991, 1993
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
 *	@(#)route.c	8.2 (Berkeley) 11/15/93
 */

/*
 *	@(#)COPYRIGHT	1.1 (NRL) 17 January 1995
 *
 * NRL grants permission for redistribution and use in source and binary
 * forms, with or without modification, of the software and documentation
 * created at NRL provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgements:
 *	This product includes software developed by the University of
 *	California, Berkeley and its contributors.
 *	This product includes software developed at the Information
 *	Technology Division, US Naval Research Laboratory.
 * 4. Neither the name of the NRL nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THE SOFTWARE PROVIDED BY NRL IS PROVIDED BY NRL AND CONTRIBUTORS ``AS
 * IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
 * PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL NRL OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * The views and conclusions contained in the software and documentation
 * are those of the authors and should not be interpreted as representing
 * official policies, either expressed or implied, of the US Naval
 * Research Laboratory (NRL).
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/mbuf.h>
#include <sys/socket.h>
#include <sys/timeout.h>
#include <sys/domain.h>
#include <sys/queue.h>
#include <sys/pool.h>
#include <sys/atomic.h>
#include <sys/mutex.h>

#include <net/if.h>
#include <net/if_var.h>
#include <net/if_dl.h>
#include <net/route.h>

#include <netinet/in.h>
#include <netinet/ip_var.h>
#include <netinet/in_var.h>

#ifdef INET6
#include <netinet/ip6.h>
#include <netinet6/ip6_var.h>
#include <netinet6/in6_var.h>
#endif

#ifdef MPLS
#include <netmpls/mpls.h>
#endif

#ifdef BFD
#include <net/bfd.h>
#endif

/*
 * Locks used to protect struct members:
 *      a       atomic operations
 *      I       immutable after creation
 *      L       rtlabel_mtx
 *      T       rttimer_mtx
 */

#define ROUNDUP(a) (a>0 ? (1 + (((a) - 1) | (sizeof(long) - 1))) : sizeof(long))

/* Give some jitter to hash, to avoid synchronization between routers. */
static uint32_t		rt_hashjitter;

extern unsigned int	rtmap_limit;

struct cpumem	*rtcounters;
int		 rttrash;	/* [a] routes not in table but not freed */
u_long		 rtgeneration;	/* [a] generation number, routes changed */

struct pool	rtentry_pool;		/* pool for rtentry structures */
struct pool	rttimer_pool;		/* pool for rttimer structures */

int	rt_setgwroute(struct rtentry *, const struct sockaddr *, u_int);
void	rt_putgwroute(struct rtentry *, struct rtentry *);
int	rtflushclone1(struct rtentry *, void *, u_int);
int	rtflushclone(struct rtentry *, unsigned int);
int	rt_ifa_purge_walker(struct rtentry *, void *, unsigned int);
struct rtentry *rt_match(const struct sockaddr *, uint32_t *, int,
    unsigned int);
int	rt_clone(struct rtentry **, const struct sockaddr *, unsigned int);
struct sockaddr *rt_plentosa(sa_family_t, int, struct sockaddr_in6 *);
static int rt_copysa(const struct sockaddr *, const struct sockaddr *,
    struct sockaddr **);

#define	LABELID_MAX	50000

struct rt_label {
	TAILQ_ENTRY(rt_label)	rtl_entry;		/* [L] */
	char			rtl_name[RTLABEL_LEN];	/* [I] */
	u_int16_t		rtl_id;			/* [I] */
	int			rtl_ref;		/* [L] */
};

TAILQ_HEAD(rt_labels, rt_label)	rt_labels =
    TAILQ_HEAD_INITIALIZER(rt_labels);		/* [L] */
struct mutex rtlabel_mtx = MUTEX_INITIALIZER(IPL_NET);

void
route_init(void)
{
	rtcounters = counters_alloc(rts_ncounters);

	pool_init(&rtentry_pool, sizeof(struct rtentry), 0, IPL_MPFLOOR, 0,
	    "rtentry", NULL);

	while (rt_hashjitter == 0)
		rt_hashjitter = arc4random();

#ifdef BFD
	bfdinit();
#endif
}

int
route_cache(struct route *ro, const struct in_addr *dst,
    const struct in_addr *src, u_int rtableid)
{
	u_long gen;

	gen = atomic_load_long(&rtgeneration);
	membar_consumer();

	if (rtisvalid(ro->ro_rt) &&
	    ro->ro_generation == gen &&
	    ro->ro_tableid == rtableid &&
	    ro->ro_dstsa.sa_family == AF_INET &&
	    ro->ro_dstsin.sin_addr.s_addr == dst->s_addr) {
		if (src == NULL || !atomic_load_int(&ipmultipath) ||
		    !ISSET(ro->ro_rt->rt_flags, RTF_MPATH) ||
		    (ro->ro_srcin.s_addr != INADDR_ANY &&
		    ro->ro_srcin.s_addr == src->s_addr)) {
			ipstat_inc(ips_rtcachehit);
			return (0);
		}
	}

	ipstat_inc(ips_rtcachemiss);
	rtfree(ro->ro_rt);
	memset(ro, 0, sizeof(*ro));
	ro->ro_generation = gen;
	ro->ro_tableid = rtableid;

	ro->ro_dstsin.sin_family = AF_INET;
	ro->ro_dstsin.sin_len = sizeof(struct sockaddr_in);
	ro->ro_dstsin.sin_addr = *dst;
	if (src != NULL)
		ro->ro_srcin = *src;

	return (ESRCH);
}

/*
 * Check cache for route, else allocate a new one, potentially using multipath
 * to select the peer.  Update cache and return valid route or NULL.
 */
struct rtentry *
route_mpath(struct route *ro, const struct in_addr *dst,
    const struct in_addr *src, u_int rtableid)
{
	if (route_cache(ro, dst, src, rtableid)) {
		uint32_t *s = NULL;

		if (ro->ro_srcin.s_addr != INADDR_ANY)
			s = &ro->ro_srcin.s_addr;
		ro->ro_rt = rtalloc_mpath(&ro->ro_dstsa, s, ro->ro_tableid);
	}
	return (ro->ro_rt);
}

#ifdef INET6
int
route6_cache(struct route *ro, const struct in6_addr *dst,
    const struct in6_addr *src, u_int rtableid)
{
	u_long gen;

	gen = atomic_load_long(&rtgeneration);
	membar_consumer();

	if (rtisvalid(ro->ro_rt) &&
	    ro->ro_generation == gen &&
	    ro->ro_tableid == rtableid &&
	    ro->ro_dstsa.sa_family == AF_INET6 &&
	    IN6_ARE_ADDR_EQUAL(&ro->ro_dstsin6.sin6_addr, dst)) {
		if (src == NULL || !atomic_load_int(&ip6_multipath) ||
		    !ISSET(ro->ro_rt->rt_flags, RTF_MPATH) ||
		    (!IN6_IS_ADDR_UNSPECIFIED(&ro->ro_srcin6) &&
		    IN6_ARE_ADDR_EQUAL(&ro->ro_srcin6, src))) {
			ip6stat_inc(ip6s_rtcachehit);
			return (0);
		}
	}

	ip6stat_inc(ip6s_rtcachemiss);
	rtfree(ro->ro_rt);
	memset(ro, 0, sizeof(*ro));
	ro->ro_generation = gen;
	ro->ro_tableid = rtableid;

	ro->ro_dstsin6.sin6_family = AF_INET6;
	ro->ro_dstsin6.sin6_len = sizeof(struct sockaddr_in6);
	ro->ro_dstsin6.sin6_addr = *dst;
	if (src != NULL)
		ro->ro_srcin6 = *src;

	return (ESRCH);
}

struct rtentry *
route6_mpath(struct route *ro, const struct in6_addr *dst,
    const struct in6_addr *src, u_int rtableid)
{
	if (route6_cache(ro, dst, src, rtableid)) {
		uint32_t *s = NULL;

		if (!IN6_IS_ADDR_UNSPECIFIED(&ro->ro_srcin6))
			s = &ro->ro_srcin6.s6_addr32[0];
		ro->ro_rt = rtalloc_mpath(&ro->ro_dstsa, s, ro->ro_tableid);
	}
	return (ro->ro_rt);
}
#endif

/*
 * Returns 1 if the (cached) ``rt'' entry is still valid, 0 otherwise.
 */
int
rtisvalid(struct rtentry *rt)
{
	if (rt == NULL)
		return (0);

	if (!ISSET(rt->rt_flags, RTF_UP))
		return (0);

	if (ISSET(rt->rt_flags, RTF_GATEWAY)) {
		if (rt->rt_gwroute == NULL)
			return (0);
		KASSERT(!ISSET(rt->rt_gwroute->rt_flags, RTF_GATEWAY));
		if (!ISSET(rt->rt_gwroute->rt_flags, RTF_UP))
			return (0);
	}

	return (1);
}

/*
 * Do the actual lookup for rtalloc(9), do not use directly!
 *
 * Return the best matching entry for the destination ``dst''.
 *
 * "RT_RESOLVE" means that a corresponding L2 entry should
 * be added to the routing table and resolved (via ARP or
 * NDP), if it does not exist.
 */
struct rtentry *
rt_match(const struct sockaddr *dst, uint32_t *src, int flags,
    unsigned int tableid)
{
	struct rtentry		*rt = NULL;

	rt = rtable_match(tableid, dst, src);
	if (rt == NULL) {
		rtstat_inc(rts_unreach);
		return (NULL);
	}

	if (ISSET(rt->rt_flags, RTF_CLONING) && ISSET(flags, RT_RESOLVE))
		rt_clone(&rt, dst, tableid);

	rt->rt_use++;
	return (rt);
}

int
rt_clone(struct rtentry **rtp, const struct sockaddr *dst,
    unsigned int rtableid)
{
	struct rt_addrinfo	 info;
	struct rtentry		*rt = *rtp;
	int			 error = 0;

	memset(&info, 0, sizeof(info));
	info.rti_info[RTAX_DST] = dst;

	/*
	 * The priority of cloned route should be different
	 * to avoid conflict with /32 cloning routes.
	 *
	 * It should also be higher to let the ARP layer find
	 * cloned routes instead of the cloning one.
	 */
	KERNEL_LOCK();
	error = rtrequest(RTM_RESOLVE, &info, rt->rt_priority - 1, &rt,
	    rtableid);
	KERNEL_UNLOCK();
	if (error) {
		rtm_miss(RTM_MISS, &info, 0, RTP_NONE, 0, error, rtableid);
	} else {
		/* Inform listeners of the new route */
		rtm_send(rt, RTM_ADD, 0, rtableid);
		rtfree(*rtp);
		*rtp = rt;
	}
	return (error);
}

/*
 * Originated from bridge_hash() in if_bridge.c
 */
#define mix(a, b, c) do {						\
	a -= b; a -= c; a ^= (c >> 13);					\
	b -= c; b -= a; b ^= (a << 8);					\
	c -= a; c -= b; c ^= (b >> 13);					\
	a -= b; a -= c; a ^= (c >> 12);					\
	b -= c; b -= a; b ^= (a << 16);					\
	c -= a; c -= b; c ^= (b >> 5);					\
	a -= b; a -= c; a ^= (c >> 3);					\
	b -= c; b -= a; b ^= (a << 10);					\
	c -= a; c -= b; c ^= (b >> 15);					\
} while (0)

int
rt_hash(struct rtentry *rt, const struct sockaddr *dst, uint32_t *src)
{
	uint32_t a, b, c;

	if (src == NULL || !rtisvalid(rt) || !ISSET(rt->rt_flags, RTF_MPATH))
		return (-1);

	a = b = 0x9e3779b9;
	c = rt_hashjitter;

	switch (dst->sa_family) {
	case AF_INET:
	    {
		const struct sockaddr_in *sin;

		if (!atomic_load_int(&ipmultipath))
			return (-1);

		sin = satosin_const(dst);
		a += sin->sin_addr.s_addr;
		b += src[0];
		mix(a, b, c);
		break;
	    }
#ifdef INET6
	case AF_INET6:
	    {
		const struct sockaddr_in6 *sin6;

		if (!atomic_load_int(&ip6_multipath))
			return (-1);

		sin6 = satosin6_const(dst);
		a += sin6->sin6_addr.s6_addr32[0];
		b += sin6->sin6_addr.s6_addr32[2];
		c += src[0];
		mix(a, b, c);
		a += sin6->sin6_addr.s6_addr32[1];
		b += sin6->sin6_addr.s6_addr32[3];
		c += src[1];
		mix(a, b, c);
		a += sin6->sin6_addr.s6_addr32[2];
		b += sin6->sin6_addr.s6_addr32[1];
		c += src[2];
		mix(a, b, c);
		a += sin6->sin6_addr.s6_addr32[3];
		b += sin6->sin6_addr.s6_addr32[0];
		c += src[3];
		mix(a, b, c);
		break;
	    }
#endif /* INET6 */
	}

	return (c & 0xffff);
}

/*
 * Allocate a route, potentially using multipath to select the peer.
 */
struct rtentry *
rtalloc_mpath(const struct sockaddr *dst, uint32_t *src, unsigned int rtableid)
{
	return (rt_match(dst, src, RT_RESOLVE, rtableid));
}

/*
 * Look in the routing table for the best matching entry for
 * ``dst''.
 *
 * If a route with a gateway is found and its next hop is no
 * longer valid, try to cache it.
 */
struct rtentry *
rtalloc(const struct sockaddr *dst, int flags, unsigned int rtableid)
{
	return (rt_match(dst, NULL, flags, rtableid));
}

/*
 * Cache the route entry corresponding to a reachable next hop in
 * the gateway entry ``rt''.
 */
int
rt_setgwroute(struct rtentry *rt, const struct sockaddr *gate, u_int rtableid)
{
	struct rtentry *prt, *nhrt;
	unsigned int rdomain = rtable_l2(rtableid);
	int error;

	NET_ASSERT_LOCKED();

	/* If we cannot find a valid next hop bail. */
	nhrt = rt_match(gate, NULL, RT_RESOLVE, rdomain);
	if (nhrt == NULL)
		return (ENOENT);

	/* Next hop entry must be on the same interface. */
	if (nhrt->rt_ifidx != rt->rt_ifidx) {
		struct sockaddr_in6	sa_mask;

		if (!ISSET(nhrt->rt_flags, RTF_LLINFO) ||
		    !ISSET(nhrt->rt_flags, RTF_CLONED)) {
			rtfree(nhrt);
			return (EHOSTUNREACH);
		}

		/*
		 * We found a L2 entry, so we might have multiple
		 * RTF_CLONING routes for the same subnet.  Query
		 * the first route of the multipath chain and iterate
		 * until we find the correct one.
		 */
		prt = rtable_lookup(rdomain, rt_key(nhrt->rt_parent),
		    rt_plen2mask(nhrt->rt_parent, &sa_mask), NULL, RTP_ANY);
		rtfree(nhrt);

		while (prt != NULL && prt->rt_ifidx != rt->rt_ifidx)
			prt = rtable_iterate(prt);

		/* We found nothing or a non-cloning MPATH route. */
		if (prt == NULL || !ISSET(prt->rt_flags, RTF_CLONING)) {
			rtfree(prt);
			return (EHOSTUNREACH);
		}

		error = rt_clone(&prt, gate, rdomain);
		if (error) {
			rtfree(prt);
			return (error);
		}
		nhrt = prt;
	}

	/*
	 * Next hop must be reachable, this also prevents rtentry
	 * loops for example when rt->rt_gwroute points to rt.
	 */
	if (ISSET(nhrt->rt_flags, RTF_CLONING|RTF_GATEWAY)) {
		rtfree(nhrt);
		return (ENETUNREACH);
	}

	/*
	 * If the MTU of next hop is 0, this will reset the MTU of the
	 * route to run PMTUD again from scratch.
	 */
	if (!ISSET(rt->rt_locks, RTV_MTU)) {
		u_int mtu, nhmtu;

		mtu = atomic_load_int(&rt->rt_mtu);
		nhmtu = atomic_load_int(&nhrt->rt_mtu);
		if (mtu > nhmtu)
			atomic_cas_uint(&rt->rt_mtu, mtu, nhmtu);
	}

	/* commit */
	rt_putgwroute(rt, nhrt);

	return (0);
}

/*
 * Invalidate the cached route entry of the gateway entry ``rt''.
 */
void
rt_putgwroute(struct rtentry *rt, struct rtentry *nhrt)
{
	struct rtentry *onhrt;

	NET_ASSERT_LOCKED();

	if (!ISSET(rt->rt_flags, RTF_GATEWAY))
		return;

	/*
	 * To avoid reference counting problems when writing link-layer
	 * addresses in an outgoing packet, we ensure that the lifetime
	 * of a cached entry is greater than the bigger lifetime of the
	 * gateway entries it is pointed by.
	 */
	if (nhrt != NULL) {
		mtx_enter(&nhrt->rt_mtx);
		SET(nhrt->rt_flags, RTF_CACHED);
		nhrt->rt_cachecnt++;
		mtx_leave(&nhrt->rt_mtx);
	}

	onhrt = rt->rt_gwroute;
	rt->rt_gwroute = nhrt;

	if (onhrt != NULL) {
		mtx_enter(&onhrt->rt_mtx);
		KASSERT(onhrt->rt_cachecnt > 0);
		KASSERT(ISSET(onhrt->rt_flags, RTF_CACHED));
		onhrt->rt_cachecnt--;
		if (onhrt->rt_cachecnt == 0)
			CLR(onhrt->rt_flags, RTF_CACHED);
		mtx_leave(&onhrt->rt_mtx);

		rtfree(onhrt);
	}
}

void
rtref(struct rtentry *rt)
{
	refcnt_take(&rt->rt_refcnt);
}

void
rtfree(struct rtentry *rt)
{
	if (rt == NULL)
		return;

	if (refcnt_rele(&rt->rt_refcnt) == 0)
		return;

	KASSERT(!ISSET(rt->rt_flags, RTF_UP));
	KASSERT(!RT_ROOT(rt));
	atomic_dec_int(&rttrash);

	rt_timer_remove_all(rt);
	ifafree(rt->rt_ifa);
	rtlabel_unref(rt->rt_labelid);
#ifdef MPLS
	rt_mpls_clear(rt);
#endif
	if (rt->rt_gateway != NULL) {
		free(rt->rt_gateway, M_RTABLE,
		    ROUNDUP(rt->rt_gateway->sa_len));
	}
	free(rt_key(rt), M_RTABLE, rt_key(rt)->sa_len);

	pool_put(&rtentry_pool, rt);
}

struct ifaddr *
ifaref(struct ifaddr *ifa)
{
	refcnt_take(&ifa->ifa_refcnt);
	return ifa;
}

void
ifafree(struct ifaddr *ifa)
{
	if (refcnt_rele(&ifa->ifa_refcnt) == 0)
		return;
	free(ifa, M_IFADDR, 0);
}

/*
 * Force a routing table entry to the specified
 * destination to go through the given gateway.
 * Normally called as a result of a routing redirect
 * message from the network layer.
 */
void
rtredirect(struct sockaddr *dst, struct sockaddr *gateway,
    struct sockaddr *src, struct rtentry **rtp, unsigned int rdomain)
{
	struct rtentry		*rt;
	int			 error = 0;
	enum rtstat_counters	 stat = rts_ncounters;
	struct rt_addrinfo	 info;
	struct ifaddr		*ifa;
	unsigned int		 ifidx = 0;
	int			 flags = RTF_GATEWAY|RTF_HOST;
	uint8_t			 prio = RTP_NONE;

	NET_ASSERT_LOCKED();

	/* verify the gateway is directly reachable */
	rt = rtalloc(gateway, 0, rdomain);
	if (!rtisvalid(rt) || ISSET(rt->rt_flags, RTF_GATEWAY)) {
		rtfree(rt);
		error = ENETUNREACH;
		goto out;
	}
	ifidx = rt->rt_ifidx;
	ifa = rt->rt_ifa;
	rtfree(rt);
	rt = NULL;

	rt = rtable_lookup(rdomain, dst, NULL, NULL, RTP_ANY);
	/*
	 * If the redirect isn't from our current router for this dst,
	 * it's either old or wrong.  If it redirects us to ourselves,
	 * we have a routing loop, perhaps as a result of an interface
	 * going down recently.
	 */
#define	equal(a1, a2) \
	((a1)->sa_len == (a2)->sa_len && \
	 bcmp((caddr_t)(a1), (caddr_t)(a2), (a1)->sa_len) == 0)
	if (rt != NULL && (!equal(src, rt->rt_gateway) || rt->rt_ifa != ifa))
		error = EINVAL;
	else if (ifa_ifwithaddr(gateway, rdomain) != NULL ||
	    (gateway->sa_family == AF_INET &&
	    in_broadcast(satosin(gateway)->sin_addr, rdomain)))
		error = EHOSTUNREACH;
	if (error)
		goto done;
	/*
	 * Create a new entry if we just got back a wildcard entry
	 * or the lookup failed.  This is necessary for hosts
	 * which use routing redirects generated by smart gateways
	 * to dynamically build the routing tables.
	 */
	if (rt == NULL)
		goto create;
	/*
	 * Don't listen to the redirect if it's
	 * for a route to an interface.
	 */
	if (ISSET(rt->rt_flags, RTF_GATEWAY)) {
		if (!ISSET(rt->rt_flags, RTF_HOST)) {
			/*
			 * Changing from route to net => route to host.
			 * Create new route, rather than smashing route to net.
			 */
create:
			rtfree(rt);
			flags |= RTF_DYNAMIC;
			bzero(&info, sizeof(info));
			info.rti_info[RTAX_DST] = dst;
			info.rti_info[RTAX_GATEWAY] = gateway;
			info.rti_ifa = ifa;
			info.rti_flags = flags;
			rt = NULL;
			error = rtrequest(RTM_ADD, &info, RTP_DEFAULT, &rt,
			    rdomain);
			if (error == 0) {
				flags = rt->rt_flags;
				prio = rt->rt_priority;
			}
			stat = rts_dynamic;
		} else {
			/*
			 * Smash the current notion of the gateway to
			 * this destination.  Should check about netmask!!!
			 */
			rt->rt_flags |= RTF_MODIFIED;
			flags |= RTF_MODIFIED;
			prio = rt->rt_priority;
			stat = rts_newgateway;
			rt_setgate(rt, gateway, rdomain);
		}
	} else
		error = EHOSTUNREACH;
done:
	if (rt) {
		if (rtp && !error)
			*rtp = rt;
		else
			rtfree(rt);
	}
out:
	if (error)
		rtstat_inc(rts_badredirect);
	else if (stat != rts_ncounters)
		rtstat_inc(stat);
	bzero((caddr_t)&info, sizeof(info));
	info.rti_info[RTAX_DST] = dst;
	info.rti_info[RTAX_GATEWAY] = gateway;
	info.rti_info[RTAX_AUTHOR] = src;
	rtm_miss(RTM_REDIRECT, &info, flags, prio, ifidx, error, rdomain);
}

/*
 * Delete a route and generate a message
 */
int
rtdeletemsg(struct rtentry *rt, struct ifnet *ifp, u_int tableid)
{
	int			error;
	struct rt_addrinfo	info;
	struct sockaddr_rtlabel sa_rl;
	struct sockaddr_in6	sa_mask;

	KASSERT(rt->rt_ifidx == ifp->if_index);

	/*
	 * Request the new route so that the entry is not actually
	 * deleted.  That will allow the information being reported to
	 * be accurate (and consistent with route_output()).
	 */
	memset(&info, 0, sizeof(info));
	info.rti_info[RTAX_DST] = rt_key(rt);
	info.rti_info[RTAX_GATEWAY] = rt->rt_gateway;
	if (!ISSET(rt->rt_flags, RTF_HOST))
		info.rti_info[RTAX_NETMASK] = rt_plen2mask(rt, &sa_mask);
	info.rti_info[RTAX_LABEL] = rtlabel_id2sa(rt->rt_labelid, &sa_rl);
	info.rti_flags = rt->rt_flags;
	info.rti_info[RTAX_IFP] = sdltosa(ifp->if_sadl);
	info.rti_info[RTAX_IFA] = rt->rt_ifa->ifa_addr;
	KERNEL_LOCK();
	error = rtrequest_delete(&info, rt->rt_priority, ifp, &rt, tableid);
	KERNEL_UNLOCK();
	rtm_miss(RTM_DELETE, &info, info.rti_flags, rt->rt_priority,
	    rt->rt_ifidx, error, tableid);
	if (error == 0)
		rtfree(rt);
	return (error);
}

static inline int
rtequal(struct rtentry *a, struct rtentry *b)
{
	if (a == b)
		return 1;

	if (memcmp(rt_key(a), rt_key(b), rt_key(a)->sa_len) == 0 &&
	    rt_plen(a) == rt_plen(b))
		return 1;
	else
		return 0;
}

int
rtflushclone1(struct rtentry *rt, void *arg, u_int id)
{
	struct rtentry *cloningrt = arg;
	struct ifnet *ifp;

	if (!ISSET(rt->rt_flags, RTF_CLONED))
		return 0;

	/* Cached route must stay alive as long as their parent are alive. */
	if (ISSET(rt->rt_flags, RTF_CACHED) && (rt->rt_parent != cloningrt))
		return 0;

	if (!rtequal(rt->rt_parent, cloningrt))
		return 0;
	/*
	 * This happens when an interface with a RTF_CLONING route is
	 * being detached.  In this case it's safe to bail because all
	 * the routes are being purged by rt_ifa_purge().
	 */
	ifp = if_get(rt->rt_ifidx);
	if (ifp == NULL)
		return 0;

	if_put(ifp);
	return EEXIST;
}

int
rtflushclone(struct rtentry *parent, unsigned int rtableid)
{
	struct rtentry *rt = NULL;
	struct ifnet *ifp;
	int error;

#ifdef DIAGNOSTIC
	if (!parent || (parent->rt_flags & RTF_CLONING) == 0)
		panic("rtflushclone: called with a non-cloning route");
#endif

	do {
		error = rtable_walk(rtableid, rt_key(parent)->sa_family, &rt,
		    rtflushclone1, parent);
		if (rt != NULL && error == EEXIST) {
			ifp = if_get(rt->rt_ifidx);
			if (ifp == NULL) {
				error = EAGAIN;
			} else {
				error = rtdeletemsg(rt, ifp, rtableid);
				if (error == 0)
					error = EAGAIN;
				if_put(ifp);
			}
		}
		rtfree(rt);
		rt = NULL;
	} while (error == EAGAIN);

	return error;

}

int
rtrequest_delete(struct rt_addrinfo *info, u_int8_t prio, struct ifnet *ifp,
    struct rtentry **ret_nrt, u_int tableid)
{
	struct rtentry	*rt;
	int		 error;

	NET_ASSERT_LOCKED();

	if (!rtable_exists(tableid))
		return (EAFNOSUPPORT);
	rt = rtable_lookup(tableid, info->rti_info[RTAX_DST],
	    info->rti_info[RTAX_NETMASK], info->rti_info[RTAX_GATEWAY], prio);
	if (rt == NULL)
		return (ESRCH);

	/* Make sure that's the route the caller want to delete. */
	if (ifp->if_index != rt->rt_ifidx) {
		rtfree(rt);
		return (ESRCH);
	}

#ifdef BFD
	if (ISSET(rt->rt_flags, RTF_BFD))
		bfdclear(rt);
#endif

	error = rtable_delete(tableid, info->rti_info[RTAX_DST],
	    info->rti_info[RTAX_NETMASK], rt);
	if (error != 0) {
		rtfree(rt);
		return (ESRCH);
	}

	/* Release next hop cache before flushing cloned entries. */
	rt_putgwroute(rt, NULL);

	/* Clean up any cloned children. */
	if (ISSET(rt->rt_flags, RTF_CLONING))
		rtflushclone(rt, tableid);

	rtfree(rt->rt_parent);
	rt->rt_parent = NULL;

	rt->rt_flags &= ~RTF_UP;

	KASSERT(ifp->if_index == rt->rt_ifidx);
	ifp->if_rtrequest(ifp, RTM_DELETE, rt);

	atomic_inc_int(&rttrash);

	if (ret_nrt != NULL)
		*ret_nrt = rt;
	else
		rtfree(rt);

	membar_producer();
	atomic_inc_long(&rtgeneration);

	return (0);
}

int
rtrequest(int req, struct rt_addrinfo *info, u_int8_t prio,
    struct rtentry **ret_nrt, u_int tableid)
{
	struct ifnet		*ifp;
	struct rtentry		*rt, *crt;
	struct ifaddr		*ifa;
	struct sockaddr		*ndst;
	struct sockaddr_rtlabel	*sa_rl, sa_rl2;
	struct sockaddr_dl	 sa_dl = { sizeof(sa_dl), AF_LINK };
	int			 error;

	NET_ASSERT_LOCKED();

	if (!rtable_exists(tableid))
		return (EAFNOSUPPORT);
	if (info->rti_flags & RTF_HOST)
		info->rti_info[RTAX_NETMASK] = NULL;
	switch (req) {
	case RTM_DELETE:
		return (EINVAL);

	case RTM_RESOLVE:
		if (ret_nrt == NULL || (rt = *ret_nrt) == NULL)
			return (EINVAL);
		if ((rt->rt_flags & RTF_CLONING) == 0)
			return (EINVAL);
		info->rti_ifa = rt->rt_ifa;
		info->rti_flags = rt->rt_flags | (RTF_CLONED|RTF_HOST);
		info->rti_flags &= ~(RTF_CLONING|RTF_CONNECTED|RTF_STATIC);
		info->rti_info[RTAX_GATEWAY] = sdltosa(&sa_dl);
		info->rti_info[RTAX_LABEL] =
		    rtlabel_id2sa(rt->rt_labelid, &sa_rl2);
		/* FALLTHROUGH */

	case RTM_ADD:
		if (info->rti_ifa == NULL)
			return (EINVAL);
		KASSERT(info->rti_ifa->ifa_ifp != NULL);
		ifa = info->rti_ifa;
		ifp = ifa->ifa_ifp;
		if (prio == 0)
			prio = ifp->if_priority + RTP_STATIC;

		error = rt_copysa(info->rti_info[RTAX_DST],
		    info->rti_info[RTAX_NETMASK], &ndst);
		if (error)
			return (error);

		rt = pool_get(&rtentry_pool, PR_NOWAIT | PR_ZERO);
		if (rt == NULL) {
			free(ndst, M_RTABLE, ndst->sa_len);
			return (ENOBUFS);
		}

		mtx_init_flags(&rt->rt_mtx, IPL_SOFTNET, "rtentry", 0);
		refcnt_init_trace(&rt->rt_refcnt, DT_REFCNT_IDX_RTENTRY);
		rt->rt_flags = info->rti_flags | RTF_UP;
		rt->rt_priority = prio;	/* init routing priority */
		LIST_INIT(&rt->rt_timer);

		/* Check the link state if the table supports it. */
		if (rtable_mpath_capable(tableid, ndst->sa_family) &&
		    !ISSET(rt->rt_flags, RTF_LOCAL) &&
		    (!LINK_STATE_IS_UP(ifp->if_link_state) ||
		    !ISSET(ifp->if_flags, IFF_UP))) {
			rt->rt_flags &= ~RTF_UP;
			rt->rt_priority |= RTP_DOWN;
		}

		if (info->rti_info[RTAX_LABEL] != NULL) {
			sa_rl = (struct sockaddr_rtlabel *)
			    info->rti_info[RTAX_LABEL];
			rt->rt_labelid = rtlabel_name2id(sa_rl->sr_label);
		}

#ifdef MPLS
		/* We have to allocate additional space for MPLS infos */
		if (info->rti_flags & RTF_MPLS &&
		    (info->rti_info[RTAX_SRC] != NULL ||
		    info->rti_info[RTAX_DST]->sa_family == AF_MPLS)) {
			error = rt_mpls_set(rt, info->rti_info[RTAX_SRC],
			    info->rti_mpls);
			if (error) {
				free(ndst, M_RTABLE, ndst->sa_len);
				pool_put(&rtentry_pool, rt);
				return (error);
			}
		} else
			rt_mpls_clear(rt);
#endif

		rt->rt_ifa = ifaref(ifa);
		rt->rt_ifidx = ifp->if_index;
		/*
		 * Copy metrics and a back pointer from the cloned
		 * route's parent.
		 */
		if (ISSET(rt->rt_flags, RTF_CLONED)) {
			rtref(*ret_nrt);
			rt->rt_parent = *ret_nrt;
			rt->rt_rmx = (*ret_nrt)->rt_rmx;
		}

		/*
		 * We must set rt->rt_gateway before adding ``rt'' to
		 * the routing table because the radix MPATH code use
		 * it to (re)order routes.
		 */
		if ((error = rt_setgate(rt, info->rti_info[RTAX_GATEWAY],
		    tableid))) {
			ifafree(ifa);
			rtfree(rt->rt_parent);
			rt_putgwroute(rt, NULL);
			if (rt->rt_gateway != NULL) {
				free(rt->rt_gateway, M_RTABLE,
				    ROUNDUP(rt->rt_gateway->sa_len));
			}
			free(ndst, M_RTABLE, ndst->sa_len);
			pool_put(&rtentry_pool, rt);
			return (error);
		}

		error = rtable_insert(tableid, ndst,
		    info->rti_info[RTAX_NETMASK], info->rti_info[RTAX_GATEWAY],
		    rt->rt_priority, rt);
		if (error != 0 &&
		    (crt = rtable_match(tableid, ndst, NULL)) != NULL) {
			/* overwrite cloned route */
			if (ISSET(crt->rt_flags, RTF_CLONED) &&
			    !ISSET(crt->rt_flags, RTF_CACHED)) {
				struct ifnet *cifp;

				cifp = if_get(crt->rt_ifidx);
				KASSERT(cifp != NULL);
				rtdeletemsg(crt, cifp, tableid);
				if_put(cifp);

				error = rtable_insert(tableid, ndst,
				    info->rti_info[RTAX_NETMASK],
				    info->rti_info[RTAX_GATEWAY],
				    rt->rt_priority, rt);
			}
			rtfree(crt);
		}
		if (error != 0) {
			ifafree(ifa);
			rtfree(rt->rt_parent);
			rt_putgwroute(rt, NULL);
			if (rt->rt_gateway != NULL) {
				free(rt->rt_gateway, M_RTABLE,
				    ROUNDUP(rt->rt_gateway->sa_len));
			}
			free(ndst, M_RTABLE, ndst->sa_len);
			pool_put(&rtentry_pool, rt);
			return (EEXIST);
		}
		ifp->if_rtrequest(ifp, req, rt);

		if_group_routechange(info->rti_info[RTAX_DST],
			info->rti_info[RTAX_NETMASK]);

		if (ret_nrt != NULL)
			*ret_nrt = rt;
		else
			rtfree(rt);

		membar_producer();
		atomic_inc_long(&rtgeneration);

		break;
	}

	return (0);
}

int
rt_setgate(struct rtentry *rt, const struct sockaddr *gate, u_int rtableid)
{
	int glen = ROUNDUP(gate->sa_len);
	struct sockaddr *sa, *osa;
	int error = 0;

	KASSERT(gate != NULL);
	if (rt->rt_gateway == gate) {
		/* nop */
		return (0);
	}

	sa = malloc(glen, M_RTABLE, M_NOWAIT | M_ZERO);
	if (sa == NULL)
		return (ENOBUFS);
	memcpy(sa, gate, gate->sa_len);

	KERNEL_LOCK(); /* see [X] in route.h */
	osa = rt->rt_gateway;
	rt->rt_gateway = sa;

	if (ISSET(rt->rt_flags, RTF_GATEWAY))
		error = rt_setgwroute(rt, gate, rtableid);
	KERNEL_UNLOCK();

	if (osa != NULL)
		free(osa, M_RTABLE, ROUNDUP(osa->sa_len));

	return (error);
}

/*
 * Return the route entry containing the next hop link-layer
 * address corresponding to ``rt''.
 */
struct rtentry *
rt_getll(struct rtentry *rt)
{
	if (ISSET(rt->rt_flags, RTF_GATEWAY)) {
	 	/* We may return NULL here. */
		return (rt->rt_gwroute);
	}

	return (rt);
}

void
rt_maskedcopy(struct sockaddr *src, struct sockaddr *dst,
    struct sockaddr *netmask)
{
	u_char	*cp1 = (u_char *)src;
	u_char	*cp2 = (u_char *)dst;
	u_char	*cp3 = (u_char *)netmask;
	u_char	*cplim = cp2 + *cp3;
	u_char	*cplim2 = cp2 + *cp1;

	*cp2++ = *cp1++; *cp2++ = *cp1++; /* copies sa_len & sa_family */
	cp3 += 2;
	if (cplim > cplim2)
		cplim = cplim2;
	while (cp2 < cplim)
		*cp2++ = *cp1++ & *cp3++;
	if (cp2 < cplim2)
		bzero(cp2, cplim2 - cp2);
}

/*
 * allocate new sockaddr structure based on the user supplied src and mask
 * that is useable for the routing table.
 */
static int
rt_copysa(const struct sockaddr *src, const struct sockaddr *mask,
    struct sockaddr **dst)
{
	static const u_char maskarray[] = {
	    0x0, 0x80, 0xc0, 0xe0, 0xf0, 0xf8, 0xfc, 0xfe };
	struct sockaddr *ndst;
	const struct domain *dp;
	u_char *csrc, *cdst;
	int i, plen;

	for (i = 0; (dp = domains[i]) != NULL; i++) {
		if (dp->dom_rtoffset == 0)
			continue;
		if (src->sa_family == dp->dom_family)
			break;
	}
	if (dp == NULL)
		return (EAFNOSUPPORT);

	if (src->sa_len < dp->dom_sasize)
		return (EINVAL);

	plen = rtable_satoplen(src->sa_family, mask);
	if (plen == -1)
		return (EINVAL);

	ndst = malloc(dp->dom_sasize, M_RTABLE, M_NOWAIT|M_ZERO);
	if (ndst == NULL)
		return (ENOBUFS);

	ndst->sa_family = src->sa_family;
	ndst->sa_len = dp->dom_sasize;

	csrc = (u_char *)src + dp->dom_rtoffset;
	cdst = (u_char *)ndst + dp->dom_rtoffset;

	memcpy(cdst, csrc, plen / 8);
	if (plen % 8 != 0)
		cdst[plen / 8] = csrc[plen / 8] & maskarray[plen % 8];

	*dst = ndst;
	return (0);
}

int
rt_ifa_add(struct ifaddr *ifa, int flags, struct sockaddr *dst,
    unsigned int rdomain)
{
	struct ifnet		*ifp = ifa->ifa_ifp;
	struct rtentry		*rt;
	struct sockaddr_rtlabel	 sa_rl;
	struct rt_addrinfo	 info;
	uint8_t			 prio = ifp->if_priority + RTP_STATIC;
	int			 error;

	KASSERT(rdomain == rtable_l2(rdomain));

	memset(&info, 0, sizeof(info));
	info.rti_ifa = ifa;
	info.rti_flags = flags;
	info.rti_info[RTAX_DST] = dst;
	if (flags & RTF_LLINFO)
		info.rti_info[RTAX_GATEWAY] = sdltosa(ifp->if_sadl);
	else
		info.rti_info[RTAX_GATEWAY] = ifa->ifa_addr;
	info.rti_info[RTAX_LABEL] = rtlabel_id2sa(ifp->if_rtlabelid, &sa_rl);

#ifdef MPLS
	if ((flags & RTF_MPLS) == RTF_MPLS)
		info.rti_mpls = MPLS_OP_POP;
#endif /* MPLS */

	if ((flags & RTF_HOST) == 0)
		info.rti_info[RTAX_NETMASK] = ifa->ifa_netmask;

	if (flags & (RTF_LOCAL|RTF_BROADCAST))
		prio = RTP_LOCAL;

	if (flags & RTF_CONNECTED)
		prio = ifp->if_priority + RTP_CONNECTED;

	error = rtrequest(RTM_ADD, &info, prio, &rt, rdomain);
	if (error == 0) {
		/*
		 * A local route is created for every address configured
		 * on an interface, so use this information to notify
		 * userland that a new address has been added.
		 */
		if (flags & RTF_LOCAL)
			rtm_addr(RTM_NEWADDR, ifa);
		rtm_send(rt, RTM_ADD, 0, rdomain);
		rtfree(rt);
	}
	return (error);
}

int
rt_ifa_del(struct ifaddr *ifa, int flags, struct sockaddr *dst,
    unsigned int rdomain)
{
	struct ifnet		*ifp = ifa->ifa_ifp;
	struct rtentry		*rt;
	struct mbuf		*m = NULL;
	struct sockaddr		*deldst;
	struct rt_addrinfo	 info;
	struct sockaddr_rtlabel	 sa_rl;
	uint8_t			 prio = ifp->if_priority + RTP_STATIC;
	int			 error;

	KASSERT(rdomain == rtable_l2(rdomain));

	if ((flags & RTF_HOST) == 0 && ifa->ifa_netmask) {
		m = m_get(M_DONTWAIT, MT_SONAME);
		if (m == NULL)
			return (ENOBUFS);
		deldst = mtod(m, struct sockaddr *);
		rt_maskedcopy(dst, deldst, ifa->ifa_netmask);
		dst = deldst;
	}

	memset(&info, 0, sizeof(info));
	info.rti_ifa = ifa;
	info.rti_flags = flags;
	info.rti_info[RTAX_DST] = dst;
	if ((flags & RTF_LLINFO) == 0)
		info.rti_info[RTAX_GATEWAY] = ifa->ifa_addr;
	info.rti_info[RTAX_LABEL] = rtlabel_id2sa(ifp->if_rtlabelid, &sa_rl);

	if ((flags & RTF_HOST) == 0)
		info.rti_info[RTAX_NETMASK] = ifa->ifa_netmask;

	if (flags & (RTF_LOCAL|RTF_BROADCAST))
		prio = RTP_LOCAL;

	if (flags & RTF_CONNECTED)
		prio = ifp->if_priority + RTP_CONNECTED;

	rtable_clearsource(rdomain, ifa->ifa_addr);
	KERNEL_LOCK();
	error = rtrequest_delete(&info, prio, ifp, &rt, rdomain);
	KERNEL_UNLOCK();
	if (error == 0) {
		rtm_send(rt, RTM_DELETE, 0, rdomain);
		if (flags & RTF_LOCAL)
			rtm_addr(RTM_DELADDR, ifa);
		rtfree(rt);
	}
	m_free(m);

	return (error);
}

/*
 * Add ifa's address as a local rtentry.
 */
int
rt_ifa_addlocal(struct ifaddr *ifa)
{
	struct ifnet *ifp = ifa->ifa_ifp;
	struct rtentry *rt;
	u_int flags = RTF_HOST|RTF_LOCAL;
	int error = 0;

	/*
	 * If the configured address correspond to the magical "any"
	 * address do not add a local route entry because that might
	 * corrupt the routing tree which uses this value for the
	 * default routes.
	 */
	switch (ifa->ifa_addr->sa_family) {
	case AF_INET:
		if (satosin(ifa->ifa_addr)->sin_addr.s_addr == INADDR_ANY)
			return (0);
		break;
#ifdef INET6
	case AF_INET6:
		if (IN6_ARE_ADDR_EQUAL(&satosin6(ifa->ifa_addr)->sin6_addr,
		    &in6addr_any))
			return (0);
		break;
#endif
	default:
		break;
	}

	if (!ISSET(ifp->if_flags, (IFF_LOOPBACK|IFF_POINTOPOINT)))
		flags |= RTF_LLINFO;

	/* If there is no local entry, allocate one. */
	rt = rtalloc(ifa->ifa_addr, 0, ifp->if_rdomain);
	if (rt == NULL || ISSET(rt->rt_flags, flags) != flags) {
		error = rt_ifa_add(ifa, flags | RTF_MPATH, ifa->ifa_addr,
		    ifp->if_rdomain);
	}
	rtfree(rt);

	return (error);
}

/*
 * Remove local rtentry of ifa's address if it exists.
 */
int
rt_ifa_dellocal(struct ifaddr *ifa)
{
	struct ifnet *ifp = ifa->ifa_ifp;
	struct rtentry *rt;
	u_int flags = RTF_HOST|RTF_LOCAL;
	int error = 0;

	/*
	 * We do not add local routes for such address, so do not bother
	 * removing them.
	 */
	switch (ifa->ifa_addr->sa_family) {
	case AF_INET:
		if (satosin(ifa->ifa_addr)->sin_addr.s_addr == INADDR_ANY)
			return (0);
		break;
#ifdef INET6
	case AF_INET6:
		if (IN6_ARE_ADDR_EQUAL(&satosin6(ifa->ifa_addr)->sin6_addr,
		    &in6addr_any))
			return (0);
		break;
#endif
	default:
		break;
	}

	if (!ISSET(ifp->if_flags, (IFF_LOOPBACK|IFF_POINTOPOINT)))
		flags |= RTF_LLINFO;

	/*
	 * Before deleting, check if a corresponding local host
	 * route surely exists.  With this check, we can avoid to
	 * delete an interface direct route whose destination is same
	 * as the address being removed.  This can happen when removing
	 * a subnet-router anycast address on an interface attached
	 * to a shared medium.
	 */
	rt = rtalloc(ifa->ifa_addr, 0, ifp->if_rdomain);
	if (rt != NULL && ISSET(rt->rt_flags, flags) == flags) {
		error = rt_ifa_del(ifa, flags, ifa->ifa_addr,
		    ifp->if_rdomain);
	}
	rtfree(rt);

	return (error);
}

/*
 * Remove all addresses attached to ``ifa''.
 */
void
rt_ifa_purge(struct ifaddr *ifa)
{
	struct ifnet		*ifp = ifa->ifa_ifp;
	struct rtentry		*rt = NULL;
	unsigned int		 rtableid;
	int			 error, af = ifa->ifa_addr->sa_family;

	KASSERT(ifp != NULL);

	for (rtableid = 0; rtableid < rtmap_limit; rtableid++) {
		/* skip rtables that are not in the rdomain of the ifp */
		if (rtable_l2(rtableid) != ifp->if_rdomain)
			continue;

		do {
			error = rtable_walk(rtableid, af, &rt,
			    rt_ifa_purge_walker, ifa);
			if (rt != NULL && error == EEXIST) {
				error = rtdeletemsg(rt, ifp, rtableid);
				if (error == 0)
					error = EAGAIN;
			}
			rtfree(rt);
			rt = NULL;
		} while (error == EAGAIN);

		if (error == EAFNOSUPPORT)
			error = 0;

		if (error)
			break;
	}
}

int
rt_ifa_purge_walker(struct rtentry *rt, void *vifa, unsigned int rtableid)
{
	struct ifaddr		*ifa = vifa;

	if (rt->rt_ifa == ifa)
		return EEXIST;

	return 0;
}

/*
 * Route timer routines.  These routines allow functions to be called
 * for various routes at any time.  This is useful in supporting
 * path MTU discovery and redirect route deletion.
 *
 * This is similar to some BSDI internal functions, but it provides
 * for multiple queues for efficiency's sake...
 */

struct mutex			rttimer_mtx;

struct rttimer {
	TAILQ_ENTRY(rttimer)	rtt_next;	/* [T] entry on timer queue */
	LIST_ENTRY(rttimer)	rtt_link;	/* [T] timers per rtentry */
	struct timeout		rtt_timeout;	/* [I] timeout for this entry */
	struct rttimer_queue	*rtt_queue;	/* [I] back pointer to queue */
	struct rtentry		*rtt_rt;	/* [T] back pointer to route */
	time_t			rtt_expire;	/* [I] rt expire time */
	u_int			rtt_tableid;	/* [I] rtable id of rtt_rt */
};

#define RTTIMER_CALLOUT(r)	{					\
	if (r->rtt_queue->rtq_func != NULL) {				\
		(*r->rtt_queue->rtq_func)(r->rtt_rt, r->rtt_tableid);	\
	} else {							\
		struct ifnet *ifp;					\
									\
		ifp = if_get(r->rtt_rt->rt_ifidx);			\
		if (ifp != NULL &&					\
		    (r->rtt_rt->rt_flags & (RTF_DYNAMIC|RTF_HOST)) ==	\
		    (RTF_DYNAMIC|RTF_HOST))				\
			rtdeletemsg(r->rtt_rt, ifp, r->rtt_tableid);	\
		if_put(ifp);						\
	}								\
}

void
rt_timer_init(void)
{
	pool_init(&rttimer_pool, sizeof(struct rttimer), 0,
	    IPL_MPFLOOR, 0, "rttmr", NULL);
	mtx_init(&rttimer_mtx, IPL_MPFLOOR);
}

void
rt_timer_queue_init(struct rttimer_queue *rtq, int timeout,
    void (*func)(struct rtentry *, u_int))
{
	rtq->rtq_timeout = timeout;
	rtq->rtq_count = 0;
	rtq->rtq_func = func;
	TAILQ_INIT(&rtq->rtq_head);
}

void
rt_timer_queue_change(struct rttimer_queue *rtq, int timeout)
{
	mtx_enter(&rttimer_mtx);
	rtq->rtq_timeout = timeout;
	mtx_leave(&rttimer_mtx);
}

void
rt_timer_queue_flush(struct rttimer_queue *rtq)
{
	struct rttimer		*r;
	TAILQ_HEAD(, rttimer)	 rttlist;

	NET_ASSERT_LOCKED();

	TAILQ_INIT(&rttlist);
	mtx_enter(&rttimer_mtx);
	while ((r = TAILQ_FIRST(&rtq->rtq_head)) != NULL) {
		LIST_REMOVE(r, rtt_link);
		TAILQ_REMOVE(&rtq->rtq_head, r, rtt_next);
		TAILQ_INSERT_TAIL(&rttlist, r, rtt_next);
		KASSERT(rtq->rtq_count > 0);
		rtq->rtq_count--;
	}
	mtx_leave(&rttimer_mtx);

	while ((r = TAILQ_FIRST(&rttlist)) != NULL) {
		TAILQ_REMOVE(&rttlist, r, rtt_next);
		RTTIMER_CALLOUT(r);
		pool_put(&rttimer_pool, r);
	}
}

unsigned long
rt_timer_queue_count(struct rttimer_queue *rtq)
{
	return (rtq->rtq_count);
}

static inline struct rttimer *
rt_timer_unlink(struct rttimer *r)
{
	MUTEX_ASSERT_LOCKED(&rttimer_mtx);

	LIST_REMOVE(r, rtt_link);
	r->rtt_rt = NULL;

	if (timeout_del(&r->rtt_timeout) == 0) {
		/* timeout fired, so rt_timer_timer will do the cleanup */
		return NULL;
	}

	TAILQ_REMOVE(&r->rtt_queue->rtq_head, r, rtt_next);
	KASSERT(r->rtt_queue->rtq_count > 0);
	r->rtt_queue->rtq_count--;
	return r;
}

void
rt_timer_remove_all(struct rtentry *rt)
{
	struct rttimer		*r;
	TAILQ_HEAD(, rttimer)	 rttlist;

	TAILQ_INIT(&rttlist);
	mtx_enter(&rttimer_mtx);
	while ((r = LIST_FIRST(&rt->rt_timer)) != NULL) {
		r = rt_timer_unlink(r);
		if (r != NULL)
			TAILQ_INSERT_TAIL(&rttlist, r, rtt_next);
	}
	mtx_leave(&rttimer_mtx);

	while ((r = TAILQ_FIRST(&rttlist)) != NULL) {
		TAILQ_REMOVE(&rttlist, r, rtt_next);
		pool_put(&rttimer_pool, r);
	}
}

time_t
rt_timer_get_expire(const struct rtentry *rt)
{
	const struct rttimer	*r;
	time_t			 expire = 0;

	mtx_enter(&rttimer_mtx);
	LIST_FOREACH(r, &rt->rt_timer, rtt_link) {
		if (expire == 0 || expire > r->rtt_expire)
			expire = r->rtt_expire;
	}
	mtx_leave(&rttimer_mtx);

	return expire;
}

int
rt_timer_add(struct rtentry *rt, struct rttimer_queue *queue, u_int rtableid)
{
	struct rttimer	*r, *rnew;

	rnew = pool_get(&rttimer_pool, PR_NOWAIT | PR_ZERO);
	if (rnew == NULL)
		return (ENOBUFS);

	rnew->rtt_rt = rt;
	rnew->rtt_queue = queue;
	rnew->rtt_tableid = rtableid;
	rnew->rtt_expire = getuptime() + queue->rtq_timeout;
	timeout_set_proc(&rnew->rtt_timeout, rt_timer_timer, rnew);

	mtx_enter(&rttimer_mtx);
	/*
	 * If there's already a timer with this action, destroy it before
	 * we add a new one.
	 */
	LIST_FOREACH(r, &rt->rt_timer, rtt_link) {
		if (r->rtt_queue == queue) {
			r = rt_timer_unlink(r);
			break;  /* only one per list, so we can quit... */
		}
	}

	LIST_INSERT_HEAD(&rt->rt_timer, rnew, rtt_link);
	TAILQ_INSERT_TAIL(&queue->rtq_head, rnew, rtt_next);
	timeout_add_sec(&rnew->rtt_timeout, queue->rtq_timeout);
	rnew->rtt_queue->rtq_count++;
	mtx_leave(&rttimer_mtx);

	if (r != NULL)
		pool_put(&rttimer_pool, r);

	return (0);
}

void
rt_timer_timer(void *arg)
{
	struct rttimer		*r = arg;
	struct rttimer_queue	*rtq = r->rtt_queue;

	NET_LOCK();
	mtx_enter(&rttimer_mtx);

	if (r->rtt_rt != NULL)
		LIST_REMOVE(r, rtt_link);
	TAILQ_REMOVE(&rtq->rtq_head, r, rtt_next);
	KASSERT(rtq->rtq_count > 0);
	rtq->rtq_count--;

	mtx_leave(&rttimer_mtx);

	if (r->rtt_rt != NULL)
		RTTIMER_CALLOUT(r);
	NET_UNLOCK();

	pool_put(&rttimer_pool, r);
}

#ifdef MPLS
int
rt_mpls_set(struct rtentry *rt, const struct sockaddr *src, uint8_t op)
{
	struct sockaddr_mpls	*psa_mpls = (struct sockaddr_mpls *)src;
	struct rt_mpls		*rt_mpls;

	if (psa_mpls == NULL && op != MPLS_OP_POP)
		return (EOPNOTSUPP);
	if (psa_mpls != NULL && psa_mpls->smpls_len != sizeof(*psa_mpls))
		return (EINVAL);
	if (psa_mpls != NULL && psa_mpls->smpls_family != AF_MPLS)
		return (EAFNOSUPPORT);

	rt->rt_llinfo = malloc(sizeof(struct rt_mpls), M_TEMP, M_NOWAIT|M_ZERO);
	if (rt->rt_llinfo == NULL)
		return (ENOMEM);

	rt_mpls = (struct rt_mpls *)rt->rt_llinfo;
	if (psa_mpls != NULL)
		rt_mpls->mpls_label = psa_mpls->smpls_label;
	rt_mpls->mpls_operation = op;
	/* XXX: set experimental bits */
	rt->rt_flags |= RTF_MPLS;

	return (0);
}

void
rt_mpls_clear(struct rtentry *rt)
{
	if (rt->rt_llinfo != NULL && rt->rt_flags & RTF_MPLS) {
		free(rt->rt_llinfo, M_TEMP, sizeof(struct rt_mpls));
		rt->rt_llinfo = NULL;
	}
	rt->rt_flags &= ~RTF_MPLS;
}
#endif

u_int16_t
rtlabel_name2id(const char *name)
{
	struct rt_label		*label, *p;
	u_int16_t		 new_id = 1, id = 0;

	if (!name[0])
		return (0);

	mtx_enter(&rtlabel_mtx);
	TAILQ_FOREACH(label, &rt_labels, rtl_entry)
		if (strcmp(name, label->rtl_name) == 0) {
			label->rtl_ref++;
			id = label->rtl_id;
			goto out;
		}

	/*
	 * to avoid fragmentation, we do a linear search from the beginning
	 * and take the first free slot we find. if there is none or the list
	 * is empty, append a new entry at the end.
	 */
	TAILQ_FOREACH(p, &rt_labels, rtl_entry) {
		if (p->rtl_id != new_id)
			break;
		new_id = p->rtl_id + 1;
	}
	if (new_id > LABELID_MAX)
		goto out;

	label = malloc(sizeof(*label), M_RTABLE, M_NOWAIT|M_ZERO);
	if (label == NULL)
		goto out;
	strlcpy(label->rtl_name, name, sizeof(label->rtl_name));
	label->rtl_id = new_id;
	label->rtl_ref++;

	if (p != NULL)	/* insert new entry before p */
		TAILQ_INSERT_BEFORE(p, label, rtl_entry);
	else		/* either list empty or no free slot in between */
		TAILQ_INSERT_TAIL(&rt_labels, label, rtl_entry);

	id = label->rtl_id;
out:
	mtx_leave(&rtlabel_mtx);

	return (id);
}

const char *
rtlabel_id2name_locked(u_int16_t id)
{
	struct rt_label	*label;

	MUTEX_ASSERT_LOCKED(&rtlabel_mtx);

	TAILQ_FOREACH(label, &rt_labels, rtl_entry)
		if (label->rtl_id == id)
			return (label->rtl_name);

	return (NULL);
}

const char *
rtlabel_id2name(u_int16_t id, char *rtlabelbuf, size_t sz)
{
	const char *label;

	if (id == 0)
		return (NULL);

	mtx_enter(&rtlabel_mtx);
	if ((label = rtlabel_id2name_locked(id)) != NULL)
		strlcpy(rtlabelbuf, label, sz);
	mtx_leave(&rtlabel_mtx);

	if (label == NULL)
		return (NULL);

	return (rtlabelbuf);
}

struct sockaddr *
rtlabel_id2sa(u_int16_t labelid, struct sockaddr_rtlabel *sa_rl)
{
	const char	*label;

	if (labelid == 0)
		return (NULL);

	mtx_enter(&rtlabel_mtx);
	if ((label = rtlabel_id2name_locked(labelid)) != NULL) {
		bzero(sa_rl, sizeof(*sa_rl));
		sa_rl->sr_len = sizeof(*sa_rl);
		sa_rl->sr_family = AF_UNSPEC;
		strlcpy(sa_rl->sr_label, label, sizeof(sa_rl->sr_label));
	}
	mtx_leave(&rtlabel_mtx);

	if (label == NULL)
		return (NULL);

	return ((struct sockaddr *)sa_rl);
}

void
rtlabel_unref(u_int16_t id)
{
	struct rt_label	*p, *next;

	if (id == 0)
		return;

	mtx_enter(&rtlabel_mtx);
	TAILQ_FOREACH_SAFE(p, &rt_labels, rtl_entry, next) {
		if (id == p->rtl_id) {
			if (--p->rtl_ref == 0) {
				TAILQ_REMOVE(&rt_labels, p, rtl_entry);
				free(p, M_RTABLE, sizeof(*p));
			}
			break;
		}
	}
	mtx_leave(&rtlabel_mtx);
}

int
rt_if_track(struct ifnet *ifp)
{
	unsigned int rtableid;
	struct rtentry *rt = NULL;
	int i, error = 0;

	for (rtableid = 0; rtableid < rtmap_limit; rtableid++) {
		/* skip rtables that are not in the rdomain of the ifp */
		if (rtable_l2(rtableid) != ifp->if_rdomain)
			continue;
		for (i = 1; i <= AF_MAX; i++) {
			if (!rtable_mpath_capable(rtableid, i))
				continue;

			do {
				error = rtable_walk(rtableid, i, &rt,
				    rt_if_linkstate_change, ifp);
				if (rt != NULL && error == EEXIST) {
					error = rtdeletemsg(rt, ifp, rtableid);
					if (error == 0)
						error = EAGAIN;
				}
				rtfree(rt);
				rt = NULL;
			} while (error == EAGAIN);

			if (error == EAFNOSUPPORT)
				error = 0;

			if (error)
				break;
		}
	}

	return (error);
}

int
rt_if_linkstate_change(struct rtentry *rt, void *arg, u_int id)
{
	struct ifnet *ifp = arg;
	struct sockaddr_in6 sa_mask;
	int error;

	if (rt->rt_ifidx != ifp->if_index)
		return (0);

	/* Local routes are always usable. */
	if (rt->rt_flags & RTF_LOCAL) {
		rt->rt_flags |= RTF_UP;
		return (0);
	}

	if (LINK_STATE_IS_UP(ifp->if_link_state) && ifp->if_flags & IFF_UP) {
		if (ISSET(rt->rt_flags, RTF_UP))
			return (0);

		/* bring route up */
		rt->rt_flags |= RTF_UP;
		error = rtable_mpath_reprio(id, rt_key(rt), rt_plen(rt),
		    rt->rt_priority & RTP_MASK, rt);
	} else {
		/*
		 * Remove redirected and cloned routes (mainly ARP)
		 * from down interfaces so we have a chance to get
		 * new routes from a better source.
		 */
		if (ISSET(rt->rt_flags, RTF_CLONED|RTF_DYNAMIC) &&
		    !ISSET(rt->rt_flags, RTF_CACHED|RTF_BFD)) {
			return (EEXIST);
		}

		if (!ISSET(rt->rt_flags, RTF_UP))
			return (0);

		/* take route down */
		rt->rt_flags &= ~RTF_UP;
		error = rtable_mpath_reprio(id, rt_key(rt), rt_plen(rt),
		    rt->rt_priority | RTP_DOWN, rt);
	}
	if_group_routechange(rt_key(rt), rt_plen2mask(rt, &sa_mask));

	membar_producer();
	atomic_inc_long(&rtgeneration);

	return (error);
}

struct sockaddr *
rt_plentosa(sa_family_t af, int plen, struct sockaddr_in6 *sa_mask)
{
	struct sockaddr_in	*sin = (struct sockaddr_in *)sa_mask;
#ifdef INET6
	struct sockaddr_in6	*sin6 = (struct sockaddr_in6 *)sa_mask;
#endif

	KASSERT(plen >= 0 || plen == -1);

	if (plen == -1)
		return (NULL);

	memset(sa_mask, 0, sizeof(*sa_mask));

	switch (af) {
	case AF_INET:
		sin->sin_family = AF_INET;
		sin->sin_len = sizeof(struct sockaddr_in);
		in_prefixlen2mask(&sin->sin_addr, plen);
		break;
#ifdef INET6
	case AF_INET6:
		sin6->sin6_family = AF_INET6;
		sin6->sin6_len = sizeof(struct sockaddr_in6);
		in6_prefixlen2mask(&sin6->sin6_addr, plen);
		break;
#endif /* INET6 */
	default:
		return (NULL);
	}

	return ((struct sockaddr *)sa_mask);
}

struct sockaddr *
rt_plen2mask(const struct rtentry *rt, struct sockaddr_in6 *sa_mask)
{
	return (rt_plentosa(rt_key(rt)->sa_family, rt_plen(rt), sa_mask));
}

#ifdef DDB
#include <ddb/db_output.h>

void	db_print_sa(struct sockaddr *);
void	db_print_ifa(struct ifaddr *);

void
db_print_sa(struct sockaddr *sa)
{
	int len;
	u_char *p;

	if (sa == NULL) {
		db_printf("[NULL]");
		return;
	}

	p = (u_char *)sa;
	len = sa->sa_len;
	db_printf("[");
	while (len > 0) {
		db_printf("%d", *p);
		p++;
		len--;
		if (len)
			db_printf(",");
	}
	db_printf("]\n");
}

void
db_print_ifa(struct ifaddr *ifa)
{
	if (ifa == NULL)
		return;
	db_printf("  ifa_addr=");
	db_print_sa(ifa->ifa_addr);
	db_printf("  ifa_dsta=");
	db_print_sa(ifa->ifa_dstaddr);
	db_printf("  ifa_mask=");
	db_print_sa(ifa->ifa_netmask);
	db_printf("  flags=0x%x, refcnt=%u, metric=%d\n",
	    ifa->ifa_flags, ifa->ifa_refcnt.r_refs, ifa->ifa_metric);
}

/*
 * Function to pass to rtable_walk().
 * Return non-zero error to abort walk.
 */
int
db_show_rtentry(struct rtentry *rt, void *w, unsigned int id)
{
	db_printf("rtentry=%p", rt);

	db_printf(" flags=0x%x refcnt=%u use=%llu expire=%lld\n",
	    rt->rt_flags, rt->rt_refcnt.r_refs, rt->rt_use, rt->rt_expire);

	db_printf(" key="); db_print_sa(rt_key(rt));
	db_printf(" plen=%d", rt_plen(rt));
	db_printf(" gw="); db_print_sa(rt->rt_gateway);
	db_printf(" ifidx=%u ", rt->rt_ifidx);
	db_printf(" ifa=%p\n", rt->rt_ifa);
	db_print_ifa(rt->rt_ifa);

	db_printf(" gwroute=%p llinfo=%p priority=%d\n",
	    rt->rt_gwroute, rt->rt_llinfo, rt->rt_priority);
	return (0);
}

/*
 * Function to print all the route trees.
 */
int
db_show_rtable(int af, unsigned int rtableid)
{
	db_printf("Route tree for af %d, rtableid %u\n", af, rtableid);
	rtable_walk(rtableid, af, NULL, db_show_rtentry, NULL);
	return (0);
}
#endif /* DDB */
