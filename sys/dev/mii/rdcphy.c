/*	$OpenBSD: rdcphy.c,v 1.5 2022/04/19 03:26:33 kevlo Exp $	*/
/*-
 * Copyright (c) 2010, Pyun YongHyeon <yongari@FreeBSD.org>
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
 * Driver for the RDC Semiconductor R6040 10/100 PHY.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/device.h>
#include <sys/socket.h>

#include <net/if.h>
#include <net/if_var.h>
#include <net/if_media.h>

#include <dev/mii/mii.h>
#include <dev/mii/miivar.h>
#include <dev/mii/miidevs.h>

#define	MII_RDCPHY_DEBUG	0x11
#define	DEBUG_JABBER_DIS	0x0040
#define	DEBUG_LOOP_BACK_10MBPS	0x0400

#define	MII_RDCPHY_CTRL		0x14
#define	CTRL_SQE_ENB		0x0100
#define	CTRL_NEG_POLARITY	0x0400
#define	CTRL_AUTO_POLARITY	0x0800
#define	CTRL_MDIXSEL_RX		0x2000
#define	CTRL_MDIXSEL_TX		0x4000
#define	CTRL_AUTO_MDIX_DIS	0x8000

#define	MII_RDCPHY_CTRL2	0x15
#define	CTRL2_LED_DUPLEX	0x0000
#define	CTRL2_LED_DUPLEX_COL	0x0008
#define	CTRL2_LED_ACT		0x0010
#define	CTRL2_LED_SPEED_ACT	0x0018
#define	CTRL2_LED_BLK_100MBPS_DIS	0x0020
#define	CTRL2_LED_BLK_10MBPS_DIS	0x0040
#define	CTRL2_LED_BLK_LINK_ACT_DIS	0x0080
#define	CTRL2_SDT_THRESH_MASK	0x3E00
#define	CTRL2_TIMING_ERR_SEL	0x4000
#define	CTRL2_LED_BLK_80MS	0x8000
#define	CTRL2_LED_BLK_160MS	0x0000
#define	CTRL2_LED_MASK		0x0018

#define	MII_RDCPHY_STATUS	0x16
#define	STATUS_AUTO_MDIX_RX	0x0200
#define	STATUS_AUTO_MDIX_TX	0x0400
#define	STATUS_NEG_POLARITY	0x0800
#define	STATUS_FULL_DUPLEX	0x1000
#define	STATUS_SPEED_10		0x0000
#define	STATUS_SPEED_100	0x2000
#define	STATUS_SPEED_MASK	0x6000
#define	STATUS_LINK_UP		0x8000

/* Analog test register 2 */
#define	MII_RDCPHY_TEST2	0x1A
#define	TEST2_PWR_DOWN		0x0200

struct rdcphy_softc {
	struct mii_softc sc_mii;
	int mii_link_tick;
#define	RDCPHY_MANNEG_TICK	3
};

int	rdcphy_service(struct mii_softc *, struct mii_data *, int);
void	rdcphy_attach(struct device *, struct device *, void *);
int	rdcphy_match(struct device *, void *, void *);
void	rdcphy_status(struct mii_softc *);

const struct mii_phy_funcs rdcphy_funcs = {
	rdcphy_service, rdcphy_status, mii_phy_reset,
};

static const struct mii_phydesc rdcphys[] = {
	{ MII_OUI_RDC,		MII_MODEL_RDC_R6040,
	  MII_STR_RDC_R6040 },
	{ MII_OUI_RDC,		MII_MODEL_RDC_R6040_2,
	  MII_STR_RDC_R6040_2 },
	{ 0,			0,
	  NULL },
};

const struct cfattach rdcphy_ca = {
	sizeof(struct rdcphy_softc), rdcphy_match, rdcphy_attach,
	mii_phy_detach
};

struct cfdriver rdcphy_cd = {
	NULL, "rdcphy", DV_DULL
};

int
rdcphy_match(struct device *parent, void *match, void *aux)
{
	struct mii_attach_args *ma = aux;

	if (mii_phy_match(ma, rdcphys) != NULL)
		return (10);

	return (0);
}

void
rdcphy_attach(struct device *parent, struct device *self, void *aux)
{
	struct rdcphy_softc *sc = (struct rdcphy_softc *)self;
	struct mii_attach_args *ma = aux;
	struct mii_data *mii = ma->mii_data;
	const struct mii_phydesc *mpd;

	mpd = mii_phy_match(ma, rdcphys);
	printf(": %s, rev. %d\n", mpd->mpd_name, MII_REV(ma->mii_id2));

	sc->sc_mii.mii_inst = mii->mii_instance;
	sc->sc_mii.mii_phy = ma->mii_phyno;
	sc->sc_mii.mii_funcs = &rdcphy_funcs;
	sc->sc_mii.mii_pdata = mii;
	sc->sc_mii.mii_flags = ma->mii_flags;

	PHY_RESET(&sc->sc_mii);

	sc->sc_mii.mii_capabilities = 
	    PHY_READ(&sc->sc_mii, MII_BMSR) & ma->mii_capmask;
	if (sc->sc_mii.mii_capabilities & BMSR_EXTSTAT)
		sc->sc_mii.mii_extcapabilities = 
		    PHY_READ(&sc->sc_mii, MII_EXTSR);

	if (sc->sc_mii.mii_capabilities & BMSR_MEDIAMASK)
		mii_phy_add_media(&sc->sc_mii);
}

