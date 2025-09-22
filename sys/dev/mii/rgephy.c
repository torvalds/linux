/*	$OpenBSD: rgephy.c,v 1.43 2023/04/05 10:45:07 kettenis Exp $	*/
/*
 * Copyright (c) 2003
 *	Bill Paul <wpaul@windriver.com>.  All rights reserved.
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
 * $FreeBSD: rgephy.c,v 1.5 2004/05/30 17:57:40 phk Exp $
 */

/*
 * Driver for the Realtek 8169S/8110S internal 10/100/1000 PHY.
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

#include <dev/mii/rgephyreg.h>

#include <dev/ic/rtl81x9reg.h>

int	rgephymatch(struct device *, void *, void *);
void	rgephyattach(struct device *, struct device *, void *);

const struct cfattach rgephy_ca = { sizeof(struct mii_softc),
	rgephymatch, rgephyattach, mii_phy_detach,
};

struct cfdriver rgephy_cd = {
	NULL, "rgephy", DV_DULL
};

int	rgephy_service(struct mii_softc *, struct mii_data *, int);
void	rgephy_status(struct mii_softc *);
int	rgephy_mii_phy_auto(struct mii_softc *);
void	rgephy_reset(struct mii_softc *);
void	rgephy_loop(struct mii_softc *);
void	rgephy_init_rtl8211f(struct mii_softc *);
void	rgephy_load_dspcode(struct mii_softc *);

const struct mii_phy_funcs rgephy_funcs = {
	rgephy_service, rgephy_status, rgephy_reset,
};

static const struct mii_phydesc rgephys[] = {
	{ MII_OUI_REALTEK2,		MII_MODEL_xxREALTEK_RTL8169S,
	  MII_STR_xxREALTEK_RTL8169S },
	{ MII_OUI_xxREALTEK,		MII_MODEL_xxREALTEK_RTL8169S,
	  MII_STR_xxREALTEK_RTL8169S },
	{ MII_OUI_xxREALTEK,		MII_MODEL_xxREALTEK_RTL8251,
	  MII_STR_xxREALTEK_RTL8251 },
	{ MII_OUI_xxREALTEK,		MII_MODEL_xxREALTEK_RTL8211FVD,
	  MII_STR_xxREALTEK_RTL8211FVD },

	{ 0,			0,
	  NULL },
};

int
rgephymatch(struct device *parent, void *match, void *aux)
{
	struct mii_attach_args *ma = aux;

	if (mii_phy_match(ma, rgephys) != NULL)
		return (10);

	return (0);
}

void
rgephyattach(struct device *parent, struct device *self, void *aux)
{
	struct mii_softc *sc = (struct mii_softc *)self;
	struct mii_attach_args *ma = aux;
	struct mii_data *mii = ma->mii_data;
	const struct mii_phydesc *mpd;

	mpd = mii_phy_match(ma, rgephys);
	printf(": %s, rev. %d\n", mpd->mpd_name, MII_REV(ma->mii_id2));

	sc->mii_inst = mii->mii_instance;
	sc->mii_phy = ma->mii_phyno;
	sc->mii_funcs = &rgephy_funcs;
	sc->mii_model = MII_MODEL(ma->mii_id2);
	sc->mii_rev = MII_REV(ma->mii_id2);
	sc->mii_pdata = mii;
	sc->mii_flags = ma->mii_flags;
	sc->mii_anegticks = MII_ANEGTICKS_GIGE;

	sc->mii_flags |= MIIF_NOISOLATE;

	sc->mii_capabilities = PHY_READ(sc, MII_BMSR) & ma->mii_capmask;

	if (sc->mii_capabilities & BMSR_EXTSTAT)
		sc->mii_extcapabilities = PHY_READ(sc, MII_EXTSR);
	if ((sc->mii_capabilities & BMSR_MEDIAMASK) ||
	    (sc->mii_extcapabilities & EXTSR_MEDIAMASK))
		mii_phy_add_media(sc);

	if (sc->mii_model == MII_MODEL_xxREALTEK_RTL8211FVD ||
	    (sc->mii_model == MII_MODEL_xxREALTEK_RTL8169S &&
	     sc->mii_rev == RGEPHY_8211F))
		rgephy_init_rtl8211f(sc);

	PHY_RESET(sc);
}

int
rgephy_service(struct mii_softc *sc, struct mii_data *mii, int cmd)
{
	struct ifmedia_entry *ife = mii->mii_media.ifm_cur;
	int anar, reg, speed, gig = 0;
	char *devname;

	devname = sc->mii_dev.dv_parent->dv_cfdata->cf_driver->cd_name;

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

		PHY_RESET(sc);	/* XXX hardware bug work-around */

		anar = PHY_READ(sc, MII_ANAR);
		anar &= ~(ANAR_TX_FD | ANAR_TX | ANAR_10_FD | ANAR_10);

		switch (IFM_SUBTYPE(ife->ifm_media)) {
		case IFM_AUTO:
			(void) rgephy_mii_phy_auto(sc);
			break;
		case IFM_1000_T:
			speed = BMCR_S1000;
			goto setit;
		case IFM_100_TX:
			speed = BMCR_S100;
			anar |= ANAR_TX_FD | ANAR_TX;
			goto setit;
		case IFM_10_T:
			speed = BMCR_S10;
			anar |= ANAR_10_FD | ANAR_10;
setit:
			rgephy_loop(sc);
			if ((ife->ifm_media & IFM_GMASK) == IFM_FDX) {
				speed |= BMCR_FDX;
				if (IFM_SUBTYPE(ife->ifm_media) == IFM_1000_T)
					gig = GTCR_ADV_1000TFDX;
				anar &= ~(ANAR_TX | ANAR_10);
			} else {
				if (IFM_SUBTYPE(ife->ifm_media) == IFM_1000_T)
					gig = GTCR_ADV_1000THDX;
				anar &=
				    ~(ANAR_TX_FD | ANAR_10_FD);
			}

			if (IFM_SUBTYPE(ife->ifm_media) == IFM_1000_T &&
			    mii->mii_media.ifm_media & IFM_ETH_MASTER)
				gig |= GTCR_MAN_MS|GTCR_ADV_MS;

			PHY_WRITE(sc, MII_100T2CR, gig);
			PHY_WRITE(sc, MII_BMCR, speed | BMCR_AUTOEN |
			  BMCR_STARTNEG);
			PHY_WRITE(sc, MII_ANAR, anar);
			break;
#if 0
		case IFM_NONE:
			PHY_WRITE(sc, MII_BMCR, BMCR_ISO|BMCR_PDOWN);
			break;
#endif
		default:
			return (EINVAL);
		}
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
		if (IFM_SUBTYPE(ife->ifm_media) != IFM_AUTO)
			break;

		/*
		 * Check to see if we have link.  If we do, we don't
		 * need to restart the autonegotiation process.  Read
		 * the BMSR twice in case it's latched.
		 */
		if (strcmp(devname, "re") == 0 || strcmp(devname, "ure") == 0) {
			reg = PHY_READ(sc, RL_GMEDIASTAT);
			if (reg & RL_GMEDIASTAT_LINK) {
				sc->mii_ticks = 0;
				break;
			}
		} else if (sc->mii_model == MII_MODEL_xxREALTEK_RTL8211FVD ||
		    (sc->mii_model == MII_MODEL_xxREALTEK_RTL8169S &&
		     sc->mii_rev == RGEPHY_8211F)) {
			reg = PHY_READ(sc, RGEPHY_F_SR);
			if (reg & RGEPHY_F_SR_LINK) {
				sc->mii_ticks = 0;
			}
		} else {
			reg = PHY_READ(sc, RGEPHY_SR);
			if (reg & RGEPHY_SR_LINK) {
				sc->mii_ticks = 0;
				break;
			}
		}

		/*
	 	 * Only retry autonegotiation every mii_anegticks seconds.
		 */
		if (++sc->mii_ticks <= sc->mii_anegticks)
			break;
		
		sc->mii_ticks = 0;
		rgephy_mii_phy_auto(sc);
		break;
	}

	/* Update the media status. */
	mii_phy_status(sc);

	/*
	 * Callback if something changed. Note that we need to poke
	 * the DSP on the Realtek PHYs if the media changes.
	 *
	 */
	if (sc->mii_media_active != mii->mii_media_active || 
	    sc->mii_media_status != mii->mii_media_status ||
	    cmd == MII_MEDIACHG)
		rgephy_load_dspcode(sc);

	/* Callback if something changed. */
	mii_phy_update(sc, cmd);

	return (0);
}

