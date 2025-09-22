/*	$OpenBSD: if_gif.c,v 1.141 2025/07/07 02:28:50 jsg Exp $	*/
/*	$KAME: if_gif.c,v 1.43 2001/02/20 08:51:07 itojun Exp $	*/

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
#include <sys/sockio.h>
#include <sys/queue.h>

#include <net/if.h>
#include <net/if_var.h>
#include <net/if_types.h>

#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/ip_var.h>
#include <netinet/ip_ipip.h>
#include <netinet/ip_ecn.h>

#ifdef INET6
#include <netinet/ip6.h>
#include <netinet6/ip6_var.h>
#endif /* INET6 */

#include <net/if_gif.h>

#include "bpfilter.h"
#if NBPFILTER > 0
#include <net/bpf.h>
#endif

#ifdef MPLS
#include <netmpls/mpls.h>
#endif

#include "pf.h"
#if NPF > 0
#include <net/pfvar.h>
#endif

#define GIF_MTU		(1280)	/* Default MTU */
#define GIF_MTU_MIN	(1280)	/* Minimum MTU */
#define GIF_MTU_MAX	(8192)	/* Maximum MTU */

union gif_addr {
	struct in6_addr		in6;
	struct in_addr		in4;
};

struct gif_tunnel {
	TAILQ_ENTRY(gif_tunnel)	t_entry;

	union gif_addr		t_src;
#define t_src4		t_src.in4
#define t_src6		t_src.in6
	union gif_addr		t_dst;
#define t_dst4		t_dst.in4
#define t_dst6		t_dst.in6
	u_int			t_rtableid;

	sa_family_t		t_af;
};

TAILQ_HEAD(gif_list, gif_tunnel);

static inline int	gif_cmp(const struct gif_tunnel *,
			    const struct gif_tunnel *);

struct gif_softc {
	struct gif_tunnel	sc_tunnel; /* must be first */
	struct ifnet		sc_if;
	uint16_t		sc_df;
	int			sc_ttl;
	int			sc_txhprio;
	int			sc_rxhprio;
	int			sc_ecn;
};

struct gif_list gif_list = TAILQ_HEAD_INITIALIZER(gif_list);

void	gifattach(int);
int	gif_clone_create(struct if_clone *, int);
int	gif_clone_destroy(struct ifnet *);

void	gif_start(struct ifnet *);
int	gif_ioctl(struct ifnet *, u_long, caddr_t);
int	gif_output(struct ifnet *, struct mbuf *, struct sockaddr *,
	    struct rtentry *);
int	gif_send(struct gif_softc *, struct mbuf *, uint8_t, uint8_t, uint8_t);

int	gif_up(struct gif_softc *);
int	gif_down(struct gif_softc *);
int	gif_set_tunnel(struct gif_softc *, struct if_laddrreq *);
int	gif_get_tunnel(struct gif_softc *, struct if_laddrreq *);
int	gif_del_tunnel(struct gif_softc *);
int	gif_input(struct gif_tunnel *, struct mbuf **, int *, int, int,
	    uint8_t, struct netstack *);

/*
 * gif global variable definitions
 */
struct if_clone gif_cloner =
    IF_CLONE_INITIALIZER("gif", gif_clone_create, gif_clone_destroy);

void
gifattach(int count)
{
	if_clone_attach(&gif_cloner);
}

