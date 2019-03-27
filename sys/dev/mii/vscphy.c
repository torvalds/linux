/*-
 * Copyright (c) 2017 Ian Lepore <ian@freebsd.org>
 * All rights reserved.
 *
 * Development sponsored by Microsemi, Inc.
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

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

/*
 * Microsemi / Vitesse VSC8501 (and similar).
 */

#include "opt_platform.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/socket.h>
#include <sys/errno.h>
#include <sys/module.h>
#include <sys/bus.h>
#include <sys/malloc.h>

#include <net/if.h>
#include <net/if_media.h>

#include <dev/mii/mii.h>
#include <dev/mii/miivar.h>
#include "miidevs.h"
#include "miibus_if.h"

#ifdef FDT
#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>
#include <dev/mii/mii_fdt.h>
#endif

/* Vitesse VSC8501 */
#define	VSC8501_EXTPAGE_REG		0x001f

#define	VSC8501_EXTCTL1_REG		0x0017
#define	  VSC8501_EXTCTL1_RGMII_MODE	  (1u << 12)

#define	VSC8501_RGMII_CTRL_PAGE		0x02
#define	VSC8501_RGMII_CTRL_REG		0x14
#define	  VSC8501_RGMII_DELAY_MASK	  0x07
#define	  VSC8501_RGMII_DELAY_TXSHIFT	  0
#define	  VSC8501_RGMII_DELAY_RXSHIFT	  4
#define	  VSC8501_RGMII_RXCLOCK_DISABLE	  (1u << 11)
#define	  VSC8501_RGMII_RXSWAP		  (1u <<  7)
#define	  VSC8501_RGMII_TXSWAP		  (1u <<  3)
#define	  VSC8501_RGMII_LANESWAP	  (VSC8501_RGMII_RXSWAP | \
					   VSC8501_RGMII_TXSWAP)

struct vscphy_softc {
	mii_softc_t	mii_sc;
	device_t	dev;
	mii_contype_t	contype;
	int		rxdelay;
	int		txdelay;
	bool		laneswap;
};

static void vscphy_reset(struct mii_softc *);
static int  vscphy_service(struct mii_softc *, struct mii_data *, int);

static const struct mii_phydesc vscphys[] = {
	MII_PHY_DESC(xxVITESSE, VSC8501),
	MII_PHY_END
};

static const struct mii_phy_funcs vscphy_funcs = {
	vscphy_service,
	ukphy_status,
	vscphy_reset
};

#ifdef FDT
static void
vscphy_fdt_get_config(struct vscphy_softc *vsc)
{
	mii_fdt_phy_config_t *cfg;
	pcell_t val;

	cfg = mii_fdt_get_config(vsc->dev);
	vsc->contype = cfg->con_type;
	vsc->laneswap = (cfg->flags & MIIF_FDT_LANE_SWAP) &&
	    !(cfg->flags & MIIF_FDT_NO_LANE_SWAP);
	if (OF_getencprop(cfg->phynode, "rx-delay", &val, sizeof(val)) > 0)
		vsc->rxdelay = val;
	if (OF_getencprop(cfg->phynode, "tx-delay", &val, sizeof(val)) > 0)
		vsc->txdelay = val;
	mii_fdt_free_config(cfg);
}
#endif

static inline int
vscphy_read(struct vscphy_softc *sc, u_int reg)
{
	u_int val;

	val = PHY_READ(&sc->mii_sc, reg);
	return (val);
}

static inline void
vscphy_write(struct vscphy_softc *sc, u_int reg, u_int val)
{

	PHY_WRITE(&sc->mii_sc, reg, val);
}

