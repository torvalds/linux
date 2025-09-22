/*	$OpenBSD: nd6.c,v 1.303 2025/09/16 09:19:43 florian Exp $	*/
/*	$KAME: nd6.c,v 1.280 2002/06/08 19:52:07 itojun Exp $	*/

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

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/timeout.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/socket.h>
#include <sys/sockio.h>
#include <sys/time.h>
#include <sys/pool.h>
#include <sys/errno.h>
#include <sys/ioctl.h>
#include <sys/syslog.h>
#include <sys/queue.h>
#include <sys/stdint.h>
#include <sys/task.h>

#include <net/if.h>
#include <net/if_dl.h>
#include <net/if_types.h>
#include <net/route.h>

#include <netinet/in.h>
#include <netinet/if_ether.h>

#include <netinet6/in6_var.h>
#include <netinet/ip6.h>
#include <netinet6/ip6_var.h>
#include <netinet6/nd6.h>
#include <netinet/icmp6.h>

/*
 * Locks used to protect struct members in this file:
 *	a	atomic operations
 *	I	immutable after creation
 *	K	kernel lock
 *	m	nd6 mutex, needed when net lock is shared
 *	N	net lock
 */

#define ND6_SLOWTIMER_INTERVAL (60 * 60) /* 1 hour */
#define ND6_RECALC_REACHTM_INTERVAL (60 * 120) /* 2 hours */

/* timer values */
time_t	nd6_timer_next	= -1;	/* [N] at which uptime nd6_timer runs */
time_t	nd6_expire_next	= -1;	/* at which uptime nd6_expire runs */
int	nd6_delay	= 5;	/* [a] delay first probe time 5 second */
int	nd6_umaxtries	= 3;	/* [a] maximum unicast query */
int	nd6_mmaxtries	= 3;	/* [a] maximum multicast query */
const int nd6_gctimer	= (60 * 60 * 24); /* 1 day: garbage collection timer */

/* preventing too many loops in ND option parsing */
int nd6_maxndopt = 10;	/* max # of ND options allowed */

/* llinfo_nd6 live time, rt_llinfo and RTF_LLINFO are protected by nd6_mtx */
struct mutex nd6_mtx = MUTEX_INITIALIZER(IPL_SOFTNET);

TAILQ_HEAD(llinfo_nd6_head, llinfo_nd6) nd6_list =
    TAILQ_HEAD_INITIALIZER(nd6_list);
				/* [m] list of llinfo_nd6 structures */
struct	pool nd6_pool;		/* [I] pool for llinfo_nd6 structures */
int	nd6_inuse;		/* [m] limit neighbor discovery routes */
unsigned int	ln_hold_total;	/* [a] packets currently in the nd6 queue */

void nd6_timer(void *);
void nd6_slowtimo(void *);
void nd6_expire(void *);
void nd6_expire_timer(void *);
void nd6_invalidate(struct rtentry *);
void nd6_free(struct rtentry *, struct ifnet *ifp, int);
int nd6_llinfo_timer(struct rtentry *, int);

struct timeout nd6_timer_to;
struct timeout nd6_slowtimo_ch;
struct timeout nd6_expire_timeout;
struct task nd6_expire_task;

void
nd6_init(void)
{
	pool_init(&nd6_pool, sizeof(struct llinfo_nd6), 0,
	    IPL_SOFTNET, 0, "nd6", NULL);

	task_set(&nd6_expire_task, nd6_expire, NULL);

	/* start timer */
	timeout_set_proc(&nd6_timer_to, nd6_timer, NULL);
	timeout_set_proc(&nd6_slowtimo_ch, nd6_slowtimo, NULL);
	timeout_add_sec(&nd6_slowtimo_ch, ND6_SLOWTIMER_INTERVAL);
	timeout_set(&nd6_expire_timeout, nd6_expire_timer, NULL);
}

void
nd6_ifattach(struct ifnet *ifp)
{
	struct nd_ifinfo *nd;

	nd = malloc(sizeof(*nd), M_IP6NDP, M_WAITOK | M_ZERO);

	nd->reachable = ND_COMPUTE_RTIME(REACHABLE_TIME);

	ifp->if_nd = nd;
}

void
nd6_ifdetach(struct ifnet *ifp)
{
	struct nd_ifinfo *nd = ifp->if_nd;

	free(nd, M_IP6NDP, sizeof(*nd));
}

/*
 * Parse multiple ND options.
 * This function is much easier to use, for ND routines that do not need
 * multiple options of the same type.
 */
int
nd6_options(void *opt, int icmp6len, struct nd_opts *ndopts)
{
	struct nd_opt_hdr *nd_opt, *next_opt, *last_opt;
	int i = 0;

	bzero(ndopts, sizeof(*ndopts));

	if (icmp6len == 0)
		return 0;

	next_opt = opt;
	last_opt = (struct nd_opt_hdr *)((u_char *)opt + icmp6len);

	while (next_opt != NULL) {
		int olen;

		nd_opt = next_opt;

		/* make sure nd_opt_len is inside the buffer */
		if ((caddr_t)&nd_opt->nd_opt_len >= (caddr_t)last_opt)
			goto invalid;

		/* every option must have a length greater than zero */
		olen = nd_opt->nd_opt_len << 3;
		if (olen == 0)
			goto invalid;

		next_opt = (struct nd_opt_hdr *)((caddr_t)nd_opt + olen);
		if (next_opt > last_opt) {
			/* option overruns the end of buffer */
			goto invalid;
		} else if (next_opt == last_opt) {
			/* reached the end of options chain */
			next_opt = NULL;
		}

		switch (nd_opt->nd_opt_type) {
		case ND_OPT_SOURCE_LINKADDR:
			if (ndopts->nd_opts_src_lladdr == NULL)
				ndopts->nd_opts_src_lladdr = nd_opt;
			break;
		case ND_OPT_TARGET_LINKADDR:
			if (ndopts->nd_opts_tgt_lladdr == NULL)
				ndopts->nd_opts_tgt_lladdr = nd_opt;
			break;
		case ND_OPT_MTU:
		case ND_OPT_REDIRECTED_HEADER:
		case ND_OPT_PREFIX_INFORMATION:
		case ND_OPT_DNSSL:
		case ND_OPT_RDNSS:
			/* Don't warn, not used by kernel */
			break;
		default:
			/*
			 * Unknown options must be silently ignored,
			 * to accommodate future extension to the protocol.
			 */
			break;
		}

		i++;
		if (i > nd6_maxndopt) {
			icmp6stat_inc(icp6s_nd_toomanyopt);
			break;
		}
	}

	return 0;

invalid:
	bzero(ndopts, sizeof(*ndopts));
	icmp6stat_inc(icp6s_nd_badopt);
	return -1;
}

