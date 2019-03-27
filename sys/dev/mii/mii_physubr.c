/*	$NetBSD: mii_physubr.c,v 1.5 1999/08/03 19:41:49 drochner Exp $	*/

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

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

/*
 * Subroutines common to all PHYs.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/socket.h>
#include <sys/errno.h>
#include <sys/module.h>
#include <sys/bus.h>

#include <net/if.h>
#include <net/if_media.h>

#include <dev/mii/mii.h>
#include <dev/mii/miivar.h>

#include "miibus_if.h"

/*
 *
 * An array of structures to map MII media types to BMCR/ANAR settings.
 */
enum { 
	MII_MEDIA_NONE = 0,
	MII_MEDIA_10_T,
	MII_MEDIA_10_T_FDX,
	MII_MEDIA_100_T4,
	MII_MEDIA_100_TX,
	MII_MEDIA_100_TX_FDX,
	MII_MEDIA_1000_X,
	MII_MEDIA_1000_X_FDX,
	MII_MEDIA_1000_T,
	MII_MEDIA_1000_T_FDX,
	MII_NMEDIA,
};

static const struct mii_media {
	u_int	mm_bmcr;		/* BMCR settings for this media */
	u_int	mm_anar;		/* ANAR settings for this media */
	u_int	mm_gtcr;		/* 100base-T2 or 1000base-T CR */
} mii_media_table[MII_NMEDIA] = {
	/* None */
	{ BMCR_ISO,		ANAR_CSMA,
	  0, },

	/* 10baseT */
	{ BMCR_S10,		ANAR_CSMA|ANAR_10,
	  0, },

	/* 10baseT-FDX */
	{ BMCR_S10|BMCR_FDX,	ANAR_CSMA|ANAR_10_FD,
	  0, },

	/* 100baseT4 */
	{ BMCR_S100,		ANAR_CSMA|ANAR_T4,
	  0, },

	/* 100baseTX */
	{ BMCR_S100,		ANAR_CSMA|ANAR_TX,
	  0, },

	/* 100baseTX-FDX */
	{ BMCR_S100|BMCR_FDX,	ANAR_CSMA|ANAR_TX_FD,
	  0, },

	/* 1000baseX */
	{ BMCR_S1000,		ANAR_CSMA,
	  0, },

	/* 1000baseX-FDX */
	{ BMCR_S1000|BMCR_FDX,	ANAR_CSMA,
	  0, },

	/* 1000baseT */
	{ BMCR_S1000,		ANAR_CSMA,
	  GTCR_ADV_1000THDX },

	/* 1000baseT-FDX */
	{ BMCR_S1000,		ANAR_CSMA,
	  GTCR_ADV_1000TFDX },
};

