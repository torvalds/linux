/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2008, Pyun YongHyeon <yongari@FreeBSD.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice unmodified, this list of conditions, and the following
 *    disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

/*
 * Driver for the JMicron JMP211 10/100/1000, JMP202 10/100 PHY.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/socket.h>
#include <sys/bus.h>

#include <net/if.h>
#include <net/if_var.h>
#include <net/if_media.h>

#include <dev/mii/mii.h>
#include <dev/mii/miivar.h>
#include "miidevs.h"

#include <dev/mii/jmphyreg.h>

#include "miibus_if.h"

static int	jmphy_probe(device_t);
static int	jmphy_attach(device_t);
static void	jmphy_reset(struct mii_softc *);
static uint16_t	jmphy_anar(struct ifmedia_entry *);
static int	jmphy_setmedia(struct mii_softc *, struct ifmedia_entry *);

static device_method_t jmphy_methods[] = {
	/* Device interface. */
	DEVMETHOD(device_probe,		jmphy_probe),
	DEVMETHOD(device_attach,	jmphy_attach),
	DEVMETHOD(device_detach,	mii_phy_detach),
	DEVMETHOD(device_shutdown,	bus_generic_shutdown),
	DEVMETHOD_END
};

static devclass_t jmphy_devclass;
static driver_t jmphy_driver = {
	"jmphy",
	jmphy_methods,
	sizeof(struct mii_softc)
};

DRIVER_MODULE(jmphy, miibus, jmphy_driver, jmphy_devclass, 0, 0);

static int	jmphy_service(struct mii_softc *, struct mii_data *, int);
static void	jmphy_status(struct mii_softc *);

static const struct mii_phydesc jmphys[] = {
	MII_PHY_DESC(JMICRON, JMP202),
	MII_PHY_DESC(JMICRON, JMP211),
	MII_PHY_END
};

static const struct mii_phy_funcs jmphy_funcs = {
	jmphy_service,
	jmphy_status,
	jmphy_reset
};

static int
jmphy_probe(device_t dev)
{

	return (mii_phy_dev_probe(dev, jmphys, BUS_PROBE_DEFAULT));
}

static int
jmphy_attach(device_t dev)
{
	u_int flags;

	flags = 0;
	if (mii_dev_mac_match(dev, "jme") &&
	    (miibus_get_flags(dev) & MIIF_MACPRIV0) != 0)
		flags |= MIIF_PHYPRIV0;
	mii_phy_dev_attach(dev, flags, &jmphy_funcs, 1);
	return (0);
}

static int
jmphy_service(struct mii_softc *sc, struct mii_data *mii, int cmd)
{
	struct ifmedia_entry *ife = mii->mii_media.ifm_cur;

	switch (cmd) {
	case MII_POLLSTAT:
		break;

	case MII_MEDIACHG:
		if (jmphy_setmedia(sc, ife) != EJUSTRETURN)
			return (EINVAL);
		break;

	case MII_TICK:
		/*
		 * Only used for autonegotiation.
		 */
		if (IFM_SUBTYPE(ife->ifm_media) != IFM_AUTO) {
			sc->mii_ticks = 0;
			break;
		}

		/* Check for link. */
		if ((PHY_READ(sc, JMPHY_SSR) & JMPHY_SSR_LINK_UP) != 0) {
			sc->mii_ticks = 0;
			break;
		}

		/* Announce link loss right after it happens. */
		if (sc->mii_ticks++ == 0)
			break;
		if (sc->mii_ticks <= sc->mii_anegticks)
			return (0);

		sc->mii_ticks = 0;
		(void)jmphy_setmedia(sc, ife);
		break;
	}

	/* Update the media status. */
	PHY_STATUS(sc);

	/* Callback if something changed. */
	mii_phy_update(sc, cmd);
	return (0);
}

static void
jmphy_status(struct mii_softc *sc)
{
	struct mii_data *mii = sc->mii_pdata;
	int bmcr, ssr;

	mii->mii_media_status = IFM_AVALID;
	mii->mii_media_active = IFM_ETHER;

	ssr = PHY_READ(sc, JMPHY_SSR);
	if ((ssr & JMPHY_SSR_LINK_UP) != 0)
		mii->mii_media_status |= IFM_ACTIVE;

	bmcr = PHY_READ(sc, MII_BMCR);
	if ((bmcr & BMCR_ISO) != 0) {
		mii->mii_media_active |= IFM_NONE;
		mii->mii_media_status = 0;
		return;
	}

	if ((bmcr & BMCR_LOOP) != 0)
		mii->mii_media_active |= IFM_LOOP;

	if ((ssr & JMPHY_SSR_SPD_DPLX_RESOLVED) == 0) {
		/* Erg, still trying, I guess... */
		mii->mii_media_active |= IFM_NONE;
		return;
	}

	switch ((ssr & JMPHY_SSR_SPEED_MASK)) {
	case JMPHY_SSR_SPEED_1000:
		mii->mii_media_active |= IFM_1000_T;
		/*
		 * jmphy(4) got a valid link so reset mii_ticks.
		 * Resetting mii_ticks is needed in order to
		 * detect link loss after auto-negotiation.
		 */
		sc->mii_ticks = 0;
		break;
	case JMPHY_SSR_SPEED_100:
		mii->mii_media_active |= IFM_100_TX;
		sc->mii_ticks = 0;
		break;
	case JMPHY_SSR_SPEED_10:
		mii->mii_media_active |= IFM_10_T;
		sc->mii_ticks = 0;
		break;
	default:
		mii->mii_media_active |= IFM_NONE;
		return;
	}

	if ((ssr & JMPHY_SSR_DUPLEX) != 0)
		mii->mii_media_active |= IFM_FDX | mii_phy_flowstatus(sc);
	else
		mii->mii_media_active |= IFM_HDX;

	if (IFM_SUBTYPE(mii->mii_media_active) == IFM_1000_T) {
		if ((PHY_READ(sc, MII_100T2SR) & GTSR_MS_RES) != 0)
			mii->mii_media_active |= IFM_ETH_MASTER;
	}
}

