/*	$FreeBSD$	*/
/*	$KAME: if_stf.c,v 1.73 2001/12/03 11:08:30 keiichi Exp $	*/

/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (C) 2000 WIDE Project.
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
 * 6to4 interface, based on RFC3056.
 *
 * 6to4 interface is NOT capable of link-layer (I mean, IPv4) multicasting.
 * There is no address mapping defined from IPv6 multicast address to IPv4
 * address.  Therefore, we do not have IFF_MULTICAST on the interface.
 *
 * Due to the lack of address mapping for link-local addresses, we cannot
 * throw packets toward link-local addresses (fe80::x).  Also, we cannot throw
 * packets to link-local multicast addresses (ff02::x).
 *
 * Here are interesting symptoms due to the lack of link-local address:
 *
 * Unicast routing exchange:
 * - RIPng: Impossible.  Uses link-local multicast packet toward ff02::9,
 *   and link-local addresses as nexthop.
 * - OSPFv6: Impossible.  OSPFv6 assumes that there's link-local address
 *   assigned to the link, and makes use of them.  Also, HELLO packets use
 *   link-local multicast addresses (ff02::5 and ff02::6).
 * - BGP4+: Maybe.  You can only use global address as nexthop, and global
 *   address as TCP endpoint address.
 *
 * Multicast routing protocols:
 * - PIM: Hello packet cannot be used to discover adjacent PIM routers.
 *   Adjacent PIM routers must be configured manually (is it really spec-wise
 *   correct thing to do?).
 *
 * ICMPv6:
 * - Redirects cannot be used due to the lack of link-local address.
 *
 * stf interface does not have, and will not need, a link-local address.  
 * It seems to have no real benefit and does not help the above symptoms much.
 * Even if we assign link-locals to interface, we cannot really
 * use link-local unicast/multicast on top of 6to4 cloud (since there's no
 * encapsulation defined for link-local address), and the above analysis does
 * not change.  RFC3056 does not mandate the assignment of link-local address
 * either.
 *
 * 6to4 interface has security issues.  Refer to
 * http://playground.iijlab.net/i-d/draft-itojun-ipv6-transition-abuse-00.txt
 * for details.  The code tries to filter out some of malicious packets.
 * Note that there is no way to be 100% secure.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/socket.h>
#include <sys/sockio.h>
#include <sys/mbuf.h>
#include <sys/errno.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/module.h>
#include <sys/proc.h>
#include <sys/queue.h>
#include <sys/rmlock.h>
#include <sys/sysctl.h>
#include <machine/cpu.h>

#include <sys/malloc.h>

#include <net/if.h>
#include <net/if_var.h>
#include <net/if_clone.h>
#include <net/route.h>
#include <net/netisr.h>
#include <net/if_types.h>
#include <net/vnet.h>

#include <netinet/in.h>
#include <netinet/in_fib.h>
#include <netinet/in_systm.h>
#include <netinet/ip.h>
#include <netinet/ip_var.h>
#include <netinet/in_var.h>

#include <netinet/ip6.h>
#include <netinet6/ip6_var.h>
#include <netinet6/in6_var.h>
#include <netinet/ip_ecn.h>

#include <netinet/ip_encap.h>

#include <machine/stdarg.h>

#include <net/bpf.h>

#include <security/mac/mac_framework.h>

SYSCTL_DECL(_net_link);
static SYSCTL_NODE(_net_link, IFT_STF, stf, CTLFLAG_RW, 0, "6to4 Interface");

static int stf_permit_rfc1918 = 0;
SYSCTL_INT(_net_link_stf, OID_AUTO, permit_rfc1918, CTLFLAG_RWTUN,
    &stf_permit_rfc1918, 0, "Permit the use of private IPv4 addresses");

#define STFUNIT		0

#define IN6_IS_ADDR_6TO4(x)	(ntohs((x)->s6_addr16[0]) == 0x2002)

/*
 * XXX: Return a pointer with 16-bit aligned.  Don't cast it to
 * struct in_addr *; use bcopy() instead.
 */
#define GET_V4(x)	(&(x)->s6_addr16[1])

struct stf_softc {
	struct ifnet	*sc_ifp;
	u_int	sc_fibnum;
	const struct encaptab *encap_cookie;
};
#define STF2IFP(sc)	((sc)->sc_ifp)

