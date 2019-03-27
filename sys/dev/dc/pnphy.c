/*
 * SPDX-License-Identifier: BSD-4-Clause
 *
 * Copyright (c) 1997, 1998, 1999
 *	Bill Paul <wpaul@ee.columbia.edu>.  All rights reserved.
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
 * Pseudo-driver for media selection on the Lite-On PNIC 82c168
 * chip.  The NWAY support on this chip is horribly broken, so we
 * only support manual mode selection.  This is lame, but getting
 * NWAY to work right is amazingly difficult.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/socket.h>
#include <sys/errno.h>
#include <sys/lock.h>
#include <sys/module.h>
#include <sys/mutex.h>
#include <sys/bus.h>

#include <net/if.h>
#include <net/if_var.h>
#include <net/if_arp.h>
#include <net/if_media.h>

#include <dev/mii/mii.h>
#include <dev/mii/miivar.h>
#include "miidevs.h"

#include <machine/bus.h>
#include <machine/resource.h>

#include <dev/dc/if_dcreg.h>

#include "miibus_if.h"

static int pnphy_probe(device_t);
static int pnphy_attach(device_t);

static device_method_t pnphy_methods[] = {
	/* device interface */
	DEVMETHOD(device_probe,		pnphy_probe),
	DEVMETHOD(device_attach,	pnphy_attach),
	DEVMETHOD(device_detach,	mii_phy_detach),
	DEVMETHOD(device_shutdown,	bus_generic_shutdown),
	DEVMETHOD_END
};

static devclass_t pnphy_devclass;

static driver_t pnphy_driver = {
	"pnphy",
	pnphy_methods,
	sizeof(struct mii_softc)
};

DRIVER_MODULE(pnphy, miibus, pnphy_driver, pnphy_devclass, 0, 0);

static int	pnphy_service(struct mii_softc *, struct mii_data *, int);
static void	pnphy_status(struct mii_softc *);
static void	pnphy_reset(struct mii_softc *);

static const struct mii_phy_funcs pnphy_funcs = {
	pnphy_service,
	pnphy_status,
	pnphy_reset
};

static int
pnphy_probe(device_t dev)
{
	struct mii_attach_args *ma;

	ma = device_get_ivars(dev);

	/*
	 * The dc driver will report the 82c168 vendor and device
	 * ID to let us know that it wants us to attach.
	 */
	if (ma->mii_id1 != DC_VENDORID_LO ||
	    ma->mii_id2 != DC_DEVICEID_82C168)
		return (ENXIO);

	device_set_desc(dev, "PNIC 82c168 media interface");

	return (BUS_PROBE_DEFAULT);
}

static int
pnphy_attach(device_t dev)
{
	struct mii_softc *sc;

	sc = device_get_softc(dev);

	mii_phy_dev_attach(dev, MIIF_NOISOLATE | MIIF_NOMANPAUSE,
	    &pnphy_funcs, 0);

	sc->mii_capabilities =
	    BMSR_100TXFDX | BMSR_100TXHDX | BMSR_10TFDX | BMSR_10THDX;
	sc->mii_capabilities &= sc->mii_capmask;
	device_printf(dev, " ");
	mii_phy_add_media(sc);
	printf("\n");

	MIIBUS_MEDIAINIT(sc->mii_dev);
	return (0);
}

static int
pnphy_service(struct mii_softc *sc, struct mii_data *mii, int cmd)
{
	struct ifmedia_entry *ife = mii->mii_media.ifm_cur;

	switch (cmd) {
	case MII_POLLSTAT:
		break;

	case MII_MEDIACHG:
		/*
		 * If the interface is not up, don't do anything.
		 */
		if ((if_getflags(mii->mii_ifp) & IFF_UP) == 0)
			break;

		/*
		 * Note that auto-negotiation is broken on this chip.
		 */
		switch (IFM_SUBTYPE(ife->ifm_media)) {
		case IFM_100_TX:
			mii->mii_media_active = IFM_ETHER | IFM_100_TX;
			if ((ife->ifm_media & IFM_FDX) != 0)
				mii->mii_media_active |= IFM_FDX;
			MIIBUS_STATCHG(sc->mii_dev);
			return (0);
		case IFM_10_T:
			mii->mii_media_active = IFM_ETHER | IFM_10_T;
			if ((ife->ifm_media & IFM_FDX) != 0)
				mii->mii_media_active |= IFM_FDX;
			MIIBUS_STATCHG(sc->mii_dev);
			return (0);
		default:
			return (EINVAL);
		}
		break;

	case MII_TICK:
		/*
		 * Is the interface even up?
		 */
		if ((if_getflags(mii->mii_ifp) & IFF_UP) == 0)
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
pnphy_status(struct mii_softc *sc)
{
	struct mii_data *mii = sc->mii_pdata;
	int reg;
	struct dc_softc		*dc_sc;

	dc_sc = if_getsoftc(mii->mii_ifp);

	mii->mii_media_status = IFM_AVALID;
	mii->mii_media_active = IFM_ETHER;

	reg = CSR_READ_4(dc_sc, DC_ISR);
	if (!(reg & DC_ISR_LINKFAIL))
		mii->mii_media_status |= IFM_ACTIVE;
	reg = CSR_READ_4(dc_sc, DC_NETCFG);
	if (reg & DC_NETCFG_SPEEDSEL)
		mii->mii_media_active |= IFM_10_T;
	else
		mii->mii_media_active |= IFM_100_TX;
	if (reg & DC_NETCFG_FULLDUPLEX)
		mii->mii_media_active |= IFM_FDX;
	else
		mii->mii_media_active |= IFM_HDX;
}

static void
pnphy_reset(struct mii_softc *sc __unused)
{

}
