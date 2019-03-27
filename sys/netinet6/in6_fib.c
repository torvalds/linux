/*-
 * Copyright (c) 2015
 * 	Alexander V. Chernikov <melifaro@FreeBSD.org>
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
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "opt_inet.h"
#include "opt_inet6.h"
#include "opt_route.h"
#include "opt_mpath.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/lock.h>
#include <sys/rmlock.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/socket.h>
#include <sys/sysctl.h>
#include <sys/kernel.h>

#include <net/if.h>
#include <net/if_var.h>
#include <net/if_dl.h>
#include <net/route.h>
#include <net/route_var.h>
#include <net/vnet.h>

#ifdef RADIX_MPATH
#include <net/radix_mpath.h>
#endif

#include <netinet/in.h>
#include <netinet/in_var.h>
#include <netinet/ip_mroute.h>
#include <netinet/ip6.h>
#include <netinet6/in6_fib.h>
#include <netinet6/in6_var.h>
#include <netinet6/nd6.h>
#include <netinet6/scope6_var.h>

#include <net/if_types.h>

#ifdef INET6
static void fib6_rte_to_nh_extended(struct rtentry *rte,
    const struct in6_addr *dst, uint32_t flags, struct nhop6_extended *pnh6);
static void fib6_rte_to_nh_basic(struct rtentry *rte, const struct in6_addr *dst,
    uint32_t flags, struct nhop6_basic *pnh6);
static struct ifnet *fib6_get_ifaifp(struct rtentry *rte);
#define RNTORT(p)	((struct rtentry *)(p))

/*
 * Gets real interface for the @rte.
 * Returns rt_ifp for !IFF_LOOPBACK routers.
 * Extracts "real" address interface from interface address
 * loopback routes.
 */
static struct ifnet *
fib6_get_ifaifp(struct rtentry *rte)
{
	struct ifnet *ifp;
	struct sockaddr_dl *sdl;

	ifp = rte->rt_ifp;
	if ((ifp->if_flags & IFF_LOOPBACK) &&
	    rte->rt_gateway->sa_family == AF_LINK) {
		sdl = (struct sockaddr_dl *)rte->rt_gateway;
		return (ifnet_byindex(sdl->sdl_index));
	}

	return (ifp);
}

static void
fib6_rte_to_nh_basic(struct rtentry *rte, const struct in6_addr *dst,
    uint32_t flags, struct nhop6_basic *pnh6)
{
	struct sockaddr_in6 *gw;

	/* Do explicit nexthop zero unless we're copying it */
	memset(pnh6, 0, sizeof(*pnh6));

	if ((flags & NHR_IFAIF) != 0)
		pnh6->nh_ifp = fib6_get_ifaifp(rte);
	else
		pnh6->nh_ifp = rte->rt_ifp;

	pnh6->nh_mtu = min(rte->rt_mtu, IN6_LINKMTU(rte->rt_ifp));
	if (rte->rt_flags & RTF_GATEWAY) {
		gw = (struct sockaddr_in6 *)rte->rt_gateway;
		pnh6->nh_addr = gw->sin6_addr;
		in6_clearscope(&pnh6->nh_addr);
	} else
		pnh6->nh_addr = *dst;
	/* Set flags */
	pnh6->nh_flags = fib_rte_to_nh_flags(rte->rt_flags);
	gw = (struct sockaddr_in6 *)rt_key(rte);
	if (IN6_IS_ADDR_UNSPECIFIED(&gw->sin6_addr))
		pnh6->nh_flags |= NHF_DEFAULT;
}

static void
fib6_rte_to_nh_extended(struct rtentry *rte, const struct in6_addr *dst,
    uint32_t flags, struct nhop6_extended *pnh6)
{
	struct sockaddr_in6 *gw;

	/* Do explicit nexthop zero unless we're copying it */
	memset(pnh6, 0, sizeof(*pnh6));

	if ((flags & NHR_IFAIF) != 0)
		pnh6->nh_ifp = fib6_get_ifaifp(rte);
	else
		pnh6->nh_ifp = rte->rt_ifp;

	pnh6->nh_mtu = min(rte->rt_mtu, IN6_LINKMTU(rte->rt_ifp));
	if (rte->rt_flags & RTF_GATEWAY) {
		gw = (struct sockaddr_in6 *)rte->rt_gateway;
		pnh6->nh_addr = gw->sin6_addr;
		in6_clearscope(&pnh6->nh_addr);
	} else
		pnh6->nh_addr = *dst;
	/* Set flags */
	pnh6->nh_flags = fib_rte_to_nh_flags(rte->rt_flags);
	gw = (struct sockaddr_in6 *)rt_key(rte);
	if (IN6_IS_ADDR_UNSPECIFIED(&gw->sin6_addr))
		pnh6->nh_flags |= NHF_DEFAULT;
}

