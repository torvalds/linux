/*-
 * SPDX-License-Identifier: BSD-4-Clause
 *
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
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

/*
 * Driver for the RealTek 8169S/8110S/8211B/8211C internal 10/100/1000 PHY.
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
#include <net/if_arp.h>
#include <net/if_media.h>

#include <dev/mii/mii.h>
#include <dev/mii/miivar.h>
#include "miidevs.h"

#include <dev/mii/rgephyreg.h>

#include "miibus_if.h"

#include <machine/bus.h>
#include <dev/rl/if_rlreg.h>

static int rgephy_probe(device_t);
static int rgephy_attach(device_t);

static device_method_t rgephy_methods[] = {
	/* device interface */
	DEVMETHOD(device_probe,		rgephy_probe),
	DEVMETHOD(device_attach,	rgephy_attach),
	DEVMETHOD(device_detach,	mii_phy_detach),
	DEVMETHOD(device_shutdown,	bus_generic_shutdown),
	DEVMETHOD_END
};

static devclass_t rgephy_devclass;

static driver_t rgephy_driver = {
	"rgephy",
	rgephy_methods,
	sizeof(struct mii_softc)
};

DRIVER_MODULE(rgephy, miibus, rgephy_driver, rgephy_devclass, 0, 0);

static int	rgephy_service(struct mii_softc *, struct mii_data *, int);
static void	rgephy_status(struct mii_softc *);
static int	rgephy_mii_phy_auto(struct mii_softc *, int);
static void	rgephy_reset(struct mii_softc *);
static int	rgephy_linkup(struct mii_softc *);
static void	rgephy_loop(struct mii_softc *);
static void	rgephy_load_dspcode(struct mii_softc *);
static void	rgephy_disable_eee(struct mii_softc *);

static const struct mii_phydesc rgephys[] = {
	MII_PHY_DESC(REALTEK, RTL8169S),
	MII_PHY_DESC(REALTEK, RTL8251),
	MII_PHY_END
};

static const struct mii_phy_funcs rgephy_funcs = {
	rgephy_service,
	rgephy_status,
	rgephy_reset
};

static int
rgephy_probe(device_t dev)
{

	return (mii_phy_dev_probe(dev, rgephys, BUS_PROBE_DEFAULT));
}

static int
rgephy_attach(device_t dev)
{
	struct mii_softc *sc;
	u_int flags;

	sc = device_get_softc(dev);
	flags = 0;
	if (mii_dev_mac_match(dev, "re"))
		flags |= MIIF_PHYPRIV0;
	else if (mii_dev_mac_match(dev, "ure"))
		flags |= MIIF_PHYPRIV1;
	mii_phy_dev_attach(dev, flags, &rgephy_funcs, 0);

	/* RTL8169S do not report auto-sense; add manually. */
	sc->mii_capabilities = (PHY_READ(sc, MII_BMSR) | BMSR_ANEG) &
	    sc->mii_capmask;
	if (sc->mii_capabilities & BMSR_EXTSTAT)
		sc->mii_extcapabilities = PHY_READ(sc, MII_EXTSR);
	device_printf(dev, " ");
	mii_phy_add_media(sc);
	printf("\n");
	/*
	 * Allow IFM_FLAG0 to be set indicating that auto-negotiation with
	 * manual configuration, which is used to work around issues with
	 * certain setups by default, should not be triggered as it may in
	 * turn cause harm in some edge cases.
	 */
	sc->mii_pdata->mii_media.ifm_mask |= IFM_FLAG0;

	PHY_RESET(sc);

	MIIBUS_MEDIAINIT(sc->mii_dev);
	return (0);
}