static const char stfname[] = "stf";

static MALLOC_DEFINE(M_STF, stfname, "6to4 Tunnel Interface");
static const int ip_stf_ttl = 40;

static int in_stf_input(struct mbuf *, int, int, void *);
static char *stfnames[] = {"stf0", "stf", "6to4", NULL};

static int stfmodevent(module_t, int, void *);
static int stf_encapcheck(const struct mbuf *, int, int, void *);
static int stf_getsrcifa6(struct ifnet *, struct in6_addr *, struct in6_addr *);
static int stf_output(struct ifnet *, struct mbuf *, const struct sockaddr *,
	struct route *);
static int isrfc1918addr(struct in_addr *);
static int stf_checkaddr4(struct stf_softc *, struct in_addr *,
	struct ifnet *);
static int stf_checkaddr6(struct stf_softc *, struct in6_addr *,
	struct ifnet *);
static int stf_ioctl(struct ifnet *, u_long, caddr_t);

static int stf_clone_match(struct if_clone *, const char *);
static int stf_clone_create(struct if_clone *, char *, size_t, caddr_t);
static int stf_clone_destroy(struct if_clone *, struct ifnet *);
static struct if_clone *stf_cloner;

static const struct encap_config ipv4_encap_cfg = {
	.proto = IPPROTO_IPV6,
	.min_length = sizeof(struct ip),
	.exact_match = (sizeof(in_addr_t) << 3) + 8,
	.check = stf_encapcheck,
	.input = in_stf_input
};

static int
stf_clone_match(struct if_clone *ifc, const char *name)
{
	int i;

	for(i = 0; stfnames[i] != NULL; i++) {
		if (strcmp(stfnames[i], name) == 0)
			return (1);
	}

	return (0);
}

static int
stf_clone_create(struct if_clone *ifc, char *name, size_t len, caddr_t params)
{
	char *dp;
	int err, unit, wildcard;
	struct stf_softc *sc;
	struct ifnet *ifp;

	err = ifc_name2unit(name, &unit);
	if (err != 0)
		return (err);
	wildcard = (unit < 0);

	/*
	 * We can only have one unit, but since unit allocation is
	 * already locked, we use it to keep from allocating extra
	 * interfaces.
	 */
	unit = STFUNIT;
	err = ifc_alloc_unit(ifc, &unit);
	if (err != 0)
		return (err);

	sc = malloc(sizeof(struct stf_softc), M_STF, M_WAITOK | M_ZERO);
	ifp = STF2IFP(sc) = if_alloc(IFT_STF);
	if (ifp == NULL) {
		free(sc, M_STF);
		ifc_free_unit(ifc, unit);
		return (ENOSPC);
	}
	ifp->if_softc = sc;
	sc->sc_fibnum = curthread->td_proc->p_fibnum;

	/*
	 * Set the name manually rather then using if_initname because
	 * we don't conform to the default naming convention for interfaces.
	 * In the wildcard case, we need to update the name.
	 */
	if (wildcard) {
		for (dp = name; *dp != '\0'; dp++);
		if (snprintf(dp, len - (dp-name), "%d", unit) >
		    len - (dp-name) - 1) {
			/*
			 * This can only be a programmer error and
			 * there's no straightforward way to recover if
			 * it happens.
			 */
			panic("if_clone_create(): interface name too long");
		}
	}
	strlcpy(ifp->if_xname, name, IFNAMSIZ);
	ifp->if_dname = stfname;
	ifp->if_dunit = IF_DUNIT_NONE;

	sc->encap_cookie = ip_encap_attach(&ipv4_encap_cfg, sc, M_WAITOK);
	if (sc->encap_cookie == NULL) {
		if_printf(ifp, "attach failed\n");
		free(sc, M_STF);
		ifc_free_unit(ifc, unit);
		return (ENOMEM);
	}

	ifp->if_mtu    = IPV6_MMTU;
	ifp->if_ioctl  = stf_ioctl;
	ifp->if_output = stf_output;
	ifp->if_snd.ifq_maxlen = ifqmaxlen;
	if_attach(ifp);
	bpfattach(ifp, DLT_NULL, sizeof(u_int32_t));
	return (0);
}

