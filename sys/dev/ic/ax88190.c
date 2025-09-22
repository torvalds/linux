/*	$OpenBSD: ax88190.c,v 1.9 2021/03/07 06:21:38 jsg Exp $	*/
/*	$NetBSD$	*/

/*-
 * Copyright (c) 2001 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Enami Tsugutomo.
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

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/device.h>
#include <sys/socket.h>

#include <net/if.h>
#include <net/if_media.h>

#include <netinet/in.h>
#include <netinet/if_ether.h>

#include <machine/bus.h>

#include <dev/mii/miivar.h>
#include <dev/mii/mii.h>
#include <dev/mii/mii_bitbang.h>

#include <dev/ic/dp8390reg.h>
#include <dev/ic/dp8390var.h>

#include <dev/ic/ne2000reg.h>
#include <dev/ic/ne2000var.h>

#include <dev/ic/ax88190reg.h>

int	ax88190_mii_readreg(struct device *, int, int);
void	ax88190_mii_writereg(struct device *, int, int, int);
void	ax88190_mii_statchg(struct device *);

/*
 * MII bit-bang glue.
 */
u_int32_t	ax88190_mii_bitbang_read(struct device *);
void		ax88190_mii_bitbang_write(struct device *, u_int32_t);

const struct mii_bitbang_ops ax88190_mii_bitbang_ops = {
	ax88190_mii_bitbang_read,
	ax88190_mii_bitbang_write,
	{
		AX88190_MEMR_MDO,	/* MII_BIT_MDO */
		AX88190_MEMR_MDI,	/* MII_BIT_MDI */
		AX88190_MEMR_MDC,	/* MII_BIT_MDC */
		0,			/* MII_BIT_DIR_HOST_PHY */
		AX88190_MEMR_MDIR,	/* MII_BIT_DIR_PHY_HOST */
	}
};

void
ax88190_media_init(struct dp8390_softc *sc)
{
	struct ifnet *ifp = &sc->sc_arpcom.ac_if;

	sc->sc_mii.mii_ifp = ifp;
	sc->sc_mii.mii_readreg = ax88190_mii_readreg;
	sc->sc_mii.mii_writereg = ax88190_mii_writereg;
	sc->sc_mii.mii_statchg = ax88190_mii_statchg;
	ifmedia_init(&sc->sc_mii.mii_media, 0, dp8390_mediachange,
	    dp8390_mediastatus);

	mii_attach(&sc->sc_dev, &sc->sc_mii, 0xffffffff, MII_PHY_ANY,
	    MII_OFFSET_ANY, 0);

	if (LIST_FIRST(&sc->sc_mii.mii_phys) == NULL) {
		ifmedia_add(&sc->sc_mii.mii_media, IFM_ETHER|IFM_NONE, 0,
		    NULL);
		ifmedia_set(&sc->sc_mii.mii_media, IFM_ETHER|IFM_NONE);
	} else
		ifmedia_set(&sc->sc_mii.mii_media, IFM_ETHER|IFM_AUTO);
}

void
ax88190_media_fini(struct dp8390_softc *sc)
{
	mii_detach(&sc->sc_mii, MII_PHY_ANY, MII_OFFSET_ANY);
}

int
ax88190_mediachange(struct dp8390_softc *sc)
{
	mii_mediachg(&sc->sc_mii);
	return (0);
}

void
ax88190_mediastatus(struct dp8390_softc *sc, struct ifmediareq *ifmr)
{
	mii_pollstat(&sc->sc_mii);
	ifmr->ifm_status = sc->sc_mii.mii_media_status;
	ifmr->ifm_active = sc->sc_mii.mii_media_active;
}

void
ax88190_init_card(struct dp8390_softc *sc)
{
	mii_mediachg(&sc->sc_mii);
}

void
ax88190_stop_card(struct dp8390_softc *sc)
{
	mii_down(&sc->sc_mii);
}

u_int32_t
ax88190_mii_bitbang_read(struct device *self)
{
	struct ne2000_softc *sc = (void *)self;

	return (bus_space_read_1(sc->sc_asict, sc->sc_asich, AX88190_MEMR));
}

void
ax88190_mii_bitbang_write(struct device *self, u_int32_t val)
{
	struct ne2000_softc *sc = (void *)self;

	bus_space_write_1(sc->sc_asict, sc->sc_asich, AX88190_MEMR, val);
}

int
ax88190_mii_readreg(struct device *self, int phy, int reg)
{
	return (mii_bitbang_readreg(self, &ax88190_mii_bitbang_ops, phy, reg));
}

void
ax88190_mii_writereg(struct device *self, int phy, int reg, int val)
{
	mii_bitbang_writereg(self, &ax88190_mii_bitbang_ops, phy, reg, val);
}

void
ax88190_mii_statchg(struct device *self)
{
	/* XXX */
}
