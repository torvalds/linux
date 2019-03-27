/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2015-2016 Landon Fuller <landonf@FreeBSD.org>
 * All rights reserved.
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

#include "opt_bwn.h"
#include "opt_wlan.h"

#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/bus.h>
#include <sys/module.h>

#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>

#include <dev/bhnd/bhndb/bhndb_pcivar.h>
#include <dev/bhnd/bhndb/bhndb_hwdata.h>
#include <dev/bhnd/bhndb/bhndb_pci_hwdata.h>

#include <dev/bhnd/bhnd_ids.h>

#include "bhndb_bus_if.h"

#include "if_bwn_pcivar.h"

/* If non-zero, enable attachment of BWN_QUIRK_UNTESTED devices */
static int attach_untested = 0; 
TUNABLE_INT("hw.bwn_pci.attach_untested", &attach_untested);

/* SIBA Devices */
static const struct bwn_pci_device siba_devices[] = {
	BWN_BCM_DEV(BCM4306_D11A,	"BCM4306 802.11a",
	    BWN_QUIRK_WLAN_DUALCORE|BWN_QUIRK_SOFTMODEM_UNPOPULATED),
	BWN_BCM_DEV(BCM4306_D11G,	"BCM4306 802.11b/g",
	    BWN_QUIRK_SOFTMODEM_UNPOPULATED),
	BWN_BCM_DEV(BCM4306_D11G_ID2,	"BCM4306 802.11b/g",
	    BWN_QUIRK_SOFTMODEM_UNPOPULATED),
	BWN_BCM_DEV(BCM4306_D11DUAL,	"BCM4306 802.11a/b/g",
	    BWN_QUIRK_SOFTMODEM_UNPOPULATED),
	BWN_BCM_DEV(BCM4307,		"BCM4307 802.11b",		0),

	BWN_BCM_DEV(BCM4311_D11G,	"BCM4311 802.11b/g",		0),
	BWN_BCM_DEV(BCM4311_D11DUAL,	"BCM4311 802.11a/b/g",		0),
	BWN_BCM_DEV(BCM4311_D11A,	"BCM4311 802.11a",
	    BWN_QUIRK_UNTESTED|BWN_QUIRK_WLAN_DUALCORE),

	BWN_BCM_DEV(BCM4318_D11G,	"BCM4318 802.11b/g",		0),
	BWN_BCM_DEV(BCM4318_D11DUAL,	"BCM4318 802.11a/b/g",		0),
	BWN_BCM_DEV(BCM4318_D11A,	"BCM4318 802.11a",
	    BWN_QUIRK_UNTESTED|BWN_QUIRK_WLAN_DUALCORE),

	BWN_BCM_DEV(BCM4321_D11N,	"BCM4321 802.11n Dual-Band",
	    BWN_QUIRK_USBH_UNPOPULATED),
	BWN_BCM_DEV(BCM4321_D11N2G,	"BCM4321 802.11n 2GHz",
	    BWN_QUIRK_USBH_UNPOPULATED),
	BWN_BCM_DEV(BCM4321_D11N5G,	"BCM4321 802.11n 5GHz",
	    BWN_QUIRK_UNTESTED|BWN_QUIRK_USBH_UNPOPULATED),

	BWN_BCM_DEV(BCM4322_D11N,	"BCM4322 802.11n Dual-Band",	0),
	BWN_BCM_DEV(BCM4322_D11N2G,	"BCM4322 802.11n 2GHz",
	    BWN_QUIRK_UNTESTED),
	BWN_BCM_DEV(BCM4322_D11N5G,	"BCM4322 802.11n 5GHz",
	    BWN_QUIRK_UNTESTED),

	BWN_BCM_DEV(BCM4328_D11G,	"BCM4328/4312 802.11g",		0),

	{ 0, 0, NULL, 0 }
};

