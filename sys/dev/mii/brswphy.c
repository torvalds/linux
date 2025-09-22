/*	$OpenBSD: brswphy.c,v 1.5 2024/05/13 01:15:51 jsg Exp $	*/

/*
 * Copyright (c) 2014 Paul Irofti <paul@irofti.net>
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

/* Copyright (C) 2011-2013 Jonas Gorski <jogo@openwrt.org>
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

/*-
 * Copyright (c) 1998, 1999 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Jason R. Thorpe of the Numerical Aerospace Simulation Facility,
 * NASA Ames Research Center, and by Frank van der Linden.
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

/*
 * Copyright (c) 1997 Manuel Bouyer.  All rights reserved.
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
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/device.h>
#include <sys/socket.h>
#include <sys/errno.h>

#include <net/if.h>
#include <net/if_var.h>
#include <net/if_media.h>

#include <dev/mii/mii.h>
#include <dev/mii/miivar.h>
#include <dev/mii/miidevs.h>

#define BRSW_PSEUDO_PHY	0x1e /* Register Access Pseudo PHY */

/* MII registers */
#define REG_MII_PAGE    0x10    /* MII Page register */
#define REG_MII_ADDR    0x11    /* MII Address register */
#define REG_MII_DATA0   0x18    /* MII Data register 0 */
#define REG_MII_DATA1   0x19    /* MII Data register 1 */
#define REG_MII_DATA2   0x1a    /* MII Data register 2 */
#define REG_MII_DATA3   0x1b    /* MII Data register 3 */

#define REG_MII_PAGE_ENABLE     1
#define REG_MII_ADDR_WRITE      1
#define REG_MII_ADDR_READ       2

/* Management Port (SMP) Page offsets */
#define BRSW_STAT_PAGE                   0x01 /* Status */
/* Link Status Summary Register (16bit) */
#define BRSW_LINK_STAT                   0x00

/* Duplex Status Summary (16 bit) */
#define BRSW_DUPLEX_STAT_FE              0x06
#define BRSW_DUPLEX_STAT_GE              0x08
#define BRSW_DUPLEX_STAT_63XX            0x0c

/* Port Speed Summary Register (16 bit for FE, 32 bit for GE) */
#define BRSW_SPEED_STAT			0x04
#define SPEED_PORT_FE(reg, port)	(((reg) >> (port)) & 1)
#define SPEED_PORT_GE(reg, port)	(((reg) >> 2 * (port)) & 3)
#define SPEED_STAT_10M			0
#define SPEED_STAT_100M			1
#define SPEED_STAT_1000M		2

#define BRSW_CPU_PORT    8

#define	BRSW_PHY_READ(p, r) \
	(*(p)->mii_pdata->mii_readreg)((p)->mii_dev.dv_parent, \
	    BRSW_PSEUDO_PHY, (r))
#define	BRSW_PHY_WRITE(p, r, v) \
	(*(p)->mii_pdata->mii_writereg)((p)->mii_dev.dv_parent, \
	    BRSW_PSEUDO_PHY, (r), (v))

struct brswphy_softc {
	struct mii_softc	sc_mii; /* common mii device part */

	uint8_t sc_current_page;
};

int	brswphymatch(struct device *, void *, void *);
void	brswphyattach(struct device *, struct device *, void *);

const struct cfattach brswphy_ca = { sizeof(struct brswphy_softc),
	brswphymatch, brswphyattach, mii_phy_detach,
};

struct cfdriver brswphy_cd = {
	NULL, "brswphy", DV_DULL
};

int	brswphy_service(struct mii_softc *, struct mii_data *, int);
void	brswphy_status(struct mii_softc *);

const struct mii_phy_funcs brswphy_funcs = {
	brswphy_service, brswphy_status, mii_phy_reset,
};

static int brswphy_read16(struct mii_softc *sc, uint8_t page, uint8_t reg,
    uint16_t *val);
static int brswphy_read32(struct mii_softc *sc, uint8_t page, uint8_t reg,
    uint32_t *val);
static int brswphy_op(struct mii_softc *sc, uint8_t page, uint8_t reg,
    uint16_t op);

static const struct mii_phydesc brswphys[] = {
	{ MII_OUI_xxBROADCOM2,		MII_MODEL_xxBROADCOM2_BCM53115,
	  MII_STR_xxBROADCOM2_BCM53115 },

	{ 0,			0,
	  NULL },
};

