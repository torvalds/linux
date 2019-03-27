/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 1997, Stefan Esser <se@freebsd.org>
 * Copyright (c) 2000, Michael Smith <msmith@freebsd.org>
 * Copyright (c) 2000, BSDi
 * Copyright (c) 2003, Thomas Moestl <tmm@FreeBSD.org>
 * Copyright (c) 2005 - 2009 Marius Strobl <marius@FreeBSD.org>
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

#include "opt_ofw_pci.h"

#include <sys/param.h>
#include <sys/bus.h>
#include <sys/kernel.h>
#include <sys/libkern.h>
#include <sys/module.h>
#include <sys/pciio.h>

#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_pci.h>
#include <dev/ofw/openfirm.h>

#include <machine/bus.h>
#ifndef SUN4V
#include <machine/bus_common.h>
#include <machine/iommureg.h>
#endif
#include <machine/resource.h>

#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>
#include <dev/pci/pci_private.h>

#include <sparc64/pci/ofw_pci.h>

#include "pcib_if.h"
#include "pci_if.h"

/* Helper functions */
static void ofw_pcibus_setup_device(device_t bridge, uint32_t clock,
    u_int busno, u_int slot, u_int func);

/* Methods */
static bus_child_deleted_t ofw_pcibus_child_deleted;
static bus_child_pnpinfo_str_t ofw_pcibus_pnpinfo_str;
static device_attach_t ofw_pcibus_attach;
static device_probe_t ofw_pcibus_probe;
static ofw_bus_get_devinfo_t ofw_pcibus_get_devinfo;
static pci_alloc_devinfo_t ofw_pcibus_alloc_devinfo;
static pci_assign_interrupt_t ofw_pcibus_assign_interrupt;

static device_method_t ofw_pcibus_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		ofw_pcibus_probe),
	DEVMETHOD(device_attach,	ofw_pcibus_attach),

	/* Bus interface */
	DEVMETHOD(bus_child_deleted,	ofw_pcibus_child_deleted),
	DEVMETHOD(bus_child_pnpinfo_str, ofw_pcibus_pnpinfo_str),
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

struct ofw_pcibus_devinfo {
	struct pci_devinfo	opd_dinfo;
	struct ofw_bus_devinfo	opd_obdinfo;
};

static devclass_t pci_devclass;

DEFINE_CLASS_1(pci, ofw_pcibus_driver, ofw_pcibus_methods,
    sizeof(struct pci_softc), pci_driver);
EARLY_DRIVER_MODULE(ofw_pcibus, pcib, ofw_pcibus_driver, pci_devclass, 0, 0,
    BUS_PASS_BUS);
MODULE_VERSION(ofw_pcibus, 1);
MODULE_DEPEND(ofw_pcibus, pci, 1, 1, 1);

static int
ofw_pcibus_probe(device_t dev)
{

	if (ofw_bus_get_node(dev) == -1)
		return (ENXIO);
	device_set_desc(dev, "OFW PCI bus");

	return (0);
}

/*
 * Perform miscellaneous setups the firmware usually does not do for us.
 */
