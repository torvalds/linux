/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2000 Doug Rabson
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
#include <sys/malloc.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/bus.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/proc.h>

#include <dev/agp/agppriv.h>
#include <dev/agp/agpreg.h>
#include <dev/pci/pcivar.h>
#include <dev/pci/pcireg.h>

#include <vm/vm.h>
#include <vm/vm_object.h>
#include <vm/pmap.h>

#define	REG_GARTCTRL	0
#define	REG_APSIZE	1
#define	REG_ATTBASE	2

struct agp_via_softc {
	struct agp_softc agp;
	u_int32_t	initial_aperture; /* aperture size at startup */
	struct agp_gatt *gatt;
	int		*regs;
};

static int via_v2_regs[] = { AGP_VIA_GARTCTRL, AGP_VIA_APSIZE,
    AGP_VIA_ATTBASE };
static int via_v3_regs[] = { AGP3_VIA_GARTCTRL, AGP3_VIA_APSIZE,
    AGP3_VIA_ATTBASE };

static const char*
agp_via_match(device_t dev)
{
	if (pci_get_class(dev) != PCIC_BRIDGE
	    || pci_get_subclass(dev) != PCIS_BRIDGE_HOST)
		return NULL;

	if (agp_find_caps(dev) == 0)
		return NULL;

	switch (pci_get_devid(dev)) {
	case 0x01981106:
		return ("VIA 8763 (P4X600) host to PCI bridge");
	case 0x02591106:
		return ("VIA PM800/PN800/PM880/PN880 host to PCI bridge");
	case 0x02691106:
		return ("VIA KT880 host to PCI bridge");
	case 0x02961106:
		return ("VIA 3296 (P4M800) host to PCI bridge");
	case 0x03051106:
		return ("VIA 82C8363 (Apollo KT133x/KM133) host to PCI bridge");
	case 0x03141106:
		return ("VIA 3314 (P4M800CE) host to PCI bridge");
	case 0x03241106:
		return ("VIA VT3324 (CX700) host to PCI bridge");
	case 0x03271106:
		return ("VIA 3327 (P4M890) host to PCI bridge");
	case 0x03641106:
		return ("VIA 3364 (P4M900) host to PCI bridge");
	case 0x03911106:
		return ("VIA 8371 (Apollo KX133) host to PCI bridge");
	case 0x05011106:
		return ("VIA 8501 (Apollo MVP4) host to PCI bridge");
	case 0x05971106:
		return ("VIA 82C597 (Apollo VP3) host to PCI bridge");
	case 0x05981106:
		return ("VIA 82C598 (Apollo MVP3) host to PCI bridge");
	case 0x06011106:
		return ("VIA 8601 (Apollo ProMedia/PLE133Ta) host to PCI bridge");
	case 0x06051106:
		return ("VIA 82C694X (Apollo Pro 133A) host to PCI bridge");
	case 0x06911106:
		return ("VIA 82C691 (Apollo Pro) host to PCI bridge");
	case 0x30911106:
		return ("VIA 8633 (Pro 266) host to PCI bridge");
	case 0x30991106:
		return ("VIA 8367 (KT266/KY266x/KT333) host to PCI bridge");
	case 0x31011106:
		return ("VIA 8653 (Pro266T) host to PCI bridge");
	case 0x31121106:
		return ("VIA 8361 (KLE133) host to PCI bridge");
	case 0x31161106:
		return ("VIA XM266 (PM266/KM266) host to PCI bridge");
	case 0x31231106:
		return ("VIA 862x (CLE266) host to PCI bridge");
	case 0x31281106:
		return ("VIA 8753 (P4X266) host to PCI bridge");
	case 0x31481106:
		return ("VIA 8703 (P4M266x/P4N266) host to PCI bridge");
	case 0x31561106:
		return ("VIA XN266 (Apollo Pro266) host to PCI bridge");
	case 0x31681106:
		return ("VIA 8754 (PT800) host to PCI bridge");
	case 0x31891106:
		return ("VIA 8377 (Apollo KT400/KT400A/KT600) host to PCI bridge");
	case 0x32051106:
		return ("VIA 8235/8237 (Apollo KM400/KM400A) host to PCI bridge");
	case 0x32081106:
		return ("VIA 8783 (PT890) host to PCI bridge");
	case 0x32581106:
		return ("VIA PT880 host to PCI bridge");
	case 0xb1981106:
		return ("VIA VT83xx/VT87xx/KTxxx/Px8xx host to PCI bridge");
	}

	return NULL;
}

