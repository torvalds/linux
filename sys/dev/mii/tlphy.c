/*	$NetBSD: tlphy.c,v 1.18 1999/05/14 11:40:28 drochner Exp $	*/

/*-
 * SPDX-License-Identifier: BSD-2-Clause-NetBSD AND BSD-2-Clause
 *
 * Copyright (c) 1998, 1999 The NetBSD Foundation, Inc.
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

/*-
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
 * Driver for Texas Instruments's ThunderLAN PHYs
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/socket.h>
#include <sys/errno.h>
#include <sys/module.h>
#include <sys/bus.h>
#include <sys/malloc.h>

#include <machine/bus.h>

#include <net/if.h>
#include <net/if_media.h>

#include <dev/mii/mii.h>
#include <dev/mii/miivar.h>
#include "miidevs.h"

#include <dev/mii/tlphyreg.h>

#include "miibus_if.h"

struct tlphy_softc {
	struct mii_softc sc_mii;		/* generic PHY */
	int sc_need_acomp;
};

static int tlphy_probe(device_t);
static int tlphy_attach(device_t);

static device_method_t tlphy_methods[] = {
	/* device interface */
	DEVMETHOD(device_probe,		tlphy_probe),
	DEVMETHOD(device_attach,	tlphy_attach),
	DEVMETHOD(device_detach,	mii_phy_detach),
	DEVMETHOD(device_shutdown,	bus_generic_shutdown),
	DEVMETHOD_END
};

static devclass_t tlphy_devclass;

static driver_t tlphy_driver = {
	"tlphy",
	tlphy_methods,
	sizeof(struct tlphy_softc)
};

DRIVER_MODULE(tlphy, miibus, tlphy_driver, tlphy_devclass, 0, 0);

static int	tlphy_service(struct mii_softc *, struct mii_data *, int);
static int	tlphy_auto(struct tlphy_softc *);
static void	tlphy_acomp(struct tlphy_softc *);
static void	tlphy_status(struct mii_softc *);

static const struct mii_phydesc tlphys[] = {
	MII_PHY_DESC(TI, TLAN10T),
	MII_PHY_END
};

static const struct mii_phy_funcs tlphy_funcs = {
	tlphy_service,
	tlphy_status,
	mii_phy_reset
};

static int
tlphy_probe(device_t dev)
{

	if (!mii_dev_mac_match(dev, "tl"))
		return (ENXIO);
	return (mii_phy_dev_probe(dev, tlphys, BUS_PROBE_DEFAULT));
}

static int
tlphy_attach(device_t dev)
{
	device_t *devlist;
	struct mii_softc *other, *sc_mii;
	const char *sep = "";
	int capmask, devs, i;

	sc_mii = device_get_softc(dev);

	mii_phy_dev_attach(dev, MIIF_NOMANPAUSE, &tlphy_funcs, 0);

	/*
	 * Note that if we're on a device that also supports 100baseTX,
	 * we are not going to want to use the built-in 10baseT port,
	 * since there will be another PHY on the MII wired up to the
	 * UTP connector.
	 */
	capmask = BMSR_DEFCAPMASK;
	if (sc_mii->mii_inst &&
	    device_get_children(sc_mii->mii_dev, &devlist, &devs) == 0) {
		for (i = 0; i < devs; i++) {
			if (devlist[i] != dev) {
				other = device_get_softc(devlist[i]);
				capmask &= ~other->mii_capabilities;
				break;
			}
		}
		free(devlist, M_TEMP);
	}

	PHY_RESET(sc_mii);

	sc_mii->mii_capabilities = PHY_READ(sc_mii, MII_BMSR) & capmask;

#define	ADD(m, c)							\
    ifmedia_add(&sc_mii->mii_pdata->mii_media, (m), (c), NULL)
#define	PRINT(s)	printf("%s%s", sep, s); sep = ", "

	if ((sc_mii->mii_flags & (MIIF_MACPRIV0 | MIIF_MACPRIV1)) != 0 &&
	    (sc_mii->mii_capabilities & BMSR_MEDIAMASK) != 0)
		device_printf(dev, " ");
	if ((sc_mii->mii_flags & MIIF_MACPRIV0) != 0) {
		ADD(IFM_MAKEWORD(IFM_ETHER, IFM_10_2, 0, sc_mii->mii_inst),
		    0);
		PRINT("10base2/BNC");
	}
	if ((sc_mii->mii_flags & MIIF_MACPRIV1) != 0) {
		ADD(IFM_MAKEWORD(IFM_ETHER, IFM_10_5, 0, sc_mii->mii_inst),
		    0);
		PRINT("10base5/AUI");
	}
	if ((sc_mii->mii_capabilities & BMSR_MEDIAMASK) != 0) {
		printf("%s", sep);
		mii_phy_add_media(sc_mii);
	}
	if ((sc_mii->mii_flags & (MIIF_MACPRIV0 | MIIF_MACPRIV1)) != 0 &&
	    (sc_mii->mii_capabilities & BMSR_MEDIAMASK) != 0)
		printf("\n");
#undef ADD
#undef PRINT

	MIIBUS_MEDIAINIT(sc_mii->mii_dev);
	return (0);
}

