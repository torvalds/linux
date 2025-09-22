/*	$OpenBSD: rtl80x9.c,v 1.12 2021/03/07 06:21:38 jsg Exp $	*/
/*	$NetBSD: rtl80x9.c,v 1.1 1998/10/31 00:44:33 thorpej Exp $	*/

/*-
 * Copyright (c) 1998 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Jason R. Thorpe of the Numerical Aerospace Simulation Facility,
 * NASA Ames Research Center.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include "bpfilter.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/mbuf.h>
#include <sys/syslog.h>
#include <sys/socket.h>
#include <sys/device.h>

#include <net/if.h>
#include <net/if_media.h>

#include <netinet/in.h>
#include <netinet/if_ether.h>

#include <machine/bus.h>

#include <dev/ic/dp8390reg.h>
#include <dev/ic/dp8390var.h>

#include <dev/ic/ne2000var.h>

#include <dev/ic/rtl80x9reg.h>
#include <dev/ic/rtl80x9var.h>

int
rtl80x9_mediachange(struct dp8390_softc *dsc)
{

	/*
	 * Current media is already set up.  Just reset the interface
	 * to let the new value take hold.  The new media will be
	 * set up in ne_pci_rtl8029_init_card() called via dp8390_init().
	 */
	dp8390_reset(dsc);
	return (0);
}

void
rtl80x9_mediastatus(struct dp8390_softc *sc, struct ifmediareq *ifmr)
{
	struct ifnet *ifp = &sc->sc_arpcom.ac_if;
	u_int8_t cr_proto = sc->cr_proto |
	    ((ifp->if_flags & IFF_RUNNING) ? ED_CR_STA : ED_CR_STP);

	/*
	 * Sigh, can detect which media is being used, but can't
	 * detect if we have link or not.
	 */

	/* Set NIC to page 3 registers. */
	NIC_PUT(sc->sc_regt, sc->sc_regh, ED_P0_CR, cr_proto | ED_CR_PAGE_3);

	if (NIC_GET(sc->sc_regt, sc->sc_regh, NERTL_RTL3_CONFIG0) &
	    RTL3_CONFIG0_BNC)
		ifmr->ifm_active = IFM_ETHER|IFM_10_2;
	else {
		ifmr->ifm_active = IFM_ETHER|IFM_10_T;
		if (NIC_GET(sc->sc_regt, sc->sc_regh, NERTL_RTL3_CONFIG3) &
		    RTL3_CONFIG3_FUDUP)
			ifmr->ifm_active |= IFM_FDX;
	}

	/* Set NIC to page 0 registers. */
	NIC_PUT(sc->sc_regt, sc->sc_regh, ED_P0_CR, cr_proto | ED_CR_PAGE_0);
}

void
rtl80x9_init_card(struct dp8390_softc *sc)
{
	struct ifmedia *ifm = &sc->sc_media;
	struct ifnet *ifp = &sc->sc_arpcom.ac_if;
	u_int8_t cr_proto = sc->cr_proto |
	    ((ifp->if_flags & IFF_RUNNING) ? ED_CR_STA : ED_CR_STP);
	u_int8_t reg;

	/* Set NIC to page 3 registers. */
	NIC_PUT(sc->sc_regt, sc->sc_regh, ED_P0_CR, cr_proto | ED_CR_PAGE_3);

	/* write enable config1-3. */
	NIC_PUT(sc->sc_regt, sc->sc_regh, NERTL_RTL3_EECR,
	    RTL3_EECR_EEM1|RTL3_EECR_EEM0);

	/* First, set basic media type. */
	reg = NIC_GET(sc->sc_regt, sc->sc_regh, NERTL_RTL3_CONFIG2);
	reg &= ~(RTL3_CONFIG2_PL1|RTL3_CONFIG2_PL0);
	switch (IFM_SUBTYPE(ifm->ifm_cur->ifm_media)) {
	case IFM_AUTO:
		/* Nothing to do; both bits clear == auto-detect. */
		break;

	case IFM_10_T:
		/*
		 * According to docs, this should be:
		 * reg |= RTL3_CONFIG2_PL0;
		 * but this doesn't work, so make it the same as AUTO.
		 */
		break;

	case IFM_10_2:
		reg |= RTL3_CONFIG2_PL1|RTL3_CONFIG2_PL0;
		break;
	}
	NIC_PUT(sc->sc_regt, sc->sc_regh, NERTL_RTL3_CONFIG2, reg);

	/* Now, set duplex mode. */
	reg = NIC_GET(sc->sc_regt, sc->sc_regh, NERTL_RTL3_CONFIG3);
	if (ifm->ifm_cur->ifm_media & IFM_FDX)
		reg |= RTL3_CONFIG3_FUDUP;
	else
		reg &= ~RTL3_CONFIG3_FUDUP;
	NIC_PUT(sc->sc_regt, sc->sc_regh, NERTL_RTL3_CONFIG3, reg);

	/* write disable config1-3 */
	NIC_PUT(sc->sc_regt, sc->sc_regh, NERTL_RTL3_EECR, 0);

	/* Set NIC to page 0 registers. */
	NIC_PUT(sc->sc_regt, sc->sc_regh, ED_P0_CR, cr_proto | ED_CR_PAGE_0);
}

void
rtl80x9_media_init(struct dp8390_softc *sc)
{
	static uint64_t rtl80x9_media[] = {
		IFM_ETHER|IFM_AUTO,
		IFM_ETHER|IFM_10_T,
		IFM_ETHER|IFM_10_T|IFM_FDX,
		IFM_ETHER|IFM_10_2,
	};
	static const int rtl80x9_nmedia =
	    sizeof(rtl80x9_media) / sizeof(rtl80x9_media[0]);

	int i;
	uint64_t defmedia;
	u_int8_t conf2, conf3;

	/* Set NIC to page 3 registers. */
	bus_space_write_1(sc->sc_regt, sc->sc_regh, ED_P0_CR, ED_CR_PAGE_3);

	conf2 = bus_space_read_1(sc->sc_regt, sc->sc_regh, NERTL_RTL3_CONFIG2);
	conf3 = bus_space_read_1(sc->sc_regt, sc->sc_regh, NERTL_RTL3_CONFIG3);

	conf2 &= RTL3_CONFIG2_PL1|RTL3_CONFIG2_PL0;

	switch (conf2) {
	case 0:
		defmedia = IFM_ETHER|IFM_AUTO;
		break;

	case RTL3_CONFIG2_PL1|RTL3_CONFIG2_PL0:
	case RTL3_CONFIG2_PL1:	/* XXX rtl docs sys 10base5, but chip cant do */
		defmedia = IFM_ETHER|IFM_10_2;
		break;

	case RTL3_CONFIG2_PL0:
		if (conf3 & RTL3_CONFIG3_FUDUP)
			defmedia = IFM_ETHER|IFM_10_T|IFM_FDX;
		else
			defmedia = IFM_ETHER|IFM_10_T;
		break;
	}

	/* Set NIC to page 0 registers. */
	bus_space_write_1(sc->sc_regt, sc->sc_regh, ED_P0_CR, ED_CR_PAGE_0);

	ifmedia_init(&sc->sc_media, 0, dp8390_mediachange, dp8390_mediastatus);
	for (i = 0; i < rtl80x9_nmedia; i++)
		ifmedia_add(&sc->sc_media, rtl80x9_media[i], 0, NULL);
	ifmedia_set(&sc->sc_media, defmedia);
}
