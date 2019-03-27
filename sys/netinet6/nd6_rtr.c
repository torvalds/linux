/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
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
 *
 *	$KAME: nd6_rtr.c,v 1.111 2001/04/27 01:37:15 jinmei Exp $
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "opt_inet.h"
#include "opt_inet6.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/refcount.h>
#include <sys/socket.h>
#include <sys/sockio.h>
#include <sys/time.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/errno.h>
#include <sys/rmlock.h>
#include <sys/rwlock.h>
#include <sys/syslog.h>
#include <sys/queue.h>

#include <net/if.h>
#include <net/if_var.h>
#include <net/if_types.h>
#include <net/if_dl.h>
#include <net/route.h>
#include <net/route_var.h>
#include <net/radix.h>
#include <net/vnet.h>

#include <netinet/in.h>
#include <net/if_llatbl.h>
#include <netinet6/in6_var.h>
#include <netinet6/in6_ifattach.h>
#include <netinet/ip6.h>
#include <netinet6/ip6_var.h>
#include <netinet6/nd6.h>
#include <netinet/icmp6.h>
#include <netinet6/scope6_var.h>

static int rtpref(struct nd_defrouter *);
static struct nd_defrouter *defrtrlist_update(struct nd_defrouter *);
static int prelist_update(struct nd_prefixctl *, struct nd_defrouter *,
    struct mbuf *, int);
static struct in6_ifaddr *in6_ifadd(struct nd_prefixctl *, int);
static struct nd_pfxrouter *pfxrtr_lookup(struct nd_prefix *,
    struct nd_defrouter *);
static void pfxrtr_add(struct nd_prefix *, struct nd_defrouter *);
static void pfxrtr_del(struct nd_pfxrouter *);
static struct nd_pfxrouter *find_pfxlist_reachable_router(struct nd_prefix *);
static void defrouter_delreq(struct nd_defrouter *);
static void nd6_rtmsg(int, struct rtentry *);

static int in6_init_prefix_ltimes(struct nd_prefix *);
static void in6_init_address_ltimes(struct nd_prefix *,
    struct in6_addrlifetime *);

static int rt6_deleteroute(const struct rtentry *, void *);

VNET_DECLARE(int, nd6_recalc_reachtm_interval);
#define	V_nd6_recalc_reachtm_interval	VNET(nd6_recalc_reachtm_interval)

VNET_DEFINE_STATIC(struct ifnet *, nd6_defifp);
VNET_DEFINE(int, nd6_defifindex);
#define	V_nd6_defifp			VNET(nd6_defifp)

VNET_DEFINE(int, ip6_use_tempaddr) = 0;

VNET_DEFINE(int, ip6_desync_factor);
VNET_DEFINE(u_int32_t, ip6_temp_preferred_lifetime) = DEF_TEMP_PREFERRED_LIFETIME;
VNET_DEFINE(u_int32_t, ip6_temp_valid_lifetime) = DEF_TEMP_VALID_LIFETIME;

VNET_DEFINE(int, ip6_temp_regen_advance) = TEMPADDR_REGEN_ADVANCE;

#ifdef EXPERIMENTAL
VNET_DEFINE(int, nd6_ignore_ipv6_only_ra) = 1;
#endif

/* RTPREF_MEDIUM has to be 0! */
#define RTPREF_HIGH	1
#define RTPREF_MEDIUM	0
#define RTPREF_LOW	(-1)
#define RTPREF_RESERVED	(-2)
#define RTPREF_INVALID	(-3)	/* internal */

/*
 * Receive Router Solicitation Message - just for routers.
 * Router solicitation/advertisement is mostly managed by userland program
 * (rtadvd) so here we have no function like nd6_ra_output().
 *
 * Based on RFC 2461
 */
void
nd6_rs_input(struct mbuf *m, int off, int icmp6len)
{
	struct ifnet *ifp = m->m_pkthdr.rcvif;
	struct ip6_hdr *ip6 = mtod(m, struct ip6_hdr *);
	struct nd_router_solicit *nd_rs;
	struct in6_addr saddr6 = ip6->ip6_src;
	char *lladdr = NULL;
	int lladdrlen = 0;
	union nd_opts ndopts;
	char ip6bufs[INET6_ADDRSTRLEN], ip6bufd[INET6_ADDRSTRLEN];

	/*
	 * Accept RS only when V_ip6_forwarding=1 and the interface has
	 * no ND6_IFF_ACCEPT_RTADV.
	 */
	if (!V_ip6_forwarding || ND_IFINFO(ifp)->flags & ND6_IFF_ACCEPT_RTADV)
		goto freeit;

	/* RFC 6980: Nodes MUST silently ignore fragments */   
	if(m->m_flags & M_FRAGMENTED)
		goto freeit;

	/* Sanity checks */
	if (ip6->ip6_hlim != 255) {
		nd6log((LOG_ERR,
		    "nd6_rs_input: invalid hlim (%d) from %s to %s on %s\n",
		    ip6->ip6_hlim, ip6_sprintf(ip6bufs, &ip6->ip6_src),
		    ip6_sprintf(ip6bufd, &ip6->ip6_dst), if_name(ifp)));
		goto bad;
	}

	/*
	 * Don't update the neighbor cache, if src = ::.
	 * This indicates that the src has no IP address assigned yet.
	 */
	if (IN6_IS_ADDR_UNSPECIFIED(&saddr6))
		goto freeit;

#ifndef PULLDOWN_TEST
	IP6_EXTHDR_CHECK(m, off, icmp6len,);
	nd_rs = (struct nd_router_solicit *)((caddr_t)ip6 + off);
#else
	IP6_EXTHDR_GET(nd_rs, struct nd_router_solicit *, m, off, icmp6len);
	if (nd_rs == NULL) {
		ICMP6STAT_INC(icp6s_tooshort);
		return;
	}
#endif

	icmp6len -= sizeof(*nd_rs);
	nd6_option_init(nd_rs + 1, icmp6len, &ndopts);
	if (nd6_options(&ndopts) < 0) {
		nd6log((LOG_INFO,
		    "nd6_rs_input: invalid ND option, ignored\n"));
		/* nd6_options have incremented stats */
		goto freeit;
	}

	if (ndopts.nd_opts_src_lladdr) {
		lladdr = (char *)(ndopts.nd_opts_src_lladdr + 1);
		lladdrlen = ndopts.nd_opts_src_lladdr->nd_opt_len << 3;
	}

	if (lladdr && ((ifp->if_addrlen + 2 + 7) & ~7) != lladdrlen) {
		nd6log((LOG_INFO,
		    "nd6_rs_input: lladdrlen mismatch for %s "
		    "(if %d, RS packet %d)\n",
		    ip6_sprintf(ip6bufs, &saddr6),
		    ifp->if_addrlen, lladdrlen - 2));
		goto bad;
	}

	nd6_cache_lladdr(ifp, &saddr6, lladdr, lladdrlen, ND_ROUTER_SOLICIT, 0);

 freeit:
	m_freem(m);
	return;

 bad:
	ICMP6STAT_INC(icp6s_badrs);
	m_freem(m);
}

#ifdef EXPERIMENTAL
/*
 * An initial update routine for draft-ietf-6man-ipv6only-flag.
 * We need to iterate over all default routers for the given
 * interface to see whether they are all advertising the "S"
 * (IPv6-Only) flag.  If they do set, otherwise unset, the
 * interface flag we later use to filter on.
 */
static void
defrtr_ipv6_only_ifp(struct ifnet *ifp)
{
	struct nd_defrouter *dr;
	bool ipv6_only, ipv6_only_old;
#ifdef INET
	struct epoch_tracker et;
	struct ifaddr *ifa;
	bool has_ipv4_addr;
#endif

	if (V_nd6_ignore_ipv6_only_ra != 0)
		return;

	ipv6_only = true;
	ND6_RLOCK();
	TAILQ_FOREACH(dr, &V_nd_defrouter, dr_entry)
		if (dr->ifp == ifp &&
		    (dr->raflags & ND_RA_FLAG_IPV6_ONLY) == 0)
			ipv6_only = false;
	ND6_RUNLOCK();

	IF_AFDATA_WLOCK(ifp);
	ipv6_only_old = ND_IFINFO(ifp)->flags & ND6_IFF_IPV6_ONLY;
	IF_AFDATA_WUNLOCK(ifp);

	/* If nothing changed, we have an early exit. */
	if (ipv6_only == ipv6_only_old)
		return;

#ifdef INET
	/*
	 * Should we want to set the IPV6-ONLY flag, check if the
	 * interface has a non-0/0 and non-link-local IPv4 address
	 * configured on it.  If it has we will assume working
	 * IPv4 operations and will clear the interface flag.
	 */
	has_ipv4_addr = false;
	if (ipv6_only) {
		NET_EPOCH_ENTER(et);
		CK_STAILQ_FOREACH(ifa, &ifp->if_addrhead, ifa_link) {
			if (ifa->ifa_addr->sa_family != AF_INET)
				continue;
			if (in_canforward(
			    satosin(ifa->ifa_addr)->sin_addr)) {
				has_ipv4_addr = true;
				break;
			}
		}
		NET_EPOCH_EXIT(et);
	}
	if (ipv6_only && has_ipv4_addr) {
		log(LOG_NOTICE, "%s rcvd RA w/ IPv6-Only flag set but has IPv4 "
		    "configured, ignoring IPv6-Only flag.\n", ifp->if_xname);
		ipv6_only = false;
	}
#endif

	IF_AFDATA_WLOCK(ifp);
	if (ipv6_only)
		ND_IFINFO(ifp)->flags |= ND6_IFF_IPV6_ONLY;
	else
		ND_IFINFO(ifp)->flags &= ~ND6_IFF_IPV6_ONLY;
	IF_AFDATA_WUNLOCK(ifp);

#ifdef notyet
	/* Send notification of flag change. */
#endif
}

static void
defrtr_ipv6_only_ipf_down(struct ifnet *ifp)
{

	IF_AFDATA_WLOCK(ifp);
	ND_IFINFO(ifp)->flags &= ~ND6_IFF_IPV6_ONLY;
	IF_AFDATA_WUNLOCK(ifp);
}
#endif	/* EXPERIMENTAL */

void
nd6_ifnet_link_event(void *arg __unused, struct ifnet *ifp, int linkstate)
{

	/*
	 * XXX-BZ we might want to trigger re-evaluation of our default router
	 * availability. E.g., on link down the default router might be
	 * unreachable but a different interface might still have connectivity.
	 */

#ifdef EXPERIMENTAL
	if (linkstate == LINK_STATE_DOWN)
		defrtr_ipv6_only_ipf_down(ifp);
#endif
}

/*
 * Receive Router Advertisement Message.
 *
 * Based on RFC 2461
 * TODO: on-link bit on prefix information
 * TODO: ND_RA_FLAG_{OTHER,MANAGED} processing
 */