/*
 * ND6 timer routine to handle ND6 entries
 */
void
nd6_llinfo_settimer(const struct llinfo_nd6 *ln, unsigned int secs)
{
	time_t expire = getuptime() + secs;

	NET_ASSERT_LOCKED();
	KASSERT(!ISSET(ln->ln_rt->rt_flags, RTF_LOCAL));

	ln->ln_rt->rt_expire = expire;
	if (!timeout_pending(&nd6_timer_to) || expire < nd6_timer_next) {
		nd6_timer_next = expire;
		timeout_add_sec(&nd6_timer_to, secs);
	}
}

static struct llinfo_nd6 *
nd6_iterator(struct llinfo_nd6 *ln, struct llinfo_nd6_iterator *iter)
{
	struct llinfo_nd6 *tmp;

	MUTEX_ASSERT_LOCKED(&nd6_mtx);

	if (ln)
		tmp = TAILQ_NEXT((struct llinfo_nd6 *)iter, ln_list);
	else
		tmp = TAILQ_FIRST(&nd6_list);

	while (tmp && tmp->ln_rt == NULL)
		tmp = TAILQ_NEXT(tmp, ln_list);

	if (ln) {
		TAILQ_REMOVE(&nd6_list, (struct llinfo_nd6 *)iter, ln_list);
		if (refcnt_rele(&ln->ln_refcnt))
			pool_put(&nd6_pool, ln);
	}
	if (tmp) {
		TAILQ_INSERT_AFTER(&nd6_list, tmp, (struct llinfo_nd6 *)iter,
		    ln_list);
		refcnt_take(&tmp->ln_refcnt);
	}

	return tmp;
}

void
nd6_timer(void *unused)
{
	struct llinfo_nd6_iterator iter = { .ln_rt = NULL };
	struct llinfo_nd6 *ln = NULL;
	time_t uptime, expire;
	int i_am_router = (atomic_load_int(&ip6_forwarding) != 0);
	int secs;

	uptime = getuptime();
	expire = uptime + nd6_gctimer;

	mtx_enter(&nd6_mtx);
	while ((ln = nd6_iterator(ln, &iter)) != NULL) {
		struct rtentry *rt = ln->ln_rt;

		if (rt->rt_expire && rt->rt_expire <= uptime) {
			rtref(rt);
			mtx_leave(&nd6_mtx);
			NET_LOCK();
			if (!nd6_llinfo_timer(rt, i_am_router)) {
				if (rt->rt_expire && rt->rt_expire < expire)
					expire = rt->rt_expire;
			}
			NET_UNLOCK();
			rtfree(rt);
			mtx_enter(&nd6_mtx);
		} else if (rt->rt_expire && rt->rt_expire < expire)
			expire = rt->rt_expire;
	}
	mtx_leave(&nd6_mtx);

	secs = expire - uptime;
	if (secs < 1)
		secs = 1;

	NET_LOCK();
	if (!TAILQ_EMPTY(&nd6_list)) {
		nd6_timer_next = uptime + secs;
		timeout_add_sec(&nd6_timer_to, secs);
	}
	NET_UNLOCK();
}

/*
 * ND timer state handling.
 *
 * Returns 1 if `rt' should no longer be used, 0 otherwise.
 */
int
nd6_llinfo_timer(struct rtentry *rt, int i_am_router)
{
	struct llinfo_nd6 *ln = (struct llinfo_nd6 *)rt->rt_llinfo;
	struct sockaddr_in6 *dst = satosin6(rt_key(rt));
	struct ifnet *ifp;

	NET_ASSERT_LOCKED_EXCLUSIVE();

	/* might have been freed between leave nd6_mtx and enter net lock */
	if (!ISSET(rt->rt_flags, RTF_LLINFO))
		return 0;

	if ((ifp = if_get(rt->rt_ifidx)) == NULL)
		return 1;

	switch (ln->ln_state) {
	case ND6_LLINFO_INCOMPLETE:
		if (ln->ln_asked < atomic_load_int(&nd6_mmaxtries)) {
			ln->ln_asked++;
			nd6_llinfo_settimer(ln, RETRANS_TIMER / 1000);
			nd6_ns_output(ifp, NULL, &dst->sin6_addr,
			    &ln->ln_saddr6, 0);
		} else {
			struct mbuf_list ml;
			struct mbuf *m;
			unsigned int len;

			mq_delist(&ln->ln_mq, &ml);
			len = ml_len(&ml);
			while ((m = ml_dequeue(&ml)) != NULL) {
				/*
				 * Fake rcvif to make the ICMP error
				 * more helpful in diagnosing for the
				 * receiver.
				 * XXX: should we consider older rcvif?
				 */
				m->m_pkthdr.ph_ifidx = rt->rt_ifidx;

				icmp6_error(m, ICMP6_DST_UNREACH,
				    ICMP6_DST_UNREACH_ADDR, 0);
			}

			/* XXXSMP we also discard if other CPU enqueues */
			if (mq_len(&ln->ln_mq) > 0) {
				/* mbuf is back in queue. Discard. */
				atomic_sub_int(&ln_hold_total,
				    len + mq_purge(&ln->ln_mq));
			} else
				atomic_sub_int(&ln_hold_total, len);

			nd6_free(rt, ifp, i_am_router);
			ln = NULL;
		}
		break;

	case ND6_LLINFO_REACHABLE:
		if (!ND6_LLINFO_PERMANENT(ln)) {
			ln->ln_state = ND6_LLINFO_STALE;
			nd6_llinfo_settimer(ln, nd6_gctimer);
		}
		break;

	case ND6_LLINFO_STALE:
	case ND6_LLINFO_PURGE:
		/* Garbage Collection(RFC 2461 5.3) */
		if (!ND6_LLINFO_PERMANENT(ln)) {
			nd6_free(rt, ifp, i_am_router);
			ln = NULL;
		}
		break;

	case ND6_LLINFO_DELAY:
		/* We need NUD */
		ln->ln_asked = 1;
		ln->ln_state = ND6_LLINFO_PROBE;
		nd6_llinfo_settimer(ln, RETRANS_TIMER / 1000);
		nd6_ns_output(ifp, &dst->sin6_addr, &dst->sin6_addr,
		    &ln->ln_saddr6, 0);
		break;

	case ND6_LLINFO_PROBE:
		if (ln->ln_asked < atomic_load_int(&nd6_umaxtries)) {
			ln->ln_asked++;
			nd6_llinfo_settimer(ln, RETRANS_TIMER / 1000);
			nd6_ns_output(ifp, &dst->sin6_addr, &dst->sin6_addr,
			    &ln->ln_saddr6, 0);
		} else {
			nd6_free(rt, ifp, i_am_router);
			ln = NULL;
		}
		break;
	}

	if_put(ifp);

	return (ln == NULL);
}