void
mii_phy_setmedia(struct mii_softc *sc)
{
	struct mii_data *mii = sc->mii_pdata;
	struct ifmedia_entry *ife = mii->mii_media.ifm_cur;
	int bmcr, anar, gtcr;
	int index = -1;

	switch (IFM_SUBTYPE(ife->ifm_media)) {
	case IFM_AUTO:
		/*
		 * Force renegotiation if MIIF_DOPAUSE or MIIF_FORCEANEG.
		 * The former is necessary as we might switch from flow-
		 * control advertisement being off to on or vice versa.
		 */
		if ((PHY_READ(sc, MII_BMCR) & BMCR_AUTOEN) == 0 ||
		    (sc->mii_flags & (MIIF_DOPAUSE | MIIF_FORCEANEG)) != 0)
			(void)mii_phy_auto(sc);
		return;

	case IFM_NONE:
		index = MII_MEDIA_NONE;
		break;

	case IFM_HPNA_1:
		index = MII_MEDIA_10_T;
		break;

	case IFM_10_T:
		switch (IFM_OPTIONS(ife->ifm_media)) {
		case 0:
			index = MII_MEDIA_10_T;
			break;
		case IFM_FDX:
		case (IFM_FDX | IFM_FLOW):
			index = MII_MEDIA_10_T_FDX;
			break;
		}
		break;

	case IFM_100_TX:
	case IFM_100_FX:
		switch (IFM_OPTIONS(ife->ifm_media)) {
		case 0:
			index = MII_MEDIA_100_TX;
			break;
		case IFM_FDX:
		case (IFM_FDX | IFM_FLOW):
			index = MII_MEDIA_100_TX_FDX;
			break;
		}
		break;

	case IFM_100_T4:
		index = MII_MEDIA_100_T4;
		break;

	case IFM_1000_SX:
		switch (IFM_OPTIONS(ife->ifm_media)) {
		case 0:
			index = MII_MEDIA_1000_X;
			break;
		case IFM_FDX:
		case (IFM_FDX | IFM_FLOW):
			index = MII_MEDIA_1000_X_FDX;
			break;
		}
		break;

	case IFM_1000_T:
		switch (IFM_OPTIONS(ife->ifm_media)) {
		case 0:
		case IFM_ETH_MASTER:
			index = MII_MEDIA_1000_T;
			break;
		case IFM_FDX:
		case (IFM_FDX | IFM_ETH_MASTER):
		case (IFM_FDX | IFM_FLOW):
		case (IFM_FDX | IFM_FLOW | IFM_ETH_MASTER):
			index = MII_MEDIA_1000_T_FDX;
			break;
		}
		break;
	}

	KASSERT(index != -1, ("%s: failed to map media word %d",
	    __func__, ife->ifm_media));

	anar = mii_media_table[index].mm_anar;
	bmcr = mii_media_table[index].mm_bmcr;
	gtcr = mii_media_table[index].mm_gtcr;

	if (IFM_SUBTYPE(ife->ifm_media) == IFM_1000_T) {
		gtcr |= GTCR_MAN_MS;
		if ((ife->ifm_media & IFM_ETH_MASTER) != 0)
			gtcr |= GTCR_ADV_MS;
	}

	if ((ife->ifm_media & IFM_FDX) != 0 &&
	    ((ife->ifm_media & IFM_FLOW) != 0 ||
	    (sc->mii_flags & MIIF_FORCEPAUSE) != 0)) {
		if ((sc->mii_flags & MIIF_IS_1000X) != 0)
			anar |= ANAR_X_PAUSE_TOWARDS;
		else {
			anar |= ANAR_FC;
			/* XXX Only 1000BASE-T has PAUSE_ASYM? */
			if ((sc->mii_flags & MIIF_HAVE_GTCR) != 0 &&
			    (sc->mii_extcapabilities &
			    (EXTSR_1000THDX | EXTSR_1000TFDX)) != 0)
				anar |= ANAR_X_PAUSE_ASYM;
		}
	}

	PHY_WRITE(sc, MII_ANAR, anar);
	PHY_WRITE(sc, MII_BMCR, bmcr);
	if ((sc->mii_flags & MIIF_HAVE_GTCR) != 0)
		PHY_WRITE(sc, MII_100T2CR, gtcr);
}

int
mii_phy_auto(struct mii_softc *sc)
{
	struct ifmedia_entry *ife = sc->mii_pdata->mii_media.ifm_cur;
	int anar, gtcr;

	/*
	 * Check for 1000BASE-X.  Autonegotiation is a bit
	 * different on such devices.
	 */
	if ((sc->mii_flags & MIIF_IS_1000X) != 0) {
		anar = 0;
		if ((sc->mii_extcapabilities & EXTSR_1000XFDX) != 0)
			anar |= ANAR_X_FD;
		if ((sc->mii_extcapabilities & EXTSR_1000XHDX) != 0)
			anar |= ANAR_X_HD;

		if ((ife->ifm_media & IFM_FLOW) != 0 ||
		    (sc->mii_flags & MIIF_FORCEPAUSE) != 0)
			anar |= ANAR_X_PAUSE_TOWARDS;
		PHY_WRITE(sc, MII_ANAR, anar);
	} else {
		anar = BMSR_MEDIA_TO_ANAR(sc->mii_capabilities) |
		    ANAR_CSMA;
		if ((ife->ifm_media & IFM_FLOW) != 0 ||
		    (sc->mii_flags & MIIF_FORCEPAUSE) != 0) {
			if ((sc->mii_capabilities &
			    (BMSR_10TFDX | BMSR_100TXFDX)) != 0)
				anar |= ANAR_FC;
			/* XXX Only 1000BASE-T has PAUSE_ASYM? */
			if (((sc->mii_flags & MIIF_HAVE_GTCR) != 0) &&
			    (sc->mii_extcapabilities &
			    (EXTSR_1000THDX | EXTSR_1000TFDX)) != 0)
				anar |= ANAR_X_PAUSE_ASYM;
		}
		PHY_WRITE(sc, MII_ANAR, anar);
		if ((sc->mii_flags & MIIF_HAVE_GTCR) != 0) {
			gtcr = 0;
			if ((sc->mii_extcapabilities & EXTSR_1000TFDX) != 0)
				gtcr |= GTCR_ADV_1000TFDX;
			if ((sc->mii_extcapabilities & EXTSR_1000THDX) != 0)
				gtcr |= GTCR_ADV_1000THDX;
			PHY_WRITE(sc, MII_100T2CR, gtcr);
		}
	}
	PHY_WRITE(sc, MII_BMCR, BMCR_AUTOEN | BMCR_STARTNEG);
	return (EJUSTRETURN);
}

