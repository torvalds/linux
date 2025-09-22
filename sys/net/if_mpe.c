/* $OpenBSD: if_mpe.c,v 1.107 2025/07/07 02:28:50 jsg Exp $ */

/*
 * Copyright (c) 2008 Pierre-Yves Ritschard <pyr@spootnik.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/mbuf.h>
#include <sys/socket.h>
#include <sys/sockio.h>

#include <net/if.h>
#include <net/if_dl.h>
#include <net/if_var.h>
#include <net/if_types.h>
#include <net/route.h>

#include <netinet/in.h>
#include <netinet/ip.h>

#ifdef INET6
#include <netinet/ip6.h>
#endif /* INET6 */

#include "bpfilter.h"
#if NBPFILTER > 0
#include <net/bpf.h>
#endif

#include <netmpls/mpls.h>



#ifdef MPLS_DEBUG
#define DPRINTF(x)    do { if (mpedebug) printf x ; } while (0)
#else
#define DPRINTF(x)
#endif

struct mpe_softc {
	struct ifnet		sc_if;		/* the interface */
	int			sc_txhprio;
	int			sc_rxhprio;
	unsigned int		sc_rdomain;
	struct ifaddr		sc_ifa;
	struct sockaddr_mpls	sc_smpls;

	int			sc_dead;
};

#define MPE_HDRLEN	sizeof(struct shim_hdr)
#define MPE_MTU		1500
#define MPE_MTU_MIN	256
#define MPE_MTU_MAX	8192

void	mpeattach(int);
int	mpe_output(struct ifnet *, struct mbuf *, struct sockaddr *,
	    struct rtentry *);
int	mpe_ioctl(struct ifnet *, u_long, caddr_t);
void	mpe_start(struct ifnet *);
int	mpe_clone_create(struct if_clone *, int);
int	mpe_clone_destroy(struct ifnet *);
void	mpe_input(struct ifnet *, struct mbuf *);

struct if_clone	mpe_cloner =
    IF_CLONE_INITIALIZER("mpe", mpe_clone_create, mpe_clone_destroy);

extern int	mpls_mapttl_ip;
#ifdef INET6
extern int	mpls_mapttl_ip6;
#endif

void
mpeattach(int nmpe)
{
	if_clone_attach(&mpe_cloner);
}

int
mpe_clone_create(struct if_clone *ifc, int unit)
{
	struct mpe_softc	*sc;
	struct ifnet		*ifp;

	sc = malloc(sizeof(*sc), M_DEVBUF, M_WAITOK|M_CANFAIL|M_ZERO);
	if (sc == NULL)
		return (ENOMEM);

	ifp = &sc->sc_if;
	snprintf(ifp->if_xname, sizeof ifp->if_xname, "mpe%d", unit);
	ifp->if_flags = IFF_POINTOPOINT;
	ifp->if_xflags = IFXF_CLONED;
	ifp->if_softc = sc;
	ifp->if_mtu = MPE_MTU;
	ifp->if_ioctl = mpe_ioctl;
	ifp->if_bpf_mtap = p2p_bpf_mtap;
	ifp->if_input = p2p_input;
	ifp->if_output = mpe_output;
	ifp->if_start = mpe_start;
	ifp->if_type = IFT_MPLS;
	ifp->if_hdrlen = MPE_HDRLEN;

	sc->sc_dead = 0;

	if_counters_alloc(ifp);
	if_attach(ifp);
	if_alloc_sadl(ifp);

#if NBPFILTER > 0
	bpfattach(&ifp->if_bpf, ifp, DLT_LOOP, sizeof(u_int32_t));
#endif

	sc->sc_txhprio = 0;
	sc->sc_rxhprio = IF_HDRPRIO_PACKET;
	sc->sc_rdomain = 0;
	refcnt_init_trace(&sc->sc_ifa.ifa_refcnt, DT_REFCNT_IDX_IFADDR);
	sc->sc_ifa.ifa_ifp = ifp;
	sc->sc_ifa.ifa_addr = sdltosa(ifp->if_sadl);
	sc->sc_smpls.smpls_len = sizeof(sc->sc_smpls);
	sc->sc_smpls.smpls_family = AF_MPLS;

	return (0);
}