int
gif_clone_create(struct if_clone *ifc, int unit)
{
	struct gif_softc *sc;
	struct ifnet *ifp;

	sc = malloc(sizeof(*sc), M_DEVBUF, M_WAITOK|M_ZERO);
	ifp = &sc->sc_if;

	sc->sc_df = htons(0);
	sc->sc_ttl = atomic_load_int(&ip_defttl);
	sc->sc_txhprio = IF_HDRPRIO_PAYLOAD;
	sc->sc_rxhprio = IF_HDRPRIO_PAYLOAD;
	sc->sc_ecn = ECN_ALLOWED;

	snprintf(ifp->if_xname, sizeof(ifp->if_xname),
	    "%s%d", ifc->ifc_name, unit);

	ifp->if_mtu    = GIF_MTU;
	ifp->if_flags  = IFF_POINTOPOINT | IFF_MULTICAST;
	ifp->if_xflags = IFXF_CLONED;
	ifp->if_ioctl  = gif_ioctl;
	ifp->if_bpf_mtap = p2p_bpf_mtap;
	ifp->if_input  = p2p_input;
	ifp->if_start  = gif_start;
	ifp->if_output = gif_output;
	ifp->if_rtrequest = p2p_rtrequest;
	ifp->if_type   = IFT_GIF;
	ifp->if_softc = sc;

	if_counters_alloc(ifp);
	if_attach(ifp);
	if_alloc_sadl(ifp);

#if NBPFILTER > 0
	bpfattach(&ifp->if_bpf, ifp, DLT_LOOP, sizeof(uint32_t));
#endif

	NET_LOCK();
	TAILQ_INSERT_TAIL(&gif_list, &sc->sc_tunnel, t_entry);
	NET_UNLOCK();

	return (0);
}

int
gif_clone_destroy(struct ifnet *ifp)
{
	struct gif_softc *sc = ifp->if_softc;

	NET_LOCK();
	if (ISSET(ifp->if_flags, IFF_RUNNING))
		gif_down(sc);

	TAILQ_REMOVE(&gif_list, &sc->sc_tunnel, t_entry);
	NET_UNLOCK();

	if_detach(ifp);

	free(sc, M_DEVBUF, sizeof(*sc));

	return (0);
}

void
gif_start(struct ifnet *ifp)
{
	struct gif_softc *sc = ifp->if_softc;
	struct mbuf *m;
#if NBPFILTER > 0
	caddr_t if_bpf;
#endif
	uint8_t proto, ttl, tos;
	int ttloff, tttl;

	tttl = sc->sc_ttl;

	while ((m = ifq_dequeue(&ifp->if_snd)) != NULL) {
#if NBPFILTER > 0
		if_bpf = ifp->if_bpf;
		if (if_bpf) {
			bpf_mtap_af(if_bpf, m->m_pkthdr.ph_family, m,
			    BPF_DIRECTION_OUT);
		}
#endif

		switch (m->m_pkthdr.ph_family) {
		case AF_INET: {
			struct ip *ip;

			m = m_pullup(m, sizeof(*ip));
			if (m == NULL)
				continue;

			ip = mtod(m, struct ip *);
			tos = ip->ip_tos;

			ttloff = offsetof(struct ip, ip_ttl);
			proto = IPPROTO_IPV4;
			break;
		}
#ifdef INET6
		case AF_INET6: {
			struct ip6_hdr *ip6;

			m = m_pullup(m, sizeof(*ip6));
			if (m == NULL)
				continue;

			ip6 = mtod(m, struct ip6_hdr *);
			tos = ntohl(ip6->ip6_flow >> 20);

			ttloff = offsetof(struct ip6_hdr, ip6_hlim);
			proto = IPPROTO_IPV6;
			break;
		}
#endif
#ifdef MPLS
		case AF_MPLS: {
			uint32_t shim;

			m = m_pullup(m, sizeof(shim));
			if (m == NULL)
				continue;

			shim = *mtod(m, uint32_t *) & MPLS_EXP_MASK;
			tos = (ntohl(shim) >> MPLS_EXP_OFFSET) << 5;

			ttloff = 3;

			proto = IPPROTO_MPLS;
			break;
		}
#endif
		default:
			unhandled_af(m->m_pkthdr.ph_family);
		}

		if (tttl == -1) {
			KASSERT(m->m_len > ttloff);

			ttl = *(m->m_data + ttloff);
		} else
			ttl = tttl;

		switch (sc->sc_txhprio) {
		case IF_HDRPRIO_PAYLOAD:
			/* tos is already set */
			break;
		case IF_HDRPRIO_PACKET:
			tos = IFQ_PRIO2TOS(m->m_pkthdr.pf.prio);
			break;
		default:
			tos = IFQ_PRIO2TOS(sc->sc_txhprio);
			break;
		}

		gif_send(sc, m, proto, ttl, tos);
	}
}

