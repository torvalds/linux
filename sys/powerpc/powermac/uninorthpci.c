/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (C) 2002 Benno Rice.
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
 * THIS SOFTWARE IS PROVIDED BY Benno Rice ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL TOOLS GMBH BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/module.h>
#include <sys/bus.h>
#include <sys/conf.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/rman.h>

#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_pci.h>
#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>
#include <dev/ofw/ofwpci.h>

#include <dev/pci/pcivar.h>
#include <dev/pci/pcireg.h>

#include <machine/bus.h>
#include <machine/intr_machdep.h>
#include <machine/md_var.h>
#include <machine/pio.h>
#include <machine/resource.h>

#include <powerpc/powermac/uninorthvar.h>

#include <vm/vm.h>
#include <vm/pmap.h>

#include "pcib_if.h"

#define	UNINORTH_DEBUG	0

/*
 * Device interface.
 */
static int		uninorth_probe(device_t);
static int		uninorth_attach(device_t);

/*
 * pcib interface.
 */
static u_int32_t	uninorth_read_config(device_t, u_int, u_int, u_int,
			    u_int, int);
static void		uninorth_write_config(device_t, u_int, u_int, u_int,
			    u_int, u_int32_t, int);

/*
 * Local routines.
 */
static int		uninorth_enable_config(struct uninorth_softc *, u_int,
			    u_int, u_int, u_int);

/*
 * Driver methods.
 */
static device_method_t	uninorth_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		uninorth_probe),
	DEVMETHOD(device_attach,	uninorth_attach),

	/* pcib interface */
	DEVMETHOD(pcib_read_config,	uninorth_read_config),
	DEVMETHOD(pcib_write_config,	uninorth_write_config),

	DEVMETHOD_END
};

static devclass_t	uninorth_devclass;

DEFINE_CLASS_1(pcib, uninorth_driver, uninorth_methods,
    sizeof(struct uninorth_softc), ofw_pci_driver);
EARLY_DRIVER_MODULE(uninorth, ofwbus, uninorth_driver, uninorth_devclass, 0, 0,
    BUS_PASS_BUS);

static int
uninorth_probe(device_t dev)
{
	const char	*type, *compatible;

	type = ofw_bus_get_type(dev);
	compatible = ofw_bus_get_compat(dev);

	if (type == NULL || compatible == NULL)
		return (ENXIO);

	if (strcmp(type, "pci") != 0)
		return (ENXIO);

	if (strcmp(compatible, "uni-north") == 0) {
		device_set_desc(dev, "Apple UniNorth Host-PCI bridge");
		return (0);
	} else if (strcmp(compatible, "u3-agp") == 0) {
		device_set_desc(dev, "Apple U3 Host-AGP bridge");
		return (0);
	} else if (strcmp(compatible, "u4-pcie") == 0) {
		device_set_desc(dev, "IBM CPC945 PCI Express Root");
		return (0);
	}
	
	return (ENXIO);
}

static int
uninorth_attach(device_t dev)
{
	struct		uninorth_softc *sc;
	const char	*compatible;
	const char	*name;
	phandle_t	node;
	uint32_t	reg[3];
	uint64_t	regbase;
	cell_t		acells;
	int		unit;

	node = ofw_bus_get_node(dev);
	sc = device_get_softc(dev);
	name = device_get_name(dev);
	unit = device_get_unit(dev);

	if (OF_getprop(node, "reg", reg, sizeof(reg)) < 8)
		return (ENXIO);

	sc->sc_ver = 0;
	compatible = ofw_bus_get_compat(dev);
	if (strcmp(compatible, "u3-agp") == 0)
		sc->sc_ver = 3;
	if (strcmp(compatible, "u4-pcie") == 0)
		sc->sc_ver = 4;

	acells = 1;
	OF_getprop(OF_parent(node), "#address-cells", &acells, sizeof(acells));

	regbase = reg[0];
	if (acells == 2) {
		regbase <<= 32;
		regbase |= reg[1];
	}

	sc->sc_addr = (vm_offset_t)pmap_mapdev(regbase + 0x800000, PAGE_SIZE);
	sc->sc_data = (vm_offset_t)pmap_mapdev(regbase + 0xc00000, PAGE_SIZE);

	if (resource_int_value(name, unit, "skipslot", &sc->sc_skipslot) != 0)
		sc->sc_skipslot = -1;

	mtx_init(&sc->sc_cfg_mtx, "uninorth pcicfg", NULL, MTX_SPIN);

	return (ofw_pci_attach(dev));
}

static u_int32_t
uninorth_read_config(device_t dev, u_int bus, u_int slot, u_int func, u_int reg,
    int width)
{
	struct		uninorth_softc *sc;
	vm_offset_t	caoff;
	u_int32_t	val;

	sc = device_get_softc(dev);
	caoff = sc->sc_data + (reg & 0x07);
	val = 0xffffffff;

	mtx_lock_spin(&sc->sc_cfg_mtx);
	if (uninorth_enable_config(sc, bus, slot, func, reg) != 0) {
		switch (width) {
		case 1: 
			val = in8rb(caoff);
			break;
		case 2:
			val = in16rb(caoff);
			break;
		case 4:
			val = in32rb(caoff);
			break;
		}
	}
	mtx_unlock_spin(&sc->sc_cfg_mtx);

	return (val);
}

static void
uninorth_write_config(device_t dev, u_int bus, u_int slot, u_int func,
    u_int reg, u_int32_t val, int width)
{
	struct		uninorth_softc *sc;
	vm_offset_t	caoff;

	sc = device_get_softc(dev);
	caoff = sc->sc_data + (reg & 0x07);

	mtx_lock_spin(&sc->sc_cfg_mtx);
	if (uninorth_enable_config(sc, bus, slot, func, reg)) {
		switch (width) {
		case 1:
			out8rb(caoff, val);
			break;
		case 2:
			out16rb(caoff, val);
			break;
		case 4:
			out32rb(caoff, val);
			break;
		}
	}
	mtx_unlock_spin(&sc->sc_cfg_mtx);
}

static int
uninorth_enable_config(struct uninorth_softc *sc, u_int bus, u_int slot,
    u_int func, u_int reg)
{
	uint32_t	cfgval;

	mtx_assert(&sc->sc_cfg_mtx, MA_OWNED);

	if (sc->sc_skipslot == slot)
		return (0);

	/*
	 * Issue type 0 configuration space accesses for the root bus.
	 *
	 * NOTE: On U4, issue only type 1 accesses. There is a secret
	 * PCI Express <-> PCI Express bridge not present in the device tree,
	 * and we need to route all of our configuration space through it.
	 */
	if (sc->pci_sc.sc_bus == bus && sc->sc_ver < 4) {
		/*
		 * No slots less than 11 on the primary bus on U3 and lower
		 */
		if (slot < 11)
			return (0);

		cfgval = (1 << slot) | (func << 8) | (reg & 0xfc);
	} else {
		cfgval = (bus << 16) | (slot << 11) | (func << 8) |
		    (reg & 0xfc) | 1;
	}

	/* Set extended register bits on U4 */
	if (sc->sc_ver == 4)
		cfgval |= (reg >> 8) << 28;

	do {
		out32rb(sc->sc_addr, cfgval);
	} while (in32rb(sc->sc_addr) != cfgval);

	return (1);
}

