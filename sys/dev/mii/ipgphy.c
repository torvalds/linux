/*	$OpenBSD: ipgphy.c,v 1.20 2022/04/06 18:59:29 naddy Exp $	*/

/*-
 * Copyright (c) 2006, Pyun YongHyeon <yongari@FreeBSD.org>
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
 */

/*
 * Driver for the IC Plus IP1000A/IP1001 10/100/1000 PHY.
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

#include <dev/mii/ipgphyreg.h>

#include <dev/pci/if_stgereg.h>

int ipgphy_probe(struct device *, void *, void *);
void ipgphy_attach(struct device *, struct device *, void *);

const struct cfattach ipgphy_ca = {
	sizeof(struct mii_softc), ipgphy_probe, ipgphy_attach, mii_phy_detach
};

struct cfdriver ipgphy_cd = {
	NULL, "ipgphy", DV_DULL
};

int	ipgphy_service(struct mii_softc *, struct mii_data *, int);
void	ipgphy_status(struct mii_softc *);
int	ipgphy_mii_phy_auto(struct mii_softc *);
void	ipgphy_load_dspcode(struct mii_softc *);
void	ipgphy_reset(struct mii_softc *);

const struct mii_phy_funcs ipgphy_funcs = {
	ipgphy_service, ipgphy_status, ipgphy_reset,
};

static const struct mii_phydesc ipgphys[] = {
	{ MII_OUI_ICPLUS,		MII_MODEL_ICPLUS_IP1000A,
	  MII_STR_ICPLUS_IP1000A },
	{ MII_OUI_ICPLUS,		MII_MODEL_ICPLUS_IP1001,
	  MII_STR_ICPLUS_IP1001 },

	{ 0,
	  0 },
};

int
ipgphy_probe(struct device *parent, void *match, void *aux)
{
	struct mii_attach_args *ma = aux;

	if (mii_phy_match(ma, ipgphys) != NULL)
		return (10);

	return (0);
}

void
ipgphy_attach(struct device *parent, struct device *self, void *aux)
{
	struct mii_softc *sc = (struct mii_softc *)self;
	struct mii_attach_args *ma = aux;
	struct mii_data *mii = ma->mii_data;
	const struct mii_phydesc *mpd;

	mpd = mii_phy_match(ma, ipgphys);
	printf(": %s, rev. %d\n", mpd->mpd_name, MII_REV(ma->mii_id2));

	sc->mii_inst = mii->mii_instance;
	sc->mii_phy = ma->mii_phyno;
	sc->mii_funcs = &ipgphy_funcs;
	sc->mii_model = MII_MODEL(ma->mii_id2);
	sc->mii_pdata = mii;
	sc->mii_flags = ma->mii_flags;

	sc->mii_flags |= MIIF_NOISOLATE;

	PHY_RESET(sc);

	sc->mii_capabilities = PHY_READ(sc, MII_BMSR) & ma->mii_capmask;
	if (sc->mii_capabilities & BMSR_EXTSTAT)
		sc->mii_extcapabilities = PHY_READ(sc, MII_EXTSR);
 
	mii_phy_add_media(sc);

}

int
ipgphy_service(struct mii_softc *sc, struct mii_data *mii, int cmd)
{
	struct ifmedia_entry *ife = mii->mii_media.ifm_cur;
	uint32_t gig, reg, speed;

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

		PHY_RESET(sc);

		switch (IFM_SUBTYPE(ife->ifm_media)) {
		case IFM_AUTO:
			(void)ipgphy_mii_phy_auto(sc);
			goto done;
			break;

		case IFM_1000_T:
			/*
			 * XXX
			 * Manual 1000baseT setting doesn't seem to work.
			 */
			speed = BMCR_S1000;
			break;

		case IFM_100_TX:
			speed = BMCR_S100;
			break;

		case IFM_10_T:
			speed = BMCR_S10;
			break;

		default:
			return (EINVAL);
		}

		if (((ife->ifm_media & IFM_GMASK) & IFM_FDX) != 0) {
			speed |= BMCR_FDX;
			gig = GTCR_ADV_1000TFDX;
		} else
			gig = GTCR_ADV_1000THDX;

		PHY_WRITE(sc, MII_100T2CR, 0);
		PHY_WRITE(sc, MII_BMCR, speed);

		if (IFM_SUBTYPE(ife->ifm_media) != IFM_1000_T)
			break;

		PHY_WRITE(sc, MII_100T2CR, gig);
		PHY_WRITE(sc, MII_BMCR, speed);

		if (mii->mii_media.ifm_media & IFM_ETH_MASTER)
			gig |= GTCR_MAN_MS | GTCR_ADV_MS;

		PHY_WRITE(sc, MII_100T2CR, gig);

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
		 * check for link.
		 */
		reg = PHY_READ(sc, MII_BMSR) | PHY_READ(sc, MII_BMSR);
		if (reg & BMSR_LINK) {
			sc->mii_ticks = 0;
			break;
		}

		/* Announce link loss right after it happens */
		if (sc->mii_ticks++ == 0)
			break;

		/*
		 * Only retry autonegotiation every mii_anegticks seconds.
		 */
		if (sc->mii_ticks <= sc->mii_anegticks)
			break;

		sc->mii_ticks = 0;
		ipgphy_mii_phy_auto(sc);
		break;
	}

	/* Update the media status. */
	mii_phy_status(sc);

	/* Callback if something changed. */
	mii_phy_update(sc, cmd);
	return (0);
}