int
brswphymatch(struct device *parent, void *match, void *aux)
{
	struct mii_attach_args *ma = aux;

	if (mii_phy_match(ma, brswphys) != NULL)
		return (10);

	return (0);
}

void
brswphyattach(struct device *parent, struct device *self, void *aux)
{
	struct brswphy_softc *bsc = (struct brswphy_softc *)self;
	struct mii_softc *sc = &bsc->sc_mii;
	struct mii_attach_args *ma = aux;
	struct mii_data *mii = ma->mii_data;
	const struct mii_phydesc *mpd;

	mpd = mii_phy_match(ma, brswphys);
	printf(": %s, rev. %d\n", mpd->mpd_name, MII_REV(ma->mii_id2));

	sc->mii_inst = mii->mii_instance;
	sc->mii_phy = ma->mii_phyno;
	sc->mii_funcs = &brswphy_funcs;
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

	PHY_RESET(sc);

	bsc->sc_current_page = 0xff;
}

int
brswphy_service(struct mii_softc *sc, struct mii_data *mii, int cmd)
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
brswphy_status(struct mii_softc *sc)
{
	struct mii_data *mii = sc->mii_pdata;
	uint32_t speed;
	uint16_t link, duplex;

	mii->mii_media_status = IFM_AVALID;
	mii->mii_media_active = IFM_ETHER;

	/* XXX: check that this is the CPU port when switch support arrives */

	brswphy_read16(sc, BRSW_STAT_PAGE, BRSW_LINK_STAT, &link);
	if ((link >> BRSW_CPU_PORT) & 1)
		mii->mii_media_status |= IFM_ACTIVE;

	brswphy_read16(sc, BRSW_STAT_PAGE, BRSW_DUPLEX_STAT_GE, &duplex);
	duplex = (duplex >> BRSW_CPU_PORT) & 1;

	brswphy_read32(sc, BRSW_STAT_PAGE, BRSW_SPEED_STAT, &speed);
	speed = SPEED_PORT_GE(speed, BRSW_CPU_PORT);
	switch (speed) {
	case SPEED_STAT_10M:
		mii->mii_media_active |= IFM_10_T;
		break;
	case SPEED_STAT_100M:
		mii->mii_media_active |= IFM_100_TX;
		break;
	case SPEED_STAT_1000M:
		mii->mii_media_active |= IFM_1000_T;
		break;
	}

	if (duplex)
		mii->mii_media_active |= IFM_FDX;
	else
		mii->mii_media_active |= IFM_HDX;

	mii->mii_media_active |= IFM_ETH_MASTER;
}

static int
brswphy_op(struct mii_softc *sc, uint8_t page, uint8_t reg, uint16_t op)
{
	struct brswphy_softc *bsc = (struct brswphy_softc *)sc;
	int i;
	uint16_t v;

	if (bsc->sc_current_page != page) {
		/* set page number */
		v = (page << 8) | REG_MII_PAGE_ENABLE;
		BRSW_PHY_WRITE(sc, REG_MII_PAGE, v);
		bsc->sc_current_page = page;
	}

	/* set register address */
	v = (reg << 8) | op;
	BRSW_PHY_WRITE(sc, REG_MII_ADDR, v);

	/* check if operation completed */
	for (i = 0; i < 5; ++i) {
		v = BRSW_PHY_READ(sc, REG_MII_ADDR);
		if (!(v & (REG_MII_ADDR_WRITE | REG_MII_ADDR_READ)))
			break;
		delay(10);
	}

	if (i == 5)
		return -EIO;

	return 0;
}

static int
brswphy_read16(struct mii_softc *sc, uint8_t page, uint8_t reg, uint16_t *val)
{
	int ret;

	ret = brswphy_op(sc, page, reg, REG_MII_ADDR_READ);
	if (ret)
		return ret;

	*val = BRSW_PHY_READ(sc, REG_MII_DATA0);

	return 0;
}

static int
brswphy_read32(struct mii_softc *sc, uint8_t page, uint8_t reg, uint32_t *val)
{
	int ret;

	ret = brswphy_op(sc, page, reg, REG_MII_ADDR_READ);
	if (ret)
		return ret;

	*val = BRSW_PHY_READ(sc, REG_MII_DATA0);
	*val |= BRSW_PHY_READ(sc, REG_MII_DATA1) << 16;

	return 0;
}