static int
stf_clone_destroy(struct if_clone *ifc, struct ifnet *ifp)
{
	struct stf_softc *sc = ifp->if_softc;
	int err __unused;

	err = ip_encap_detach(sc->encap_cookie);
	KASSERT(err == 0, ("Unexpected error detaching encap_cookie"));
	bpfdetach(ifp);
	if_detach(ifp);
	if_free(ifp);

	free(sc, M_STF);
	ifc_free_unit(ifc, STFUNIT);

	return (0);
}

static int
stfmodevent(module_t mod, int type, void *data)
{

	switch (type) {
	case MOD_LOAD:
		stf_cloner = if_clone_advanced(stfname, 0, stf_clone_match,
		    stf_clone_create, stf_clone_destroy);
		break;
	case MOD_UNLOAD:
		if_clone_detach(stf_cloner);
		break;
	default:
		return (EOPNOTSUPP);
	}

	return (0);
}

static moduledata_t stf_mod = {
	"if_stf",
	stfmodevent,
	0
};

DECLARE_MODULE(if_stf, stf_mod, SI_SUB_PSEUDO, SI_ORDER_ANY);

static int
stf_encapcheck(const struct mbuf *m, int off, int proto, void *arg)
{
	struct ip ip;
	struct stf_softc *sc;
	struct in_addr a, b, mask;
	struct in6_addr addr6, mask6;

	sc = (struct stf_softc *)arg;
	if (sc == NULL)
		return 0;

	if ((STF2IFP(sc)->if_flags & IFF_UP) == 0)
		return 0;

	/* IFF_LINK0 means "no decapsulation" */
	if ((STF2IFP(sc)->if_flags & IFF_LINK0) != 0)
		return 0;

	if (proto != IPPROTO_IPV6)
		return 0;

	m_copydata(m, 0, sizeof(ip), (caddr_t)&ip);

	if (ip.ip_v != 4)
		return 0;

	if (stf_getsrcifa6(STF2IFP(sc), &addr6, &mask6) != 0)
		return (0);

	/*
	 * check if IPv4 dst matches the IPv4 address derived from the
	 * local 6to4 address.
	 * success on: dst = 10.1.1.1, ia6->ia_addr = 2002:0a01:0101:...
	 */
	if (bcmp(GET_V4(&addr6), &ip.ip_dst, sizeof(ip.ip_dst)) != 0)
		return 0;

	/*
	 * check if IPv4 src matches the IPv4 address derived from the
	 * local 6to4 address masked by prefixmask.
	 * success on: src = 10.1.1.1, ia6->ia_addr = 2002:0a00:.../24
	 * fail on: src = 10.1.1.1, ia6->ia_addr = 2002:0b00:.../24
	 */
	bzero(&a, sizeof(a));
	bcopy(GET_V4(&addr6), &a, sizeof(a));
	bcopy(GET_V4(&mask6), &mask, sizeof(mask));
	a.s_addr &= mask.s_addr;
	b = ip.ip_src;
	b.s_addr &= mask.s_addr;
	if (a.s_addr != b.s_addr)
		return 0;

	/* stf interface makes single side match only */
	return 32;
}

static int
stf_getsrcifa6(struct ifnet *ifp, struct in6_addr *addr, struct in6_addr *mask)
{
	struct rm_priotracker in_ifa_tracker;
	struct ifaddr *ia;
	struct in_ifaddr *ia4;
	struct in6_ifaddr *ia6;
	struct sockaddr_in6 *sin6;
	struct in_addr in;

	if_addr_rlock(ifp);
	CK_STAILQ_FOREACH(ia, &ifp->if_addrhead, ifa_link) {
		if (ia->ifa_addr->sa_family != AF_INET6)
			continue;
		sin6 = (struct sockaddr_in6 *)ia->ifa_addr;
		if (!IN6_IS_ADDR_6TO4(&sin6->sin6_addr))
			continue;

		bcopy(GET_V4(&sin6->sin6_addr), &in, sizeof(in));
		IN_IFADDR_RLOCK(&in_ifa_tracker);
		LIST_FOREACH(ia4, INADDR_HASH(in.s_addr), ia_hash)
			if (ia4->ia_addr.sin_addr.s_addr == in.s_addr)
				break;
		IN_IFADDR_RUNLOCK(&in_ifa_tracker);
		if (ia4 == NULL)
			continue;

		ia6 = (struct in6_ifaddr *)ia;

		*addr = sin6->sin6_addr;
		*mask = ia6->ia_prefixmask.sin6_addr;
		if_addr_runlock(ifp);
		return (0);
	}
	if_addr_runlock(ifp);

	return (ENOENT);
}

