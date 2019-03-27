/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1994,1995 Stefan Esser, Wolfgang StanglMeier
 * Copyright (c) 2000 Michael Smith <msmith@freebsd.org>
 * Copyright (c) 2000 BSDi
 * Copyright (c) 2001 - 2003 Thomas Moestl <tmm@FreeBSD.org>
 * Copyright (c) 2009 by Marius Strobl <marius@FreeBSD.org>
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
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
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
 *
 *	from: FreeBSD: src/sys/dev/pci/pci_pci.c,v 1.3 2000/12/13
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "opt_ofw_pci.h"

#include <sys/param.h>
#include <sys/bus.h>
#include <sys/kernel.h>
#include <sys/libkern.h>
#include <sys/module.h>
#include <sys/rman.h>

#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/openfirm.h>

#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>
#include <dev/pci/pcib_private.h>

#include "pcib_if.h"

#include <sparc64/pci/ofw_pci.h>
#include <sparc64/pci/ofw_pcib_subr.h>

#define	PCI_DEVID_ALI_M5249	0x524910b9
#define	PCI_VENDOR_PLX		0x10b5

static device_probe_t ofw_pcib_probe;
static device_attach_t ofw_pcib_attach;
static ofw_pci_setup_device_t ofw_pcib_setup_device;

static device_method_t ofw_pcib_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		ofw_pcib_probe),
	DEVMETHOD(device_attach,	ofw_pcib_attach),

	/* Bus interface */

	/* pcib interface */
	DEVMETHOD(pcib_route_interrupt, ofw_pcib_gen_route_interrupt),
	DEVMETHOD(pcib_request_feature,	pcib_request_feature_allow),

	/* ofw_bus interface */
	DEVMETHOD(ofw_bus_get_node,	ofw_pcib_gen_get_node),
	DEVMETHOD(ofw_pci_setup_device, ofw_pcib_setup_device),

	DEVMETHOD_END
};

static devclass_t pcib_devclass;

DEFINE_CLASS_1(pcib, ofw_pcib_driver, ofw_pcib_methods,
    sizeof(struct ofw_pcib_gen_softc), pcib_driver);
EARLY_DRIVER_MODULE(ofw_pcib, pci, ofw_pcib_driver, pcib_devclass, NULL, NULL,
    BUS_PASS_BUS);
MODULE_DEPEND(ofw_pcib, pci, 1, 1, 1);

static int
ofw_pcib_probe(device_t dev)
{
	char desc[sizeof("OFW PCIe-PCIe bridge")];
	const char *dtype, *pbdtype;

#define	ISDTYPE(dtype, type)						\
	(((dtype) != NULL) && strcmp((dtype), (type)) == 0)

	if ((pci_get_class(dev) == PCIC_BRIDGE) &&
	    (pci_get_subclass(dev) == PCIS_BRIDGE_PCI) &&
	    ofw_bus_get_node(dev) != 0) {
		dtype = ofw_bus_get_type(dev);
		pbdtype = ofw_bus_get_type(device_get_parent(
		    device_get_parent(dev)));
		snprintf(desc, sizeof(desc), "OFW PCI%s-PCI%s bridge",
		    ISDTYPE(pbdtype, OFW_TYPE_PCIE) ? "e" : "",
		    ISDTYPE(dtype, OFW_TYPE_PCIE) ? "e" : "");
		device_set_desc_copy(dev, desc);
		return (BUS_PROBE_DEFAULT);
	}

#undef ISDTYPE

	return (ENXIO);
}

static int
ofw_pcib_attach(device_t dev)
{
	struct ofw_pcib_gen_softc *sc;

	sc = device_get_softc(dev);

	switch (pci_get_devid(dev)) {
	/*
	 * The ALi M5249 found in Fire-based machines by definition must me
	 * subtractive as they have a ISA bridge on their secondary side but
	 * don't indicate this in the class code although the ISA I/O range
	 * isn't included in their bridge decode.
	 */
	case PCI_DEVID_ALI_M5249:
		sc->ops_pcib_sc.flags |= PCIB_SUBTRACTIVE;
		break;
	}

	switch (pci_get_vendor(dev)) {
	/*
	 * Concurrently write the primary and secondary bus numbers in order
	 * to work around a bug in PLX PEX 8114 causing the internal shadow
	 * copies of these not to be updated when setting them bytewise.
	 */
	case PCI_VENDOR_PLX:
		pci_write_config(dev, PCIR_PRIBUS_1,
		    pci_read_config(dev, PCIR_SECBUS_1, 1) << 8 |
		    pci_read_config(dev, PCIR_PRIBUS_1, 1), 2);
		break;
	}

	ofw_pcib_gen_setup(dev);
	pcib_attach_common(dev);
	return (pcib_attach_child(dev));
}

static void
ofw_pcib_setup_device(device_t bus, device_t child)
{
	int i;
	uint16_t reg;

	switch (pci_get_vendor(bus)) {
	/*
	 * For PLX PEX 8532 issue 64 TLPs to the child from the downstream
	 * port to the child device in order to work around a hardware bug.
	 */
	case PCI_VENDOR_PLX:
		for (i = 0, reg = 0; i < 64; i++)
			reg |= pci_get_devid(child);
		break;
	}

	OFW_PCI_SETUP_DEVICE(device_get_parent(bus), child);
}
