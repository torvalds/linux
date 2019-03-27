/*-
 * SPDX-License-Identifier: BSD-4-Clause
 *
 * Copyright (c) 1997, 1998, 1999
 *	Bill Paul <wpaul@ctr.columbia.edu>.  All rights reserved.
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

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

/*
 * driver for Micro Linear 6692 PHYs
 *
 * The Micro Linear 6692 is a strange beast, and dealing with it using
 * this code framework is tricky. The 6692 is actually a 100Mbps-only
 * device, which means that a second PHY is required to support 10Mbps
 * modes. However, even though the 6692 does not support 10Mbps modes,
 * it can still advertise them when performing autonegotiation. If a
 * 10Mbps mode is negotiated, we must program the registers of the
 * companion PHY accordingly in addition to programming the registers
 * of the 6692.
 *
 * This device also does not have vendor/device ID registers.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/socket.h>
#include <sys/module.h>
#include <sys/bus.h>
#include <sys/malloc.h>

#include <net/if.h>
#include <net/if_media.h>

#include <dev/mii/mii.h>
#include <dev/mii/miivar.h>

#include "miibus_if.h"

#define ML_STATE_AUTO_SELF	1
#define ML_STATE_AUTO_OTHER	2

struct mlphy_softc	{
	struct mii_softc	ml_mii;
	device_t		ml_dev;
	int			ml_state;
	int			ml_linked;
};

static int mlphy_probe(device_t);
static int mlphy_attach(device_t);

static device_method_t mlphy_methods[] = {
	/* device interface */
	DEVMETHOD(device_probe,		mlphy_probe),
	DEVMETHOD(device_attach,	mlphy_attach),
	DEVMETHOD(device_detach,	mii_phy_detach),
	DEVMETHOD(device_shutdown,	bus_generic_shutdown),
	DEVMETHOD_END
};

static devclass_t mlphy_devclass;

static driver_t mlphy_driver = {
	"mlphy",
	mlphy_methods,
	sizeof(struct mlphy_softc)
};

DRIVER_MODULE(mlphy, miibus, mlphy_driver, mlphy_devclass, 0, 0);

static struct mii_softc *mlphy_find_other(struct mlphy_softc *);
static int	mlphy_service(struct mii_softc *, struct mii_data *, int);
static void	mlphy_reset(struct mii_softc *);
static void	mlphy_status(struct mii_softc *);

static const struct mii_phy_funcs mlphy_funcs = {
	mlphy_service,
	mlphy_status,
	mlphy_reset
};

static int
mlphy_probe(dev)
	device_t		dev;
{
	struct mii_attach_args	*ma;

	ma = device_get_ivars(dev);

	/*
	 * Micro Linear PHY reports oui == 0 model == 0
	 */
	if (MII_OUI(ma->mii_id1, ma->mii_id2) != 0 ||
	    MII_MODEL(ma->mii_id2) != 0)
		return (ENXIO);

	/*
	 * Make sure the parent is a `tl'. So far, I have only
	 * encountered the 6692 on an Olicom card with a ThunderLAN
	 * controller chip.
	 */
	if (!mii_dev_mac_match(dev, "tl"))
		return (ENXIO);

	device_set_desc(dev, "Micro Linear 6692 media interface");

	return (BUS_PROBE_DEFAULT);
}

static int
mlphy_attach(dev)
	device_t		dev;
{
	struct mlphy_softc *msc;
	struct mii_softc *sc;

	msc = device_get_softc(dev);
	sc = &msc->ml_mii;
	msc->ml_dev = dev;
	mii_phy_dev_attach(dev, MIIF_NOMANPAUSE, &mlphy_funcs, 0);

	PHY_RESET(sc);

	sc->mii_capabilities = PHY_READ(sc, MII_BMSR) & sc->mii_capmask;
	/* Let the companion PHY (if any) only handle the media we don't. */
	sc->mii_capmask = ~sc->mii_capabilities;
	device_printf(dev, " ");
	mii_phy_add_media(sc);
	printf("\n");

	MIIBUS_MEDIAINIT(sc->mii_dev);
	return (0);
}

static struct mii_softc *
mlphy_find_other(struct mlphy_softc *msc)
{
	device_t		*devlist;
	struct mii_softc *retval;
	int i, devs;

	retval = NULL;
	if (device_get_children(msc->ml_mii.mii_dev, &devlist, &devs) != 0)
		return (NULL);
	for (i = 0; i < devs; i++) {
		if (devlist[i] != msc->ml_dev) {
			retval = device_get_softc(devlist[i]);
			break;
		}
	}
	free(devlist, M_TEMP);
	return (retval);
}

