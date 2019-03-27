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
#include <netinet/in_fib.h>

#ifdef INET
static void fib4_rte_to_nh_basic(struct rtentry *rte, struct in_addr dst,
    uint32_t flags, struct nhop4_basic *pnh4);
static void fib4_rte_to_nh_extended(struct rtentry *rte, struct in_addr dst,
    uint32_t flags, struct nhop4_extended *pnh4);

#define RNTORT(p)	((struct rtentry *)(p))

static void
fib4_rte_to_nh_basic(struct rtentry *rte, struct in_addr dst,
    uint32_t flags, struct nhop4_basic *pnh4)
{
	struct sockaddr_in *gw;

	if ((flags & NHR_IFAIF) != 0)
		pnh4->nh_ifp = rte->rt_ifa->ifa_ifp;
	else
		pnh4->nh_ifp = rte->rt_ifp;
	pnh4->nh_mtu = min(rte->rt_mtu, rte->rt_ifp->if_mtu);
	if (rte->rt_flags & RTF_GATEWAY) {
		gw = (struct sockaddr_in *)rte->rt_gateway;
		pnh4->nh_addr = gw->sin_addr;
	} else
		pnh4->nh_addr = dst;
	/* Set flags */
	pnh4->nh_flags = fib_rte_to_nh_flags(rte->rt_flags);
	gw = (struct sockaddr_in *)rt_key(rte);
	if (gw->sin_addr.s_addr == 0)
		pnh4->nh_flags |= NHF_DEFAULT;
	/* TODO: Handle RTF_BROADCAST here */
}

static void
fib4_rte_to_nh_extended(struct rtentry *rte, struct in_addr dst,
    uint32_t flags, struct nhop4_extended *pnh4)
{
	struct sockaddr_in *gw;
	struct in_ifaddr *ia;

	if ((flags & NHR_IFAIF) != 0)
		pnh4->nh_ifp = rte->rt_ifa->ifa_ifp;
	else
		pnh4->nh_ifp = rte->rt_ifp;
	pnh4->nh_mtu = min(rte->rt_mtu, rte->rt_ifp->if_mtu);
	if (rte->rt_flags & RTF_GATEWAY) {
		gw = (struct sockaddr_in *)rte->rt_gateway;
		pnh4->nh_addr = gw->sin_addr;
	} else
		pnh4->nh_addr = dst;
	/* Set flags */
	pnh4->nh_flags = fib_rte_to_nh_flags(rte->rt_flags);
	gw = (struct sockaddr_in *)rt_key(rte);
	if (gw->sin_addr.s_addr == 0)
		pnh4->nh_flags |= NHF_DEFAULT;
	/* XXX: Set RTF_BROADCAST if GW address is broadcast */

	ia = ifatoia(rte->rt_ifa);
	pnh4->nh_src = IA_SIN(ia)->sin_addr;
}

/*
 * Performs IPv4 route table lookup on @dst. Returns 0 on success.
 * Stores nexthop info provided @pnh4 structure.
 * Note that
 * - nh_ifp cannot be safely dereferenced
 * - nh_ifp represents logical transmit interface (rt_ifp) (e.g. if
 *   looking up address on interface "ix0" pointer to "lo0" interface
 *   will be returned instead of "ix0")
 * - nh_ifp represents "address" interface if NHR_IFAIF flag is passed
 * - howewer mtu from "transmit" interface will be returned.
 */
int
fib4_lookup_nh_basic(uint32_t fibnum, struct in_addr dst, uint32_t flags,
    uint32_t flowid, struct nhop4_basic *pnh4)
{
	RIB_RLOCK_TRACKER;
	struct rib_head *rh;
	struct radix_node *rn;
	struct sockaddr_in sin;
	struct rtentry *rte;

	KASSERT((fibnum < rt_numfibs), ("fib4_lookup_nh_basic: bad fibnum"));
	rh = rt_tables_get_rnh(fibnum, AF_INET);
	if (rh == NULL)
		return (ENOENT);

	/* Prepare lookup key */
	memset(&sin, 0, sizeof(sin));
	sin.sin_len = sizeof(struct sockaddr_in);
	sin.sin_addr = dst;

	RIB_RLOCK(rh);
	rn = rh->rnh_matchaddr((void *)&sin, &rh->head);
	if (rn != NULL && ((rn->rn_flags & RNF_ROOT) == 0)) {
		rte = RNTORT(rn);
		/* Ensure route & ifp is UP */
		if (RT_LINK_IS_UP(rte->rt_ifp)) {
			fib4_rte_to_nh_basic(rte, dst, flags, pnh4);
			RIB_RUNLOCK(rh);

			return (0);
		}
	}
	RIB_RUNLOCK(rh);

	return (ENOENT);
}

/*
 * Performs IPv4 route table lookup on @dst. Returns 0 on success.
 * Stores extende nexthop info provided @pnh4 structure.
 * Note that
 * - nh_ifp cannot be safely dereferenced unless NHR_REF is specified.
 * - in that case you need to call fib4_free_nh_ext()
 * - nh_ifp represents logical transmit interface (rt_ifp) (e.g. if
 *   looking up address of interface "ix0" pointer to "lo0" interface
 *   will be returned instead of "ix0")
 * - nh_ifp represents "address" interface if NHR_IFAIF flag is passed
 * - howewer mtu from "transmit" interface will be returned.
 */
int
fib4_lookup_nh_ext(uint32_t fibnum, struct in_addr dst, uint32_t flags,
    uint32_t flowid, struct nhop4_extended *pnh4)
{
	RIB_RLOCK_TRACKER;
	struct rib_head *rh;
	struct radix_node *rn;
	struct sockaddr_in sin;
	struct rtentry *rte;

	KASSERT((fibnum < rt_numfibs), ("fib4_lookup_nh_ext: bad fibnum"));
	rh = rt_tables_get_rnh(fibnum, AF_INET);
	if (rh == NULL)
		return (ENOENT);

	/* Prepare lookup key */
	memset(&sin, 0, sizeof(sin));
	sin.sin_len = sizeof(struct sockaddr_in);
	sin.sin_addr = dst;

	RIB_RLOCK(rh);
	rn = rh->rnh_matchaddr((void *)&sin, &rh->head);
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
			fib4_rte_to_nh_extended(rte, dst, flags, pnh4);
			if ((flags & NHR_REF) != 0) {
				/* TODO: lwref on egress ifp's ? */
			}
			RIB_RUNLOCK(rh);

			return (0);
		}
	}
	RIB_RUNLOCK(rh);

	return (ENOENT);
}

void
fib4_free_nh_ext(uint32_t fibnum, struct nhop4_extended *pnh4)
{

}

#endif
