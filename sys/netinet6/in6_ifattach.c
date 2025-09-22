/*	$OpenBSD: in6_ifattach.c,v 1.124 2025/07/08 00:47:41 jsg Exp $	*/
/*	$KAME: in6_ifattach.c,v 1.124 2001/07/18 08:32:51 jinmei Exp $	*/

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
#include <sys/socket.h>

#include <net/if.h>
#include <net/if_var.h>
#include <net/if_dl.h>
#include <net/if_types.h>
#include <net/route.h>

#include <netinet/in.h>

#include <netinet6/in6_var.h>
#include <netinet/ip6.h>
#include <netinet6/in6_ifattach.h>
#include <netinet6/nd6.h>
#ifdef MROUTING
#include <netinet6/ip6_mroute.h>
#endif

void	in6_get_rand_ifid(struct ifnet *, struct in6_addr *);
int	in6_get_hw_ifid(struct ifnet *, struct in6_addr *);
void	in6_get_ifid(struct ifnet *, struct in6_addr *);
int	in6_ifattach_loopback(struct ifnet *);

#define EUI64_GBIT	0x01
#define EUI64_UBIT	0x02
#define EUI64_TO_IFID(in6)	do {(in6)->s6_addr[8] ^= EUI64_UBIT; } while (0)
#define EUI64_GROUP(in6)	((in6)->s6_addr[8] & EUI64_GBIT)
#define EUI64_INDIVIDUAL(in6)	(!EUI64_GROUP(in6))
#define EUI64_LOCAL(in6)	((in6)->s6_addr[8] & EUI64_UBIT)
#define EUI64_UNIVERSAL(in6)	(!EUI64_LOCAL(in6))

#define IFID_LOCAL(in6)		(!EUI64_LOCAL(in6))
#define IFID_UNIVERSAL(in6)	(!EUI64_UNIVERSAL(in6))

/*
 * Generate a random interface identifier.
 *
 * in6 - upper 64bits are preserved
 */
void
in6_get_rand_ifid(struct ifnet *ifp, struct in6_addr *in6)
{
	arc4random_buf(&in6->s6_addr32[2], 8);

	/* make sure to set "u" bit to local, and "g" bit to individual. */
	in6->s6_addr[8] &= ~EUI64_GBIT;	/* g bit to "individual" */
	in6->s6_addr[8] |= EUI64_UBIT;	/* u bit to "local" */

	/* convert EUI64 into IPv6 interface identifier */
	EUI64_TO_IFID(in6);
}

/*
 * Get interface identifier for the specified interface.
 *
 * in6 - upper 64bits are preserved
 */
