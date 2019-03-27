/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 1997, Stefan Esser <se@freebsd.org>
 * Copyright (c) 2000, Michael Smith <msmith@freebsd.org>
 * Copyright (c) 2000, BSDi
 * Copyright (c) 2003, Thomas Moestl <tmm@FreeBSD.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice unmodified, this list of conditions, and the following
 *    disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/bus.h>
#include <sys/kernel.h>
#include <sys/libkern.h>
#include <sys/module.h>
#include <sys/pciio.h>

#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>
#include <dev/ofw/ofw_pci.h>
#include <dev/ofw/openfirm.h>

#include <machine/bus.h>
#include <machine/intr_machdep.h>
#include <machine/resource.h>

#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>
#include <dev/pci/pci_private.h>

#include "ofw_pcibus.h"
#include "pcib_if.h"
#include "pci_if.h"

typedef uint32_t ofw_pci_intr_t;

/* Methods */
static device_probe_t ofw_pcibus_probe;
static device_attach_t ofw_pcibus_attach;
static pci_alloc_devinfo_t ofw_pcibus_alloc_devinfo;
static pci_assign_interrupt_t ofw_pcibus_assign_interrupt;
static ofw_bus_get_devinfo_t ofw_pcibus_get_devinfo;
static bus_child_deleted_t ofw_pcibus_child_deleted;
static int ofw_pcibus_child_pnpinfo_str_method(device_t cbdev, device_t child,
    char *buf, size_t buflen);

static void ofw_pcibus_enum_devtree(device_t dev, u_int domain, u_int busno);
static void ofw_pcibus_enum_bus(device_t dev, u_int domain, u_int busno);

static device_method_t ofw_pcibus_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		ofw_pcibus_probe),
	DEVMETHOD(device_attach,	ofw_pcibus_attach),

	/* Bus interface */
	DEVMETHOD(bus_child_deleted,	ofw_pcibus_child_deleted),
	DEVMETHOD(bus_child_pnpinfo_str, ofw_pcibus_child_pnpinfo_str_method),
	DEVMETHOD(bus_rescan,		bus_null_rescan),

	/* PCI interface */
	DEVMETHOD(pci_alloc_devinfo,	ofw_pcibus_alloc_devinfo),
	DEVMETHOD(pci_assign_interrupt, ofw_pcibus_assign_interrupt),

	/* ofw_bus interface */
	DEVMETHOD(ofw_bus_get_devinfo,	ofw_pcibus_get_devinfo),
	DEVMETHOD(ofw_bus_get_compat,	ofw_bus_gen_get_compat),
	DEVMETHOD(ofw_bus_get_model,	ofw_bus_gen_get_model),
	DEVMETHOD(ofw_bus_get_name,	ofw_bus_gen_get_name),
	DEVMETHOD(ofw_bus_get_node,	ofw_bus_gen_get_node),
	DEVMETHOD(ofw_bus_get_type,	ofw_bus_gen_get_type),

	DEVMETHOD_END
};

static devclass_t pci_devclass;

DEFINE_CLASS_1(pci, ofw_pcibus_driver, ofw_pcibus_methods,
    sizeof(struct pci_softc), pci_driver);
EARLY_DRIVER_MODULE(ofw_pcibus, pcib, ofw_pcibus_driver, pci_devclass, 0, 0,
    BUS_PASS_BUS);
MODULE_VERSION(ofw_pcibus, 1);
MODULE_DEPEND(ofw_pcibus, pci, 1, 1, 1);

static int ofw_devices_only = 0;
TUNABLE_INT("hw.pci.ofw_devices_only", &ofw_devices_only);

static int
ofw_pcibus_probe(device_t dev)
{

	if (ofw_bus_get_node(dev) == -1)
		return (ENXIO);
	device_set_desc(dev, "OFW PCI bus");

	return (BUS_PROBE_DEFAULT);
}

static int
ofw_pcibus_attach(device_t dev)
{
	u_int busno, domain;
	int error;

	error = pci_attach_common(dev);
	if (error)
		return (error);
	domain = pcib_get_domain(dev);
	busno = pcib_get_bus(dev);

	/*
	 * Attach those children represented in the device tree.
	 */

	ofw_pcibus_enum_devtree(dev, domain, busno);

	/*
	 * We now attach any laggard devices. FDT, for instance, allows
	 * the device tree to enumerate only some PCI devices. Apple's
	 * OF device tree on some Grackle-based hardware can also miss
	 * functions on multi-function cards.
	 */

	if (!ofw_devices_only)
		ofw_pcibus_enum_bus(dev, domain, busno);

	return (bus_generic_attach(dev));
}

