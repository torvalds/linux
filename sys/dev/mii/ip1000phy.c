/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
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

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

/*
 * Driver for the IC Plus IP1000A/IP1001 10/100/1000 PHY.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/socket.h>
#include <sys/taskqueue.h>
#include <sys/bus.h>

#include <net/if.h>
#include <net/if_var.h>
#include <net/if_media.h>

#include <dev/mii/mii.h>
#include <dev/mii/miivar.h>
#include "miidevs.h"

#include <dev/mii/ip1000phyreg.h>

#include "miibus_if.h"

#include <machine/bus.h>
#include <dev/stge/if_stgereg.h>

static int ip1000phy_probe(device_t);
static int ip1000phy_attach(device_t);

static device_method_t ip1000phy_methods[] = {
	/* device interface */
	DEVMETHOD(device_probe,		ip1000phy_probe),
	DEVMETHOD(device_attach,	ip1000phy_attach),
	DEVMETHOD(device_detach,	mii_phy_detach),
	DEVMETHOD(device_shutdown,	bus_generic_shutdown),
	DEVMETHOD_END
};

static devclass_t ip1000phy_devclass;
static driver_t ip1000phy_driver = {
	"ip1000phy",
	ip1000phy_methods,
	sizeof(struct mii_softc)
};

DRIVER_MODULE(ip1000phy, miibus, ip1000phy_driver, ip1000phy_devclass, 0, 0);

static int	ip1000phy_service(struct mii_softc *, struct mii_data *, int);
static void	ip1000phy_status(struct mii_softc *);
static void	ip1000phy_reset(struct mii_softc *);
static int	ip1000phy_mii_phy_auto(struct mii_softc *, int);

static const struct mii_phydesc ip1000phys[] = {
	MII_PHY_DESC(xxICPLUS, IP1000A),
	MII_PHY_DESC(xxICPLUS, IP1001),
	MII_PHY_END
};

static const struct mii_phy_funcs ip1000phy_funcs = {
	ip1000phy_service,
	ip1000phy_status,
	ip1000phy_reset
};

static int
ip1000phy_probe(device_t dev)
{

	return (mii_phy_dev_probe(dev, ip1000phys, BUS_PROBE_DEFAULT));
}

static int
ip1000phy_attach(device_t dev)
{
	struct mii_attach_args *ma;
	u_int flags;

	ma = device_get_ivars(dev);
	flags = MIIF_NOISOLATE | MIIF_NOMANPAUSE;
	if (MII_MODEL(ma->mii_id2) == MII_MODEL_xxICPLUS_IP1000A &&
	     mii_dev_mac_match(dev, "stge") &&
	     (miibus_get_flags(dev) & MIIF_MACPRIV0) != 0)
		flags |= MIIF_PHYPRIV0;
	mii_phy_dev_attach(dev, flags, &ip1000phy_funcs, 1);
	return (0);
}

static int
ip1000phy_service(struct mii_softc *sc, struct mii_data *mii, int cmd)
{
	struct ifmedia_entry *ife = mii->mii_media.ifm_cur;
	uint32_t gig, reg, speed;

	switch (cmd) {
	case MII_POLLSTAT:
		break;

	case MII_MEDIACHG:
		PHY_RESET(sc);
		switch (IFM_SUBTYPE(ife->ifm_media)) {
		case IFM_AUTO:
			(void)ip1000phy_mii_phy_auto(sc, ife->ifm_media);
			goto done;

		case IFM_1000_T:
			/*
			 * XXX
			 * Manual 1000baseT setting doesn't seem to work.
			 */
			speed = IP1000PHY_BMCR_1000;
			break;

		case IFM_100_TX:
			speed = IP1000PHY_BMCR_100;
			break;

		case IFM_10_T:
			speed = IP1000PHY_BMCR_10;
			break;

		default:
			return (EINVAL);
		}

		if ((ife->ifm_media & IFM_FDX) != 0) {
			speed |= IP1000PHY_BMCR_FDX;
			gig = IP1000PHY_1000CR_1000T_FDX;
		} else
			gig = IP1000PHY_1000CR_1000T;

		if (IFM_SUBTYPE(ife->ifm_media) == IFM_1000_T) {
			gig |=
			    IP1000PHY_1000CR_MASTER | IP1000PHY_1000CR_MANUAL;
			if ((ife->ifm_media & IFM_ETH_MASTER) != 0)
				gig |= IP1000PHY_1000CR_MMASTER;
		} else
			gig = 0;
		PHY_WRITE(sc, IP1000PHY_MII_1000CR, gig);
		PHY_WRITE(sc, IP1000PHY_MII_BMCR, speed);

done:
		break;

	case MII_TICK:
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
		ip1000phy_mii_phy_auto(sc, ife->ifm_media);
		break;
	}

	/* Update the media status. */
	PHY_STATUS(sc);

	/* Callback if something changed. */
	mii_phy_update(sc, cmd);
	return (0);
}

