/*-
 * SPDX-License-Identifier: BSD-2-Clause-NetBSD
 *
 * Principal Author: Parag Patel
 * Copyright (c) 2001
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
 *
 * Additional Copyright (c) 2001 by Traakan Software under same licence.
 * Secondary Author: Matthew Jacob
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

/*
 * driver for the Marvell 88E1000 series external 1000/100/10-BT PHY.
 */

/*
 * Support added for the Marvell 88E1011 (Alaska) 1000/100/10baseTX and
 * 1000baseSX PHY.
 * Nathan Binkert <nate@openbsd.org>
 * Jung-uk Kim <jkim@niksun.com>
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

#include <dev/mii/e1000phyreg.h>

#include "miibus_if.h"

static int	e1000phy_probe(device_t);
static int	e1000phy_attach(device_t);

static device_method_t e1000phy_methods[] = {
	/* device interface */
	DEVMETHOD(device_probe,		e1000phy_probe),
	DEVMETHOD(device_attach,	e1000phy_attach),
	DEVMETHOD(device_detach,	mii_phy_detach),
	DEVMETHOD(device_shutdown,	bus_generic_shutdown),
	DEVMETHOD_END
};

static devclass_t e1000phy_devclass;
static driver_t e1000phy_driver = {
	"e1000phy",
	e1000phy_methods,
	sizeof(struct mii_softc)
};

DRIVER_MODULE(e1000phy, miibus, e1000phy_driver, e1000phy_devclass, 0, 0);

static int	e1000phy_service(struct mii_softc *, struct mii_data *, int);
static void	e1000phy_status(struct mii_softc *);
static void	e1000phy_reset(struct mii_softc *);
static int	e1000phy_mii_phy_auto(struct mii_softc *, int);

static const struct mii_phydesc e1000phys[] = {
	MII_PHY_DESC(MARVELL, E1000),
	MII_PHY_DESC(MARVELL, E1011),
	MII_PHY_DESC(MARVELL, E1000_3),
	MII_PHY_DESC(MARVELL, E1000_5),
	MII_PHY_DESC(MARVELL, E1111),
	MII_PHY_DESC(xxMARVELL, E1000),
	MII_PHY_DESC(xxMARVELL, E1011),
	MII_PHY_DESC(xxMARVELL, E1000_3),
	MII_PHY_DESC(xxMARVELL, E1000S),
	MII_PHY_DESC(xxMARVELL, E1000_5),
	MII_PHY_DESC(xxMARVELL, E1101),
	MII_PHY_DESC(xxMARVELL, E3082),
	MII_PHY_DESC(xxMARVELL, E1112),
	MII_PHY_DESC(xxMARVELL, E1149),
	MII_PHY_DESC(xxMARVELL, E1111),
	MII_PHY_DESC(xxMARVELL, E1116),
	MII_PHY_DESC(xxMARVELL, E1116R),
	MII_PHY_DESC(xxMARVELL, E1116R_29),
	MII_PHY_DESC(xxMARVELL, E1118),
	MII_PHY_DESC(xxMARVELL, E1145),
	MII_PHY_DESC(xxMARVELL, E1149R),
	MII_PHY_DESC(xxMARVELL, E3016),
	MII_PHY_DESC(xxMARVELL, PHYG65G),
	MII_PHY_END
};

static const struct mii_phy_funcs e1000phy_funcs = {
	e1000phy_service,
	e1000phy_status,
	e1000phy_reset
};

static int
e1000phy_probe(device_t	dev)
{

	return (mii_phy_dev_probe(dev, e1000phys, BUS_PROBE_DEFAULT));
}