int
gif_send(struct gif_softc *sc, struct mbuf *m,
    uint8_t proto, uint8_t ttl, uint8_t itos)
{
	uint8_t otos;

	m->m_flags &= ~(M_BCAST|M_MCAST);
	m->m_pkthdr.ph_rtableid = sc->sc_tunnel.t_rtableid;

#if NPF > 0
	pf_pkt_addr_changed(m);
#endif

	ip_ecn_ingress(sc->sc_ecn, &otos, &itos);

	switch (sc->sc_tunnel.t_af) {
	case AF_INET: {
		struct ip *ip;

		if (in_nullhost(sc->sc_tunnel.t_dst4))
			goto drop;

		m = m_prepend(m, sizeof(*ip), M_DONTWAIT);
		if (m == NULL)
			return (-1);

		ip = mtod(m, struct ip *);
		ip->ip_off = sc->sc_df;
		ip->ip_tos = otos;
		ip->ip_len = htons(m->m_pkthdr.len);
		ip->ip_ttl = ttl;
		ip->ip_p = proto;
		ip->ip_src = sc->sc_tunnel.t_src4;
		ip->ip_dst = sc->sc_tunnel.t_dst4;

		ip_send(m);
		break;
	}
#ifdef INET6
	case AF_INET6: {
		struct ip6_hdr *ip6;
		int len = m->m_pkthdr.len;
		uint32_t flow;

		if (IN6_IS_ADDR_UNSPECIFIED(&sc->sc_tunnel.t_dst6))
			goto drop;

		m = m_prepend(m, sizeof(*ip6), M_DONTWAIT);
		if (m == NULL)
			return (-1);

		flow = otos << 20;
		if (ISSET(m->m_pkthdr.csum_flags, M_FLOWID))
			flow |= m->m_pkthdr.ph_flowid;

		ip6 = mtod(m, struct ip6_hdr *);
		ip6->ip6_flow = htonl(flow);
		ip6->ip6_vfc |= IPV6_VERSION;
		ip6->ip6_plen = htons(len);
		ip6->ip6_nxt = proto;
		ip6->ip6_hlim = ttl;
		ip6->ip6_src = sc->sc_tunnel.t_src6;
		ip6->ip6_dst = sc->sc_tunnel.t_dst6;

		if (sc->sc_df)
			SET(m->m_pkthdr.csum_flags, M_IPV6_DF_OUT);

		ip6_send(m);
		break;
	}
#endif
	default:
		m_freem(m);
		break;
	}

	return (0);

drop:
	m_freem(m);
	return (0);
}

int
gif_output(struct ifnet *ifp, struct mbuf *m, struct sockaddr *dst,
    struct rtentry *rt)
{
	struct m_tag *mtag;
	int error = 0;

	if (!ISSET(ifp->if_flags, IFF_RUNNING)) {
		error = ENETDOWN;
		goto drop;
	}

	switch (dst->sa_family) {
	case AF_INET:
#ifdef INET6
	case AF_INET6:
#endif
#ifdef MPLS
	case AF_MPLS:
#endif
		break;
	default:
		error = EAFNOSUPPORT;
		goto drop;
	}

	/* Try to limit infinite recursion through misconfiguration. */
	for (mtag = m_tag_find(m, PACKET_TAG_GRE, NULL); mtag;
	     mtag = m_tag_find(m, PACKET_TAG_GRE, mtag)) {
		if (memcmp((caddr_t)(mtag + 1), &ifp->if_index,
		    sizeof(ifp->if_index)) == 0) {
			error = EIO;
			goto drop;
		}
	}