static int
rgephy_service(struct mii_softc *sc, struct mii_data *mii, int cmd)
{
	struct ifmedia_entry *ife = mii->mii_media.ifm_cur;
	int speed, gig, anar;

	switch (cmd) {
	case MII_POLLSTAT:
		break;

	case MII_MEDIACHG:
		PHY_RESET(sc);	/* XXX hardware bug work-around */

		anar = PHY_READ(sc, RGEPHY_MII_ANAR);
		anar &= ~(RGEPHY_ANAR_PC | RGEPHY_ANAR_ASP |
		    RGEPHY_ANAR_TX_FD | RGEPHY_ANAR_TX |
		    RGEPHY_ANAR_10_FD | RGEPHY_ANAR_10);

		switch (IFM_SUBTYPE(ife->ifm_media)) {
		case IFM_AUTO:
#ifdef foo
			/*
			 * If we're already in auto mode, just return.
			 */
			if (PHY_READ(sc, RGEPHY_MII_BMCR) & RGEPHY_BMCR_AUTOEN)
				return (0);
#endif
			(void)rgephy_mii_phy_auto(sc, ife->ifm_media);
			break;
		case IFM_1000_T:
			speed = RGEPHY_S1000;
			goto setit;
		case IFM_100_TX:
			speed = RGEPHY_S100;
			anar |= RGEPHY_ANAR_TX_FD | RGEPHY_ANAR_TX;
			goto setit;
		case IFM_10_T:
			speed = RGEPHY_S10;
			anar |= RGEPHY_ANAR_10_FD | RGEPHY_ANAR_10;
setit:
			if ((ife->ifm_media & IFM_FLOW) != 0 &&
			    (mii->mii_media.ifm_media & IFM_FLAG0) != 0)
				return (EINVAL);

			if ((ife->ifm_media & IFM_FDX) != 0) {
				speed |= RGEPHY_BMCR_FDX;
				gig = RGEPHY_1000CTL_AFD;
				anar &= ~(RGEPHY_ANAR_TX | RGEPHY_ANAR_10);
				if ((ife->ifm_media & IFM_FLOW) != 0 ||
				    (sc->mii_flags & MIIF_FORCEPAUSE) != 0)
					anar |=
					    RGEPHY_ANAR_PC | RGEPHY_ANAR_ASP;
			} else {
				gig = RGEPHY_1000CTL_AHD;
				anar &=
				    ~(RGEPHY_ANAR_TX_FD | RGEPHY_ANAR_10_FD);
			}
			if (IFM_SUBTYPE(ife->ifm_media) == IFM_1000_T) {
				gig |= RGEPHY_1000CTL_MSE;
				if ((ife->ifm_media & IFM_ETH_MASTER) != 0)
				    gig |= RGEPHY_1000CTL_MSC;
			} else {
				gig = 0;
				anar &= ~RGEPHY_ANAR_ASP;
			}
			if ((mii->mii_media.ifm_media & IFM_FLAG0) == 0)
				speed |=
				    RGEPHY_BMCR_AUTOEN | RGEPHY_BMCR_STARTNEG;
			rgephy_loop(sc);
			PHY_WRITE(sc, RGEPHY_MII_1000CTL, gig);
			PHY_WRITE(sc, RGEPHY_MII_ANAR, anar);
			PHY_WRITE(sc, RGEPHY_MII_BMCR, speed);
			break;
		case IFM_NONE:
			PHY_WRITE(sc, MII_BMCR, BMCR_ISO | BMCR_PDOWN);
			break;
		default:
			return (EINVAL);
		}
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
		 * Check to see if we have link.  If we do, we don't
		 * need to restart the autonegotiation process.
		 */
		if (rgephy_linkup(sc) != 0) {
			sc->mii_ticks = 0;
			break;
		}

		/* Announce link loss right after it happens. */
		if (sc->mii_ticks++ == 0)
			break;

		/* Only retry autonegotiation every mii_anegticks seconds. */
		if (sc->mii_ticks <= sc->mii_anegticks)
			return (0);

		sc->mii_ticks = 0;
		rgephy_mii_phy_auto(sc, ife->ifm_media);
		break;
	}

	/* Update the media status. */
	PHY_STATUS(sc);

	/*
	 * Callback if something changed. Note that we need to poke
	 * the DSP on the RealTek PHYs if the media changes.
	 *
	 */
	if (sc->mii_media_active != mii->mii_media_active ||
	    sc->mii_media_status != mii->mii_media_status ||
	    cmd == MII_MEDIACHG) {
		rgephy_load_dspcode(sc);
	}
	mii_phy_update(sc, cmd);
	return (0);
}

static int
rgephy_linkup(struct mii_softc *sc)
{
	int linkup;
	uint16_t reg;

	linkup = 0;
	if ((sc->mii_flags & MIIF_PHYPRIV0) == 0 &&
	    sc->mii_mpd_rev >= RGEPHY_8211B) {
		if (sc->mii_mpd_rev == RGEPHY_8211F) {
			reg = PHY_READ(sc, RGEPHY_F_MII_SSR);
			if (reg & RGEPHY_F_SSR_LINK)
				linkup++;
		} else {
			reg = PHY_READ(sc, RGEPHY_MII_SSR);
			if (reg & RGEPHY_SSR_LINK)
				linkup++;
		}
	} else {
		if (sc->mii_flags & MIIF_PHYPRIV1)
			reg = PHY_READ(sc, URE_GMEDIASTAT);
		else
			reg = PHY_READ(sc, RL_GMEDIASTAT);
		if (reg & RL_GMEDIASTAT_LINK)
			linkup++;
	}

	return (linkup);
}