void
nd6_expire_timer_update(struct in6_ifaddr *ia6)
{
	time_t expire_time = INT64_MAX;

	if (ia6->ia6_lifetime.ia6t_vltime != ND6_INFINITE_LIFETIME)
		expire_time = ia6->ia6_lifetime.ia6t_expire;

	if (!(ia6->ia6_flags & IN6_IFF_DEPRECATED) &&
	    ia6->ia6_lifetime.ia6t_pltime != ND6_INFINITE_LIFETIME &&
	    expire_time > ia6->ia6_lifetime.ia6t_preferred)
		expire_time = ia6->ia6_lifetime.ia6t_preferred;

	if (expire_time == INT64_MAX)
		return;

	/*
	 * IFA6_IS_INVALID() and IFA6_IS_DEPRECATED() check for uptime
	 * greater than ia6t_expire or ia6t_preferred, not greater or equal.
	 * Schedule timeout one second later so that either IFA6_IS_INVALID()
	 * or IFA6_IS_DEPRECATED() is true.
	 */
	expire_time++;

	if (!timeout_pending(&nd6_expire_timeout) ||
	    nd6_expire_next > expire_time) {
		int secs;

		secs = expire_time - getuptime();
		if (secs < 0)
			secs = 0;

		timeout_add_sec(&nd6_expire_timeout, secs);
		nd6_expire_next = expire_time;
	}
}

/*
 * Expire interface addresses.
 */
void
nd6_expire(void *unused)
{
	struct ifnet *ifp;

	NET_LOCK();

	TAILQ_FOREACH(ifp, &ifnetlist, if_list) {
		struct ifaddr *ifa, *nifa;
		struct in6_ifaddr *ia6;

		TAILQ_FOREACH_SAFE(ifa, &ifp->if_addrlist, ifa_list, nifa) {
			if (ifa->ifa_addr->sa_family != AF_INET6)
				continue;
			ia6 = ifatoia6(ifa);
			/* check address lifetime */
			if (IFA6_IS_INVALID(ia6)) {
				in6_purgeaddr(&ia6->ia_ifa);
			} else {
				if (IFA6_IS_DEPRECATED(ia6))
					ia6->ia6_flags |= IN6_IFF_DEPRECATED;
				nd6_expire_timer_update(ia6);
			}
		}
	}

	NET_UNLOCK();
}

void
nd6_expire_timer(void *unused)
{
	task_add(net_tq(0), &nd6_expire_task);
}

/*
 * Nuke neighbor cache/prefix/default router management table, right before
 * ifp goes away.
 */
void
nd6_purge(struct ifnet *ifp)
{
	struct llinfo_nd6_iterator iter = { .ln_rt = NULL };
	struct llinfo_nd6 *ln = NULL;
	int i_am_router = (atomic_load_int(&ip6_forwarding) != 0);

	/*
	 * Nuke neighbor cache entries for the ifp.
	 */
	mtx_enter(&nd6_mtx);
	while ((ln = nd6_iterator(ln, &iter)) != NULL) {
		struct rtentry *rt = ln->ln_rt;
		struct sockaddr_dl *sdl;

		if (rt != NULL && rt->rt_gateway != NULL &&
		    rt->rt_gateway->sa_family == AF_LINK) {
			sdl = satosdl(rt->rt_gateway);
			if (sdl->sdl_index == ifp->if_index) {
				rtref(rt);
				mtx_leave(&nd6_mtx);
				nd6_free(rt, ifp, i_am_router);
				rtfree(rt);
				mtx_enter(&nd6_mtx);
			}
		}
	}
	mtx_leave(&nd6_mtx);
}

