/*-
 * SPDX-License-Identifier: BSD-4-Clause
 *
 * Copyright (c) 2000
 *	Bill Paul <wpaul@ee.columbia.edu>.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by Bill Paul.
 * 4. Neither the name of the author nor the names of any co-contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY Bill Paul AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL Bill Paul OR THE VOICES IN HIS HEAD
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

/*
 * driver for the XaQti XMAC II's internal PHY. This is sort of
 * like a 10/100 PHY, except the only thing we're really autoselecting
 * here is full/half duplex. Speed is always 1000mbps.
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

#include <dev/mii/xmphyreg.h>

#include "miibus_if.h"

static int xmphy_probe(device_t);
static int xmphy_attach(device_t);

static device_method_t xmphy_methods[] = {
	/* device interface */
	DEVMETHOD(device_probe,		xmphy_probe),
	DEVMETHOD(device_attach,	xmphy_attach),
	DEVMETHOD(device_detach,	mii_phy_detach),
	DEVMETHOD(device_shutdown,	bus_generic_shutdown),
	DEVMETHOD_END
};

static devclass_t xmphy_devclass;

static driver_t xmphy_driver = {
	"xmphy",
	xmphy_methods,
	sizeof(struct mii_softc)
};

DRIVER_MODULE(xmphy, miibus, xmphy_driver, xmphy_devclass, 0, 0);

static int	xmphy_service(struct mii_softc *, struct mii_data *, int);
static void	xmphy_status(struct mii_softc *);
static int	xmphy_mii_phy_auto(struct mii_softc *);

static const struct mii_phydesc xmphys[] = {
	MII_PHY_DESC(xxJATO, BASEX),
	MII_PHY_DESC(xxXAQTI, XMACII),
	MII_PHY_END
};

static const struct mii_phy_funcs xmphy_funcs = {
	xmphy_service,
	xmphy_status,
	mii_phy_reset
};

static int
xmphy_probe(device_t dev)
{

	return (mii_phy_dev_probe(dev, xmphys, BUS_PROBE_DEFAULT));
}

static int
xmphy_attach(device_t dev)
{
	struct mii_softc *sc;
	const char *sep = "";

	sc = device_get_softc(dev);

	mii_phy_dev_attach(dev, MIIF_NOISOLATE | MIIF_NOMANPAUSE,
	    &xmphy_funcs, 0);
	sc->mii_anegticks = MII_ANEGTICKS;

	PHY_RESET(sc);

#define	ADD(m)		ifmedia_add(&sc->mii_pdata->mii_media, (m), 0, NULL)
#define PRINT(s)	printf("%s%s", sep, s); sep = ", "

	device_printf(dev, " ");
	ADD(IFM_MAKEWORD(IFM_ETHER, IFM_1000_SX, 0, sc->mii_inst));
	PRINT("1000baseSX");
	ADD(IFM_MAKEWORD(IFM_ETHER, IFM_1000_SX, IFM_FDX, sc->mii_inst));
	PRINT("1000baseSX-FDX");
	ADD(IFM_MAKEWORD(IFM_ETHER, IFM_AUTO, 0, sc->mii_inst));
	PRINT("auto");

	printf("\n");

#undef ADD
#undef PRINT

	MIIBUS_MEDIAINIT(sc->mii_dev);
	return (0);
}

static int
xmphy_service(struct mii_softc *sc, struct mii_data *mii, int cmd)
{
	struct ifmedia_entry *ife = mii->mii_media.ifm_cur;
	int reg;

	switch (cmd) {
	case MII_POLLSTAT:
		break;

	case MII_MEDIACHG:
		switch (IFM_SUBTYPE(ife->ifm_media)) {
		case IFM_AUTO:
#ifdef foo
			/*
			 * If we're already in auto mode, just return.
			 */
			if (PHY_READ(sc, XMPHY_MII_BMCR) & XMPHY_BMCR_AUTOEN)
				return (0);
#endif
			(void)xmphy_mii_phy_auto(sc);
			break;
		case IFM_1000_SX:
			PHY_RESET(sc);
			if ((ife->ifm_media & IFM_FDX) != 0) {
				PHY_WRITE(sc, XMPHY_MII_ANAR, XMPHY_ANAR_FDX);
				PHY_WRITE(sc, XMPHY_MII_BMCR, XMPHY_BMCR_FDX);
			} else {
				PHY_WRITE(sc, XMPHY_MII_ANAR, XMPHY_ANAR_HDX);
				PHY_WRITE(sc, XMPHY_MII_BMCR, 0);
			}
			break;
		default:
			return (EINVAL);
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
		 */
		reg = PHY_READ(sc, MII_BMSR) | PHY_READ(sc, MII_BMSR);
		if (reg & BMSR_LINK)
			break;

		/* Only retry autonegotiation every mii_anegticks seconds. */
		if (sc->mii_ticks <= sc->mii_anegticks)
			break;

		sc->mii_ticks = 0;

		PHY_RESET(sc);
		xmphy_mii_phy_auto(sc);
		return (0);
	}

	/* Update the media status. */
	xmphy_status(sc);

	/* Callback if something changed. */
	mii_phy_update(sc, cmd);
	return (0);
}

static void
xmphy_status(struct mii_softc *sc)
{
	struct mii_data *mii = sc->mii_pdata;
	int bmsr, bmcr, anlpar;

	mii->mii_media_status = IFM_AVALID;
	mii->mii_media_active = IFM_ETHER;

	bmsr = PHY_READ(sc, XMPHY_MII_BMSR) |
	    PHY_READ(sc, XMPHY_MII_BMSR);
	if (bmsr & XMPHY_BMSR_LINK)
		mii->mii_media_status |= IFM_ACTIVE;

	/* Do dummy read of extended status register. */
	bmcr = PHY_READ(sc, XMPHY_MII_EXTSTS);

	bmcr = PHY_READ(sc, XMPHY_MII_BMCR);

	if (bmcr & XMPHY_BMCR_LOOP)
		mii->mii_media_active |= IFM_LOOP;

	if (bmcr & XMPHY_BMCR_AUTOEN) {
		if ((bmsr & XMPHY_BMSR_ACOMP) == 0) {
			if (bmsr & XMPHY_BMSR_LINK) {
				mii->mii_media_active |= IFM_1000_SX|IFM_HDX;
				return;
			}
			/* Erg, still trying, I guess... */
			mii->mii_media_active |= IFM_NONE;
			return;
		}

		mii->mii_media_active |= IFM_1000_SX;
		anlpar = PHY_READ(sc, XMPHY_MII_ANAR) &
		    PHY_READ(sc, XMPHY_MII_ANLPAR);
		if (anlpar & XMPHY_ANLPAR_FDX)
			mii->mii_media_active |= IFM_FDX;
		else
			mii->mii_media_active |= IFM_HDX;
		return;
	}

	mii->mii_media_active |= IFM_1000_SX;
	if (bmcr & XMPHY_BMCR_FDX)
		mii->mii_media_active |= IFM_FDX;
	else
		mii->mii_media_active |= IFM_HDX;
}

static int
xmphy_mii_phy_auto(struct mii_softc *mii)
{
	int anar = 0;

	anar = PHY_READ(mii, XMPHY_MII_ANAR);
	anar |= XMPHY_ANAR_FDX|XMPHY_ANAR_HDX;
	PHY_WRITE(mii, XMPHY_MII_ANAR, anar);
	DELAY(1000);
	PHY_WRITE(mii, XMPHY_MII_BMCR,
	    XMPHY_BMCR_AUTOEN | XMPHY_BMCR_STARTNEG);

	return (EJUSTRETURN);
}