static int
e1000phy_attach(device_t dev)
{
	struct mii_softc *sc;

	sc = device_get_softc(dev);

	mii_phy_dev_attach(dev, MIIF_NOMANPAUSE, &e1000phy_funcs, 0);

	if (mii_dev_mac_match(dev, "msk") &&
	    (sc->mii_flags & MIIF_MACPRIV0) != 0)
		sc->mii_flags |= MIIF_PHYPRIV0;

	switch (sc->mii_mpd_model) {
	case MII_MODEL_xxMARVELL_E1011:
	case MII_MODEL_xxMARVELL_E1112:
		if (PHY_READ(sc, E1000_ESSR) & E1000_ESSR_FIBER_LINK)
			sc->mii_flags |= MIIF_HAVEFIBER;
		break;
	case MII_MODEL_xxMARVELL_E1149:
	case MII_MODEL_xxMARVELL_E1149R:
		/*
		 * Some 88E1149 PHY's page select is initialized to
		 * point to other bank instead of copper/fiber bank
		 * which in turn resulted in wrong registers were
		 * accessed during PHY operation. It is believed that
		 * page 0 should be used for copper PHY so reinitialize
		 * E1000_EADR to select default copper PHY. If parent
		 * device know the type of PHY(either copper or fiber),
		 * that information should be used to select default
		 * type of PHY.
		 */
		PHY_WRITE(sc, E1000_EADR, 0);
		break;
	}

	PHY_RESET(sc);

	sc->mii_capabilities = PHY_READ(sc, MII_BMSR) & sc->mii_capmask;
	if (sc->mii_capabilities & BMSR_EXTSTAT) {
		sc->mii_extcapabilities = PHY_READ(sc, MII_EXTSR);
		if ((sc->mii_extcapabilities &
		    (EXTSR_1000TFDX | EXTSR_1000THDX)) != 0)
			sc->mii_flags |= MIIF_HAVE_GTCR;
	}
	device_printf(dev, " ");
	mii_phy_add_media(sc);
	printf("\n");

	MIIBUS_MEDIAINIT(sc->mii_dev);
	return (0);
}