	mtag = m_tag_get(PACKET_TAG_GRE, sizeof(ifp->if_index), M_NOWAIT);
	if (mtag == NULL) {
		error = ENOBUFS;
		goto drop;
	}
	memcpy((caddr_t)(mtag + 1), &ifp->if_index, sizeof(ifp->if_index));
	m_tag_prepend(m, mtag);

	m->m_pkthdr.ph_family = dst->sa_family;

	error = if_enqueue(ifp, m);

	if (error)
		ifp->if_oerrors++;
	return (error);

drop:
	m_freem(m);
	return (error);
}

int
gif_up(struct gif_softc *sc)
{
	NET_ASSERT_LOCKED();

	SET(sc->sc_if.if_flags, IFF_RUNNING);

	return (0);
}

int
gif_down(struct gif_softc *sc)
{
	NET_ASSERT_LOCKED();

	CLR(sc->sc_if.if_flags, IFF_RUNNING);

	/* barrier? */

	return (0);
}

int
gif_ioctl(struct ifnet *ifp, u_long cmd, caddr_t data)
{
	struct gif_softc *sc = ifp->if_softc;
	struct ifreq *ifr = (struct ifreq *)data;
	int error = 0;

	switch (cmd) {
	case SIOCSIFADDR:
		SET(ifp->if_flags, IFF_UP);
		/* FALLTHROUGH */
	case SIOCSIFFLAGS:
		if (ISSET(ifp->if_flags, IFF_UP)) {
			if (!ISSET(ifp->if_flags, IFF_RUNNING))
				error = gif_up(sc);
			else
				error = 0;
		} else {
			if (ISSET(ifp->if_flags, IFF_RUNNING))
				error = gif_down(sc);
		}
		break;

	case SIOCADDMULTI:
	case SIOCDELMULTI:
		break;

	case SIOCSLIFPHYADDR:
		error = gif_set_tunnel(sc, (struct if_laddrreq *)data);
		break;
	case SIOCGLIFPHYADDR:
		error = gif_get_tunnel(sc, (struct if_laddrreq *)data);
		break;
	case SIOCDIFPHYADDR:
		error = gif_del_tunnel(sc);
		break;

	case SIOCSIFMTU:
		if (ifr->ifr_mtu < GIF_MTU_MIN || ifr->ifr_mtu > GIF_MTU_MAX) {
			error = EINVAL;
			break;
		}

		ifp->if_mtu = ifr->ifr_mtu;
		break;

	case SIOCSLIFPHYRTABLE:
		if (ifr->ifr_rdomainid < 0 ||
		    ifr->ifr_rdomainid > RT_TABLEID_MAX ||
		    !rtable_exists(ifr->ifr_rdomainid)) {
			error = EINVAL;
			break;
		}
		sc->sc_tunnel.t_rtableid = ifr->ifr_rdomainid;
		break;
	case SIOCGLIFPHYRTABLE:
		ifr->ifr_rdomainid = sc->sc_tunnel.t_rtableid;
		break;

	case SIOCSLIFPHYTTL:
		if (ifr->ifr_ttl != -1 &&
		    (ifr->ifr_ttl < 1 || ifr->ifr_ttl > 0xff)) {
			error = EINVAL;
			break;
		}

		/* commit */
		sc->sc_ttl = ifr->ifr_ttl;
		break;
	case SIOCGLIFPHYTTL:
		ifr->ifr_ttl = sc->sc_ttl;
		break;

	case SIOCSLIFPHYDF:
		/* commit */
		sc->sc_df = ifr->ifr_df ? htons(IP_DF) : htons(0);
		break;
	case SIOCGLIFPHYDF:
		ifr->ifr_df = sc->sc_df ? 1 : 0;
		break;

	case SIOCSLIFPHYECN:
		sc->sc_ecn = ifr->ifr_metric ? ECN_ALLOWED : ECN_FORBIDDEN;
		break;
	case SIOCGLIFPHYECN:
		ifr->ifr_metric = (sc->sc_ecn == ECN_ALLOWED);
		break;

	case SIOCSTXHPRIO:
		error = if_txhprio_l3_check(ifr->ifr_hdrprio);
		if (error != 0)
			break;

		sc->sc_txhprio = ifr->ifr_hdrprio;
		break;
	case SIOCGTXHPRIO:
		ifr->ifr_hdrprio = sc->sc_txhprio;
		break;

	case SIOCSRXHPRIO:
		error = if_rxhprio_l3_check(ifr->ifr_hdrprio);
		if (error != 0)
			break;

		sc->sc_rxhprio = ifr->ifr_hdrprio;
		break;
	case SIOCGRXHPRIO:
		ifr->ifr_hdrprio = sc->sc_rxhprio;
		break;

	default:
		error = ENOTTY;
		break;
	}

	return (error);
}

