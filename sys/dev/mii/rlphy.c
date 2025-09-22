/*	$OpenBSD: rlphy.c,v 1.35 2025/05/09 13:53:10 jcs Exp $	*/

/*
 * Copyright (c) 1998, 1999 Jason L. Wright (jason@thought.net)
 * All rights reserved.
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
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * Driver for the internal PHY found on RTL8139 based nics, based
 * on drivers for the 'exphy' (Internal 3Com phys) and 'nsphy'
 * (National Semiconductor DP83840).
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/device.h>
#include <sys/socket.h>
#include <sys/errno.h>

#include <machine/bus.h>

#include <net/if.h>
#include <net/if_media.h>

#include <netinet/in.h>
#include <netinet/if_ether.h>

#include <dev/mii/mii.h>
#include <dev/mii/miivar.h>
#include <dev/mii/miidevs.h>

#include <dev/ic/rtl81x9reg.h>

int	rlphymatch(struct device *, void *, void *);
void	rlphyattach(struct device *, struct device *, void *);

const struct cfattach rlphy_ca = {
	sizeof(struct mii_softc), rlphymatch, rlphyattach, mii_phy_detach
};

struct cfdriver rlphy_cd = {
	NULL, "rlphy", DV_DULL
};

int	rlphy_service(struct mii_softc *, struct mii_data *, int);
void	rlphy_status(struct mii_softc *);

const struct mii_phy_funcs rlphy_funcs = {
	rlphy_service, rlphy_status, mii_phy_reset,
};

static const struct mii_phydesc rlphys[] = {
	{ MII_OUI_REALTEK,		MII_MODEL_REALTEK_RTL8201L,
          MII_STR_REALTEK_RTL8201L },
	{ MII_OUI_xxREALTEK,		MII_MODEL_xxREALTEK_RTL8201E,
          MII_STR_xxREALTEK_RTL8201E },
	{ MII_OUI_xxREALTEK,		MII_MODEL_xxREALTEK_RTL8201F,
	  MII_STR_xxREALTEK_RTL8201F },
	{ MII_OUI_ICPLUS,		MII_MODEL_ICPLUS_IP101,
	  MII_STR_ICPLUS_IP101 },

	{ 0,				0,
	  NULL },
};
int
rlphymatch(struct device *parent, void *match, void *aux)
{
	struct mii_attach_args *ma = aux;
	char *devname;

	devname = parent->dv_cfdata->cf_driver->cd_name;

	if (mii_phy_match(ma, rlphys) != NULL)
		return (10);

	if (MII_OUI(ma->mii_id1, ma->mii_id2) != 0 ||
	    MII_MODEL(ma->mii_id2) != 0)
		return (0);

	if ((strcmp(devname, "re") != 0) &&
	    (strcmp(devname, "rl") != 0))
		return (0);

	/*
	 * A "real" phy should get preference, but on the 8139 there
	 * is no phyid register.
	 */
	return (5);
}

void
rlphyattach(struct device *parent, struct device *self, void *aux)
{
	struct mii_softc *sc = (struct mii_softc *)self;
	struct mii_attach_args *ma = aux;
	struct mii_data *mii = ma->mii_data;
	const struct mii_phydesc *mpd;

	mpd = mii_phy_match(ma, rlphys);
	if (mpd != NULL) {
		printf(": %s, rev. %d\n", mpd->mpd_name,
		    MII_REV(ma->mii_id2));
	} else
		printf(": RTL internal PHY\n");

	sc->mii_inst = mii->mii_instance;
	sc->mii_phy = ma->mii_phyno;
	sc->mii_funcs = &rlphy_funcs;
	sc->mii_pdata = mii;
	sc->mii_flags = ma->mii_flags;

	sc->mii_flags |= MIIF_NOISOLATE;

	PHY_RESET(sc);

	sc->mii_capabilities =
	    PHY_READ(sc, MII_BMSR) & ma->mii_capmask;
	if (sc->mii_capabilities & BMSR_MEDIAMASK)
		mii_phy_add_media(sc);
}