static void
ip1000phy_status(struct mii_softc *sc)
{
	struct mii_data *mii = sc->mii_pdata;
	uint32_t bmsr, bmcr, stat;

	mii->mii_media_status = IFM_AVALID;
	mii->mii_media_active = IFM_ETHER;

	bmsr = PHY_READ(sc, IP1000PHY_MII_BMSR) |
	    PHY_READ(sc, IP1000PHY_MII_BMSR);
	if ((bmsr & IP1000PHY_BMSR_LINK) != 0)
		mii->mii_media_status |= IFM_ACTIVE;

	bmcr = PHY_READ(sc, IP1000PHY_MII_BMCR);
	if ((bmcr & IP1000PHY_BMCR_LOOP) != 0)
		mii->mii_media_active |= IFM_LOOP;

	if ((bmcr & IP1000PHY_BMCR_AUTOEN) != 0) {
		if ((bmsr & IP1000PHY_BMSR_ANEGCOMP) == 0) {
			/* Erg, still trying, I guess... */
			mii->mii_media_active |= IFM_NONE;
			return;
                }
        }

	if (sc->mii_mpd_model == MII_MODEL_xxICPLUS_IP1001) {
		stat = PHY_READ(sc, IP1000PHY_LSR);
		switch (stat & IP1000PHY_LSR_SPEED_MASK) {
		case IP1000PHY_LSR_SPEED_10:
			mii->mii_media_active |= IFM_10_T;
			break;
		case IP1000PHY_LSR_SPEED_100:
			mii->mii_media_active |= IFM_100_TX;
			break;
		case IP1000PHY_LSR_SPEED_1000:
			mii->mii_media_active |= IFM_1000_T;
			break;
		default:
			mii->mii_media_active |= IFM_NONE;
			return;
		}
		if ((stat & IP1000PHY_LSR_FULL_DUPLEX) != 0)
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
		if ((stat & PC_PhyDuplexStatus) != 0)
			mii->mii_media_active |= IFM_FDX;
		else
			mii->mii_media_active |= IFM_HDX;
	}

	if ((mii->mii_media_active & IFM_FDX) != 0)
		mii->mii_media_active |= mii_phy_flowstatus(sc);

	if ((mii->mii_media_active & IFM_1000_T) != 0) {
		stat = PHY_READ(sc, IP1000PHY_MII_1000SR);
		if ((stat & IP1000PHY_1000SR_MASTER) != 0)
			mii->mii_media_active |= IFM_ETH_MASTER;
	}
}

static int
ip1000phy_mii_phy_auto(struct mii_softc *sc, int media)
{
	uint32_t reg;

	reg = 0;
	if (sc->mii_mpd_model == MII_MODEL_xxICPLUS_IP1001) {
		reg = PHY_READ(sc, IP1000PHY_MII_ANAR);
		reg &= ~(IP1000PHY_ANAR_PAUSE | IP1000PHY_ANAR_APAUSE);
		reg |= IP1000PHY_ANAR_NP;
	}
	reg |= IP1000PHY_ANAR_10T | IP1000PHY_ANAR_10T_FDX |
	    IP1000PHY_ANAR_100TX | IP1000PHY_ANAR_100TX_FDX;
	if ((media & IFM_FLOW) != 0 || (sc->mii_flags & MIIF_FORCEPAUSE) != 0)
		reg |= IP1000PHY_ANAR_PAUSE | IP1000PHY_ANAR_APAUSE;
	PHY_WRITE(sc, IP1000PHY_MII_ANAR, reg | IP1000PHY_ANAR_CSMA);

	reg = IP1000PHY_1000CR_1000T | IP1000PHY_1000CR_1000T_FDX;
	if (sc->mii_mpd_model != MII_MODEL_xxICPLUS_IP1001)
		reg |= IP1000PHY_1000CR_MASTER;
	PHY_WRITE(sc, IP1000PHY_MII_1000CR, reg);
	PHY_WRITE(sc, IP1000PHY_MII_BMCR, (IP1000PHY_BMCR_FDX |
	    IP1000PHY_BMCR_AUTOEN | IP1000PHY_BMCR_STARTNEG));

	return (EJUSTRETURN);
}

static void
ip1000phy_load_dspcode(struct mii_softc *sc)
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

static void
ip1000phy_reset(struct mii_softc *sc)
{
	uint32_t reg;

	mii_phy_reset(sc);

	/* clear autoneg/full-duplex as we don't want it after reset */
	reg = PHY_READ(sc, IP1000PHY_MII_BMCR);
	reg &= ~(IP1000PHY_BMCR_AUTOEN | IP1000PHY_BMCR_FDX);
	PHY_WRITE(sc, MII_BMCR, reg);

	if ((sc->mii_flags & MIIF_PHYPRIV0) != 0)
		ip1000phy_load_dspcode(sc);
}
