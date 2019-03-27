/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2011 Nathan Whitehorn
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

#include <sys/param.h>
#include <sys/bus.h>
#include <sys/kernel.h>
#include <sys/libkern.h>
#include <sys/module.h>
#include <sys/pciio.h>

#include <dev/ofw/openfirm.h>

#include <dev/pci/pcivar.h>
#include <dev/pci/pcireg.h>
#include <dev/pci/pci_private.h>

#include <machine/bus.h>
#include <machine/rtas.h>

#include <powerpc/ofw/ofw_pcibus.h>
#include <powerpc/pseries/plpar_iommu.h>

#include "pci_if.h"
#include "iommu_if.h"

static int		plpar_pcibus_probe(device_t);
static bus_dma_tag_t	plpar_pcibus_get_dma_tag(device_t dev, device_t child);

/*
 * Driver methods.
 */
static device_method_t	plpar_pcibus_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		plpar_pcibus_probe),

	/* IOMMU functions */
	DEVMETHOD(bus_get_dma_tag,	plpar_pcibus_get_dma_tag),
	DEVMETHOD(iommu_map,		phyp_iommu_map),
	DEVMETHOD(iommu_unmap,		phyp_iommu_unmap),

	DEVMETHOD_END
};

static devclass_t pci_devclass;
DEFINE_CLASS_1(pci, plpar_pcibus_driver, plpar_pcibus_methods,
    sizeof(struct pci_softc), ofw_pcibus_driver);
DRIVER_MODULE(plpar_pcibus, pcib, plpar_pcibus_driver, pci_devclass, 0, 0);

static int
plpar_pcibus_probe(device_t dev)
{
	phandle_t rtas;
 
	if (ofw_bus_get_node(dev) == -1 || !rtas_exists())
		return (ENXIO);

	rtas = OF_finddevice("/rtas");
	if (!OF_hasprop(rtas, "ibm,hypertas-functions"))
		return (ENXIO);

	device_set_desc(dev, "POWER Hypervisor PCI bus");

	return (BUS_PROBE_SPECIFIC);
}

static bus_dma_tag_t
plpar_pcibus_get_dma_tag(device_t dev, device_t child)
{
	struct ofw_pcibus_devinfo *dinfo;

	while (device_get_parent(child) != dev)
		child = device_get_parent(child);

	dinfo = device_get_ivars(child);

	if (dinfo->opd_dma_tag != NULL)
		return (dinfo->opd_dma_tag);

	bus_dma_tag_create(bus_get_dma_tag(dev),
	    1, 0, BUS_SPACE_MAXADDR, BUS_SPACE_MAXADDR,
	    NULL, NULL, BUS_SPACE_MAXSIZE, BUS_SPACE_UNRESTRICTED,
	    BUS_SPACE_MAXSIZE, 0, NULL, NULL, &dinfo->opd_dma_tag);
	phyp_iommu_set_dma_tag(dev, child, dinfo->opd_dma_tag);

	return (dinfo->opd_dma_tag);
}

