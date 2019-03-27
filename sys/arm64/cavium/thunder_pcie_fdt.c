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
#include "opt_platform.h"

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

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

#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>
#include <dev/pci/pci_host_generic.h>
#include <dev/pci/pci_host_generic_fdt.h>
#include <dev/pci/pcib_private.h>

#include "thunder_pcie_common.h"

#include "pcib_if.h"

#ifdef THUNDERX_PASS_1_1_ERRATA
static struct resource * thunder_pcie_fdt_alloc_resource(device_t, device_t,
    int, int *, rman_res_t, rman_res_t, rman_res_t, u_int);
#endif
static int thunder_pcie_fdt_attach(device_t);
static int thunder_pcie_fdt_probe(device_t);
static int thunder_pcie_fdt_get_id(device_t, device_t, enum pci_id_type,
    uintptr_t *);

static device_method_t thunder_pcie_fdt_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		thunder_pcie_fdt_probe),
	DEVMETHOD(device_attach,	thunder_pcie_fdt_attach),
#ifdef THUNDERX_PASS_1_1_ERRATA
	DEVMETHOD(bus_alloc_resource,	thunder_pcie_fdt_alloc_resource),
#endif

	/* pcib interface */
	DEVMETHOD(pcib_get_id,		thunder_pcie_fdt_get_id),

	/* End */
	DEVMETHOD_END
};

DEFINE_CLASS_1(pcib, thunder_pcie_fdt_driver, thunder_pcie_fdt_methods,
    sizeof(struct generic_pcie_fdt_softc), generic_pcie_fdt_driver);

static devclass_t thunder_pcie_fdt_devclass;

DRIVER_MODULE(thunder_pcib, simplebus, thunder_pcie_fdt_driver,
    thunder_pcie_fdt_devclass, 0, 0);
DRIVER_MODULE(thunder_pcib, ofwbus, thunder_pcie_fdt_driver,
    thunder_pcie_fdt_devclass, 0, 0);


static int
thunder_pcie_fdt_probe(device_t dev)
{

	/* Check if we're running on Cavium ThunderX */
	if (!CPU_MATCH(CPU_IMPL_MASK | CPU_PART_MASK,
	    CPU_IMPL_CAVIUM, CPU_PART_THUNDERX, 0, 0))
		return (ENXIO);

	if (!ofw_bus_status_okay(dev))
		return (ENXIO);

	if (ofw_bus_is_compatible(dev, "pci-host-ecam-generic") ||
	    ofw_bus_is_compatible(dev, "cavium,thunder-pcie") ||
	    ofw_bus_is_compatible(dev, "cavium,pci-host-thunder-ecam")) {
		device_set_desc(dev, "Cavium Integrated PCI/PCI-E Controller");
		return (BUS_PROBE_DEFAULT);
	}

	return (ENXIO);
}

static int
thunder_pcie_fdt_attach(device_t dev)
{
	struct generic_pcie_fdt_softc *sc;

	sc = device_get_softc(dev);
	thunder_pcie_identify_ecam(dev, &sc->base.ecam);
	sc->base.coherent = 1;

	return (pci_host_generic_attach(dev));
}

static int
thunder_pcie_fdt_get_id(device_t pci, device_t child, enum pci_id_type type,
    uintptr_t *id)
{
	phandle_t node;
	int bsf;

	if (type != PCI_ID_MSI)
		return (pcib_get_id(pci, child, type, id));

	node = ofw_bus_get_node(pci);
	if (OF_hasprop(node, "msi-map"))
		return (generic_pcie_get_id(pci, child, type, id));

	bsf = pci_get_rid(child);
	*id = (pci_get_domain(child) << PCI_RID_DOMAIN_SHIFT) | bsf;

	return (0);
}

#ifdef THUNDERX_PASS_1_1_ERRATA
static struct resource *
thunder_pcie_fdt_alloc_resource(device_t dev, device_t child, int type, int *rid,
    rman_res_t start, rman_res_t end, rman_res_t count, u_int flags)
{

	if ((int)ofw_bus_get_node(child) > 0)
		return (pci_host_generic_alloc_resource(dev, child,
		    type, rid, start, end, count, flags));

	return (thunder_pcie_alloc_resource(dev, child,
	    type, rid, start, end, count, flags));
}
#endif
