/* $OpenBSD: if_vether.c,v 1.39 2025/09/16 23:11:39 jan Exp $ */

/*
 * Copyright (c) 2009 Theo de Raadt
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
#include <sys/sockio.h>

#include <net/if.h>
#include <net/if_media.h>

#include <netinet/in.h>
#include <netinet/if_ether.h>

#include "vlan.h"
#include "bpfilter.h"
#if NBPFILTER > 0
#include <net/bpf.h>
#endif

void	vetherattach(int);
int	vetherioctl(struct ifnet *, u_long, caddr_t);
void	vetherqstart(struct ifqueue *);
int	vether_clone_create(struct if_clone *, int);
int	vether_clone_destroy(struct ifnet *);
int	vether_media_change(struct ifnet *);
void	vether_media_status(struct ifnet *, struct ifmediareq *);

struct vether_softc {
	struct arpcom		sc_ac;
	struct ifmedia		sc_media;
};

struct if_clone	vether_cloner =
    IF_CLONE_INITIALIZER("vether", vether_clone_create, vether_clone_destroy);

int
vether_media_change(struct ifnet *ifp)
{
	return (0);
}

void
vether_media_status(struct ifnet *ifp, struct ifmediareq *imr)
{
	imr->ifm_active = IFM_ETHER | IFM_AUTO;
	imr->ifm_status = IFM_AVALID | IFM_ACTIVE;
}

void
vetherattach(int nvether)
{
	if_clone_attach(&vether_cloner);
}

int
vether_clone_create(struct if_clone *ifc, int unit)
{
	struct ifnet		*ifp;
	struct vether_softc	*sc;

	sc = malloc(sizeof(*sc), M_DEVBUF, M_WAITOK|M_ZERO);
	ifp = &sc->sc_ac.ac_if;
	snprintf(ifp->if_xname, sizeof ifp->if_xname, "vether%d", unit);
	ifp->if_flags = IFF_BROADCAST | IFF_SIMPLEX | IFF_MULTICAST;
	ether_fakeaddr(ifp);

	ifp->if_softc = sc;
	ifp->if_ioctl = vetherioctl;
	ifp->if_qstart = vetherqstart;

	ifp->if_hardmtu = ETHER_MAX_HARDMTU_LEN;
	ifp->if_capabilities = IFCAP_VLAN_MTU;
#if NVLAN > 0
	ifp->if_capabilities |= IFCAP_VLAN_HWTAGGING;
#endif
	ifp->if_capabilities |= IFCAP_CSUM_IPv4;
	ifp->if_capabilities |= IFCAP_CSUM_TCPv4 | IFCAP_CSUM_UDPv4;
	ifp->if_capabilities |= IFCAP_CSUM_TCPv6 | IFCAP_CSUM_UDPv6;
	ifp->if_xflags = IFXF_CLONED | IFXF_MPSAFE;

	ifmedia_init(&sc->sc_media, 0, vether_media_change,
	    vether_media_status);
	ifmedia_add(&sc->sc_media, IFM_ETHER | IFM_AUTO, 0, NULL);
	ifmedia_set(&sc->sc_media, IFM_ETHER | IFM_AUTO);

	if_attach(ifp);
	ether_ifattach(ifp);
	return (0);
}

int
vether_clone_destroy(struct ifnet *ifp)
{
	struct vether_softc	*sc = ifp->if_softc;

	ifmedia_delete_instance(&sc->sc_media, IFM_INST_ANY);
	ether_ifdetach(ifp);
	if_detach(ifp);
	free(sc, M_DEVBUF, sizeof(*sc));
	return (0);
}

/*
 * The bridge has magically already done all the work for us,
 * and we only need to discard the packets.
 */
void
vetherqstart(struct ifqueue *ifq)
{
	struct mbuf		*m;

	while ((m = ifq_dequeue(ifq)) != NULL) {
#if NBPFILTER > 0
		struct ifnet	*ifp = ifq->ifq_if;

		if (ifp->if_bpf)
			bpf_mtap(ifp->if_bpf, m, BPF_DIRECTION_OUT);
#endif /* NBPFILTER > 0 */

		m_freem(m);
	}
}

int
vetherioctl(struct ifnet *ifp, u_long cmd, caddr_t data)
{
	struct vether_softc	*sc = (struct vether_softc *)ifp->if_softc;
	struct ifreq		*ifr = (struct ifreq *)data;
	int			 error = 0, link_state;

	switch (cmd) {
	case SIOCSIFADDR:
		ifp->if_flags |= IFF_UP;
		/* FALLTHROUGH */

	case SIOCSIFFLAGS:
		if (ifp->if_flags & IFF_UP) {
			ifp->if_flags |= IFF_RUNNING;
			link_state = LINK_STATE_UP;
		} else {
			ifp->if_flags &= ~IFF_RUNNING;
			link_state = LINK_STATE_DOWN;
		}
		if (ifp->if_link_state != link_state) {
			ifp->if_link_state = link_state;
			if_link_state_change(ifp);
		}
		break;

	case SIOCADDMULTI:
	case SIOCDELMULTI:
		break;

	case SIOCGIFMEDIA:
	case SIOCSIFMEDIA:
		error = ifmedia_ioctl(ifp, ifr, &sc->sc_media, cmd);
		break;

	default:
		error = ether_ioctl(ifp, &sc->sc_ac, cmd, data);
	}
	return (error);
}
