/*	$OpenBSD: if_pair.c,v 1.18 2025/07/07 02:28:50 jsg Exp $	*/

/*
 * Copyright (c) 2015 Reyk Floeter <reyk@openbsd.org>
 * Copyright (c) 2009 Theo de Raadt <deraadt@openbsd.org>
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

#include "bpfilter.h"
#if NBPFILTER > 0
#include <net/bpf.h>
#endif

void	pairattach(int);
int	pairioctl(struct ifnet *, u_long, caddr_t);
void	pairstart(struct ifqueue *);
int	pair_clone_create(struct if_clone *, int);
int	pair_clone_destroy(struct ifnet *);
int	pair_media_change(struct ifnet *);
void	pair_media_status(struct ifnet *, struct ifmediareq *);
void	pair_link_state(struct ifnet *);

struct pair_softc {
	struct arpcom		sc_ac;
	struct ifmedia		sc_media;
	unsigned int		sc_pairedif;
};

struct if_clone	pair_cloner =
    IF_CLONE_INITIALIZER("pair", pair_clone_create, pair_clone_destroy);

int
pair_media_change(struct ifnet *ifp)
{
	return (0);
}

void
pair_media_status(struct ifnet *ifp, struct ifmediareq *imr)
{
	struct pair_softc	*sc = ifp->if_softc;
	struct ifnet		*pairedifp;

	imr->ifm_active = IFM_ETHER | IFM_AUTO;

	if ((pairedifp = if_get(sc->sc_pairedif)) == NULL) {
		imr->ifm_status = 0;
		return;
	}
	if_put(pairedifp);

	imr->ifm_status = IFM_AVALID | IFM_ACTIVE;
}

void
pair_link_state(struct ifnet *ifp)
{
	struct pair_softc	*sc = ifp->if_softc;
	struct ifnet		*pairedifp;
	unsigned int		 link_state;

	/* The pair state is determined by the paired interface */
	if ((pairedifp = if_get(sc->sc_pairedif)) != NULL) {
		link_state = LINK_STATE_UP;
		if_put(pairedifp);
	} else
		link_state = LINK_STATE_DOWN;

	if (ifp->if_link_state != link_state) {
		ifp->if_link_state = link_state;
		if_link_state_change(ifp);
	}
}

void
pairattach(int npair)
{
	if_clone_attach(&pair_cloner);
}

int
pair_clone_create(struct if_clone *ifc, int unit)
{
	struct ifnet		*ifp;
	struct pair_softc	*sc;

	sc = malloc(sizeof(*sc), M_DEVBUF, M_WAITOK|M_ZERO);
	ifp = &sc->sc_ac.ac_if;
	snprintf(ifp->if_xname, sizeof ifp->if_xname, "pair%d", unit);
	ifp->if_flags = IFF_BROADCAST | IFF_SIMPLEX | IFF_MULTICAST;
	ether_fakeaddr(ifp);

	ifp->if_softc = sc;
	ifp->if_ioctl = pairioctl;
	ifp->if_qstart = pairstart;
	ifp->if_xflags = IFXF_CLONED | IFXF_MPSAFE;

	ifp->if_hardmtu = ETHER_MAX_HARDMTU_LEN;
	ifp->if_capabilities = IFCAP_VLAN_MTU;

	ifmedia_init(&sc->sc_media, 0, pair_media_change,
	    pair_media_status);
	ifmedia_add(&sc->sc_media, IFM_ETHER | IFM_AUTO, 0, NULL);
	ifmedia_set(&sc->sc_media, IFM_ETHER | IFM_AUTO);

	if_attach(ifp);
	ether_ifattach(ifp);

	pair_link_state(ifp);

	return (0);
}

int
pair_clone_destroy(struct ifnet *ifp)
{
	struct pair_softc	*sc = ifp->if_softc;
	struct ifnet		*pairedifp;
	struct pair_softc	*dstsc = ifp->if_softc;

	if ((pairedifp = if_get(sc->sc_pairedif)) != NULL) {
		dstsc = pairedifp->if_softc;
		dstsc->sc_pairedif = 0;
		pair_link_state(pairedifp);
		if_put(pairedifp);
	}

	ifmedia_delete_instance(&sc->sc_media, IFM_INST_ANY);
	ether_ifdetach(ifp);
	if_detach(ifp);
	free(sc, M_DEVBUF, sizeof(*sc));

	return (0);
}

