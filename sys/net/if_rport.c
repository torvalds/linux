/*	$OpenBSD: if_rport.c,v 1.3 2025/07/07 02:28:50 jsg Exp $ */

/*
 * Copyright (c) 2023 David Gwynne <dlg@openbsd.org>
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
#include <net/if_var.h>
#include <net/if_types.h>

#include <netinet/in.h>

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

#define RPORT_MTU_MIN		1280
#define RPORT_MTU_MAX		32768 /* LOMTU, but could be higher */
#define RPORT_MTU_DEFAULT	RPORT_MTU_MAX

struct rport_softc {
	struct ifnet			 sc_if;

	unsigned int			 sc_peer_idx;
};

static int	rport_clone_create(struct if_clone *, int);
static int	rport_clone_destroy(struct ifnet *);

static int	rport_ioctl(struct ifnet *, u_long, caddr_t);
static int	rport_output(struct ifnet *, struct mbuf *, struct sockaddr *,
		    struct rtentry *);
static int	rport_enqueue(struct ifnet *, struct mbuf *);
static void	rport_start(struct ifqueue *);
static void	rport_input(struct ifnet *, struct mbuf *, struct netstack *);

static int	rport_up(struct rport_softc *);
static int	rport_down(struct rport_softc *);

static int	rport_set_parent(struct rport_softc *,
		    const struct if_parent *);
static int	rport_get_parent(struct rport_softc *, struct if_parent *);
static int	rport_del_parent(struct rport_softc *);

static struct if_clone rport_cloner =
    IF_CLONE_INITIALIZER("rport", rport_clone_create, rport_clone_destroy);

static struct rwlock rport_interfaces_lock =
    RWLOCK_INITIALIZER("rports");

void
rportattach(int count)
{
	if_clone_attach(&rport_cloner);
}

static int
rport_clone_create(struct if_clone *ifc, int unit)
{
	struct rport_softc *sc;
	struct ifnet *ifp;

	sc = malloc(sizeof(*sc), M_DEVBUF, M_WAITOK|M_ZERO);
	ifp = &sc->sc_if;

	snprintf(ifp->if_xname, sizeof(ifp->if_xname),
	    "%s%d", ifc->ifc_name, unit);

	ifp->if_mtu = RPORT_MTU_DEFAULT;
	ifp->if_flags = IFF_POINTOPOINT | IFF_MULTICAST;
	ifp->if_xflags = IFXF_CLONED | IFXF_MPSAFE;
	ifp->if_ioctl = rport_ioctl;
	ifp->if_bpf_mtap = p2p_bpf_mtap;
	ifp->if_output = rport_output;
	ifp->if_enqueue = rport_enqueue;
	ifp->if_qstart = rport_start;
	ifp->if_input = rport_input;
	ifp->if_rtrequest = p2p_rtrequest;
	ifp->if_type = IFT_TUNNEL;
	ifp->if_softc = sc;

	if_attach(ifp);
	if_alloc_sadl(ifp);
	if_counters_alloc(ifp);

#if NBPFILTER > 0
	bpfattach(&ifp->if_bpf, ifp, DLT_LOOP, sizeof(uint32_t));
#endif

	return (0);
}

int
rport_clone_destroy(struct ifnet *ifp)
{
	struct rport_softc *sc = ifp->if_softc;

	NET_LOCK();
	if (ISSET(ifp->if_flags, IFF_RUNNING))
		rport_down(sc);
	rport_del_parent(sc);
	NET_UNLOCK();

	if_detach(ifp);

	free(sc, M_DEVBUF, sizeof(*sc));

	return (0);
}

static int
rport_output(struct ifnet *ifp, struct mbuf *m, struct sockaddr *dst,
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
	mtag = NULL;
	while ((mtag = m_tag_find(m, PACKET_TAG_GRE, mtag)) != NULL) {
		if (*(int *)(mtag + 1) == ifp->if_index) {
			error = EIO;
			goto drop;
		}
	}

	mtag = m_tag_get(PACKET_TAG_GRE, sizeof(ifp->if_index), M_NOWAIT);
	if (mtag == NULL) {
		error = ENOBUFS;
		goto drop;
	}
	*(int *)(mtag + 1) = ifp->if_index;
	m_tag_prepend(m, mtag);

	m->m_flags &= ~(M_BCAST|M_MCAST);
	m->m_pkthdr.ph_family = dst->sa_family;
#if NPF > 0
	pf_pkt_addr_changed(m);
#endif

	error = if_enqueue(ifp, m);
	if (error)
		counters_inc(ifp->if_counters, ifc_oerrors);

	return (error);

drop:
	m_freem(m);
	return (error);
}

static int
rport_enqueue(struct ifnet *ifp, struct mbuf *m)
{
	struct ifqueue *ifq = &ifp->if_snd;
	int error;

	error = ifq_enqueue(ifq, m);
	if (error)
		return (error);

	/*
	 * always defer handover of packets to the peer to the ifq
	 * bundle task to provide control over the NET_LOCK scope.
	 */
	task_add(ifq->ifq_softnet, &ifq->ifq_bundle);

	return (0);
}