static void
ofw_pcibus_setup_device(device_t bridge, uint32_t clock, u_int busno,
    u_int slot, u_int func)
{
#define	CS_READ(n, w)							\
	PCIB_READ_CONFIG(bridge, busno, slot, func, (n), (w))
#define	CS_WRITE(n, v, w)						\
	PCIB_WRITE_CONFIG(bridge, busno, slot, func, (n), (v), (w))

#ifndef SUN4V
	uint32_t reg;

	/*
	 * Initialize the latency timer register for busmaster devices to
	 * work properly.  This is another task which the firmware doesn't
	 * always perform.  The Min_Gnt register can be used to compute its
	 * recommended value: it contains the desired latency in units of
	 * 1/4 us assuming a clock rate of 33MHz.  To calculate the correct
	 * latency timer value, the clock frequency of the bus (defaulting
	 * to 33MHz) should be used and no wait states assumed.
	 * For bridges, we additionally set up the bridge control and the
	 * secondary latency registers.
	 */
	if ((CS_READ(PCIR_HDRTYPE, 1) & PCIM_HDRTYPE) ==
	    PCIM_HDRTYPE_BRIDGE) {
		reg = CS_READ(PCIR_BRIDGECTL_1, 1);
		reg |= PCIB_BCR_MASTER_ABORT_MODE | PCIB_BCR_SERR_ENABLE |
		    PCIB_BCR_PERR_ENABLE;
#ifdef OFW_PCI_DEBUG
		device_printf(bridge,
		    "bridge %d/%d/%d: control 0x%x -> 0x%x\n",
		    busno, slot, func, CS_READ(PCIR_BRIDGECTL_1, 1), reg);
#endif /* OFW_PCI_DEBUG */
		CS_WRITE(PCIR_BRIDGECTL_1, reg, 1);

		reg = OFW_PCI_LATENCY;
#ifdef OFW_PCI_DEBUG
		device_printf(bridge,
		    "bridge %d/%d/%d: latency timer %d -> %d\n",
		    busno, slot, func, CS_READ(PCIR_SECLAT_1, 1), reg);
#endif /* OFW_PCI_DEBUG */
		CS_WRITE(PCIR_SECLAT_1, reg, 1);
	} else {
		reg = CS_READ(PCIR_MINGNT, 1);
		if ((int)reg > 0) {
			switch (clock) {
			case 33000000:
				reg *= 8;
				break;
			case 66000000:
				reg *= 4;
				break;
			}
			reg = min(reg, 255);
		} else
			reg = OFW_PCI_LATENCY;
	}
#ifdef OFW_PCI_DEBUG
	device_printf(bridge, "device %d/%d/%d: latency timer %d -> %d\n",
	    busno, slot, func, CS_READ(PCIR_LATTIMER, 1), reg);
#endif /* OFW_PCI_DEBUG */
	CS_WRITE(PCIR_LATTIMER, reg, 1);

	/*
	 * Compute a value to write into the cache line size register.
	 * The role of the streaming cache is unclear in write invalidate
	 * transfers, so it is made sure that it's line size is always
	 * reached.  Generally, the cache line size is fixed at 64 bytes
	 * by Fireplane/Safari, JBus and UPA.
	 */
	CS_WRITE(PCIR_CACHELNSZ, STRBUF_LINESZ / sizeof(uint32_t), 1);
#endif

	/*
	 * Ensure that ALi M5229 report the actual content of PCIR_PROGIF
	 * and that IDE I/O is force enabled.  The former is done in order
	 * to have unique behavior across revisions as some default to
	 * hiding bits 4-6 for compliance with PCI 2.3.  The latter is done
	 * as at least revision 0xc8 requires the PCIM_CMD_PORTEN bypass
	 * to be always enabled as otherwise even enabling PCIM_CMD_PORTEN
	 * results in an instant data access trap on Fire-based machines.
	 * Thus these quirks have to be handled before pci(4) adds the maps.
	 * Note that for older revisions bit 0 of register 0x50 enables the
	 * internal IDE function instead of force enabling IDE I/O.
	 */
	if ((CS_READ(PCIR_VENDOR, 2) == 0x10b9 &&
	    CS_READ(PCIR_DEVICE, 2) == 0x5229))
		CS_WRITE(0x50, CS_READ(0x50, 1) | 0x3, 1);

	/*
	 * The preset in the intline register is usually wrong.  Reset
	 * it to 255, so that the PCI code will reroute the interrupt if
	 * needed.
	 */
	CS_WRITE(PCIR_INTLINE, PCI_INVALID_IRQ, 1);

#undef CS_READ
#undef CS_WRITE
}