void
nd6_ra_input(struct mbuf *m, int off, int icmp6len)
{
	struct ifnet *ifp = m->m_pkthdr.rcvif;
	struct nd_ifinfo *ndi = ND_IFINFO(ifp);
	struct ip6_hdr *ip6 = mtod(m, struct ip6_hdr *);
	struct nd_router_advert *nd_ra;
	struct in6_addr saddr6 = ip6->ip6_src;
	int mcast = 0;
	union nd_opts ndopts;
	struct nd_defrouter *dr;
	char ip6bufs[INET6_ADDRSTRLEN], ip6bufd[INET6_ADDRSTRLEN];

	dr = NULL;

	/*
	 * We only accept RAs only when the per-interface flag
	 * ND6_IFF_ACCEPT_RTADV is on the receiving interface.
	 */
	if (!(ndi->flags & ND6_IFF_ACCEPT_RTADV))
		goto freeit;

	/* RFC 6980: Nodes MUST silently ignore fragments */
	if(m->m_flags & M_FRAGMENTED)
		goto freeit;

	if (ip6->ip6_hlim != 255) {
		nd6log((LOG_ERR,
		    "nd6_ra_input: invalid hlim (%d) from %s to %s on %s\n",
		    ip6->ip6_hlim, ip6_sprintf(ip6bufs, &ip6->ip6_src),
		    ip6_sprintf(ip6bufd, &ip6->ip6_dst), if_name(ifp)));
		goto bad;
	}

	if (!IN6_IS_ADDR_LINKLOCAL(&saddr6)) {
		nd6log((LOG_ERR,
		    "nd6_ra_input: src %s is not link-local\n",
		    ip6_sprintf(ip6bufs, &saddr6)));
		goto bad;
	}

#ifndef PULLDOWN_TEST
	IP6_EXTHDR_CHECK(m, off, icmp6len,);
	nd_ra = (struct nd_router_advert *)((caddr_t)ip6 + off);
#else
	IP6_EXTHDR_GET(nd_ra, struct nd_router_advert *, m, off, icmp6len);
	if (nd_ra == NULL) {
		ICMP6STAT_INC(icp6s_tooshort);
		return;
	}
#endif

	icmp6len -= sizeof(*nd_ra);
	nd6_option_init(nd_ra + 1, icmp6len, &ndopts);
	if (nd6_options(&ndopts) < 0) {
		nd6log((LOG_INFO,
		    "nd6_ra_input: invalid ND option, ignored\n"));
		/* nd6_options have incremented stats */
		goto freeit;
	}

    {
	struct nd_defrouter dr0;
	u_int32_t advreachable = nd_ra->nd_ra_reachable;

	/* remember if this is a multicasted advertisement */
	if (IN6_IS_ADDR_MULTICAST(&ip6->ip6_dst))
		mcast = 1;

	bzero(&dr0, sizeof(dr0));
	dr0.rtaddr = saddr6;
	dr0.raflags = nd_ra->nd_ra_flags_reserved;
	/*
	 * Effectively-disable routes from RA messages when
	 * ND6_IFF_NO_RADR enabled on the receiving interface or
	 * (ip6.forwarding == 1 && ip6.rfc6204w3 != 1).
	 */
	if (ndi->flags & ND6_IFF_NO_RADR)
		dr0.rtlifetime = 0;
	else if (V_ip6_forwarding && !V_ip6_rfc6204w3)
		dr0.rtlifetime = 0;
	else
		dr0.rtlifetime = ntohs(nd_ra->nd_ra_router_lifetime);
	dr0.expire = time_uptime + dr0.rtlifetime;
	dr0.ifp = ifp;
	/* unspecified or not? (RFC 2461 6.3.4) */
	if (advreachable) {
		advreachable = ntohl(advreachable);
		if (advreachable <= MAX_REACHABLE_TIME &&
		    ndi->basereachable != advreachable) {
			ndi->basereachable = advreachable;
			ndi->reachable = ND_COMPUTE_RTIME(ndi->basereachable);
			ndi->recalctm = V_nd6_recalc_reachtm_interval; /* reset */
		}
	}
	if (nd_ra->nd_ra_retransmit)
		ndi->retrans = ntohl(nd_ra->nd_ra_retransmit);
	if (nd_ra->nd_ra_curhoplimit) {
		if (ndi->chlim < nd_ra->nd_ra_curhoplimit)
			ndi->chlim = nd_ra->nd_ra_curhoplimit;
		else if (ndi->chlim != nd_ra->nd_ra_curhoplimit) {
			log(LOG_ERR, "RA with a lower CurHopLimit sent from "
			    "%s on %s (current = %d, received = %d). "
			    "Ignored.\n", ip6_sprintf(ip6bufs, &ip6->ip6_src),
			    if_name(ifp), ndi->chlim, nd_ra->nd_ra_curhoplimit);
		}
	}
	dr = defrtrlist_update(&dr0);
#ifdef EXPERIMENTAL
	defrtr_ipv6_only_ifp(ifp);
#endif
    }

	/*
	 * prefix
	 */
	if (ndopts.nd_opts_pi) {
		struct nd_opt_hdr *pt;
		struct nd_opt_prefix_info *pi = NULL;
		struct nd_prefixctl pr;

		for (pt = (struct nd_opt_hdr *)ndopts.nd_opts_pi;
		     pt <= (struct nd_opt_hdr *)ndopts.nd_opts_pi_end;
		     pt = (struct nd_opt_hdr *)((caddr_t)pt +
						(pt->nd_opt_len << 3))) {
			if (pt->nd_opt_type != ND_OPT_PREFIX_INFORMATION)
				continue;
			pi = (struct nd_opt_prefix_info *)pt;

			if (pi->nd_opt_pi_len != 4) {
				nd6log((LOG_INFO,
				    "nd6_ra_input: invalid option "
				    "len %d for prefix information option, "
				    "ignored\n", pi->nd_opt_pi_len));
				continue;
			}

			if (128 < pi->nd_opt_pi_prefix_len) {
				nd6log((LOG_INFO,
				    "nd6_ra_input: invalid prefix "
				    "len %d for prefix information option, "
				    "ignored\n", pi->nd_opt_pi_prefix_len));
				continue;
			}

			if (IN6_IS_ADDR_MULTICAST(&pi->nd_opt_pi_prefix)
			 || IN6_IS_ADDR_LINKLOCAL(&pi->nd_opt_pi_prefix)) {
				nd6log((LOG_INFO,
				    "nd6_ra_input: invalid prefix "
				    "%s, ignored\n",
				    ip6_sprintf(ip6bufs,
					&pi->nd_opt_pi_prefix)));
				continue;
			}

			bzero(&pr, sizeof(pr));
			pr.ndpr_prefix.sin6_family = AF_INET6;
			pr.ndpr_prefix.sin6_len = sizeof(pr.ndpr_prefix);
			pr.ndpr_prefix.sin6_addr = pi->nd_opt_pi_prefix;
			pr.ndpr_ifp = (struct ifnet *)m->m_pkthdr.rcvif;

			pr.ndpr_raf_onlink = (pi->nd_opt_pi_flags_reserved &
			    ND_OPT_PI_FLAG_ONLINK) ? 1 : 0;
			pr.ndpr_raf_auto = (pi->nd_opt_pi_flags_reserved &
			    ND_OPT_PI_FLAG_AUTO) ? 1 : 0;
			pr.ndpr_plen = pi->nd_opt_pi_prefix_len;
			pr.ndpr_vltime = ntohl(pi->nd_opt_pi_valid_time);
			pr.ndpr_pltime = ntohl(pi->nd_opt_pi_preferred_time);
			(void)prelist_update(&pr, dr, m, mcast);
		}
	}
	if (dr != NULL) {
		defrouter_rele(dr);
		dr = NULL;
	}

	/*
	 * MTU
	 */
	if (ndopts.nd_opts_mtu && ndopts.nd_opts_mtu->nd_opt_mtu_len == 1) {
		u_long mtu;
		u_long maxmtu;

		mtu = (u_long)ntohl(ndopts.nd_opts_mtu->nd_opt_mtu_mtu);

		/* lower bound */
		if (mtu < IPV6_MMTU) {
			nd6log((LOG_INFO, "nd6_ra_input: bogus mtu option "
			    "mtu=%lu sent from %s, ignoring\n",
			    mtu, ip6_sprintf(ip6bufs, &ip6->ip6_src)));
			goto skip;
		}

		/* upper bound */
		maxmtu = (ndi->maxmtu && ndi->maxmtu < ifp->if_mtu)
		    ? ndi->maxmtu : ifp->if_mtu;
		if (mtu <= maxmtu) {
			int change = (ndi->linkmtu != mtu);

			ndi->linkmtu = mtu;
			if (change) {
				/* in6_maxmtu may change */
				in6_setmaxmtu();
				rt_updatemtu(ifp);
			}
		} else {
			nd6log((LOG_INFO, "nd6_ra_input: bogus mtu "
			    "mtu=%lu sent from %s; "
			    "exceeds maxmtu %lu, ignoring\n",
			    mtu, ip6_sprintf(ip6bufs, &ip6->ip6_src), maxmtu));
		}
	}

 skip:

	/*
	 * Source link layer address
	 */
    {
	char *lladdr = NULL;
	int lladdrlen = 0;

	if (ndopts.nd_opts_src_lladdr) {
		lladdr = (char *)(ndopts.nd_opts_src_lladdr + 1);
		lladdrlen = ndopts.nd_opts_src_lladdr->nd_opt_len << 3;
	}

	if (lladdr && ((ifp->if_addrlen + 2 + 7) & ~7) != lladdrlen) {
		nd6log((LOG_INFO,
		    "nd6_ra_input: lladdrlen mismatch for %s "
		    "(if %d, RA packet %d)\n", ip6_sprintf(ip6bufs, &saddr6),
		    ifp->if_addrlen, lladdrlen - 2));
		goto bad;
	}

	nd6_cache_lladdr(ifp, &saddr6, lladdr,
	    lladdrlen, ND_ROUTER_ADVERT, 0);

	/*
	 * Installing a link-layer address might change the state of the
	 * router's neighbor cache, which might also affect our on-link
	 * detection of adveritsed prefixes.
	 */
	pfxlist_onlink_check();
    }

 freeit:
	m_freem(m);
	return;

 bad:
	ICMP6STAT_INC(icp6s_badra);
	m_freem(m);
}

/* tell the change to user processes watching the routing socket. */
static void
nd6_rtmsg(int cmd, struct rtentry *rt)
{
	struct rt_addrinfo info;
	struct ifnet *ifp;
	struct ifaddr *ifa;

	bzero((caddr_t)&info, sizeof(info));
	info.rti_info[RTAX_DST] = rt_key(rt);
	info.rti_info[RTAX_GATEWAY] = rt->rt_gateway;
	info.rti_info[RTAX_NETMASK] = rt_mask(rt);
	ifp = rt->rt_ifp;
	if (ifp != NULL) {
		struct epoch_tracker et;

		NET_EPOCH_ENTER(et);
		ifa = CK_STAILQ_FIRST(&ifp->if_addrhead);
		info.rti_info[RTAX_IFP] = ifa->ifa_addr;
		ifa_ref(ifa);
		NET_EPOCH_EXIT(et);
		info.rti_info[RTAX_IFA] = rt->rt_ifa->ifa_addr;
	} else
		ifa = NULL;

	rt_missmsg_fib(cmd, &info, rt->rt_flags, 0, rt->rt_fibnum);
	if (ifa != NULL)
		ifa_free(ifa);
}

/*
 * default router list processing sub routines
 */

