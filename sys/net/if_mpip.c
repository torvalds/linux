/*	$OpenBSD: if_mpip.c,v 1.20 2025/03/02 21:28:32 bluhm Exp $ */

/*
 * Copyright (c) 2015 Rafael Zalamena <rzalamena@openbsd.org>
 * Copyright (c) 2019 David Gwynne <dlg@openbsd.org>
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

#include "bpfilter.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/mbuf.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <sys/errno.h>

#include <net/if.h>
#include <net/if_var.h>
#include <net/if_dl.h>
#include <net/if_types.h>
#include <net/route.h>

#include <netinet/in.h>
#include <netinet/ip.h>

#ifdef INET6
#include <netinet/ip6.h>
#endif

#include <netmpls/mpls.h>

#if NBPFILTER > 0
#include <net/bpf.h>
#endif /* NBPFILTER */

struct mpip_neighbor {
	struct shim_hdr		n_rshim;
	struct sockaddr_storage	n_nexthop;
};

struct mpip_softc {
	struct ifnet		sc_if;
	unsigned int		sc_dead;
	uint32_t		sc_flow; /* xor for mbuf flowid */

	int			sc_txhprio;
	int			sc_rxhprio;
	struct ifaddr		sc_ifa;
	struct sockaddr_mpls	sc_smpls; /* Local label */
	unsigned int		sc_rdomain;
	struct mpip_neighbor	*sc_neighbor;

	unsigned int		sc_cword; /* control word */
	unsigned int		sc_fword; /* flow-aware transport */
	int			sc_ttl;
};

void	mpipattach(int);
int	mpip_clone_create(struct if_clone *, int);
int	mpip_clone_destroy(struct ifnet *);
int	mpip_ioctl(struct ifnet *, u_long, caddr_t);
int	mpip_output(struct ifnet *, struct mbuf *, struct sockaddr *,
	    struct rtentry *);
void	mpip_start(struct ifnet *);

struct if_clone mpip_cloner =
    IF_CLONE_INITIALIZER("mpip", mpip_clone_create, mpip_clone_destroy);

void
mpipattach(int n)
{
	if_clone_attach(&mpip_cloner);
}

int
mpip_clone_create(struct if_clone *ifc, int unit)
{
	struct mpip_softc *sc;
	struct ifnet *ifp;

	sc = malloc(sizeof(*sc), M_DEVBUF, M_WAITOK|M_CANFAIL|M_ZERO);
	if (sc == NULL)
		return (ENOMEM);

	sc->sc_txhprio = 0;
	sc->sc_rxhprio = IF_HDRPRIO_PACKET;
	sc->sc_neighbor = 0;
	sc->sc_cword = 0; /* default to no control word */
	sc->sc_fword = 0; /* both sides have to agree on FAT first */
	sc->sc_flow = arc4random() & 0xfffff;
	sc->sc_smpls.smpls_len = sizeof(sc->sc_smpls);
	sc->sc_smpls.smpls_family = AF_MPLS;
	sc->sc_ttl = -1;

	ifp = &sc->sc_if;
	snprintf(ifp->if_xname, sizeof(ifp->if_xname), "%s%d",
	    ifc->ifc_name, unit);
	ifp->if_softc = sc;
	ifp->if_type = IFT_TUNNEL;
	ifp->if_flags = IFF_POINTOPOINT | IFF_MULTICAST;
	ifp->if_xflags = IFXF_CLONED;
	ifp->if_ioctl = mpip_ioctl;
	ifp->if_bpf_mtap = p2p_bpf_mtap;
	ifp->if_input = p2p_input;
	ifp->if_output = mpip_output;
	ifp->if_start = mpip_start;
	ifp->if_rtrequest = p2p_rtrequest;
	ifp->if_mtu = 1500;
	ifp->if_hardmtu = 65535;

	if_counters_alloc(ifp);
	if_attach(ifp);
	if_alloc_sadl(ifp);

#if NBPFILTER > 0
	bpfattach(&ifp->if_bpf, ifp, DLT_LOOP, sizeof(uint32_t));
#endif

	refcnt_init_trace(&sc->sc_ifa.ifa_refcnt, DT_REFCNT_IDX_IFADDR);
	sc->sc_ifa.ifa_ifp = ifp;
	sc->sc_ifa.ifa_addr = sdltosa(ifp->if_sadl);

	return (0);
}

