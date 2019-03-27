/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2007 The DragonFly Project.  All rights reserved.
 * 
 * This code is derived from software contributed to The DragonFly Project
 * by Sepherosa Ziehau <sepherosa@gmail.com>
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 * 3. Neither the name of The DragonFly Project nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific, prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
 * COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 * 
 * $DragonFly: src/sys/dev/netif/mii_layer/truephy.c,v 1.3 2008/02/10 07:29:27 sephe Exp $
 * $FreeBSD$
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/socket.h>
#include <sys/errno.h>
#include <sys/module.h>
#include <sys/bus.h>

#include <net/if.h>
#include <net/if_var.h>
#include <net/if_media.h>
#include <net/if_arp.h>
#include <net/ethernet.h>
#include <net/if_vlan_var.h>

#include <dev/mii/mii.h>
#include <dev/mii/miivar.h>
#include "miidevs.h"

#include <dev/mii/truephyreg.h>

#include "miibus_if.h"

#define	TRUEPHY_FRAMELEN(mtu)	\
    (ETHER_HDR_LEN + ETHER_VLAN_ENCAP_LEN + (mtu) + ETHER_CRC_LEN)

static int	truephy_service(struct mii_softc *, struct mii_data *, int);
static int	truephy_attach(device_t);
static int	truephy_probe(device_t);
static void	truephy_reset(struct mii_softc *);
static void	truephy_status(struct mii_softc *);

static device_method_t truephy_methods[] = {
	/* device interface */
	DEVMETHOD(device_probe,		truephy_probe),
	DEVMETHOD(device_attach,	truephy_attach),
	DEVMETHOD(device_detach,	mii_phy_detach),
	DEVMETHOD(device_shutdown,	bus_generic_shutdown),
	DEVMETHOD_END
};

static const struct mii_phydesc truephys[] = {
	MII_PHY_DESC(AGERE,	ET1011),
	MII_PHY_DESC(AGERE,	ET1011C),
	MII_PHY_END
};

static devclass_t truephy_devclass;

static driver_t truephy_driver = {
	"truephy",
	truephy_methods,
	sizeof(struct mii_softc)
};

DRIVER_MODULE(truephy, miibus, truephy_driver, truephy_devclass, 0, 0);

static const struct mii_phy_funcs truephy_funcs = {
	truephy_service,
	truephy_status,
	truephy_reset
};

static const struct truephy_dsp {
	uint16_t	index;
	uint16_t	data;
} truephy_dspcode[] = {
	{ 0x880b,	0x0926 },	/* AfeIfCreg4B1000Msbs */
	{ 0x880c,	0x0926 },	/* AfeIfCreg4B100Msbs */
	{ 0x880d,	0x0926 },	/* AfeIfCreg4B10Msbs */

	{ 0x880e,	0xb4d3 },	/* AfeIfCreg4B1000Lsbs */
	{ 0x880f,	0xb4d3 },	/* AfeIfCreg4B100Lsbs */
	{ 0x8810,	0xb4d3 },	/* AfeIfCreg4B10Lsbs */

	{ 0x8805,	0xb03e },	/* AfeIfCreg3B1000Msbs */
	{ 0x8806,	0xb03e },	/* AfeIfCreg3B100Msbs */
	{ 0x8807,	0xff00 },	/* AfeIfCreg3B10Msbs */

	{ 0x8808,	0xe090 },	/* AfeIfCreg3B1000Lsbs */
	{ 0x8809,	0xe110 },	/* AfeIfCreg3B100Lsbs */
	{ 0x880a,	0x0000 },	/* AfeIfCreg3B10Lsbs */

	{ 0x300d,	1      },	/* DisableNorm */

	{ 0x280c,	0x0180 },	/* LinkHoldEnd */

	{ 0x1c21,	0x0002 },	/* AlphaM */

	{ 0x3821,	6      },	/* FfeLkgTx0 */
	{ 0x381d,	1      },	/* FfeLkg1g4 */
	{ 0x381e,	1      },	/* FfeLkg1g5 */
	{ 0x381f,	1      },	/* FfeLkg1g6 */
	{ 0x3820,	1      },	/* FfeLkg1g7 */

	{ 0x8402,	0x01f0 },	/* Btinact */
	{ 0x800e,	20     },	/* LftrainTime */
	{ 0x800f,	24     },	/* DvguardTime */
	{ 0x8010,	46     }	/* IdlguardTime */
};

static int
truephy_probe(device_t dev)
{

	return (mii_phy_dev_probe(dev, truephys, BUS_PROBE_DEFAULT));
}

static int
truephy_attach(device_t dev)
{
	struct mii_softc *sc;

	sc = device_get_softc(dev);

	mii_phy_dev_attach(dev, MIIF_NOISOLATE | MIIF_NOMANPAUSE,
	   &truephy_funcs, 0);

	PHY_RESET(sc);

	sc->mii_capabilities = PHY_READ(sc, MII_BMSR) & sc->mii_capmask;
	if (sc->mii_capabilities & BMSR_EXTSTAT) {
		sc->mii_extcapabilities = PHY_READ(sc, MII_EXTSR);
		/* No 1000baseT half-duplex support */
		sc->mii_extcapabilities &= ~EXTSR_1000THDX;
	}

	device_printf(dev, " ");
	mii_phy_add_media(sc);
	printf("\n");

	MIIBUS_MEDIAINIT(sc->mii_dev);
	return (0);
}