static void
e1000phy_reset(struct mii_softc *sc)
{
	uint16_t reg, page;

	reg = PHY_READ(sc, E1000_SCR);
	if ((sc->mii_flags & MIIF_HAVEFIBER) != 0) {
		reg &= ~E1000_SCR_AUTO_X_MODE;
		PHY_WRITE(sc, E1000_SCR, reg);
		if (sc->mii_mpd_model == MII_MODEL_xxMARVELL_E1112) {
			/* Select 1000BASE-X only mode. */
			page = PHY_READ(sc, E1000_EADR);
			PHY_WRITE(sc, E1000_EADR, 2);
			reg = PHY_READ(sc, E1000_SCR);
			reg &= ~E1000_SCR_MODE_MASK;
			reg |= E1000_SCR_MODE_1000BX;
			PHY_WRITE(sc, E1000_SCR, reg);
			if ((sc->mii_flags & MIIF_PHYPRIV0) != 0) {
				/* Set SIGDET polarity low for SFP module. */
				PHY_WRITE(sc, E1000_EADR, 1);
				reg = PHY_READ(sc, E1000_SCR);
				reg |= E1000_SCR_FIB_SIGDET_POLARITY;
				PHY_WRITE(sc, E1000_SCR, reg);
			}
			PHY_WRITE(sc, E1000_EADR, page);
		}
	} else {
		switch (sc->mii_mpd_model) {
		case MII_MODEL_xxMARVELL_E1111:
		case MII_MODEL_xxMARVELL_E1112:
		case MII_MODEL_xxMARVELL_E1116:
		case MII_MODEL_xxMARVELL_E1116R_29:
		case MII_MODEL_xxMARVELL_E1118:
		case MII_MODEL_xxMARVELL_E1149:
		case MII_MODEL_xxMARVELL_E1149R:
		case MII_MODEL_xxMARVELL_PHYG65G:
			/* Disable energy detect mode. */
			reg &= ~E1000_SCR_EN_DETECT_MASK;
			reg |= E1000_SCR_AUTO_X_MODE;
			if (sc->mii_mpd_model == MII_MODEL_xxMARVELL_E1116 ||
			    sc->mii_mpd_model == MII_MODEL_xxMARVELL_E1116R_29)
				reg &= ~E1000_SCR_POWER_DOWN;
			reg |= E1000_SCR_ASSERT_CRS_ON_TX;
			break;
		case MII_MODEL_xxMARVELL_E3082:
			reg |= (E1000_SCR_AUTO_X_MODE >> 1);
			reg |= E1000_SCR_ASSERT_CRS_ON_TX;
			break;
		case MII_MODEL_xxMARVELL_E3016:
			reg |= E1000_SCR_AUTO_MDIX;
			reg &= ~(E1000_SCR_EN_DETECT |
			    E1000_SCR_SCRAMBLER_DISABLE);
			reg |= E1000_SCR_LPNP;
			/* XXX Enable class A driver for Yukon FE+ A0. */
			PHY_WRITE(sc, 0x1C, PHY_READ(sc, 0x1C) | 0x0001);
			break;
		default:
			reg &= ~E1000_SCR_AUTO_X_MODE;
			reg |= E1000_SCR_ASSERT_CRS_ON_TX;
			break;
		}
		if (sc->mii_mpd_model != MII_MODEL_xxMARVELL_E3016) {
			/* Auto correction for reversed cable polarity. */
			reg &= ~E1000_SCR_POLARITY_REVERSAL;
		}
		PHY_WRITE(sc, E1000_SCR, reg);

		if (sc->mii_mpd_model == MII_MODEL_xxMARVELL_E1116 ||
		    sc->mii_mpd_model == MII_MODEL_xxMARVELL_E1116R_29 ||
		    sc->mii_mpd_model == MII_MODEL_xxMARVELL_E1149 ||
		    sc->mii_mpd_model == MII_MODEL_xxMARVELL_E1149R) {
			PHY_WRITE(sc, E1000_EADR, 2);
			reg = PHY_READ(sc, E1000_SCR);
			reg |= E1000_SCR_RGMII_POWER_UP;
			PHY_WRITE(sc, E1000_SCR, reg);
			PHY_WRITE(sc, E1000_EADR, 0);
		}
	}

	switch (sc->mii_mpd_model) {
	case MII_MODEL_xxMARVELL_E3082:
	case MII_MODEL_xxMARVELL_E1112:
	case MII_MODEL_xxMARVELL_E1118:
		break;
	case MII_MODEL_xxMARVELL_E1116:
	case MII_MODEL_xxMARVELL_E1116R_29:
		page = PHY_READ(sc, E1000_EADR);
		/* Select page 3, LED control register. */
		PHY_WRITE(sc, E1000_EADR, 3);
		PHY_WRITE(sc, E1000_SCR,
		    E1000_SCR_LED_LOS(1) |	/* Link/Act */
		    E1000_SCR_LED_INIT(8) |	/* 10Mbps */
		    E1000_SCR_LED_STAT1(7) |	/* 100Mbps */
		    E1000_SCR_LED_STAT0(7));	/* 1000Mbps */
		/* Set blink rate. */
		PHY_WRITE(sc, E1000_IER, E1000_PULSE_DUR(E1000_PULSE_170MS) |
		    E1000_BLINK_RATE(E1000_BLINK_84MS));
		PHY_WRITE(sc, E1000_EADR, page);
		break;
	case MII_MODEL_xxMARVELL_E3016:
		/* LED2 -> ACT, LED1 -> LINK, LED0 -> SPEED. */
		PHY_WRITE(sc, 0x16, 0x0B << 8 | 0x05 << 4 | 0x04);
		/* Integrated register calibration workaround. */
		PHY_WRITE(sc, 0x1D, 17);
		PHY_WRITE(sc, 0x1E, 0x3F60);
		break;
	default:
		/* Force TX_CLK to 25MHz clock. */
		reg = PHY_READ(sc, E1000_ESCR);
		reg |= E1000_ESCR_TX_CLK_25;
		PHY_WRITE(sc, E1000_ESCR, reg);
		break;
	}

	/* Reset the PHY so all changes take effect. */
	reg = PHY_READ(sc, E1000_CR);
	reg |= E1000_CR_RESET;
	PHY_WRITE(sc, E1000_CR, reg);
}