int
rlphy_service(struct mii_softc *sc, struct mii_data *mii, int cmd)
{
	struct ifmedia_entry *ife = mii->mii_media.ifm_cur;

	if ((sc->mii_dev.dv_flags & DVF_ACTIVE) == 0)
		return (ENXIO);

	/*
	 * Can't isolate the RTL8139 phy, so it has to be the only one.
	 */
	if (IFM_INST(ife->ifm_media) != sc->mii_inst)
		panic("rlphy_service: attempt to isolate phy");

	switch (cmd) {
	case MII_POLLSTAT:
		break;

	case MII_MEDIACHG:
		/*
		 * If the interface is not up, don't do anything.
		 */
		if ((mii->mii_ifp->if_flags & IFF_UP) == 0)
			break;

		switch (IFM_SUBTYPE(ife->ifm_media)) {
		case IFM_AUTO:
			/*
			 * If we're already in auto mode, just return.
			 */
			if (PHY_READ(sc, MII_BMCR) & BMCR_AUTOEN)
				return (0);
			(void) mii_phy_auto(sc, 0);
			break;
		case IFM_100_T4:
			/*
			 * XXX Not supported as a manual setting right now.
			 */
			return (EINVAL);
		default:
			/*
			 * BMCR data is stored in the ifmedia entry.
			 */
			PHY_WRITE(sc, MII_ANAR,
			    mii_anar(ife->ifm_media));
			PHY_WRITE(sc, MII_BMCR, ife->ifm_data);
		}
		break;

	case MII_TICK:
		/*
		 * Is the interface even up?
		 */
		if ((mii->mii_ifp->if_flags & IFF_UP) == 0)
			return (0);

		/*
		 * The Realtek PHY's autonegotiation doesn't need to be
		 * kicked; it continues in the background.
		 */
		break;

	case MII_DOWN:
		mii_phy_down(sc);
		return (0);
	}

	/* Update the media status. */
	mii_phy_status(sc);

	/* Callback if something changed. */
	mii_phy_update(sc, cmd);
	return (0);
}

void
rlphy_status(struct mii_softc *sc)
{
	struct mii_data *mii = sc->mii_pdata;
	struct ifmedia_entry *ife = mii->mii_media.ifm_cur;
	int bmsr, bmcr, anlpar;
	char *devname;

	devname = sc->mii_dev.dv_parent->dv_cfdata->cf_driver->cd_name;

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
		 * NWay autonegotiation takes the highest-order common
		 * bit of the ANAR and ANLPAR (i.e. best media advertised
		 * both by us and our link partner).
		 */
		if ((bmsr & BMSR_ACOMP) == 0) {
			/* Erg, still trying, I guess... */
			mii->mii_media_active |= IFM_NONE;
			return;
		}

		if ((anlpar = PHY_READ(sc, MII_ANAR) &
		    PHY_READ(sc, MII_ANLPAR))) {
			if (anlpar & ANLPAR_TX_FD)
				mii->mii_media_active |= IFM_100_TX|IFM_FDX;
			else if (anlpar & ANLPAR_T4)
				mii->mii_media_active |= IFM_100_T4|IFM_HDX;
			else if (anlpar & ANLPAR_TX)
				mii->mii_media_active |= IFM_100_TX|IFM_HDX;
			else if (anlpar & ANLPAR_10_FD)
				mii->mii_media_active |= IFM_10_T|IFM_FDX;
			else if (anlpar & ANLPAR_10)
				mii->mii_media_active |= IFM_10_T|IFM_HDX;
			else
				mii->mii_media_active |= IFM_NONE;
			return;
		}

		/*
		 * If the other side doesn't support NWAY, then the
		 * best we can do is determine if we have a 10Mbps or
		 * 100Mbps link. There's no way to know if the link 
		 * is full or half duplex, so we default to half duplex
		 * and hope that the user is clever enough to manually
		 * change the media settings if we're wrong.
		 */

		/*
		 * The Realtek PHY supports non-NWAY link speed
		 * detection, however it does not report the link
		 * detection results via the ANLPAR or BMSR registers.
		 * (What? Realtek doesn't do things the way everyone
		 * else does? I'm just shocked, shocked I tell you.)
		 * To determine the link speed, we have to do one
		 * of two things:
		 *
		 * - If this is a standalone Realtek RTL8201(L) PHY,
		 *   we can determine the link speed by testing bit 0
		 *   in the magic, vendor-specific register at offset
		 *   0x19.
		 *
		 * - If this is a Realtek MAC with integrated PHY, we
		 *   can test the 'SPEED10' bit of the MAC's media status
		 *   register.
		 */
		if (strcmp("rl", devname) == 0 ||
		    strcmp("re", devname) == 0) {
			if (PHY_READ(sc, RL_MEDIASTAT) & RL_MEDIASTAT_SPEED10)
				mii->mii_media_active |= IFM_10_T;
			else
				mii->mii_media_active |= IFM_100_TX;
		} else {
			if (PHY_READ(sc, 0x0019) & 0x01)
				mii->mii_media_active |= IFM_100_TX;
			else
				mii->mii_media_active |= IFM_10_T;
		}
		mii->mii_media_active |= IFM_HDX;
	} else
		mii->mii_media_active = ife->ifm_media;
}
