/*	$OpenBSD: eephy.c,v 1.66 2024/06/26 01:40:49 jsg Exp $	*/
/*
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

/*
 * driver for the Marvell 88E1000 series external 1000/100/10-BT PHY.
 */

/*
 * Support added for the Marvell 88E1011 (Alaska) 1000/100/10baseTX and
 * 1000baseSX PHY.
 * Nathan Binkert <nate@openbsd.org>
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

#include <dev/mii/eephyreg.h>

#ifdef __HAVE_FDT
#include <machine/fdt.h>
#include <dev/ofw/openfirm.h>
#endif

int	eephy_match(struct device *, void *, void *);
void	eephy_attach(struct device *, struct device *, void *);

const struct cfattach eephy_ca = {
	sizeof (struct mii_softc), eephy_match, eephy_attach, mii_phy_detach
};

struct cfdriver eephy_cd = {
	NULL, "eephy", DV_DULL
};

int	eephy_service(struct mii_softc *, struct mii_data *, int);
void	eephy_status(struct mii_softc *);
void	eephy_reset(struct mii_softc *);

#ifdef __HAVE_FDT
void	eephy_fdt_reg_init(struct mii_softc *);
#endif

const struct mii_phy_funcs eephy_funcs = {
	eephy_service, eephy_status, eephy_reset,
};

static const struct mii_phydesc eephys[] = {
	{ MII_OUI_MARVELL,		MII_MODEL_MARVELL_E1000_1,
	  MII_STR_MARVELL_E1000_1 },
	{ MII_OUI_MARVELL,		MII_MODEL_MARVELL_E1000_2,
	  MII_STR_MARVELL_E1000_2 },
	{ MII_OUI_MARVELL,		MII_MODEL_MARVELL_E1000_3,
	  MII_STR_MARVELL_E1000_3 },
	{ MII_OUI_MARVELL,		MII_MODEL_MARVELL_E1000_4,
	  MII_STR_MARVELL_E1000_4 },
	{ MII_OUI_MARVELL,		MII_MODEL_MARVELL_E1000S,
	  MII_STR_MARVELL_E1000S },
	{ MII_OUI_MARVELL,		MII_MODEL_MARVELL_E1011,
	  MII_STR_MARVELL_E1011 },
	{ MII_OUI_MARVELL,		MII_MODEL_MARVELL_E1111,
	  MII_STR_MARVELL_E1111 },
	{ MII_OUI_MARVELL,		MII_MODEL_MARVELL_E1112,
	  MII_STR_MARVELL_E1112 },
	{ MII_OUI_MARVELL,		MII_MODEL_MARVELL_E1116,
	  MII_STR_MARVELL_E1116 },
	{ MII_OUI_MARVELL,		MII_MODEL_MARVELL_E1116R,
	  MII_STR_MARVELL_E1116R },
	{ MII_OUI_MARVELL,		MII_MODEL_MARVELL_E1118,
	  MII_STR_MARVELL_E1118 },
	{ MII_OUI_MARVELL,		MII_MODEL_MARVELL_E1149,
	  MII_STR_MARVELL_E1149 },
	{ MII_OUI_MARVELL,		MII_MODEL_MARVELL_E1512,
	  MII_STR_MARVELL_E1512 },
	{ MII_OUI_MARVELL,		MII_MODEL_MARVELL_E1545,
	  MII_STR_MARVELL_E1545 },
	{ MII_OUI_MARVELL,		MII_MODEL_MARVELL_E3016,
	  MII_STR_MARVELL_E3016},
	{ MII_OUI_MARVELL,		MII_MODEL_MARVELL_E3082,
	  MII_STR_MARVELL_E3082 },
	{ MII_OUI_MARVELL,		MII_MODEL_MARVELL_PHYG65G,
	  MII_STR_MARVELL_PHYG65G },
	{ MII_OUI_xxMARVELL,		MII_MODEL_xxMARVELL_E1000_5,
	  MII_STR_xxMARVELL_E1000_5 },
	{ MII_OUI_xxMARVELL,		MII_MODEL_xxMARVELL_E1000_6,
	  MII_STR_xxMARVELL_E1000_6 },
	{ MII_OUI_xxMARVELL,		MII_MODEL_xxMARVELL_E1000_7,
	  MII_STR_xxMARVELL_E1000_7 },
	{ MII_OUI_xxMARVELL,		MII_MODEL_xxMARVELL_E1111,
	  MII_STR_xxMARVELL_E1111 },

	{ 0,				0,
	  NULL },
};

int
eephy_match(struct device *parent, void *match, void *aux)
{
	struct mii_attach_args *ma = aux;

	if (mii_phy_match(ma, eephys) != NULL)
		return (10);

	return (0);
}

void
eephy_attach(struct device *parent, struct device *self, void *aux)
{
	struct mii_softc *sc = (struct mii_softc *)self;
	struct mii_attach_args *ma = aux;
	struct mii_data *mii = ma->mii_data;
	const struct mii_phydesc *mpd;
	int reg, page;

	mpd = mii_phy_match(ma, eephys);
	printf(": %s, rev. %d\n", mpd->mpd_name, MII_REV(ma->mii_id2));

	sc->mii_inst = mii->mii_instance;
	sc->mii_phy = ma->mii_phyno;
	sc->mii_funcs = &eephy_funcs;
	sc->mii_model = MII_MODEL(ma->mii_id2);
	sc->mii_pdata = mii;
	sc->mii_flags = ma->mii_flags;

	/* XXX No loopback support yet, although the hardware can do it. */
	sc->mii_flags |= MIIF_NOLOOP;

	/* Make sure page 0 is selected. */
        PHY_WRITE(sc, E1000_EADR, 0);

	/* Switch to copper-only mode if necessary. */
	if (sc->mii_model == MII_MODEL_MARVELL_E1111 &&
	    (sc->mii_flags & MIIF_HAVEFIBER) == 0) {
		/*
		 * The onboard 88E1111 PHYs on the Sun X4100 M2 come
		 * up with fiber/copper auto-selection enabled, even
		 * though the machine only has copper ports.  This
		 * makes the chip autoselect to 1000baseX, and makes
		 * it impossible to select any other media.  So
		 * disable fiber/copper autoselection.
		 */
		reg = PHY_READ(sc, E1000_ESSR);
		if ((reg & E1000_ESSR_HWCFG_MODE) == E1000_ESSR_RGMII_COPPER) {
			reg |= E1000_ESSR_DIS_FC;
			PHY_WRITE(sc, E1000_ESSR, reg);
		}
	}

	/* Switch to fiber-only mode if necessary. */
	if (sc->mii_model == MII_MODEL_MARVELL_E1112 &&
	    sc->mii_flags & MIIF_HAVEFIBER) {
		page = PHY_READ(sc, E1000_EADR);
		PHY_WRITE(sc, E1000_EADR, 2);
		reg = PHY_READ(sc, E1000_SCR);
		reg &= ~E1000_SCR_MODE_MASK;
		reg |= E1000_SCR_MODE_1000BX;
		PHY_WRITE(sc, E1000_SCR, reg);
		PHY_WRITE(sc, E1000_EADR, page);
	}

	/*
	 * GCR1 MII mode defaults to an invalid value on E1512/E1514
	 * and must be programmed with the desired mode of operation.
	 */
	if (sc->mii_model == MII_MODEL_MARVELL_E1512) {
		uint32_t mode;

		page = PHY_READ(sc, E1000_EADR);
		PHY_WRITE(sc, E1000_EADR, 18);

		reg = PHY_READ(sc, E1000_GCR1);
		mode = reg & E1000_GCR1_MODE_MASK;

		if (mode == E1000_GCR1_MODE_UNSET)
			mode = E1000_GCR1_MODE_RGMII;
		if (sc->mii_flags & MIIF_SGMII)
			mode = E1000_GCR1_MODE_SGMII;

		reg &= ~E1000_GCR1_MODE_MASK;
		reg |= E1000_GCR1_RESET | mode;
		PHY_WRITE(sc, E1000_GCR1, reg);

		PHY_WRITE(sc, E1000_EADR, page);
	}