static int
stf_output(struct ifnet *ifp, struct mbuf *m, const struct sockaddr *dst,
    struct route *ro)
{
	struct stf_softc *sc;
	const struct sockaddr_in6 *dst6;
	struct in_addr in4;
	const void *ptr;
	u_int8_t tos;
	struct ip *ip;
	struct ip6_hdr *ip6;
	struct in6_addr addr6, mask6;
	int error;

#ifdef MAC
	error = mac_ifnet_check_transmit(ifp, m);
	if (error) {
		m_freem(m);
		return (error);
	}
#endif

	sc = ifp->if_softc;
	dst6 = (const struct sockaddr_in6 *)dst;

	/* just in case */
	if ((ifp->if_flags & IFF_UP) == 0) {
		m_freem(m);
		if_inc_counter(ifp, IFCOUNTER_OERRORS, 1);
		return ENETDOWN;
	}

	/*
	 * If we don't have an ip4 address that match my inner ip6 address,
	 * we shouldn't generate output.  Without this check, we'll end up
	 * using wrong IPv4 source.
	 */
	if (stf_getsrcifa6(ifp, &addr6, &mask6) != 0) {
		m_freem(m);
		if_inc_counter(ifp, IFCOUNTER_OERRORS, 1);
		return ENETDOWN;
	}

	if (m->m_len < sizeof(*ip6)) {
		m = m_pullup(m, sizeof(*ip6));
		if (!m) {
			if_inc_counter(ifp, IFCOUNTER_OERRORS, 1);
			return ENOBUFS;
		}
	}
	ip6 = mtod(m, struct ip6_hdr *);
	tos = (ntohl(ip6->ip6_flow) >> 20) & 0xff;

	/*
	 * Pickup the right outer dst addr from the list of candidates.
	 * ip6_dst has priority as it may be able to give us shorter IPv4 hops.
	 */
	ptr = NULL;
	if (IN6_IS_ADDR_6TO4(&ip6->ip6_dst))
		ptr = GET_V4(&ip6->ip6_dst);
	else if (IN6_IS_ADDR_6TO4(&dst6->sin6_addr))
		ptr = GET_V4(&dst6->sin6_addr);
	else {
		m_freem(m);
		if_inc_counter(ifp, IFCOUNTER_OERRORS, 1);
		return ENETUNREACH;
	}
	bcopy(ptr, &in4, sizeof(in4));

	if (bpf_peers_present(ifp->if_bpf)) {
		/*
		 * We need to prepend the address family as
		 * a four byte field.  Cons up a dummy header
		 * to pacify bpf.  This is safe because bpf
		 * will only read from the mbuf (i.e., it won't
		 * try to free it or keep a pointer a to it).
		 */
		u_int af = AF_INET6;
		bpf_mtap2(ifp->if_bpf, &af, sizeof(af), m);
	}

	M_PREPEND(m, sizeof(struct ip), M_NOWAIT);
	if (m == NULL) {
		if_inc_counter(ifp, IFCOUNTER_OERRORS, 1);
		return ENOBUFS;
	}
	ip = mtod(m, struct ip *);

	bzero(ip, sizeof(*ip));

	bcopy(GET_V4(&addr6), &ip->ip_src, sizeof(ip->ip_src));
	bcopy(&in4, &ip->ip_dst, sizeof(ip->ip_dst));
	ip->ip_p = IPPROTO_IPV6;
	ip->ip_ttl = ip_stf_ttl;
	ip->ip_len = htons(m->m_pkthdr.len);
	if (ifp->if_flags & IFF_LINK1)
		ip_ecn_ingress(ECN_ALLOWED, &ip->ip_tos, &tos);
	else
		ip_ecn_ingress(ECN_NOCARE, &ip->ip_tos, &tos);