static int
tlphy_service(struct mii_softc *self, struct mii_data *mii, int cmd)
{
	struct tlphy_softc *sc = (struct tlphy_softc *)self;
	struct ifmedia_entry *ife = mii->mii_media.ifm_cur;
	int reg;

	if (sc->sc_need_acomp)
		tlphy_acomp(sc);

	switch (cmd) {
	case MII_POLLSTAT:
		break;

	case MII_MEDIACHG:
		switch (IFM_SUBTYPE(ife->ifm_media)) {
		case IFM_AUTO:
			/*
			 * The ThunderLAN PHY doesn't self-configure after
			 * an autonegotiation cycle, so there's no such
			 * thing as "already in auto mode".
			 */
			(void)tlphy_auto(sc);
			break;
		case IFM_10_2:
		case IFM_10_5:
			PHY_WRITE(&sc->sc_mii, MII_BMCR, 0);
			PHY_WRITE(&sc->sc_mii, MII_TLPHY_CTRL, CTRL_AUISEL);
			DELAY(100000);
			break;
		default:
			PHY_WRITE(&sc->sc_mii, MII_TLPHY_CTRL, 0);
			DELAY(100000);
			mii_phy_setmedia(&sc->sc_mii);
		}
		break;

	case MII_TICK:
		/*
		 * Only used for autonegotiation.
		 */
		if (IFM_SUBTYPE(ife->ifm_media) != IFM_AUTO)
			break;

		/*
		 * Check to see if we have link.  If we do, we don't
		 * need to restart the autonegotiation process.  Read
		 * the BMSR twice in case it's latched.
		 *
		 * XXX WHAT ABOUT CHECKING LINK ON THE BNC/AUI?!
		 */
		reg = PHY_READ(&sc->sc_mii, MII_BMSR) |
		    PHY_READ(&sc->sc_mii, MII_BMSR);
		if (reg & BMSR_LINK)
			break;

		/*
		 * Only retry autonegotiation every 5 seconds.
		 */
		if (++sc->sc_mii.mii_ticks <= MII_ANEGTICKS)
			break;

		sc->sc_mii.mii_ticks = 0;
		PHY_RESET(&sc->sc_mii);
		(void)tlphy_auto(sc);
		return (0);
	}

	/* Update the media status. */
	PHY_STATUS(self);

	/* Callback if something changed. */
	mii_phy_update(&sc->sc_mii, cmd);
	return (0);
}

static void
tlphy_status(struct mii_softc *self)
{
	struct tlphy_softc *sc = (struct tlphy_softc *)self;
	struct mii_data *mii = sc->sc_mii.mii_pdata;
	int bmsr, bmcr, tlctrl;

	mii->mii_media_status = IFM_AVALID;
	mii->mii_media_active = IFM_ETHER;

	bmcr = PHY_READ(&sc->sc_mii, MII_BMCR);
	if (bmcr & BMCR_ISO) {
		mii->mii_media_active |= IFM_NONE;
		mii->mii_media_status = 0;
		return;
	}

	tlctrl = PHY_READ(&sc->sc_mii, MII_TLPHY_CTRL);
	if (tlctrl & CTRL_AUISEL) {
		mii->mii_media_status = 0;
		mii->mii_media_active = mii->mii_media.ifm_cur->ifm_media;
		return;
	}

	bmsr = PHY_READ(&sc->sc_mii, MII_BMSR) |
	    PHY_READ(&sc->sc_mii, MII_BMSR);
	if (bmsr & BMSR_LINK)
		mii->mii_media_status |= IFM_ACTIVE;

	if (bmcr & BMCR_LOOP)
		mii->mii_media_active |= IFM_LOOP;

	/*
	 * Grr, braindead ThunderLAN PHY doesn't have any way to
	 * tell which media is actually active.  (Note it also
	 * doesn't self-configure after autonegotiation.)  We
	 * just have to report what's in the BMCR.
	 */
	if (bmcr & BMCR_FDX)
		mii->mii_media_active |= IFM_FDX | mii_phy_flowstatus(self);
	else
		mii->mii_media_active |= IFM_HDX;
	mii->mii_media_active |= IFM_10_T;
}

static int
tlphy_auto(struct tlphy_softc *sc)
{
	int error;

	switch ((error = mii_phy_auto(&sc->sc_mii))) {
	case EIO:
		/*
		 * Just assume we're not in full-duplex mode.
		 * XXX Check link and try AUI/BNC?
		 */
		PHY_WRITE(&sc->sc_mii, MII_BMCR, 0);
		break;

	case EJUSTRETURN:
		/* Flag that we need to program when it completes. */
		sc->sc_need_acomp = 1;
		break;

	default:
		tlphy_acomp(sc);
	}

	return (error);
}

static void
tlphy_acomp(struct tlphy_softc *sc)
{
	int aner, anlpar;

	sc->sc_need_acomp = 0;

	/*
	 * Grr, braindead ThunderLAN PHY doesn't self-configure
	 * after autonegotiation.  We have to do it ourselves
	 * based on the link partner status.
	 */

	aner = PHY_READ(&sc->sc_mii, MII_ANER);
	if (aner & ANER_LPAN) {
		anlpar = PHY_READ(&sc->sc_mii, MII_ANLPAR) &
		    PHY_READ(&sc->sc_mii, MII_ANAR);
		if (anlpar & ANAR_10_FD) {
			PHY_WRITE(&sc->sc_mii, MII_BMCR, BMCR_FDX);
			return;
		}
	}
	PHY_WRITE(&sc->sc_mii, MII_BMCR, 0);
}