int
mpip_clone_destroy(struct ifnet *ifp)
{
	struct mpip_softc *sc = ifp->if_softc;

	NET_LOCK();
	ifp->if_flags &= ~IFF_RUNNING;
	sc->sc_dead = 1;

	if (sc->sc_smpls.smpls_label) {
		rt_ifa_del(&sc->sc_ifa, RTF_LOCAL | RTF_MPLS,
		    smplstosa(&sc->sc_smpls), sc->sc_rdomain);
	}
	NET_UNLOCK();

	ifq_barrier(&ifp->if_snd);

	if_detach(ifp);
	if (refcnt_rele(&sc->sc_ifa.ifa_refcnt) == 0) {
		panic("%s: ifa refcnt has %u refs", __func__,
		    sc->sc_ifa.ifa_refcnt.r_refs);
	}
	free(sc->sc_neighbor, M_DEVBUF, sizeof(*sc->sc_neighbor));
	free(sc, M_DEVBUF, sizeof(*sc));

	return (0);
}

static int
mpip_set_route(struct mpip_softc *sc, uint32_t shim, unsigned int rdomain)
{
	int error;

	rt_ifa_del(&sc->sc_ifa, RTF_MPLS | RTF_LOCAL,
	    smplstosa(&sc->sc_smpls), sc->sc_rdomain);

	sc->sc_smpls.smpls_label = shim;
	sc->sc_rdomain = rdomain;

	/* only install with a label or mpip_clone_destroy() will ignore it */
	if (sc->sc_smpls.smpls_label == MPLS_LABEL2SHIM(0))
		return 0;

	error = rt_ifa_add(&sc->sc_ifa, RTF_MPLS | RTF_LOCAL,
	    smplstosa(&sc->sc_smpls), sc->sc_rdomain);
	if (error) {
		sc->sc_smpls.smpls_label = MPLS_LABEL2SHIM(0);
		return (error);
	}

	return (0);
}

static int
mpip_set_label(struct mpip_softc *sc, struct ifreq *ifr)
{
	struct shim_hdr label;
	uint32_t shim;
	int error;

	error = copyin(ifr->ifr_data, &label, sizeof(label));
	if (error != 0)
		return (error);

	if (label.shim_label > MPLS_LABEL_MAX ||
	    label.shim_label <= MPLS_LABEL_RESERVED_MAX)
		return (EINVAL);

	shim = MPLS_LABEL2SHIM(label.shim_label);

	if (sc->sc_smpls.smpls_label == shim)
		return (0);

	return (mpip_set_route(sc, shim, sc->sc_rdomain));
}

static int
mpip_get_label(struct mpip_softc *sc, struct ifreq *ifr)
{
	struct shim_hdr label;

	label.shim_label = MPLS_SHIM2LABEL(sc->sc_smpls.smpls_label);

	if (label.shim_label == 0)
		return (EADDRNOTAVAIL);

	return (copyout(&label, ifr->ifr_data, sizeof(label)));
}

static int
mpip_del_label(struct mpip_softc *sc)
{
	if (sc->sc_smpls.smpls_label != MPLS_LABEL2SHIM(0)) {
		rt_ifa_del(&sc->sc_ifa, RTF_MPLS | RTF_LOCAL,
		    smplstosa(&sc->sc_smpls), sc->sc_rdomain);
	}

	sc->sc_smpls.smpls_label = MPLS_LABEL2SHIM(0);

	return (0);
}