	M_SETFIB(m, sc->sc_fibnum);
	if_inc_counter(ifp, IFCOUNTER_OPACKETS, 1);
	error = ip_output(m, NULL, NULL, 0, NULL, NULL);

	return error;
}

static int
isrfc1918addr(struct in_addr *in)
{
	/*
	 * returns 1 if private address range:
	 * 10.0.0.0/8 172.16.0.0/12 192.168.0.0/16
	 */
	if (stf_permit_rfc1918 == 0 && (
	    (ntohl(in->s_addr) & 0xff000000) >> 24 == 10 ||
	    (ntohl(in->s_addr) & 0xfff00000) >> 16 == 172 * 256 + 16 ||
	    (ntohl(in->s_addr) & 0xffff0000) >> 16 == 192 * 256 + 168))
		return 1;

	return 0;
}

static int
stf_checkaddr4(struct stf_softc *sc, struct in_addr *in, struct ifnet *inifp)
{
	struct rm_priotracker in_ifa_tracker;
	struct in_ifaddr *ia4;

	/*
	 * reject packets with the following address:
	 * 224.0.0.0/4 0.0.0.0/8 127.0.0.0/8 255.0.0.0/8
	 */
	if (IN_MULTICAST(ntohl(in->s_addr)))
		return -1;
	switch ((ntohl(in->s_addr) & 0xff000000) >> 24) {
	case 0: case 127: case 255:
		return -1;
	}

	/*
	 * reject packets with private address range.
	 * (requirement from RFC3056 section 2 1st paragraph)
	 */
	if (isrfc1918addr(in))
		return -1;

	/*
	 * reject packets with broadcast
	 */
	IN_IFADDR_RLOCK(&in_ifa_tracker);
	CK_STAILQ_FOREACH(ia4, &V_in_ifaddrhead, ia_link) {
		if ((ia4->ia_ifa.ifa_ifp->if_flags & IFF_BROADCAST) == 0)
			continue;
		if (in->s_addr == ia4->ia_broadaddr.sin_addr.s_addr) {
			IN_IFADDR_RUNLOCK(&in_ifa_tracker);
			return -1;
		}
	}
	IN_IFADDR_RUNLOCK(&in_ifa_tracker);

	/*
	 * perform ingress filter
	 */
	if (sc && (STF2IFP(sc)->if_flags & IFF_LINK2) == 0 && inifp) {
		struct nhop4_basic nh4;

		if (fib4_lookup_nh_basic(sc->sc_fibnum, *in, 0, 0, &nh4) != 0)
			return (-1);

		if (nh4.nh_ifp != inifp)
			return (-1);
	}

	return 0;
}

static int
stf_checkaddr6(struct stf_softc *sc, struct in6_addr *in6, struct ifnet *inifp)
{
	/*
	 * check 6to4 addresses
	 */
	if (IN6_IS_ADDR_6TO4(in6)) {
		struct in_addr in4;
		bcopy(GET_V4(in6), &in4, sizeof(in4));
		return stf_checkaddr4(sc, &in4, inifp);
	}

	/*
	 * reject anything that look suspicious.  the test is implemented
	 * in ip6_input too, but we check here as well to
	 * (1) reject bad packets earlier, and
	 * (2) to be safe against future ip6_input change.
	 */
	if (IN6_IS_ADDR_V4COMPAT(in6) || IN6_IS_ADDR_V4MAPPED(in6))
		return -1;

	return 0;
}

