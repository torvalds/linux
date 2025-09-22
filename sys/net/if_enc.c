/*	$OpenBSD: if_enc.c,v 1.79 2022/08/29 07:51:45 bluhm Exp $	*/

/*
 * Copyright (c) 2010 Reyk Floeter <reyk@vantronix.net>
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
#include <sys/malloc.h>
#include <sys/socket.h>
#include <sys/sockio.h>
#include <sys/mbuf.h>

#include <net/if.h>
#include <net/if_dl.h>
#include <net/if_var.h>
#include <net/if_enc.h>
#include <net/if_types.h>
#if NBPFILTER > 0
#include <net/bpf.h>
#endif

struct ifnet			**enc_ifps;	/* rdomain-mapped enc ifs */
u_int				  enc_max_rdomain;
struct ifnet			**enc_allifps;	/* unit-mapped enc ifs */
u_int				  enc_max_unit;
#define ENC_MAX_UNITS		  4096		/* XXX n per rdomain */

void	 encattach(int);

int	 enc_clone_create(struct if_clone *, int);
int	 enc_clone_destroy(struct ifnet *);
int	 enc_output(struct ifnet *, struct mbuf *, struct sockaddr *,
	    struct rtentry *);
int	 enc_ioctl(struct ifnet *, u_long, caddr_t);

int	 enc_setif(struct ifnet *, u_int);
void	 enc_unsetif(struct ifnet *);

struct if_clone enc_cloner =
    IF_CLONE_INITIALIZER("enc", enc_clone_create, enc_clone_destroy);

void
encattach(int count)
{
	/* Create enc0 by default */
	(void)enc_clone_create(&enc_cloner, 0);

	if_clone_attach(&enc_cloner);
}

int
enc_clone_create(struct if_clone *ifc, int unit)
{
	struct enc_softc	*sc;
	struct ifnet		*ifp;
	struct ifnet		**new;
	size_t			 oldlen;
	int			 error;

	if (unit > ENC_MAX_UNITS)
		return (EINVAL);

	if ((sc = malloc(sizeof(*sc), M_DEVBUF, M_NOWAIT|M_ZERO)) == NULL)
		return (ENOBUFS);

	sc->sc_unit = unit;

	ifp = &sc->sc_if;
	ifp->if_softc = sc;
	ifp->if_type = IFT_ENC;
	ifp->if_xflags = IFXF_CLONED;
	ifp->if_output = enc_output;
	ifp->if_ioctl = enc_ioctl;
	ifp->if_hdrlen = ENC_HDRLEN;

	snprintf(ifp->if_xname, sizeof(ifp->if_xname), "%s%d",
	    ifc->ifc_name, unit);

	if_attach(ifp);
	if (unit == 0)
		if_addgroup(ifp, ifc->ifc_name);
	/*
	 * enc(4) does not have a link-layer address but rtrequest()
	 * wants an ifa for every route entry.  So let's setup a fake
	 * and empty ifa of type AF_LINK for this purpose.
	 */
	if_alloc_sadl(ifp);
	refcnt_init_trace(&sc->sc_ifa.ifa_refcnt, DT_REFCNT_IDX_IFADDR);
	sc->sc_ifa.ifa_ifp = ifp;
	sc->sc_ifa.ifa_addr = sdltosa(ifp->if_sadl);
	sc->sc_ifa.ifa_netmask = NULL;

#if NBPFILTER > 0
	bpfattach(&ifp->if_bpf, ifp, DLT_ENC, ENC_HDRLEN);
#endif
	NET_LOCK();
	error = enc_setif(ifp, 0);
	if (error != 0) {
		NET_UNLOCK();
		if_detach(ifp);
		free(sc, M_DEVBUF, sizeof(*sc));
		return (error);
	}

	if (enc_allifps == NULL || unit > enc_max_unit) {
		if ((new = mallocarray(unit + 1, sizeof(struct ifnet *),
		    M_DEVBUF, M_NOWAIT|M_ZERO)) == NULL) {
			NET_UNLOCK();
			return (ENOBUFS);
		}

		if (enc_allifps != NULL) {
			oldlen = sizeof(struct ifnet *) * (enc_max_unit + 1);
			memcpy(new, enc_allifps, oldlen);
			free(enc_allifps, M_DEVBUF, oldlen);
		}
		enc_allifps = new;
		enc_max_unit = unit;
	}
	enc_allifps[unit] = ifp;
	NET_UNLOCK();

	return (0);
}

