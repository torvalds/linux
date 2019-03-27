/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2002-2007 Sam Leffler, Errno Consulting
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
 * PCI/Cardbus front-end for the Broadcom Wireless LAN controller driver.
 */

#include "opt_wlan.h"

#include <sys/param.h>
#include <sys/systm.h> 
#include <sys/module.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/mutex.h>
#include <sys/errno.h>

#include <machine/bus.h>
#include <machine/resource.h>
#include <sys/bus.h>
#include <sys/rman.h>

#include <sys/socket.h>
 
#include <net/if.h>
#include <net/if_var.h>
#include <net/if_media.h>
#include <net/if_arp.h>
#include <net/ethernet.h>

#include <net80211/ieee80211_var.h>
#include <net80211/ieee80211_radiotap.h>
#include <net80211/ieee80211_amrr.h>

#include <dev/pci/pcivar.h>
#include <dev/pci/pcireg.h>

#include <dev/bwi/if_bwivar.h>
#include <dev/bwi/if_bwireg.h>
#include <dev/bwi/bitops.h>

/*
 * PCI glue.
 */

struct bwi_pci_softc {
	struct bwi_softc	sc_sc;
};

#define	BS_BAR	0x10
#define	PCIR_RETRY_TIMEOUT	0x41

static const struct bwi_dev {
	uint16_t	vid;
	uint16_t	did;
	const char	*desc;
} bwi_devices[] = {
	{ PCI_VENDOR_BROADCOM, 0x4301,"Broadcom BCM4301 802.11b Wireless Lan" },
	{ PCI_VENDOR_BROADCOM, 0x4307,"Broadcom BCM4307 802.11b Wireless Lan" },
	{ PCI_VENDOR_BROADCOM, 0x4311,"Broadcom BCM4311 802.11b/g Wireless Lan" },
	{ PCI_VENDOR_BROADCOM, 0x4312,"Broadcom BCM4312 802.11a/b/g Wireless Lan" },
	{ PCI_VENDOR_BROADCOM, 0x4313,"Broadcom BCM4312 802.11a Wireless Lan" },
	{ PCI_VENDOR_BROADCOM, 0x4320,"Broadcom BCM4306 802.11b/g Wireless Lan"},
	{ PCI_VENDOR_BROADCOM, 0x4321,"Broadcom BCM4306 802.11a Wireless Lan"},
	{ PCI_VENDOR_BROADCOM, 0x4325,"Broadcom BCM4306 802.11b/g Wireless Lan"},
	{ PCI_VENDOR_BROADCOM, 0x4324,"Broadcom BCM4309 802.11a/b/g Wireless Lan" },
	{ PCI_VENDOR_BROADCOM, 0x4318,"Broadcom BCM4318 802.11b/g Wireless Lan" },
	{ PCI_VENDOR_BROADCOM, 0x4319,"Broadcom BCM4318 802.11a/b/g Wireless Lan" },
	{ PCI_VENDOR_BROADCOM, 0x431a,"Broadcom BCM4318 802.11a Wireless Lan" },
	{ 0, 0, NULL }
};

static int
bwi_pci_probe(device_t dev)
{
	const struct bwi_dev *b;
	uint16_t did, vid;

	did = pci_get_device(dev);
	vid = pci_get_vendor(dev);

	for (b = bwi_devices; b->desc != NULL; ++b) {
		if (b->did == did && b->vid == vid) {
			device_set_desc(dev, b->desc);
			return BUS_PROBE_DEFAULT;
		}
	}
	return ENXIO;
}