void
rgephy_status(struct mii_softc *sc)
{
	struct mii_data *mii = sc->mii_pdata;
	int bmsr, bmcr, gtsr;
	char *devname;

	devname = sc->mii_dev.dv_parent->dv_cfdata->cf_driver->cd_name;

	mii->mii_media_status = IFM_AVALID;
	mii->mii_media_active = IFM_ETHER;

	if (strcmp(devname, "re") == 0 || strcmp(devname, "ure") == 0) {
		bmsr = PHY_READ(sc, RL_GMEDIASTAT);
		if (bmsr & RL_GMEDIASTAT_LINK)
			mii->mii_media_status |= IFM_ACTIVE;
	} else if (sc->mii_model == MII_MODEL_xxREALTEK_RTL8211FVD ||
	    (sc->mii_model == MII_MODEL_xxREALTEK_RTL8169S &&
	     sc->mii_rev == RGEPHY_8211F)) {
		bmsr = PHY_READ(sc, RGEPHY_F_SR);
		if (bmsr & RGEPHY_F_SR_LINK)
			mii->mii_media_status |= IFM_ACTIVE;
	} else {
		bmsr = PHY_READ(sc, RGEPHY_SR);
		if (bmsr & RGEPHY_SR_LINK)
			mii->mii_media_status |= IFM_ACTIVE;
	}	

	bmsr = PHY_READ(sc, MII_BMSR);

	bmcr = PHY_READ(sc, MII_BMCR);

	if (bmcr & BMCR_LOOP)
		mii->mii_media_active |= IFM_LOOP;

	if (bmcr & BMCR_AUTOEN) {
		if ((bmsr & BMSR_ACOMP) == 0) {
			/* Erg, still trying, I guess... */
			mii->mii_media_active |= IFM_NONE;
			return;
		}
	}

	if (strcmp(devname, "re") == 0 || strcmp(devname, "ure") == 0) {
		bmsr = PHY_READ(sc, RL_GMEDIASTAT);
		if (bmsr & RL_GMEDIASTAT_1000MBPS)
			mii->mii_media_active |= IFM_1000_T;
		else if (bmsr & RL_GMEDIASTAT_100MBPS)
			mii->mii_media_active |= IFM_100_TX;
		else if (bmsr & RL_GMEDIASTAT_10MBPS)
			mii->mii_media_active |= IFM_10_T;

		if (bmsr & RL_GMEDIASTAT_FDX)
			mii->mii_media_active |= mii_phy_flowstatus(sc) |
			    IFM_FDX;
		else
			mii->mii_media_active |= IFM_HDX;
	} else if (sc->mii_model == MII_MODEL_xxREALTEK_RTL8211FVD ||
	    (sc->mii_model == MII_MODEL_xxREALTEK_RTL8169S &&
	     sc->mii_rev == RGEPHY_8211F)) {
		bmsr = PHY_READ(sc, RGEPHY_F_SR);
		if (RGEPHY_F_SR_SPEED(bmsr) == RGEPHY_F_SR_SPEED_1000MBPS)
			mii->mii_media_active |= IFM_1000_T;
		else if (RGEPHY_F_SR_SPEED(bmsr) == RGEPHY_F_SR_SPEED_100MBPS)
			mii->mii_media_active |= IFM_100_TX;
		else if (RGEPHY_F_SR_SPEED(bmsr) == RGEPHY_F_SR_SPEED_10MBPS)
			mii->mii_media_active |= IFM_10_T;

		if (bmsr & RGEPHY_F_SR_FDX)
			mii->mii_media_active |= mii_phy_flowstatus(sc) |
			    IFM_FDX;
		else
			mii->mii_media_active |= IFM_HDX;
	} else {
		bmsr = PHY_READ(sc, RGEPHY_SR);
		if (RGEPHY_SR_SPEED(bmsr) == RGEPHY_SR_SPEED_1000MBPS)
			mii->mii_media_active |= IFM_1000_T;
		else if (RGEPHY_SR_SPEED(bmsr) == RGEPHY_SR_SPEED_100MBPS)
			mii->mii_media_active |= IFM_100_TX;
		else if (RGEPHY_SR_SPEED(bmsr) == RGEPHY_SR_SPEED_10MBPS)
			mii->mii_media_active |= IFM_10_T;

		if (bmsr & RGEPHY_SR_FDX)
			mii->mii_media_active |= mii_phy_flowstatus(sc) |
			    IFM_FDX;
		else
			mii->mii_media_active |= IFM_HDX;
	}

	gtsr = PHY_READ(sc, MII_100T2SR);
	if ((IFM_SUBTYPE(mii->mii_media_active) == IFM_1000_T) &&
	    gtsr & GTSR_MS_RES)
		mii->mii_media_active |= IFM_ETH_MASTER;
}