struct rtentry *
nd6_lookup(const struct in6_addr *addr6, int create, struct ifnet *ifp,
    u_int rtableid)
{
	struct rtentry *rt;
	struct sockaddr_in6 sin6;
	int flags;

	bzero(&sin6, sizeof(sin6));
	sin6.sin6_len = sizeof(struct sockaddr_in6);
	sin6.sin6_family = AF_INET6;
	sin6.sin6_addr = *addr6;
	flags = (create) ? RT_RESOLVE : 0;

	rt = rtalloc(sin6tosa(&sin6), flags, rtableid);
	if (rt != NULL && (rt->rt_flags & RTF_LLINFO) == 0) {
		/*
		 * This is the case for the default route.
		 * If we want to create a neighbor cache for the address, we
		 * should free the route for the destination and allocate an
		 * interface route.
		 */
		if (create) {
			rtfree(rt);
			rt = NULL;
		}
	}
	if (rt == NULL) {
		if (create && ifp) {
			struct rt_addrinfo info;
			struct llinfo_nd6 *ln;
			struct ifaddr *ifa;
			int error;

			/*
			 * If no route is available and create is set,
			 * we allocate a host route for the destination
			 * and treat it like an interface route.
			 * This hack is necessary for a neighbor which can't
			 * be covered by our own prefix.
			 */
			ifa = ifaof_ifpforaddr(sin6tosa(&sin6), ifp);
			if (ifa == NULL)
				return (NULL);

			/*
			 * Create a new route.  RTF_LLINFO is necessary
			 * to create a Neighbor Cache entry for the
			 * destination in nd6_rtrequest which will be
			 * called in rtrequest.
			 */
			bzero(&info, sizeof(info));
			info.rti_ifa = ifa;
			info.rti_flags = RTF_HOST | RTF_LLINFO;
			info.rti_info[RTAX_DST] = sin6tosa(&sin6);
			info.rti_info[RTAX_GATEWAY] = sdltosa(ifp->if_sadl);
			error = rtrequest(RTM_ADD, &info, RTP_CONNECTED, &rt,
			    rtableid);
			if (error)
				return (NULL);
			mtx_enter(&nd6_mtx);
			ln = (struct llinfo_nd6 *)rt->rt_llinfo;
			if (ln != NULL)
				ln->ln_state = ND6_LLINFO_NOSTATE;
			mtx_leave(&nd6_mtx);
		} else
			return (NULL);
	}
	/*
	 * Validation for the entry.
	 * Note that the check for rt_llinfo is necessary because a cloned
	 * route from a parent route that has the L flag (e.g. the default
	 * route to a p2p interface) may have the flag, too, while the
	 * destination is not actually a neighbor.
	 */
	if ((rt->rt_flags & RTF_GATEWAY) || (rt->rt_flags & RTF_LLINFO) == 0 ||
	    rt->rt_gateway->sa_family != AF_LINK || rt->rt_llinfo == NULL ||
	    (ifp != NULL && rt->rt_ifidx != ifp->if_index)) {
		rtfree(rt);
		return (NULL);
	}
	return (rt);
}

/*
 * Detect if a given IPv6 address identifies a neighbor on a given link.
 * XXX: should take care of the destination of a p2p link?
 */
int
nd6_is_addr_neighbor(const struct sockaddr_in6 *addr, struct ifnet *ifp)
{
	struct in6_ifaddr *ia6;
	struct ifaddr *ifa;
	struct rtentry *rt;

	/*
	 * A link-local address is always a neighbor.
	 * XXX: we should use the sin6_scope_id field rather than the embedded
	 * interface index.
	 * XXX: a link does not necessarily specify a single interface.
	 */
	if (IN6_IS_ADDR_LINKLOCAL(&addr->sin6_addr) &&
	    ntohs(*(u_int16_t *)&addr->sin6_addr.s6_addr[2]) == ifp->if_index)
		return (1);

	TAILQ_FOREACH(ifa, &ifp->if_addrlist, ifa_list) {
		if (ifa->ifa_addr->sa_family != AF_INET6)
			continue;

		ia6 = ifatoia6(ifa);

		/* Prefix check down below. */
		if (ia6->ia6_flags & IN6_IFF_AUTOCONF)
			continue;

		if (IN6_ARE_MASKED_ADDR_EQUAL(&addr->sin6_addr,
		    &ia6->ia_addr.sin6_addr,
		    &ia6->ia_prefixmask.sin6_addr))
			return (1);
	}

	/*
	 * Even if the address matches none of our addresses, it might be
	 * in the neighbor cache.
	 */
	rt = nd6_lookup(&addr->sin6_addr, 0, ifp, ifp->if_rdomain);
	if (rt != NULL) {
		rtfree(rt);
		return (1);
	}

	return (0);
}

void
nd6_invalidate(struct rtentry *rt)
{
	struct llinfo_nd6 *ln;
	struct sockaddr_dl *sdl = satosdl(rt->rt_gateway);

	mtx_enter(&nd6_mtx);
	ln = (struct llinfo_nd6 *)rt->rt_llinfo;
	if (ln == NULL) {
		mtx_leave(&nd6_mtx);
		return;
	}
	atomic_sub_int(&ln_hold_total, mq_purge(&ln->ln_mq));
	sdl->sdl_alen = 0;
	ln->ln_state = ND6_LLINFO_INCOMPLETE;
	ln->ln_asked = 0;
	mtx_leave(&nd6_mtx);
}

/*
 * Free an nd6 llinfo entry.
 */
void
nd6_free(struct rtentry *rt, struct ifnet *ifp, int i_am_router)
{
	struct llinfo_nd6 *ln = (struct llinfo_nd6 *)rt->rt_llinfo;
	struct in6_addr in6 = satosin6(rt_key(rt))->sin6_addr;

	NET_ASSERT_LOCKED_EXCLUSIVE();

	if (!i_am_router) {
		if (ln->ln_router) {
			/*
			 * rt6_flush must be called whether or not the neighbor
			 * is in the Default Router List.
			 * See a corresponding comment in nd6_na_input().
			 */
			rt6_flush(&in6, ifp);
		}
	}

	KASSERT(!ISSET(rt->rt_flags, RTF_LOCAL));
	nd6_invalidate(rt);

	/*
	 * Detach the route from the routing tree and the list of neighbor
	 * caches, and disable the route entry not to be used in already
	 * cached routes.
	 */
	if (!ISSET(rt->rt_flags, RTF_STATIC|RTF_CACHED))
		rtdeletemsg(rt, ifp, ifp->if_rdomain);
}