static void
rgephy_status(struct mii_softc *sc)
{
	struct mii_data *mii = sc->mii_pdata;
	int bmsr, bmcr;
	uint16_t ssr;

	mii->mii_media_status = IFM_AVALID;
	mii->mii_media_active = IFM_ETHER;

	if (rgephy_linkup(sc) != 0)
		mii->mii_media_status |= IFM_ACTIVE;

	bmsr = PHY_READ(sc, RGEPHY_MII_BMSR);
	bmcr = PHY_READ(sc, RGEPHY_MII_BMCR);
	if (bmcr & RGEPHY_BMCR_ISO) {
		mii->mii_media_active |= IFM_NONE;
		mii->mii_media_status = 0;
		return;
	}

	if (bmcr & RGEPHY_BMCR_LOOP)
		mii->mii_media_active |= IFM_LOOP;

	if (bmcr & RGEPHY_BMCR_AUTOEN) {
		if ((bmsr & RGEPHY_BMSR_ACOMP) == 0) {
			/* Erg, still trying, I guess... */
			mii->mii_media_active |= IFM_NONE;
			return;
		}
	}

	if ((sc->mii_flags & MIIF_PHYPRIV0) == 0 &&
	    sc->mii_mpd_rev >= RGEPHY_8211B) {
		if (sc->mii_mpd_rev == RGEPHY_8211F) {
			ssr = PHY_READ(sc, RGEPHY_F_MII_SSR);
			switch (ssr & RGEPHY_F_SSR_SPD_MASK) {
			case RGEPHY_F_SSR_S1000:
				mii->mii_media_active |= IFM_1000_T;
				break;
			case RGEPHY_F_SSR_S100:
				mii->mii_media_active |= IFM_100_TX;
				break;
			case RGEPHY_F_SSR_S10:
				mii->mii_media_active |= IFM_10_T;
				break;
			default:
				mii->mii_media_active |= IFM_NONE;
				break;
			}
			if (ssr & RGEPHY_F_SSR_FDX)
				mii->mii_media_active |= IFM_FDX;
			else
				mii->mii_media_active |= IFM_HDX;

		} else {
			ssr = PHY_READ(sc, RGEPHY_MII_SSR);
			switch (ssr & RGEPHY_SSR_SPD_MASK) {
			case RGEPHY_SSR_S1000:
				mii->mii_media_active |= IFM_1000_T;
				break;
			case RGEPHY_SSR_S100:
				mii->mii_media_active |= IFM_100_TX;
				break;
			case RGEPHY_SSR_S10:
				mii->mii_media_active |= IFM_10_T;
				break;
			default:
				mii->mii_media_active |= IFM_NONE;
				break;
			}
			if (ssr & RGEPHY_SSR_FDX)
				mii->mii_media_active |= IFM_FDX;
			else
				mii->mii_media_active |= IFM_HDX;
		}
	} else {
		if (sc->mii_flags & MIIF_PHYPRIV1)
			bmsr = PHY_READ(sc, URE_GMEDIASTAT);
		else
			bmsr = PHY_READ(sc, RL_GMEDIASTAT);
		if (bmsr & RL_GMEDIASTAT_1000MBPS)
			mii->mii_media_active |= IFM_1000_T;
		else if (bmsr & RL_GMEDIASTAT_100MBPS)
			mii->mii_media_active |= IFM_100_TX;
		else if (bmsr & RL_GMEDIASTAT_10MBPS)
			mii->mii_media_active |= IFM_10_T;
		else
			mii->mii_media_active |= IFM_NONE;
		if (bmsr & RL_GMEDIASTAT_FDX)
			mii->mii_media_active |= IFM_FDX;
		else
			mii->mii_media_active |= IFM_HDX;
	}

	if ((mii->mii_media_active & IFM_FDX) != 0)
		mii->mii_media_active |= mii_phy_flowstatus(sc);

	if ((IFM_SUBTYPE(mii->mii_media_active) == IFM_1000_T) &&
	    (PHY_READ(sc, RGEPHY_MII_1000STS) & RGEPHY_1000STS_MSR) != 0)
		mii->mii_media_active |= IFM_ETH_MASTER;
}

static int
rgephy_mii_phy_auto(struct mii_softc *sc, int media)
{
	int anar;

	rgephy_loop(sc);
	PHY_RESET(sc);

	anar = BMSR_MEDIA_TO_ANAR(sc->mii_capabilities) | ANAR_CSMA;
	if ((media & IFM_FLOW) != 0 || (sc->mii_flags & MIIF_FORCEPAUSE) != 0)
		anar |= RGEPHY_ANAR_PC | RGEPHY_ANAR_ASP;
	PHY_WRITE(sc, RGEPHY_MII_ANAR, anar);
	DELAY(1000);
	PHY_WRITE(sc, RGEPHY_MII_1000CTL,
	    RGEPHY_1000CTL_AHD | RGEPHY_1000CTL_AFD);
	DELAY(1000);
	PHY_WRITE(sc, RGEPHY_MII_BMCR,
	    RGEPHY_BMCR_AUTOEN | RGEPHY_BMCR_STARTNEG);
	DELAY(100);

	return (EJUSTRETURN);
}

