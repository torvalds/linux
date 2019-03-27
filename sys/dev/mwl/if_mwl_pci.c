/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2007-2009 Sam Leffler, Errno Consulting
 * Copyright (c) 2007-2009 Marvell Semiconductor, Inc.
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
#ifdef __FreeBSD__
__FBSDID("$FreeBSD$");
#endif

/*
 * PCI front-end for the Marvell Wireless LAN controller driver.
 */

#include <sys/param.h>
#include <sys/systm.h> 
#include <sys/module.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/mutex.h>
#include <sys/errno.h>

#include <machine/bus.h>
#include <machine/resource.h>
#include <sys/bus.h>
#include <sys/rman.h>

#include <sys/socket.h>
 
#include <net/ethernet.h>
#include <net/if.h>
#include <net/if_media.h>
#include <net/if_arp.h>
#include <net/route.h>

#include <net80211/ieee80211_var.h>

#include <dev/mwl/if_mwlvar.h>

#include <dev/pci/pcivar.h>
#include <dev/pci/pcireg.h>

/*
 * PCI glue.
 */

struct mwl_pci_softc {
	struct mwl_softc	sc_sc;
	struct resource		*sc_sr0;	/* BAR0 memory resource */
	struct resource		*sc_sr1;	/* BAR1 memory resource */
	struct resource		*sc_irq;	/* irq resource */
	void			*sc_ih;		/* interrupt handler */
};

#define	BS_BAR0	0x10
#define	BS_BAR1	0x14

struct mwl_pci_ident {
	uint16_t	vendor;
	uint16_t	device;
	const char	*name;
};

static const struct mwl_pci_ident mwl_pci_ids[] = {
	{ 0x11ab, 0x2a02, "Marvell 88W8363" },
	{ 0x11ab, 0x2a03, "Marvell 88W8363" },
	{ 0x11ab, 0x2a0a, "Marvell 88W8363" },
	{ 0x11ab, 0x2a0b, "Marvell 88W8363" },
	{ 0x11ab, 0x2a0c, "Marvell 88W8363" },
	{ 0x11ab, 0x2a21, "Marvell 88W8363" },
	{ 0x11ab, 0x2a24, "Marvell 88W8363" },

	{ 0, 0, NULL }
};

const static struct mwl_pci_ident *
mwl_pci_lookup(int vendor, int device)
{
	const struct mwl_pci_ident *ident;

	for (ident = mwl_pci_ids; ident->name != NULL; ident++)
		if (vendor == ident->vendor && device == ident->device)
			return ident;
	return NULL;
}

static int
mwl_pci_probe(device_t dev)
{
	const struct mwl_pci_ident *ident;

	ident = mwl_pci_lookup(pci_get_vendor(dev), pci_get_device(dev));
	if (ident != NULL) {
		device_set_desc(dev, ident->name);
		return BUS_PROBE_DEFAULT;
	}
	return ENXIO;
}