int
mpe_clone_destroy(struct ifnet *ifp)
{
	struct mpe_softc	*sc = ifp->if_softc;

	NET_LOCK();
	CLR(ifp->if_flags, IFF_RUNNING);
	sc->sc_dead = 1;

	if (sc->sc_smpls.smpls_label) {
		rt_ifa_del(&sc->sc_ifa, RTF_MPLS|RTF_LOCAL,
		    smplstosa(&sc->sc_smpls), sc->sc_rdomain);
	}
	NET_UNLOCK();

	ifq_barrier(&ifp->if_snd);

	if_detach(ifp);
	if (refcnt_rele(&sc->sc_ifa.ifa_refcnt) == 0) {
		panic("%s: ifa refcnt has %u refs", __func__,
		    sc->sc_ifa.ifa_refcnt.r_refs);
	}
	free(sc, M_DEVBUF, sizeof *sc);
	return (0);
}

/*
 * Start output on the mpe interface.
 */
void
mpe_start(struct ifnet *ifp)
{
	struct mpe_softc	*sc = ifp->if_softc;
	struct mbuf		*m;
	struct sockaddr		*sa;
	struct sockaddr		smpls = { .sa_family = AF_MPLS };
	struct rtentry		*rt;
	struct ifnet		*ifp0;

	while ((m = ifq_dequeue(&ifp->if_snd)) != NULL) {
		sa = mtod(m, struct sockaddr *);
		rt = rtalloc(sa, RT_RESOLVE, sc->sc_rdomain);
		if (!rtisvalid(rt)) {
			m_freem(m);
			rtfree(rt);
			continue;
		}

		ifp0 = if_get(rt->rt_ifidx);
		if (ifp0 == NULL) {
			m_freem(m);
			rtfree(rt);
			continue;
		}

		m_adj(m, sa->sa_len);

#if NBPFILTER > 0
		if (ifp->if_bpf) {
			/* remove MPLS label before passing packet to bpf */
			m->m_data += sizeof(struct shim_hdr);
			m->m_len -= sizeof(struct shim_hdr);
			m->m_pkthdr.len -= sizeof(struct shim_hdr);
			bpf_mtap_af(ifp->if_bpf, m->m_pkthdr.ph_family,
			    m, BPF_DIRECTION_OUT);
			m->m_data -= sizeof(struct shim_hdr);
			m->m_len += sizeof(struct shim_hdr);
			m->m_pkthdr.len += sizeof(struct shim_hdr);
		}
#endif

		m->m_pkthdr.ph_rtableid = sc->sc_rdomain;
		CLR(m->m_flags, M_BCAST|M_MCAST);

		mpls_output(ifp0, m, &smpls, rt);
		if_put(ifp0);
		rtfree(rt);
	}
}

int
mpe_output(struct ifnet *ifp, struct mbuf *m, struct sockaddr *dst,
	struct rtentry *rt)
{
	struct mpe_softc *sc;
	struct rt_mpls	*rtmpls;
	struct shim_hdr	shim;
	int		error;
	int		txprio;
	uint8_t		ttl = mpls_defttl;
	uint8_t		tos, prio;
	size_t		ttloff;
	socklen_t	slen;

	if (!rtisvalid(rt) || !ISSET(rt->rt_flags, RTF_MPLS)) {
		m_freem(m);
		return (ENETUNREACH);
	}

	if (dst->sa_family == AF_LINK && ISSET(rt->rt_flags, RTF_LOCAL)) {
		mpe_input(ifp, m);
		return (0);
	}

#ifdef DIAGNOSTIC
	if (ifp->if_rdomain != rtable_l2(m->m_pkthdr.ph_rtableid)) {
		printf("%s: trying to send packet on wrong domain. "
		    "if %d vs. mbuf %d\n", ifp->if_xname,
		    ifp->if_rdomain, rtable_l2(m->m_pkthdr.ph_rtableid));
	}
#endif

	rtmpls = (struct rt_mpls *)rt->rt_llinfo;
	if (rtmpls->mpls_operation != MPLS_OP_PUSH) {
		m_freem(m);
		return (ENETUNREACH);
	}