static void
rgephy_loop(struct mii_softc *sc)
{
	int i;

	if (sc->mii_mpd_model != MII_MODEL_REALTEK_RTL8251 &&
	    sc->mii_mpd_rev < RGEPHY_8211B) {
		PHY_WRITE(sc, RGEPHY_MII_BMCR, RGEPHY_BMCR_PDOWN);
		DELAY(1000);
	}

	for (i = 0; i < 15000; i++) {
		if (!(PHY_READ(sc, RGEPHY_MII_BMSR) & RGEPHY_BMSR_LINK)) {
#if 0
			device_printf(sc->mii_dev, "looped %d\n", i);
#endif
			break;
		}
		DELAY(10);
	}
}

#define PHY_SETBIT(x, y, z) \
	PHY_WRITE(x, y, (PHY_READ(x, y) | (z)))
#define PHY_CLRBIT(x, y, z) \
	PHY_WRITE(x, y, (PHY_READ(x, y) & ~(z)))

/*
 * Initialize RealTek PHY per the datasheet. The DSP in the PHYs of
 * existing revisions of the 8169S/8110S chips need to be tuned in
 * order to reliably negotiate a 1000Mbps link. This is only needed
 * for rev 0 and rev 1 of the PHY. Later versions work without
 * any fixups.
 */
static void
rgephy_load_dspcode(struct mii_softc *sc)
{
	int val;

	if (sc->mii_mpd_model == MII_MODEL_REALTEK_RTL8251 ||
	    sc->mii_mpd_rev >= RGEPHY_8211B)
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

static void
rgephy_reset(struct mii_softc *sc)
{
	uint16_t pcr, ssr;

	switch (sc->mii_mpd_rev) {
	case RGEPHY_8211F:
		pcr = PHY_READ(sc, RGEPHY_F_MII_PCR1);
		pcr &= ~(RGEPHY_F_PCR1_MDI_MM | RGEPHY_F_PCR1_ALDPS_EN);
		PHY_WRITE(sc, RGEPHY_F_MII_PCR1, pcr);
		rgephy_disable_eee(sc);
		break;
	case RGEPHY_8211C:
		if ((sc->mii_flags & MIIF_PHYPRIV0) == 0) {
			/* RTL8211C(L) */
			ssr = PHY_READ(sc, RGEPHY_MII_SSR);
			if ((ssr & RGEPHY_SSR_ALDPS) != 0) {
				ssr &= ~RGEPHY_SSR_ALDPS;
				PHY_WRITE(sc, RGEPHY_MII_SSR, ssr);
			}
		}
		/* FALLTHROUGH */
	default:
		if (sc->mii_mpd_rev >= RGEPHY_8211B) {
			pcr = PHY_READ(sc, RGEPHY_MII_PCR);
			if ((pcr & RGEPHY_PCR_MDIX_AUTO) == 0) {
				pcr &= ~RGEPHY_PCR_MDI_MASK;
				pcr |= RGEPHY_PCR_MDIX_AUTO;
				PHY_WRITE(sc, RGEPHY_MII_PCR, pcr);
			}
		}
		break;
	}

	mii_phy_reset(sc);
	DELAY(1000);
	rgephy_load_dspcode(sc);
}

static void
rgephy_disable_eee(struct mii_softc *sc)
{
	uint16_t anar;

	PHY_WRITE(sc, RGEPHY_F_EPAGSR, 0x0000);
	PHY_WRITE(sc, MII_MMDACR, MMDACR_FN_ADDRESS |
	    (MMDACR_DADDRMASK & RGEPHY_F_MMD_DEV_7));
	PHY_WRITE(sc, MII_MMDAADR, RGEPHY_F_MMD_EEEAR);
	PHY_WRITE(sc, MII_MMDACR, MMDACR_FN_DATANPI |
	    (MMDACR_DADDRMASK & RGEPHY_F_MMD_DEV_7));
	PHY_WRITE(sc, MII_MMDAADR, 0x0000);
	PHY_WRITE(sc, MII_MMDACR, 0x0000);
	/*
	 * XXX
	 * Restart auto-negotiation to take changes effect.
	 * This may result in link establishment.
	 */
	anar = BMSR_MEDIA_TO_ANAR(sc->mii_capabilities) | ANAR_CSMA;
	PHY_WRITE(sc, RGEPHY_MII_ANAR, anar);
	PHY_WRITE(sc, RGEPHY_MII_1000CTL, RGEPHY_1000CTL_AHD |
	    RGEPHY_1000CTL_AFD);
	PHY_WRITE(sc, RGEPHY_MII_BMCR, RGEPHY_BMCR_RESET |
	    RGEPHY_BMCR_AUTOEN | RGEPHY_BMCR_STARTNEG);
}
