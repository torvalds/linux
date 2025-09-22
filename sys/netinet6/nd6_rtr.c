/*	$OpenBSD: nd6_rtr.c,v 1.176 2025/07/08 00:47:41 jsg Exp $	*/
/*	$KAME: nd6_rtr.c,v 1.97 2001/02/07 11:09:13 itojun Exp $	*/

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
#include <sys/mbuf.h>
#include <sys/socket.h>
#include <sys/errno.h>

#include <net/if.h>
#include <net/if_var.h>
#include <net/route.h>
#include <net/rtable.h>

#include <netinet/in.h>
#include <netinet/ip6.h>
#include <netinet6/ip6_var.h>
#include <netinet6/nd6.h>
#include <netinet/icmp6.h>

int rt6_deleteroute(struct rtentry *, void *, unsigned int);

/*
 * Process Source Link-layer Address Options from
 * Router Solicitation / Advertisement Messages.
 */
void
nd6_rtr_cache(struct mbuf *m, int off, int icmp6len, int icmp6_type)
{
	struct ifnet *ifp;
	struct ip6_hdr *ip6 = mtod(m, struct ip6_hdr *);
	struct nd_router_solicit *nd_rs;
	struct nd_router_advert *nd_ra;
	struct in6_addr saddr6 = ip6->ip6_src;
	char *lladdr = NULL;
	int lladdrlen = 0;
	int i_am_router = (atomic_load_int(&ip6_forwarding) != 0);
	struct nd_opts ndopts;

	KASSERT(icmp6_type == ND_ROUTER_SOLICIT || icmp6_type ==
	    ND_ROUTER_ADVERT);

	/* Sanity checks */
	if (ip6->ip6_hlim != 255)
		goto bad;

	switch (icmp6_type) {
	case ND_ROUTER_SOLICIT:
		/*
		 * Don't update the neighbor cache, if src = ::.
		 * This indicates that the src has no IP address assigned yet.
		 */
		if (IN6_IS_ADDR_UNSPECIFIED(&saddr6))
			goto freeit;

		nd_rs = ip6_exthdr_get(&m, off, icmp6len);
		if (nd_rs == NULL) {
			icmp6stat_inc(icp6s_tooshort);
			return;
		}

		icmp6len -= sizeof(*nd_rs);
		if (nd6_options(nd_rs + 1, icmp6len, &ndopts) < 0) {
			/* nd6_options have incremented stats */
			goto freeit;
		}
		break;
	case ND_ROUTER_ADVERT:
		if (!IN6_IS_ADDR_LINKLOCAL(&saddr6))
			goto bad;

		nd_ra = ip6_exthdr_get(&m, off, icmp6len);
		if (nd_ra == NULL) {
			icmp6stat_inc(icp6s_tooshort);
			return;
		}

		icmp6len -= sizeof(*nd_ra);
		if (nd6_options(nd_ra + 1, icmp6len, &ndopts) < 0) {
			/* nd6_options have incremented stats */
			goto freeit;
		}
		break;
	}

	if (ndopts.nd_opts_src_lladdr) {
		lladdr = (char *)(ndopts.nd_opts_src_lladdr + 1);
		lladdrlen = ndopts.nd_opts_src_lladdr->nd_opt_len << 3;
	}

	ifp = if_get(m->m_pkthdr.ph_ifidx);
	if (ifp == NULL)
		goto freeit;

	if (lladdr && ((ifp->if_addrlen + 2 + 7) & ~7) != lladdrlen) {
		if_put(ifp);
		goto bad;
	}

	nd6_cache_lladdr(ifp, &saddr6, lladdr, lladdrlen, icmp6_type, 0,
	    i_am_router);
	if_put(ifp);

 freeit:
	m_freem(m);
	return;

 bad:
	icmp6stat_inc(icmp6_type == ND_ROUTER_SOLICIT ? icp6s_badrs :
	    icp6s_badra);
	m_freem(m);
}

/*
 * Delete all the routing table entries that use the specified gateway.
 * XXX: this function causes search through all entries of routing table, so
 * it shouldn't be called when acting as a router.
 * The gateway must already contain KAME's hack for link-local scope.
 */
int
rt6_flush(struct in6_addr *gateway, struct ifnet *ifp)
{
	struct rt_addrinfo info;
	struct sockaddr_in6 sa_mask;
	struct rtentry *rt = NULL;
	int error;

	NET_ASSERT_LOCKED();

	/* We'll care only link-local addresses */
	if (!IN6_IS_ADDR_LINKLOCAL(gateway))
		return (0);

	KASSERT(gateway->s6_addr16[1] != 0);

	do {
		error = rtable_walk(ifp->if_rdomain, AF_INET6, &rt,
		    rt6_deleteroute, gateway);
		if (rt != NULL && error == EEXIST) {
			memset(&info, 0, sizeof(info));
			info.rti_flags =  rt->rt_flags;
			info.rti_info[RTAX_DST] = rt_key(rt);
			info.rti_info[RTAX_GATEWAY] = rt->rt_gateway;
			info.rti_info[RTAX_NETMASK] = rt_plen2mask(rt,
			    &sa_mask);
			KERNEL_LOCK();
			error = rtrequest_delete(&info, RTP_ANY, ifp, NULL,
			    ifp->if_rdomain);
			KERNEL_UNLOCK();
			if (error == 0)
				error = EAGAIN;
		}
		rtfree(rt);
		rt = NULL;
	} while (error == EAGAIN);

	return (error);
}

int
rt6_deleteroute(struct rtentry *rt, void *arg, unsigned int id)
{
	struct in6_addr *gate = (struct in6_addr *)arg;

	if (rt->rt_gateway == NULL || rt->rt_gateway->sa_family != AF_INET6)
		return (0);

	if (!IN6_ARE_ADDR_EQUAL(gate, &satosin6(rt->rt_gateway)->sin6_addr))
		return (0);

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

	return (EEXIST);
}