static void
defrouter_addreq(struct nd_defrouter *new)
{
	struct sockaddr_in6 def, mask, gate;
	struct rtentry *newrt = NULL;
	int error;

	bzero(&def, sizeof(def));
	bzero(&mask, sizeof(mask));
	bzero(&gate, sizeof(gate));

	def.sin6_len = mask.sin6_len = gate.sin6_len =
	    sizeof(struct sockaddr_in6);
	def.sin6_family = gate.sin6_family = AF_INET6;
	gate.sin6_addr = new->rtaddr;

	error = in6_rtrequest(RTM_ADD, (struct sockaddr *)&def,
	    (struct sockaddr *)&gate, (struct sockaddr *)&mask,
	    RTF_GATEWAY, &newrt, new->ifp->if_fib);
	if (newrt) {
		nd6_rtmsg(RTM_ADD, newrt); /* tell user process */
		RTFREE(newrt);
	}
	if (error == 0)
		new->installed = 1;
}

struct nd_defrouter *
defrouter_lookup_locked(struct in6_addr *addr, struct ifnet *ifp)
{
	struct nd_defrouter *dr;

	ND6_LOCK_ASSERT();
	TAILQ_FOREACH(dr, &V_nd_defrouter, dr_entry)
		if (dr->ifp == ifp && IN6_ARE_ADDR_EQUAL(addr, &dr->rtaddr)) {
			defrouter_ref(dr);
			return (dr);
		}
	return (NULL);
}

struct nd_defrouter *
defrouter_lookup(struct in6_addr *addr, struct ifnet *ifp)
{
	struct nd_defrouter *dr;

	ND6_RLOCK();
	dr = defrouter_lookup_locked(addr, ifp);
	ND6_RUNLOCK();
	return (dr);
}

void
defrouter_ref(struct nd_defrouter *dr)
{

	refcount_acquire(&dr->refcnt);
}

void
defrouter_rele(struct nd_defrouter *dr)
{

	if (refcount_release(&dr->refcnt))
		free(dr, M_IP6NDP);
}

/*
 * Remove the default route for a given router.
 * This is just a subroutine function for defrouter_select_fib(), and
 * should not be called from anywhere else.
 */
static void
defrouter_delreq(struct nd_defrouter *dr)
{
	struct sockaddr_in6 def, mask, gate;
	struct rtentry *oldrt = NULL;

	bzero(&def, sizeof(def));
	bzero(&mask, sizeof(mask));
	bzero(&gate, sizeof(gate));

	def.sin6_len = mask.sin6_len = gate.sin6_len =
	    sizeof(struct sockaddr_in6);
	def.sin6_family = gate.sin6_family = AF_INET6;
	gate.sin6_addr = dr->rtaddr;

	in6_rtrequest(RTM_DELETE, (struct sockaddr *)&def,
	    (struct sockaddr *)&gate,
	    (struct sockaddr *)&mask, RTF_GATEWAY, &oldrt, dr->ifp->if_fib);
	if (oldrt) {
		nd6_rtmsg(RTM_DELETE, oldrt);
		RTFREE(oldrt);
	}

	dr->installed = 0;
}

/*
 * Remove all default routes from default router list.
 */
void
defrouter_reset(void)
{
	struct nd_defrouter *dr, **dra;
	int count, i;

	count = i = 0;

	/*
	 * We can't delete routes with the ND lock held, so make a copy of the
	 * current default router list and use that when deleting routes.
	 */
	ND6_RLOCK();
	TAILQ_FOREACH(dr, &V_nd_defrouter, dr_entry)
		count++;
	ND6_RUNLOCK();

	dra = malloc(count * sizeof(*dra), M_TEMP, M_WAITOK | M_ZERO);

	ND6_RLOCK();
	TAILQ_FOREACH(dr, &V_nd_defrouter, dr_entry) {
		if (i == count)
			break;
		defrouter_ref(dr);
		dra[i++] = dr;
	}
	ND6_RUNLOCK();

	for (i = 0; i < count && dra[i] != NULL; i++) {
		defrouter_delreq(dra[i]);
		defrouter_rele(dra[i]);
	}
	free(dra, M_TEMP);

	/*
	 * XXX should we also nuke any default routers in the kernel, by
	 * going through them by rtalloc1()?
	 */
}

/*
 * Look up a matching default router list entry and remove it. Returns true if a
 * matching entry was found, false otherwise.
 */
bool
defrouter_remove(struct in6_addr *addr, struct ifnet *ifp)
{
	struct nd_defrouter *dr;

	ND6_WLOCK();
	dr = defrouter_lookup_locked(addr, ifp);
	if (dr == NULL) {
		ND6_WUNLOCK();
		return (false);
	}

	defrouter_unlink(dr, NULL);
	ND6_WUNLOCK();
	defrouter_del(dr);
	defrouter_rele(dr);
	return (true);
}

/*
 * Remove a router from the global list and optionally stash it in a
 * caller-supplied queue.
 *
 * The ND lock must be held.
 */
void
defrouter_unlink(struct nd_defrouter *dr, struct nd_drhead *drq)
{

	ND6_WLOCK_ASSERT();
	TAILQ_REMOVE(&V_nd_defrouter, dr, dr_entry);
	V_nd6_list_genid++;
	if (drq != NULL)
		TAILQ_INSERT_TAIL(drq, dr, dr_entry);
}

void
defrouter_del(struct nd_defrouter *dr)
{
	struct nd_defrouter *deldr = NULL;
	struct nd_prefix *pr;
	struct nd_pfxrouter *pfxrtr;

	ND6_UNLOCK_ASSERT();

	/*
	 * Flush all the routing table entries that use the router
	 * as a next hop.
	 */
	if (ND_IFINFO(dr->ifp)->flags & ND6_IFF_ACCEPT_RTADV)
		rt6_flush(&dr->rtaddr, dr->ifp);

#ifdef EXPERIMENTAL
	defrtr_ipv6_only_ifp(dr->ifp);
#endif

	if (dr->installed) {
		deldr = dr;
		defrouter_delreq(dr);
	}

	/*
	 * Also delete all the pointers to the router in each prefix lists.
	 */
	ND6_WLOCK();
	LIST_FOREACH(pr, &V_nd_prefix, ndpr_entry) {
		if ((pfxrtr = pfxrtr_lookup(pr, dr)) != NULL)
			pfxrtr_del(pfxrtr);
	}
	ND6_WUNLOCK();

	pfxlist_onlink_check();

	/*
	 * If the router is the primary one, choose a new one.
	 * Note that defrouter_select_fib() will remove the current
         * gateway from the routing table.
	 */
	if (deldr)
		defrouter_select_fib(deldr->ifp->if_fib);

	/*
	 * Release the list reference.
	 */
	defrouter_rele(dr);
}

/*
 * Default Router Selection according to Section 6.3.6 of RFC 2461 and
 * draft-ietf-ipngwg-router-selection:
 * 1) Routers that are reachable or probably reachable should be preferred.
 *    If we have more than one (probably) reachable router, prefer ones
 *    with the highest router preference.
 * 2) When no routers on the list are known to be reachable or
 *    probably reachable, routers SHOULD be selected in a round-robin
 *    fashion, regardless of router preference values.
 * 3) If the Default Router List is empty, assume that all
 *    destinations are on-link.
 *
 * We assume nd_defrouter is sorted by router preference value.
 * Since the code below covers both with and without router preference cases,
 * we do not need to classify the cases by ifdef.
 *
 * At this moment, we do not try to install more than one default router,
 * even when the multipath routing is available, because we're not sure about
 * the benefits for stub hosts comparing to the risk of making the code
 * complicated and the possibility of introducing bugs.
 *
 * We maintain a single list of routers for multiple FIBs, only considering one
 * at a time based on the receiving interface's FIB. If @fibnum is RT_ALL_FIBS,
 * we do the whole thing multiple times.
 */
void
defrouter_select_fib(int fibnum)
{
	struct epoch_tracker et;
	struct nd_defrouter *dr, *selected_dr, *installed_dr;
	struct llentry *ln = NULL;

	if (fibnum == RT_ALL_FIBS) {
		for (fibnum = 0; fibnum < rt_numfibs; fibnum++) {
			defrouter_select_fib(fibnum);
		}
	}

	ND6_RLOCK();
	/*
	 * Let's handle easy case (3) first:
	 * If default router list is empty, there's nothing to be done.
	 */
	if (TAILQ_EMPTY(&V_nd_defrouter)) {
		ND6_RUNLOCK();
		return;
	}

	/*
	 * Search for a (probably) reachable router from the list.
	 * We just pick up the first reachable one (if any), assuming that
	 * the ordering rule of the list described in defrtrlist_update().
	 */
	selected_dr = installed_dr = NULL;
	TAILQ_FOREACH(dr, &V_nd_defrouter, dr_entry) {
		NET_EPOCH_ENTER(et);
		if (selected_dr == NULL && dr->ifp->if_fib == fibnum &&
		    (ln = nd6_lookup(&dr->rtaddr, 0, dr->ifp)) &&
		    ND6_IS_LLINFO_PROBREACH(ln)) {
			selected_dr = dr;
			defrouter_ref(selected_dr);
		}
		NET_EPOCH_EXIT(et);
		if (ln != NULL) {
			LLE_RUNLOCK(ln);
			ln = NULL;
		}

		if (dr->installed && dr->ifp->if_fib == fibnum) {
			if (installed_dr == NULL) {
				installed_dr = dr;
				defrouter_ref(installed_dr);
			} else {
				/*
				 * this should not happen.
				 * warn for diagnosis.
				 */
				log(LOG_ERR, "defrouter_select_fib: more than "
				             "one router is installed\n");
			}
		}
	}
	/*
	 * If none of the default routers was found to be reachable,
	 * round-robin the list regardless of preference.
	 * Otherwise, if we have an installed router, check if the selected
	 * (reachable) router should really be preferred to the installed one.
	 * We only prefer the new router when the old one is not reachable
	 * or when the new one has a really higher preference value.
	 */
	if (selected_dr == NULL) {
		if (installed_dr == NULL ||
		    TAILQ_NEXT(installed_dr, dr_entry) == NULL)
			dr = TAILQ_FIRST(&V_nd_defrouter);
		else
			dr = TAILQ_NEXT(installed_dr, dr_entry);

		/* Ensure we select a router for this FIB. */
		TAILQ_FOREACH_FROM(dr, &V_nd_defrouter, dr_entry) {
			if (dr->ifp->if_fib == fibnum) {
				selected_dr = dr;
				defrouter_ref(selected_dr);
				break;
			}
		}
	} else if (installed_dr != NULL) {
		NET_EPOCH_ENTER(et);
		if ((ln = nd6_lookup(&installed_dr->rtaddr, 0,
		                     installed_dr->ifp)) &&
		    ND6_IS_LLINFO_PROBREACH(ln) &&
		    installed_dr->ifp->if_fib == fibnum &&
		    rtpref(selected_dr) <= rtpref(installed_dr)) {
			defrouter_rele(selected_dr);
			selected_dr = installed_dr;
		}
		NET_EPOCH_EXIT(et);
		if (ln != NULL)
			LLE_RUNLOCK(ln);
	}
	ND6_RUNLOCK();

	/*
	 * If we selected a router for this FIB and it's different
	 * than the installed one, remove the installed router and
	 * install the selected one in its place.
	 */
	if (installed_dr != selected_dr) {
		if (installed_dr != NULL) {
			defrouter_delreq(installed_dr);
			defrouter_rele(installed_dr);
		}
		if (selected_dr != NULL)
			defrouter_addreq(selected_dr);
	}
	if (selected_dr != NULL)
		defrouter_rele(selected_dr);
}

/*
 * Maintain old KPI for default router selection.
 * If unspecified, we can re-select routers for all FIBs.
 */
