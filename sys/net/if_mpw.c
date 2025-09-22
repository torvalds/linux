/*	$OpenBSD: if_mpw.c,v 1.68 2025/07/07 02:28:50 jsg Exp $ */

/*
 * Copyright (c) 2015 Rafael Zalamena <rzalamena@openbsd.org>
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
#include <net/if_dl.h>
#include <net/route.h>

#include <netinet/in.h>

#include <netinet/if_ether.h>
#include <netmpls/mpls.h>

#if NBPFILTER > 0
#include <net/bpf.h>
#endif /* NBPFILTER */

struct mpw_neighbor {
	struct shim_hdr		n_rshim;
	struct sockaddr_storage	n_nexthop;
};

struct mpw_softc {
	struct arpcom		sc_ac;
#define sc_if			sc_ac.ac_if

	int			sc_txhprio;
	int			sc_rxhprio;
	unsigned int		sc_rdomain;
	struct ifaddr		sc_ifa;
	struct sockaddr_mpls	sc_smpls; /* Local label */

	unsigned int		sc_cword;
	unsigned int		sc_fword;
	uint32_t		sc_flow;
	uint32_t		sc_type;
	struct mpw_neighbor	*sc_neighbor;

	unsigned int		sc_dead;
};

void	mpwattach(int);
int	mpw_clone_create(struct if_clone *, int);
int	mpw_clone_destroy(struct ifnet *);
int	mpw_ioctl(struct ifnet *, u_long, caddr_t);
int	mpw_output(struct ifnet *, struct mbuf *, struct sockaddr *,
    struct rtentry *);
void	mpw_start(struct ifnet *);

struct if_clone mpw_cloner =
    IF_CLONE_INITIALIZER("mpw", mpw_clone_create, mpw_clone_destroy);

void
mpwattach(int n)
{
	if_clone_attach(&mpw_cloner);
}