	error = 0;
	switch (dst->sa_family) {
	case AF_INET: {
		struct ip *ip = mtod(m, struct ip *);
		tos = ip->ip_tos;
		ttloff = offsetof(struct ip, ip_ttl);
		slen = sizeof(struct sockaddr_in);
		break;
	}
#ifdef INET6
	case AF_INET6: {
		struct ip6_hdr *ip6 = mtod(m, struct ip6_hdr *);
		uint32_t flow = bemtoh32(&ip6->ip6_flow);
		tos = flow >> 20;
		ttloff = offsetof(struct ip6_hdr, ip6_hlim);
		slen = sizeof(struct sockaddr_in6);
		break;
	}
#endif
	default:
		m_freem(m);
		return (EPFNOSUPPORT);
	}

	if (mpls_mapttl_ip) {
		/* assumes the ip header is already contig */
		ttl = *(mtod(m, uint8_t *) + ttloff);
	}

	sc = ifp->if_softc;
	txprio = sc->sc_txhprio;

	switch (txprio) {
	case IF_HDRPRIO_PACKET:
		prio = m->m_pkthdr.pf.prio;
		break;
	case IF_HDRPRIO_PAYLOAD:
		prio = IFQ_TOS2PRIO(tos);
		break;
	default:
		prio = txprio;
		break;
	}

	shim.shim_label = rtmpls->mpls_label | htonl(prio << MPLS_EXP_OFFSET) |
	    MPLS_BOS_MASK | htonl(ttl);

	m = m_prepend(m, sizeof(shim), M_NOWAIT);
	if (m == NULL) {
		error = ENOMEM;
		goto out;
	}
	*mtod(m, struct shim_hdr *) = shim;

	m = m_prepend(m, slen, M_WAITOK);
	if (m == NULL) {
		error = ENOMEM;
		goto out;
	}
	memcpy(mtod(m, struct sockaddr *), rt->rt_gateway, slen);
	mtod(m, struct sockaddr *)->sa_len = slen; /* to be sure */

	m->m_pkthdr.ph_family = dst->sa_family;

	error = if_enqueue(ifp, m);
out:
	if (error)
		ifp->if_oerrors++;
	return (error);
}

int
mpe_set_label(struct mpe_softc *sc, uint32_t label, unsigned int rdomain)
{
	int error;

	if (sc->sc_dead)
		return (ENXIO);

	if (sc->sc_smpls.smpls_label) {
		/* remove old MPLS route */
		rt_ifa_del(&sc->sc_ifa, RTF_MPLS|RTF_LOCAL,
		    smplstosa(&sc->sc_smpls), sc->sc_rdomain);
	}

	/* add new MPLS route */
	sc->sc_smpls.smpls_label = label;
	sc->sc_rdomain = rdomain;

	/* only install with a label or mpe_clone_destroy() will ignore it */
	if (sc->sc_smpls.smpls_label == MPLS_LABEL2SHIM(0))
		return 0;

	error = rt_ifa_add(&sc->sc_ifa, RTF_MPLS|RTF_LOCAL,
	    smplstosa(&sc->sc_smpls), sc->sc_rdomain);
	if (error)
		sc->sc_smpls.smpls_label = 0;

	return (error);
}