void
defrouter_select(void)
{
	defrouter_select_fib(RT_ALL_FIBS);
}

/*
 * for default router selection
 * regards router-preference field as a 2-bit signed integer
 */
static int
rtpref(struct nd_defrouter *dr)
{
	switch (dr->raflags & ND_RA_FLAG_RTPREF_MASK) {
	case ND_RA_FLAG_RTPREF_HIGH:
		return (RTPREF_HIGH);
	case ND_RA_FLAG_RTPREF_MEDIUM:
	case ND_RA_FLAG_RTPREF_RSV:
		return (RTPREF_MEDIUM);
	case ND_RA_FLAG_RTPREF_LOW:
		return (RTPREF_LOW);
	default:
		/*
		 * This case should never happen.  If it did, it would mean a
		 * serious bug of kernel internal.  We thus always bark here.
		 * Or, can we even panic?
		 */
		log(LOG_ERR, "rtpref: impossible RA flag %x\n", dr->raflags);
		return (RTPREF_INVALID);
	}
	/* NOTREACHED */
}

static struct nd_defrouter *
defrtrlist_update(struct nd_defrouter *new)
{
	struct nd_defrouter *dr, *n;
	uint64_t genid;
	int oldpref;
	bool writelocked;

	if (new->rtlifetime == 0) {
		defrouter_remove(&new->rtaddr, new->ifp);
		return (NULL);
	}

	ND6_RLOCK();
	writelocked = false;
restart:
	dr = defrouter_lookup_locked(&new->rtaddr, new->ifp);
	if (dr != NULL) {
		oldpref = rtpref(dr);

		/* override */
		dr->raflags = new->raflags; /* XXX flag check */
		dr->rtlifetime = new->rtlifetime;
		dr->expire = new->expire;

		/*
		 * If the preference does not change, there's no need
		 * to sort the entries. Also make sure the selected
		 * router is still installed in the kernel.
		 */
		if (dr->installed && rtpref(new) == oldpref) {
			if (writelocked)
				ND6_WUNLOCK();
			else
				ND6_RUNLOCK();
			return (dr);
		}
	}

	/*
	 * The router needs to be reinserted into the default router
	 * list, so upgrade to a write lock. If that fails and the list
	 * has potentially changed while the lock was dropped, we'll
	 * redo the lookup with the write lock held.
	 */
	if (!writelocked) {
		writelocked = true;
		if (!ND6_TRY_UPGRADE()) {
			genid = V_nd6_list_genid;
			ND6_RUNLOCK();
			ND6_WLOCK();
			if (genid != V_nd6_list_genid)
				goto restart;
		}
	}

	if (dr != NULL) {
		/*
		 * The preferred router may have changed, so relocate this
		 * router.
		 */
		TAILQ_REMOVE(&V_nd_defrouter, dr, dr_entry);
		n = dr;
	} else {
		n = malloc(sizeof(*n), M_IP6NDP, M_NOWAIT | M_ZERO);
		if (n == NULL) {
			ND6_WUNLOCK();
			return (NULL);
		}
		memcpy(n, new, sizeof(*n));
		/* Initialize with an extra reference for the caller. */
		refcount_init(&n->refcnt, 2);
	}

	/*
	 * Insert the new router in the Default Router List;
	 * The Default Router List should be in the descending order
	 * of router-preferece.  Routers with the same preference are
	 * sorted in the arriving time order.
	 */

	/* insert at the end of the group */
	TAILQ_FOREACH(dr, &V_nd_defrouter, dr_entry) {
		if (rtpref(n) > rtpref(dr))
			break;
	}
	if (dr != NULL)
		TAILQ_INSERT_BEFORE(dr, n, dr_entry);
	else
		TAILQ_INSERT_TAIL(&V_nd_defrouter, n, dr_entry);
	V_nd6_list_genid++;
	ND6_WUNLOCK();

	defrouter_select_fib(new->ifp->if_fib);

	return (n);
}

static struct nd_pfxrouter *
pfxrtr_lookup(struct nd_prefix *pr, struct nd_defrouter *dr)
{
	struct nd_pfxrouter *search;

	ND6_LOCK_ASSERT();

	LIST_FOREACH(search, &pr->ndpr_advrtrs, pfr_entry) {
		if (search->router == dr)
			break;
	}
	return (search);
}

static void
pfxrtr_add(struct nd_prefix *pr, struct nd_defrouter *dr)
{
	struct nd_pfxrouter *new;
	bool update;

	ND6_UNLOCK_ASSERT();

	ND6_RLOCK();
	if (pfxrtr_lookup(pr, dr) != NULL) {
		ND6_RUNLOCK();
		return;
	}
	ND6_RUNLOCK();

	new = malloc(sizeof(*new), M_IP6NDP, M_NOWAIT | M_ZERO);
	if (new == NULL)
		return;
	defrouter_ref(dr);
	new->router = dr;

	ND6_WLOCK();
	if (pfxrtr_lookup(pr, dr) == NULL) {
		LIST_INSERT_HEAD(&pr->ndpr_advrtrs, new, pfr_entry);
		update = true;
	} else {
		/* We lost a race to add the reference. */
		defrouter_rele(dr);
		free(new, M_IP6NDP);
		update = false;
	}
	ND6_WUNLOCK();

	if (update)
		pfxlist_onlink_check();
}

static void
pfxrtr_del(struct nd_pfxrouter *pfr)
{

	ND6_WLOCK_ASSERT();

	LIST_REMOVE(pfr, pfr_entry);
	defrouter_rele(pfr->router);
	free(pfr, M_IP6NDP);
}

static struct nd_prefix *
nd6_prefix_lookup_locked(struct nd_prefixctl *key)
{
	struct nd_prefix *search;

	ND6_LOCK_ASSERT();

	LIST_FOREACH(search, &V_nd_prefix, ndpr_entry) {
		if (key->ndpr_ifp == search->ndpr_ifp &&
		    key->ndpr_plen == search->ndpr_plen &&
		    in6_are_prefix_equal(&key->ndpr_prefix.sin6_addr,
		    &search->ndpr_prefix.sin6_addr, key->ndpr_plen)) {
			nd6_prefix_ref(search);
			break;
		}
	}
	return (search);
}

struct nd_prefix *
nd6_prefix_lookup(struct nd_prefixctl *key)
{
	struct nd_prefix *search;

	ND6_RLOCK();
	search = nd6_prefix_lookup_locked(key);
	ND6_RUNLOCK();
	return (search);
}

void
nd6_prefix_ref(struct nd_prefix *pr)
{

	refcount_acquire(&pr->ndpr_refcnt);
}

void
nd6_prefix_rele(struct nd_prefix *pr)
{

	if (refcount_release(&pr->ndpr_refcnt)) {
		KASSERT(LIST_EMPTY(&pr->ndpr_advrtrs),
		    ("prefix %p has advertising routers", pr));
		free(pr, M_IP6NDP);
	}
}

int
nd6_prelist_add(struct nd_prefixctl *pr, struct nd_defrouter *dr,
    struct nd_prefix **newp)
{
	struct nd_prefix *new;
	char ip6buf[INET6_ADDRSTRLEN];
	int error;

	new = malloc(sizeof(*new), M_IP6NDP, M_NOWAIT | M_ZERO);
	if (new == NULL)
		return (ENOMEM);
	refcount_init(&new->ndpr_refcnt, newp != NULL ? 2 : 1);
	new->ndpr_ifp = pr->ndpr_ifp;
	new->ndpr_prefix = pr->ndpr_prefix;
	new->ndpr_plen = pr->ndpr_plen;
	new->ndpr_vltime = pr->ndpr_vltime;
	new->ndpr_pltime = pr->ndpr_pltime;
	new->ndpr_flags = pr->ndpr_flags;
	if ((error = in6_init_prefix_ltimes(new)) != 0) {
		free(new, M_IP6NDP);
		return (error);
	}
	new->ndpr_lastupdate = time_uptime;

	/* initialization */
	LIST_INIT(&new->ndpr_advrtrs);
	in6_prefixlen2mask(&new->ndpr_mask, new->ndpr_plen);
	/* make prefix in the canonical form */
	IN6_MASK_ADDR(&new->ndpr_prefix.sin6_addr, &new->ndpr_mask);

	ND6_WLOCK();
	LIST_INSERT_HEAD(&V_nd_prefix, new, ndpr_entry);
	V_nd6_list_genid++;
	ND6_WUNLOCK();

	/* ND_OPT_PI_FLAG_ONLINK processing */
	if (new->ndpr_raf_onlink) {
		ND6_ONLINK_LOCK();
		if ((error = nd6_prefix_onlink(new)) != 0) {
			nd6log((LOG_ERR, "nd6_prelist_add: failed to make "
			    "the prefix %s/%d on-link on %s (errno=%d)\n",
			    ip6_sprintf(ip6buf, &pr->ndpr_prefix.sin6_addr),
			    pr->ndpr_plen, if_name(pr->ndpr_ifp), error));
			/* proceed anyway. XXX: is it correct? */
		}
		ND6_ONLINK_UNLOCK();
	}

	if (dr != NULL)
		pfxrtr_add(new, dr);
	if (newp != NULL)
		*newp = new;
	return (0);
}

/*
 * Remove a prefix from the prefix list and optionally stash it in a
 * caller-provided list.
 *
 * The ND6 lock must be held.
 */
void
nd6_prefix_unlink(struct nd_prefix *pr, struct nd_prhead *list)
{

	ND6_WLOCK_ASSERT();

	LIST_REMOVE(pr, ndpr_entry);
	V_nd6_list_genid++;
	if (list != NULL)
		LIST_INSERT_HEAD(list, pr, ndpr_entry);
}

/*
 * Free an unlinked prefix, first marking it off-link if necessary.
 */
void
nd6_prefix_del(struct nd_prefix *pr)
{
	struct nd_pfxrouter *pfr, *next;
	int e;
	char ip6buf[INET6_ADDRSTRLEN];

	KASSERT(pr->ndpr_addrcnt == 0,
	    ("prefix %p has referencing addresses", pr));
	ND6_UNLOCK_ASSERT();

	/*
	 * Though these flags are now meaningless, we'd rather keep the value
	 * of pr->ndpr_raf_onlink and pr->ndpr_raf_auto not to confuse users
	 * when executing "ndp -p".
	 */
	if ((pr->ndpr_stateflags & NDPRF_ONLINK) != 0) {
		ND6_ONLINK_LOCK();
		if ((e = nd6_prefix_offlink(pr)) != 0) {
			nd6log((LOG_ERR,
			    "nd6_prefix_del: failed to make %s/%d offlink "
			    "on %s, errno=%d\n",
			    ip6_sprintf(ip6buf, &pr->ndpr_prefix.sin6_addr),
			    pr->ndpr_plen, if_name(pr->ndpr_ifp), e));
			/* what should we do? */
		}
		ND6_ONLINK_UNLOCK();
	}

	/* Release references to routers that have advertised this prefix. */
	ND6_WLOCK();
	LIST_FOREACH_SAFE(pfr, &pr->ndpr_advrtrs, pfr_entry, next)
		pfxrtr_del(pfr);
	ND6_WUNLOCK();

	nd6_prefix_rele(pr);

	pfxlist_onlink_check();
}