static int
mpip_set_neighbor(struct mpip_softc *sc, struct if_laddrreq *req)
{
	struct mpip_neighbor *n, *o;
	struct sockaddr *sa = (struct sockaddr *)&req->addr;
	struct sockaddr_mpls *smpls = (struct sockaddr_mpls *)&req->dstaddr;
	uint32_t label;

	if (smpls->smpls_family != AF_MPLS)
		return (EINVAL);
	label = smpls->smpls_label;
	if (label > MPLS_LABEL_MAX || label <= MPLS_LABEL_RESERVED_MAX)
		return (EINVAL);

	switch (sa->sa_family) {
	case AF_INET: {
		struct sockaddr_in *sin = (struct sockaddr_in *)sa;

		if (in_nullhost(sin->sin_addr) ||
		    IN_MULTICAST(sin->sin_addr.s_addr))
			return (EINVAL);

		break;
	}
#ifdef INET6
	case AF_INET6: {
		struct sockaddr_in6 *sin6 = (struct sockaddr_in6 *)sa;

		if (IN6_IS_ADDR_UNSPECIFIED(&sin6->sin6_addr) ||
		    IN6_IS_ADDR_MULTICAST(&sin6->sin6_addr))
			return (EINVAL);

		/* check scope */

		break;
	}
#endif
	default:
		return (EAFNOSUPPORT);
	}

	if (sc->sc_dead)
		return (ENXIO);

	n = malloc(sizeof(*n), M_DEVBUF, M_WAITOK|M_CANFAIL|M_ZERO);
	if (n == NULL)
		return (ENOMEM);

	n->n_rshim.shim_label = MPLS_LABEL2SHIM(label);
	n->n_nexthop = req->addr;

	o = sc->sc_neighbor;
	sc->sc_neighbor = n;

	NET_UNLOCK();
	ifq_barrier(&sc->sc_if.if_snd);
	NET_LOCK();

	free(o, M_DEVBUF, sizeof(*o));

	return (0);
}

static int
mpip_get_neighbor(struct mpip_softc *sc, struct if_laddrreq *req)
{
	struct sockaddr_mpls *smpls = (struct sockaddr_mpls *)&req->dstaddr;
	struct mpip_neighbor *n = sc->sc_neighbor;

	if (n == NULL)
		return (EADDRNOTAVAIL);

	smpls->smpls_len = sizeof(*smpls);
	smpls->smpls_family = AF_MPLS;
	smpls->smpls_label = MPLS_SHIM2LABEL(n->n_rshim.shim_label);
	req->addr = n->n_nexthop;

	return (0);
}

static int
mpip_del_neighbor(struct mpip_softc *sc, struct ifreq *req)
{
	struct mpip_neighbor *o;

	if (sc->sc_dead)
		return (ENXIO);

	o = sc->sc_neighbor;
	sc->sc_neighbor = NULL;

	NET_UNLOCK();
	ifq_barrier(&sc->sc_if.if_snd);
	NET_LOCK();

	free(o, M_DEVBUF, sizeof(*o));

	return (0);
}

int
mpip_ioctl(struct ifnet *ifp, u_long cmd, caddr_t data)
{
	struct mpip_softc *sc = ifp->if_softc;
	struct ifreq *ifr = (struct ifreq *)data;
	int error = 0;

	switch (cmd) {
	case SIOCSIFADDR:
		break;
	case SIOCSIFFLAGS:
		if ((ifp->if_flags & IFF_UP))
			ifp->if_flags |= IFF_RUNNING;
		else
			ifp->if_flags &= ~IFF_RUNNING;
		break;
	case SIOCSIFMTU:
		if (ifr->ifr_mtu < 60 || /* XXX */
		    ifr->ifr_mtu > 65536) /* XXX */
			error = EINVAL;
		else
			ifp->if_mtu = ifr->ifr_mtu;
		break;

	case SIOCGPWE3:
		ifr->ifr_pwe3 = IF_PWE3_IP;
		break;
	case SIOCSPWE3CTRLWORD:
		sc->sc_cword = ifr->ifr_pwe3 ? 1 : 0;
		break;
	case SIOCGPWE3CTRLWORD:
		ifr->ifr_pwe3 = sc->sc_cword;
		break;
	case SIOCSPWE3FAT:
		sc->sc_fword = ifr->ifr_pwe3 ? 1 : 0;
		break;
	case SIOCGPWE3FAT:
		ifr->ifr_pwe3 = sc->sc_fword;
		break;

	case SIOCSETLABEL:
		error = mpip_set_label(sc, ifr);
		break;
	case SIOCGETLABEL:
		error = mpip_get_label(sc, ifr);
		break;
	case SIOCDELLABEL:
		error = mpip_del_label(sc);
		break;

	case SIOCSPWE3NEIGHBOR:
		error = mpip_set_neighbor(sc, (struct if_laddrreq *)data);
		break;
	case SIOCGPWE3NEIGHBOR:
		error = mpip_get_neighbor(sc, (struct if_laddrreq *)data);
		break;
	case SIOCDPWE3NEIGHBOR:
		error = mpip_del_neighbor(sc, ifr);
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
			error = mpip_set_route(sc, sc->sc_smpls.smpls_label,
			    ifr->ifr_rdomainid);
		}
		break;
	case SIOCGLIFPHYRTABLE:
		ifr->ifr_rdomainid = sc->sc_rdomain;
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

	case SIOCADDMULTI:
	case SIOCDELMULTI:
		break;

	default:
		error = ENOTTY;
		break;
	}

	return (error);
}

