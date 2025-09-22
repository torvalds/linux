/*	$OpenBSD: atphy.c,v 1.13 2022/04/06 18:59:29 naddy Exp $	*/

/*-
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

/*
 * Driver for the Attansic F1 10/100/1000 PHY.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/device.h>
#include <sys/socket.h>

#include <net/if.h>
#include <net/if_var.h>
#include <net/if_media.h>

#include <dev/mii/mii.h>
#include <dev/mii/miivar.h>
#include <dev/mii/miidevs.h>

/* Special Control Register */
#define ATPHY_SCR			0x10
#define ATPHY_SCR_JABBER_DISABLE	0x0001
#define ATPHY_SCR_POLARITY_REVERSAL	0x0002
#define ATPHY_SCR_SQE_TEST		0x0004
#define ATPHY_SCR_MAC_PDOWN		0x0008
#define ATPHY_SCR_CLK125_DISABLE	0x0010
#define ATPHY_SCR_MDI_MANUAL_MODE	0x0000
#define ATPHY_SCR_MDIX_MANUAL_MODE	0x0020
#define ATPHY_SCR_AUTO_X_1000T		0x0040
#define ATPHY_SCR_AUTO_X_MODE		0x0060
#define ATPHY_SCR_10BT_EXT_ENABLE	0x0080
#define ATPHY_SCR_MII_5BIT_ENABLE	0x0100
#define ATPHY_SCR_SCRAMBLER_DISABLE	0x0200
#define ATPHY_SCR_FORCE_LINK_GOOD	0x0400
#define ATPHY_SCR_ASSERT_CRS_ON_TX	0x0800

/* Special Status Register. */
#define ATPHY_SSR			0x11
#define ATPHY_SSR_SPD_DPLX_RESOLVED	0x0800
#define ATPHY_SSR_DUPLEX		0x2000
#define ATPHY_SSR_SPEED_MASK		0xC000
#define ATPHY_SSR_10MBS			0x0000
#define ATPHY_SSR_100MBS		0x4000
#define ATPHY_SSR_1000MBS		0x8000

int	atphy_service(struct mii_softc *, struct mii_data *, int);
void	atphy_attach(struct device *, struct device *, void *);
int	atphy_match(struct device *, void *, void *);
void	atphy_reset(struct mii_softc *);
void	atphy_status(struct mii_softc *);
int	atphy_mii_phy_auto(struct mii_softc *);

const struct mii_phy_funcs atphy_funcs = {
        atphy_service, atphy_status, atphy_reset,
};

static const struct mii_phydesc atphys[] = {
	{ MII_OUI_ATHEROS,	MII_MODEL_ATHEROS_F1,
	  MII_STR_ATHEROS_F1 },
	{ MII_OUI_ATHEROS,	MII_MODEL_ATHEROS_F2,
	  MII_STR_ATHEROS_F2 },
	{ MII_OUI_ATHEROS,	MII_MODEL_ATHEROS_AR8035,
	  MII_STR_ATHEROS_AR8035 },
	{ 0,			0,
	  NULL },
};

const struct cfattach atphy_ca = {
	sizeof (struct mii_softc), atphy_match, atphy_attach,
	mii_phy_detach
};

struct cfdriver atphy_cd = {
	NULL, "atphy", DV_DULL
};

int
atphy_match(struct device *parent, void *match, void *aux)
{
	struct mii_attach_args *ma = aux;

	if (mii_phy_match(ma, atphys) != NULL)
		return (10);

	return (0);
}

void
atphy_attach(struct device *parent, struct device *self, void *aux)
{
	struct mii_softc *sc = (struct mii_softc *)self;
	struct mii_attach_args *ma = aux;
	struct mii_data *mii = ma->mii_data;
	const struct mii_phydesc *mpd;

	mpd = mii_phy_match(ma, atphys);
	printf(": %s, rev. %d\n", mpd->mpd_name, MII_REV(ma->mii_id2));

	sc->mii_inst = mii->mii_instance;
	sc->mii_phy = ma->mii_phyno;
	sc->mii_funcs = &atphy_funcs;
	sc->mii_oui = MII_OUI(ma->mii_id1, ma->mii_id2);
	sc->mii_model = MII_MODEL(ma->mii_id2);
	sc->mii_pdata = mii;
	sc->mii_flags = ma->mii_flags;
	sc->mii_anegticks = MII_ANEGTICKS_GIGE;

	sc->mii_flags |= MIIF_NOLOOP;

	PHY_RESET(sc);

	sc->mii_capabilities = PHY_READ(sc, MII_BMSR) & ma->mii_capmask;
	if (sc->mii_capabilities & BMSR_EXTSTAT)
		sc->mii_extcapabilities = PHY_READ(sc, MII_EXTSR);

	mii_phy_add_media(sc);
}