static int
bwi_pci_attach(device_t dev)
{
	struct bwi_pci_softc *psc = device_get_softc(dev);
	struct bwi_softc *sc = &psc->sc_sc;
	int error = ENXIO;

	sc->sc_dev = dev;

	/*
	 * Enable bus mastering.
	 */
	pci_enable_busmaster(dev);

	/* 
	 * Setup memory-mapping of PCI registers.
	 */
	sc->sc_mem_rid = BWI_PCIR_BAR;
	sc->sc_mem_res = bus_alloc_resource_any(dev, SYS_RES_MEMORY,
		&sc->sc_mem_rid, RF_ACTIVE);
	if (sc->sc_mem_res == NULL) {
		device_printf(dev, "cannot map register space\n");
		goto bad;
	}
	sc->sc_mem_bt = rman_get_bustag(sc->sc_mem_res);
	sc->sc_mem_bh = rman_get_bushandle(sc->sc_mem_res);
	/*
	 * Mark device invalid so any interrupts (shared or otherwise)
	 * that arrive before the card is setup are discarded.
	 */
	sc->sc_invalid = 1;

	/*
	 * Arrange interrupt line.
	 */
	sc->sc_irq_rid = 0;
	sc->sc_irq_res = bus_alloc_resource_any(dev, SYS_RES_IRQ,
						&sc->sc_irq_rid,
						RF_SHAREABLE|RF_ACTIVE);
	if (sc->sc_irq_res == NULL) {
		device_printf(dev, "could not map interrupt\n");
		goto bad1;
	}

	/* Get more PCI information */
	sc->sc_pci_did = pci_get_device(dev);
	sc->sc_pci_revid = pci_get_revid(dev);
	sc->sc_pci_subvid = pci_get_subvendor(dev);
	sc->sc_pci_subdid = pci_get_subdevice(dev);

	if ((error = bwi_attach(sc)) != 0)
		goto bad2;

	if (bus_setup_intr(dev, sc->sc_irq_res,
			   INTR_TYPE_NET | INTR_MPSAFE,
			   NULL, bwi_intr, sc, &sc->sc_irq_handle)) {
		device_printf(dev, "could not establish interrupt\n");
		goto bad2;
	}
	return (0);

bad2:
	bus_release_resource(dev, SYS_RES_IRQ, 0, sc->sc_irq_res);
bad1:
	bus_release_resource(dev, SYS_RES_MEMORY, BS_BAR, sc->sc_mem_res);
bad:
	return (error);
}

static int
bwi_pci_detach(device_t dev)
{
	struct bwi_pci_softc *psc = device_get_softc(dev);
	struct bwi_softc *sc = &psc->sc_sc;

	/* check if device was removed */
	sc->sc_invalid = !bus_child_present(dev);

	bwi_detach(sc);

	bus_generic_detach(dev);
	bus_teardown_intr(dev, sc->sc_irq_res, sc->sc_irq_handle);
	bus_release_resource(dev, SYS_RES_IRQ, 0, sc->sc_irq_res);

	bus_release_resource(dev, SYS_RES_MEMORY, BS_BAR, sc->sc_mem_res);

	return (0);
}

static int
bwi_pci_shutdown(device_t dev)
{
	struct bwi_pci_softc *psc = device_get_softc(dev);

	bwi_shutdown(&psc->sc_sc);
	return (0);
}

static int
bwi_pci_suspend(device_t dev)
{
	struct bwi_pci_softc *psc = device_get_softc(dev);

	bwi_suspend(&psc->sc_sc);

	return (0);
}

static int
bwi_pci_resume(device_t dev)
{
	struct bwi_pci_softc *psc = device_get_softc(dev);

	bwi_resume(&psc->sc_sc);

	return (0);
}

static device_method_t bwi_pci_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		bwi_pci_probe),
	DEVMETHOD(device_attach,	bwi_pci_attach),
	DEVMETHOD(device_detach,	bwi_pci_detach),
	DEVMETHOD(device_shutdown,	bwi_pci_shutdown),
	DEVMETHOD(device_suspend,	bwi_pci_suspend),
	DEVMETHOD(device_resume,	bwi_pci_resume),

	{ 0,0 }
};
static driver_t bwi_driver = {
	"bwi",
	bwi_pci_methods,
	sizeof (struct bwi_pci_softc)
};
static	devclass_t bwi_devclass;
DRIVER_MODULE(bwi, pci, bwi_driver, bwi_devclass, 0, 0);
MODULE_PNP_INFO("U16:vendor;U16:device;D:#", pci, bwi, bwi_devices,
    nitems(bwi_devices) - 1);
MODULE_DEPEND(bwi, wlan, 1, 1, 1);		/* 802.11 media layer */
MODULE_DEPEND(bwi, firmware, 1, 1, 1);		/* firmware support */
MODULE_DEPEND(bwi, wlan_amrr, 1, 1, 1);