static int
agp_via_probe(device_t dev)
{
	const char *desc;

	if (resource_disabled("agp", device_get_unit(dev)))
		return (ENXIO);
	desc = agp_via_match(dev);
	if (desc) {
		device_set_desc(dev, desc);
		return BUS_PROBE_DEFAULT;
	}

	return ENXIO;
}

static int
agp_via_attach(device_t dev)
{
	struct agp_via_softc *sc = device_get_softc(dev);
	struct agp_gatt *gatt;
	int error;
	u_int32_t agpsel;
	u_int32_t capid;

	sc->regs = via_v2_regs;

	/* Look at the capability register to see if we handle AGP3 */
	capid = pci_read_config(dev, agp_find_caps(dev) + AGP_CAPID, 4);
	if (((capid >> 20) & 0x0f) >= 3) { 
		agpsel = pci_read_config(dev, AGP_VIA_AGPSEL, 1);
		if ((agpsel & (1 << 1)) == 0)
			sc->regs = via_v3_regs;
	}
	
	error = agp_generic_attach(dev);
	if (error)
		return error;

	sc->initial_aperture = AGP_GET_APERTURE(dev);

	for (;;) {
		gatt = agp_alloc_gatt(dev);
		if (gatt)
			break;

		/*
		 * Probably contigmalloc failure. Try reducing the
		 * aperture so that the gatt size reduces.
		 */
		if (AGP_SET_APERTURE(dev, AGP_GET_APERTURE(dev) / 2)) {
			agp_generic_detach(dev);
			return ENOMEM;
		}
	}
	sc->gatt = gatt;

	if (sc->regs == via_v2_regs) {
		/* Install the gatt. */
		pci_write_config(dev, sc->regs[REG_ATTBASE], gatt->ag_physical | 3, 4);
		
		/* Enable the aperture. */
		pci_write_config(dev, sc->regs[REG_GARTCTRL], 0x0f, 4);
	} else {
		u_int32_t gartctrl;

		/* Install the gatt. */
		pci_write_config(dev, sc->regs[REG_ATTBASE], gatt->ag_physical, 4);
		
		/* Enable the aperture. */
		gartctrl = pci_read_config(dev, sc->regs[REG_GARTCTRL], 4);
		pci_write_config(dev, sc->regs[REG_GARTCTRL], gartctrl | (3 << 7), 4);
	}

	device_printf(dev, "aperture size is %dM\n",
		sc->initial_aperture / 1024 / 1024);

	return 0;
}

static int
agp_via_detach(device_t dev)
{
	struct agp_via_softc *sc = device_get_softc(dev);

	agp_free_cdev(dev);

	pci_write_config(dev, sc->regs[REG_GARTCTRL], 0, 4);
	pci_write_config(dev, sc->regs[REG_ATTBASE], 0, 4);
	AGP_SET_APERTURE(dev, sc->initial_aperture);
	agp_free_gatt(sc->gatt);
	agp_free_res(dev);

	return 0;
}

static u_int32_t
agp_via_get_aperture(device_t dev)
{
	struct agp_via_softc *sc = device_get_softc(dev);
	u_int32_t apsize;

	if (sc->regs == via_v2_regs) {
		apsize = pci_read_config(dev, sc->regs[REG_APSIZE], 1);

		/*
		 * The size is determined by the number of low bits of
		 * register APBASE which are forced to zero. The low 20 bits
		 * are always forced to zero and each zero bit in the apsize
		 * field just read forces the corresponding bit in the 27:20
		 * to be zero. We calculate the aperture size accordingly.
		 */
		return (((apsize ^ 0xff) << 20) | ((1 << 20) - 1)) + 1;
	} else {
		apsize = pci_read_config(dev, sc->regs[REG_APSIZE], 2) & 0xfff;
		switch (apsize) {
		case 0x800:
			return 0x80000000;
		case 0xc00:
			return 0x40000000;
		case 0xe00:
			return 0x20000000;
		case 0xf00:
			return 0x10000000;
		case 0xf20:
			return 0x08000000;
		case 0xf30:
			return 0x04000000;
		case 0xf38:
			return 0x02000000;
		case 0xf3c:
			return 0x01000000;
		case 0xf3e:
			return 0x00800000;
		case 0xf3f:
			return 0x00400000;
		default:
			device_printf(dev, "Invalid aperture setting 0x%x\n",
			    pci_read_config(dev, sc->regs[REG_APSIZE], 2));
			return 0;
		}
	}
}