int
atphy_service(struct mii_softc *sc, struct mii_data *mii, int cmd)
{
	struct ifmedia_entry *ife = mii->mii_media.ifm_cur;
	uint16_t anar, bmcr, bmsr;

	switch (cmd) {
	case MII_POLLSTAT:
		/*
		 * If we're not polling our PHY instance, just return.
		 */
		if (IFM_INST(ife->ifm_media) != sc->mii_inst)
			return (0);
		break;

	case MII_MEDIACHG:
		/*
		 * If the media indicates a different PHY instance,
		 * isolate ourselves.
		 */
		if (IFM_INST(ife->ifm_media) != sc->mii_inst) {
			bmcr = PHY_READ(sc, MII_BMCR);
			PHY_WRITE(sc, MII_BMCR, bmcr | BMCR_ISO);
			return (0);
		}

		/*
		 * If the interface is not up, don't do anything.
		 */
		if ((mii->mii_ifp->if_flags & IFF_UP) == 0)
			break;

		bmcr = 0;
		switch (IFM_SUBTYPE(ife->ifm_media)) {
		case IFM_AUTO:
		case IFM_1000_T:
			atphy_mii_phy_auto(sc);
			goto done;
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
			 * powering down PHY until I get more information for
			 * Attansic/Atheros PHY hardware.
			 */
			PHY_WRITE(sc, MII_BMCR, bmcr | BMCR_ISO);
			goto done;
		default:
			return (EINVAL);
		}

		anar = mii_anar(ife->ifm_media);
		if (((ife->ifm_media & IFM_GMASK) & IFM_FDX) != 0) {
			bmcr |= BMCR_FDX;
			/* Enable pause. */
			if (sc->mii_flags & MIIF_DOPAUSE)
				anar |= ANAR_PAUSE_TOWARDS;
		}

		if ((sc->mii_extcapabilities & (EXTSR_1000TFDX |
		    EXTSR_1000THDX)) != 0)
			PHY_WRITE(sc, MII_100T2CR, 0);
		PHY_WRITE(sc, MII_ANAR, anar);

		/*
		 * Reset the PHY so all changes take effect.
		 */
		PHY_WRITE(sc, MII_BMCR, bmcr | BMCR_RESET | BMCR_AUTOEN |
		    BMCR_STARTNEG);
done:
		break;

	case MII_TICK:
		/*
		 * If we're not currently selected, just return.
		 */
		if (IFM_INST(ife->ifm_media) != sc->mii_inst)
			return (0);

		/*
		 * Is the interface even up?
		 */
		if ((mii->mii_ifp->if_flags & IFF_UP) == 0)
			return (0);

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

		/*
		 * Only retry autonegotiation every mii_anegticks seconds.
		 */
		if (sc->mii_ticks <= sc->mii_anegticks)
			return (0);

		sc->mii_ticks = 0;
		atphy_mii_phy_auto(sc);
		break;
	}

	/* Update the media status. */
	mii_phy_status(sc);

	/* Callback if something changed. */
	mii_phy_update(sc, cmd);
	return (0);
}

void
atphy_status(struct mii_softc *sc)
{
	struct mii_data *mii = sc->mii_pdata;
	uint32_t bmsr, bmcr, gsr, ssr;

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

	ssr = PHY_READ(sc, ATPHY_SSR);
	if (!(ssr & ATPHY_SSR_SPD_DPLX_RESOLVED)) {
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

	if (ssr & ATPHY_SSR_DUPLEX)
		mii->mii_media_active |= IFM_FDX | mii_phy_flowstatus(sc);
	else
		mii->mii_media_active |= IFM_HDX;

	gsr = PHY_READ(sc, MII_100T2SR);
	if ((IFM_SUBTYPE(mii->mii_media_active) == IFM_1000_T) &&
	    gsr & GTSR_MS_RES)
		mii->mii_media_active |= IFM_ETH_MASTER;
}

void
atphy_reset(struct mii_softc *sc)
{
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
	atphy_mii_phy_auto(sc);

	for (i = 0; i < 1000; i++) {
		DELAY(1);
		if ((PHY_READ(sc, MII_BMCR) & BMCR_RESET) == 0)
			break;
	}
}

int
atphy_mii_phy_auto(struct mii_softc *sc)
{
	uint16_t anar;

	anar = BMSR_MEDIA_TO_ANAR(sc->mii_capabilities) | ANAR_CSMA;
	if (sc->mii_flags & MIIF_DOPAUSE)
		anar |= ANAR_PAUSE_TOWARDS;
	PHY_WRITE(sc, MII_ANAR, anar);
	if (sc->mii_extcapabilities & (EXTSR_1000TFDX | EXTSR_1000THDX))
		PHY_WRITE(sc, MII_100T2CR, GTCR_ADV_1000TFDX |
		    GTCR_ADV_1000THDX);
	else if (sc->mii_model == MII_MODEL_ATHEROS_F1) {
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