static int
ofw_pcibus_attach(device_t dev)
{
	device_t pcib;
	struct ofw_pci_register pcir;
	struct ofw_pcibus_devinfo *dinfo;
	phandle_t node, child;
	uint32_t clock;
	u_int busno, domain, func, slot;
	int error;

	error = pci_attach_common(dev);
	if (error)
		return (error);
	pcib = device_get_parent(dev);
	domain = pcib_get_domain(dev);
	busno = pcib_get_bus(dev);
	node = ofw_bus_get_node(dev);

	/*
	 * Add the PCI side of the host-PCI bridge itself to the bus.
	 * Note that we exclude the host-PCIe bridges here as these
	 * have no configuration space implemented themselves.
	 */
	if (strcmp(device_get_name(device_get_parent(pcib)), "nexus") == 0 &&
	    ofw_bus_get_type(pcib) != NULL &&
	    strcmp(ofw_bus_get_type(pcib), OFW_TYPE_PCIE) != 0 &&
	    (dinfo = (struct ofw_pcibus_devinfo *)pci_read_device(pcib, dev,
	    domain, busno, 0, 0)) != NULL) {
		if (ofw_bus_gen_setup_devinfo(&dinfo->opd_obdinfo, node) != 0)
			pci_freecfg((struct pci_devinfo *)dinfo);
		else
			pci_add_child(dev, (struct pci_devinfo *)dinfo);
	}

	if (OF_getprop(ofw_bus_get_node(pcib), "clock-frequency", &clock,
	    sizeof(clock)) == -1)
		clock = 33000000;
	for (child = OF_child(node); child != 0; child = OF_peer(child)) {
		if (OF_getprop(child, "reg", &pcir, sizeof(pcir)) == -1)
			continue;
		slot = OFW_PCI_PHYS_HI_DEVICE(pcir.phys_hi);
		func = OFW_PCI_PHYS_HI_FUNCTION(pcir.phys_hi);
		/* Some OFW device trees contain dupes. */
		if (pci_find_dbsf(domain, busno, slot, func) != NULL)
			continue;
		ofw_pcibus_setup_device(pcib, clock, busno, slot, func);
		dinfo = (struct ofw_pcibus_devinfo *)pci_read_device(pcib, dev,
		    domain, busno, slot, func);
		if (dinfo == NULL)
			continue;
		if (ofw_bus_gen_setup_devinfo(&dinfo->opd_obdinfo, child) !=
		    0) {
			pci_freecfg((struct pci_devinfo *)dinfo);
			continue;
		}
		pci_add_child(dev, (struct pci_devinfo *)dinfo);
		OFW_PCI_SETUP_DEVICE(pcib, dinfo->opd_dinfo.cfg.dev);
	}

	return (bus_generic_attach(dev));
}

struct pci_devinfo *
ofw_pcibus_alloc_devinfo(device_t dev)
{
	struct ofw_pcibus_devinfo *dinfo;

	dinfo = malloc(sizeof(*dinfo), M_DEVBUF, M_WAITOK | M_ZERO);
	return (&dinfo->opd_dinfo);
}

static int
ofw_pcibus_assign_interrupt(device_t dev, device_t child)
{
	ofw_pci_intr_t intr;
	int isz;

	isz = OF_getprop(ofw_bus_get_node(child), "interrupts", &intr,
	    sizeof(intr));
	if (isz != sizeof(intr)) {
		/* No property; our best guess is the intpin. */
		intr = pci_get_intpin(child);
#ifndef SUN4V
	} else if (intr >= 255) {
		/*
		 * A fully specified interrupt (including IGN), as present on
		 * SPARCengine Ultra AX and E450.  Extract the INO and return
		 * it.
		 */
		return (INTINO(intr));
#endif
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
	return (PCIB_ROUTE_INTERRUPT(device_get_parent(dev), child, intr));
}

static const struct ofw_bus_devinfo *
ofw_pcibus_get_devinfo(device_t bus, device_t dev)
{
	struct ofw_pcibus_devinfo *dinfo;

	dinfo = device_get_ivars(dev);
	return (&dinfo->opd_obdinfo);
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
ofw_pcibus_pnpinfo_str(device_t dev, device_t child, char *buf,
    size_t buflen)
{

	pci_child_pnpinfo_str_method(dev, child, buf, buflen);
	if (ofw_bus_get_node(child) != -1)  {
		strlcat(buf, " ", buflen); /* Separate info. */
		ofw_bus_gen_child_pnpinfo_str(dev, child, buf, buflen);
	}

	return (0);
}
