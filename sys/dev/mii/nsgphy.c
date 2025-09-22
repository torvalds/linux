/*	$OpenBSD: nsgphy.c,v 1.26 2022/04/06 18:59:29 naddy Exp $	*/
/*
 * Copyright (c) 2001 Wind River Systems
 * Copyright (c) 2001
 *	Bill Paul <wpaul@bsdi.com>.  All rights reserved.
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
 *
 */

/*
 * Driver for the National Semiconductor DP83861, DP83865 and DP83891
 * 10/100/1000 PHYs.
 * Datasheet available at: http://www.national.com/ds/DP/DP83861.pdf
 * and at: http://www.national.com/ds/DP/DP83865.pdf
 *
 * The DP83891 is the older NatSemi gigE PHY which isn't being sold
 * anymore. The DP83861 is its replacement, which is an 'enhanced'
 * firmware driven component. The major difference between the
 * two is that the 83891 can't generate interrupts, while the
 * 83861 can. (I think it wasn't originally designed to do this, but
 * it can now thanks to firmware updates.) The 83861 also allows
 * access to its internal RAM via indirect register access.
 *
 * The DP83865 is a low power version of the DP83861.
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

#include <dev/mii/nsgphyreg.h>

int	nsgphymatch(struct device*, void *, void *);
void	nsgphyattach(struct device *, struct device *, void *);

const struct cfattach nsgphy_ca = {
	sizeof(struct mii_softc), nsgphymatch, nsgphyattach, mii_phy_detach
};

struct cfdriver nsgphy_cd = {
	NULL, "nsgphy", DV_DULL
};

int	nsgphy_service(struct mii_softc *, struct mii_data *, int);
void	nsgphy_status(struct mii_softc *);

const struct mii_phy_funcs nsgphy_funcs = {
	nsgphy_service, nsgphy_status, mii_phy_reset,
};

static const struct mii_phydesc nsgphys[] = {
	{ MII_OUI_NATSEMI,		MII_MODEL_NATSEMI_DP83861,
	  MII_STR_NATSEMI_DP83861 },
	{ MII_OUI_NATSEMI,		MII_MODEL_NATSEMI_DP83865,
	  MII_STR_NATSEMI_DP83865 },
	{ MII_OUI_NATSEMI,		MII_MODEL_NATSEMI_DP83891,
	  MII_STR_NATSEMI_DP83891 },

	{ 0,			0,
	  NULL },
};

int
nsgphymatch(struct device *parent, void *match, void *aux)
{
	struct mii_attach_args *ma = aux;

	if (mii_phy_match(ma, nsgphys) != NULL)
		return (10);

	return (0);
}

void
nsgphyattach(struct device *parent, struct device *self, void *aux)
{
	struct mii_softc *sc = (struct mii_softc *)self;
	struct mii_attach_args *ma = aux;
	struct mii_data *mii = ma->mii_data;
	const struct mii_phydesc *mpd;
	int anar;

	mpd = mii_phy_match(ma, nsgphys);
	printf(": %s, rev. %d\n", mpd->mpd_name, MII_REV(ma->mii_id2));

	sc->mii_inst = mii->mii_instance;
	sc->mii_phy = ma->mii_phyno;
	sc->mii_funcs = &nsgphy_funcs;
	sc->mii_pdata = mii;
	sc->mii_flags = ma->mii_flags;
	sc->mii_anegticks = MII_ANEGTICKS;

	PHY_RESET(sc);

	sc->mii_capabilities =
		PHY_READ(sc, MII_BMSR) & ma->mii_capmask;
	if (sc->mii_capabilities & BMSR_EXTSTAT)
		sc->mii_extcapabilities = PHY_READ(sc, MII_EXTSR);

	/*
	 * The PHY seems to have the 10baseT BMSR bits
	 * hard-wired to 0, even though the device supports
	 * 10baseT.  What we do instead is read the post-reset
	 * ANAR, who's 10baseT-related bits are set by strapping
	 * pin 180, and fake the BMSR bits.
	 */
	anar = PHY_READ(sc, MII_ANAR);
	if (anar & ANAR_10)
		sc->mii_capabilities |= (BMSR_10THDX & ma->mii_capmask);
	if (anar & ANAR_10_FD)
		sc->mii_capabilities |= (BMSR_10TFDX & ma->mii_capmask);

	if ((sc->mii_capabilities & BMSR_MEDIAMASK) ||
	    (sc->mii_extcapabilities & EXTSR_MEDIAMASK))
		mii_phy_add_media(sc);
}

int
nsgphy_service(struct mii_softc *sc, struct mii_data *mii, int cmd)
{
	struct ifmedia_entry *ife = mii->mii_media.ifm_cur;
	int reg;

	if ((sc->mii_dev.dv_flags & DVF_ACTIVE) == 0)
		return (ENXIO);

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
			reg = PHY_READ(sc, MII_BMCR);
			PHY_WRITE(sc, MII_BMCR, reg | BMCR_ISO);
			return (0);
		}

		/*
		 * If the interface is not up, don't do anything.
		 */
		if ((mii->mii_ifp->if_flags & IFF_UP) == 0)
			break;

		mii_phy_setmedia(sc);
		break;

	case MII_TICK:
		/*
		 * If we're not currently selected, just return.
		 */
		if (IFM_INST(ife->ifm_media) != sc->mii_inst)
			return (0);

		if (mii_phy_tick(sc) == EJUSTRETURN)
			return (0);
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
nsgphy_status(struct mii_softc *sc)
{
	struct mii_data *mii = sc->mii_pdata;
	struct ifmedia_entry *ife = mii->mii_media.ifm_cur;
	int bmsr, bmcr, physup, gtsr;

	mii->mii_media_status = IFM_AVALID;
	mii->mii_media_active = IFM_ETHER;

	bmsr = PHY_READ(sc, MII_BMSR) | PHY_READ(sc, MII_BMSR);

	physup = PHY_READ(sc, NSGPHY_MII_PHYSUP);

	if (physup & PHY_SUP_LINK)
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
		if ((bmsr & BMSR_ACOMP) == 0) {
			/* Erg, still trying, I guess... */
			mii->mii_media_active |= IFM_NONE;
			return;
		}

		switch (physup & (PHY_SUP_SPEED1|PHY_SUP_SPEED0)) {
		case PHY_SUP_SPEED1:
			mii->mii_media_active |= IFM_1000_T;
			gtsr = PHY_READ(sc, MII_100T2SR);
			if (gtsr & GTSR_MS_RES)
				mii->mii_media_active |= IFM_ETH_MASTER;
			break;

		case PHY_SUP_SPEED0:
			mii->mii_media_active |= IFM_100_TX;
			break;

		case 0:
			mii->mii_media_active |= IFM_10_T;
			break;

		default:
			mii->mii_media_active |= IFM_NONE;
			mii->mii_media_status = 0;
			return;
		}

		if (physup & PHY_SUP_DUPLEX)
			mii->mii_media_active |= mii_phy_flowstatus(sc) | IFM_FDX;
		else
			mii->mii_media_active |= IFM_HDX;
	} else
		mii->mii_media_active = ife->ifm_media;
}