static void
rport_start(struct ifqueue *ifq)
{
	struct ifnet *ifp = ifq->ifq_if;
	struct rport_softc *sc = ifp->if_softc;
	struct ifnet *ifp0;
	struct mbuf *m;

	ifp0 = if_get(sc->sc_peer_idx);
	if (ifp0 == NULL || !ISSET(ifp0->if_flags, IFF_RUNNING)) {
		ifq_purge(ifq);
		if_put(ifp0);
		return;
	}

	NET_LOCK_SHARED();
	while ((m = ifq_dequeue(ifq)) != NULL) {
#if NBPFILTER > 0
		caddr_t if_bpf = READ_ONCE(ifp->if_bpf);
		if (if_bpf && bpf_mtap_af(if_bpf, m->m_pkthdr.ph_family,
		    m, BPF_DIRECTION_OUT)) {
			m_freem(m);
			continue;
		}
#endif

		if_vinput(ifp0, m, NULL);
	}
	NET_UNLOCK_SHARED();

	if_put(ifp0);
}

static void
rport_input(struct ifnet *ifp, struct mbuf *m, struct netstack *ns)
{
        switch (m->m_pkthdr.ph_family) {
        case AF_INET:
                ipv4_input(ifp, m, ns);
                break;
#ifdef INET6
        case AF_INET6:
                ipv6_input(ifp, m, ns);
                break;
#endif
#ifdef MPLS
        case AF_MPLS:
                mpls_input(ifp, m, ns);
                break;
#endif
        default:
		counters_inc(ifp->if_counters, ifc_noproto);
                m_freem(m);
                break;
        }
}

static int
rport_up(struct rport_softc *sc)
{
	NET_ASSERT_LOCKED();

	SET(sc->sc_if.if_flags, IFF_RUNNING);

	return (0);
}

static int
rport_down(struct rport_softc *sc)
{
	NET_ASSERT_LOCKED();

	CLR(sc->sc_if.if_flags, IFF_RUNNING);

	return (0);
}

static int
rport_set_parent(struct rport_softc *sc, const struct if_parent *p)
{
	struct ifnet *ifp = &sc->sc_if;
	struct ifnet *ifp0;
	struct rport_softc *sc0;
	int error;

	error = rw_enter(&rport_interfaces_lock, RW_WRITE | RW_INTR);
	if (error != 0)
		return (error);

	ifp0 = if_unit(p->ifp_parent);
	if (ifp0 == NULL) {
		error = EINVAL;
		goto leave;
	}

	if (ifp0 == ifp) {
		error = EINVAL;
		goto leave;
	}

	if (ifp0->if_input != rport_input) {
		error = EPROTONOSUPPORT;
		goto put;
	}

	sc0 = ifp0->if_softc;

	if (sc->sc_peer_idx == ifp0->if_index) {
		/* nop */
		KASSERT(sc0->sc_peer_idx == ifp->if_index);
		goto put;
	}

	if (sc->sc_peer_idx != 0 || sc0->sc_peer_idx != 0) {
		error = EBUSY;
		goto put;
	}

	/* commit */
	sc->sc_peer_idx = ifp0->if_index;
	sc0->sc_peer_idx = ifp->if_index;

put:
	if_put(ifp0);
leave:
	rw_exit(&rport_interfaces_lock);

	return (error);
}

static int
rport_get_parent(struct rport_softc *sc, struct if_parent *p)
{
	struct ifnet *ifp0;
	int error = 0;

	ifp0 = if_get(sc->sc_peer_idx);
	if (ifp0 == NULL)
		error = EADDRNOTAVAIL;
	else {
		if (strlcpy(p->ifp_parent, ifp0->if_xname,
		    sizeof(p->ifp_parent)) >= sizeof(p->ifp_parent))
			panic("%s strlcpy", __func__);
	}
	if_put(ifp0);

	return (error);
}

static int
rport_del_parent(struct rport_softc *sc)
{
	struct rport_softc *sc0;
	struct ifnet *ifp0;
	int error;

	error = rw_enter(&rport_interfaces_lock, RW_WRITE | RW_INTR);
	if (error != 0)
		return (error);

	ifp0 = if_get(sc->sc_peer_idx);
	sc->sc_peer_idx = 0;

	if (ifp0 != NULL) {
		sc0 = ifp0->if_softc;
		sc0->sc_peer_idx = 0;
	}
	if_put(ifp0);

	rw_exit(&rport_interfaces_lock);

	return (0);
}

static int
rport_ioctl(struct ifnet *ifp, u_long cmd, caddr_t data)
{
	struct rport_softc *sc = ifp->if_softc;
	struct ifreq *ifr = (struct ifreq *)data;
	int error = 0;

	switch (cmd) {
	case SIOCSIFADDR:
		break;
	case SIOCSIFFLAGS:
		if (ISSET(ifp->if_flags, IFF_UP)) {
			if (!ISSET(ifp->if_flags, IFF_RUNNING))
				error = rport_up(sc);
		} else {
			if (ISSET(ifp->if_flags, IFF_RUNNING))
				error = rport_down(sc);
		}
		break;

	case SIOCADDMULTI:
	case SIOCDELMULTI:
		break;

	case SIOCSIFMTU:
		if (ifr->ifr_mtu < RPORT_MTU_MIN ||
		    ifr->ifr_mtu > RPORT_MTU_MAX) {
			error = EINVAL;
			break;
		}

		ifp->if_mtu = ifr->ifr_mtu;
		break;

	case SIOCSIFPARENT:
		error = rport_set_parent(sc, (struct if_parent *)data);
		break;
	case SIOCGIFPARENT:
		error = rport_get_parent(sc, (struct if_parent *)data);
		break;
	case SIOCDIFPARENT:
		error = rport_del_parent(sc);
		break;

	default:
		error = ENOTTY;
		break;
	}

	return (error);
}