int
mpe_ioctl(struct ifnet *ifp, u_long cmd, caddr_t data)
{
	struct mpe_softc	*sc = ifp->if_softc;
	struct ifreq		*ifr;
	struct shim_hdr		 shim;
	int			 error = 0;

	ifr = (struct ifreq *)data;
	switch (cmd) {
	case SIOCSIFADDR:
		break;
	case SIOCSIFFLAGS:
		if (ifp->if_flags & IFF_UP)
			ifp->if_flags |= IFF_RUNNING;
		else
			ifp->if_flags &= ~IFF_RUNNING;
		break;
	case SIOCSIFMTU:
		if (ifr->ifr_mtu < MPE_MTU_MIN ||
		    ifr->ifr_mtu > MPE_MTU_MAX)
			error = EINVAL;
		else
			ifp->if_mtu = ifr->ifr_mtu;
		break;
	case SIOCGETLABEL:
		shim.shim_label = MPLS_SHIM2LABEL(sc->sc_smpls.smpls_label);
		if (shim.shim_label == 0) {
			error = EADDRNOTAVAIL;
			break;
		}
		error = copyout(&shim, ifr->ifr_data, sizeof(shim));
		break;
	case SIOCSETLABEL:
		error = copyin(ifr->ifr_data, &shim, sizeof(shim));
		if (error != 0)
			break;
		if (shim.shim_label > MPLS_LABEL_MAX ||
		    shim.shim_label <= MPLS_LABEL_RESERVED_MAX) {
			error = EINVAL;
			break;
		}
		shim.shim_label = MPLS_LABEL2SHIM(shim.shim_label);
		if (sc->sc_smpls.smpls_label != shim.shim_label) {
			error = mpe_set_label(sc, shim.shim_label,
			    sc->sc_rdomain);
		}
		break;
	case SIOCDELLABEL:
		if (sc->sc_smpls.smpls_label != MPLS_LABEL2SHIM(0)) {
			rt_ifa_del(&sc->sc_ifa, RTF_MPLS|RTF_LOCAL,
			    smplstosa(&sc->sc_smpls), sc->sc_rdomain);
		}
		sc->sc_smpls.smpls_label = MPLS_LABEL2SHIM(0);
		break;

	case SIOCSLIFPHYRTABLE:
		if (ifr->ifr_rdomainid < 0 ||
		    ifr->ifr_rdomainid > RT_TABLEID_MAX ||
		    !rtable_exists(ifr->ifr_rdomainid) ||
		    ifr->ifr_rdomainid != rtable_l2(ifr->ifr_rdomainid)) {
			error = EINVAL;
			break;
		}
		if (sc->sc_rdomain != ifr->ifr_rdomainid) {
			error = mpe_set_label(sc, sc->sc_smpls.smpls_label,
			    ifr->ifr_rdomainid);
		}
		break;
	case SIOCGLIFPHYRTABLE:
		ifr->ifr_rdomainid = sc->sc_rdomain;
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
		return (ENOTTY);
	}

	return (error);
}

void
mpe_input(struct ifnet *ifp, struct mbuf *m)
{
	struct mpe_softc *sc = ifp->if_softc;
	struct shim_hdr	*shim;
	struct mbuf	*n;
	uint8_t		 ttl, tos;
	uint32_t	 exp;
	int rxprio = sc->sc_rxhprio;

	shim = mtod(m, struct shim_hdr *);
	exp = ntohl(shim->shim_label & MPLS_EXP_MASK) >> MPLS_EXP_OFFSET;
	if (!MPLS_BOS_ISSET(shim->shim_label))
		goto drop;

	ttl = ntohl(shim->shim_label & MPLS_TTL_MASK);
	m_adj(m, sizeof(*shim));

	n = m;
	while (n->m_len == 0) {
		n = n->m_next;
		if (n == NULL)
			goto drop;
	}

	switch (*mtod(n, uint8_t *) >> 4) {
	case 4: {
		struct ip *ip;
		if (m->m_len < sizeof(*ip)) {
			m = m_pullup(m, sizeof(*ip));
			if (m == NULL)
				return;
		}
		ip = mtod(m, struct ip *);
		tos = ip->ip_tos;

		if (mpls_mapttl_ip) {
			m = mpls_ip_adjttl(m, ttl);
			if (m == NULL)
				return;
		}

		m->m_pkthdr.ph_family = AF_INET;
		break;
	}
#ifdef INET6
	case 6: {
		struct ip6_hdr *ip6;
		uint32_t flow;
		if (m->m_len < sizeof(*ip6)) {
			m = m_pullup(m, sizeof(*ip6));
			if (m == NULL)
				return;
		}
		ip6 = mtod(m, struct ip6_hdr *);
		flow = bemtoh32(&ip6->ip6_flow);
		tos = flow >> 20;

		if (mpls_mapttl_ip6) {
			m = mpls_ip6_adjttl(m, ttl);
			if (m == NULL)
				return;
		}

		m->m_pkthdr.ph_family = AF_INET6;
		break;
	}
#endif /* INET6 */
	default:
		goto drop;
	}

	switch (rxprio) {
	case IF_HDRPRIO_PACKET:
		/* nop */
		break;
	case IF_HDRPRIO_OUTER:
		m->m_pkthdr.pf.prio = exp;
		break;
	case IF_HDRPRIO_PAYLOAD:
		m->m_pkthdr.pf.prio = IFQ_TOS2PRIO(tos);
		break;
	default:
		m->m_pkthdr.pf.prio = rxprio;
		break;
	}

	if_vinput(ifp, m, NULL);
	return;
drop:
	m_freem(m);
}