int
rgephy_mii_phy_auto(struct mii_softc *sc)
{
	int anar;

	rgephy_loop(sc);
	PHY_RESET(sc);

	anar = BMSR_MEDIA_TO_ANAR(sc->mii_capabilities) | ANAR_CSMA;
	if (sc->mii_flags & MIIF_DOPAUSE)
		anar |= ANAR_FC | ANAR_X_PAUSE_ASYM;

	PHY_WRITE(sc, MII_ANAR, anar);
	DELAY(1000);
	PHY_WRITE(sc, MII_100T2CR, GTCR_ADV_1000THDX | GTCR_ADV_1000TFDX);
	DELAY(1000);
	PHY_WRITE(sc, MII_BMCR, BMCR_AUTOEN | BMCR_STARTNEG);
	DELAY(100);

	return (EJUSTRETURN);
}

void
rgephy_loop(struct mii_softc *sc)
{
	u_int32_t bmsr;
	int i;

	if (sc->mii_model == MII_MODEL_xxREALTEK_RTL8169S &&
	    sc->mii_rev < 2) {
		PHY_WRITE(sc, MII_BMCR, BMCR_PDOWN);
		DELAY(1000);
	}

	for (i = 0; i < 15000; i++) {
		bmsr = PHY_READ(sc, MII_BMSR);
		if (!(bmsr & BMSR_LINK))
			break;
		DELAY(10);
	}
}

