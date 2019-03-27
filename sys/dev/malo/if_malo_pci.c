/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2007 Marvell Semiconductor, Inc.
 * Copyright (c) 2007 Sam Leffler, Errno Consulting
 * Copyright (c) 2008 Weongyo Jeong <weongyo@freebsd.org>
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
 * PCI front-end for the Marvell 88W8335 Wireless LAN controller driver.
 */

#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/module.h>
#include <sys/socket.h>
#include <sys/sysctl.h>

#include <machine/bus.h>
#include <sys/bus.h>
#include <sys/rman.h>
 
#include <net/if.h>
#include <net/if_var.h>
#include <net/if_media.h>
#include <net/ethernet.h>

#include <net80211/ieee80211_var.h>

#include <dev/malo/if_malo.h>

#include <dev/pci/pcivar.h>
#include <dev/pci/pcireg.h>

/*
 * PCI glue.
 */

#define MALO_RESOURCE_MAX		2
#define MALO_MSI_MESSAGES		1

struct malo_pci_softc {
	struct malo_softc		malo_sc;
	struct resource_spec		*malo_mem_spec;
	struct resource			*malo_res_mem[MALO_RESOURCE_MAX];
	struct resource_spec		*malo_irq_spec;
	struct resource			*malo_res_irq[MALO_MSI_MESSAGES];
	void				*malo_intrhand[MALO_MSI_MESSAGES];
	int				malo_msi;
};

/*
 * Tunable variables.
 */
SYSCTL_DECL(_hw_malo);
static SYSCTL_NODE(_hw_malo, OID_AUTO, pci, CTLFLAG_RD, 0,
    "Marvell 88W8335 driver PCI parameters");

static int msi_disable = 0;				/* MSI disabled  */
SYSCTL_INT(_hw_malo_pci, OID_AUTO, msi_disable, CTLFLAG_RWTUN, &msi_disable,
	    0, "MSI disabled");

/*
 * Devices supported by this driver.
 */
#define	VENDORID_MARVELL		0X11AB
#define	DEVICEID_MRVL_88W8310		0X1FA7
#define	DEVICEID_MRVL_88W8335R1		0X1FAA
#define	DEVICEID_MRVL_88W8335R2		0X1FAB

static struct malo_product {
	uint16_t			mp_vendorid;
	uint16_t			mp_deviceid;
	const char			*mp_name;
} malo_products[] = {
	{ VENDORID_MARVELL, DEVICEID_MRVL_88W8310,
	    "Marvell Libertas 88W8310 802.11g Wireless Adapter" },
	{ VENDORID_MARVELL, DEVICEID_MRVL_88W8335R1,
	    "Marvell Libertas 88W8335 802.11g Wireless Adapter" },
	{ VENDORID_MARVELL, DEVICEID_MRVL_88W8335R2,
	    "Marvell Libertas 88W8335 802.11g Wireless Adapter" }
};

static struct resource_spec malo_res_spec_mem[] = {
	{ SYS_RES_MEMORY,	PCIR_BAR(0),	RF_ACTIVE },
	{ SYS_RES_MEMORY,	PCIR_BAR(1),	RF_ACTIVE },
	{ -1,			0,		0 }
};

static struct resource_spec malo_res_spec_legacy[] = {
	{ SYS_RES_IRQ,		0,		RF_ACTIVE | RF_SHAREABLE },
	{ -1,			0,		0 }
};

static struct resource_spec malo_res_spec_msi[] = {
	{ SYS_RES_IRQ,		1,		RF_ACTIVE },
	{ -1,			0,		0 }
};

static int	malo_pci_detach(device_t);

static int
malo_pci_probe(device_t dev)
{
	struct malo_product *mp;
	uint16_t vendor, devid;
	int i;

	vendor = pci_get_vendor(dev);
	devid = pci_get_device(dev);
	mp = malo_products;

	for (i = 0; i < nitems(malo_products); i++, mp++) {
		if (vendor == mp->mp_vendorid && devid == mp->mp_deviceid) {
			device_set_desc(dev, mp->mp_name);
			return (BUS_PROBE_DEFAULT);
		}
	}

	return (ENXIO);
}