static void
vsc8501_setup_rgmii(struct vscphy_softc *vsc)
{
	int reg;

	vscphy_write(vsc, VSC8501_EXTPAGE_REG, VSC8501_RGMII_CTRL_PAGE);

	reg = vscphy_read(vsc, VSC8501_RGMII_CTRL_REG);
	reg &= ~VSC8501_RGMII_RXCLOCK_DISABLE;
	reg &= ~VSC8501_RGMII_LANESWAP;
	reg &= ~(VSC8501_RGMII_DELAY_MASK << VSC8501_RGMII_DELAY_TXSHIFT);
	reg &= ~(VSC8501_RGMII_DELAY_MASK << VSC8501_RGMII_DELAY_RXSHIFT);
	if (vsc->laneswap)
		reg |= VSC8501_RGMII_LANESWAP;
	if (vsc->contype == MII_CONTYPE_RGMII_ID || 
	    vsc->contype == MII_CONTYPE_RGMII_TXID) {
		reg |= vsc->txdelay << VSC8501_RGMII_DELAY_TXSHIFT;
	}
	if (vsc->contype == MII_CONTYPE_RGMII_ID || 
	    vsc->contype == MII_CONTYPE_RGMII_RXID) {
		reg |= vsc->rxdelay << VSC8501_RGMII_DELAY_RXSHIFT;
	}
	vscphy_write(vsc, VSC8501_RGMII_CTRL_REG, reg);

	vscphy_write(vsc, VSC8501_EXTPAGE_REG, 0);
}

static void
vsc8501_reset(struct vscphy_softc *vsc)
{
	int reg;

	/*
	 * Must set whether the mac<->phy connection is RGMII first; changes to
	 * that bit take effect only after a softreset.
	 */
	reg = vscphy_read(vsc, VSC8501_EXTCTL1_REG);
	if (mii_contype_is_rgmii(vsc->contype))
		reg |= VSC8501_EXTCTL1_RGMII_MODE;
	else
		reg &= ~VSC8501_EXTCTL1_RGMII_MODE;
	vscphy_write(vsc, VSC8501_EXTCTL1_REG, reg);

	mii_phy_reset(&vsc->mii_sc);

	/*
	 * Setup rgmii control register if necessary, after softreset.
	 */
	if (mii_contype_is_rgmii(vsc->contype))
	    vsc8501_setup_rgmii(vsc);
}

static void
vscphy_reset(struct mii_softc *sc)
{
	struct vscphy_softc *vsc = (struct vscphy_softc *)sc;

	switch (sc->mii_mpd_model) {
	case MII_MODEL_xxVITESSE_VSC8501:
		vsc8501_reset(vsc);
		break;
	default:
		mii_phy_reset(sc);
		break;
	}
}

static int
vscphy_service(struct mii_softc *sc, struct mii_data *mii, int cmd)
{

	switch (cmd) {
	case MII_POLLSTAT:
		break;

	case MII_MEDIACHG:
		mii_phy_setmedia(sc);
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

static int
vscphy_probe(device_t dev)
{

	return (mii_phy_dev_probe(dev, vscphys, BUS_PROBE_DEFAULT));
}

static int
vscphy_attach(device_t dev)
{
	struct vscphy_softc *vsc;

	vsc = device_get_softc(dev);
	vsc->dev = dev;

#ifdef FDT
	vscphy_fdt_get_config(vsc);
#endif	

	mii_phy_dev_attach(dev, MIIF_NOMANPAUSE, &vscphy_funcs, 1);
	mii_phy_setmedia(&vsc->mii_sc);

	return (0);
}

static device_method_t vscphy_methods[] = {
	/* device interface */
	DEVMETHOD(device_probe,		vscphy_probe),
	DEVMETHOD(device_attach,	vscphy_attach),
	DEVMETHOD(device_detach,	mii_phy_detach),
	DEVMETHOD(device_shutdown,	bus_generic_shutdown),
	DEVMETHOD_END
};

static devclass_t vscphy_devclass;

static driver_t vscphy_driver = {
	"vscphy",
	vscphy_methods,
	sizeof(struct vscphy_softc)
};

DRIVER_MODULE(vscphy, miibus, vscphy_driver, vscphy_devclass, 0, 0);
