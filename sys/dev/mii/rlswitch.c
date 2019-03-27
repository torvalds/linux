/*-
 * SPDX-License-Identifier: BSD-4-Clause
 *
 * Copyright (c) 1997, 1998, 1999
 *	Bill Paul <wpaul@ctr.columbia.edu>.  All rights reserved.
 * Copyright (c) 2006 Bernd Walter.  All rights reserved.
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
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

/*
 * driver for RealTek 8305 pseudo PHYs
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/socket.h>
#include <sys/bus.h>
#include <sys/taskqueue.h>	/* XXXGL: if_rlreg.h contamination */

#include <net/if.h>
#include <net/if_arp.h>
#include <net/if_media.h>

#include <dev/mii/mii.h>
#include <dev/mii/miivar.h>
#include "miidevs.h"

#include <machine/bus.h>
#include <dev/rl/if_rlreg.h>

#include "miibus_if.h"

//#define RL_DEBUG
#define RL_VLAN

static int rlswitch_probe(device_t);
static int rlswitch_attach(device_t);

static device_method_t rlswitch_methods[] = {
	/* device interface */
	DEVMETHOD(device_probe,		rlswitch_probe),
	DEVMETHOD(device_attach,	rlswitch_attach),
	DEVMETHOD(device_detach,	mii_phy_detach),
	DEVMETHOD(device_shutdown,	bus_generic_shutdown),
	DEVMETHOD_END
};

static devclass_t rlswitch_devclass;

static driver_t rlswitch_driver = {
	"rlswitch",
	rlswitch_methods,
	sizeof(struct mii_softc)
};

DRIVER_MODULE(rlswitch, miibus, rlswitch_driver, rlswitch_devclass, 0, 0);

static int	rlswitch_service(struct mii_softc *, struct mii_data *, int);
static void	rlswitch_status(struct mii_softc *);

#ifdef RL_DEBUG
static void	rlswitch_phydump(device_t dev);
#endif

static const struct mii_phydesc rlswitches[] = {
	MII_PHY_DESC(REALTEK, RTL8305SC),
	MII_PHY_END
};

static const struct mii_phy_funcs rlswitch_funcs = {
	rlswitch_service,
	rlswitch_status,
	mii_phy_reset
};

static int
rlswitch_probe(device_t dev)
{
	int rv;

	rv = mii_phy_dev_probe(dev, rlswitches, BUS_PROBE_DEFAULT);
	if (rv <= 0)
		return (rv);

	return (ENXIO);
}