int
mpw_clone_create(struct if_clone *ifc, int unit)
{
	struct mpw_softc *sc;
	struct ifnet *ifp;

	sc = malloc(sizeof(*sc), M_DEVBUF, M_WAITOK|M_CANFAIL|M_ZERO);
	if (sc == NULL)
		return (ENOMEM);

	sc->sc_flow = arc4random();
	sc->sc_neighbor = NULL;

	ifp = &sc->sc_if;
	snprintf(ifp->if_xname, sizeof(ifp->if_xname), "%s%d",
	    ifc->ifc_name, unit);
	ifp->if_softc = sc;
	ifp->if_flags = IFF_BROADCAST | IFF_SIMPLEX | IFF_MULTICAST;
	ifp->if_xflags = IFXF_CLONED;
	ifp->if_ioctl = mpw_ioctl;
	ifp->if_output = mpw_output;
	ifp->if_start = mpw_start;
	ifp->if_hardmtu = ETHER_MAX_HARDMTU_LEN;
	ether_fakeaddr(ifp);

	sc->sc_dead = 0;

	if_counters_alloc(ifp);
	if_attach(ifp);
	ether_ifattach(ifp);

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
mpw_clone_destroy(struct ifnet *ifp)
{
	struct mpw_softc *sc = ifp->if_softc;

	NET_LOCK();
	ifp->if_flags &= ~IFF_RUNNING;
	sc->sc_dead = 1;

	if (sc->sc_smpls.smpls_label) {
		rt_ifa_del(&sc->sc_ifa, RTF_MPLS|RTF_LOCAL,
		    smplstosa(&sc->sc_smpls), sc->sc_rdomain);
	}
	NET_UNLOCK();

	ifq_barrier(&ifp->if_snd);

	ether_ifdetach(ifp);
	if_detach(ifp);
	if (refcnt_rele(&sc->sc_ifa.ifa_refcnt) == 0) {
		panic("%s: ifa refcnt has %u refs", __func__,
		    sc->sc_ifa.ifa_refcnt.r_refs);
	}
	free(sc->sc_neighbor, M_DEVBUF, sizeof(*sc->sc_neighbor));
	free(sc, M_DEVBUF, sizeof(*sc));

	return (0);
}

int
mpw_set_route(struct mpw_softc *sc, uint32_t label, unsigned int rdomain)
{
	int error;

	if (sc->sc_dead)
		return (ENXIO);

	if (sc->sc_smpls.smpls_label) {
		rt_ifa_del(&sc->sc_ifa, RTF_MPLS|RTF_LOCAL,
		    smplstosa(&sc->sc_smpls), sc->sc_rdomain);
	}

	sc->sc_smpls.smpls_label = label;
	sc->sc_rdomain = rdomain;

	/* only install with a label or mpw_clone_destroy() will ignore it */
	if (sc->sc_smpls.smpls_label == MPLS_LABEL2SHIM(0))
		return 0;

	error = rt_ifa_add(&sc->sc_ifa, RTF_MPLS|RTF_LOCAL,
	    smplstosa(&sc->sc_smpls), sc->sc_rdomain);
	if (error != 0)
		sc->sc_smpls.smpls_label = 0;

	return (error);
}

static int
mpw_set_neighbor(struct mpw_softc *sc, const struct if_laddrreq *req)
{
	struct mpw_neighbor *n, *o;
	const struct sockaddr_storage *ss;
	const struct sockaddr_mpls *smpls;
	uint32_t label;

	smpls = (const struct sockaddr_mpls *)&req->dstaddr;

	if (smpls->smpls_family != AF_MPLS)
		return (EINVAL);
	label = smpls->smpls_label;
	if (label > MPLS_LABEL_MAX || label <= MPLS_LABEL_RESERVED_MAX)
		return (EINVAL);

	ss = &req->addr;
	switch (ss->ss_family) {
	case AF_INET: {
		const struct sockaddr_in *sin =
		    (const struct sockaddr_in *)ss;

		if (in_nullhost(sin->sin_addr) ||
		    IN_MULTICAST(sin->sin_addr.s_addr))
			return (EINVAL);

		break;
	}
#ifdef INET6
	case AF_INET6: {
		const struct sockaddr_in6 *sin6 =
		    (const struct sockaddr_in6 *)ss;

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
	n->n_nexthop = *ss;

	o = sc->sc_neighbor;
	sc->sc_neighbor = n;

	NET_UNLOCK();
	ifq_barrier(&sc->sc_if.if_snd);
	NET_LOCK();

	free(o, M_DEVBUF, sizeof(*o));

	return (0);
}

static int
mpw_get_neighbor(struct mpw_softc *sc, struct if_laddrreq *req)
{
	struct sockaddr_mpls *smpls = (struct sockaddr_mpls *)&req->dstaddr;
	struct mpw_neighbor *n = sc->sc_neighbor;

	if (n == NULL)
		return (EADDRNOTAVAIL);

	smpls->smpls_len = sizeof(*smpls);
	smpls->smpls_family = AF_MPLS;
	smpls->smpls_label = MPLS_SHIM2LABEL(n->n_rshim.shim_label);

	req->addr = n->n_nexthop;

	return (0);
}

static int
mpw_del_neighbor(struct mpw_softc *sc)
{
	struct mpw_neighbor *o;

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

static int
mpw_set_label(struct mpw_softc *sc, const struct shim_hdr *label)
{
	uint32_t shim;

	if (label->shim_label > MPLS_LABEL_MAX ||
	    label->shim_label <= MPLS_LABEL_RESERVED_MAX)
		return (EINVAL);

	shim = MPLS_LABEL2SHIM(label->shim_label);
	if (sc->sc_smpls.smpls_label == shim)
		return (0);

	return (mpw_set_route(sc, shim, sc->sc_rdomain));
}

static int
mpw_get_label(struct mpw_softc *sc, struct ifreq *ifr)
{
	struct shim_hdr label;

	label.shim_label = MPLS_SHIM2LABEL(sc->sc_smpls.smpls_label);

	if (label.shim_label == 0)
		return (EADDRNOTAVAIL);

	return (copyout(&label, ifr->ifr_data, sizeof(label)));
}

static int
mpw_del_label(struct mpw_softc *sc)
{
	if (sc->sc_dead)
		return (ENXIO);

	if (sc->sc_smpls.smpls_label != MPLS_LABEL2SHIM(0)) {
		rt_ifa_del(&sc->sc_ifa, RTF_MPLS | RTF_LOCAL,
		    smplstosa(&sc->sc_smpls), sc->sc_rdomain);
	}

	sc->sc_smpls.smpls_label = MPLS_LABEL2SHIM(0);

	return (0);
}

static int
mpw_set_config(struct mpw_softc *sc, const struct ifreq *ifr)
{
	struct ifmpwreq imr;
	struct if_laddrreq req;
	struct sockaddr_mpls *smpls;
	struct sockaddr_in *sin;
	int error;

	error = copyin(ifr->ifr_data, &imr, sizeof(imr));
	if (error != 0)
		return (error);

	/* Teardown all configuration if got no nexthop */
	sin = (struct sockaddr_in *)&imr.imr_nexthop;
	if (sin->sin_addr.s_addr == 0) {
		mpw_del_label(sc);
		mpw_del_neighbor(sc);
		sc->sc_cword = 0;
		sc->sc_type = 0;
		return (0);
	}

	error = mpw_set_label(sc, &imr.imr_lshim);
	if (error != 0)
		return (error);

	smpls = (struct sockaddr_mpls *)&req.dstaddr;
	smpls->smpls_family = AF_MPLS;
	smpls->smpls_label = imr.imr_rshim.shim_label;
	req.addr = imr.imr_nexthop;

	error = mpw_set_neighbor(sc, &req);
	if (error != 0)
		return (error);

	sc->sc_cword = ISSET(imr.imr_flags, IMR_FLAG_CONTROLWORD);
	sc->sc_type = imr.imr_type;

	return (0);
}

static int
mpw_get_config(struct mpw_softc *sc, const struct ifreq *ifr)
{
	struct ifmpwreq imr;

	memset(&imr, 0, sizeof(imr));
	imr.imr_flags = sc->sc_cword ? IMR_FLAG_CONTROLWORD : 0;
	imr.imr_type = sc->sc_type;

	imr.imr_lshim.shim_label = MPLS_SHIM2LABEL(sc->sc_smpls.smpls_label);
	if (sc->sc_neighbor) {
		imr.imr_rshim.shim_label =
		    MPLS_SHIM2LABEL(sc->sc_neighbor->n_rshim.shim_label);
		imr.imr_nexthop = sc->sc_neighbor->n_nexthop;
	}

	return (copyout(&imr, ifr->ifr_data, sizeof(imr)));
}

int
mpw_ioctl(struct ifnet *ifp, u_long cmd, caddr_t data)
{
	struct ifreq *ifr = (struct ifreq *) data;
	struct mpw_softc *sc = ifp->if_softc;
	struct shim_hdr shim;
	int error = 0;

	switch (cmd) {
	case SIOCSIFFLAGS:
		if ((ifp->if_flags & IFF_UP))
			ifp->if_flags |= IFF_RUNNING;
		else
			ifp->if_flags &= ~IFF_RUNNING;
		break;

	case SIOCGPWE3:
		ifr->ifr_pwe3 = IF_PWE3_ETHERNET;
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

	case SIOCSPWE3NEIGHBOR:
		error = mpw_set_neighbor(sc, (struct if_laddrreq *)data);
		break;
	case SIOCGPWE3NEIGHBOR:
		error = mpw_get_neighbor(sc, (struct if_laddrreq *)data);
		break;
	case SIOCDPWE3NEIGHBOR:
		error = mpw_del_neighbor(sc);
		break;

	case SIOCGETLABEL:
		error = mpw_get_label(sc, ifr);
		break;
	case SIOCSETLABEL:
		error = copyin(ifr->ifr_data, &shim, sizeof(shim));
		if (error != 0)
			break;
		error = mpw_set_label(sc, &shim);
		break;
	case SIOCDELLABEL:
		error = mpw_del_label(sc);
		break;

	case SIOCSETMPWCFG:
		error = mpw_set_config(sc, ifr);
		break;

	case SIOCGETMPWCFG:
		error = mpw_get_config(sc, ifr);
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
			error = mpw_set_route(sc, sc->sc_smpls.smpls_label,
			    ifr->ifr_rdomainid);
		}
		break;
	case SIOCGLIFPHYRTABLE:
		ifr->ifr_rdomainid = sc->sc_rdomain;
		break;

	case SIOCSTXHPRIO:
		error = if_txhprio_l2_check(ifr->ifr_hdrprio);
		if (error != 0)
			break;

		sc->sc_txhprio = ifr->ifr_hdrprio;
		break;
	case SIOCGTXHPRIO:
		ifr->ifr_hdrprio = sc->sc_txhprio;
		break;

	case SIOCSRXHPRIO:
		error = if_rxhprio_l2_check(ifr->ifr_hdrprio);
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
		error = ether_ioctl(ifp, &sc->sc_ac, cmd, data);
		break;
	}

	return (error);
}

static void
mpw_input(struct mpw_softc *sc, struct mbuf *m)
{
	struct ifnet *ifp = &sc->sc_if;
	struct shim_hdr *shim;
	struct mbuf *n;
	uint32_t exp;
	int rxprio;
	int off;

	if (!ISSET(ifp->if_flags, IFF_RUNNING))
		goto drop;

	shim = mtod(m, struct shim_hdr *);
	exp = ntohl(shim->shim_label & MPLS_EXP_MASK) >> MPLS_EXP_OFFSET;
	if (sc->sc_fword) {
		uint32_t flow;

		if (MPLS_BOS_ISSET(shim->shim_label))
			goto drop;
		m_adj(m, sizeof(*shim));

		if (m->m_len < sizeof(*shim)) {
			m = m_pullup(m, sizeof(*shim));
			if (m == NULL)
				return;
		}
		shim = mtod(m, struct shim_hdr *);

		if (!MPLS_BOS_ISSET(shim->shim_label))
			goto drop;

		flow = MPLS_SHIM2LABEL(shim->shim_label);
		flow ^= sc->sc_flow;
		SET(m->m_pkthdr.csum_flags, M_FLOWID);
		m->m_pkthdr.ph_flowid = flow;
	} else {
		if (!MPLS_BOS_ISSET(shim->shim_label))
			goto drop;
	}
	m_adj(m, sizeof(*shim));

	if (sc->sc_cword) {
		if (m->m_len < sizeof(*shim)) {
			m = m_pullup(m, sizeof(*shim));
			if (m == NULL)
				return;
		}
		shim = mtod(m, struct shim_hdr *);

		/*
		 * The first 4 bits identifies that this packet is a
		 * control word. If the control word is configured and
		 * we received an IP datagram we shall drop it.
		 */
		if (shim->shim_label & CW_ZERO_MASK) {
			ifp->if_ierrors++;
			goto drop;
		}

		/* We don't support fragmentation just yet. */
		if (shim->shim_label & CW_FRAG_MASK) {
			ifp->if_ierrors++;
			goto drop;
		}

		m_adj(m, MPLS_HDRLEN);
	}

	if (m->m_len < sizeof(struct ether_header)) {
		m = m_pullup(m, sizeof(struct ether_header));
		if (m == NULL)
			return;
	}

	n = m_getptr(m, sizeof(struct ether_header), &off);
	if (n == NULL) {
		ifp->if_ierrors++;
		goto drop;
	}
	if (!ALIGNED_POINTER(mtod(n, caddr_t) + off, uint32_t)) {
		n = m_dup_pkt(m, ETHER_ALIGN, M_NOWAIT);
		/* Dispose of the original mbuf chain */
		m_freem(m);
		if (n == NULL)
			return;
		m = n;
	}

	rxprio = sc->sc_rxhprio;
	switch (rxprio) {
	case IF_HDRPRIO_PACKET:
		/* nop */
		break;
	case IF_HDRPRIO_OUTER:
		m->m_pkthdr.pf.prio = exp;
		break;
	default:
		m->m_pkthdr.pf.prio = rxprio;
		break;
	}

	m->m_pkthdr.ph_ifidx = ifp->if_index;
	m->m_pkthdr.ph_rtableid = ifp->if_rdomain;

	/* packet has not been processed by PF yet. */
	KASSERT(m->m_pkthdr.pf.statekey == NULL);

	if_vinput(ifp, m, NULL);
	return;
drop:
	m_freem(m);
}

int
mpw_output(struct ifnet *ifp, struct mbuf *m, struct sockaddr *dst,
    struct rtentry *rt)
{
	struct mpw_softc *sc = ifp->if_softc;

	if (dst->sa_family == AF_LINK &&
	    rt != NULL && ISSET(rt->rt_flags, RTF_LOCAL)) {
		mpw_input(sc, m);
		return (0);
	}

	return (ether_output(ifp, m, dst, rt));
}

void
mpw_start(struct ifnet *ifp)
{
	struct mpw_softc *sc = ifp->if_softc;
	struct rtentry *rt;
	struct ifnet *ifp0;
	struct mbuf *m, *m0;
	struct shim_hdr *shim;
	struct mpw_neighbor *n;
	struct sockaddr_mpls smpls = {
		.smpls_len = sizeof(smpls),
		.smpls_family = AF_MPLS,
	};
	int txprio = sc->sc_txhprio;
	uint8_t prio;
	uint32_t exp, bos;

	n = sc->sc_neighbor;
	if (!ISSET(ifp->if_flags, IFF_RUNNING) ||
	    n == NULL) {
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
		if (sc->sc_if.if_bpf)
			bpf_mtap(sc->sc_if.if_bpf, m, BPF_DIRECTION_OUT);
#endif /* NBPFILTER */

		m0 = m_get(M_DONTWAIT, m->m_type);
		if (m0 == NULL) {
			m_freem(m);
			continue;
		}

		M_MOVE_PKTHDR(m0, m);
		m0->m_next = m;
		m_align(m0, 0);
		m0->m_len = 0;

		if (sc->sc_cword) {
			m0 = m_prepend(m0, sizeof(*shim), M_NOWAIT);
			if (m0 == NULL)
				continue;

			shim = mtod(m0, struct shim_hdr *);
			memset(shim, 0, sizeof(*shim));
		}

		switch (txprio) {
		case IF_HDRPRIO_PACKET:
			prio = m->m_pkthdr.pf.prio;
			break;
		default:
			prio = txprio;
			break;
		}
		exp = htonl(prio << MPLS_EXP_OFFSET);

		bos = MPLS_BOS_MASK;
		if (sc->sc_fword) {
			uint32_t flow = sc->sc_flow;

			m0 = m_prepend(m0, sizeof(*shim), M_NOWAIT);
			if (m0 == NULL)
				continue;

			if (ISSET(m->m_pkthdr.csum_flags, M_FLOWID))
				flow ^= m->m_pkthdr.ph_flowid;

			shim = mtod(m0, struct shim_hdr *);
			shim->shim_label = htonl(1) & MPLS_TTL_MASK;
			shim->shim_label |= MPLS_LABEL2SHIM(flow) | exp | bos;

			bos = 0;
		}

		m0 = m_prepend(m0, sizeof(*shim), M_NOWAIT);
		if (m0 == NULL)
			continue;

		shim = mtod(m0, struct shim_hdr *);
		shim->shim_label = htonl(mpls_defttl) & MPLS_TTL_MASK;
		shim->shim_label |= n->n_rshim.shim_label | exp | bos;

		m0->m_pkthdr.ph_rtableid = sc->sc_rdomain;
		CLR(m0->m_flags, M_BCAST|M_MCAST);

		mpls_output(ifp0, m0, (struct sockaddr *)&smpls, rt);
	}

	if_put(ifp0);
rtfree:
	rtfree(rt);
}