static void
mpip_input(struct mpip_softc *sc, struct mbuf *m)
{
	struct ifnet *ifp = &sc->sc_if;
	int rxprio = sc->sc_rxhprio;
	uint32_t shim, exp;
	struct mbuf *n;
	uint8_t ttl, tos;

	if (!ISSET(ifp->if_flags, IFF_RUNNING))
		goto drop;

	shim = *mtod(m, uint32_t *);
	m_adj(m, sizeof(shim));

	ttl = ntohl(shim & MPLS_TTL_MASK);
	exp = ntohl(shim & MPLS_EXP_MASK) >> MPLS_EXP_OFFSET;

	if (sc->sc_fword) {
		uint32_t label;

		if (MPLS_BOS_ISSET(shim))
			goto drop;

		if (m->m_len < sizeof(shim)) {
			m = m_pullup(m, sizeof(shim));
			if (m == NULL)
				return;
		}

		shim = *mtod(m, uint32_t *);
		if (!MPLS_BOS_ISSET(shim))
			goto drop;

		label = MPLS_SHIM2LABEL(shim);
		if (label <= MPLS_LABEL_RESERVED_MAX) {
			counters_inc(ifp->if_counters, ifc_noproto); /* ? */
			goto drop;
		}

		label -= MPLS_LABEL_RESERVED_MAX + 1;
		label ^= sc->sc_flow;
		SET(m->m_pkthdr.csum_flags, M_FLOWID);
		m->m_pkthdr.ph_flowid = label;

		m_adj(m, sizeof(shim));
	} else if (!MPLS_BOS_ISSET(shim))
		goto drop;

	if (sc->sc_cword) {
		if (m->m_len < sizeof(shim)) {
			m = m_pullup(m, sizeof(shim));
			if (m == NULL)
				return;
		}
		shim = *mtod(m, uint32_t *);

		/*
		 * The first 4 bits identifies that this packet is a
		 * control word. If the control word is configured and
		 * we received an IP datagram we shall drop it.
		 */
		if (shim & CW_ZERO_MASK) {
			counters_inc(ifp->if_counters, ifc_ierrors);
			goto drop;
		}

		/* We don't support fragmentation just yet. */
		if (shim & CW_FRAG_MASK) {
			counters_inc(ifp->if_counters, ifc_ierrors);
			goto drop;
		}

		m_adj(m, sizeof(shim));
	}

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

		if (sc->sc_ttl == -1) {
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

		if (sc->sc_ttl == -1) {
			m = mpls_ip6_adjttl(m, ttl);
			if (m == NULL)
				return;
		}

		m->m_pkthdr.ph_family = AF_INET6;
		break;
	}
#endif /* INET6 */
	default:
		counters_inc(ifp->if_counters, ifc_noproto);
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

int
mpip_output(struct ifnet *ifp, struct mbuf *m, struct sockaddr *dst,
    struct rtentry *rt)
{
	struct mpip_softc *sc = ifp->if_softc;
	int error;

	if (dst->sa_family == AF_LINK &&
	    rt != NULL && ISSET(rt->rt_flags, RTF_LOCAL)) {
		mpip_input(sc, m);
		return (0);
	}

	if (!ISSET(ifp->if_flags, IFF_RUNNING)) {
		error = ENETDOWN;
		goto drop;
	}

	switch (dst->sa_family) {
	case AF_INET:
#ifdef INET6
	case AF_INET6:
#endif
		break;
	default:
		error = EAFNOSUPPORT;
		goto drop;
	}

	m->m_pkthdr.ph_family = dst->sa_family;

	error = if_enqueue(ifp, m);
	if (error)
		counters_inc(ifp->if_counters, ifc_oerrors);
	return (error);

drop:
	m_freem(m);
	return (error);
}

void
mpip_start(struct ifnet *ifp)
{
	struct mpip_softc *sc = ifp->if_softc;
	struct mpip_neighbor *n = sc->sc_neighbor;
	struct rtentry *rt;
	struct ifnet *ifp0;
	struct mbuf *m;
	uint32_t shim;
	struct sockaddr_mpls smpls = {
		.smpls_len = sizeof(smpls),
		.smpls_family = AF_MPLS,
	};
	int txprio = sc->sc_txhprio;
	uint32_t exp, bos;
	uint8_t tos, prio, ttl;

	if (!ISSET(ifp->if_flags, IFF_RUNNING) || n == NULL) {
		ifq_purge(&ifp->if_snd);
		return;
	}

	rt = rtalloc(sstosa(&n->n_nexthop), RT_RESOLVE, sc->sc_rdomain);
	if (!rtisvalid(rt)) {
		ifq_purge(&ifp->if_snd);
		goto rtfree;
	}

	ifp0 = if_get(rt->rt_ifidx);
	if (ifp0 == NULL) {
		ifq_purge(&ifp->if_snd);
		goto rtfree;
	}

	while ((m = ifq_dequeue(&ifp->if_snd)) != NULL) {
#if NBPFILTER > 0
		caddr_t if_bpf = sc->sc_if.if_bpf;
		if (if_bpf) {
			bpf_mtap_af(if_bpf, m->m_pkthdr.ph_family,
			    m, BPF_DIRECTION_OUT);
		}
#endif /* NBPFILTER */

		if (sc->sc_ttl == -1) {
			switch (m->m_pkthdr.ph_family) {
			case AF_INET: {
				struct ip *ip;
				ip = mtod(m, struct ip *);
				ttl = ip->ip_ttl;
				break;
			}
#ifdef INET6
			case AF_INET6: {
				struct ip6_hdr *ip6;
				ip6 = mtod(m, struct ip6_hdr *);
				ttl = ip6->ip6_hlim;
				break;
			}
#endif
			default:
				unhandled_af(m->m_pkthdr.ph_family);
			}
		} else
			ttl = mpls_defttl;

		switch (txprio) {
		case IF_HDRPRIO_PACKET:
			prio = m->m_pkthdr.pf.prio;
			break;
		case IF_HDRPRIO_PAYLOAD:
			switch (m->m_pkthdr.ph_family) {
			case AF_INET: {
				struct ip *ip;
				ip = mtod(m, struct ip *);
				tos = ip->ip_tos;
				break;
			}
#ifdef INET6
			case AF_INET6: {
				struct ip6_hdr *ip6;
				uint32_t flow;
				ip6 = mtod(m, struct ip6_hdr *);
				flow = bemtoh32(&ip6->ip6_flow);
				tos = flow >> 20;
				break;
			}
#endif
			default:
				unhandled_af(m->m_pkthdr.ph_family);
			}

			prio = IFQ_TOS2PRIO(tos);
			break;
		default:
			prio = txprio;
			break;
		}
		exp = htonl(prio << MPLS_EXP_OFFSET);

		if (sc->sc_cword) {
			m = m_prepend(m, sizeof(shim), M_NOWAIT);
			if (m == NULL)
				continue;

			*mtod(m, uint32_t *) = 0;
		}

		bos = MPLS_BOS_MASK;

		if (sc->sc_fword) {
			uint32_t flow = 0;
			m = m_prepend(m, sizeof(shim), M_NOWAIT);
			if (m == NULL)
				continue;

			if (ISSET(m->m_pkthdr.csum_flags, M_FLOWID))
				flow = m->m_pkthdr.ph_flowid;
			flow ^= sc->sc_flow;
			flow += MPLS_LABEL_RESERVED_MAX + 1;

			shim = htonl(1) & MPLS_TTL_MASK;
			shim |= htonl(flow << MPLS_LABEL_OFFSET) &
			    MPLS_LABEL_MASK;
			shim |= exp | bos;
			*mtod(m, uint32_t *) = shim;

			bos = 0;
		}

		m = m_prepend(m, sizeof(shim), M_NOWAIT);
		if (m == NULL)
			continue;

		shim = htonl(ttl) & MPLS_TTL_MASK;
		shim |= n->n_rshim.shim_label;
		shim |= exp | bos;
		*mtod(m, uint32_t *) = shim;

		m->m_pkthdr.ph_rtableid = sc->sc_rdomain;
		CLR(m->m_flags, M_BCAST|M_MCAST);

		mpls_output(ifp0, m, (struct sockaddr *)&smpls, rt);
	}

	if_put(ifp0);
rtfree:
	rtfree(rt);
}