int
mii_phy_tick(struct mii_softc *sc)
{
	struct ifmedia_entry *ife = sc->mii_pdata->mii_media.ifm_cur;
	int reg;

	/*
	 * If we're not doing autonegotiation, we don't need to do
	 * any extra work here.  However, we need to check the link
	 * status so we can generate an announcement if the status
	 * changes.
	 */
	if (IFM_SUBTYPE(ife->ifm_media) != IFM_AUTO) {
		sc->mii_ticks = 0;	/* reset autonegotiation timer. */
		return (0);
	}

	/* Read the status register twice; BMSR_LINK is latch-low. */
	reg = PHY_READ(sc, MII_BMSR) | PHY_READ(sc, MII_BMSR);
	if ((reg & BMSR_LINK) != 0) {
		sc->mii_ticks = 0;	/* reset autonegotiation timer. */
		/* See above. */
		return (0);
	}

	/* Announce link loss right after it happens */
	if (sc->mii_ticks++ == 0)
		return (0);

	/* XXX: use default value if phy driver did not set mii_anegticks */
	if (sc->mii_anegticks == 0)
		sc->mii_anegticks = MII_ANEGTICKS_GIGE;

	/* Only retry autonegotiation every mii_anegticks ticks. */
	if (sc->mii_ticks <= sc->mii_anegticks)
		return (EJUSTRETURN);

	sc->mii_ticks = 0;
	PHY_RESET(sc);
	mii_phy_auto(sc);
	return (0);
}

void
mii_phy_reset(struct mii_softc *sc)
{
	struct ifmedia_entry *ife = sc->mii_pdata->mii_media.ifm_cur;
	int i, reg;

	if ((sc->mii_flags & MIIF_NOISOLATE) != 0)
		reg = BMCR_RESET;
	else
		reg = BMCR_RESET | BMCR_ISO;
	PHY_WRITE(sc, MII_BMCR, reg);

	/* Wait 100ms for it to complete. */
	for (i = 0; i < 100; i++) {
		reg = PHY_READ(sc, MII_BMCR);
		if ((reg & BMCR_RESET) == 0)
			break;
		DELAY(1000);
	}

	/* NB: a PHY may default to being powered down and/or isolated. */
	reg &= ~(BMCR_PDOWN | BMCR_ISO);
	if ((sc->mii_flags & MIIF_NOISOLATE) == 0 &&
	    ((ife == NULL && sc->mii_inst != 0) ||
	    (ife != NULL && IFM_INST(ife->ifm_media) != sc->mii_inst)))
		reg |= BMCR_ISO;
	if (PHY_READ(sc, MII_BMCR) != reg)
		PHY_WRITE(sc, MII_BMCR, reg);
}

void
mii_phy_update(struct mii_softc *sc, int cmd)
{
	struct mii_data *mii = sc->mii_pdata;

	if (sc->mii_media_active != mii->mii_media_active ||
	    cmd == MII_MEDIACHG) {
		MIIBUS_STATCHG(sc->mii_dev);
		sc->mii_media_active = mii->mii_media_active;
	}
	if (sc->mii_media_status != mii->mii_media_status) {
		MIIBUS_LINKCHG(sc->mii_dev);
		sc->mii_media_status = mii->mii_media_status;
	}
}

/*
 * Initialize generic PHY media based on BMSR, called when a PHY is
 * attached.  We expect to be set up to print a comma-separated list
 * of media names.  Does not print a newline.
 */
