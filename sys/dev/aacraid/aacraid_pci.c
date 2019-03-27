/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2000 Michael Smith
 * Copyright (c) 2001 Scott Long
 * Copyright (c) 2000 BSDi
 * Copyright (c) 2001-2010 Adaptec, Inc.
 * Copyright (c) 2010-2012 PMC-Sierra, Inc.
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

/*
 * PCI bus interface and resource allocation.
 */

#include "opt_aacraid.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/module.h>

#include <sys/bio.h>
#include <sys/bus.h>
#include <sys/conf.h>
#include <sys/disk.h>

#include <machine/bus.h>
#include <machine/resource.h>
#include <sys/rman.h>

#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>

#include <dev/aacraid/aacraid_reg.h>
#include <sys/aac_ioctl.h>
#include <dev/aacraid/aacraid_debug.h>
#include <dev/aacraid/aacraid_var.h>

static int	aacraid_pci_probe(device_t dev);
static int	aacraid_pci_attach(device_t dev);

static device_method_t aacraid_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		aacraid_pci_probe),
	DEVMETHOD(device_attach,	aacraid_pci_attach),
	DEVMETHOD(device_detach,	aacraid_detach),
	DEVMETHOD(device_suspend,	aacraid_suspend),
	DEVMETHOD(device_resume,	aacraid_resume),

	DEVMETHOD(bus_print_child,	bus_generic_print_child),
	DEVMETHOD(bus_driver_added,	bus_generic_driver_added),
	{ 0, 0 }
};

static driver_t aacraid_pci_driver = {
	"aacraid",
	aacraid_methods,
	sizeof(struct aac_softc)
};

static devclass_t	aacraid_devclass;

struct aac_ident
{
	u_int16_t		vendor;
	u_int16_t		device;
	u_int16_t		subvendor;
	u_int16_t		subdevice;
	int			hwif;
	int			quirks;
	char			*desc;
} aacraid_family_identifiers[] = {
	{0x9005, 0x028b, 0, 0, AAC_HWIF_SRC, 0,
	 "Adaptec RAID Controller"},
	{0x9005, 0x028c, 0, 0, AAC_HWIF_SRCV, 0,
	 "Adaptec RAID Controller"},
	{0x9005, 0x028d, 0, 0, AAC_HWIF_SRCV, 0,
	 "Adaptec RAID Controller"},
	{0, 0, 0, 0, 0, 0, 0}
};

DRIVER_MODULE(aacraid, pci, aacraid_pci_driver, aacraid_devclass, 0, 0);
MODULE_PNP_INFO("U16:vendor;U16:device", pci, aacraid,
    aacraid_family_identifiers,
    nitems(aacraid_family_identifiers) - 1);
MODULE_DEPEND(aacraid, pci, 1, 1, 1);

static struct aac_ident *
aac_find_ident(device_t dev)
{
	struct aac_ident *m;
	u_int16_t vendid, devid;

	vendid = pci_get_vendor(dev);
	devid = pci_get_device(dev);

	for (m = aacraid_family_identifiers; m->vendor != 0; m++) {
		if ((m->vendor == vendid) && (m->device == devid))
			return (m);
	}

	return (NULL);
}

/*
 * Determine whether this is one of our supported adapters.
 */
static int
aacraid_pci_probe(device_t dev)
{
	struct aac_ident *id;

	fwprintf(NULL, HBA_FLAGS_DBG_FUNCTION_ENTRY_B, "");

	if ((id = aac_find_ident(dev)) != NULL) {
		device_set_desc(dev, id->desc);
		return(BUS_PROBE_DEFAULT);
	}
	return(ENXIO);
}

/*
 * Allocate resources for our device, set up the bus interface.
 */