void
nd6_rtrequest(struct ifnet *ifp, int req, struct rtentry *rt)
{
	struct sockaddr *gate = rt->rt_gateway;
	struct llinfo_nd6 *ln;
	struct ifaddr *ifa;
	struct in6_ifaddr *ifa6;

	if (ISSET(rt->rt_flags, RTF_GATEWAY|RTF_MULTICAST|RTF_MPLS))
		return;

	if (nd6_need_cache(ifp) == 0 && (rt->rt_flags & RTF_HOST) == 0) {
		/*
		 * This is probably an interface direct route for a link
		 * which does not need neighbor caches (e.g. fe80::%lo0/64).
		 * We do not need special treatment below for such a route.
		 * Moreover, the RTF_LLINFO flag which would be set below
		 * would annoy the ndp(8) command.
		 */
		return;
	}

	if (req == RTM_RESOLVE && nd6_need_cache(ifp) == 0) {
		/*
		 * For routing daemons like ospf6d we allow neighbor discovery
		 * based on the cloning route only.  This allows us to send
		 * packets directly into a network without having an address
		 * with matching prefix on the interface.  If the cloning
		 * route is used for an 6to4 interface, we would mistakenly
		 * make a neighbor cache for the host route, and would see
		 * strange neighbor solicitation for the corresponding
		 * destination.  In order to avoid confusion, we check if the
		 * interface is suitable for neighbor discovery, and stop the
		 * process if not.  Additionally, we remove the LLINFO flag
		 * so that ndp(8) will not try to get the neighbor information
		 * of the destination.
		 */
		rt->rt_flags &= ~RTF_LLINFO;
		return;
	}

	switch (req) {
	case RTM_ADD:
		if (rt->rt_flags & RTF_CLONING) {
			rt->rt_expire = 0;
			break;
		}
		if ((rt->rt_flags & RTF_LOCAL) && rt->rt_llinfo == NULL)
			rt->rt_expire = 0;
		/* FALLTHROUGH */
	case RTM_RESOLVE:
		if (gate->sa_family != AF_LINK ||
		    gate->sa_len < sizeof(struct sockaddr_dl)) {
			log(LOG_DEBUG, "%s: bad gateway value: %s\n",
			    __func__, ifp->if_xname);
			break;
		}
		satosdl(gate)->sdl_type = ifp->if_type;
		satosdl(gate)->sdl_index = ifp->if_index;
		/*
		 * Case 2: This route may come from cloning, or a manual route
		 * add with a LL address.
		 */
		ln = pool_get(&nd6_pool, PR_NOWAIT | PR_ZERO);
		if (ln == NULL) {
			log(LOG_DEBUG, "%s: pool get failed\n", __func__);
			break;
		}

		mtx_enter(&nd6_mtx);
		if (rt->rt_llinfo != NULL) {
			/* we lost the race, another thread has entered it */
			mtx_leave(&nd6_mtx);
			pool_put(&nd6_pool, ln);
			break;
		}
		nd6_inuse++;
		refcnt_init(&ln->ln_refcnt);
		mq_init(&ln->ln_mq, LN_HOLD_QUEUE, IPL_SOFTNET);
		rt->rt_llinfo = (caddr_t)ln;
		ln->ln_rt = rt;
		rt->rt_flags |= RTF_LLINFO;
		TAILQ_INSERT_HEAD(&nd6_list, ln, ln_list);
		/* this is required for "ndp" command. - shin */
		if (req == RTM_ADD) {
			/*
			 * gate should have some valid AF_LINK entry,
			 * and ln expire should have some lifetime
			 * which is specified by ndp command.
			 */
			ln->ln_state = ND6_LLINFO_REACHABLE;
		} else {
			/*
			 * When req == RTM_RESOLVE, rt is created and
			 * initialized in rtrequest(), so rt_expire is 0.
			 */
			ln->ln_state = ND6_LLINFO_NOSTATE;
			nd6_llinfo_settimer(ln, 0);
		}

		/*
		 * If we have too many cache entries, initiate immediate
		 * purging for some "less recently used" entries.  Note that
		 * we cannot directly call nd6_free() here because it would
		 * cause re-entering rtable related routines triggering
		 * lock-order-reversal problems.
		 */
		if (nd6_inuse >= atomic_load_int(&ip6_neighborgcthresh)) {
			int i;

			for (i = 0; i < 10; i++) {
				struct llinfo_nd6 *ln_end;

				ln_end = TAILQ_LAST(&nd6_list, llinfo_nd6_head);
				if (ln_end == ln)
					break;

				/* Move this entry to the head */
				TAILQ_REMOVE(&nd6_list, ln_end, ln_list);
				TAILQ_INSERT_HEAD(&nd6_list, ln_end, ln_list);

				if (ND6_LLINFO_PERMANENT(ln_end))
					continue;

				if (ln_end->ln_state > ND6_LLINFO_INCOMPLETE)
					ln_end->ln_state = ND6_LLINFO_STALE;
				else
					ln_end->ln_state = ND6_LLINFO_PURGE;
				nd6_llinfo_settimer(ln_end, 0);
			}
		}

		/*
		 * check if rt_key(rt) is one of my address assigned
		 * to the interface.
		 */
		ifa6 = in6ifa_ifpwithaddr(ifp,
		    &satosin6(rt_key(rt))->sin6_addr);
		ifa = ifa6 ? &ifa6->ia_ifa : NULL;
		if (ifa != NULL ||
		    (rt->rt_flags & RTF_ANNOUNCE)) {
			ln->ln_state = ND6_LLINFO_REACHABLE;
			rt->rt_expire = 0;
		}
		mtx_leave(&nd6_mtx);

		/* join solicited node multicast for proxy ND */
		if (ifa == NULL &&
		    (rt->rt_flags & RTF_ANNOUNCE) &&
		    (ifp->if_flags & IFF_MULTICAST)) {
			struct in6_addr llsol;
			int error;

			llsol = satosin6(rt_key(rt))->sin6_addr;
			llsol.s6_addr16[0] = htons(0xff02);
			llsol.s6_addr16[1] = htons(ifp->if_index);
			llsol.s6_addr32[1] = 0;
			llsol.s6_addr32[2] = htonl(1);
			llsol.s6_addr8[12] = 0xff;

			KERNEL_LOCK();
			in6_addmulti(&llsol, ifp, &error);
			KERNEL_UNLOCK();
		}
		break;

	case RTM_DELETE:
		mtx_enter(&nd6_mtx);
		ln = (struct llinfo_nd6 *)rt->rt_llinfo;
		if (ln == NULL) {
			/* we lost the race, another thread has removed it */
			mtx_leave(&nd6_mtx);
			break;
		}
		nd6_inuse--;
		TAILQ_REMOVE(&nd6_list, ln, ln_list);
		rt->rt_expire = 0;
		rt->rt_llinfo = NULL;
		rt->rt_flags &= ~RTF_LLINFO;
		atomic_sub_int(&ln_hold_total, mq_purge(&ln->ln_mq));
		mtx_leave(&nd6_mtx);

		if (refcnt_rele(&ln->ln_refcnt))
			pool_put(&nd6_pool, ln);

		/* leave from solicited node multicast for proxy ND */
		if ((rt->rt_flags & RTF_ANNOUNCE) != 0 &&
		    (ifp->if_flags & IFF_MULTICAST) != 0) {
			struct in6_addr llsol;
			struct in6_multi *in6m;

			llsol = satosin6(rt_key(rt))->sin6_addr;
			llsol.s6_addr16[0] = htons(0xff02);
			llsol.s6_addr16[1] = htons(ifp->if_index);
			llsol.s6_addr32[1] = 0;
			llsol.s6_addr32[2] = htonl(1);
			llsol.s6_addr8[12] = 0xff;

			KERNEL_LOCK();
			IN6_LOOKUP_MULTI(llsol, ifp, in6m);
			if (in6m)
				in6_delmulti(in6m);
			KERNEL_UNLOCK();
		}
		break;

	case RTM_INVALIDATE:
		if (!ISSET(rt->rt_flags, RTF_LOCAL))
			nd6_invalidate(rt);
		break;
	}
}