void
pairstart(struct ifqueue *ifq)
{
	struct ifnet		*ifp = ifq->ifq_if;
	struct pair_softc	*sc = (struct pair_softc *)ifp->if_softc;
	struct mbuf_list	 ml = MBUF_LIST_INITIALIZER();
	struct ifnet		*pairedifp;
	struct mbuf		*m;

	pairedifp = if_get(sc->sc_pairedif);

	while ((m = ifq_dequeue(ifq)) != NULL) {
#if NBPFILTER > 0
		if (ifp->if_bpf)
			bpf_mtap(ifp->if_bpf, m, BPF_DIRECTION_OUT);
#endif /* NBPFILTER > 0 */

		if (pairedifp != NULL) {
			if (m->m_flags & M_PKTHDR)
				m_resethdr(m);
			ml_enqueue(&ml, m);
		} else
			m_freem(m);
	}

	if (pairedifp != NULL) {
		if_input(pairedifp, &ml);
		if_put(pairedifp);
	}
}

int
pairioctl(struct ifnet *ifp, u_long cmd, caddr_t data)
{
	struct pair_softc	*sc = (struct pair_softc *)ifp->if_softc;
	struct ifreq		*ifr = (struct ifreq *)data;
	struct if_clone		*ifc;
	struct pair_softc	*pairedsc = ifp->if_softc;
	struct ifnet		*oldifp = NULL, *newifp = NULL;
	int			 error = 0, unit;

	switch (cmd) {
	case SIOCSIFADDR:
		ifp->if_flags |= IFF_UP;
		/* FALLTHROUGH */

	case SIOCSIFFLAGS:
		if (ifp->if_flags & IFF_UP)
			ifp->if_flags |= IFF_RUNNING;
		else
			ifp->if_flags &= ~IFF_RUNNING;
		break;

	case SIOCADDMULTI:
	case SIOCDELMULTI:
		break;

	case SIOCGIFMEDIA:
	case SIOCSIFMEDIA:
		error = ifmedia_ioctl(ifp, ifr, &sc->sc_media, cmd);
		break;

	case SIOCSIFPAIR:
		if (sc->sc_pairedif == ifr->ifr_index)
			break;

		/* Cannot link to myself */
		if (ifr->ifr_index == ifp->if_index) {
			error = EINVAL;
			break;
		}

		oldifp = if_get(sc->sc_pairedif);
		newifp = if_get(ifr->ifr_index);

		if (newifp != NULL) {
			pairedsc = newifp->if_softc;

			if (pairedsc->sc_pairedif != 0) {
				error = EBUSY;
				break;
			}

			/* Only allow pair(4) interfaces for the pair */
			if ((ifc = if_clone_lookup(newifp->if_xname,
			    &unit)) == NULL || strcmp("pair",
			    ifc->ifc_name) != 0) {
				error = ENODEV;
				break;
			}

			pairedsc->sc_pairedif = ifp->if_index;
			sc->sc_pairedif = ifr->ifr_index;
		} else
			sc->sc_pairedif = 0;

		if (oldifp != NULL) {
			pairedsc = oldifp->if_softc;
			pairedsc->sc_pairedif = 0;
		}
		break;

	case SIOCGIFPAIR:
		ifr->ifr_index = sc->sc_pairedif;
		break;

	default:
		error = ether_ioctl(ifp, &sc->sc_ac, cmd, data);
	}

	if (newifp != NULL || oldifp != NULL)
		pair_link_state(ifp);
	if (oldifp != NULL) {
		pair_link_state(oldifp);
		if_put(oldifp);
	}
	if (newifp != NULL) {
		pair_link_state(newifp);
		if_put(newifp);
	}

	return (error);
}