#ifdef __HAVE_FDT
	eephy_fdt_reg_init(sc);
#endif

	PHY_RESET(sc);

	sc->mii_capabilities = PHY_READ(sc, E1000_SR) & ma->mii_capmask;
	if (sc->mii_capabilities & BMSR_EXTSTAT)
		sc->mii_extcapabilities = PHY_READ(sc, E1000_ESR);

	mii_phy_add_media(sc);

}

void
eephy_reset(struct mii_softc *sc)
{
	int reg, i;

	reg = PHY_READ(sc, E1000_CR);
	reg |= E1000_CR_RESET;
	PHY_WRITE(sc, E1000_CR, reg);
	
	for (i = 0; i < 500; i++) {
		DELAY(1);
		reg = PHY_READ(sc, E1000_CR);
		if (!(reg & E1000_CR_RESET))
			break;
	}

	/*
	 * Initialize PHY Specific Control Register.
	 */
	reg = PHY_READ(sc, E1000_SCR);

	/* Assert CRS on transmit. */
	reg |= E1000_SCR_ASSERT_CRS_ON_TX;

	/* Enable auto crossover. */
	switch (sc->mii_model) {
	case MII_MODEL_MARVELL_E3016:
	case MII_MODEL_MARVELL_E3082:
		/* Bits are in a different position.  */
		reg |= (E1000_SCR_AUTO_X_MODE >> 1);
		break;
	default:
		/* Automatic crossover causes problems for 1000baseX. */
		if (sc->mii_flags & MIIF_IS_1000X)
			reg &= ~E1000_SCR_AUTO_X_MODE;
		else
			reg |= E1000_SCR_AUTO_X_MODE;
	}

	/* Disable energy detect; only available on some models. */
	switch(sc->mii_model) {
	case MII_MODEL_MARVELL_E3016:
		reg &= ~E3000_SCR_EN_DETECT_MASK;
		break;
	case MII_MODEL_MARVELL_E1011:
	case MII_MODEL_MARVELL_E1111:
	case MII_MODEL_MARVELL_E1112:
	case MII_MODEL_MARVELL_E1512:
	case MII_MODEL_MARVELL_PHYG65G:
		reg &= ~E1000_SCR_EN_DETECT_MASK;
		break;
	}

	/* Enable scrambler if necessary. */
	if (sc->mii_model == MII_MODEL_MARVELL_E3016)
		reg &= ~E3000_SCR_SCRAMBLER_DISABLE;

	/*
	 * Store next page in the Link Partner Next Page register for
	 * compatibility with 802.3ab.
	 */
	if (sc->mii_model == MII_MODEL_MARVELL_E3016)
		reg |= E3000_SCR_REG8_NEXT_PAGE;

	PHY_WRITE(sc, E1000_SCR, reg);

	if (sc->mii_model != MII_MODEL_MARVELL_E1512 &&
	    sc->mii_model != MII_MODEL_MARVELL_E1545) {
		/* 25 MHz TX_CLK should always work. */
		reg = PHY_READ(sc, E1000_ESCR);
		reg |= E1000_ESCR_TX_CLK_25;
		PHY_WRITE(sc, E1000_ESCR, reg);
	}

	/* Configure LEDs if they were left unconfigured. */
	if (sc->mii_model == MII_MODEL_MARVELL_E3016 &&
	    PHY_READ(sc, 0x16) == 0) {
		reg = (0x0b << 8) | (0x05 << 4) | 0x04;	/* XXX */
		PHY_WRITE(sc, 0x16, reg);
	}

	/*
	 * Do a software reset for these settings to take effect.
	 */
	reg = PHY_READ(sc, E1000_CR);
	PHY_WRITE(sc, E1000_CR, reg | E1000_CR_RESET);
}