static int
malo_pci_attach(device_t dev)
{
	int error = ENXIO, i, msic, reg;
	struct malo_pci_softc *psc = device_get_softc(dev);
	struct malo_softc *sc = &psc->malo_sc;

	sc->malo_dev = dev;
	
	pci_enable_busmaster(dev);

	/* 
	 * Setup memory-mapping of PCI registers.
	 */
	psc->malo_mem_spec = malo_res_spec_mem;
	error = bus_alloc_resources(dev, psc->malo_mem_spec, psc->malo_res_mem);
	if (error) {
		device_printf(dev, "couldn't allocate memory resources\n");
		return (ENXIO);
	}

	/*
	 * Arrange and allocate interrupt line.
	 */
	sc->malo_invalid = 1;

	if (pci_find_cap(dev, PCIY_EXPRESS, &reg) == 0) {
		msic = pci_msi_count(dev);
		if (bootverbose)
			device_printf(dev, "MSI count : %d\n", msic);
	} else
		msic = 0;

	psc->malo_irq_spec = malo_res_spec_legacy;
	if (msic == MALO_MSI_MESSAGES && msi_disable == 0) {
		if (pci_alloc_msi(dev, &msic) == 0) {
			if (msic == MALO_MSI_MESSAGES) {
				device_printf(dev, "Using %d MSI messages\n",
				    msic);
				psc->malo_irq_spec = malo_res_spec_msi;
				psc->malo_msi = 1;
			} else
				pci_release_msi(dev);
		}
	}

	error = bus_alloc_resources(dev, psc->malo_irq_spec, psc->malo_res_irq);
	if (error) {
		device_printf(dev, "couldn't allocate IRQ resources\n");
		goto bad;
	}

	if (psc->malo_msi == 0)
		error = bus_setup_intr(dev, psc->malo_res_irq[0],
		    INTR_TYPE_NET | INTR_MPSAFE, malo_intr, NULL, sc,
		    &psc->malo_intrhand[0]);
	else {
		for (i = 0; i < MALO_MSI_MESSAGES; i++) {
			error = bus_setup_intr(dev, psc->malo_res_irq[i],
			    INTR_TYPE_NET | INTR_MPSAFE, malo_intr, NULL, sc,
			    &psc->malo_intrhand[i]);
			if (error != 0)
				break;
		}
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
			       0,			/* nsegments */
			       BUS_SPACE_MAXSIZE,	/* maxsegsize */
			       0,			/* flags */
			       NULL,			/* lockfunc */
			       NULL,			/* lockarg */
			       &sc->malo_dmat)) {
		device_printf(dev, "cannot allocate DMA tag\n");
		goto bad1;
	}

	sc->malo_io0t = rman_get_bustag(psc->malo_res_mem[0]);
	sc->malo_io0h = rman_get_bushandle(psc->malo_res_mem[0]);
	sc->malo_io1t = rman_get_bustag(psc->malo_res_mem[1]);
	sc->malo_io1h = rman_get_bushandle(psc->malo_res_mem[1]);

	error = malo_attach(pci_get_device(dev), sc);

	if (error != 0)
		goto bad2;

	return (error);

bad2:
	bus_dma_tag_destroy(sc->malo_dmat);
bad1:
	if (psc->malo_msi == 0)
		bus_teardown_intr(dev, psc->malo_res_irq[0],
		    psc->malo_intrhand[0]);
	else {
		for (i = 0; i < MALO_MSI_MESSAGES; i++)
			bus_teardown_intr(dev, psc->malo_res_irq[i],
			    psc->malo_intrhand[i]);
	}
	bus_release_resources(dev, psc->malo_irq_spec, psc->malo_res_irq);
bad:
	if (psc->malo_msi != 0)
		pci_release_msi(dev);
	bus_release_resources(dev, psc->malo_mem_spec, psc->malo_res_mem);

	return (error);
}

static int
malo_pci_detach(device_t dev)
{
	int i;
	struct malo_pci_softc *psc = device_get_softc(dev);
	struct malo_softc *sc = &psc->malo_sc;

	/* check if device was removed */
	sc->malo_invalid = !bus_child_present(dev);

	malo_detach(sc);

	bus_generic_detach(dev);

	if (psc->malo_msi == 0)
		bus_teardown_intr(dev, psc->malo_res_irq[0],
		    psc->malo_intrhand[0]);
	else {
		for (i = 0; i < MALO_MSI_MESSAGES; i++)
			bus_teardown_intr(dev, psc->malo_res_irq[i],
			    psc->malo_intrhand[i]);

		pci_release_msi(dev);
	}

	bus_release_resources(dev, psc->malo_irq_spec, psc->malo_res_irq);
	bus_dma_tag_destroy(sc->malo_dmat);
	bus_release_resources(dev, psc->malo_mem_spec, psc->malo_res_mem);

	return (0);
}

static int
malo_pci_shutdown(device_t dev)
{
	struct malo_pci_softc *psc = device_get_softc(dev);

	malo_shutdown(&psc->malo_sc);

	return (0);
}

static int
malo_pci_suspend(device_t dev)
{
	struct malo_pci_softc *psc = device_get_softc(dev);

	malo_suspend(&psc->malo_sc);

	return (0);
}

static int
malo_pci_resume(device_t dev)
{
	struct malo_pci_softc *psc = device_get_softc(dev);

	malo_resume(&psc->malo_sc);

	return (0);
}

static device_method_t malo_pci_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		malo_pci_probe),
	DEVMETHOD(device_attach,	malo_pci_attach),
	DEVMETHOD(device_detach,	malo_pci_detach),
	DEVMETHOD(device_shutdown,	malo_pci_shutdown),
	DEVMETHOD(device_suspend,	malo_pci_suspend),
	DEVMETHOD(device_resume,	malo_pci_resume),
	{ 0,0 }
};

static driver_t malo_pci_driver = {
	"malo",
	malo_pci_methods,
	sizeof(struct malo_pci_softc)
};

static	devclass_t malo_devclass;
DRIVER_MODULE(malo, pci, malo_pci_driver, malo_devclass, 0, 0);
MODULE_VERSION(malo, 1);
MODULE_DEPEND(malo, wlan, 1, 1, 1);		/* 802.11 media layer */
MODULE_DEPEND(malo, firmware, 1, 1, 1);