static int
mwl_pci_attach(device_t dev)
{
	struct mwl_pci_softc *psc = device_get_softc(dev);
	struct mwl_softc *sc = &psc->sc_sc;
	int rid, error = ENXIO;

	sc->sc_dev = dev;

	pci_enable_busmaster(dev);

	/* 
	 * Setup memory-mapping of PCI registers.
	 */
	rid = BS_BAR0;
	psc->sc_sr0 = bus_alloc_resource_any(dev, SYS_RES_MEMORY, &rid,
					    RF_ACTIVE);
	if (psc->sc_sr0 == NULL) {
		device_printf(dev, "cannot map BAR0 register space\n");
		goto bad;
	}
	rid = BS_BAR1;
	psc->sc_sr1 = bus_alloc_resource_any(dev, SYS_RES_MEMORY, &rid,
					    RF_ACTIVE);
	if (psc->sc_sr1 == NULL) {
		device_printf(dev, "cannot map BAR1 register space\n");
		goto bad1;
	}
	sc->sc_invalid = 1;

	/*
	 * Arrange interrupt line.
	 */
	rid = 0;
	psc->sc_irq = bus_alloc_resource_any(dev, SYS_RES_IRQ, &rid,
					     RF_SHAREABLE|RF_ACTIVE);
	if (psc->sc_irq == NULL) {
		device_printf(dev, "could not map interrupt\n");
		goto bad2;
	}
	if (bus_setup_intr(dev, psc->sc_irq,
			   INTR_TYPE_NET | INTR_MPSAFE,
			   NULL, mwl_intr, sc, &psc->sc_ih)) {
		device_printf(dev, "could not establish interrupt\n");
		goto bad3;
	}

	/*
	 * Setup DMA descriptor area.
	 */
	if (bus_dma_tag_create(bus_get_dma_tag(dev),	/* parent */
			       1, 0,			/* alignment, bounds */
			       BUS_SPACE_MAXADDR_32BIT,	/* lowaddr */
			       BUS_SPACE_MAXADDR,	/* highaddr */
			       NULL, NULL,		/* filter, filterarg */
			       BUS_SPACE_MAXSIZE,	/* maxsize */
			       MWL_TXDESC,		/* nsegments */
			       BUS_SPACE_MAXSIZE,	/* maxsegsize */
			       0,			/* flags */
			       NULL,			/* lockfunc */
			       NULL,			/* lockarg */
			       &sc->sc_dmat)) {
		device_printf(dev, "cannot allocate DMA tag\n");
		goto bad4;
	}

	/*
	 * Finish off the attach.
	 */
	MWL_LOCK_INIT(sc);
	sc->sc_io0t = rman_get_bustag(psc->sc_sr0);
	sc->sc_io0h = rman_get_bushandle(psc->sc_sr0);
	sc->sc_io1t = rman_get_bustag(psc->sc_sr1);
	sc->sc_io1h = rman_get_bushandle(psc->sc_sr1);
	if (mwl_attach(pci_get_device(dev), sc) == 0)
		return (0);

	MWL_LOCK_DESTROY(sc);
	bus_dma_tag_destroy(sc->sc_dmat);
bad4:
	bus_teardown_intr(dev, psc->sc_irq, psc->sc_ih);
bad3:
	bus_release_resource(dev, SYS_RES_IRQ, 0, psc->sc_irq);
bad2:
	bus_release_resource(dev, SYS_RES_MEMORY, BS_BAR1, psc->sc_sr1);
bad1:
	bus_release_resource(dev, SYS_RES_MEMORY, BS_BAR0, psc->sc_sr0);
bad:
	return (error);
}

static int
mwl_pci_detach(device_t dev)
{
	struct mwl_pci_softc *psc = device_get_softc(dev);
	struct mwl_softc *sc = &psc->sc_sc;

	/* check if device was removed */
	sc->sc_invalid = !bus_child_present(dev);

	mwl_detach(sc);

	bus_generic_detach(dev);
	bus_teardown_intr(dev, psc->sc_irq, psc->sc_ih);
	bus_release_resource(dev, SYS_RES_IRQ, 0, psc->sc_irq);

	bus_dma_tag_destroy(sc->sc_dmat);
	bus_release_resource(dev, SYS_RES_MEMORY, BS_BAR1, psc->sc_sr1);
	bus_release_resource(dev, SYS_RES_MEMORY, BS_BAR0, psc->sc_sr0);

	MWL_LOCK_DESTROY(sc);

	return (0);
}

static int
mwl_pci_shutdown(device_t dev)
{
	struct mwl_pci_softc *psc = device_get_softc(dev);

	mwl_shutdown(&psc->sc_sc);
	return (0);
}

static int
mwl_pci_suspend(device_t dev)
{
	struct mwl_pci_softc *psc = device_get_softc(dev);

	mwl_suspend(&psc->sc_sc);

	return (0);
}

static int
mwl_pci_resume(device_t dev)
{
	struct mwl_pci_softc *psc = device_get_softc(dev);

	pci_enable_busmaster(dev);

	mwl_resume(&psc->sc_sc);

	return (0);
}

static device_method_t mwl_pci_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		mwl_pci_probe),
	DEVMETHOD(device_attach,	mwl_pci_attach),
	DEVMETHOD(device_detach,	mwl_pci_detach),
	DEVMETHOD(device_shutdown,	mwl_pci_shutdown),
	DEVMETHOD(device_suspend,	mwl_pci_suspend),
	DEVMETHOD(device_resume,	mwl_pci_resume),

	{ 0,0 }
};
static driver_t mwl_pci_driver = {
	"mwl",
	mwl_pci_methods,
	sizeof (struct mwl_pci_softc)
};
static	devclass_t mwl_devclass;
DRIVER_MODULE(mwl, pci, mwl_pci_driver, mwl_devclass, 0, 0);
MODULE_VERSION(mwl, 1);
MODULE_DEPEND(mwl, wlan, 1, 1, 1);		/* 802.11 media layer */
MODULE_DEPEND(mwl, firmware, 1, 1, 1);
