/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2000 Michael Smith
 * Copyright (c) 2000 BSDi
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
#include <sys/systm.h>
#include <sys/module.h>
#include <sys/bus.h>
#include <sys/malloc.h>
#include <sys/kernel.h>

#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_pci.h>
#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>

#include <dev/pci/pcivar.h>
#include <dev/pci/pcireg.h>
#include <dev/pci/pcib_private.h>

#include <machine/intr_machdep.h>

#include "pcib_if.h"

static int	ofw_pcib_pci_probe(device_t bus);
static int	ofw_pcib_pci_attach(device_t bus);
static phandle_t ofw_pcib_pci_get_node(device_t bus, device_t dev);
static int	ofw_pcib_pci_route_interrupt(device_t bridge, device_t dev,
		    int intpin);

static device_method_t ofw_pcib_pci_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		ofw_pcib_pci_probe),
	DEVMETHOD(device_attach,	ofw_pcib_pci_attach),

	/* pcib interface */
	DEVMETHOD(pcib_route_interrupt,	ofw_pcib_pci_route_interrupt),
	DEVMETHOD(pcib_request_feature,	pcib_request_feature_allow),

	/* ofw_bus interface */
	DEVMETHOD(ofw_bus_get_node,	ofw_pcib_pci_get_node),

	DEVMETHOD_END
};

static devclass_t pcib_devclass;

struct ofw_pcib_softc {
        /*
         * This is here so that we can use pci bridge methods, too - the
         * generic routines only need the dev, secbus and subbus members
         * filled.
         */
        struct pcib_softc       ops_pcib_sc;
	phandle_t		ops_node;
        struct ofw_bus_iinfo    ops_iinfo;
};

DEFINE_CLASS_1(pcib, ofw_pcib_pci_driver, ofw_pcib_pci_methods,
    sizeof(struct ofw_pcib_softc), pcib_driver);
EARLY_DRIVER_MODULE(ofw_pcib, pci, ofw_pcib_pci_driver, pcib_devclass, 0, 0,
    BUS_PASS_BUS);

static int
ofw_pcib_pci_probe(device_t dev)
{

	if ((pci_get_class(dev) != PCIC_BRIDGE) ||
	    (pci_get_subclass(dev) != PCIS_BRIDGE_PCI)) {
		return (ENXIO);
	}

	if (ofw_bus_get_node(dev) == -1)
		return (ENXIO);

	device_set_desc(dev, "OFW PCI-PCI bridge");
	return (0);
}

static int
ofw_pcib_pci_attach(device_t dev)
{
	struct ofw_pcib_softc *sc;

	sc = device_get_softc(dev);
	sc->ops_pcib_sc.dev = dev;
	sc->ops_node = ofw_bus_get_node(dev);

	ofw_bus_setup_iinfo(sc->ops_node, &sc->ops_iinfo,
	    sizeof(cell_t));

	pcib_attach_common(dev);
	return (pcib_attach_child(dev));
}

static phandle_t
ofw_pcib_pci_get_node(device_t bridge, device_t dev)
{
	/* We have only one child, the PCI bus, so pass it our node */

	return (ofw_bus_get_node(bridge));
}

static int
ofw_pcib_pci_route_interrupt(device_t bridge, device_t dev, int intpin)
{
	struct ofw_pcib_softc *sc;
	struct ofw_bus_iinfo *ii;
	struct ofw_pci_register reg;
	cell_t pintr, mintr[2];
	int intrcells;
	phandle_t iparent;

	sc = device_get_softc(bridge);
	ii = &sc->ops_iinfo;
	if (ii->opi_imapsz > 0) {
		pintr = intpin;

		/* Fabricate imap information if this isn't an OFW device */
		bzero(&reg, sizeof(reg));
		reg.phys_hi = (pci_get_bus(dev) << OFW_PCI_PHYS_HI_BUSSHIFT) |
		    (pci_get_slot(dev) << OFW_PCI_PHYS_HI_DEVICESHIFT) |
		    (pci_get_function(dev) << OFW_PCI_PHYS_HI_FUNCTIONSHIFT);

		intrcells = ofw_bus_lookup_imap(ofw_bus_get_node(dev), ii, &reg,
		    sizeof(reg), &pintr, sizeof(pintr), mintr, sizeof(mintr),
		    &iparent);
		if (intrcells) {
			/*
			 * If we've found a mapping, return it and don't map
			 * it again on higher levels - that causes problems
			 * in some cases, and never seems to be required.
			 */
			mintr[0] = ofw_bus_map_intr(dev, iparent, intrcells,
			    mintr);
			return (mintr[0]);
		}
	} else if (intpin >= 1 && intpin <= 4) {
		/*
		 * When an interrupt map is missing, we need to do the
		 * standard PCI swizzle and continue mapping at the parent.
		 */
		return (pcib_route_interrupt(bridge, dev, intpin));
	}
	return (PCIB_ROUTE_INTERRUPT(device_get_parent(device_get_parent(
	    bridge)), bridge, intpin));
}