static int
prelist_update(struct nd_prefixctl *new, struct nd_defrouter *dr,
    struct mbuf *m, int mcast)
{
	struct in6_ifaddr *ia6 = NULL, *ia6_match = NULL;
	struct ifaddr *ifa;
	struct ifnet *ifp = new->ndpr_ifp;
	struct nd_prefix *pr;
	int error = 0;
	int auth;
	struct in6_addrlifetime lt6_tmp;
	char ip6buf[INET6_ADDRSTRLEN];
	struct epoch_tracker et;

	auth = 0;
	if (m) {
		/*
		 * Authenticity for NA consists authentication for
		 * both IP header and IP datagrams, doesn't it ?
		 */
#if defined(M_AUTHIPHDR) && defined(M_AUTHIPDGM)
		auth = ((m->m_flags & M_AUTHIPHDR) &&
		    (m->m_flags & M_AUTHIPDGM));
#endif
	}

	if ((pr = nd6_prefix_lookup(new)) != NULL) {
		/*
		 * nd6_prefix_lookup() ensures that pr and new have the same
		 * prefix on a same interface.
		 */

		/*
		 * Update prefix information.  Note that the on-link (L) bit
		 * and the autonomous (A) bit should NOT be changed from 1
		 * to 0.
		 */
		if (new->ndpr_raf_onlink == 1)
			pr->ndpr_raf_onlink = 1;
		if (new->ndpr_raf_auto == 1)
			pr->ndpr_raf_auto = 1;
		if (new->ndpr_raf_onlink) {
			pr->ndpr_vltime = new->ndpr_vltime;
			pr->ndpr_pltime = new->ndpr_pltime;
			(void)in6_init_prefix_ltimes(pr); /* XXX error case? */
			pr->ndpr_lastupdate = time_uptime;
		}

		if (new->ndpr_raf_onlink &&
		    (pr->ndpr_stateflags & NDPRF_ONLINK) == 0) {
			ND6_ONLINK_LOCK();
			if ((error = nd6_prefix_onlink(pr)) != 0) {
				nd6log((LOG_ERR,
				    "prelist_update: failed to make "
				    "the prefix %s/%d on-link on %s "
				    "(errno=%d)\n",
				    ip6_sprintf(ip6buf,
				        &pr->ndpr_prefix.sin6_addr),
				    pr->ndpr_plen, if_name(pr->ndpr_ifp),
				    error));
				/* proceed anyway. XXX: is it correct? */
			}
			ND6_ONLINK_UNLOCK();
		}

		if (dr != NULL)
			pfxrtr_add(pr, dr);
	} else {
		if (new->ndpr_vltime == 0)
			goto end;
		if (new->ndpr_raf_onlink == 0 && new->ndpr_raf_auto == 0)
			goto end;

		error = nd6_prelist_add(new, dr, &pr);
		if (error != 0) {
			nd6log((LOG_NOTICE, "prelist_update: "
			    "nd6_prelist_add failed for %s/%d on %s errno=%d\n",
			    ip6_sprintf(ip6buf, &new->ndpr_prefix.sin6_addr),
			    new->ndpr_plen, if_name(new->ndpr_ifp), error));
			goto end; /* we should just give up in this case. */
		}

		/*
		 * XXX: from the ND point of view, we can ignore a prefix
		 * with the on-link bit being zero.  However, we need a
		 * prefix structure for references from autoconfigured
		 * addresses.  Thus, we explicitly make sure that the prefix
		 * itself expires now.
		 */
		if (pr->ndpr_raf_onlink == 0) {
			pr->ndpr_vltime = 0;
			pr->ndpr_pltime = 0;
			in6_init_prefix_ltimes(pr);
		}
	}

	/*
	 * Address autoconfiguration based on Section 5.5.3 of RFC 2462.
	 * Note that pr must be non NULL at this point.
	 */

	/* 5.5.3 (a). Ignore the prefix without the A bit set. */
	if (!new->ndpr_raf_auto)
		goto end;

	/*
	 * 5.5.3 (b). the link-local prefix should have been ignored in
	 * nd6_ra_input.
	 */

	/* 5.5.3 (c). Consistency check on lifetimes: pltime <= vltime. */
	if (new->ndpr_pltime > new->ndpr_vltime) {
		error = EINVAL;	/* XXX: won't be used */
		goto end;
	}

	/*
	 * 5.5.3 (d).  If the prefix advertised is not equal to the prefix of
	 * an address configured by stateless autoconfiguration already in the
	 * list of addresses associated with the interface, and the Valid
	 * Lifetime is not 0, form an address.  We first check if we have
	 * a matching prefix.
	 * Note: we apply a clarification in rfc2462bis-02 here.  We only
	 * consider autoconfigured addresses while RFC2462 simply said
	 * "address".
	 */
	NET_EPOCH_ENTER(et);
	CK_STAILQ_FOREACH(ifa, &ifp->if_addrhead, ifa_link) {
		struct in6_ifaddr *ifa6;
		u_int32_t remaininglifetime;

		if (ifa->ifa_addr->sa_family != AF_INET6)
			continue;

		ifa6 = (struct in6_ifaddr *)ifa;

		/*
		 * We only consider autoconfigured addresses as per rfc2462bis.
		 */
		if (!(ifa6->ia6_flags & IN6_IFF_AUTOCONF))
			continue;

		/*
		 * Spec is not clear here, but I believe we should concentrate
		 * on unicast (i.e. not anycast) addresses.
		 * XXX: other ia6_flags? detached or duplicated?
		 */
		if ((ifa6->ia6_flags & IN6_IFF_ANYCAST) != 0)
			continue;

		/*
		 * Ignore the address if it is not associated with a prefix
		 * or is associated with a prefix that is different from this
		 * one.  (pr is never NULL here)
		 */
		if (ifa6->ia6_ndpr != pr)
			continue;

		if (ia6_match == NULL) /* remember the first one */
			ia6_match = ifa6;

		/*
		 * An already autoconfigured address matched.  Now that we
		 * are sure there is at least one matched address, we can
		 * proceed to 5.5.3. (e): update the lifetimes according to the
		 * "two hours" rule and the privacy extension.
		 * We apply some clarifications in rfc2462bis:
		 * - use remaininglifetime instead of storedlifetime as a
		 *   variable name
		 * - remove the dead code in the "two-hour" rule
		 */
#define TWOHOUR		(120*60)
		lt6_tmp = ifa6->ia6_lifetime;

		if (lt6_tmp.ia6t_vltime == ND6_INFINITE_LIFETIME)
			remaininglifetime = ND6_INFINITE_LIFETIME;
		else if (time_uptime - ifa6->ia6_updatetime >
			 lt6_tmp.ia6t_vltime) {
			/*
			 * The case of "invalid" address.  We should usually
			 * not see this case.
			 */
			remaininglifetime = 0;
		} else
			remaininglifetime = lt6_tmp.ia6t_vltime -
			    (time_uptime - ifa6->ia6_updatetime);

		/* when not updating, keep the current stored lifetime. */
		lt6_tmp.ia6t_vltime = remaininglifetime;

		if (TWOHOUR < new->ndpr_vltime ||
		    remaininglifetime < new->ndpr_vltime) {
			lt6_tmp.ia6t_vltime = new->ndpr_vltime;
		} else if (remaininglifetime <= TWOHOUR) {
			if (auth) {
				lt6_tmp.ia6t_vltime = new->ndpr_vltime;
			}
		} else {
			/*
			 * new->ndpr_vltime <= TWOHOUR &&
			 * TWOHOUR < remaininglifetime
			 */
			lt6_tmp.ia6t_vltime = TWOHOUR;
		}

		/* The 2 hour rule is not imposed for preferred lifetime. */
		lt6_tmp.ia6t_pltime = new->ndpr_pltime;

		in6_init_address_ltimes(pr, &lt6_tmp);

		/*
		 * We need to treat lifetimes for temporary addresses
		 * differently, according to
		 * draft-ietf-ipv6-privacy-addrs-v2-01.txt 3.3 (1);
		 * we only update the lifetimes when they are in the maximum
		 * intervals.
		 */
		if ((ifa6->ia6_flags & IN6_IFF_TEMPORARY) != 0) {
			u_int32_t maxvltime, maxpltime;

			if (V_ip6_temp_valid_lifetime >
			    (u_int32_t)((time_uptime - ifa6->ia6_createtime) +
			    V_ip6_desync_factor)) {
				maxvltime = V_ip6_temp_valid_lifetime -
				    (time_uptime - ifa6->ia6_createtime) -
				    V_ip6_desync_factor;
			} else
				maxvltime = 0;
			if (V_ip6_temp_preferred_lifetime >
			    (u_int32_t)((time_uptime - ifa6->ia6_createtime) +
			    V_ip6_desync_factor)) {
				maxpltime = V_ip6_temp_preferred_lifetime -
				    (time_uptime - ifa6->ia6_createtime) -
				    V_ip6_desync_factor;
			} else
				maxpltime = 0;

			if (lt6_tmp.ia6t_vltime == ND6_INFINITE_LIFETIME ||
			    lt6_tmp.ia6t_vltime > maxvltime) {
				lt6_tmp.ia6t_vltime = maxvltime;
			}
			if (lt6_tmp.ia6t_pltime == ND6_INFINITE_LIFETIME ||
			    lt6_tmp.ia6t_pltime > maxpltime) {
				lt6_tmp.ia6t_pltime = maxpltime;
			}
		}
		ifa6->ia6_lifetime = lt6_tmp;
		ifa6->ia6_updatetime = time_uptime;
	}
	NET_EPOCH_EXIT(et);
	if (ia6_match == NULL && new->ndpr_vltime) {
		int ifidlen;

		/*
		 * 5.5.3 (d) (continued)
		 * No address matched and the valid lifetime is non-zero.
		 * Create a new address.
		 */

		/*
		 * Prefix Length check:
		 * If the sum of the prefix length and interface identifier
		 * length does not equal 128 bits, the Prefix Information
		 * option MUST be ignored.  The length of the interface
		 * identifier is defined in a separate link-type specific
		 * document.
		 */
		ifidlen = in6_if2idlen(ifp);
		if (ifidlen < 0) {
			/* this should not happen, so we always log it. */
			log(LOG_ERR, "prelist_update: IFID undefined (%s)\n",
			    if_name(ifp));
			goto end;
		}
		if (ifidlen + pr->ndpr_plen != 128) {
			nd6log((LOG_INFO,
			    "prelist_update: invalid prefixlen "
			    "%d for %s, ignored\n",
			    pr->ndpr_plen, if_name(ifp)));
			goto end;
		}

		if ((ia6 = in6_ifadd(new, mcast)) != NULL) {
			/*
			 * note that we should use pr (not new) for reference.
			 */
			pr->ndpr_addrcnt++;
			ia6->ia6_ndpr = pr;

			/*
			 * RFC 3041 3.3 (2).
			 * When a new public address is created as described
			 * in RFC2462, also create a new temporary address.
			 *
			 * RFC 3041 3.5.
			 * When an interface connects to a new link, a new
			 * randomized interface identifier should be generated
			 * immediately together with a new set of temporary
			 * addresses.  Thus, we specifiy 1 as the 2nd arg of
			 * in6_tmpifadd().
			 */
			if (V_ip6_use_tempaddr) {
				int e;
				if ((e = in6_tmpifadd(ia6, 1, 1)) != 0) {
					nd6log((LOG_NOTICE, "prelist_update: "
					    "failed to create a temporary "
					    "address, errno=%d\n",
					    e));
				}
			}
			ifa_free(&ia6->ia_ifa);

			/*
			 * A newly added address might affect the status
			 * of other addresses, so we check and update it.
			 * XXX: what if address duplication happens?
			 */
			pfxlist_onlink_check();
		} else {
			/* just set an error. do not bark here. */
			error = EADDRNOTAVAIL; /* XXX: might be unused. */
		}
	}

end:
	if (pr != NULL)
		nd6_prefix_rele(pr);
	return (error);
}