int
nd6_ioctl(u_long cmd, caddr_t data, struct ifnet *ifp)
{
	struct in6_ndireq *ndi = (struct in6_ndireq *)data;
	struct in6_nbrinfo *nbi = (struct in6_nbrinfo *)data;
	struct rtentry *rt;

	switch (cmd) {
	case SIOCGIFINFO_IN6:
		NET_LOCK_SHARED();
		ndi->ndi = *ifp->if_nd;
		NET_UNLOCK_SHARED();
		return (0);
	case SIOCGNBRINFO_IN6:
	{
		struct llinfo_nd6 *ln;
		struct in6_addr nb_addr = nbi->addr; /* make local for safety */
		time_t expire;

		NET_LOCK_SHARED();
		/*
		 * XXX: KAME specific hack for scoped addresses
		 *      XXXX: for other scopes than link-local?
		 */
		if (IN6_IS_ADDR_LINKLOCAL(&nb_addr) ||
		    IN6_IS_ADDR_MC_LINKLOCAL(&nb_addr)) {
			u_int16_t *idp = (u_int16_t *)&nb_addr.s6_addr[2];

			if (*idp == 0)
				*idp = htons(ifp->if_index);
		}

		rt = nd6_lookup(&nb_addr, 0, ifp, ifp->if_rdomain);
		mtx_enter(&nd6_mtx);
		if (rt == NULL ||
		    (ln = (struct llinfo_nd6 *)rt->rt_llinfo) == NULL) {
			mtx_leave(&nd6_mtx);
			rtfree(rt);
			NET_UNLOCK_SHARED();
			return (EINVAL);
		}
		expire = ln->ln_rt->rt_expire;
		if (expire != 0) {
			expire -= getuptime();
			expire += gettime();
		}

		nbi->state = ln->ln_state;
		nbi->asked = ln->ln_asked;
		nbi->isrouter = ln->ln_router;
		nbi->expire = expire;
		mtx_leave(&nd6_mtx);

		rtfree(rt);
		NET_UNLOCK_SHARED();
		return (0);
	}
	}
	return (0);
}

/*
 * Create neighbor cache entry and cache link-layer address,
 * on reception of inbound ND6 packets.  (RS/RA/NS/redirect)
 *
 * type - ICMP6 type
 * code - type dependent information
 */
