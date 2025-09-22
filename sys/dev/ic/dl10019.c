/*	$OpenBSD: dl10019.c,v 1.9 2015/11/24 17:11:39 mpi Exp $	*/
/*	$NetBSD$	*/

/*-
 * Copyright (c) 2001 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Jason R. Thorpe.
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
#include <sys/mbuf.h>
#include <sys/socket.h>
#include <sys/syslog.h>

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

#include <dev/ic/dl10019reg.h>

int	dl10019_mii_readreg(struct device *, int, int);
void	dl10019_mii_writereg(struct device *, int, int, int);
void	dl10019_mii_statchg(struct device *);

void	dl10019_mii_reset(struct dp8390_softc *);

/*
 * MII bit-bang glue.
 */
u_int32_t dl10019_mii_bitbang_read(struct device *);
void dl10019_mii_bitbang_write(struct device *, u_int32_t);

const struct mii_bitbang_ops dl10019_mii_bitbang_ops = {
	dl10019_mii_bitbang_read,
	dl10019_mii_bitbang_write,
	{
		DL0_GPIO_MII_DATAOUT,	/* MII_BIT_MDO */
		DL0_GPIO_MII_DATAIN,	/* MII_BIT_MDI */
		DL0_GPIO_MII_CLK,	/* MII_BIT_MDC */
		DL0_19_GPIO_MII_DIROUT,	/* MII_BIT_DIR_HOST_PHY */
		0,			/* MII_BIT_DIR_PHY_HOST */
	}
};

const struct mii_bitbang_ops dl10022_mii_bitbang_ops = {
	dl10019_mii_bitbang_read,
	dl10019_mii_bitbang_write,
	{
		DL0_GPIO_MII_DATAOUT,	/* MII_BIT_MDO */
		DL0_GPIO_MII_DATAIN,	/* MII_BIT_MDI */
		DL0_GPIO_MII_CLK,	/* MII_BIT_MDC */
		DL0_22_GPIO_MII_DIROUT,	/* MII_BIT_DIR_HOST_PHY */
		0,			/* MII_BIT_DIR_PHY_HOST */
	}
};

void
dl10019_mii_reset(struct dp8390_softc *sc)
{
	struct ne2000_softc *nsc = (void *) sc;
	int i;

	if (nsc->sc_type != NE2000_TYPE_DL10022)
		return;

	for (i = 0; i < 2; i++) {
		bus_space_write_1(sc->sc_regt, sc->sc_regh, NEDL_DL0_GPIO,
		    0x08);
		DELAY(1);
		bus_space_write_1(sc->sc_regt, sc->sc_regh, NEDL_DL0_GPIO,
		    0x0c);
		DELAY(1);
	}
	bus_space_write_1(sc->sc_regt, sc->sc_regh, NEDL_DL0_GPIO, 0x00);
}

void
dl10019_media_init(struct dp8390_softc *sc)
{
	struct ifnet *ifp = &sc->sc_arpcom.ac_if;

	sc->sc_mii.mii_ifp = ifp;
	sc->sc_mii.mii_readreg = dl10019_mii_readreg;
	sc->sc_mii.mii_writereg = dl10019_mii_writereg;
	sc->sc_mii.mii_statchg = dl10019_mii_statchg;
	ifmedia_init(&sc->sc_mii.mii_media, 0, dp8390_mediachange,
	    dp8390_mediastatus);

	dl10019_mii_reset(sc);

	mii_attach(&sc->sc_dev, &sc->sc_mii, 0xffffffff, MII_PHY_ANY,
	    MII_OFFSET_ANY, 0);

	if (LIST_FIRST(&sc->sc_mii.mii_phys) == NULL) {
		ifmedia_add(&sc->sc_mii.mii_media, IFM_ETHER|IFM_NONE, 0, NULL);
		ifmedia_set(&sc->sc_mii.mii_media, IFM_ETHER|IFM_NONE);
	} else
		ifmedia_set(&sc->sc_mii.mii_media, IFM_ETHER|IFM_AUTO);
}

void
dl10019_media_fini(struct dp8390_softc *sc)
{
	mii_detach(&sc->sc_mii, MII_PHY_ANY, MII_OFFSET_ANY);
}

int
dl10019_mediachange(struct dp8390_softc *sc)
{
	mii_mediachg(&sc->sc_mii);
	return (0);
}

void
dl10019_mediastatus(struct dp8390_softc *sc, struct ifmediareq *ifmr)
{
	mii_pollstat(&sc->sc_mii);
	ifmr->ifm_status = sc->sc_mii.mii_media_status;
	ifmr->ifm_active = sc->sc_mii.mii_media_active;
}

void
dl10019_init_card(struct dp8390_softc *sc)
{
	dl10019_mii_reset(sc);
	mii_mediachg(&sc->sc_mii);
}

void
dl10019_stop_card(struct dp8390_softc *sc)
{
	mii_down(&sc->sc_mii);
}

u_int32_t
dl10019_mii_bitbang_read(struct device *self)
{
	struct dp8390_softc *sc = (void *) self;

	/* We're already in Page 0. */
	return (bus_space_read_1(sc->sc_regt, sc->sc_regh, NEDL_DL0_GPIO) &
	    ~DL0_GPIO_PRESERVE);
}

void
dl10019_mii_bitbang_write(struct device *self, u_int32_t val)
{
	struct dp8390_softc *sc = (void *) self;
	u_int8_t gpio;

	/* We're already in Page 0. */
	gpio = bus_space_read_1(sc->sc_regt, sc->sc_regh, NEDL_DL0_GPIO);
	bus_space_write_1(sc->sc_regt, sc->sc_regh, NEDL_DL0_GPIO,
	    (val & ~DL0_GPIO_PRESERVE) | (gpio & DL0_GPIO_PRESERVE));
}

int
dl10019_mii_readreg(struct device *self, int phy, int reg)
{
	struct ne2000_softc *nsc = (void *) self;
	const struct mii_bitbang_ops *ops;

	ops = (nsc->sc_type == NE2000_TYPE_DL10022) ?
	    &dl10022_mii_bitbang_ops : &dl10019_mii_bitbang_ops;

	return (mii_bitbang_readreg(self, ops, phy, reg));
}

void
dl10019_mii_writereg(struct device *self, int phy, int reg, int val)
{
	struct ne2000_softc *nsc = (void *) self;
	const struct mii_bitbang_ops *ops;
	
	ops = (nsc->sc_type == NE2000_TYPE_DL10022) ?
	    &dl10022_mii_bitbang_ops : &dl10019_mii_bitbang_ops;

	mii_bitbang_writereg(self, ops, phy, reg, val);
}

void
dl10019_mii_statchg(struct device *self)
{
	struct dp8390_softc *sc = (void *) self;
	struct ne2000_softc *nsc = (void *) self;

	/*
	 * Disable collision detection on the DL10022 if
	 * we are on a full-duplex link.
	 */
	if (nsc->sc_type == NE2000_TYPE_DL10022) {
		u_int8_t diag;

		if (sc->sc_mii.mii_media_active & IFM_FDX)
			diag = DL0_DIAG_NOCOLLDETECT;
		else
			diag = 0;
		bus_space_write_1(sc->sc_regt, sc->sc_regh,
		    NEDL_DL0_DIAG, diag);
	}
}