static int
e1000phy_service(struct mii_softc *sc, struct mii_data *mii, int cmd)
{
	struct ifmedia_entry *ife = mii->mii_media.ifm_cur;
	uint16_t speed, gig;
	int reg;

	switch (cmd) {
	case MII_POLLSTAT:
		break;

	case MII_MEDIACHG:
		if (IFM_SUBTYPE(ife->ifm_media) == IFM_AUTO) {
			e1000phy_mii_phy_auto(sc, ife->ifm_media);
			break;
		}

		speed = 0;
		switch (IFM_SUBTYPE(ife->ifm_media)) {
		case IFM_1000_T:
			if ((sc->mii_flags & MIIF_HAVE_GTCR) == 0)
				return (EINVAL);
			speed = E1000_CR_SPEED_1000;
			break;
		case IFM_1000_SX:
			if ((sc->mii_extcapabilities &
			    (EXTSR_1000XFDX | EXTSR_1000XHDX)) == 0)
				return (EINVAL);
			speed = E1000_CR_SPEED_1000;
			break;
		case IFM_100_TX:
			speed = E1000_CR_SPEED_100;
			break;
		case IFM_10_T:
			speed = E1000_CR_SPEED_10;
			break;
		case IFM_NONE:
			reg = PHY_READ(sc, E1000_CR);
			PHY_WRITE(sc, E1000_CR,
			    reg | E1000_CR_ISOLATE | E1000_CR_POWER_DOWN);
			goto done;
		default:
			return (EINVAL);
		}

		if ((ife->ifm_media & IFM_FDX) != 0) {
			speed |= E1000_CR_FULL_DUPLEX;
			gig = E1000_1GCR_1000T_FD;
		} else
			gig = E1000_1GCR_1000T;

		reg = PHY_READ(sc, E1000_CR);
		reg &= ~E1000_CR_AUTO_NEG_ENABLE;
		PHY_WRITE(sc, E1000_CR, reg | E1000_CR_RESET);

		if (IFM_SUBTYPE(ife->ifm_media) == IFM_1000_T) {
			gig |= E1000_1GCR_MS_ENABLE;
			if ((ife->ifm_media & IFM_ETH_MASTER) != 0)
				gig |= E1000_1GCR_MS_VALUE;
		} else if ((sc->mii_flags & MIIF_HAVE_GTCR) != 0)
			gig = 0;
		PHY_WRITE(sc, E1000_1GCR, gig);
		PHY_WRITE(sc, E1000_AR, E1000_AR_SELECTOR_FIELD);
		PHY_WRITE(sc, E1000_CR, speed | E1000_CR_RESET);
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
		 * check for link.
		 * Read the status register twice; BMSR_LINK is latch-low.
		 */
		reg = PHY_READ(sc, MII_BMSR) | PHY_READ(sc, MII_BMSR);
		if (reg & BMSR_LINK) {
			sc->mii_ticks = 0;
			break;
		}

		/* Announce link loss right after it happens. */
		if (sc->mii_ticks++ == 0)
			break;
		if (sc->mii_ticks <= sc->mii_anegticks)
			break;

		sc->mii_ticks = 0;
		PHY_RESET(sc);
		e1000phy_mii_phy_auto(sc, ife->ifm_media);
		break;
	}

	/* Update the media status. */
	PHY_STATUS(sc);

	/* Callback if something changed. */
	mii_phy_update(sc, cmd);
	return (0);
}