/*
 * A supplement function used in the on-link detection below;
 * detect if a given prefix has a (probably) reachable advertising router.
 * XXX: lengthy function name...
 */
static struct nd_pfxrouter *
find_pfxlist_reachable_router(struct nd_prefix *pr)
{
	struct epoch_tracker et;
	struct nd_pfxrouter *pfxrtr;
	struct llentry *ln;
	int canreach;

	ND6_LOCK_ASSERT();

	LIST_FOREACH(pfxrtr, &pr->ndpr_advrtrs, pfr_entry) {
		NET_EPOCH_ENTER(et);
		ln = nd6_lookup(&pfxrtr->router->rtaddr, 0, pfxrtr->router->ifp);
		NET_EPOCH_EXIT(et);
		if (ln == NULL)
			continue;
		canreach = ND6_IS_LLINFO_PROBREACH(ln);
		LLE_RUNLOCK(ln);
		if (canreach)
			break;
	}
	return (pfxrtr);
}

/*
 * Check if each prefix in the prefix list has at least one available router
 * that advertised the prefix (a router is "available" if its neighbor cache
 * entry is reachable or probably reachable).
 * If the check fails, the prefix may be off-link, because, for example,
 * we have moved from the network but the lifetime of the prefix has not
 * expired yet.  So we should not use the prefix if there is another prefix
 * that has an available router.
 * But, if there is no prefix that has an available router, we still regard
 * all the prefixes as on-link.  This is because we can't tell if all the
 * routers are simply dead or if we really moved from the network and there
 * is no router around us.
 */
void
pfxlist_onlink_check(void)
{
	struct nd_prefix *pr;
	struct in6_ifaddr *ifa;
	struct nd_defrouter *dr;
	struct nd_pfxrouter *pfxrtr = NULL;
	struct rm_priotracker in6_ifa_tracker;
	uint64_t genid;
	uint32_t flags;

	ND6_ONLINK_LOCK();
	ND6_RLOCK();

	/*
	 * Check if there is a prefix that has a reachable advertising
	 * router.
	 */
	LIST_FOREACH(pr, &V_nd_prefix, ndpr_entry) {
		if (pr->ndpr_raf_onlink && find_pfxlist_reachable_router(pr))
			break;
	}

	/*
	 * If we have no such prefix, check whether we still have a router
	 * that does not advertise any prefixes.
	 */
	if (pr == NULL) {
		TAILQ_FOREACH(dr, &V_nd_defrouter, dr_entry) {
			struct nd_prefix *pr0;

			LIST_FOREACH(pr0, &V_nd_prefix, ndpr_entry) {
				if ((pfxrtr = pfxrtr_lookup(pr0, dr)) != NULL)
					break;
			}
			if (pfxrtr != NULL)
				break;
		}
	}
	if (pr != NULL || (!TAILQ_EMPTY(&V_nd_defrouter) && pfxrtr == NULL)) {
		/*
		 * There is at least one prefix that has a reachable router,
		 * or at least a router which probably does not advertise
		 * any prefixes.  The latter would be the case when we move
		 * to a new link where we have a router that does not provide
		 * prefixes and we configure an address by hand.
		 * Detach prefixes which have no reachable advertising
		 * router, and attach other prefixes.
		 */
		LIST_FOREACH(pr, &V_nd_prefix, ndpr_entry) {
			/* XXX: a link-local prefix should never be detached */
			if (IN6_IS_ADDR_LINKLOCAL(&pr->ndpr_prefix.sin6_addr) ||
			    pr->ndpr_raf_onlink == 0 ||
			    pr->ndpr_raf_auto == 0)
				continue;

			if ((pr->ndpr_stateflags & NDPRF_DETACHED) == 0 &&
			    find_pfxlist_reachable_router(pr) == NULL)
				pr->ndpr_stateflags |= NDPRF_DETACHED;
			else if ((pr->ndpr_stateflags & NDPRF_DETACHED) != 0 &&
			    find_pfxlist_reachable_router(pr) != NULL)
				pr->ndpr_stateflags &= ~NDPRF_DETACHED;
		}
	} else {
		/* there is no prefix that has a reachable router */
		LIST_FOREACH(pr, &V_nd_prefix, ndpr_entry) {
			if (IN6_IS_ADDR_LINKLOCAL(&pr->ndpr_prefix.sin6_addr) ||
			    pr->ndpr_raf_onlink == 0 ||
			    pr->ndpr_raf_auto == 0)
				continue;
			pr->ndpr_stateflags &= ~NDPRF_DETACHED;
		}
	}

	/*
	 * Remove each interface route associated with a (just) detached
	 * prefix, and reinstall the interface route for a (just) attached
	 * prefix.  Note that all attempt of reinstallation does not
	 * necessarily success, when a same prefix is shared among multiple
	 * interfaces.  Such cases will be handled in nd6_prefix_onlink,
	 * so we don't have to care about them.
	 */
restart:
	LIST_FOREACH(pr, &V_nd_prefix, ndpr_entry) {
		char ip6buf[INET6_ADDRSTRLEN];
		int e;

		if (IN6_IS_ADDR_LINKLOCAL(&pr->ndpr_prefix.sin6_addr) ||
		    pr->ndpr_raf_onlink == 0 ||
		    pr->ndpr_raf_auto == 0)
			continue;

		flags = pr->ndpr_stateflags & (NDPRF_DETACHED | NDPRF_ONLINK);
		if (flags == 0 || flags == (NDPRF_DETACHED | NDPRF_ONLINK)) {
			genid = V_nd6_list_genid;
			ND6_RUNLOCK();
			if ((flags & NDPRF_ONLINK) != 0 &&
			    (e = nd6_prefix_offlink(pr)) != 0) {
				nd6log((LOG_ERR,
				    "pfxlist_onlink_check: failed to "
				    "make %s/%d offlink, errno=%d\n",
				    ip6_sprintf(ip6buf,
					    &pr->ndpr_prefix.sin6_addr),
					    pr->ndpr_plen, e));
			} else if ((flags & NDPRF_ONLINK) == 0 &&
			    (e = nd6_prefix_onlink(pr)) != 0) {
				nd6log((LOG_ERR,
				    "pfxlist_onlink_check: failed to "
				    "make %s/%d onlink, errno=%d\n",
				    ip6_sprintf(ip6buf,
					    &pr->ndpr_prefix.sin6_addr),
					    pr->ndpr_plen, e));
			}
			ND6_RLOCK();
			if (genid != V_nd6_list_genid)
				goto restart;
		}
	}

	/*
	 * Changes on the prefix status might affect address status as well.
	 * Make sure that all addresses derived from an attached prefix are
	 * attached, and that all addresses derived from a detached prefix are
	 * detached.  Note, however, that a manually configured address should
	 * always be attached.
	 * The precise detection logic is same as the one for prefixes.
	 */
	IN6_IFADDR_RLOCK(&in6_ifa_tracker);
	CK_STAILQ_FOREACH(ifa, &V_in6_ifaddrhead, ia_link) {
		if (!(ifa->ia6_flags & IN6_IFF_AUTOCONF))
			continue;

		if (ifa->ia6_ndpr == NULL) {
			/*
			 * This can happen when we first configure the address
			 * (i.e. the address exists, but the prefix does not).
			 * XXX: complicated relationships...
			 */
			continue;
		}

		if (find_pfxlist_reachable_router(ifa->ia6_ndpr))
			break;
	}
	if (ifa) {
		CK_STAILQ_FOREACH(ifa, &V_in6_ifaddrhead, ia_link) {
			if ((ifa->ia6_flags & IN6_IFF_AUTOCONF) == 0)
				continue;

			if (ifa->ia6_ndpr == NULL) /* XXX: see above. */
				continue;

			if (find_pfxlist_reachable_router(ifa->ia6_ndpr)) {
				if (ifa->ia6_flags & IN6_IFF_DETACHED) {
					ifa->ia6_flags &= ~IN6_IFF_DETACHED;
					ifa->ia6_flags |= IN6_IFF_TENTATIVE;
					nd6_dad_start((struct ifaddr *)ifa, 0);
				}
			} else {
				ifa->ia6_flags |= IN6_IFF_DETACHED;
			}
		}
	} else {
		CK_STAILQ_FOREACH(ifa, &V_in6_ifaddrhead, ia_link) {
			if ((ifa->ia6_flags & IN6_IFF_AUTOCONF) == 0)
				continue;

			if (ifa->ia6_flags & IN6_IFF_DETACHED) {
				ifa->ia6_flags &= ~IN6_IFF_DETACHED;
				ifa->ia6_flags |= IN6_IFF_TENTATIVE;
				/* Do we need a delay in this case? */
				nd6_dad_start((struct ifaddr *)ifa, 0);
			}
		}
	}
	IN6_IFADDR_RUNLOCK(&in6_ifa_tracker);
	ND6_RUNLOCK();
	ND6_ONLINK_UNLOCK();
}

static int
nd6_prefix_onlink_rtrequest(struct nd_prefix *pr, struct ifaddr *ifa)
{
	static struct sockaddr_dl null_sdl = {sizeof(null_sdl), AF_LINK};
	struct rib_head *rnh;
	struct rtentry *rt;
	struct sockaddr_in6 mask6;
	u_long rtflags;
	int error, a_failure, fibnum, maxfib;

	/*
	 * in6_ifinit() sets nd6_rtrequest to ifa_rtrequest for all ifaddrs.
	 * ifa->ifa_rtrequest = nd6_rtrequest;
	 */
	bzero(&mask6, sizeof(mask6));
	mask6.sin6_len = sizeof(mask6);
	mask6.sin6_addr = pr->ndpr_mask;
	rtflags = (ifa->ifa_flags & ~IFA_RTSELF) | RTF_UP;

	if(V_rt_add_addr_allfibs) {
		fibnum = 0;
		maxfib = rt_numfibs;
	} else {
		fibnum = ifa->ifa_ifp->if_fib;
		maxfib = fibnum + 1;
	}
	a_failure = 0;
	for (; fibnum < maxfib; fibnum++) {

		rt = NULL;
		error = in6_rtrequest(RTM_ADD,
		    (struct sockaddr *)&pr->ndpr_prefix, ifa->ifa_addr,
		    (struct sockaddr *)&mask6, rtflags, &rt, fibnum);
		if (error == 0) {
			KASSERT(rt != NULL, ("%s: in6_rtrequest return no "
			    "error(%d) but rt is NULL, pr=%p, ifa=%p", __func__,
			    error, pr, ifa));

			rnh = rt_tables_get_rnh(rt->rt_fibnum, AF_INET6);
			/* XXX what if rhn == NULL? */
			RIB_WLOCK(rnh);
			RT_LOCK(rt);
			if (rt_setgate(rt, rt_key(rt),
			    (struct sockaddr *)&null_sdl) == 0) {
				struct sockaddr_dl *dl;

				dl = (struct sockaddr_dl *)rt->rt_gateway;
				dl->sdl_type = rt->rt_ifp->if_type;
				dl->sdl_index = rt->rt_ifp->if_index;
			}
			RIB_WUNLOCK(rnh);
			nd6_rtmsg(RTM_ADD, rt);
			RT_UNLOCK(rt);
			pr->ndpr_stateflags |= NDPRF_ONLINK;
		} else {
			char ip6buf[INET6_ADDRSTRLEN];
			char ip6bufg[INET6_ADDRSTRLEN];
			char ip6bufm[INET6_ADDRSTRLEN];
			struct sockaddr_in6 *sin6;

			sin6 = (struct sockaddr_in6 *)ifa->ifa_addr;
			nd6log((LOG_ERR, "nd6_prefix_onlink: failed to add "
			    "route for a prefix (%s/%d) on %s, gw=%s, mask=%s, "
			    "flags=%lx errno = %d\n",
			    ip6_sprintf(ip6buf, &pr->ndpr_prefix.sin6_addr),
			    pr->ndpr_plen, if_name(pr->ndpr_ifp),
			    ip6_sprintf(ip6bufg, &sin6->sin6_addr),
			    ip6_sprintf(ip6bufm, &mask6.sin6_addr),
			    rtflags, error));

			/* Save last error to return, see rtinit(). */
			a_failure = error;
		}

		if (rt != NULL) {
			RT_LOCK(rt);
			RT_REMREF(rt);
			RT_UNLOCK(rt);
		}
	}

	/* Return the last error we got. */
	return (a_failure);
}