struct pci_devinfo *
ofw_pcibus_alloc_devinfo(device_t dev)
{
	struct ofw_pcibus_devinfo *dinfo;

	dinfo = malloc(sizeof(*dinfo), M_DEVBUF, M_WAITOK | M_ZERO);
	return (&dinfo->opd_dinfo);
}

static void
ofw_pcibus_enum_devtree(device_t dev, u_int domain, u_int busno)
{
	device_t pcib;
	struct ofw_pci_register pcir;
	struct ofw_pcibus_devinfo *dinfo;
	phandle_t node, child;
	u_int func, slot;
	int intline;

	pcib = device_get_parent(dev);
	node = ofw_bus_get_node(dev);

	for (child = OF_child(node); child != 0; child = OF_peer(child)) {
		if (OF_getencprop(child, "reg", (pcell_t *)&pcir,
		    sizeof(pcir)) == -1)
			continue;
		slot = OFW_PCI_PHYS_HI_DEVICE(pcir.phys_hi);
		func = OFW_PCI_PHYS_HI_FUNCTION(pcir.phys_hi);

		/* Some OFW device trees contain dupes. */
		if (pci_find_dbsf(domain, busno, slot, func) != NULL)
			continue;

		/*
		 * The preset in the intline register is usually bogus.  Reset
		 * it such that the PCI code will reroute the interrupt if
		 * needed.
		 */

		intline = PCI_INVALID_IRQ;
		if (OF_getproplen(child, "interrupts") > 0)
			intline = 0;
		PCIB_WRITE_CONFIG(pcib, busno, slot, func, PCIR_INTLINE,
		    intline, 1);

		/*
		 * Now set up the PCI and OFW bus layer devinfo and add it
		 * to the PCI bus.
		 */

		dinfo = (struct ofw_pcibus_devinfo *)pci_read_device(pcib, dev,
		    domain, busno, slot, func);
		if (dinfo == NULL)
			continue;
		if (ofw_bus_gen_setup_devinfo(&dinfo->opd_obdinfo, child) !=
		    0) {
			pci_freecfg((struct pci_devinfo *)dinfo);
			continue;
		}
		dinfo->opd_dma_tag = NULL;
		pci_add_child(dev, (struct pci_devinfo *)dinfo);

		/*
		 * Some devices don't have an intpin set, but do have
		 * interrupts. These are fully specified, and set in the
		 * interrupts property, so add that value to the device's
		 * resource list.
		 */
		if (dinfo->opd_dinfo.cfg.intpin == 0)
			ofw_bus_intr_to_rl(dev, child,
				&dinfo->opd_dinfo.resources, NULL);
	}
}

/*
 * The following is an almost exact clone of pci_add_children(), with the
 * addition that it (a) will not add children that have already been added,
 * and (b) will set up the OFW devinfo to point to invalid values. This is
 * to handle non-enumerated PCI children as exist in FDT and on the second
 * function of the Rage 128 in my Blue & White G3.
 */

static void
ofw_pcibus_enum_bus(device_t dev, u_int domain, u_int busno)
{
	device_t pcib;
	struct ofw_pcibus_devinfo *dinfo;
	int maxslots;
	int s, f, pcifunchigh;
	uint8_t hdrtype;

	pcib = device_get_parent(dev);

	maxslots = PCIB_MAXSLOTS(pcib);
	for (s = 0; s <= maxslots; s++) {
		pcifunchigh = 0;
		f = 0;
		DELAY(1);
		hdrtype = PCIB_READ_CONFIG(pcib, busno, s, f, PCIR_HDRTYPE, 1);
		if ((hdrtype & PCIM_HDRTYPE) > PCI_MAXHDRTYPE)
			continue;
		if (hdrtype & PCIM_MFDEV)
			pcifunchigh = PCI_FUNCMAX;
		for (f = 0; f <= pcifunchigh; f++) {
			/* Filter devices we have already added */
			if (pci_find_dbsf(domain, busno, s, f) != NULL)
				continue;

			dinfo = (struct ofw_pcibus_devinfo *)pci_read_device(
			    pcib, dev, domain, busno, s, f);
			if (dinfo == NULL)
				continue;

			dinfo->opd_dma_tag = NULL;
			dinfo->opd_obdinfo.obd_node = -1;

			dinfo->opd_obdinfo.obd_name = NULL;
			dinfo->opd_obdinfo.obd_compat = NULL;
			dinfo->opd_obdinfo.obd_type = NULL;
			dinfo->opd_obdinfo.obd_model = NULL;

			/*
			 * For non OFW-devices, don't believe 0 
			 * for an interrupt.
			 */
			if (dinfo->opd_dinfo.cfg.intline == 0) {
				dinfo->opd_dinfo.cfg.intline = PCI_INVALID_IRQ;
				PCIB_WRITE_CONFIG(pcib, busno, s, f, 
				    PCIR_INTLINE, PCI_INVALID_IRQ, 1);
			}

			pci_add_child(dev, (struct pci_devinfo *)dinfo);
		}
	}
}

