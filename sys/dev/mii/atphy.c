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
 * Driver for the Attansic/Atheros F1 10/100/1000 PHY.
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

#include <dev/mii/atphyreg.h>

#include "miibus_if.h"

static int atphy_probe(device_t);
static int atphy_attach(device_t);

static device_method_t atphy_methods[] = {
	/* Device interface. */
	DEVMETHOD(device_probe,		atphy_probe),
	DEVMETHOD(device_attach,	atphy_attach),
	DEVMETHOD(device_detach,	mii_phy_detach),
	DEVMETHOD(device_shutdown,	bus_generic_shutdown),
	DEVMETHOD_END
};

static devclass_t atphy_devclass;
static driver_t atphy_driver = {
	"atphy",
	atphy_methods,
	sizeof(struct mii_softc)
};

DRIVER_MODULE(atphy, miibus, atphy_driver, atphy_devclass, 0, 0);

static int	atphy_service(struct mii_softc *, struct mii_data *, int);
static void	atphy_status(struct mii_softc *);
static void	atphy_reset(struct mii_softc *);
static uint16_t	atphy_anar(struct ifmedia_entry *);
static int	atphy_setmedia(struct mii_softc *, int);

static const struct mii_phydesc atphys[] = {
	MII_PHY_DESC(xxATHEROS, F1),
	MII_PHY_DESC(xxATHEROS, F1_7),
	MII_PHY_DESC(xxATHEROS, AR8021),
	MII_PHY_DESC(xxATHEROS, F2),
	MII_PHY_END
};

static const struct mii_phy_funcs atphy_funcs = {
	atphy_service,
	atphy_status,
	atphy_reset
};

static int
atphy_probe(device_t dev)
{

	return (mii_phy_dev_probe(dev, atphys, BUS_PROBE_DEFAULT));
}

static int
atphy_attach(device_t dev)
{

	mii_phy_dev_attach(dev, MIIF_NOMANPAUSE, &atphy_funcs, 1);
	return (0);
}

static int
atphy_service(struct mii_softc *sc, struct mii_data *mii, int cmd)
{
	struct ifmedia_entry *ife = mii->mii_media.ifm_cur;
	uint16_t anar, bmcr, bmsr;

	switch (cmd) {
	case MII_POLLSTAT:
		break;

	case MII_MEDIACHG:
		if (IFM_SUBTYPE(ife->ifm_media) == IFM_AUTO ||
		    IFM_SUBTYPE(ife->ifm_media) == IFM_1000_T) {
			atphy_setmedia(sc, ife->ifm_media);
			break;
		}

		bmcr = 0;
		switch (IFM_SUBTYPE(ife->ifm_media)) {
		case IFM_100_TX:
			bmcr = BMCR_S100;
			break;
		case IFM_10_T:
			bmcr = BMCR_S10;
			break;
		case IFM_NONE:
			bmcr = PHY_READ(sc, MII_BMCR);
			/*
			 * XXX
			 * Due to an unknown reason powering down PHY resulted
			 * in unexpected results such as inaccessibility of
			 * hardware of freshly rebooted system. Disable
			 * powering down PHY until I got more information for
			 * Attansic/Atheros PHY hardwares.
			 */
			PHY_WRITE(sc, MII_BMCR, bmcr | BMCR_ISO);
			goto done;
		default:
			return (EINVAL);
		}

		anar = atphy_anar(ife);
		if ((ife->ifm_media & IFM_FDX) != 0) {
			bmcr |= BMCR_FDX;
			if ((ife->ifm_media & IFM_FLOW) != 0 ||
			    (sc->mii_flags & MIIF_FORCEPAUSE) != 0)
				anar |= ANAR_PAUSE_TOWARDS;
		}

		if ((sc->mii_extcapabilities & (EXTSR_1000TFDX |
		    EXTSR_1000THDX)) != 0)
			PHY_WRITE(sc, MII_100T2CR, 0);
		PHY_WRITE(sc, MII_ANAR, anar | ANAR_CSMA);

		/*
		 * Reset the PHY so all changes take effect.
		 */
		PHY_WRITE(sc, MII_BMCR, bmcr | BMCR_RESET | BMCR_AUTOEN |
		    BMCR_STARTNEG);
done:
		break;

	case MII_TICK:
		/*
		 * Only used for autonegotiation.
		 */
		if (IFM_SUBTYPE(ife->ifm_media) != IFM_AUTO) {
			sc->mii_ticks = 0;
			break;
		}

		/*
		 * Check for link.
		 * Read the status register twice; BMSR_LINK is latch-low.
		 */
		bmsr = PHY_READ(sc, MII_BMSR) | PHY_READ(sc, MII_BMSR);
		if (bmsr & BMSR_LINK) {
			sc->mii_ticks = 0;
			break;
		}

		/* Announce link loss right after it happens. */
		if (sc->mii_ticks++ == 0)
			break;
		if (sc->mii_ticks <= sc->mii_anegticks)
			return (0);

		sc->mii_ticks = 0;
		atphy_setmedia(sc, ife->ifm_media);
		break;
	}

	/* Update the media status. */
	PHY_STATUS(sc);

	/* Callback if something changed. */
	mii_phy_update(sc, cmd);
	return (0);
}