int
rdcphy_service(struct mii_softc *self, struct mii_data *mii, int cmd)
{
	struct rdcphy_softc *sc = (struct rdcphy_softc *)self;
	struct ifmedia_entry *ife = mii->mii_media.ifm_cur;
	int reg;

	if ((sc->sc_mii.mii_dev.dv_flags & DVF_ACTIVE) == 0)
		return (ENXIO);

	switch (cmd) {
	case MII_POLLSTAT:
		/*
		 * If we're not polling our PHY instance, just return.
		 */
		if (IFM_INST(ife->ifm_media) != sc->sc_mii.mii_inst)
			return (0);
		break;

	case MII_MEDIACHG:
		/*
		 * If the media indicates a different PHY instance,
		 * isolate ourselves.
		 */
		if (IFM_INST(ife->ifm_media) != sc->sc_mii.mii_inst) {
			reg = PHY_READ(&sc->sc_mii, MII_BMCR);
			PHY_WRITE(&sc->sc_mii, MII_BMCR, reg | BMCR_ISO);
			return (0);
		}

		/*
		 * If the interface is not up, don't do anything.
		 */
		if ((mii->mii_ifp->if_flags & IFF_UP) == 0)
			break;

		mii_phy_setmedia(&sc->sc_mii);
		switch (IFM_SUBTYPE(ife->ifm_media)) {
		case IFM_100_TX:
		case IFM_10_T:
			/*
			 * Report fake lost link event to parent
			 * driver.  This will stop MAC of parent
			 * driver and make it possible to reconfigure
			 * MAC after completion of link establishment.
			 * Note, the parent MAC seems to require
			 * restarting MAC when underlying any PHY
			 * configuration was changed even if the
			 * resolved speed/duplex was not changed at
			 * all.
			 */
			mii->mii_media_status = 0;
			mii->mii_media_active = IFM_ETHER | IFM_NONE;
			sc->mii_link_tick = RDCPHY_MANNEG_TICK;
			/* Immediately report link down. */
			mii_phy_update(&sc->sc_mii, MII_MEDIACHG);
			return (0);
		default:
			break;
		}
		break;

	case MII_TICK:
		/*
		 * If we're not currently selected, just return.
		 */
		if (IFM_INST(ife->ifm_media) != sc->sc_mii.mii_inst)
			return (0);

		if (mii_phy_tick(&sc->sc_mii) == EJUSTRETURN)
			return (0);

		if (IFM_SUBTYPE(ife->ifm_media) != IFM_AUTO) {
			/*
			 * It seems the PHY hardware does not correctly
			 * report link status changes when manual link
			 * configuration is in progress.  It is also
			 * possible for the PHY to complete establishing
			 * a link within one second such that mii(4)
			 * did not notice the link change.  To workaround
			 * the issue, emulate lost link event and wait
			 * for 3 seconds when manual link configuration
			 * is in progress.  3 seconds would be long
			 * enough to absorb transient link flips.
			 */
			if (sc->mii_link_tick > 0) {
				sc->mii_link_tick--;
				return (0);
			}
		}
		break;
	}

	/* Update the media status. */
	mii_phy_status(&sc->sc_mii);

	/* Callback if something changed. */
	mii_phy_update(&sc->sc_mii, cmd);
	return (0);
}

void
rdcphy_status(struct mii_softc *sc)
{
	struct mii_data *mii = sc->mii_pdata;
	struct ifmedia_entry *ife;
	int bmsr, bmcr, physts;

	ife = mii->mii_media.ifm_cur;

	mii->mii_media_status = IFM_AVALID;
	mii->mii_media_active = IFM_ETHER;

	bmsr = PHY_READ(sc, MII_BMSR) | PHY_READ(sc, MII_BMSR);
	physts = PHY_READ(sc, MII_RDCPHY_STATUS);

	if (physts & STATUS_LINK_UP)
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
		if (!(bmsr & BMSR_ACOMP)) {
			/* Erg, still trying, I guess... */
			mii->mii_media_active |= IFM_NONE;
			return;
		}
	}

	switch (physts & STATUS_SPEED_MASK) {
	case STATUS_SPEED_100:
		mii->mii_media_active |= IFM_100_TX;
		break;
	case STATUS_SPEED_10:
		mii->mii_media_active |= IFM_10_T;
		break;
	default:
		mii->mii_media_active |= IFM_NONE;
		return;
	}
	if ((physts & STATUS_FULL_DUPLEX) != 0)
		mii->mii_media_active |= IFM_FDX | 
		    mii_phy_flowstatus(sc);
	else
		mii->mii_media_active |= IFM_HDX;
}