int
gif_get_tunnel(struct gif_softc *sc, struct if_laddrreq *req)
{
	struct gif_tunnel *tunnel = &sc->sc_tunnel;
	struct sockaddr *src = (struct sockaddr *)&req->addr;
	struct sockaddr *dst = (struct sockaddr *)&req->dstaddr;
	struct sockaddr_in *sin;
#ifdef INET6 /* ifconfig already embeds the scopeid */
	struct sockaddr_in6 *sin6;
#endif

	switch (tunnel->t_af) {
	case AF_UNSPEC:
		return (EADDRNOTAVAIL);
	case AF_INET:
		sin = (struct sockaddr_in *)src;
		memset(sin, 0, sizeof(*sin));
		sin->sin_family = AF_INET;
		sin->sin_len = sizeof(*sin);
		sin->sin_addr = tunnel->t_src4;

		sin = (struct sockaddr_in *)dst;
		memset(sin, 0, sizeof(*sin));
		sin->sin_family = AF_INET;
		sin->sin_len = sizeof(*sin);
		sin->sin_addr = tunnel->t_dst4;

		break;

#ifdef INET6
	case AF_INET6:
		sin6 = (struct sockaddr_in6 *)src;
		memset(sin6, 0, sizeof(*sin6));
		sin6->sin6_family = AF_INET6;
		sin6->sin6_len = sizeof(*sin6);
		in6_recoverscope(sin6, &tunnel->t_src6);

		sin6 = (struct sockaddr_in6 *)dst;
		memset(sin6, 0, sizeof(*sin6));
		sin6->sin6_family = AF_INET6;
		sin6->sin6_len = sizeof(*sin6);
		in6_recoverscope(sin6, &tunnel->t_dst6);

		break;
#endif
	default:
		return (EAFNOSUPPORT);
	}

	return (0);
}

int
gif_set_tunnel(struct gif_softc *sc, struct if_laddrreq *req)
{
	struct gif_tunnel *tunnel = &sc->sc_tunnel;
	struct sockaddr *src = (struct sockaddr *)&req->addr;
	struct sockaddr *dst = (struct sockaddr *)&req->dstaddr;
	struct sockaddr_in *src4, *dst4;
#ifdef INET6
	struct sockaddr_in6 *src6, *dst6;
	int error;
#endif

	/* sa_family and sa_len must be equal */
	if (src->sa_family != dst->sa_family || src->sa_len != dst->sa_len)
		return (EINVAL);

	/* validate */
	switch (dst->sa_family) {
	case AF_INET:
		if (dst->sa_len != sizeof(*dst4))
			return (EINVAL);

		src4 = (struct sockaddr_in *)src;
		if (in_nullhost(src4->sin_addr) ||
		    IN_MULTICAST(src4->sin_addr.s_addr))
			return (EINVAL);

		dst4 = (struct sockaddr_in *)dst;
		/* dst4 can be 0.0.0.0 */
		if (IN_MULTICAST(dst4->sin_addr.s_addr))
			return (EINVAL);

		tunnel->t_src4 = src4->sin_addr;
		tunnel->t_dst4 = dst4->sin_addr;

		break;
#ifdef INET6
	case AF_INET6:
		if (dst->sa_len != sizeof(*dst6))
			return (EINVAL);

		src6 = (struct sockaddr_in6 *)src;
		if (IN6_IS_ADDR_UNSPECIFIED(&src6->sin6_addr) ||
		    IN6_IS_ADDR_MULTICAST(&src6->sin6_addr))
			return (EINVAL);

		dst6 = (struct sockaddr_in6 *)dst;
		if (IN6_IS_ADDR_MULTICAST(&dst6->sin6_addr))
			return (EINVAL);

		error = in6_embedscope(&tunnel->t_src6, src6, NULL, NULL);
		if (error != 0)
			return (error);

		error = in6_embedscope(&tunnel->t_dst6, dst6, NULL, NULL);
		if (error != 0)
			return (error);

		break;
#endif
	default:
		return (EAFNOSUPPORT);
	}

	/* commit */
	tunnel->t_af = dst->sa_family;

	return (0);
}