int
in6_get_hw_ifid(struct ifnet *ifp, struct in6_addr *in6)
{
	struct sockaddr_dl *sdl;
	char *addr;
	size_t addrlen;
	static u_int8_t allzero[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };
	static u_int8_t allone[8] =
		{ 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };

	sdl = ifp->if_sadl;
	if (sdl == NULL || sdl->sdl_alen == 0)
		return -1;

	addr = LLADDR(sdl);
	addrlen = sdl->sdl_alen;

	switch (ifp->if_type) {
	case IFT_IEEE1394:
	case IFT_IEEE80211:
		/* IEEE1394 uses 16byte length address starting with EUI64 */
		if (addrlen > 8)
			addrlen = 8;
		break;
	default:
		break;
	}

	/* get EUI64 */
	switch (ifp->if_type) {
	/* IEEE802/EUI64 cases - what others? */
	case IFT_ETHER:
	case IFT_CARP:
	case IFT_IEEE1394:
	case IFT_IEEE80211:
		/* look at IEEE802/EUI64 only */
		if (addrlen != 8 && addrlen != 6)
			return -1;

		/*
		 * check for invalid MAC address - on bsdi, we see it a lot
		 * since wildboar configures all-zero MAC on pccard before
		 * card insertion.
		 */
		if (bcmp(addr, allzero, addrlen) == 0)
			return -1;
		if (bcmp(addr, allone, addrlen) == 0)
			return -1;

		/* make EUI64 address */
		if (addrlen == 8)
			memcpy(&in6->s6_addr[8], addr, 8);
		else if (addrlen == 6) {
			in6->s6_addr[8] = addr[0];
			in6->s6_addr[9] = addr[1];
			in6->s6_addr[10] = addr[2];
			in6->s6_addr[11] = 0xff;
			in6->s6_addr[12] = 0xfe;
			in6->s6_addr[13] = addr[3];
			in6->s6_addr[14] = addr[4];
			in6->s6_addr[15] = addr[5];
		}
		break;

	case IFT_GIF:
		/*
		 * RFC2893 says: "SHOULD use IPv4 address as ifid source".
		 * however, IPv4 address is not very suitable as unique
		 * identifier source (can be renumbered).
		 * we don't do this.
		 */
		return -1;

	default:
		return -1;
	}

	/* sanity check: g bit must not indicate "group" */
	if (EUI64_GROUP(in6))
		return -1;

	/* convert EUI64 into IPv6 interface identifier */
	EUI64_TO_IFID(in6);

	/*
	 * sanity check: ifid must not be all zero, avoid conflict with
	 * subnet router anycast
	 */
	if ((in6->s6_addr[8] & ~(EUI64_GBIT | EUI64_UBIT)) == 0x00 &&
	    bcmp(&in6->s6_addr[9], allzero, 7) == 0) {
		return -1;
	}

	return 0;
}

/*
 * Get interface identifier for the specified interface.  If it is not
 * available on ifp0, borrow interface identifier from other information
 * sources.
 */
void
in6_get_ifid(struct ifnet *ifp0, struct in6_addr *in6)
{
	struct ifnet *ifp;

	/* first, try to get it from the interface itself */
	if (in6_get_hw_ifid(ifp0, in6) == 0)
		return;

	/* next, try to get it from some other hardware interface */
	TAILQ_FOREACH(ifp, &ifnetlist, if_list) {
		if (ifp == ifp0)
			continue;
		if (in6_get_hw_ifid(ifp, in6) == 0)
			return;
	}

	/* last resort: get from random number source */
	in6_get_rand_ifid(ifp, in6);
}

/*
 * ifid - used as EUI64 if not NULL, overrides other EUI64 sources
 */