void
rgephy_init_rtl8211f(struct mii_softc *sc)
{
	if (sc->mii_flags & MIIF_SETDELAY) {
		int page, val;

		/* save page */
		page = PHY_READ(sc, RGEPHY_PS);
		PHY_WRITE(sc, RGEPHY_PS, RGEPHY_PS_PAGE_MII);

		val = PHY_READ(sc, RGEPHY_MIICR1);
		if (sc->mii_flags & MIIF_TXID)
			val |= RGEPHY_MIICR1_TXDLY_EN;
		else
			val &= ~RGEPHY_MIICR1_TXDLY_EN;
		PHY_WRITE(sc, RGEPHY_MIICR1, val);

		val = PHY_READ(sc, RGEPHY_MIICR2);
		if (sc->mii_flags & MIIF_RXID)
			val |= RGEPHY_MIICR2_RXDLY_EN;
		else
			val &= ~RGEPHY_MIICR2_RXDLY_EN;
		PHY_WRITE(sc, RGEPHY_MIICR2, val);

		/* restore page */
		PHY_WRITE(sc, RGEPHY_PS, page);
	}
}

#define PHY_SETBIT(x, y, z) \
	PHY_WRITE(x, y, (PHY_READ(x, y) | (z)))
#define PHY_CLRBIT(x, y, z) \
	PHY_WRITE(x, y, (PHY_READ(x, y) & ~(z)))