int
enc_clone_destroy(struct ifnet *ifp)
{
	struct enc_softc	*sc = ifp->if_softc;

	/* Protect users from removing enc0 */
	if (sc->sc_unit == 0)
		return (EPERM);

	NET_LOCK();
	enc_allifps[sc->sc_unit] = NULL;
	enc_unsetif(ifp);
	NET_UNLOCK();

	if_detach(ifp);
	if (refcnt_rele(&sc->sc_ifa.ifa_refcnt) == 0) {
		panic("%s: ifa refcnt has %u refs", __func__,
		    sc->sc_ifa.ifa_refcnt.r_refs);
	}
	free(sc, M_DEVBUF, sizeof(*sc));

	return (0);
}

int
enc_output(struct ifnet *ifp, struct mbuf *m, struct sockaddr *sa,
    struct rtentry *rt)
{
	m_freem(m);	/* drop packet */
	return (EAFNOSUPPORT);
}

int
enc_ioctl(struct ifnet *ifp, u_long cmd, caddr_t data)
{
	struct ifreq	*ifr = (struct ifreq *)data;
	int		 error;

	switch (cmd) {
	case SIOCSIFADDR:
	case SIOCSIFDSTADDR:
	case SIOCSIFFLAGS:
		if (ifp->if_flags & IFF_UP)
			ifp->if_flags |= IFF_RUNNING;
		else
			ifp->if_flags &= ~IFF_RUNNING;
		break;
	case SIOCSIFRDOMAIN:
		if ((error = enc_setif(ifp, ifr->ifr_rdomainid)) != 0)
			return (error);
		/* FALLTHROUGH */
	default:
		return (ENOTTY);
	}

	return (0);
}

struct ifnet *
enc_getif(u_int rdomain, u_int unit)
{
	struct ifnet	*ifp;

	NET_ASSERT_LOCKED();

	/* Check if the caller wants to get a non-default enc interface */
	if (unit > 0) {
		if (unit > enc_max_unit)
			return (NULL);
		ifp = enc_allifps[unit];
		if (ifp == NULL || ifp->if_rdomain != rdomain)
			return (NULL);
		return (ifp);
	}

	/* Otherwise return the default enc interface for this rdomain */
	if (enc_ifps == NULL)
		return (NULL);
	else if (rdomain > RT_TABLEID_MAX)
		return (NULL);
	else if (rdomain > enc_max_rdomain)
		return (NULL);
	return (enc_ifps[rdomain]);
}

struct ifaddr *
enc_getifa(u_int rdomain, u_int unit)
{
	struct ifnet		*ifp;
	struct enc_softc	*sc;

	ifp = enc_getif(rdomain, unit);
	if (ifp == NULL)
		return (NULL);

	sc = ifp->if_softc;
	return (&sc->sc_ifa);
}
int
enc_setif(struct ifnet *ifp, u_int rdomain)
{
	struct ifnet	**new;
	size_t		 oldlen;

	NET_ASSERT_LOCKED();

	enc_unsetif(ifp);

	/*
	 * There can only be one default encif per rdomain -
	 * Don't overwrite the existing enc iface that is stored
	 * for this rdomain, so only the first enc interface that
	 * was added for this rdomain becomes the default.
	 */
	if (enc_getif(rdomain, 0) != NULL)
		return (0);

	if (rdomain > RT_TABLEID_MAX)
		return (EINVAL);

	if (enc_ifps == NULL || rdomain > enc_max_rdomain) {
		if ((new = mallocarray(rdomain + 1, sizeof(struct ifnet *),
		    M_DEVBUF, M_NOWAIT|M_ZERO)) == NULL)
			return (ENOBUFS);

		if (enc_ifps != NULL) {
			oldlen = sizeof(struct ifnet *) * (enc_max_rdomain + 1);
			memcpy(new, enc_ifps, oldlen);
			free(enc_ifps, M_DEVBUF, oldlen);
		}
		enc_ifps = new;
		enc_max_rdomain = rdomain;
	}

	enc_ifps[rdomain] = ifp;

	/* Indicate that this interface is the rdomain default */
	ifp->if_link_state = LINK_STATE_UP;

	return (0);
}

void
enc_unsetif(struct ifnet *ifp)
{
	u_int			 rdomain = ifp->if_rdomain, i;
	struct ifnet		*oifp, *nifp;

	if ((oifp = enc_getif(rdomain, 0)) == NULL || oifp != ifp)
		return;

	/* Clear slot for this rdomain */
	enc_ifps[rdomain] = NULL;
	ifp->if_link_state = LINK_STATE_UNKNOWN;

	/*
	 * Now find the next available encif to be the default interface
	 * for this rdomain.
	 */
	for (i = 0; i < (enc_max_unit + 1); i++) {
		nifp = enc_allifps[i];

		if (nifp == NULL || nifp == ifp || nifp->if_rdomain != rdomain)
			continue;

		enc_ifps[rdomain] = nifp;
		nifp->if_link_state = LINK_STATE_UP;
		break;
	}
}