int
in6_ifattach_linklocal(struct ifnet *ifp, struct in6_addr *ifid)
{
	struct in6_aliasreq ifra;
	struct in6_ifaddr *ia6;
	int error, flags;

	NET_ASSERT_LOCKED();

	/*
	 * configure link-local address.
	 */
	bzero(&ifra, sizeof(ifra));
	strlcpy(ifra.ifra_name, ifp->if_xname, sizeof(ifra.ifra_name));
	ifra.ifra_addr.sin6_family = AF_INET6;
	ifra.ifra_addr.sin6_len = sizeof(struct sockaddr_in6);
	ifra.ifra_addr.sin6_addr.s6_addr16[0] = htons(0xfe80);
	ifra.ifra_addr.sin6_addr.s6_addr16[1] = htons(ifp->if_index);
	ifra.ifra_addr.sin6_addr.s6_addr32[1] = 0;
	if ((ifp->if_flags & IFF_LOOPBACK) != 0) {
		ifra.ifra_addr.sin6_addr.s6_addr32[2] = 0;
		ifra.ifra_addr.sin6_addr.s6_addr32[3] = htonl(1);
	} else if (ifid) {
		ifra.ifra_addr.sin6_addr = *ifid;
		ifra.ifra_addr.sin6_addr.s6_addr16[0] = htons(0xfe80);
		ifra.ifra_addr.sin6_addr.s6_addr16[1] = htons(ifp->if_index);
		ifra.ifra_addr.sin6_addr.s6_addr32[1] = 0;
		ifra.ifra_addr.sin6_addr.s6_addr[8] &= ~EUI64_GBIT;
		ifra.ifra_addr.sin6_addr.s6_addr[8] |= EUI64_UBIT;
	} else
		in6_get_ifid(ifp, &ifra.ifra_addr.sin6_addr);

	ifra.ifra_prefixmask.sin6_len = sizeof(struct sockaddr_in6);
	ifra.ifra_prefixmask.sin6_family = AF_INET6;
	ifra.ifra_prefixmask.sin6_addr = in6mask64;
	/* link-local addresses should NEVER expire. */
	ifra.ifra_lifetime.ia6t_vltime = ND6_INFINITE_LIFETIME;
	ifra.ifra_lifetime.ia6t_pltime = ND6_INFINITE_LIFETIME;

	/*
	 * XXX: Some P2P interfaces seem not to send packets just after
	 * becoming up, so we skip p2p interfaces for safety.
	 */
	if (in6if_do_dad(ifp) && ((ifp->if_flags & IFF_POINTOPOINT) == 0))
		ifra.ifra_flags |= IN6_IFF_TENTATIVE;

	error = in6_update_ifa(ifp, &ifra, in6ifa_ifpforlinklocal(ifp, 0));
	if (error != 0)
		return (error);

	ia6 = in6ifa_ifpforlinklocal(ifp, 0);

	/* Perform DAD, if needed. */
	if (ia6->ia6_flags & IN6_IFF_TENTATIVE)
		nd6_dad_start(&ia6->ia_ifa);

	if (ifp->if_flags & IFF_LOOPBACK) {
		if_addrhooks_run(ifp);
		return (0); /* No need to install a connected route. */
	}

	flags = RTF_CONNECTED | RTF_MPATH;
	if ((ifp->if_flags & IFF_POINTOPOINT) == 0)
		flags |= RTF_CLONING;

	error = rt_ifa_add(&ia6->ia_ifa, flags, ia6->ia_ifa.ifa_addr,
	    ifp->if_rdomain);
	if (error) {
		in6_purgeaddr(&ia6->ia_ifa);
		return (error);
	}
	if_addrhooks_run(ifp);

	return (0);
}

int
in6_ifattach_loopback(struct ifnet *ifp)
{
	struct in6_addr in6 = in6addr_loopback;
	struct in6_aliasreq ifra;

	KASSERT(ifp->if_flags & IFF_LOOPBACK);

	if (in6ifa_ifpwithaddr(ifp, &in6) != NULL)
		return (0);

	bzero(&ifra, sizeof(ifra));
	strlcpy(ifra.ifra_name, ifp->if_xname, sizeof(ifra.ifra_name));
	ifra.ifra_prefixmask.sin6_len = sizeof(struct sockaddr_in6);
	ifra.ifra_prefixmask.sin6_family = AF_INET6;
	ifra.ifra_prefixmask.sin6_addr = in6mask128;

	/*
	 * Always initialize ia_dstaddr (= broadcast address) to loopback
	 * address.  Follows IPv4 practice - see in_ifinit().
	 */
	ifra.ifra_dstaddr.sin6_len = sizeof(struct sockaddr_in6);
	ifra.ifra_dstaddr.sin6_family = AF_INET6;
	ifra.ifra_dstaddr.sin6_addr = in6addr_loopback;

	ifra.ifra_addr.sin6_len = sizeof(struct sockaddr_in6);
	ifra.ifra_addr.sin6_family = AF_INET6;
	ifra.ifra_addr.sin6_addr = in6addr_loopback;

	/* the loopback  address should NEVER expire. */
	ifra.ifra_lifetime.ia6t_vltime = ND6_INFINITE_LIFETIME;
	ifra.ifra_lifetime.ia6t_pltime = ND6_INFINITE_LIFETIME;

	/*
	 * We are sure that this is a newly assigned address, so we can set
	 * NULL to the 3rd arg.
	 */
	return (in6_update_ifa(ifp, &ifra, NULL));
}

