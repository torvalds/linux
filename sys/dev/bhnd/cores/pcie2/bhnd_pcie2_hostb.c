/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2015 Landon Fuller <landon@landonf.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer,
 *    without modification.
 * 2. Redistributions in binary form must reproduce at minimum a disclaimer
 *    similar to the "NO WARRANTY" disclaimer below ("Disclaimer") and any
 *    redistribution must be conditioned upon including a substantially
 *    similar Disclaimer requirement for further binary redistribution.
 *
 * NO WARRANTY
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF NONINFRINGEMENT, MERCHANTIBILITY
 * AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
 * THE COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR SPECIAL, EXEMPLARY,
 * OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
 * IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGES.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

/*
 * Broadcom BHND PCIe-Gen2 PCI-Host Bridge.
 * 
 * This driver handles all interactions with PCIe-G2 bridge cores operating in
 * endpoint mode.
 * 
 * Host-level PCI operations are handled at the bhndb bridge level by the
 * bhndb_pci driver.
 */

// TODO
//
// A full survey of known quirks/work-arounds has not been completed.
//
// Work-arounds for the following are not yet implemented:
// - BHND_PCIE2_QUIRK_SERDES_TXDRV_DEEMPH
//   4360 PCIe SerDes Tx amplitude/deemphasis (vendor Apple, boards
//   BCM94360X51P2, BCM94360X51A)

#include <sys/param.h>
#include <sys/kernel.h>

#include <sys/malloc.h>

#include <sys/bus.h>
#include <sys/module.h>

#include <sys/systm.h>

#include <machine/bus.h>
#include <sys/rman.h>
#include <machine/resource.h>

#include <dev/bhnd/bhnd.h>

#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>

#include "bhnd_pcie2_reg.h"
#include "bhnd_pcie2_hostbvar.h"

static const struct bhnd_device_quirk bhnd_pcie2_quirks[];


static int	bhnd_pcie2_wars_early_once(struct bhnd_pcie2hb_softc *sc);
static int	bhnd_pcie2_wars_hwup(struct bhnd_pcie2hb_softc *sc);
static int	bhnd_pcie2_wars_hwdown(struct bhnd_pcie2hb_softc *sc);

/*
 * device/quirk tables
 */

#define	BHND_PCI_DEV(_core, _quirks)		\
	BHND_DEVICE(BCM, _core, NULL, _quirks, BHND_DF_HOSTB)

static const struct bhnd_device bhnd_pcie2_devs[] = {
	BHND_PCI_DEV(PCIE2,	bhnd_pcie2_quirks),
	BHND_DEVICE_END
};

static const struct bhnd_device_quirk bhnd_pcie2_quirks[] = {
	/* Apple BCM4360 boards that require adjusting TX amplitude and
	 * differential output de-emphasis of the PCIe SerDes */
	{{ BHND_MATCH_BOARD(PCI_VENDOR_APPLE, BCM94360X51P2), },
		BHND_PCIE2_QUIRK_SERDES_TXDRV_DEEMPH },
	{{ BHND_MATCH_BOARD(PCI_VENDOR_APPLE, BCM94360X51A), },
		BHND_PCIE2_QUIRK_SERDES_TXDRV_DEEMPH },

	BHND_DEVICE_QUIRK_END
};

static int
bhnd_pcie2_hostb_attach(device_t dev)
{
	struct bhnd_pcie2hb_softc	*sc;
	int				 error;

	sc = device_get_softc(dev);
	sc->dev = dev;
	sc->quirks = bhnd_device_quirks(dev, bhnd_pcie2_devs,
	    sizeof(bhnd_pcie2_devs[0]));

	/* Find the host PCI bridge device */
	sc->pci_dev = bhnd_find_bridge_root(dev, devclass_find("pci"));
	if (sc->pci_dev == NULL) {
		device_printf(dev, "parent pci bridge device not found\n");
		return (ENXIO);
	}

	/* Common setup */
	if ((error = bhnd_pcie2_generic_attach(dev)))
		return (error);


	/* Apply early single-shot work-arounds */
	if ((error = bhnd_pcie2_wars_early_once(sc)))
		goto failed;


	/* Apply attach/resume work-arounds */
	if ((error = bhnd_pcie2_wars_hwup(sc)))
		goto failed;


	return (0);
	
failed:
	bhnd_pcie2_generic_detach(dev);
	return (error);
}

static int
bhnd_pcie2_hostb_detach(device_t dev)
{
	struct bhnd_pcie2hb_softc	*sc;
	int				 error;

	sc = device_get_softc(dev);

	/* Apply suspend/detach work-arounds */
	if ((error = bhnd_pcie2_wars_hwdown(sc)))
		return (error);

	return (bhnd_pcie2_generic_detach(dev));
}

static int
bhnd_pcie2_hostb_suspend(device_t dev)
{
	struct bhnd_pcie2hb_softc	*sc;
	int				 error;

	sc = device_get_softc(dev);

	/* Apply suspend/detach work-arounds */
	if ((error = bhnd_pcie2_wars_hwdown(sc)))
		return (error);

	return (bhnd_pcie2_generic_suspend(dev));
}

static int
bhnd_pcie2_hostb_resume(device_t dev)
{
	struct bhnd_pcie2hb_softc	*sc;
	int				 error;

	sc = device_get_softc(dev);

	if ((error = bhnd_pcie2_generic_resume(dev)))
		return (error);

	/* Apply attach/resume work-arounds */
	if ((error = bhnd_pcie2_wars_hwup(sc))) {
		bhnd_pcie2_generic_detach(dev);
		return (error);
	}

	return (0);
}

/**
 * Apply any hardware work-arounds that must be executed exactly once, early in
 * the attach process.
 * 
 * This must be called after core enumeration and discovery of all applicable
 * quirks, but prior to probe/attach of any cores, parsing of
 * SPROM, etc.
 */
static int
bhnd_pcie2_wars_early_once(struct bhnd_pcie2hb_softc *sc)
{
	// TODO
	return (ENXIO);
}

/**
 * Apply any hardware workarounds that are required upon attach or resume
 * of the bridge device.
 */
static int
bhnd_pcie2_wars_hwup(struct bhnd_pcie2hb_softc *sc)
{
	// TODO
	return (ENXIO);
}

/**
 * Apply any hardware workarounds that are required upon detach or suspend
 * of the bridge device.
 */
static int
bhnd_pcie2_wars_hwdown(struct bhnd_pcie2hb_softc *sc)
{
	// TODO
	return (ENXIO);
}

static device_method_t bhnd_pcie2_hostb_methods[] = {
	/* Device interface */
	DEVMETHOD(device_attach,		bhnd_pcie2_hostb_attach),
	DEVMETHOD(device_detach,		bhnd_pcie2_hostb_detach),
	DEVMETHOD(device_suspend,		bhnd_pcie2_hostb_suspend),
	DEVMETHOD(device_resume,		bhnd_pcie2_hostb_resume),	

	DEVMETHOD_END
};

DEFINE_CLASS_1(bhnd_hostb, bhnd_pcie2_hostb_driver,
    bhnd_pcie2_hostb_methods, sizeof(struct bhnd_pcie2hb_softc),
    bhnd_pcie2_driver);

DRIVER_MODULE(bhnd_pcie2_hostb, bhnd, bhnd_pcie2_hostb_driver, bhnd_hostb_devclass, 0, 0);

MODULE_VERSION(bhnd_pcie2_hostb, 1);
MODULE_DEPEND(bhnd_pcie2_hostb, bhnd, 1, 1, 1);
MODULE_DEPEND(bhnd_pcie2_hostb, bhnd_pcie2, 1, 1, 1);