/*
 * Initialize Realtek PHY per the datasheet. The DSP in the PHYs of
 * existing revisions of the 8169S/8110S chips need to be tuned in
 * order to reliably negotiate a 1000Mbps link. This is only needed
 * for rev 0 and rev 1 of the PHY. Later versions work without
 * any fixups.
 */
void
rgephy_load_dspcode(struct mii_softc *sc)
{
	int val;

	if (sc->mii_model != MII_MODEL_xxREALTEK_RTL8169S ||
	    sc->mii_rev > 1)
		return;

	PHY_WRITE(sc, 31, 0x0001);
	PHY_WRITE(sc, 21, 0x1000);
	PHY_WRITE(sc, 24, 0x65C7);
	PHY_CLRBIT(sc, 4, 0x0800);
	val = PHY_READ(sc, 4) & 0xFFF;
	PHY_WRITE(sc, 4, val);
	PHY_WRITE(sc, 3, 0x00A1);
	PHY_WRITE(sc, 2, 0x0008);
	PHY_WRITE(sc, 1, 0x1020);
	PHY_WRITE(sc, 0, 0x1000);
	PHY_SETBIT(sc, 4, 0x0800);
	PHY_CLRBIT(sc, 4, 0x0800);
	val = (PHY_READ(sc, 4) & 0xFFF) | 0x7000;
	PHY_WRITE(sc, 4, val);
	PHY_WRITE(sc, 3, 0xFF41);
	PHY_WRITE(sc, 2, 0xDE60);
	PHY_WRITE(sc, 1, 0x0140);
	PHY_WRITE(sc, 0, 0x0077);
	val = (PHY_READ(sc, 4) & 0xFFF) | 0xA000;
	PHY_WRITE(sc, 4, val);
	PHY_WRITE(sc, 3, 0xDF01);
	PHY_WRITE(sc, 2, 0xDF20);
	PHY_WRITE(sc, 1, 0xFF95);
	PHY_WRITE(sc, 0, 0xFA00);
	val = (PHY_READ(sc, 4) & 0xFFF) | 0xB000;
	PHY_WRITE(sc, 4, val);
	PHY_WRITE(sc, 3, 0xFF41);
	PHY_WRITE(sc, 2, 0xDE20);
	PHY_WRITE(sc, 1, 0x0140);
	PHY_WRITE(sc, 0, 0x00BB);
	val = (PHY_READ(sc, 4) & 0xFFF) | 0xF000;
	PHY_WRITE(sc, 4, val);
	PHY_WRITE(sc, 3, 0xDF01);
	PHY_WRITE(sc, 2, 0xDF20);
	PHY_WRITE(sc, 1, 0xFF95);
	PHY_WRITE(sc, 0, 0xBF00);
	PHY_SETBIT(sc, 4, 0x0800);
	PHY_CLRBIT(sc, 4, 0x0800);
	PHY_WRITE(sc, 31, 0x0000);
	
	DELAY(40);
}

void
rgephy_reset(struct mii_softc *sc)
{
	mii_phy_reset(sc);
	DELAY(1000);
	rgephy_load_dspcode(sc);
}