static int
agp_via_set_aperture(device_t dev, u_int32_t aperture)
{
	struct agp_via_softc *sc = device_get_softc(dev);
	u_int32_t apsize, key, val;

	if (sc->regs == via_v2_regs) {
		/*
		 * Reverse the magic from get_aperture.
		 */
		apsize = ((aperture - 1) >> 20) ^ 0xff;

		/*
	 	 * Double check for sanity.
	 	 */
		if ((((apsize ^ 0xff) << 20) | ((1 << 20) - 1)) + 1 != aperture)
			return EINVAL;

		pci_write_config(dev, sc->regs[REG_APSIZE], apsize, 1);
	} else {
		switch (aperture) {
		case 0x80000000:
			key = 0x800;
			break;
		case 0x40000000:
			key = 0xc00;
			break;
		case 0x20000000:
			key = 0xe00;
			break;
		case 0x10000000:
			key = 0xf00;
			break;
		case 0x08000000:
			key = 0xf20;
			break;
		case 0x04000000:
			key = 0xf30;
			break;
		case 0x02000000:
			key = 0xf38;
			break;
		case 0x01000000:
			key = 0xf3c;
			break;
		case 0x00800000:
			key = 0xf3e;
			break;
		case 0x00400000:
			key = 0xf3f;
			break;
		default:
			device_printf(dev, "Invalid aperture size (%dMb)\n",
			    aperture / 1024 / 1024);
			return EINVAL;
		}
		val = pci_read_config(dev, sc->regs[REG_APSIZE], 2);
		pci_write_config(dev, sc->regs[REG_APSIZE], 
		    ((val & ~0xfff) | key), 2);
	}
	return 0;
}

static int
agp_via_bind_page(device_t dev, vm_offset_t offset, vm_offset_t physical)
{
	struct agp_via_softc *sc = device_get_softc(dev);

	if (offset >= (sc->gatt->ag_entries << AGP_PAGE_SHIFT))
		return EINVAL;

	sc->gatt->ag_virtual[offset >> AGP_PAGE_SHIFT] = physical;
	return 0;
}

static int
agp_via_unbind_page(device_t dev, vm_offset_t offset)
{
	struct agp_via_softc *sc = device_get_softc(dev);

	if (offset >= (sc->gatt->ag_entries << AGP_PAGE_SHIFT))
		return EINVAL;

	sc->gatt->ag_virtual[offset >> AGP_PAGE_SHIFT] = 0;
	return 0;
}

static void
agp_via_flush_tlb(device_t dev)
{
	struct agp_via_softc *sc = device_get_softc(dev);
	u_int32_t gartctrl;

	if (sc->regs == via_v2_regs) {
		pci_write_config(dev, sc->regs[REG_GARTCTRL], 0x8f, 4);
		pci_write_config(dev, sc->regs[REG_GARTCTRL], 0x0f, 4);
	} else {
		gartctrl = pci_read_config(dev, sc->regs[REG_GARTCTRL], 4);
		pci_write_config(dev, sc->regs[REG_GARTCTRL], gartctrl &
		    ~(1 << 7), 4);
		pci_write_config(dev, sc->regs[REG_GARTCTRL], gartctrl, 4);
	}
	
}

static device_method_t agp_via_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		agp_via_probe),
	DEVMETHOD(device_attach,	agp_via_attach),
	DEVMETHOD(device_detach,	agp_via_detach),
	DEVMETHOD(device_shutdown,	bus_generic_shutdown),
	DEVMETHOD(device_suspend,	bus_generic_suspend),
	DEVMETHOD(device_resume,	bus_generic_resume),

	/* AGP interface */
	DEVMETHOD(agp_get_aperture,	agp_via_get_aperture),
	DEVMETHOD(agp_set_aperture,	agp_via_set_aperture),
	DEVMETHOD(agp_bind_page,	agp_via_bind_page),
	DEVMETHOD(agp_unbind_page,	agp_via_unbind_page),
	DEVMETHOD(agp_flush_tlb,	agp_via_flush_tlb),
	DEVMETHOD(agp_enable,		agp_generic_enable),
	DEVMETHOD(agp_alloc_memory,	agp_generic_alloc_memory),
	DEVMETHOD(agp_free_memory,	agp_generic_free_memory),
	DEVMETHOD(agp_bind_memory,	agp_generic_bind_memory),
	DEVMETHOD(agp_unbind_memory,	agp_generic_unbind_memory),

	{ 0, 0 }
};

static driver_t agp_via_driver = {
	"agp",
	agp_via_methods,
	sizeof(struct agp_via_softc),
};

static devclass_t agp_devclass;

DRIVER_MODULE(agp_via, hostb, agp_via_driver, agp_devclass, 0, 0);
MODULE_DEPEND(agp_via, agp, 1, 1, 1);
MODULE_DEPEND(agp_via, pci, 1, 1, 1);