int
gif_del_tunnel(struct gif_softc *sc)
{
	/* commit */
	sc->sc_tunnel.t_af = AF_UNSPEC;

	return (0);
}

int
in_gif_input(struct mbuf **mp, int *offp, int proto, int af,
    struct netstack *ns)
{
	struct mbuf *m = *mp;
	struct gif_tunnel key;
	struct ip *ip;
	int rv;

	ip = mtod(m, struct ip *);

	key.t_af = AF_INET;
	key.t_src4 = ip->ip_dst;
	key.t_dst4 = ip->ip_src;

	rv = gif_input(&key, mp, offp, proto, af, ip->ip_tos, ns);
	if (rv == -1)
		rv = ipip_input(mp, offp, proto, af, ns);

	return (rv);
}

#ifdef INET6
int
in6_gif_input(struct mbuf **mp, int *offp, int proto, int af,
    struct netstack *ns)
{
	struct mbuf *m = *mp;
	struct gif_tunnel key;
	struct ip6_hdr *ip6;
	uint32_t flow;
	int rv;

	ip6 = mtod(m, struct ip6_hdr *);

	key.t_af = AF_INET6;
	key.t_src6 = ip6->ip6_dst;
	key.t_dst6 = ip6->ip6_src;

	flow = ntohl(ip6->ip6_flow);

	rv = gif_input(&key, mp, offp, proto, af, flow >> 20, ns);
	if (rv == -1)
		rv = ipip_input(mp, offp, proto, af, ns);

	return (rv);
}
#endif /* INET6 */

struct gif_softc *
gif_find(const struct gif_tunnel *key)
{
	struct gif_tunnel *t;
	struct gif_softc *sc;

	TAILQ_FOREACH(t, &gif_list, t_entry) {
		if (gif_cmp(key, t) != 0)
			continue;

		sc = (struct gif_softc *)t;
		if (!ISSET(sc->sc_if.if_flags, IFF_RUNNING))
			continue;

		return (sc);
	}

	return (NULL);
}

