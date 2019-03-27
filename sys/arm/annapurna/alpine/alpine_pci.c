/*-
 * Copyright (c) 2015,2016 Annapurna Labs Ltd. and affiliates
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
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 * Alpine PCI/PCI-Express controller driver.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/bus.h>
#include <sys/rman.h>
#include <sys/intr.h>

#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>
#include <dev/pci/pci_host_generic.h>
#include <dev/pci/pci_host_generic_fdt.h>
#include <dev/pci/pcivar.h>
#include <dev/pci/pcireg.h>

#include "pcib_if.h"

#include "contrib/alpine-hal/al_hal_unit_adapter_regs.h"
#include "contrib/alpine-hal/al_hal_pcie.h"
#include "contrib/alpine-hal/al_hal_pcie_axi_reg.h"

#define ANNAPURNA_VENDOR_ID		0x1c36

/* Forward prototypes */
static int al_pcib_probe(device_t);
static int al_pcib_attach(device_t);
static void al_pcib_fixup(device_t);

static struct ofw_compat_data compat_data[] = {
	{"annapurna-labs,al-internal-pcie",	true},
	{"annapurna-labs,alpine-internal-pcie",	true},
	{NULL,					false}
};

/*
 * Bus interface definitions.
 */
static device_method_t al_pcib_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,			al_pcib_probe),
	DEVMETHOD(device_attach,		al_pcib_attach),

	DEVMETHOD_END
};

DEFINE_CLASS_1(pcib, al_pcib_driver, al_pcib_methods,
    sizeof(struct generic_pcie_fdt_softc), generic_pcie_fdt_driver);

static devclass_t anpa_pcib_devclass;

DRIVER_MODULE(alpine_pcib, simplebus, al_pcib_driver, anpa_pcib_devclass, 0, 0);
DRIVER_MODULE(alpine_pcib, ofwbus, al_pcib_driver, anpa_pcib_devclass, 0, 0);

static int
al_pcib_probe(device_t dev)
{

	if (!ofw_bus_status_okay(dev))
		return (ENXIO);

	if (!ofw_bus_search_compatible(dev, compat_data)->ocd_data)
		return (ENXIO);

	device_set_desc(dev,
	    "Annapurna-Labs Integrated Internal PCI-E Controller");
	return (BUS_PROBE_DEFAULT);
}

static int
al_pcib_attach(device_t dev)
{
	int rv;

	rv = pci_host_generic_attach(dev);

	/* Annapurna quirk: configure vendor-specific registers */
	if (rv == 0)
		al_pcib_fixup(dev);

	return (rv);
}

static void
al_pcib_fixup(device_t dev)
{
	uint32_t val;
	uint16_t vid;
	uint8_t hdrtype;
	int bus, slot, func, maxfunc;

	/* Fixup is only needed on bus 0 */
	bus = 0;
	for (slot = 0; slot <= PCI_SLOTMAX; slot++) {
		maxfunc = 0;
		for (func = 0; func <= maxfunc; func++) {
			hdrtype = PCIB_READ_CONFIG(dev, bus, slot, func,
			    PCIR_HDRTYPE, 1);

			if ((hdrtype & PCIM_HDRTYPE) > PCI_MAXHDRTYPE)
				continue;

			if (func == 0 && (hdrtype & PCIM_MFDEV) != 0)
				maxfunc = PCI_FUNCMAX;

			vid = PCIB_READ_CONFIG(dev, bus, slot, func,
			    PCIR_VENDOR, 2);
			if (vid == ANNAPURNA_VENDOR_ID) {
				val = PCIB_READ_CONFIG(dev, bus, slot, func,
				    AL_PCI_AXI_CFG_AND_CTR_0, 4);
				val |= PCIE_AXI_PF_AXI_ATTR_OVRD_FUNC_CTRL_2_PF_VEC_PH_VEC_OVRD_FROM_AXUSER_MASK;
				PCIB_WRITE_CONFIG(dev, bus, slot, func,
				    AL_PCI_AXI_CFG_AND_CTR_0, val, 4);

				val = PCIB_READ_CONFIG(dev, bus, slot, func,
				    AL_PCI_APP_CONTROL, 4);
				val &= ~0xffff;
				val |= PCIE_AXI_PF_AXI_ATTR_OVRD_FUNC_CTRL_4_PF_VEC_MEM_ADDR54_63_SEL_TGTID_MASK;
				PCIB_WRITE_CONFIG(dev, bus, slot, func,
				    AL_PCI_APP_CONTROL, val, 4);
			}
		}
	}
}