static void
atphy_status(struct mii_softc *sc)
{
	struct mii_data *mii = sc->mii_pdata;
	uint32_t bmsr, bmcr, ssr;

	mii->mii_media_status = IFM_AVALID;
	mii->mii_media_active = IFM_ETHER;

	bmsr = PHY_READ(sc, MII_BMSR) | PHY_READ(sc, MII_BMSR);
	if ((bmsr & BMSR_LINK) != 0)
		mii->mii_media_status |= IFM_ACTIVE;

	bmcr = PHY_READ(sc, MII_BMCR);
	if ((bmcr & BMCR_ISO) != 0) {
		mii->mii_media_active |= IFM_NONE;
		mii->mii_media_status = 0;
		return;
	}

	if ((bmcr & BMCR_LOOP) != 0)
		mii->mii_media_active |= IFM_LOOP;

	ssr = PHY_READ(sc, ATPHY_SSR);
	if ((ssr & ATPHY_SSR_SPD_DPLX_RESOLVED) == 0) {
		/* Erg, still trying, I guess... */
		mii->mii_media_active |= IFM_NONE;
		return;
	}

	switch (ssr & ATPHY_SSR_SPEED_MASK) {
	case ATPHY_SSR_1000MBS:
		mii->mii_media_active |= IFM_1000_T;
		/*
		 * atphy(4) has a valid link so reset mii_ticks.
		 * Resetting mii_ticks is needed in order to
		 * detect link loss after auto-negotiation.
		 */
		sc->mii_ticks = 0;
		break;
	case ATPHY_SSR_100MBS:
		mii->mii_media_active |= IFM_100_TX;
		sc->mii_ticks = 0;
		break;
	case ATPHY_SSR_10MBS:
		mii->mii_media_active |= IFM_10_T;
		sc->mii_ticks = 0;
		break;
	default:
		mii->mii_media_active |= IFM_NONE;
		return;
	}

	if ((ssr & ATPHY_SSR_DUPLEX) != 0)
		mii->mii_media_active |= IFM_FDX | mii_phy_flowstatus(sc);
	else
		mii->mii_media_active |= IFM_HDX;
		
	if ((IFM_SUBTYPE(mii->mii_media_active) == IFM_1000_T) &&
	    (PHY_READ(sc, MII_100T2SR) & GTSR_MS_RES) != 0)
		mii->mii_media_active |= IFM_ETH_MASTER;
}

static void
atphy_reset(struct mii_softc *sc)
{
	struct ifmedia_entry *ife = sc->mii_pdata->mii_media.ifm_cur;
	uint32_t reg;
	int i;

	/* Take PHY out of power down mode. */
	PHY_WRITE(sc, 29, 0x29);
	PHY_WRITE(sc, 30, 0);

	reg = PHY_READ(sc, ATPHY_SCR);
	/* Enable automatic crossover. */
	reg |= ATPHY_SCR_AUTO_X_MODE;
	/* Disable power down. */
	reg &= ~ATPHY_SCR_MAC_PDOWN;
	/* Enable CRS on Tx. */
	reg |= ATPHY_SCR_ASSERT_CRS_ON_TX;
	/* Auto correction for reversed cable polarity. */
	reg |= ATPHY_SCR_POLARITY_REVERSAL;
	PHY_WRITE(sc, ATPHY_SCR, reg);

	/* Workaround F1 bug to reset phy. */
	atphy_setmedia(sc, ife == NULL ? IFM_AUTO : ife->ifm_media);

	for (i = 0; i < 1000; i++) {
		DELAY(1);
		if ((PHY_READ(sc, MII_BMCR) & BMCR_RESET) == 0)
			break;
	}
}

static uint16_t
atphy_anar(struct ifmedia_entry *ife)
{
	uint16_t anar;

	anar = 0;
	switch (IFM_SUBTYPE(ife->ifm_media)) {
	case IFM_AUTO:
		anar |= ANAR_TX_FD | ANAR_TX | ANAR_10_FD | ANAR_10;
		return (anar);
	case IFM_1000_T:
		return (anar);
	case IFM_100_TX:
		anar |= ANAR_TX;
		break;
	case IFM_10_T:
		anar |= ANAR_10;
		break;
	default:
		return (0);
	}

	if ((ife->ifm_media & IFM_FDX) != 0) {
		if (IFM_SUBTYPE(ife->ifm_media) == IFM_100_TX)
			anar |= ANAR_TX_FD;
		else
			anar |= ANAR_10_FD;
	}

	return (anar);
}

static int
atphy_setmedia(struct mii_softc *sc, int media)
{
	uint16_t anar;

	anar = BMSR_MEDIA_TO_ANAR(sc->mii_capabilities) | ANAR_CSMA;
	if ((IFM_SUBTYPE(media) == IFM_AUTO || (media & IFM_FDX) != 0) &&
	    ((media & IFM_FLOW) != 0 ||
	    (sc->mii_flags & MIIF_FORCEPAUSE) != 0))
		anar |= ANAR_PAUSE_TOWARDS;
	PHY_WRITE(sc, MII_ANAR, anar);
	if ((sc->mii_extcapabilities &
	     (EXTSR_1000TFDX | EXTSR_1000THDX)) != 0)
		PHY_WRITE(sc, MII_100T2CR, GTCR_ADV_1000TFDX |
		    GTCR_ADV_1000THDX);
	else if (sc->mii_mpd_model == MII_MODEL_xxATHEROS_F1) {
		/*
		 * AR8132 has 10/100 PHY and the PHY uses the same
		 * model number of F1 gigabit PHY.  The PHY has no
		 * ability to establish gigabit link so explicitly
		 * disable 1000baseT configuration for the PHY.
		 * Otherwise, there is a case that atphy(4) could
		 * not establish a link against gigabit link partner
		 * unless the link partner supports down-shifting.
		 */
		PHY_WRITE(sc, MII_100T2CR, 0);
	}
	PHY_WRITE(sc, MII_BMCR, BMCR_RESET | BMCR_AUTOEN | BMCR_STARTNEG);

	return (EJUSTRETURN);
}