int
gif_input(struct gif_tunnel *key, struct mbuf **mp, int *offp, int proto,
    int af, uint8_t otos, struct netstack *ns)
{
	struct mbuf *m = *mp;
	struct gif_softc *sc;
	struct ifnet *ifp;
	uint8_t itos;
	int rxhprio;

	/* IP-in-IP header is caused by tunnel mode, so skip gif lookup */
	if (m->m_flags & M_TUNNEL) {
		m->m_flags &= ~M_TUNNEL;
		return (-1);
	}
	
	key->t_rtableid = m->m_pkthdr.ph_rtableid;

	sc = gif_find(key);
	if (sc == NULL) {
		memset(&key->t_dst, 0, sizeof(key->t_dst));
		sc = gif_find(key);
		if (sc == NULL)
			return (-1);
	}

	m_adj(m, *offp); /* this is ours now */

	ifp = &sc->sc_if;
	rxhprio = sc->sc_rxhprio;

	switch (proto) {
	case IPPROTO_IPV4: {
		struct ip *ip;

		m = *mp = m_pullup(m, sizeof(*ip));
		if (m == NULL)
			return (IPPROTO_DONE);

		ip = mtod(m, struct ip *);

		itos = ip->ip_tos;
		if (ip_ecn_egress(sc->sc_ecn, &otos, &itos) == 0)
			goto drop;

		if (itos != ip->ip_tos)
			ip_tos_patch(ip, itos);

		m->m_pkthdr.ph_family = AF_INET;
		break;
	}
#ifdef INET6
	case IPPROTO_IPV6: {
		struct ip6_hdr *ip6;

		m = *mp = m_pullup(m, sizeof(*ip6));
		if (m == NULL)
			return (IPPROTO_DONE);

		ip6 = mtod(m, struct ip6_hdr *);

		itos = ntohl(ip6->ip6_flow) >> 20;
		if (!ip_ecn_egress(sc->sc_ecn, &otos, &itos))
			goto drop;

		CLR(ip6->ip6_flow, htonl(0xff << 20));
		SET(ip6->ip6_flow, htonl(itos << 20));

		m->m_pkthdr.ph_family = AF_INET6;
		break;
	}
#endif /* INET6 */
#ifdef MPLS
	case IPPROTO_MPLS: {
		uint32_t shim;
		m = *mp = m_pullup(m, sizeof(shim));
		if (m == NULL)
			return (IPPROTO_DONE);

		shim = *mtod(m, uint32_t *) & MPLS_EXP_MASK;
		itos = (ntohl(shim) >> MPLS_EXP_OFFSET) << 5;
	
		m->m_pkthdr.ph_family = AF_MPLS;
		break;
	}
#endif /* MPLS */
	default:
		return (-1);
	}

	m->m_flags &= ~(M_MCAST|M_BCAST);

	switch (rxhprio) {
	case IF_HDRPRIO_PACKET:
		/* nop */
		break;
	case IF_HDRPRIO_PAYLOAD:
		m->m_pkthdr.pf.prio = IFQ_TOS2PRIO(itos);
		break;
	case IF_HDRPRIO_OUTER:
		m->m_pkthdr.pf.prio = IFQ_TOS2PRIO(otos);
		break;
	default:
		m->m_pkthdr.pf.prio = rxhprio;
		break;
	}

	*mp = NULL;
	if_vinput(ifp, m, ns);
	return (IPPROTO_DONE);

 drop:
	m_freemp(mp);
	return (IPPROTO_DONE);
}

static inline int
gif_ip_cmp(int af, const union gif_addr *a, const union gif_addr *b)
{
	switch (af) {
#ifdef INET6
	case AF_INET6:
		return (memcmp(&a->in6, &b->in6, sizeof(a->in6)));
#endif /* INET6 */
	case AF_INET:
		return (memcmp(&a->in4, &b->in4, sizeof(a->in4)));
	default:
		panic("%s: unsupported af %d", __func__, af);
	}

	return (0);
}


static inline int
gif_cmp(const struct gif_tunnel *a, const struct gif_tunnel *b)
{
	int rv;

	/* sort by routing table */
	if (a->t_rtableid > b->t_rtableid)
		return (1);
	if (a->t_rtableid < b->t_rtableid)
		return (-1);

	/* sort by address */
	if (a->t_af > b->t_af)
		return (1);
	if (a->t_af < b->t_af)
		return (-1);

	rv = gif_ip_cmp(a->t_af, &a->t_dst, &b->t_dst);
	if (rv != 0)
		return (rv);

	rv = gif_ip_cmp(a->t_af, &a->t_src, &b->t_src);
	if (rv != 0)
		return (rv);

	return (0);
}
