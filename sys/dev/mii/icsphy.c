/*	$NetBSD: icsphy.c,v 1.41 2006/11/16 21:24:07 christos Exp $	*/

/*-
 * SPDX-License-Identifier: BSD-2-Clause-NetBSD
 *
 * Copyright (c) 1998, 1999, 2000 The NetBSD Foundation, Inc.
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

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

/*
 * driver for Integrated Circuit Systems' ICS1889-1893 ethernet 10/100 PHY
 * datasheet from www.icst.com
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

#include <dev/mii/icsphyreg.h>

#include "miibus_if.h"

static int	icsphy_probe(device_t dev);
static int	icsphy_attach(device_t dev);

static device_method_t icsphy_methods[] = {
	/* device interface */
	DEVMETHOD(device_probe,		icsphy_probe),
	DEVMETHOD(device_attach,	icsphy_attach),
	DEVMETHOD(device_detach,	mii_phy_detach),
	DEVMETHOD(device_shutdown,	bus_generic_shutdown),
	DEVMETHOD_END
};

static devclass_t icsphy_devclass;

static driver_t icsphy_driver = {
	"icsphy",
	icsphy_methods,
	sizeof(struct mii_softc)
};

DRIVER_MODULE(icsphy, miibus, icsphy_driver, icsphy_devclass, 0, 0);

static int	icsphy_service(struct mii_softc *, struct mii_data *, int);
static void	icsphy_status(struct mii_softc *);
static void	icsphy_reset(struct mii_softc *);

static const struct mii_phydesc icsphys[] = {
	MII_PHY_DESC(ICS, 1889),
	MII_PHY_DESC(ICS, 1890),
	MII_PHY_DESC(ICS, 1892),
	MII_PHY_DESC(ICS, 1893),
	MII_PHY_DESC(ICS, 1893C),
	MII_PHY_END
};

static const struct mii_phy_funcs icsphy_funcs = {
	icsphy_service,
	icsphy_status,
	icsphy_reset
};

static int
icsphy_probe(device_t dev)
{

	return (mii_phy_dev_probe(dev, icsphys, BUS_PROBE_DEFAULT));
}

static int
icsphy_attach(device_t dev)
{

	mii_phy_dev_attach(dev, MIIF_NOISOLATE | MIIF_NOMANPAUSE,
	    &icsphy_funcs, 1);
	return (0);
}

static int
icsphy_service(struct mii_softc *sc, struct mii_data *mii, int cmd)
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
icsphy_status(struct mii_softc *sc)
{
	struct mii_data *mii = sc->mii_pdata;
	struct ifmedia_entry *ife = mii->mii_media.ifm_cur;
	int bmcr, qpr;

	mii->mii_media_status = IFM_AVALID;
	mii->mii_media_active = IFM_ETHER;

	/*
	 * Don't get link from the BMSR.  It's available in the QPR,
	 * and we have to read it twice to unlatch it anyhow.  This
	 * gives us fewer register reads.
	 */
	qpr = PHY_READ(sc, MII_ICSPHY_QPR);		/* unlatch */
	qpr = PHY_READ(sc, MII_ICSPHY_QPR);		/* real value */

	if (qpr & QPR_LINK)
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
		if ((qpr & QPR_ACOMP) == 0) {
			/* Erg, still trying, I guess... */
			mii->mii_media_active |= IFM_NONE;
			return;
		}
		if (qpr & QPR_SPEED)
			mii->mii_media_active |= IFM_100_TX;
		else
			mii->mii_media_active |= IFM_10_T;
		if (qpr & QPR_FDX)
			mii->mii_media_active |=
			    IFM_FDX | mii_phy_flowstatus(sc);
		else
			mii->mii_media_active |= IFM_HDX;
	} else
		mii->mii_media_active = ife->ifm_media;
}

static void
icsphy_reset(struct mii_softc *sc)
{

	mii_phy_reset(sc);
	/* set powerdown feature */
	switch (sc->mii_mpd_model) {
		case MII_MODEL_ICS_1890:
		case MII_MODEL_ICS_1893:
			PHY_WRITE(sc, MII_ICSPHY_ECR2, ECR2_100AUTOPWRDN);
			break;
		case MII_MODEL_ICS_1892:
			PHY_WRITE(sc, MII_ICSPHY_ECR2,
			    ECR2_10AUTOPWRDN|ECR2_100AUTOPWRDN);
			break;
		default:
			/* 1889 have no ECR2 */
			break;
	}
	/*
	 * There is no description that the reset do auto-negotiation in the
	 * data sheet.
	 */
	PHY_WRITE(sc, MII_BMCR, BMCR_S100|BMCR_STARTNEG|BMCR_FDX);
}