void
mii_phy_add_media(struct mii_softc *sc)
{
	struct mii_data *mii = sc->mii_pdata;
	const char *sep = "";
	int fdx = 0;

	if ((sc->mii_capabilities & BMSR_MEDIAMASK) == 0 &&
	    (sc->mii_extcapabilities & EXTSR_MEDIAMASK) == 0) {
		printf("no media present");
		return;
	}

	/*
	 * Set the autonegotiation timer for 10/100 media.  Gigabit media is
	 * handled below.
	 */
	sc->mii_anegticks = MII_ANEGTICKS;

#define	ADD(m)		ifmedia_add(&mii->mii_media, (m), 0, NULL)
#define	PRINT(s)	printf("%s%s", sep, s); sep = ", "

	if ((sc->mii_flags & MIIF_NOISOLATE) == 0) {
		ADD(IFM_MAKEWORD(IFM_ETHER, IFM_NONE, 0, sc->mii_inst));
		PRINT("none");
	}

	/*
	 * There are different interpretations for the bits in
	 * HomePNA PHYs.  And there is really only one media type
	 * that is supported.
	 */
	if ((sc->mii_flags & MIIF_IS_HPNA) != 0) {
		if ((sc->mii_capabilities & BMSR_10THDX) != 0) {
			ADD(IFM_MAKEWORD(IFM_ETHER, IFM_HPNA_1, 0,
			    sc->mii_inst));
			PRINT("HomePNA1");
		}
		return;
	}

	if ((sc->mii_capabilities & BMSR_10THDX) != 0) {
		ADD(IFM_MAKEWORD(IFM_ETHER, IFM_10_T, 0, sc->mii_inst));
		PRINT("10baseT");
	}
	if ((sc->mii_capabilities & BMSR_10TFDX) != 0) {
		ADD(IFM_MAKEWORD(IFM_ETHER, IFM_10_T, IFM_FDX, sc->mii_inst));
		PRINT("10baseT-FDX");
		if ((sc->mii_flags & MIIF_DOPAUSE) != 0 &&
		    (sc->mii_flags & MIIF_NOMANPAUSE) == 0) {
			ADD(IFM_MAKEWORD(IFM_ETHER, IFM_10_T,
			    IFM_FDX | IFM_FLOW, sc->mii_inst));
			PRINT("10baseT-FDX-flow");
		}
		fdx = 1;
	}
	if ((sc->mii_capabilities & BMSR_100TXHDX) != 0) {
		ADD(IFM_MAKEWORD(IFM_ETHER, IFM_100_TX, 0, sc->mii_inst));
		PRINT("100baseTX");
	}
	if ((sc->mii_capabilities & BMSR_100TXFDX) != 0) {
		ADD(IFM_MAKEWORD(IFM_ETHER, IFM_100_TX, IFM_FDX, sc->mii_inst));
		PRINT("100baseTX-FDX");
		if ((sc->mii_flags & MIIF_DOPAUSE) != 0 &&
		    (sc->mii_flags & MIIF_NOMANPAUSE) == 0) {
			ADD(IFM_MAKEWORD(IFM_ETHER, IFM_100_TX,
			    IFM_FDX | IFM_FLOW, sc->mii_inst));
			PRINT("100baseTX-FDX-flow");
		}
		fdx = 1;
	}
	if ((sc->mii_capabilities & BMSR_100T4) != 0) {
		ADD(IFM_MAKEWORD(IFM_ETHER, IFM_100_T4, 0, sc->mii_inst));
		PRINT("100baseT4");
	}

	if ((sc->mii_extcapabilities & EXTSR_MEDIAMASK) != 0) {
		/*
		 * XXX Right now only handle 1000SX and 1000TX.  Need
		 * XXX to handle 1000LX and 1000CX somehow.
		 */
		if ((sc->mii_extcapabilities & EXTSR_1000XHDX) != 0) {
			sc->mii_anegticks = MII_ANEGTICKS_GIGE;
			sc->mii_flags |= MIIF_IS_1000X;
			ADD(IFM_MAKEWORD(IFM_ETHER, IFM_1000_SX, 0,
			    sc->mii_inst));
			PRINT("1000baseSX");
		}
		if ((sc->mii_extcapabilities & EXTSR_1000XFDX) != 0) {
			sc->mii_anegticks = MII_ANEGTICKS_GIGE;
			sc->mii_flags |= MIIF_IS_1000X;
			ADD(IFM_MAKEWORD(IFM_ETHER, IFM_1000_SX, IFM_FDX,
			    sc->mii_inst));
			PRINT("1000baseSX-FDX");
			if ((sc->mii_flags & MIIF_DOPAUSE) != 0 &&
			    (sc->mii_flags & MIIF_NOMANPAUSE) == 0) {
				ADD(IFM_MAKEWORD(IFM_ETHER, IFM_1000_SX,
				    IFM_FDX | IFM_FLOW, sc->mii_inst));
				PRINT("1000baseSX-FDX-flow");
			}
			fdx = 1;
		}

		/*
		 * 1000baseT media needs to be able to manipulate
		 * master/slave mode.
		 *
		 * All 1000baseT PHYs have a 1000baseT control register.
		 */
		if ((sc->mii_extcapabilities & EXTSR_1000THDX) != 0) {
			sc->mii_anegticks = MII_ANEGTICKS_GIGE;
			sc->mii_flags |= MIIF_HAVE_GTCR;
			ADD(IFM_MAKEWORD(IFM_ETHER, IFM_1000_T, 0,
			    sc->mii_inst));
			PRINT("1000baseT");
			ADD(IFM_MAKEWORD(IFM_ETHER, IFM_1000_T,
			    IFM_ETH_MASTER, sc->mii_inst));
			PRINT("1000baseT-master");
		}
		if ((sc->mii_extcapabilities & EXTSR_1000TFDX) != 0) {
			sc->mii_anegticks = MII_ANEGTICKS_GIGE;
			sc->mii_flags |= MIIF_HAVE_GTCR;
			ADD(IFM_MAKEWORD(IFM_ETHER, IFM_1000_T, IFM_FDX,
			    sc->mii_inst));
			PRINT("1000baseT-FDX");
			ADD(IFM_MAKEWORD(IFM_ETHER, IFM_1000_T,
			    IFM_FDX | IFM_ETH_MASTER, sc->mii_inst));
			PRINT("1000baseT-FDX-master");
			if ((sc->mii_flags & MIIF_DOPAUSE) != 0 &&
			    (sc->mii_flags & MIIF_NOMANPAUSE) == 0) {
				ADD(IFM_MAKEWORD(IFM_ETHER, IFM_1000_T,
				    IFM_FDX | IFM_FLOW, sc->mii_inst));
				PRINT("1000baseT-FDX-flow");
				ADD(IFM_MAKEWORD(IFM_ETHER, IFM_1000_T,
				    IFM_FDX | IFM_FLOW | IFM_ETH_MASTER,
				    sc->mii_inst));
				PRINT("1000baseT-FDX-flow-master");
			}
			fdx = 1;
		}
	}

	if ((sc->mii_capabilities & BMSR_ANEG) != 0) {
		/* intentionally invalid index */
		ADD(IFM_MAKEWORD(IFM_ETHER, IFM_AUTO, 0, sc->mii_inst));
		PRINT("auto");
		if (fdx != 0 && (sc->mii_flags & MIIF_DOPAUSE) != 0) {
			ADD(IFM_MAKEWORD(IFM_ETHER, IFM_AUTO, IFM_FLOW,
			    sc->mii_inst));
			PRINT("auto-flow");
		}
	}
#undef ADD
#undef PRINT
}