static int
mlphy_service(xsc, mii, cmd)
	struct mii_softc *xsc;
	struct mii_data *mii;
	int cmd;
{
	struct ifmedia_entry	*ife = mii->mii_media.ifm_cur;
	struct mii_softc	*other = NULL;
	struct mlphy_softc	*msc = (struct mlphy_softc *)xsc;
	struct mii_softc	*sc = (struct mii_softc *)&msc->ml_mii;
	int			other_inst, reg;

	/*
	 * See if there's another PHY on this bus with us.
	 * If so, we may need it for 10Mbps modes.
	 */
	other = mlphy_find_other(msc);

	switch (cmd) {
	case MII_POLLSTAT:
		break;

	case MII_MEDIACHG:
		switch (IFM_SUBTYPE(ife->ifm_media)) {
		case IFM_AUTO:
			/*
			 * For autonegotiation, reset and isolate the
			 * companion PHY (if any) and then do NWAY
			 * autonegotiation ourselves.
			 */
			msc->ml_state = ML_STATE_AUTO_SELF;
			if (other != NULL) {
				PHY_RESET(other);
				PHY_WRITE(other, MII_BMCR, BMCR_ISO);
			}
			(void)mii_phy_auto(sc);
			msc->ml_linked = 0;
			return (0);
		case IFM_10_T:
		case IFM_100_TX:
			/*
			 * For 10baseT and 100baseTX modes, reset and isolate
			 * the companion PHY (if any), then program ourselves
			 * accordingly.
			 */
			if (other != NULL) {
				PHY_RESET(other);
				PHY_WRITE(other, MII_BMCR, BMCR_ISO);
			}
			mii_phy_setmedia(sc);
			msc->ml_state = 0;
			break;
		default:
			return (EINVAL);

		}
		break;

	case MII_TICK:
		/*
		 * Only used for autonegotiation.
		 */
		if (IFM_SUBTYPE(ife->ifm_media) != IFM_AUTO)
			break;

		/*
		 * Check to see if we have link.  If we do, we don't
		 * need to restart the autonegotiation process.  Read
		 * the BMSR twice in case it's latched.
		 * If we're in a 10Mbps mode, check the link of the
		 * 10Mbps PHY. Sometimes the Micro Linear PHY's
		 * linkstat bit will clear while the linkstat bit of
		 * the companion PHY will remain set.
		 */
		if (msc->ml_state == ML_STATE_AUTO_OTHER) {
			reg = PHY_READ(other, MII_BMSR) |
			    PHY_READ(other, MII_BMSR);
		} else {
			reg = PHY_READ(sc, MII_BMSR) |
			    PHY_READ(sc, MII_BMSR);
		}

		if (reg & BMSR_LINK) {
			if (!msc->ml_linked) {
				msc->ml_linked = 1;
				PHY_STATUS(sc);
			}
			break;
		}

		/*
		 * Only retry autonegotiation every 5 seconds.
		 */
		if (++sc->mii_ticks <= MII_ANEGTICKS)
			break;

		sc->mii_ticks = 0;
		msc->ml_linked = 0;
		mii->mii_media_active = IFM_NONE;
		PHY_RESET(sc);
		msc->ml_state = ML_STATE_AUTO_SELF;
		if (other != NULL) {
			PHY_RESET(other);
			PHY_WRITE(other, MII_BMCR, BMCR_ISO);
		}
		mii_phy_auto(sc);
		return (0);
	}

	/* Update the media status. */

	if (msc->ml_state == ML_STATE_AUTO_OTHER) {
		other_inst = other->mii_inst;
		other->mii_inst = sc->mii_inst;
		if (IFM_INST(ife->ifm_media) == other->mii_inst)
			(void)PHY_SERVICE(other, mii, MII_POLLSTAT);
		other->mii_inst = other_inst;
		sc->mii_media_active = other->mii_media_active;
		sc->mii_media_status = other->mii_media_status;
	} else
		ukphy_status(sc);

	/* Callback if something changed. */
	mii_phy_update(sc, cmd);
	return (0);
}

/*
 * The Micro Linear PHY comes out of reset with the 'autoneg
 * enable' bit set, which we don't want.
 */
static void
mlphy_reset(sc)
	struct mii_softc	*sc;
{
	int			reg;

	mii_phy_reset(sc);
	reg = PHY_READ(sc, MII_BMCR);
	reg &= ~BMCR_AUTOEN;
	PHY_WRITE(sc, MII_BMCR, reg);
}

/*
 * If we negotiate a 10Mbps mode, we need to check for an alternate
 * PHY and make sure it's enabled and set correctly.
 */
static void
mlphy_status(sc)
	struct mii_softc	*sc;
{
	struct mlphy_softc	*msc = (struct mlphy_softc *)sc;
	struct mii_data		*mii = msc->ml_mii.mii_pdata;
	struct mii_softc	*other = NULL;

	/* See if there's another PHY on the bus with us. */
	other = mlphy_find_other(msc);
	if (other == NULL)
		return;

	ukphy_status(sc);

	if (IFM_SUBTYPE(mii->mii_media_active) != IFM_10_T) {
		msc->ml_state = ML_STATE_AUTO_SELF;
		PHY_RESET(other);
		PHY_WRITE(other, MII_BMCR, BMCR_ISO);
	}

	if (IFM_SUBTYPE(mii->mii_media_active) == IFM_10_T) {
		msc->ml_state = ML_STATE_AUTO_OTHER;
		PHY_RESET(&msc->ml_mii);
		PHY_WRITE(&msc->ml_mii, MII_BMCR, BMCR_ISO);
		PHY_RESET(other);
		mii_phy_auto(other);
	}
}