int
nd6_prefix_onlink(struct nd_prefix *pr)
{
	struct ifaddr *ifa;
	struct ifnet *ifp = pr->ndpr_ifp;
	struct nd_prefix *opr;
	char ip6buf[INET6_ADDRSTRLEN];
	int error;

	ND6_ONLINK_LOCK_ASSERT();
	ND6_UNLOCK_ASSERT();

	if ((pr->ndpr_stateflags & NDPRF_ONLINK) != 0)
		return (EEXIST);

	/*
	 * Add the interface route associated with the prefix.  Before
	 * installing the route, check if there's the same prefix on another
	 * interface, and the prefix has already installed the interface route.
	 * Although such a configuration is expected to be rare, we explicitly
	 * allow it.
	 */
	ND6_RLOCK();
	LIST_FOREACH(opr, &V_nd_prefix, ndpr_entry) {
		if (opr == pr)
			continue;

		if ((opr->ndpr_stateflags & NDPRF_ONLINK) == 0)
			continue;

		if (!V_rt_add_addr_allfibs &&
		    opr->ndpr_ifp->if_fib != pr->ndpr_ifp->if_fib)
			continue;

		if (opr->ndpr_plen == pr->ndpr_plen &&
		    in6_are_prefix_equal(&pr->ndpr_prefix.sin6_addr,
		    &opr->ndpr_prefix.sin6_addr, pr->ndpr_plen)) {
			ND6_RUNLOCK();
			return (0);
		}
	}
	ND6_RUNLOCK();

	/*
	 * We prefer link-local addresses as the associated interface address.
	 */
	/* search for a link-local addr */
	ifa = (struct ifaddr *)in6ifa_ifpforlinklocal(ifp,
	    IN6_IFF_NOTREADY | IN6_IFF_ANYCAST);
	if (ifa == NULL) {
		struct epoch_tracker et;

		/* XXX: freebsd does not have ifa_ifwithaf */
		NET_EPOCH_ENTER(et);
		CK_STAILQ_FOREACH(ifa, &ifp->if_addrhead, ifa_link) {
			if (ifa->ifa_addr->sa_family == AF_INET6) {
				ifa_ref(ifa);
				break;
			}
		}
		NET_EPOCH_EXIT(et);
		/* should we care about ia6_flags? */
	}
	if (ifa == NULL) {
		/*
		 * This can still happen, when, for example, we receive an RA
		 * containing a prefix with the L bit set and the A bit clear,
		 * after removing all IPv6 addresses on the receiving
		 * interface.  This should, of course, be rare though.
		 */
		nd6log((LOG_NOTICE,
		    "nd6_prefix_onlink: failed to find any ifaddr"
		    " to add route for a prefix(%s/%d) on %s\n",
		    ip6_sprintf(ip6buf, &pr->ndpr_prefix.sin6_addr),
		    pr->ndpr_plen, if_name(ifp)));
		return (0);
	}

	error = nd6_prefix_onlink_rtrequest(pr, ifa);

	if (ifa != NULL)
		ifa_free(ifa);

	return (error);
}

int
nd6_prefix_offlink(struct nd_prefix *pr)
{
	int error = 0;
	struct ifnet *ifp = pr->ndpr_ifp;
	struct nd_prefix *opr;
	struct sockaddr_in6 sa6, mask6;
	struct rtentry *rt;
	char ip6buf[INET6_ADDRSTRLEN];
	uint64_t genid;
	int fibnum, maxfib, a_failure;

	ND6_ONLINK_LOCK_ASSERT();
	ND6_UNLOCK_ASSERT();

	if ((pr->ndpr_stateflags & NDPRF_ONLINK) == 0)
		return (EEXIST);

	bzero(&sa6, sizeof(sa6));
	sa6.sin6_family = AF_INET6;
	sa6.sin6_len = sizeof(sa6);
	bcopy(&pr->ndpr_prefix.sin6_addr, &sa6.sin6_addr,
	    sizeof(struct in6_addr));
	bzero(&mask6, sizeof(mask6));
	mask6.sin6_family = AF_INET6;
	mask6.sin6_len = sizeof(sa6);
	bcopy(&pr->ndpr_mask, &mask6.sin6_addr, sizeof(struct in6_addr));

	if (V_rt_add_addr_allfibs) {
		fibnum = 0;
		maxfib = rt_numfibs;
	} else {
		fibnum = ifp->if_fib;
		maxfib = fibnum + 1;
	}

	a_failure = 0;
	for (; fibnum < maxfib; fibnum++) {
		rt = NULL;
		error = in6_rtrequest(RTM_DELETE, (struct sockaddr *)&sa6, NULL,
		    (struct sockaddr *)&mask6, 0, &rt, fibnum);
		if (error == 0) {
			/* report the route deletion to the routing socket. */
			if (rt != NULL)
				nd6_rtmsg(RTM_DELETE, rt);
		} else {
			/* Save last error to return, see rtinit(). */
			a_failure = error;
		}
		if (rt != NULL) {
			RTFREE(rt);
		}
	}
	error = a_failure;
	a_failure = 1;
	if (error == 0) {
		pr->ndpr_stateflags &= ~NDPRF_ONLINK;

		/*
		 * There might be the same prefix on another interface,
		 * the prefix which could not be on-link just because we have
		 * the interface route (see comments in nd6_prefix_onlink).
		 * If there's one, try to make the prefix on-link on the
		 * interface.
		 */
		ND6_RLOCK();
restart:
		LIST_FOREACH(opr, &V_nd_prefix, ndpr_entry) {
			/*
			 * KAME specific: detached prefixes should not be
			 * on-link.
			 */
			if (opr == pr || (opr->ndpr_stateflags &
			    (NDPRF_ONLINK | NDPRF_DETACHED)) != 0)
				continue;

			if (opr->ndpr_plen == pr->ndpr_plen &&
			    in6_are_prefix_equal(&pr->ndpr_prefix.sin6_addr,
			    &opr->ndpr_prefix.sin6_addr, pr->ndpr_plen)) {
				int e;

				genid = V_nd6_list_genid;
				ND6_RUNLOCK();
				if ((e = nd6_prefix_onlink(opr)) != 0) {
					nd6log((LOG_ERR,
					    "nd6_prefix_offlink: failed to "
					    "recover a prefix %s/%d from %s "
					    "to %s (errno = %d)\n",
					    ip6_sprintf(ip6buf,
						&opr->ndpr_prefix.sin6_addr),
					    opr->ndpr_plen, if_name(ifp),
					    if_name(opr->ndpr_ifp), e));
				} else
					a_failure = 0;
				ND6_RLOCK();
				if (genid != V_nd6_list_genid)
					goto restart;
			}
		}
		ND6_RUNLOCK();
	} else {
		/* XXX: can we still set the NDPRF_ONLINK flag? */
		nd6log((LOG_ERR,
		    "nd6_prefix_offlink: failed to delete route: "
		    "%s/%d on %s (errno = %d)\n",
		    ip6_sprintf(ip6buf, &sa6.sin6_addr), pr->ndpr_plen,
		    if_name(ifp), error));
	}

	if (a_failure)
		lltable_prefix_free(AF_INET6, (struct sockaddr *)&sa6,
		    (struct sockaddr *)&mask6, LLE_STATIC);

	return (error);
}

static struct in6_ifaddr *
in6_ifadd(struct nd_prefixctl *pr, int mcast)
{
	struct ifnet *ifp = pr->ndpr_ifp;
	struct ifaddr *ifa;
	struct in6_aliasreq ifra;
	struct in6_ifaddr *ia, *ib;
	int error, plen0;
	struct in6_addr mask;
	int prefixlen = pr->ndpr_plen;
	int updateflags;
	char ip6buf[INET6_ADDRSTRLEN];

	in6_prefixlen2mask(&mask, prefixlen);

	/*
	 * find a link-local address (will be interface ID).
	 * Is it really mandatory? Theoretically, a global or a site-local
	 * address can be configured without a link-local address, if we
	 * have a unique interface identifier...
	 *
	 * it is not mandatory to have a link-local address, we can generate
	 * interface identifier on the fly.  we do this because:
	 * (1) it should be the easiest way to find interface identifier.
	 * (2) RFC2462 5.4 suggesting the use of the same interface identifier
	 * for multiple addresses on a single interface, and possible shortcut
	 * of DAD.  we omitted DAD for this reason in the past.
	 * (3) a user can prevent autoconfiguration of global address
	 * by removing link-local address by hand (this is partly because we
	 * don't have other way to control the use of IPv6 on an interface.
	 * this has been our design choice - cf. NRL's "ifconfig auto").
	 * (4) it is easier to manage when an interface has addresses
	 * with the same interface identifier, than to have multiple addresses
	 * with different interface identifiers.
	 */
	ifa = (struct ifaddr *)in6ifa_ifpforlinklocal(ifp, 0); /* 0 is OK? */
	if (ifa)
		ib = (struct in6_ifaddr *)ifa;
	else
		return NULL;

	/* prefixlen + ifidlen must be equal to 128 */
	plen0 = in6_mask2len(&ib->ia_prefixmask.sin6_addr, NULL);
	if (prefixlen != plen0) {
		ifa_free(ifa);
		nd6log((LOG_INFO, "in6_ifadd: wrong prefixlen for %s "
		    "(prefix=%d ifid=%d)\n",
		    if_name(ifp), prefixlen, 128 - plen0));
		return NULL;
	}

	/* make ifaddr */
	in6_prepare_ifra(&ifra, &pr->ndpr_prefix.sin6_addr, &mask);

	IN6_MASK_ADDR(&ifra.ifra_addr.sin6_addr, &mask);
	/* interface ID */
	ifra.ifra_addr.sin6_addr.s6_addr32[0] |=
	    (ib->ia_addr.sin6_addr.s6_addr32[0] & ~mask.s6_addr32[0]);
	ifra.ifra_addr.sin6_addr.s6_addr32[1] |=
	    (ib->ia_addr.sin6_addr.s6_addr32[1] & ~mask.s6_addr32[1]);
	ifra.ifra_addr.sin6_addr.s6_addr32[2] |=
	    (ib->ia_addr.sin6_addr.s6_addr32[2] & ~mask.s6_addr32[2]);
	ifra.ifra_addr.sin6_addr.s6_addr32[3] |=
	    (ib->ia_addr.sin6_addr.s6_addr32[3] & ~mask.s6_addr32[3]);
	ifa_free(ifa);

	/* lifetimes. */
	ifra.ifra_lifetime.ia6t_vltime = pr->ndpr_vltime;
	ifra.ifra_lifetime.ia6t_pltime = pr->ndpr_pltime;

	/* XXX: scope zone ID? */

	ifra.ifra_flags |= IN6_IFF_AUTOCONF; /* obey autoconf */

	/*
	 * Make sure that we do not have this address already.  This should
	 * usually not happen, but we can still see this case, e.g., if we
	 * have manually configured the exact address to be configured.
	 */
	ifa = (struct ifaddr *)in6ifa_ifpwithaddr(ifp,
	    &ifra.ifra_addr.sin6_addr);
	if (ifa != NULL) {
		ifa_free(ifa);
		/* this should be rare enough to make an explicit log */
		log(LOG_INFO, "in6_ifadd: %s is already configured\n",
		    ip6_sprintf(ip6buf, &ifra.ifra_addr.sin6_addr));
		return (NULL);
	}

	/*
	 * Allocate ifaddr structure, link into chain, etc.
	 * If we are going to create a new address upon receiving a multicasted
	 * RA, we need to impose a random delay before starting DAD.
	 * [draft-ietf-ipv6-rfc2462bis-02.txt, Section 5.4.2]
	 */
	updateflags = 0;
	if (mcast)
		updateflags |= IN6_IFAUPDATE_DADDELAY;
	if ((error = in6_update_ifa(ifp, &ifra, NULL, updateflags)) != 0) {
		nd6log((LOG_ERR,
		    "in6_ifadd: failed to make ifaddr %s on %s (errno=%d)\n",
		    ip6_sprintf(ip6buf, &ifra.ifra_addr.sin6_addr),
		    if_name(ifp), error));
		return (NULL);	/* ifaddr must not have been allocated. */
	}

	ia = in6ifa_ifpwithaddr(ifp, &ifra.ifra_addr.sin6_addr);
	/*
	 * XXXRW: Assumption of non-NULLness here might not be true with
	 * fine-grained locking -- should we validate it?  Or just return
	 * earlier ifa rather than looking it up again?
	 */
	return (ia);		/* this is always non-NULL  and referenced. */
}