static int
rlswitch_attach(device_t dev)
{
	struct mii_softc	*sc;

	sc = device_get_softc(dev);

	/*
	 * We handle all pseudo PHYs in a single instance.
	 */
	mii_phy_dev_attach(dev, MIIF_NOISOLATE | MIIF_NOMANPAUSE,
	    &rlswitch_funcs, 0);

	sc->mii_capabilities = BMSR_100TXFDX & sc->mii_capmask;
	device_printf(dev, " ");
	mii_phy_add_media(sc);
	printf("\n");
#ifdef RL_DEBUG
	rlswitch_phydump(dev);
#endif
	
#ifdef RL_VLAN
	int val;

	/* Global Control 0 */
	val = 0;
	val |= 0 << 10;		/* enable 802.1q VLAN Tag support */
	val |= 0 << 9;		/* enable VLAN ingress filtering */
	val |= 1 << 8;		/* disable VLAN tag admit control */
	val |= 1 << 6;		/* internal use */
	val |= 1 << 5;		/* internal use */
	val |= 1 << 4;		/* internal use */
	val |= 1 << 3;		/* internal use */
	val |= 1 << 1;		/* reserved */
	MIIBUS_WRITEREG(sc->mii_dev, 0, 16, val);

	/* Global Control 2 */
	val = 0;
	val |= 1 << 15;		/* reserved */
	val |= 0 << 14;		/* enable 1552 Bytes support */
	val |= 1 << 13;		/* enable broadcast input drop */
	val |= 1 << 12;		/* forward reserved control frames */
	val |= 1 << 11;		/* disable forwarding unicast frames to other VLAN's */
	val |= 1 << 10;		/* disable forwarding ARP broadcasts to other VLAN's */
	val |= 1 << 9;		/* enable 48 pass 1 */
	val |= 0 << 8;		/* enable VLAN */
	val |= 1 << 7;		/* reserved */
	val |= 1 << 6;		/* enable defer */
	val |= 1 << 5;		/* 43ms LED blink time */
	val |= 3 << 3;		/* 16:1 queue weight */
	val |= 1 << 2;		/* disable broadcast storm control */
	val |= 1 << 1;		/* enable power-on LED blinking */
	val |= 1 << 0;		/* reserved */
	MIIBUS_WRITEREG(sc->mii_dev, 0, 18, val);

	/* Port 0 Control Register 0 */
	val = 0;
	val |= 1 << 15;		/* reserved */
	val |= 1 << 11;		/* drop received packets with wrong VLAN tag */
	val |= 1 << 10;		/* disable 802.1p priority classification */
	val |= 1 << 9;		/* disable diffserv priority classification */
	val |= 1 << 6;		/* internal use */
	val |= 3 << 4;		/* internal use */
	val |= 1 << 3;		/* internal use */
	val |= 1 << 2;		/* internal use */
	val |= 1 << 0;		/* remove VLAN tags on output */
	MIIBUS_WRITEREG(sc->mii_dev, 0, 22, val);

	/* Port 1 Control Register 0 */
	val = 0;
	val |= 1 << 15;		/* reserved */
	val |= 1 << 11;		/* drop received packets with wrong VLAN tag */
	val |= 1 << 10;		/* disable 802.1p priority classification */
	val |= 1 << 9;		/* disable diffserv priority classification */
	val |= 1 << 6;		/* internal use */
	val |= 3 << 4;		/* internal use */
	val |= 1 << 3;		/* internal use */
	val |= 1 << 2;		/* internal use */
	val |= 1 << 0;		/* remove VLAN tags on output */
	MIIBUS_WRITEREG(sc->mii_dev, 1, 22, val);

	/* Port 2 Control Register 0 */
	val = 0;
	val |= 1 << 15;		/* reserved */
	val |= 1 << 11;		/* drop received packets with wrong VLAN tag */
	val |= 1 << 10;		/* disable 802.1p priority classification */
	val |= 1 << 9;		/* disable diffserv priority classification */
	val |= 1 << 6;		/* internal use */
	val |= 3 << 4;		/* internal use */
	val |= 1 << 3;		/* internal use */
	val |= 1 << 2;		/* internal use */
	val |= 1 << 0;		/* remove VLAN tags on output */
	MIIBUS_WRITEREG(sc->mii_dev, 2, 22, val);

	/* Port 3 Control Register 0 */
	val = 0;
	val |= 1 << 15;		/* reserved */
	val |= 1 << 11;		/* drop received packets with wrong VLAN tag */
	val |= 1 << 10;		/* disable 802.1p priority classification */
	val |= 1 << 9;		/* disable diffserv priority classification */
	val |= 1 << 6;		/* internal use */
	val |= 3 << 4;		/* internal use */
	val |= 1 << 3;		/* internal use */
	val |= 1 << 2;		/* internal use */
	val |= 1 << 0;		/* remove VLAN tags on output */
	MIIBUS_WRITEREG(sc->mii_dev, 3, 22, val);

	/* Port 4 (system port) Control Register 0 */
	val = 0;
	val |= 1 << 15;		/* reserved */
	val |= 0 << 11;		/* don't drop received packets with wrong VLAN tag */
	val |= 1 << 10;		/* disable 802.1p priority classification */
	val |= 1 << 9;		/* disable diffserv priority classification */
	val |= 1 << 6;		/* internal use */
	val |= 3 << 4;		/* internal use */
	val |= 1 << 3;		/* internal use */
	val |= 1 << 2;		/* internal use */
	val |= 2 << 0;		/* add VLAN tags for untagged packets on output */
	MIIBUS_WRITEREG(sc->mii_dev, 4, 22, val);

	/* Port 0 Control Register 1 and VLAN A */
	val = 0;
	val |= 0x0 << 12;	/* Port 0 VLAN Index */
	val |= 1 << 11;		/* internal use */
	val |= 1 << 10;		/* internal use */
	val |= 1 << 9;		/* internal use */
	val |= 1 << 7;		/* internal use */
	val |= 1 << 6;		/* internal use */
	val |= 0x11 << 0;	/* VLAN A membership */
	MIIBUS_WRITEREG(sc->mii_dev, 0, 24, val);

	/* Port 0 Control Register 2 and VLAN A */
	val = 0;
	val |= 1 << 15;		/* internal use */
	val |= 1 << 14;		/* internal use */
	val |= 1 << 13;		/* internal use */
	val |= 1 << 12;		/* internal use */
	val |= 0x100 << 0;	/* VLAN A ID */
	MIIBUS_WRITEREG(sc->mii_dev, 0, 25, val);

	/* Port 1 Control Register 1 and VLAN B */
	val = 0;
	val |= 0x1 << 12;	/* Port 1 VLAN Index */
	val |= 1 << 11;		/* internal use */
	val |= 1 << 10;		/* internal use */
	val |= 1 << 9;		/* internal use */
	val |= 1 << 7;		/* internal use */
	val |= 1 << 6;		/* internal use */
	val |= 0x12 << 0;	/* VLAN B membership */
	MIIBUS_WRITEREG(sc->mii_dev, 1, 24, val);

	/* Port 1 Control Register 2 and VLAN B */
	val = 0;
	val |= 1 << 15;		/* internal use */
	val |= 1 << 14;		/* internal use */
	val |= 1 << 13;		/* internal use */
	val |= 1 << 12;		/* internal use */
	val |= 0x101 << 0;	/* VLAN B ID */
	MIIBUS_WRITEREG(sc->mii_dev, 1, 25, val);

	/* Port 2 Control Register 1 and VLAN C */
	val = 0;
	val |= 0x2 << 12;	/* Port 2 VLAN Index */
	val |= 1 << 11;		/* internal use */
	val |= 1 << 10;		/* internal use */
	val |= 1 << 9;		/* internal use */
	val |= 1 << 7;		/* internal use */
	val |= 1 << 6;		/* internal use */
	val |= 0x14 << 0;	/* VLAN C membership */
	MIIBUS_WRITEREG(sc->mii_dev, 2, 24, val);

	/* Port 2 Control Register 2 and VLAN C */
	val = 0;
	val |= 1 << 15;		/* internal use */
	val |= 1 << 14;		/* internal use */
	val |= 1 << 13;		/* internal use */
	val |= 1 << 12;		/* internal use */
	val |= 0x102 << 0;	/* VLAN C ID */
	MIIBUS_WRITEREG(sc->mii_dev, 2, 25, val);

	/* Port 3 Control Register 1 and VLAN D */
	val = 0;
	val |= 0x3 << 12;	/* Port 3 VLAN Index */
	val |= 1 << 11;		/* internal use */
	val |= 1 << 10;		/* internal use */
	val |= 1 << 9;		/* internal use */
	val |= 1 << 7;		/* internal use */
	val |= 1 << 6;		/* internal use */
	val |= 0x18 << 0;	/* VLAN D membership */
	MIIBUS_WRITEREG(sc->mii_dev, 3, 24, val);

	/* Port 3 Control Register 2 and VLAN D */
	val = 0;
	val |= 1 << 15;		/* internal use */
	val |= 1 << 14;		/* internal use */
	val |= 1 << 13;		/* internal use */
	val |= 1 << 12;		/* internal use */
	val |= 0x103 << 0;	/* VLAN D ID */
	MIIBUS_WRITEREG(sc->mii_dev, 3, 25, val);

	/* Port 4 Control Register 1 and VLAN E */
	val = 0;
	val |= 0x0 << 12;	/* Port 4 VLAN Index */
	val |= 1 << 11;		/* internal use */
	val |= 1 << 10;		/* internal use */
	val |= 1 << 9;		/* internal use */
	val |= 1 << 7;		/* internal use */
	val |= 1 << 6;		/* internal use */
	val |= 0 << 0;		/* VLAN E membership */
	MIIBUS_WRITEREG(sc->mii_dev, 4, 24, val);

	/* Port 4 Control Register 2 and VLAN E */
	val = 0;
	val |= 1 << 15;		/* internal use */
	val |= 1 << 14;		/* internal use */
	val |= 1 << 13;		/* internal use */
	val |= 1 << 12;		/* internal use */
	val |= 0x104 << 0;	/* VLAN E ID */
	MIIBUS_WRITEREG(sc->mii_dev, 4, 25, val);
#endif

#ifdef RL_DEBUG
	rlswitch_phydump(dev);
#endif
	MIIBUS_MEDIAINIT(sc->mii_dev);
	return (0);
}