static int
truephy_service(struct mii_softc *sc, struct mii_data *mii, int cmd)
{
	struct ifmedia_entry *ife = mii->mii_media.ifm_cur;
	int bmcr;

	switch (cmd) {
	case MII_POLLSTAT:
		break;

	case MII_MEDIACHG:
		if (IFM_SUBTYPE(ife->ifm_media) != IFM_AUTO) {
			bmcr = PHY_READ(sc, MII_BMCR) & ~BMCR_AUTOEN;
			PHY_WRITE(sc, MII_BMCR, bmcr);
			PHY_WRITE(sc, MII_BMCR, bmcr | BMCR_PDOWN);
		}

		mii_phy_setmedia(sc);

		if (IFM_SUBTYPE(ife->ifm_media) != IFM_AUTO) {
			bmcr = PHY_READ(sc, MII_BMCR) & ~BMCR_PDOWN;
			PHY_WRITE(sc, MII_BMCR, bmcr);

			if (IFM_SUBTYPE(ife->ifm_media) == IFM_1000_T) {
				PHY_WRITE(sc, MII_BMCR,
				    bmcr | BMCR_AUTOEN | BMCR_STARTNEG);
			}
		}
		break;

	case MII_TICK:
		if (mii_phy_tick(sc) == EJUSTRETURN)
			return (0);
		break;
	}

	/* Update the media status. */
	PHY_STATUS(sc);

	/* Callback if something changed. */
	mii_phy_update(sc, cmd);
	return (0);
}

static void
truephy_reset(struct mii_softc *sc)
{
	int i;

	if (sc->mii_mpd_model == MII_MODEL_AGERE_ET1011) {
		mii_phy_reset(sc);
		return;
	}

	for (i = 0; i < 2; ++i) {
		PHY_READ(sc, MII_PHYIDR1);
		PHY_READ(sc, MII_PHYIDR2);

		PHY_READ(sc, TRUEPHY_CTRL);
		PHY_WRITE(sc, TRUEPHY_CTRL,
			  TRUEPHY_CTRL_DIAG | TRUEPHY_CTRL_RSV1);

		PHY_WRITE(sc, TRUEPHY_INDEX, TRUEPHY_INDEX_MAGIC);
		PHY_READ(sc, TRUEPHY_DATA);

		PHY_WRITE(sc, TRUEPHY_CTRL, TRUEPHY_CTRL_RSV1);
	}

	PHY_READ(sc, MII_BMCR);
	PHY_READ(sc, TRUEPHY_CTRL);
	PHY_WRITE(sc, MII_BMCR, BMCR_AUTOEN | BMCR_PDOWN | BMCR_S1000);
	PHY_WRITE(sc, TRUEPHY_CTRL,
		  TRUEPHY_CTRL_DIAG | TRUEPHY_CTRL_RSV1 | TRUEPHY_CTRL_RSV0);

	for (i = 0; i < nitems(truephy_dspcode); ++i) {
		const struct truephy_dsp *dsp = &truephy_dspcode[i];

		PHY_WRITE(sc, TRUEPHY_INDEX, dsp->index);
		PHY_WRITE(sc, TRUEPHY_DATA, dsp->data);

		PHY_WRITE(sc, TRUEPHY_INDEX, dsp->index);
		PHY_READ(sc, TRUEPHY_DATA);
	}

	PHY_READ(sc, MII_BMCR);
	PHY_READ(sc, TRUEPHY_CTRL);
	PHY_WRITE(sc, MII_BMCR, BMCR_AUTOEN |  BMCR_S1000);
	PHY_WRITE(sc, TRUEPHY_CTRL, TRUEPHY_CTRL_RSV1);

	mii_phy_reset(sc);

	if (TRUEPHY_FRAMELEN((if_getmtu(sc->mii_pdata->mii_ifp)) > 2048)) {
		int conf;

		conf = PHY_READ(sc, TRUEPHY_CONF);
		conf &= ~TRUEPHY_CONF_TXFIFO_MASK;
		conf |= TRUEPHY_CONF_TXFIFO_24;
		PHY_WRITE(sc, TRUEPHY_CONF, conf);
	}
}

static void
truephy_status(struct mii_softc *sc)
{
	struct mii_data *mii = sc->mii_pdata;
	int bmsr, bmcr, sr;

	mii->mii_media_status = IFM_AVALID;
	mii->mii_media_active = IFM_ETHER;

	sr = PHY_READ(sc, TRUEPHY_SR);
	bmcr = PHY_READ(sc, MII_BMCR);

	bmsr = PHY_READ(sc, MII_BMSR) | PHY_READ(sc, MII_BMSR);
	if (bmsr & BMSR_LINK)
		mii->mii_media_status |= IFM_ACTIVE;

	if (bmcr & BMCR_AUTOEN) {
		if ((bmsr & BMSR_ACOMP) == 0) {
			mii->mii_media_active |= IFM_NONE;
			return;
		}
	}

	switch (sr & TRUEPHY_SR_SPD_MASK) {
	case TRUEPHY_SR_SPD_1000T:
		mii->mii_media_active |= IFM_1000_T;
		break;
	case TRUEPHY_SR_SPD_100TX:
		mii->mii_media_active |= IFM_100_TX;
		break;
	case TRUEPHY_SR_SPD_10T:
		mii->mii_media_active |= IFM_10_T;
		break;
	default:
		/* XXX will this ever happen? */
		printf("invalid media SR %#x\n", sr);
		mii->mii_media_active |= IFM_NONE;
		return;
	}

	if (sr & TRUEPHY_SR_FDX)
		mii->mii_media_active |= IFM_FDX | mii_phy_flowstatus(sc);
	else
		mii->mii_media_active |= IFM_HDX;
}