void
ipgphy_status(struct mii_softc *sc)
{
	struct mii_data *mii = sc->mii_pdata;
	struct ifmedia_entry *ife = mii->mii_media.ifm_cur;
	uint32_t bmsr, bmcr, stat;

	mii->mii_media_status = IFM_AVALID;
	mii->mii_media_active = IFM_ETHER;

	bmsr = PHY_READ(sc, MII_BMSR) | PHY_READ(sc, MII_BMSR);
	if (bmsr & BMSR_LINK) 
		mii->mii_media_status |= IFM_ACTIVE;

	bmcr = PHY_READ(sc, MII_BMCR);
	if (bmcr & BMCR_LOOP)
		mii->mii_media_active |= IFM_LOOP;

	if (bmcr & BMCR_AUTOEN) {
		if ((bmsr & BMSR_ACOMP) == 0) {
			/* Erg, still trying, I guess... */
			mii->mii_media_active |= IFM_NONE;
			return;
		}

		if (sc->mii_model == MII_MODEL_ICPLUS_IP1001) {
			stat = PHY_READ(sc, IPGPHY_LSR);
			switch (stat & IPGPHY_LSR_SPEED_MASK) {
			case IPGPHY_LSR_SPEED_10:
				mii->mii_media_active |= IFM_10_T;
				break;
			case IPGPHY_LSR_SPEED_100:
				mii->mii_media_active |= IFM_100_TX;
				break;
			case IPGPHY_LSR_SPEED_1000:
				mii->mii_media_active |= IFM_1000_T;
				break;
			default:
				mii->mii_media_active |= IFM_NONE;
				return;
			}

			if (stat & IPGPHY_LSR_FULL_DUPLEX)
				mii->mii_media_active |= IFM_FDX;
			else
				mii->mii_media_active |= IFM_HDX;
		} else {
			stat = PHY_READ(sc, STGE_PhyCtrl);
			switch (PC_LinkSpeed(stat)) {
			case PC_LinkSpeed_Down:
				mii->mii_media_active |= IFM_NONE;
				return;
			case PC_LinkSpeed_10:
				mii->mii_media_active |= IFM_10_T;
				break;
			case PC_LinkSpeed_100:
				mii->mii_media_active |= IFM_100_TX;
				break;
			case PC_LinkSpeed_1000:
				mii->mii_media_active |= IFM_1000_T;
				break;
			default:
				mii->mii_media_active |= IFM_NONE;
				return;
			}

			if (stat & PC_PhyDuplexStatus)
				mii->mii_media_active |= IFM_FDX;
			else
				mii->mii_media_active |= IFM_HDX;
		}

		if (mii->mii_media_active & IFM_FDX)
			mii->mii_media_active |= mii_phy_flowstatus(sc);

		if (IFM_SUBTYPE(mii->mii_media_active) == IFM_1000_T) {
			if (PHY_READ(sc, MII_100T2SR) & GTSR_MS_RES)
				mii->mii_media_active |= IFM_ETH_MASTER;
		}
	} else
		mii->mii_media_active = ife->ifm_media;
}

int
ipgphy_mii_phy_auto(struct mii_softc *sc)
{
	uint32_t reg = 0;

	if (sc->mii_model == MII_MODEL_ICPLUS_IP1001) {
		reg = PHY_READ(sc, MII_ANAR);
		reg &= ~(ANAR_PAUSE_SYM | ANAR_PAUSE_ASYM);
		reg |= ANAR_NP;
	}

	reg |= ANAR_10 | ANAR_10_FD | ANAR_TX | ANAR_TX_FD;

	if (sc->mii_flags & MIIF_DOPAUSE)
		reg |= ANAR_PAUSE_SYM | ANAR_PAUSE_ASYM;

	PHY_WRITE(sc, MII_ANAR, reg | ANAR_CSMA);

	reg = GTCR_ADV_1000TFDX | GTCR_ADV_1000THDX;
	if (sc->mii_model != MII_MODEL_ICPLUS_IP1001)
		reg |= GTCR_ADV_MS;
	PHY_WRITE(sc, MII_100T2CR, reg);

	PHY_WRITE(sc, MII_BMCR, (BMCR_FDX | BMCR_AUTOEN | BMCR_STARTNEG));

	return (EJUSTRETURN);
}

void
ipgphy_load_dspcode(struct mii_softc *sc)
{
	PHY_WRITE(sc, 31, 0x0001);
	PHY_WRITE(sc, 27, 0x01e0);
	PHY_WRITE(sc, 31, 0x0002);
	PHY_WRITE(sc, 27, 0xeb8e);
	PHY_WRITE(sc, 31, 0x0000);
	PHY_WRITE(sc, 30, 0x005e);
	PHY_WRITE(sc, 9, 0x0700);

	DELAY(50);
}

void
ipgphy_reset(struct mii_softc *sc)
{
	struct ifnet *ifp = sc->mii_pdata->mii_ifp;
	struct stge_softc *stge_sc;
	uint32_t reg;

	mii_phy_reset(sc);

	/* clear autoneg/full-duplex as we don't want it after reset */
	reg = PHY_READ(sc, MII_BMCR);
	reg &= ~(BMCR_AUTOEN | BMCR_FDX);
	PHY_WRITE(sc, MII_BMCR, reg);

	if (sc->mii_model == MII_MODEL_ICPLUS_IP1000A &&
	    strcmp(ifp->if_xname, "stge") == 0) {
		stge_sc = ifp->if_softc;
		if (stge_sc->sc_rev >= 0x40 && stge_sc->sc_rev <= 0x4e)
			ipgphy_load_dspcode(sc);
	}
}
