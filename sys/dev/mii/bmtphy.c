/*-
 * SPDX-License-Identifier: BSD-2-Clause-NetBSD
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
 *
 *	from: NetBSD: bmtphy.c,v 1.8 2002/07/03 06:25:50 simonb Exp
 *
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

/*
 * Driver for the Broadcom BCM5201/BCM5202 "Mini-Theta" PHYs.  This also
 * drives the PHY on the 3Com 3c905C.  The 3c905C's PHY is described in
 * the 3c905C data sheet.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/socket.h>
#include <sys/bus.h>

#include <net/if.h>
#include <net/if_media.h>

#include <dev/mii/mii.h>
#include <dev/mii/miivar.h>
#include "miidevs.h"

#include <dev/mii/bmtphyreg.h>

#include "miibus_if.h"

static int	bmtphy_probe(device_t);
static int	bmtphy_attach(device_t);

static device_method_t bmtphy_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		bmtphy_probe),
	DEVMETHOD(device_attach,	bmtphy_attach),
	DEVMETHOD(device_detach,	mii_phy_detach),
	DEVMETHOD(device_shutdown,	bus_generic_shutdown),
	DEVMETHOD_END
};

static devclass_t	bmtphy_devclass;

static driver_t	bmtphy_driver = {
	"bmtphy",
	bmtphy_methods,
	sizeof(struct mii_softc)
};

DRIVER_MODULE(bmtphy, miibus, bmtphy_driver, bmtphy_devclass, 0, 0);

static int	bmtphy_service(struct mii_softc *, struct mii_data *, int);
static void	bmtphy_status(struct mii_softc *);
static void	bmtphy_reset(struct mii_softc *);

static const struct mii_phydesc bmtphys_dp[] = {
	MII_PHY_DESC(xxBROADCOM, BCM4401),
	MII_PHY_DESC(xxBROADCOM, BCM5201),
	MII_PHY_DESC(xxBROADCOM, BCM5214),
	MII_PHY_DESC(xxBROADCOM, BCM5221),
	MII_PHY_DESC(xxBROADCOM, BCM5222),
	MII_PHY_END
};

static const struct mii_phydesc bmtphys_lp[] = {
	MII_PHY_DESC(xxBROADCOM, 3C905B),
	MII_PHY_DESC(xxBROADCOM, 3C905C),
	MII_PHY_END
};

static const struct mii_phy_funcs bmtphy_funcs = {
	bmtphy_service,
	bmtphy_status,
	bmtphy_reset 
};

static int
bmtphy_probe(device_t dev)
{
	int	rval;

	/* Let exphy(4) take precedence for these. */
	rval = mii_phy_dev_probe(dev, bmtphys_lp, BUS_PROBE_LOW_PRIORITY);
	if (rval <= 0)
		return (rval);

	return (mii_phy_dev_probe(dev, bmtphys_dp, BUS_PROBE_DEFAULT));
}

static int
bmtphy_attach(device_t dev)
{

	mii_phy_dev_attach(dev, MIIF_NOMANPAUSE, &bmtphy_funcs, 1);
	return (0);
}

static int
bmtphy_service(struct mii_softc *sc, struct mii_data *mii, int cmd)
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
bmtphy_status(struct mii_softc *sc)
{
	struct mii_data *mii;
	struct ifmedia_entry *ife;
	int bmsr, bmcr, aux_csr;

	mii = sc->mii_pdata;
	ife = mii->mii_media.ifm_cur;

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

		aux_csr = PHY_READ(sc, MII_BMTPHY_AUX_CSR);
		if (aux_csr & AUX_CSR_SPEED)
			mii->mii_media_active |= IFM_100_TX;
		else
			mii->mii_media_active |= IFM_10_T;
		if (aux_csr & AUX_CSR_FDX)
			mii->mii_media_active |=
			    IFM_FDX | mii_phy_flowstatus(sc);
		else
			mii->mii_media_active |= IFM_HDX;
	} else
		mii->mii_media_active = ife->ifm_media;
}

static void
bmtphy_reset(struct mii_softc *sc)
{
	u_int16_t data;

	mii_phy_reset(sc);

	if (sc->mii_mpd_model == MII_MODEL_xxBROADCOM_BCM5221) {
		/* Enable shadow register mode. */
		data = PHY_READ(sc, 0x1f);
		PHY_WRITE(sc, 0x1f, data | 0x0080);

		/* Enable APD (Auto PowerDetect). */
		data = PHY_READ(sc, MII_BMTPHY_AUX2);
		PHY_WRITE(sc, MII_BMTPHY_AUX2, data | 0x0020);

		/* Enable clocks across APD for Auto-MDIX functionality. */
		data = PHY_READ(sc, MII_BMTPHY_INTR);
		PHY_WRITE(sc, MII_BMTPHY_INTR, data | 0x0004);

		/* Disable shadow register mode. */
		data = PHY_READ(sc, 0x1f);
		PHY_WRITE(sc, 0x1f, data & ~0x0080);
	}
}