int
mii_phy_detach(device_t dev)
{
	struct mii_softc *sc;

	sc = device_get_softc(dev);
	sc->mii_dev = NULL;
	LIST_REMOVE(sc, mii_list);
	return (0);
}

const struct mii_phydesc *
mii_phy_match_gen(const struct mii_attach_args *ma,
  const struct mii_phydesc *mpd, size_t len)
{

	for (; mpd->mpd_name != NULL;
	    mpd = (const struct mii_phydesc *)((const char *)mpd + len)) {
		if (MII_OUI(ma->mii_id1, ma->mii_id2) == mpd->mpd_oui &&
		    MII_MODEL(ma->mii_id2) == mpd->mpd_model)
			return (mpd);
	}
	return (NULL);
}

const struct mii_phydesc *
mii_phy_match(const struct mii_attach_args *ma, const struct mii_phydesc *mpd)
{

	return (mii_phy_match_gen(ma, mpd, sizeof(struct mii_phydesc)));
}

int
mii_phy_dev_probe(device_t dev, const struct mii_phydesc *mpd, int mrv)
{

	mpd = mii_phy_match(device_get_ivars(dev), mpd);
	if (mpd != NULL) {
		device_set_desc(dev, mpd->mpd_name);
		return (mrv);
	}
	return (ENXIO);
}