static void
e1000phy_status(struct mii_softc *sc)
{
	struct mii_data *mii = sc->mii_pdata;
	int bmcr, bmsr, ssr;

	mii->mii_media_status = IFM_AVALID;
	mii->mii_media_active = IFM_ETHER;

	bmsr = PHY_READ(sc, E1000_SR) | PHY_READ(sc, E1000_SR);
	bmcr = PHY_READ(sc, E1000_CR);
	ssr = PHY_READ(sc, E1000_SSR);

	if (bmsr & E1000_SR_LINK_STATUS)
		mii->mii_media_status |= IFM_ACTIVE;

	if (bmcr & E1000_CR_LOOPBACK)
		mii->mii_media_active |= IFM_LOOP;

	if ((bmcr & E1000_CR_AUTO_NEG_ENABLE) != 0 &&
	    (ssr & E1000_SSR_SPD_DPLX_RESOLVED) == 0) {
		/* Erg, still trying, I guess... */
		mii->mii_media_active |= IFM_NONE;
		return;
	}

	if ((sc->mii_flags & MIIF_HAVEFIBER) == 0) {
		switch (ssr & E1000_SSR_SPEED) {
		case E1000_SSR_1000MBS:
			mii->mii_media_active |= IFM_1000_T;
			break;
		case E1000_SSR_100MBS:
			mii->mii_media_active |= IFM_100_TX;
			break;
		case E1000_SSR_10MBS:
			mii->mii_media_active |= IFM_10_T;
			break;
		default:
			mii->mii_media_active |= IFM_NONE;
			return;
		}
	} else {
		/*
		 * Some fiber PHY(88E1112) does not seem to set resolved
		 * speed so always assume we've got IFM_1000_SX.
		 */
		mii->mii_media_active |= IFM_1000_SX;
	}

	if (ssr & E1000_SSR_DUPLEX) {
		mii->mii_media_active |= IFM_FDX;
		if ((sc->mii_flags & MIIF_HAVEFIBER) == 0)
			mii->mii_media_active |= mii_phy_flowstatus(sc);
	} else
		mii->mii_media_active |= IFM_HDX;

	if (IFM_SUBTYPE(mii->mii_media_active) == IFM_1000_T) {
		if (((PHY_READ(sc, E1000_1GSR) | PHY_READ(sc, E1000_1GSR)) &
		    E1000_1GSR_MS_CONFIG_RES) != 0)
			mii->mii_media_active |= IFM_ETH_MASTER;
	}
}

static int
e1000phy_mii_phy_auto(struct mii_softc *sc, int media)
{
	uint16_t reg;

	if ((sc->mii_flags & MIIF_HAVEFIBER) == 0) {
		reg = PHY_READ(sc, E1000_AR);
		reg &= ~(E1000_AR_PAUSE | E1000_AR_ASM_DIR);
		reg |= E1000_AR_10T | E1000_AR_10T_FD |
		    E1000_AR_100TX | E1000_AR_100TX_FD;
		if ((media & IFM_FLOW) != 0 ||
		    (sc->mii_flags & MIIF_FORCEPAUSE) != 0)
			reg |= E1000_AR_PAUSE | E1000_AR_ASM_DIR;
		PHY_WRITE(sc, E1000_AR, reg | E1000_AR_SELECTOR_FIELD);
	} else
		PHY_WRITE(sc, E1000_AR, E1000_FA_1000X_FD | E1000_FA_1000X);
	if ((sc->mii_flags & MIIF_HAVE_GTCR) != 0) {
		reg = 0;
		if ((sc->mii_extcapabilities & EXTSR_1000TFDX) != 0)
			reg |= E1000_1GCR_1000T_FD;
		if ((sc->mii_extcapabilities & EXTSR_1000THDX) != 0)
			reg |= E1000_1GCR_1000T;
		PHY_WRITE(sc, E1000_1GCR, reg);
	}
	PHY_WRITE(sc, E1000_CR,
	    E1000_CR_AUTO_NEG_ENABLE | E1000_CR_RESTART_AUTO_NEG);

	return (EJUSTRETURN);
}