int
eephy_service(struct mii_softc *sc, struct mii_data *mii, int cmd)
{
	struct ifmedia_entry *ife = mii->mii_media.ifm_cur;
	int bmcr;

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
			bmcr = PHY_READ(sc, E1000_CR);
			PHY_WRITE(sc, E1000_CR, bmcr | E1000_CR_ISOLATE);
			return (0);
		}

		/*
		 * If the interface is not up, don't do anything.
		 */
		if ((mii->mii_ifp->if_flags & IFF_UP) == 0)
			break;

		mii_phy_setmedia(sc);

		/*
		 * If autonegotiation is not enabled, we need a
		 * software reset for the settings to take effect.
		 */
		if (IFM_SUBTYPE(ife->ifm_media) != IFM_AUTO) {
			bmcr = PHY_READ(sc, E1000_CR);
			PHY_WRITE(sc, E1000_CR, bmcr | E1000_CR_RESET);
		}
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
eephy_status(struct mii_softc *sc)
{
	struct mii_data *mii = sc->mii_pdata;
	int bmcr, gsr, ssr;

	mii->mii_media_status = IFM_AVALID;
	mii->mii_media_active = IFM_ETHER;

	bmcr = PHY_READ(sc, E1000_CR);
	ssr = PHY_READ(sc, E1000_SSR);

	if (ssr & E1000_SSR_LINK)
		mii->mii_media_status |= IFM_ACTIVE;

	if (bmcr & E1000_CR_LOOPBACK)
		mii->mii_media_active |= IFM_LOOP;

	if (!(ssr & E1000_SSR_SPD_DPLX_RESOLVED)) {
		/* Erg, still trying, I guess... */
		mii->mii_media_active |= IFM_NONE;
		return;
	}

	if (sc->mii_flags & MIIF_IS_1000X) {
		mii->mii_media_active |= IFM_1000_SX;
	} else {
		if (ssr & E1000_SSR_1000MBS)
			mii->mii_media_active |= IFM_1000_T;
		else if (ssr & E1000_SSR_100MBS)
			mii->mii_media_active |= IFM_100_TX;
		else
			mii->mii_media_active |= IFM_10_T;
	}

	if (ssr & E1000_SSR_DUPLEX)
		mii->mii_media_active |= mii_phy_flowstatus(sc) | IFM_FDX;
	else
		mii->mii_media_active |= IFM_HDX;

	if (IFM_SUBTYPE(mii->mii_media_active) == IFM_1000_T) {
		gsr = PHY_READ(sc, E1000_1GSR) | PHY_READ(sc, E1000_1GSR);
		if (gsr & E1000_1GSR_MS_CONFIG_RES)
			mii->mii_media_active |= IFM_ETH_MASTER;
	}
}

#ifdef __HAVE_FDT
void
eephy_fdt_reg_init(struct mii_softc *sc)
{
	uint32_t *prop, opage;
	int i, len;

	if (!sc->mii_pdata->mii_node)
		return;

	len = OF_getproplen(sc->mii_pdata->mii_node, "marvell,reg-init");
	if (len <= 0 || len % (4 * sizeof(uint32_t)) != 0)
		return;

	opage = PHY_READ(sc, E1000_EADR);
	prop = malloc(len, M_TEMP, M_WAITOK);
	OF_getpropintarray(sc->mii_pdata->mii_node, "marvell,reg-init",
	    prop, len);
	for (i = 0; i < len; i += 4) {
		uint32_t page = prop[i + 0];
		uint32_t reg = prop[i + 1];
		uint32_t keep = prop[i + 2];
		uint32_t set = prop[i + 3];
		uint32_t val = 0;

		PHY_WRITE(sc, E1000_EADR, page);
		if (keep) {
			val = PHY_READ(sc, reg);
			val &= keep;
		}
		val |= set;
		PHY_WRITE(sc, reg, val);
	}
	free(prop, M_TEMP, len);
	PHY_WRITE(sc, E1000_EADR, opage);
}
#endif