static void
jmphy_reset(struct mii_softc *sc)
{
	uint16_t t2cr, val;
	int i;

	/* Disable sleep mode. */
	PHY_WRITE(sc, JMPHY_TMCTL,
	    PHY_READ(sc, JMPHY_TMCTL) & ~JMPHY_TMCTL_SLEEP_ENB);
	PHY_WRITE(sc, MII_BMCR, BMCR_RESET | BMCR_AUTOEN);

	for (i = 0; i < 1000; i++) {
		DELAY(1);
		if ((PHY_READ(sc, MII_BMCR) & BMCR_RESET) == 0)
			break;
	}
	/* Perform vendor recommended PHY calibration. */
	if ((sc->mii_flags & MIIF_PHYPRIV0) != 0) {
		/* Select PHY test mode 1. */
		t2cr = PHY_READ(sc, MII_100T2CR);
		t2cr &= ~GTCR_TEST_MASK;
		t2cr |= 0x2000;
		PHY_WRITE(sc, MII_100T2CR, t2cr);
		/* Apply calibration patch. */
		PHY_WRITE(sc, JMPHY_SPEC_ADDR, JMPHY_SPEC_ADDR_READ |
		    JMPHY_EXT_COMM_2);
		val = PHY_READ(sc, JMPHY_SPEC_DATA);
		val &= ~0x0002;
		val |= 0x0010 | 0x0001;
		PHY_WRITE(sc, JMPHY_SPEC_DATA, val);
		PHY_WRITE(sc, JMPHY_SPEC_ADDR, JMPHY_SPEC_ADDR_WRITE |
		    JMPHY_EXT_COMM_2);

		/* XXX 20ms to complete recalibration. */
		DELAY(20 * 1000);

		PHY_READ(sc, MII_100T2CR);
		PHY_WRITE(sc, JMPHY_SPEC_ADDR, JMPHY_SPEC_ADDR_READ |
		    JMPHY_EXT_COMM_2);
		val = PHY_READ(sc, JMPHY_SPEC_DATA);
		val &= ~(0x0001 | 0x0002 | 0x0010);
		PHY_WRITE(sc, JMPHY_SPEC_DATA, val);
		PHY_WRITE(sc, JMPHY_SPEC_ADDR, JMPHY_SPEC_ADDR_WRITE |
		    JMPHY_EXT_COMM_2);
		/* Disable PHY test mode. */
		PHY_READ(sc, MII_100T2CR);
		t2cr &= ~GTCR_TEST_MASK;
		PHY_WRITE(sc, MII_100T2CR, t2cr);
	}
}

static uint16_t
jmphy_anar(struct ifmedia_entry *ife)
{
	uint16_t anar;

	anar = 0;
	switch (IFM_SUBTYPE(ife->ifm_media)) {
	case IFM_AUTO:
		anar |= ANAR_TX_FD | ANAR_TX | ANAR_10_FD | ANAR_10;
		break;
	case IFM_1000_T:
		break;
	case IFM_100_TX:
		anar |= ANAR_TX | ANAR_TX_FD;
		break;
	case IFM_10_T:
		anar |= ANAR_10 | ANAR_10_FD;
		break;
	default:
		break;
	}

	return (anar);
}

static int
jmphy_setmedia(struct mii_softc *sc, struct ifmedia_entry *ife)
{
	uint16_t anar, bmcr, gig;

	gig = 0;
	bmcr = PHY_READ(sc, MII_BMCR);
	switch (IFM_SUBTYPE(ife->ifm_media)) {
	case IFM_AUTO:
		gig |= GTCR_ADV_1000TFDX | GTCR_ADV_1000THDX;
		break;
	case IFM_1000_T:
		gig |= GTCR_ADV_1000TFDX | GTCR_ADV_1000THDX;
		break;
	case IFM_100_TX:
	case IFM_10_T:
		break;
	case IFM_NONE:
		PHY_WRITE(sc, MII_BMCR, bmcr | BMCR_ISO | BMCR_PDOWN);
		return (EJUSTRETURN);
	default:
		return (EINVAL);
	}

	anar = jmphy_anar(ife);
	if ((IFM_SUBTYPE(ife->ifm_media) == IFM_AUTO ||
	    (ife->ifm_media & IFM_FDX) != 0) &&
	    ((ife->ifm_media & IFM_FLOW) != 0 ||
	    (sc->mii_flags & MIIF_FORCEPAUSE) != 0))
		anar |= ANAR_PAUSE_TOWARDS;

	if ((sc->mii_flags & MIIF_HAVE_GTCR) != 0) {
		if (IFM_SUBTYPE(ife->ifm_media) == IFM_1000_T) {
			gig |= GTCR_MAN_MS;
			if ((ife->ifm_media & IFM_ETH_MASTER) != 0)
				gig |= GTCR_ADV_MS;
		}
		PHY_WRITE(sc, MII_100T2CR, gig);
	}
	PHY_WRITE(sc, MII_ANAR, anar | ANAR_CSMA);
	PHY_WRITE(sc, MII_BMCR, bmcr | BMCR_AUTOEN | BMCR_STARTNEG);

	return (EJUSTRETURN);
}