static void
ofw_pcibus_child_deleted(device_t dev, device_t child)
{
	struct ofw_pcibus_devinfo *dinfo;

	dinfo = device_get_ivars(dev);
	ofw_bus_gen_destroy_devinfo(&dinfo->opd_obdinfo);
	pci_child_deleted(dev, child);
}

static int
ofw_pcibus_child_pnpinfo_str_method(device_t cbdev, device_t child, char *buf,
    size_t buflen)
{
	pci_child_pnpinfo_str_method(cbdev, child, buf, buflen);

	if (ofw_bus_get_node(child) != -1)  {
		strlcat(buf, " ", buflen); /* Separate info */
		ofw_bus_gen_child_pnpinfo_str(cbdev, child, buf, buflen);
	}

	return (0);
}
	
static int
ofw_pcibus_assign_interrupt(device_t dev, device_t child)
{
	ofw_pci_intr_t intr[2];
	phandle_t node, iparent;
	int isz, icells;

	node = ofw_bus_get_node(child);

	if (node == -1) {
		/* Non-firmware enumerated child, use standard routing */
	
		intr[0] = pci_get_intpin(child);
		return (PCIB_ROUTE_INTERRUPT(device_get_parent(dev), child, 
		    intr[0]));
	}
	
	/*
	 * Try to determine the node's interrupt parent so we know which
	 * PIC to use.
	 */

	iparent = -1;
	if (OF_getencprop(node, "interrupt-parent", &iparent,
	    sizeof(iparent)) < 0)
		iparent = -1;
	icells = 1;
	if (iparent != -1)
		OF_getencprop(OF_node_from_xref(iparent), "#interrupt-cells",
		    &icells, sizeof(icells));
	
	/*
	 * Any AAPL,interrupts property gets priority and is
	 * fully specified (i.e. does not need routing)
	 */

	isz = OF_getencprop(node, "AAPL,interrupts", intr, sizeof(intr));
	if (isz == sizeof(intr[0])*icells)
		return ((iparent == -1) ? intr[0] : ofw_bus_map_intr(dev,
		    iparent, icells, intr));

	isz = OF_getencprop(node, "interrupts", intr, sizeof(intr));
	if (isz == sizeof(intr[0])*icells) {
		if (iparent != -1)
			intr[0] = ofw_bus_map_intr(dev, iparent, icells, intr);
	} else {
		/* No property: our best guess is the intpin. */
		intr[0] = pci_get_intpin(child);
	}
	
	/*
	 * If we got intr from a property, it may or may not be an intpin.
	 * For on-board devices, it frequently is not, and is completely out
	 * of the valid intpin range.  For PCI slots, it hopefully is,
	 * otherwise we will have trouble interfacing with non-OFW buses
	 * such as cardbus.
	 * Since we cannot tell which it is without violating layering, we
	 * will always use the route_interrupt method, and treat exceptions
	 * on the level they become apparent.
	 */
	return (PCIB_ROUTE_INTERRUPT(device_get_parent(dev), child, intr[0]));
}

static const struct ofw_bus_devinfo *
ofw_pcibus_get_devinfo(device_t bus, device_t dev)
{
	struct ofw_pcibus_devinfo *dinfo;

	dinfo = device_get_ivars(dev);
	return (&dinfo->opd_obdinfo);
}