/*
 * XXX multiple loopback interface needs more care.  for instance,
 * nodelocal address needs to be configured onto only one of them.
 * XXX multiple link-local address case
 */
int
in6_ifattach(struct ifnet *ifp)
{
	/* some of the interfaces are inherently not IPv6 capable */
	switch (ifp->if_type) {
	case IFT_BRIDGE:
	case IFT_ENC:
	case IFT_PFLOG:
	case IFT_PFSYNC:
		return (0);
	}

	/*
	 * if link mtu is too small, don't try to configure IPv6.
	 * remember there could be some link-layer that has special
	 * fragmentation logic.
	 */
	if (ifp->if_mtu < IPV6_MMTU)
		return (EINVAL);

	if (nd6_need_cache(ifp) && (ifp->if_flags & IFF_MULTICAST) == 0)
		return (EINVAL);

	/*
	 * Assign loopback address if this lo(4) interface is the
	 * default for its rdomain.
	 */
	if ((ifp->if_flags & IFF_LOOPBACK) &&
	    (ifp->if_index == rtable_loindex(ifp->if_rdomain))) {
		int error;

		error = in6_ifattach_loopback(ifp);
		if (error)
			return (error);
	}

	switch (ifp->if_type) {
	case IFT_WIREGUARD:
		return (0);
	}

	/* Assign a link-local address, if there's none. */
	if (in6ifa_ifpforlinklocal(ifp, 0) == NULL) {
		if (in6_ifattach_linklocal(ifp, NULL) != 0) {
			/* failed to assign linklocal address. bark? */
		}
	}

	return (0);
}

/*
 * NOTE: in6_ifdetach() does not support loopback if at this moment.
 */
void
in6_ifdetach(struct ifnet *ifp)
{
	struct ifaddr *ifa, *next;
	struct rtentry *rt;
	struct sockaddr_in6 sin6;

#ifdef MROUTING
	/* remove ip6_mrouter stuff */
	ip6_mrouter_detach(ifp);
#endif

	/* nuke any of IPv6 addresses we have */
	TAILQ_FOREACH_SAFE(ifa, &ifp->if_addrlist, ifa_list, next) {
		if (ifa->ifa_addr->sa_family != AF_INET6)
			continue;
		in6_purgeaddr(ifa);
		if_addrhooks_run(ifp);
	}

	/*
	 * Remove neighbor management table.  Must be called after
	 * purging addresses.
	 */
	nd6_purge(ifp);

	/* remove route to interface local allnodes multicast (ff01::1) */
	bzero(&sin6, sizeof(sin6));
	sin6.sin6_len = sizeof(struct sockaddr_in6);
	sin6.sin6_family = AF_INET6;
	sin6.sin6_addr = in6addr_intfacelocal_allnodes;
	sin6.sin6_addr.s6_addr16[1] = htons(ifp->if_index);
	rt = rtalloc(sin6tosa(&sin6), 0, ifp->if_rdomain);
	if (rt && rt->rt_ifidx == ifp->if_index)
		rtdeletemsg(rt, ifp, ifp->if_rdomain);
	rtfree(rt);

	/* remove route to link-local allnodes multicast (ff02::1) */
	bzero(&sin6, sizeof(sin6));
	sin6.sin6_len = sizeof(struct sockaddr_in6);
	sin6.sin6_family = AF_INET6;
	sin6.sin6_addr = in6addr_linklocal_allnodes;
	sin6.sin6_addr.s6_addr16[1] = htons(ifp->if_index);
	rt = rtalloc(sin6tosa(&sin6), 0, ifp->if_rdomain);
	if (rt && rt->rt_ifidx == ifp->if_index)
		rtdeletemsg(rt, ifp, ifp->if_rdomain);
	rtfree(rt);

	if (ifp->if_xflags & (IFXF_AUTOCONF6 | IFXF_AUTOCONF6TEMP))
		ifp->if_xflags &= ~(IFXF_AUTOCONF6 | IFXF_AUTOCONF6TEMP);
}