static int
in_stf_input(struct mbuf *m, int off, int proto, void *arg)
{
	struct stf_softc *sc = arg;
	struct ip *ip;
	struct ip6_hdr *ip6;
	u_int8_t otos, itos;
	struct ifnet *ifp;

	if (proto != IPPROTO_IPV6) {
		m_freem(m);
		return (IPPROTO_DONE);
	}

	ip = mtod(m, struct ip *);
	if (sc == NULL || (STF2IFP(sc)->if_flags & IFF_UP) == 0) {
		m_freem(m);
		return (IPPROTO_DONE);
	}

	ifp = STF2IFP(sc);

#ifdef MAC
	mac_ifnet_create_mbuf(ifp, m);
#endif

	/*
	 * perform sanity check against outer src/dst.
	 * for source, perform ingress filter as well.
	 */
	if (stf_checkaddr4(sc, &ip->ip_dst, NULL) < 0 ||
	    stf_checkaddr4(sc, &ip->ip_src, m->m_pkthdr.rcvif) < 0) {
		m_freem(m);
		return (IPPROTO_DONE);
	}

	otos = ip->ip_tos;
	m_adj(m, off);

	if (m->m_len < sizeof(*ip6)) {
		m = m_pullup(m, sizeof(*ip6));
		if (!m)
			return (IPPROTO_DONE);
	}
	ip6 = mtod(m, struct ip6_hdr *);

	/*
	 * perform sanity check against inner src/dst.
	 * for source, perform ingress filter as well.
	 */
	if (stf_checkaddr6(sc, &ip6->ip6_dst, NULL) < 0 ||
	    stf_checkaddr6(sc, &ip6->ip6_src, m->m_pkthdr.rcvif) < 0) {
		m_freem(m);
		return (IPPROTO_DONE);
	}

	itos = (ntohl(ip6->ip6_flow) >> 20) & 0xff;
	if ((ifp->if_flags & IFF_LINK1) != 0)
		ip_ecn_egress(ECN_ALLOWED, &otos, &itos);
	else
		ip_ecn_egress(ECN_NOCARE, &otos, &itos);
	ip6->ip6_flow &= ~htonl(0xff << 20);
	ip6->ip6_flow |= htonl((u_int32_t)itos << 20);

	m->m_pkthdr.rcvif = ifp;

	if (bpf_peers_present(ifp->if_bpf)) {
		/*
		 * We need to prepend the address family as
		 * a four byte field.  Cons up a dummy header
		 * to pacify bpf.  This is safe because bpf
		 * will only read from the mbuf (i.e., it won't
		 * try to free it or keep a pointer a to it).
		 */
		u_int32_t af = AF_INET6;
		bpf_mtap2(ifp->if_bpf, &af, sizeof(af), m);
	}

	/*
	 * Put the packet to the network layer input queue according to the
	 * specified address family.
	 * See net/if_gif.c for possible issues with packet processing
	 * reorder due to extra queueing.
	 */
	if_inc_counter(ifp, IFCOUNTER_IPACKETS, 1);
	if_inc_counter(ifp, IFCOUNTER_IBYTES, m->m_pkthdr.len);
	M_SETFIB(m, ifp->if_fib);
	netisr_dispatch(NETISR_IPV6, m);
	return (IPPROTO_DONE);
}

static int
stf_ioctl(struct ifnet *ifp, u_long cmd, caddr_t data)
{
	struct ifaddr *ifa;
	struct ifreq *ifr;
	struct sockaddr_in6 *sin6;
	struct in_addr addr;
	int error, mtu;

	error = 0;
	switch (cmd) {
	case SIOCSIFADDR:
		ifa = (struct ifaddr *)data;
		if (ifa == NULL || ifa->ifa_addr->sa_family != AF_INET6) {
			error = EAFNOSUPPORT;
			break;
		}
		sin6 = (struct sockaddr_in6 *)ifa->ifa_addr;
		if (!IN6_IS_ADDR_6TO4(&sin6->sin6_addr)) {
			error = EINVAL;
			break;
		}
		bcopy(GET_V4(&sin6->sin6_addr), &addr, sizeof(addr));
		if (isrfc1918addr(&addr)) {
			error = EINVAL;
			break;
		}

		ifp->if_flags |= IFF_UP;
		break;

	case SIOCADDMULTI:
	case SIOCDELMULTI:
		ifr = (struct ifreq *)data;
		if (ifr && ifr->ifr_addr.sa_family == AF_INET6)
			;
		else
			error = EAFNOSUPPORT;
		break;

	case SIOCGIFMTU:
		break;

	case SIOCSIFMTU:
		ifr = (struct ifreq *)data;
		mtu = ifr->ifr_mtu;
		/* RFC 4213 3.2 ideal world MTU */
		if (mtu < IPV6_MINMTU || mtu > IF_MAXMTU - 20)
			return (EINVAL);
		ifp->if_mtu = mtu;
		break;

	default:
		error = EINVAL;
		break;
	}

	return error;
}