void
mii_phy_dev_attach(device_t dev, u_int flags, const struct mii_phy_funcs *mpf,
    int add_media)
{
	struct mii_softc *sc;
	struct mii_attach_args *ma;
	struct mii_data *mii;

	sc = device_get_softc(dev);
	ma = device_get_ivars(dev);
	sc->mii_dev = device_get_parent(dev);
	mii = ma->mii_data;
	LIST_INSERT_HEAD(&mii->mii_phys, sc, mii_list);

	sc->mii_flags = flags | miibus_get_flags(dev);
	sc->mii_mpd_oui = MII_OUI(ma->mii_id1, ma->mii_id2);
	sc->mii_mpd_model = MII_MODEL(ma->mii_id2);
	sc->mii_mpd_rev = MII_REV(ma->mii_id2);
	sc->mii_capmask = ma->mii_capmask;
	sc->mii_inst = mii->mii_instance++;
	sc->mii_phy = ma->mii_phyno;
	sc->mii_offset = ma->mii_offset;
	sc->mii_funcs = mpf;
	sc->mii_pdata = mii;

	if (bootverbose)
		device_printf(dev, "OUI 0x%06x, model 0x%04x, rev. %d\n",
		    sc->mii_mpd_oui, sc->mii_mpd_model, sc->mii_mpd_rev);

	if (add_media == 0)
		return;

	PHY_RESET(sc);

	sc->mii_capabilities = PHY_READ(sc, MII_BMSR) & sc->mii_capmask;
	if (sc->mii_capabilities & BMSR_EXTSTAT)
		sc->mii_extcapabilities = PHY_READ(sc, MII_EXTSR);
	device_printf(dev, " ");
	mii_phy_add_media(sc);
	printf("\n");

	MIIBUS_MEDIAINIT(sc->mii_dev);
}

/*
 * Return the flow control status flag from MII_ANAR & MII_ANLPAR.
 */
u_int
mii_phy_flowstatus(struct mii_softc *sc)
{
	int anar, anlpar;

	if ((sc->mii_flags & MIIF_DOPAUSE) == 0)
		return (0);

	anar = PHY_READ(sc, MII_ANAR);
	anlpar = PHY_READ(sc, MII_ANLPAR);

	/*
	 * Check for 1000BASE-X.  Autonegotiation is a bit
	 * different on such devices.
	 */
	if ((sc->mii_flags & MIIF_IS_1000X) != 0) {
		anar <<= 3;
		anlpar <<= 3;
	}

	if ((anar & ANAR_PAUSE_SYM) != 0 && (anlpar & ANLPAR_PAUSE_SYM) != 0)
		return (IFM_FLOW | IFM_ETH_TXPAUSE | IFM_ETH_RXPAUSE);

	if ((anar & ANAR_PAUSE_SYM) == 0) {
		if ((anar & ANAR_PAUSE_ASYM) != 0 &&
		    (anlpar & ANLPAR_PAUSE_TOWARDS) != 0)
			return (IFM_FLOW | IFM_ETH_TXPAUSE);
		else
			return (0);
	}

	if ((anar & ANAR_PAUSE_ASYM) == 0) {
		if ((anlpar & ANLPAR_PAUSE_SYM) != 0)
			return (IFM_FLOW | IFM_ETH_TXPAUSE | IFM_ETH_RXPAUSE);
		else
			return (0);
	}

	switch ((anlpar & ANLPAR_PAUSE_TOWARDS)) {
	case ANLPAR_PAUSE_NONE:
		return (0);
	case ANLPAR_PAUSE_ASYM:
		return (IFM_FLOW | IFM_ETH_RXPAUSE);
	default:
		return (IFM_FLOW | IFM_ETH_RXPAUSE | IFM_ETH_TXPAUSE);
	}
	/* NOTREACHED */
}
