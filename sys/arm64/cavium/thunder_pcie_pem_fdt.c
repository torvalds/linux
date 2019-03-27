/*
 * Copyright (C) 2016 Cavium Inc.
 * All rights reserved.
 *
 * Developed by Semihalf.
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

#include "opt_platform.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/malloc.h>
#include <sys/types.h>
#include <sys/sysctl.h>
#include <sys/kernel.h>
#include <sys/rman.h>
#include <sys/module.h>
#include <sys/bus.h>
#include <sys/endian.h>
#include <sys/cpuset.h>

#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>

#include <dev/pci/pcivar.h>
#include <dev/pci/pcireg.h>
#include <dev/pci/pcib_private.h>
#include <dev/pci/pci_host_generic.h>

#include <machine/intr.h>

#include "thunder_pcie_common.h"
#include "thunder_pcie_pem.h"

#include "pcib_if.h"

static int thunder_pem_fdt_probe(device_t);
static int thunder_pem_fdt_alloc_msix(device_t, device_t, int *);
static int thunder_pem_fdt_release_msix(device_t, device_t, int);
static int thunder_pem_fdt_alloc_msi(device_t, device_t, int, int, int *);
static int thunder_pem_fdt_release_msi(device_t, device_t, int, int *);
static int thunder_pem_fdt_map_msi(device_t, device_t, int, uint64_t *,
    uint32_t *);
static int thunder_pem_fdt_get_id(device_t, device_t, enum pci_id_type,
    uintptr_t *);

static device_method_t thunder_pem_fdt_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		thunder_pem_fdt_probe),

	/* pcib interface */
	DEVMETHOD(pcib_alloc_msix,	thunder_pem_fdt_alloc_msix),
	DEVMETHOD(pcib_release_msix,	thunder_pem_fdt_release_msix),
	DEVMETHOD(pcib_alloc_msi,	thunder_pem_fdt_alloc_msi),
	DEVMETHOD(pcib_release_msi,	thunder_pem_fdt_release_msi),
	DEVMETHOD(pcib_map_msi,		thunder_pem_fdt_map_msi),
	DEVMETHOD(pcib_get_id,		thunder_pem_fdt_get_id),

	/* End */
	DEVMETHOD_END
};

DEFINE_CLASS_1(pcib, thunder_pem_fdt_driver, thunder_pem_fdt_methods,
    sizeof(struct thunder_pem_softc), thunder_pem_driver);

static devclass_t thunder_pem_fdt_devclass;

DRIVER_MODULE(thunder_pem, simplebus, thunder_pem_fdt_driver,
    thunder_pem_fdt_devclass, 0, 0);
DRIVER_MODULE(thunder_pem, ofwbus, thunder_pem_fdt_driver,
    thunder_pem_fdt_devclass, 0, 0);

static int
thunder_pem_fdt_probe(device_t dev)
{

	if (!ofw_bus_status_okay(dev))
		return (ENXIO);

	if (ofw_bus_is_compatible(dev, "cavium,pci-host-thunder-pem")) {
		device_set_desc(dev, THUNDER_PEM_DESC);
		return (BUS_PROBE_DEFAULT);
	}

	return (ENXIO);
}

static int
thunder_pem_fdt_alloc_msi(device_t pci, device_t child, int count, int maxcount,
    int *irqs)
{
	phandle_t msi_parent;
	int err;

	err = ofw_bus_msimap(ofw_bus_get_node(pci), pci_get_rid(child),
	    &msi_parent, NULL);
	if (err != 0)
		return (err);
	return (intr_alloc_msi(pci, child, msi_parent, count, maxcount,
	    irqs));
}

static int
thunder_pem_fdt_release_msi(device_t pci, device_t child, int count, int *irqs)
{
	phandle_t msi_parent;
	int err;

	err = ofw_bus_msimap(ofw_bus_get_node(pci), pci_get_rid(child),
	    &msi_parent, NULL);
	if (err != 0)
		return (err);
	return (intr_release_msi(pci, child, msi_parent, count, irqs));
}

static int
thunder_pem_fdt_alloc_msix(device_t pci, device_t child, int *irq)
{
	phandle_t msi_parent;
	int err;

	err = ofw_bus_msimap(ofw_bus_get_node(pci), pci_get_rid(child),
	    &msi_parent, NULL);
	if (err != 0)
		return (err);
	return (intr_alloc_msix(pci, child, msi_parent, irq));
}

static int
thunder_pem_fdt_release_msix(device_t pci, device_t child, int irq)
{
	phandle_t msi_parent;
	int err;

	err = ofw_bus_msimap(ofw_bus_get_node(pci), pci_get_rid(child),
	    &msi_parent, NULL);
	if (err != 0)
		return (err);
	return (intr_release_msix(pci, child, msi_parent, irq));
}

static int
thunder_pem_fdt_map_msi(device_t pci, device_t child, int irq, uint64_t *addr,
    uint32_t *data)
{
	phandle_t msi_parent;
	int err;

	err = ofw_bus_msimap(ofw_bus_get_node(pci), pci_get_rid(child),
	    &msi_parent, NULL);
	if (err != 0)
		return (err);
	return (intr_map_msi(pci, child, msi_parent, irq, addr, data));
}

static int
thunder_pem_fdt_get_id(device_t dev, device_t child, enum pci_id_type type,
    uintptr_t *id)
{
	phandle_t node;
	int err;
	uint32_t rid;
	uint16_t pci_rid;

	if (type != PCI_ID_MSI)
		return (pcib_get_id(dev, child, type, id));

	node = ofw_bus_get_node(dev);
	pci_rid = pci_get_rid(child);

	err = ofw_bus_msimap(node, pci_rid, NULL, &rid);
	if (err != 0)
		return (err);
	*id = rid;

	return (0);
}