/** BCMA Devices */
static const struct bwn_pci_device bcma_devices[] = {
	BWN_BCM_DEV(BCM4331_D11N,	"BCM4331 802.11n Dual-Band",	0),
	BWN_BCM_DEV(BCM4331_D11N2G,	"BCM4331 802.11n 2GHz",		0),
	BWN_BCM_DEV(BCM4331_D11N5G,	"BCM4331 802.11n 5GHz",		0),
	BWN_BCM_DEV(BCM43224_D11N,	"BCM43224 802.11n Dual-Band",	0),
	BWN_BCM_DEV(BCM43224_D11N_ID_VEN1, "BCM43224 802.11n Dual-Band",0),
	BWN_BCM_DEV(BCM43225_D11N2G,	"BCM43225 802.11n 2GHz",	0),

	{ 0, 0, NULL, 0}
};

/** Device configuration table */
static const struct bwn_pci_devcfg bwn_pci_devcfgs[] = {
	/* SIBA devices */
	{
		.bridge_hwcfg	= &bhndb_pci_siba_generic_hwcfg,
		.bridge_hwtable	= bhndb_pci_generic_hw_table,
		.bridge_hwprio	= bhndb_siba_priority_table,
		.devices	= siba_devices
	},
	/* BCMA devices */
	{
		.bridge_hwcfg	= &bhndb_pci_bcma_generic_hwcfg,
		.bridge_hwtable	= bhndb_pci_generic_hw_table,
		.bridge_hwprio	= bhndb_bcma_priority_table,
		.devices	= bcma_devices
	},
	{ NULL, NULL, NULL }
};

/** Search the device configuration table for an entry matching @p dev. */
static int
bwn_pci_find_devcfg(device_t dev, const struct bwn_pci_devcfg **cfg,
    const struct bwn_pci_device **device)
{
	const struct bwn_pci_devcfg	*dvc;
	const struct bwn_pci_device	*dv;

	for (dvc = bwn_pci_devcfgs; dvc->devices != NULL; dvc++) {
		for (dv = dvc->devices; dv->device != 0; dv++) {
			if (pci_get_vendor(dev) == dv->vendor &&
			    pci_get_device(dev) == dv->device)
			{
				if (cfg != NULL)
					*cfg = dvc;
				
				if (device != NULL)
					*device = dv;
				
				return (0);
			}
		}
	}

	return (ENOENT);
}

static int
bwn_pci_probe(device_t dev)
{
	const struct bwn_pci_device	*ident;

	if (bwn_pci_find_devcfg(dev, NULL, &ident))
		return (ENXIO);

	/* Skip untested devices */
	if (ident->quirks & BWN_QUIRK_UNTESTED && !attach_untested)
		return (ENXIO);

	device_set_desc(dev, ident->desc);
	return (BUS_PROBE_DEFAULT);
}

static int
bwn_pci_attach(device_t dev)
{
	struct bwn_pci_softc		*sc;
	const struct bwn_pci_device	*ident;
	int				 error;

	sc = device_get_softc(dev);
	sc->dev = dev;

	/* Find our hardware config */
	if (bwn_pci_find_devcfg(dev, &sc->devcfg, &ident))
		return (ENXIO);

	/* Save quirk flags */
	sc->quirks = ident->quirks;

	/* Attach bridge device */
	if ((error = bhndb_attach_bridge(dev, &sc->bhndb_dev, -1)))
		return (ENXIO);

	/* Success */
	return (0);
}

static int
bwn_pci_detach(device_t dev)
{
	int error;

	if ((error = bus_generic_detach(dev)))
		return (error);

	return (device_delete_children(dev));
}

static void
bwn_pci_probe_nomatch(device_t dev, device_t child)
{
	const char *name;

	name = device_get_name(child);
	if (name == NULL)
		name = "unknown device";

	device_printf(dev, "<%s> (no driver attached)\n", name);
}

static const struct bhndb_hwcfg *
bwn_pci_get_generic_hwcfg(device_t dev, device_t child)
{
	struct bwn_pci_softc *sc = device_get_softc(dev);
	return (sc->devcfg->bridge_hwcfg);
}