/*
 * Performs IPv6 route table lookup on @dst. Returns 0 on success.
 * Stores basic nexthop info into provided @pnh6 structure.
 * Note that
 * - nh_ifp represents logical transmit interface (rt_ifp) by default
 * - nh_ifp represents "address" interface if NHR_IFAIF flag is passed
 * - mtu from logical transmit interface will be returned.
 * - nh_ifp cannot be safely dereferenced
 * - nh_ifp represents rt_ifp (e.g. if looking up address on
 *   interface "ix0" pointer to "ix0" interface will be returned instead
 *   of "lo0")
 * - howewer mtu from "transmit" interface will be returned.
 * - scope will be embedded in nh_addr
 */
int
fib6_lookup_nh_basic(uint32_t fibnum, const struct in6_addr *dst, uint32_t scopeid,
    uint32_t flags, uint32_t flowid, struct nhop6_basic *pnh6)
{
	RIB_RLOCK_TRACKER;
	struct rib_head *rh;
	struct radix_node *rn;
	struct sockaddr_in6 sin6;
	struct rtentry *rte;

	KASSERT((fibnum < rt_numfibs), ("fib6_lookup_nh_basic: bad fibnum"));
	rh = rt_tables_get_rnh(fibnum, AF_INET6);
	if (rh == NULL)
		return (ENOENT);

	/* Prepare lookup key */
	memset(&sin6, 0, sizeof(sin6));
	sin6.sin6_addr = *dst;
	sin6.sin6_len = sizeof(struct sockaddr_in6);
	/* Assume scopeid is valid and embed it directly */
	if (IN6_IS_SCOPE_LINKLOCAL(dst))
		sin6.sin6_addr.s6_addr16[1] = htons(scopeid & 0xffff);

	RIB_RLOCK(rh);
	rn = rh->rnh_matchaddr((void *)&sin6, &rh->head);
	if (rn != NULL && ((rn->rn_flags & RNF_ROOT) == 0)) {
		rte = RNTORT(rn);
		/* Ensure route & ifp is UP */
		if (RT_LINK_IS_UP(rte->rt_ifp)) {
			fib6_rte_to_nh_basic(rte, &sin6.sin6_addr, flags, pnh6);
			RIB_RUNLOCK(rh);
			return (0);
		}
	}
	RIB_RUNLOCK(rh);

	return (ENOENT);
}

/*
 * Performs IPv6 route table lookup on @dst. Returns 0 on success.
 * Stores extended nexthop info into provided @pnh6 structure.
 * Note that
 * - nh_ifp cannot be safely dereferenced unless NHR_REF is specified.
 * - in that case you need to call fib6_free_nh_ext()
 * - nh_ifp represents logical transmit interface (rt_ifp) by default
 * - nh_ifp represents "address" interface if NHR_IFAIF flag is passed
 * - mtu from logical transmit interface will be returned.
 * - scope will be embedded in nh_addr
 */
int
fib6_lookup_nh_ext(uint32_t fibnum, const struct in6_addr *dst,uint32_t scopeid,
    uint32_t flags, uint32_t flowid, struct nhop6_extended *pnh6)
{
	RIB_RLOCK_TRACKER;
	struct rib_head *rh;
	struct radix_node *rn;
	struct sockaddr_in6 sin6;
	struct rtentry *rte;

	KASSERT((fibnum < rt_numfibs), ("fib6_lookup_nh_ext: bad fibnum"));
	rh = rt_tables_get_rnh(fibnum, AF_INET6);
	if (rh == NULL)
		return (ENOENT);

	/* Prepare lookup key */
	memset(&sin6, 0, sizeof(sin6));
	sin6.sin6_len = sizeof(struct sockaddr_in6);
	sin6.sin6_addr = *dst;
	/* Assume scopeid is valid and embed it directly */
	if (IN6_IS_SCOPE_LINKLOCAL(dst))
		sin6.sin6_addr.s6_addr16[1] = htons(scopeid & 0xffff);

	RIB_RLOCK(rh);
	rn = rh->rnh_matchaddr((void *)&sin6, &rh->head);
	if (rn != NULL && ((rn->rn_flags & RNF_ROOT) == 0)) {
		rte = RNTORT(rn);
#ifdef RADIX_MPATH
		rte = rt_mpath_select(rte, flowid);
		if (rte == NULL) {
			RIB_RUNLOCK(rh);
			return (ENOENT);
		}
#endif
		/* Ensure route & ifp is UP */
		if (RT_LINK_IS_UP(rte->rt_ifp)) {
			fib6_rte_to_nh_extended(rte, &sin6.sin6_addr, flags,
			    pnh6);
			if ((flags & NHR_REF) != 0) {
				/* TODO: Do lwref on egress ifp's */
			}
			RIB_RUNLOCK(rh);

			return (0);
		}
	}
	RIB_RUNLOCK(rh);

	return (ENOENT);
}

void
fib6_free_nh_ext(uint32_t fibnum, struct nhop6_extended *pnh6)
{

}

#endif