/*
 * ia0 - corresponding public address
 */
int
in6_tmpifadd(const struct in6_ifaddr *ia0, int forcegen, int delay)
{
	struct ifnet *ifp = ia0->ia_ifa.ifa_ifp;
	struct in6_ifaddr *newia;
	struct in6_aliasreq ifra;
	int error;
	int trylimit = 3;	/* XXX: adhoc value */
	int updateflags;
	u_int32_t randid[2];
	time_t vltime0, pltime0;

	in6_prepare_ifra(&ifra, &ia0->ia_addr.sin6_addr,
	    &ia0->ia_prefixmask.sin6_addr);

	ifra.ifra_addr = ia0->ia_addr;	/* XXX: do we need this ? */
	/* clear the old IFID */
	IN6_MASK_ADDR(&ifra.ifra_addr.sin6_addr,
	    &ifra.ifra_prefixmask.sin6_addr);

  again:
	if (in6_get_tmpifid(ifp, (u_int8_t *)randid,
	    (const u_int8_t *)&ia0->ia_addr.sin6_addr.s6_addr[8], forcegen)) {
		nd6log((LOG_NOTICE, "in6_tmpifadd: failed to find a good "
		    "random IFID\n"));
		return (EINVAL);
	}
	ifra.ifra_addr.sin6_addr.s6_addr32[2] |=
	    (randid[0] & ~(ifra.ifra_prefixmask.sin6_addr.s6_addr32[2]));
	ifra.ifra_addr.sin6_addr.s6_addr32[3] |=
	    (randid[1] & ~(ifra.ifra_prefixmask.sin6_addr.s6_addr32[3]));

	/*
	 * in6_get_tmpifid() quite likely provided a unique interface ID.
	 * However, we may still have a chance to see collision, because
	 * there may be a time lag between generation of the ID and generation
	 * of the address.  So, we'll do one more sanity check.
	 */

	if (in6_localip(&ifra.ifra_addr.sin6_addr) != 0) {
		if (trylimit-- > 0) {
			forcegen = 1;
			goto again;
		}

		/* Give up.  Something strange should have happened.  */
		nd6log((LOG_NOTICE, "in6_tmpifadd: failed to "
		    "find a unique random IFID\n"));
		return (EEXIST);
	}

	/*
	 * The Valid Lifetime is the lower of the Valid Lifetime of the
         * public address or TEMP_VALID_LIFETIME.
	 * The Preferred Lifetime is the lower of the Preferred Lifetime
         * of the public address or TEMP_PREFERRED_LIFETIME -
         * DESYNC_FACTOR.
	 */
	if (ia0->ia6_lifetime.ia6t_vltime != ND6_INFINITE_LIFETIME) {
		vltime0 = IFA6_IS_INVALID(ia0) ? 0 :
		    (ia0->ia6_lifetime.ia6t_vltime -
		    (time_uptime - ia0->ia6_updatetime));
		if (vltime0 > V_ip6_temp_valid_lifetime)
			vltime0 = V_ip6_temp_valid_lifetime;
	} else
		vltime0 = V_ip6_temp_valid_lifetime;
	if (ia0->ia6_lifetime.ia6t_pltime != ND6_INFINITE_LIFETIME) {
		pltime0 = IFA6_IS_DEPRECATED(ia0) ? 0 :
		    (ia0->ia6_lifetime.ia6t_pltime -
		    (time_uptime - ia0->ia6_updatetime));
		if (pltime0 > V_ip6_temp_preferred_lifetime - V_ip6_desync_factor){
			pltime0 = V_ip6_temp_preferred_lifetime -
			    V_ip6_desync_factor;
		}
	} else
		pltime0 = V_ip6_temp_preferred_lifetime - V_ip6_desync_factor;
	ifra.ifra_lifetime.ia6t_vltime = vltime0;
	ifra.ifra_lifetime.ia6t_pltime = pltime0;

	/*
	 * A temporary address is created only if this calculated Preferred
	 * Lifetime is greater than REGEN_ADVANCE time units.
	 */
	if (ifra.ifra_lifetime.ia6t_pltime <= V_ip6_temp_regen_advance)
		return (0);

	/* XXX: scope zone ID? */

	ifra.ifra_flags |= (IN6_IFF_AUTOCONF|IN6_IFF_TEMPORARY);

	/* allocate ifaddr structure, link into chain, etc. */
	updateflags = 0;
	if (delay)
		updateflags |= IN6_IFAUPDATE_DADDELAY;
	if ((error = in6_update_ifa(ifp, &ifra, NULL, updateflags)) != 0)
		return (error);

	newia = in6ifa_ifpwithaddr(ifp, &ifra.ifra_addr.sin6_addr);
	if (newia == NULL) {	/* XXX: can it happen? */
		nd6log((LOG_ERR,
		    "in6_tmpifadd: ifa update succeeded, but we got "
		    "no ifaddr\n"));
		return (EINVAL); /* XXX */
	}
	newia->ia6_ndpr = ia0->ia6_ndpr;
	newia->ia6_ndpr->ndpr_addrcnt++;
	ifa_free(&newia->ia_ifa);

	/*
	 * A newly added address might affect the status of other addresses.
	 * XXX: when the temporary address is generated with a new public
	 * address, the onlink check is redundant.  However, it would be safe
	 * to do the check explicitly everywhere a new address is generated,
	 * and, in fact, we surely need the check when we create a new
	 * temporary address due to deprecation of an old temporary address.
	 */
	pfxlist_onlink_check();

	return (0);
}

static int
in6_init_prefix_ltimes(struct nd_prefix *ndpr)
{
	if (ndpr->ndpr_pltime == ND6_INFINITE_LIFETIME)
		ndpr->ndpr_preferred = 0;
	else
		ndpr->ndpr_preferred = time_uptime + ndpr->ndpr_pltime;
	if (ndpr->ndpr_vltime == ND6_INFINITE_LIFETIME)
		ndpr->ndpr_expire = 0;
	else
		ndpr->ndpr_expire = time_uptime + ndpr->ndpr_vltime;

	return 0;
}

static void
in6_init_address_ltimes(struct nd_prefix *new, struct in6_addrlifetime *lt6)
{
	/* init ia6t_expire */
	if (lt6->ia6t_vltime == ND6_INFINITE_LIFETIME)
		lt6->ia6t_expire = 0;
	else {
		lt6->ia6t_expire = time_uptime;
		lt6->ia6t_expire += lt6->ia6t_vltime;
	}

	/* init ia6t_preferred */
	if (lt6->ia6t_pltime == ND6_INFINITE_LIFETIME)
		lt6->ia6t_preferred = 0;
	else {
		lt6->ia6t_preferred = time_uptime;
		lt6->ia6t_preferred += lt6->ia6t_pltime;
	}
}

/*
 * Delete all the routing table entries that use the specified gateway.
 * XXX: this function causes search through all entries of routing table, so
 * it shouldn't be called when acting as a router.
 */
void
rt6_flush(struct in6_addr *gateway, struct ifnet *ifp)
{

	/* We'll care only link-local addresses */
	if (!IN6_IS_ADDR_LINKLOCAL(gateway))
		return;

	/* XXX Do we really need to walk any but the default FIB? */
	rt_foreach_fib_walk_del(AF_INET6, rt6_deleteroute, (void *)gateway);
}

static int
rt6_deleteroute(const struct rtentry *rt, void *arg)
{
#define SIN6(s)	((struct sockaddr_in6 *)s)
	struct in6_addr *gate = (struct in6_addr *)arg;

	if (rt->rt_gateway == NULL || rt->rt_gateway->sa_family != AF_INET6)
		return (0);

	if (!IN6_ARE_ADDR_EQUAL(gate, &SIN6(rt->rt_gateway)->sin6_addr)) {
		return (0);
	}

	/*
	 * Do not delete a static route.
	 * XXX: this seems to be a bit ad-hoc. Should we consider the
	 * 'cloned' bit instead?
	 */
	if ((rt->rt_flags & RTF_STATIC) != 0)
		return (0);

	/*
	 * We delete only host route. This means, in particular, we don't
	 * delete default route.
	 */
	if ((rt->rt_flags & RTF_HOST) == 0)
		return (0);

	return (1);
#undef SIN6
}

int
nd6_setdefaultiface(int ifindex)
{
	int error = 0;

	if (ifindex < 0 || V_if_index < ifindex)
		return (EINVAL);
	if (ifindex != 0 && !ifnet_byindex(ifindex))
		return (EINVAL);

	if (V_nd6_defifindex != ifindex) {
		V_nd6_defifindex = ifindex;
		if (V_nd6_defifindex > 0)
			V_nd6_defifp = ifnet_byindex(V_nd6_defifindex);
		else
			V_nd6_defifp = NULL;

		/*
		 * Our current implementation assumes one-to-one maping between
		 * interfaces and links, so it would be natural to use the
		 * default interface as the default link.
		 */
		scope6_setdefault(V_nd6_defifp);
	}

	return (error);
}