void
nd6_cache_lladdr(struct ifnet *ifp, const struct in6_addr *from, char *lladdr,
    int lladdrlen, int type, int code, int i_am_router)
{
	struct rtentry *rt;
	struct llinfo_nd6 *ln;
	int is_newentry;
	struct sockaddr_dl *sdl;
	int do_update;
	int olladdr;
	int llchange;
	int newstate = 0;

	NET_ASSERT_LOCKED_EXCLUSIVE();

	if (!ifp)
		panic("%s: ifp == NULL", __func__);
	if (!from)
		panic("%s: from == NULL", __func__);

	/* nothing must be updated for unspecified address */
	if (IN6_IS_ADDR_UNSPECIFIED(from))
		return;

	/*
	 * Validation about ifp->if_addrlen and lladdrlen must be done in
	 * the caller.
	 *
	 * XXX If the link does not have link-layer address, what should
	 * we do? (ifp->if_addrlen == 0)
	 * Spec says nothing in sections for RA, RS and NA.  There's small
	 * description on it in NS section (RFC 2461 7.2.3).
	 */

	rt = nd6_lookup(from, 0, ifp, ifp->if_rdomain);
	if (rt == NULL) {
		rt = nd6_lookup(from, 1, ifp, ifp->if_rdomain);
		is_newentry = 1;
	} else {
		/* do not overwrite local or static entry */
		if (ISSET(rt->rt_flags, RTF_STATIC|RTF_LOCAL)) {
			rtfree(rt);
			return;
		}
		is_newentry = 0;
	}

	if (!rt)
		return;
	if ((rt->rt_flags & (RTF_GATEWAY | RTF_LLINFO)) != RTF_LLINFO) {
fail:
		nd6_free(rt, ifp, i_am_router);
		rtfree(rt);
		return;
	}
	ln = (struct llinfo_nd6 *)rt->rt_llinfo;
	if (ln == NULL)
		goto fail;
	if (rt->rt_gateway == NULL)
		goto fail;
	if (rt->rt_gateway->sa_family != AF_LINK)
		goto fail;
	sdl = satosdl(rt->rt_gateway);

	olladdr = (sdl->sdl_alen) ? 1 : 0;
	if (olladdr && lladdr) {
		if (bcmp(lladdr, LLADDR(sdl), ifp->if_addrlen))
			llchange = 1;
		else
			llchange = 0;
	} else
		llchange = 0;

	/*
	 * newentry olladdr  lladdr  llchange	(*=record)
	 *	0	n	n	--	(1)
	 *	0	y	n	--	(2)
	 *	0	n	y	--	(3) * STALE
	 *	0	y	y	n	(4) *
	 *	0	y	y	y	(5) * STALE
	 *	1	--	n	--	(6)   NOSTATE(= PASSIVE)
	 *	1	--	y	--	(7) * STALE
	 */

	if (llchange) {
		char addr[INET6_ADDRSTRLEN];
		log(LOG_INFO, "ndp info overwritten for %s by %s on %s\n",
		    inet_ntop(AF_INET6, from, addr, sizeof(addr)),
		    ether_sprintf(lladdr), ifp->if_xname);
	}
	if (lladdr) {		/* (3-5) and (7) */
		/*
		 * Record source link-layer address
		 * XXX is it dependent to ifp->if_type?
		 */
		sdl->sdl_alen = ifp->if_addrlen;
		bcopy(lladdr, LLADDR(sdl), ifp->if_addrlen);
	}

	if (!is_newentry) {
		if ((!olladdr && lladdr) ||		/* (3) */
		    (olladdr && lladdr && llchange)) {	/* (5) */
			do_update = 1;
			newstate = ND6_LLINFO_STALE;
		} else					/* (1-2,4) */
			do_update = 0;
	} else {
		do_update = 1;
		if (!lladdr)				/* (6) */
			newstate = ND6_LLINFO_NOSTATE;
		else					/* (7) */
			newstate = ND6_LLINFO_STALE;
	}

	if (do_update) {
		/*
		 * Update the state of the neighbor cache.
		 */
		ln->ln_state = newstate;

		if (ln->ln_state == ND6_LLINFO_STALE) {
			/*
			 * Since nd6_resolve() in ifp->if_output() will cause
			 * state transition to DELAY and reset the timer,
			 * we must set the timer now, although it is actually
			 * meaningless.
			 */
			nd6_llinfo_settimer(ln, nd6_gctimer);
			if_output_mq(ifp, &ln->ln_mq, &ln_hold_total,
			    rt_key(rt), rt);
		} else if (ln->ln_state == ND6_LLINFO_INCOMPLETE) {
			/* probe right away */
			nd6_llinfo_settimer(ln, 0);
		}
	}

	/*
	 * ICMP6 type dependent behavior.
	 *
	 * NS: clear IsRouter if new entry
	 * RS: clear IsRouter
	 * RA: set IsRouter if there's lladdr
	 * redir: clear IsRouter if new entry
	 *
	 * RA case, (1):
	 * The spec says that we must set IsRouter in the following cases:
	 * - If lladdr exist, set IsRouter.  This means (1-5).
	 * - If it is old entry (!newentry), set IsRouter.  This means (7).
	 * So, based on the spec, in (1-5) and (7) cases we must set IsRouter.
	 * A question arises for (1) case.  (1) case has no lladdr in the
	 * neighbor cache, this is similar to (6).
	 * This case is rare but we figured that we MUST NOT set IsRouter.
	 *
	 * newentry olladdr  lladdr  llchange	    NS  RS  RA	redir
	 *							D R
	 *	0	n	n	--	(1)	c   ?     s
	 *	0	y	n	--	(2)	c   s     s
	 *	0	n	y	--	(3)	c   s     s
	 *	0	y	y	n	(4)	c   s     s
	 *	0	y	y	y	(5)	c   s     s
	 *	1	--	n	--	(6) c	c	c s
	 *	1	--	y	--	(7) c	c   s	c s
	 *
	 *					(c=clear s=set)
	 */
	switch (type & 0xff) {
	case ND_NEIGHBOR_SOLICIT:
		/*
		 * New entry must have is_router flag cleared.
		 */
		if (is_newentry)	/* (6-7) */
			ln->ln_router = 0;
		break;
	case ND_REDIRECT:
		/*
		 * If the icmp is a redirect to a better router, always set the
		 * is_router flag.  Otherwise, if the entry is newly created,
		 * clear the flag.  [RFC 2461, sec 8.3]
		 */
		if (code == ND_REDIRECT_ROUTER)
			ln->ln_router = 1;
		else if (is_newentry) /* (6-7) */
			ln->ln_router = 0;
		break;
	case ND_ROUTER_SOLICIT:
		/*
		 * is_router flag must always be cleared.
		 */
		ln->ln_router = 0;
		break;
	case ND_ROUTER_ADVERT:
		/*
		 * Mark an entry with lladdr as a router.
		 */
		if ((!is_newentry && (olladdr || lladdr)) ||	/* (2-5) */
		    (is_newentry && lladdr)) {			/* (7) */
			ln->ln_router = 1;
		}
		break;
	}

	rtfree(rt);
}

void
nd6_slowtimo(void *ignored_arg)
{
	struct nd_ifinfo *nd6if;
	struct ifnet *ifp;

	NET_LOCK();

	timeout_add_sec(&nd6_slowtimo_ch, ND6_SLOWTIMER_INTERVAL);

	TAILQ_FOREACH(ifp, &ifnetlist, if_list) {
		nd6if = ifp->if_nd;
		if ((nd6if->recalctm -= ND6_SLOWTIMER_INTERVAL) <= 0) {
			/*
			 * Since reachable time rarely changes by router
			 * advertisements, we SHOULD insure that a new random
			 * value gets recomputed at least once every few hours.
			 * (RFC 2461, 6.3.4)
			 */
			nd6if->recalctm = ND6_RECALC_REACHTM_INTERVAL;
			nd6if->reachable = ND_COMPUTE_RTIME(REACHABLE_TIME);
		}
	}
	NET_UNLOCK();
}