static int
rlswitch_service(struct mii_softc *sc, struct mii_data *mii, int cmd)
{

	switch (cmd) {
	case MII_POLLSTAT:
		break;

	case MII_MEDIACHG:
		break;

	case MII_TICK:
		break;
	}

	/* Update the media status. */
	PHY_STATUS(sc);

	/* Callback if something changed. */
	// mii_phy_update(sc, cmd);
	return (0);
}

static void
rlswitch_status(struct mii_softc *phy)
{
	struct mii_data *mii = phy->mii_pdata;

	mii->mii_media_status = IFM_AVALID;
	mii->mii_media_active = IFM_ETHER;
	mii->mii_media_status |= IFM_ACTIVE;
	mii->mii_media_active |=
	    IFM_100_TX | IFM_FDX | mii_phy_flowstatus(phy);
}

#ifdef RL_DEBUG
static void
rlswitch_phydump(device_t dev) {
	int phy, reg, val;
	struct mii_softc *sc;

	sc = device_get_softc(dev);
	device_printf(dev, "rlswitchphydump\n");
	for (phy = 0; phy <= 5; phy++) {
		printf("PHY%i:", phy);
		for (reg = 0; reg <= 31; reg++) {
			val = MIIBUS_READREG(sc->mii_dev, phy, reg);
			printf(" 0x%x", val);
		}
		printf("\n");
	}
}
#endif