static const struct bhndb_hw *
bwn_pci_get_bhndb_hwtable(device_t dev, device_t child)
{
	struct bwn_pci_softc *sc = device_get_softc(dev);
	return (sc->devcfg->bridge_hwtable);
}

static const struct bhndb_hw_priority *
bwn_pci_get_bhndb_hwprio(device_t dev, device_t child)
{
	struct bwn_pci_softc *sc = device_get_softc(dev);
	return (sc->devcfg->bridge_hwprio);
}

static bool
bwn_pci_is_core_disabled(device_t dev, device_t child,
    struct bhnd_core_info *core)
{
	struct bwn_pci_softc	*sc;

	sc = device_get_softc(dev);

	switch (bhnd_core_class(core)) {
	case BHND_DEVCLASS_WLAN:
		if (core->unit > 0 && !(sc->quirks & BWN_QUIRK_WLAN_DUALCORE))
			return (true);

		return (false);

	case BHND_DEVCLASS_ENET:
	case BHND_DEVCLASS_ENET_MAC:
	case BHND_DEVCLASS_ENET_PHY:
		return ((sc->quirks & BWN_QUIRK_ENET_HW_UNPOPULATED) != 0);
		
	case BHND_DEVCLASS_USB_HOST:
		return ((sc->quirks & BWN_QUIRK_USBH_UNPOPULATED) != 0);

	case BHND_DEVCLASS_SOFTMODEM:
		return ((sc->quirks & BWN_QUIRK_SOFTMODEM_UNPOPULATED) != 0);

	default:
		return (false);
	}
}

static device_method_t bwn_pci_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,			bwn_pci_probe),
	DEVMETHOD(device_attach,		bwn_pci_attach),
	DEVMETHOD(device_detach,		bwn_pci_detach),
	DEVMETHOD(device_shutdown,		bus_generic_shutdown),
	DEVMETHOD(device_suspend,		bus_generic_suspend),
	DEVMETHOD(device_resume,		bus_generic_resume),

	/* Bus interface */
	DEVMETHOD(bus_probe_nomatch,		bwn_pci_probe_nomatch),

	/* BHNDB_BUS Interface */
	DEVMETHOD(bhndb_bus_get_generic_hwcfg,	bwn_pci_get_generic_hwcfg),
	DEVMETHOD(bhndb_bus_get_hardware_table,	bwn_pci_get_bhndb_hwtable),
	DEVMETHOD(bhndb_bus_get_hardware_prio,	bwn_pci_get_bhndb_hwprio),
	DEVMETHOD(bhndb_bus_is_core_disabled,	bwn_pci_is_core_disabled),

	DEVMETHOD_END
};

static devclass_t bwn_pci_devclass;

DEFINE_CLASS_0(bwn_pci, bwn_pci_driver, bwn_pci_methods,
    sizeof(struct bwn_pci_softc));
DRIVER_MODULE_ORDERED(bwn_pci, pci, bwn_pci_driver, bwn_pci_devclass, NULL,
    NULL, SI_ORDER_ANY);
MODULE_PNP_INFO("U16:vendor;U16:device;D:#", pci, bwn_siba,
    siba_devices, nitems(siba_devices) - 1);
MODULE_PNP_INFO("U16:vendor;U16:device;D:#", pci, bwn_bcma,
    bcma_devices, nitems(bcma_devices) - 1);
DRIVER_MODULE(bhndb, bwn_pci, bhndb_pci_driver, bhndb_devclass, NULL, NULL);

MODULE_DEPEND(bwn_pci, bwn, 1, 1, 1);
MODULE_DEPEND(bwn_pci, bhnd, 1, 1, 1);
MODULE_DEPEND(bwn_pci, bhndb, 1, 1, 1);
MODULE_DEPEND(bwn_pci, bhndb_pci, 1, 1, 1);
MODULE_DEPEND(bwn_pci, bcma_bhndb, 1, 1, 1);
MODULE_DEPEND(bwn_pci, siba_bhndb, 1, 1, 1);
MODULE_VERSION(bwn_pci, 1);