int
nd6_resolve(struct ifnet *ifp, struct rtentry *rt0, struct mbuf *m,
    struct sockaddr *dst, u_char *desten)
{
	struct sockaddr_dl *sdl;
	struct rtentry *rt;
	struct llinfo_nd6 *ln;
	struct in6_addr saddr6;
	time_t uptime;
	int solicit = 0;

	if (m->m_flags & M_MCAST) {
		ETHER_MAP_IPV6_MULTICAST(&satosin6(dst)->sin6_addr, desten);
		return (0);
	}

	uptime = getuptime();
	rt = rt_getll(rt0);

	if (rt == NULL || (ISSET(rt->rt_flags, RTF_REJECT) &&
	    (rt->rt_expire == 0 || rt->rt_expire > uptime))) {
		m_freem(m);
		return (rt == rt0 ? EHOSTDOWN : EHOSTUNREACH);
	}

	/*
	 * Address resolution or Neighbor Unreachability Detection
	 * for the next hop.
	 * At this point, the destination of the packet must be a unicast
	 * or an anycast address(i.e. not a multicast).
	 */
	if (!ISSET(rt->rt_flags, RTF_LLINFO)) {
		char addr[INET6_ADDRSTRLEN];
		log(LOG_DEBUG, "%s: %s: route contains no ND information\n",
		    __func__, inet_ntop(AF_INET6,
		    &satosin6(rt_key(rt))->sin6_addr, addr, sizeof(addr)));
		goto bad;
	}

	if (rt->rt_gateway->sa_family != AF_LINK) {
		printf("%s: something odd happens\n", __func__);
		goto bad;
	}

	mtx_enter(&nd6_mtx);
	ln = (struct llinfo_nd6 *)rt->rt_llinfo;
	if (ln == NULL) {
		mtx_leave(&nd6_mtx);
		goto bad;
	}

	/*
	 * Move this entry to the head of the queue so that it is less likely
	 * for this entry to be a target of forced garbage collection (see
	 * nd6_rtrequest()).
	 */
	TAILQ_REMOVE(&nd6_list, ln, ln_list);
	TAILQ_INSERT_HEAD(&nd6_list, ln, ln_list);

	/*
	 * The first time we send a packet to a neighbor whose entry is
	 * STALE, we have to change the state to DELAY and set a timer to
	 * expire in DELAY_FIRST_PROBE_TIME seconds to ensure we do
	 * neighbor unreachability detection on expiration.
	 * (RFC 2461 7.3.3)
	 */
	if (ln->ln_state == ND6_LLINFO_STALE) {
		ln->ln_asked = 0;
		ln->ln_state = ND6_LLINFO_DELAY;
		nd6_llinfo_settimer(ln, atomic_load_int(&nd6_delay));
	}

	/*
	 * If the neighbor cache entry has a state other than INCOMPLETE
	 * (i.e. its link-layer address is already resolved), just
	 * send the packet.
	 */
	if (ln->ln_state > ND6_LLINFO_INCOMPLETE) {
		mtx_leave(&nd6_mtx);

		sdl = satosdl(rt->rt_gateway);
		if (sdl->sdl_alen != ETHER_ADDR_LEN) {
			char addr[INET6_ADDRSTRLEN];
			log(LOG_DEBUG, "%s: %s: incorrect nd6 information\n",
			    __func__,
			    inet_ntop(AF_INET6, &satosin6(dst)->sin6_addr,
				addr, sizeof(addr)));
			goto bad;
		}

		bcopy(LLADDR(sdl), desten, sdl->sdl_alen);
		return (0);
	}

	/*
	 * There is a neighbor cache entry, but no ethernet address
	 * response yet.  Insert mbuf in hold queue if below limit.
	 * If above the limit free the queue without queuing the new packet.
	 */
	if (ln->ln_state == ND6_LLINFO_NOSTATE)
		ln->ln_state = ND6_LLINFO_INCOMPLETE;
	/* source address of prompting packet is needed by nd6_ns_output() */
	if (m->m_len >= sizeof(struct ip6_hdr)) {
		memcpy(&ln->ln_saddr6, &mtod(m, struct ip6_hdr *)->ip6_src,
		    sizeof(ln->ln_saddr6));
	}
	if (atomic_inc_int_nv(&ln_hold_total) <= LN_HOLD_TOTAL) {
		if (mq_push(&ln->ln_mq, m) != 0)
			atomic_dec_int(&ln_hold_total);
	} else {
		atomic_sub_int(&ln_hold_total, mq_purge(&ln->ln_mq) + 1);
		m_freem(m);
	}

	/*
	 * If there has been no NS for the neighbor after entering the
	 * INCOMPLETE state, send the first solicitation.
	 */
	if (!ND6_LLINFO_PERMANENT(ln) && ln->ln_asked == 0) {
		ln->ln_asked++;
		nd6_llinfo_settimer(ln, RETRANS_TIMER / 1000);
		saddr6 = ln->ln_saddr6;
		solicit = 1;
	}
	mtx_leave(&nd6_mtx);

	if (solicit)
		nd6_ns_output(ifp, NULL, &satosin6(dst)->sin6_addr, &saddr6, 0);
	return (EAGAIN);

bad:
	m_freem(m);
	return (EINVAL);
}

int
nd6_need_cache(struct ifnet *ifp)
{
	/*
	 * RFC2893 says:
	 * - unidirectional tunnels needs no ND
	 */
	switch (ifp->if_type) {
	case IFT_ETHER:
	case IFT_IEEE80211:
	case IFT_CARP:
		return (1);
	default:
		return (0);
	}
}