static int
aacraid_pci_attach(device_t dev)
{
	struct aac_softc *sc;
	struct aac_ident *id;
	int error;
	u_int32_t command;

	fwprintf(NULL, HBA_FLAGS_DBG_FUNCTION_ENTRY_B, "");

	/*
	 * Initialise softc.
	 */
	sc = device_get_softc(dev);
	bzero(sc, sizeof(*sc));
	sc->aac_dev = dev;

	/* assume failure is 'not configured' */
	error = ENXIO;

	/* 
	 * Verify that the adapter is correctly set up in PCI space.
	 */
	pci_enable_busmaster(dev);
	command = pci_read_config(sc->aac_dev, PCIR_COMMAND, 2);
	if (!(command & PCIM_CMD_BUSMASTEREN)) {
		device_printf(sc->aac_dev, "can't enable bus-master feature\n");
		goto out;
	}

	/* 
	 * Detect the hardware interface version, set up the bus interface
	 * indirection.
	 */
	id = aac_find_ident(dev);
	sc->aac_hwif = id->hwif;
	switch(sc->aac_hwif) {
	case AAC_HWIF_SRC:
		fwprintf(sc, HBA_FLAGS_DBG_INIT_B, "set hardware up for PMC SRC");
		sc->aac_if = aacraid_src_interface;
		break;
	case AAC_HWIF_SRCV:
		fwprintf(sc, HBA_FLAGS_DBG_INIT_B, "set hardware up for PMC SRCv");
		sc->aac_if = aacraid_srcv_interface;
		break;
	default:
		sc->aac_hwif = AAC_HWIF_UNKNOWN;
		device_printf(sc->aac_dev, "unknown hardware type\n");
		error = ENXIO;
		goto out;
	}

	/* assume failure is 'out of memory' */
	error = ENOMEM;

	/*
	 * Allocate the PCI register window.
	 */
	sc->aac_regs_rid0 = PCIR_BAR(0);
	if ((sc->aac_regs_res0 = bus_alloc_resource_any(sc->aac_dev,
	    SYS_RES_MEMORY, &sc->aac_regs_rid0, RF_ACTIVE)) == NULL) {
		device_printf(sc->aac_dev,
		    "couldn't allocate register window 0\n");
		goto out;
	}
	sc->aac_btag0 = rman_get_bustag(sc->aac_regs_res0);
	sc->aac_bhandle0 = rman_get_bushandle(sc->aac_regs_res0);

	sc->aac_regs_rid1 = PCIR_BAR(2);
	if ((sc->aac_regs_res1 = bus_alloc_resource_any(sc->aac_dev,
	    SYS_RES_MEMORY, &sc->aac_regs_rid1, RF_ACTIVE)) == NULL) {
		device_printf(sc->aac_dev,
		    "couldn't allocate register window 1\n");
		goto out;
	}
	sc->aac_btag1 = rman_get_bustag(sc->aac_regs_res1);
	sc->aac_bhandle1 = rman_get_bushandle(sc->aac_regs_res1);

	/*
	 * Allocate the parent bus DMA tag appropriate for our PCI interface.
	 * 
	 * Note that some of these controllers are 64-bit capable.
	 */
	if (bus_dma_tag_create(bus_get_dma_tag(dev), 	/* parent */
			       PAGE_SIZE, 0,		/* algnmnt, boundary */
			       BUS_SPACE_MAXADDR,	/* lowaddr */
			       BUS_SPACE_MAXADDR, 	/* highaddr */
			       NULL, NULL, 		/* filter, filterarg */
			       BUS_SPACE_MAXSIZE_32BIT,	/* maxsize */
			       BUS_SPACE_UNRESTRICTED,	/* nsegments */
			       BUS_SPACE_MAXSIZE_32BIT,	/* maxsegsize */
			       0,			/* flags */
			       NULL, NULL,		/* No locking needed */
			       &sc->aac_parent_dmat)) {
		device_printf(sc->aac_dev, "can't allocate parent DMA tag\n");
		goto out;
	}

	/* Set up quirks */
	sc->flags = id->quirks;

	/*
	 * Do bus-independent initialisation.
	 */
	error = aacraid_attach(sc);

out:
	if (error)
		aacraid_free(sc);
	return(error);
}
