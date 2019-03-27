/*	$NetBSD: gentbi.c,v 1.15 2006/03/29 07:05:24 thorpej Exp $	*/

/*-
 * SPDX-License-Identifier: BSD-2-Clause-NetBSD AND BSD-2-Clause
 *
 * Copyright (c) 1998, 1999, 2000, 2001 The NetBSD Foundation, Inc.
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

/*
 * Copyright (c) 1997 Manuel Bouyer.  All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * Driver for generic ten-bit (1000BASE-SX) interfaces, built into
 * many Gigabit Ethernet chips.
 *
 * All we have to do here is correctly report speed and duplex.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

/*
 * Driver for generic unknown ten-bit interfaces(1000BASE-{LX,SX}
 * fiber interfaces).
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/socket.h>
#include <sys/errno.h>
#include <sys/bus.h>

#include <net/if.h>
#include <net/if_media.h>

#include <dev/mii/mii.h>
#include <dev/mii/miivar.h>
#include "miidevs.h"

#include "miibus_if.h"

static int	gentbi_probe(device_t);
static int	gentbi_attach(device_t);

static device_method_t gentbi_methods[] = {
	/* device interface */
	DEVMETHOD(device_probe,		gentbi_probe),
	DEVMETHOD(device_attach,	gentbi_attach),
	DEVMETHOD(device_detach,	mii_phy_detach),
	DEVMETHOD(device_shutdown,	bus_generic_shutdown),
	DEVMETHOD_END
};

static devclass_t gentbi_devclass;

static driver_t gentbi_driver = {
	"gentbi",
	gentbi_methods,
	sizeof(struct mii_softc)
};

DRIVER_MODULE(gentbi, miibus, gentbi_driver, gentbi_devclass, 0, 0);

static int	gentbi_service(struct mii_softc *, struct mii_data *, int);
static void	gentbi_status(struct mii_softc *);

static const struct mii_phy_funcs gentbi_funcs = {
	gentbi_service,
	gentbi_status,
	mii_phy_reset
};

static int
gentbi_probe(device_t dev)
{
	device_t parent;
	struct mii_attach_args *ma;
	int bmsr, extsr;

	parent = device_get_parent(dev);
	ma = device_get_ivars(dev);

	/*
	 * We match as a generic TBI if:
	 *
	 *	- There is no media in the BMSR.
	 *	- EXTSR has only 1000X.
	 */
	bmsr = MIIBUS_READREG(parent, ma->mii_phyno, MII_BMSR);
	if ((bmsr & BMSR_EXTSTAT) == 0 || (bmsr & BMSR_MEDIAMASK) != 0)
		return (ENXIO);

	extsr = MIIBUS_READREG(parent, ma->mii_phyno, MII_EXTSR);
	if (extsr & (EXTSR_1000TFDX|EXTSR_1000THDX))
		return (ENXIO);

	if (extsr & (EXTSR_1000XFDX|EXTSR_1000XHDX)) {
		/*
		 * We think this is a generic TBI.  Return a match
		 * priority higher than ukphy, but lower than what
		 * specific drivers will return.
		 */
		device_set_desc(dev, "Generic ten-bit interface");
		return (BUS_PROBE_LOW_PRIORITY);
	}

	return (ENXIO);
}

static int
gentbi_attach(device_t dev)
{
	struct mii_softc *sc;

	sc = device_get_softc(dev);

	mii_phy_dev_attach(dev, MIIF_NOMANPAUSE, &gentbi_funcs, 0);

	PHY_RESET(sc);

	/*
	 * Mask out all media in the BMSR.  We only are really interested
	 * in "auto".
	 */
	sc->mii_capabilities =
	    PHY_READ(sc, MII_BMSR) & sc->mii_capmask & ~BMSR_MEDIAMASK;
	if (sc->mii_capabilities & BMSR_EXTSTAT)
		sc->mii_extcapabilities = PHY_READ(sc, MII_EXTSR);

	device_printf(dev, " ");
	mii_phy_add_media(sc);
	printf("\n");

	MIIBUS_MEDIAINIT(sc->mii_dev);
	return (0);
}

static int
gentbi_service(struct mii_softc *sc, struct mii_data *mii, int cmd)
{

	switch (cmd) {
	case MII_POLLSTAT:
		break;

	case MII_MEDIACHG:
		mii_phy_setmedia(sc);
		break;

	case MII_TICK:
		if (mii_phy_tick(sc) == EJUSTRETURN)
			return (0);
		break;
	}

	/* Update the media status. */
	PHY_STATUS(sc);

	/* Callback if something changed. */
	mii_phy_update(sc, cmd);
	return (0);
}

static void
gentbi_status(struct mii_softc *sc)
{
	struct mii_data *mii = sc->mii_pdata;
	struct ifmedia_entry *ife = mii->mii_media.ifm_cur;
	int bmsr, bmcr, anlpar;

	mii->mii_media_status = IFM_AVALID;
	mii->mii_media_active = IFM_ETHER;

	bmsr = PHY_READ(sc, MII_BMSR) | PHY_READ(sc, MII_BMSR);

	if (bmsr & BMSR_LINK)
		mii->mii_media_status |= IFM_ACTIVE;

	bmcr = PHY_READ(sc, MII_BMCR);
	if (bmcr & BMCR_ISO) {
		mii->mii_media_active |= IFM_NONE;
		mii->mii_media_status = 0;
		return;
	}

	if (bmcr & BMCR_LOOP)
		mii->mii_media_active |= IFM_LOOP;

	if (bmcr & BMCR_AUTOEN) {
		/*
		 * The media status bits are only valid if autonegotiation
		 * has completed (or it's disabled).
		 */
		if ((bmsr & BMSR_ACOMP) == 0) {
			/* Erg, still trying, I guess... */
			mii->mii_media_active |= IFM_NONE;
			return;
		}

		/*
		 * The media is always 1000baseSX.  Check the ANLPAR to
		 * see if we're doing full-duplex.
		 */
		mii->mii_media_active |= IFM_1000_SX;
		anlpar = PHY_READ(sc, MII_ANLPAR);
		if ((sc->mii_extcapabilities & EXTSR_1000XFDX) != 0 &&
		    (anlpar & ANLPAR_X_FD) != 0)
			mii->mii_media_active |=
			    IFM_FDX | mii_phy_flowstatus(sc);
		else
			mii->mii_media_active |= IFM_HDX;
	} else
		mii->mii_media_active = ife->ifm_media;
}
