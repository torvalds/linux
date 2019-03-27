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

#define	MAX_APSIZE	0x3f		/* 256 MB */

struct agp_intel_softc {
	struct agp_softc agp;
	u_int32_t	initial_aperture; /* aperture size at startup */
	struct agp_gatt *gatt;
	u_int		aperture_mask;
	u_int32_t	current_aperture; /* current aperture size */
};

static const char*
agp_intel_match(device_t dev)
{
	if (pci_get_class(dev) != PCIC_BRIDGE
	    || pci_get_subclass(dev) != PCIS_BRIDGE_HOST)
		return (NULL);

	if (agp_find_caps(dev) == 0)
		return (NULL);

	switch (pci_get_devid(dev)) {
	/* Intel -- vendor 0x8086 */
	case 0x71808086:
		return ("Intel 82443LX (440 LX) host to PCI bridge");
	case 0x71908086:
		return ("Intel 82443BX (440 BX) host to PCI bridge");
 	case 0x71a08086:
 		return ("Intel 82443GX host to PCI bridge");
 	case 0x71a18086:
 		return ("Intel 82443GX host to AGP bridge");
	case 0x11308086:
		return ("Intel 82815 (i815 GMCH) host to PCI bridge");
	case 0x25008086:
	case 0x25018086:
		return ("Intel 82820 host to AGP bridge");
	case 0x35758086:
		return ("Intel 82830 host to AGP bridge");
	case 0x1a218086:
		return ("Intel 82840 host to AGP bridge");
	case 0x1a308086:
		return ("Intel 82845 host to AGP bridge");
	case 0x25308086:
		return ("Intel 82850 host to AGP bridge");
	case 0x33408086:
		return ("Intel 82855 host to AGP bridge");
	case 0x25318086:
		return ("Intel 82860 host to AGP bridge");
	case 0x25708086:
		return ("Intel 82865 host to AGP bridge");
	case 0x255d8086:
		return ("Intel E7205 host to AGP bridge");
	case 0x25508086:
		return ("Intel E7505 host to AGP bridge");
	case 0x25788086:
		return ("Intel 82875P host to AGP bridge");
	case 0x25608086:
		return ("Intel 82845G host to AGP bridge");
	case 0x35808086:
		return ("Intel 82855GM host to AGP bridge");
	}

	return (NULL);
}

static int
agp_intel_probe(device_t dev)
{
	const char *desc;

	if (resource_disabled("agp", device_get_unit(dev)))
		return (ENXIO);
	desc = agp_intel_match(dev);
	if (desc) {
		device_set_desc(dev, desc);
		return (BUS_PROBE_DEFAULT);
	}

	return (ENXIO);
}

static void
agp_intel_commit_gatt(device_t dev)
{
	struct agp_intel_softc *sc;
	u_int32_t type;
	u_int32_t value;

	sc = device_get_softc(dev);
	type = pci_get_devid(dev);

	/* Install the gatt. */
	pci_write_config(dev, AGP_INTEL_ATTBASE, sc->gatt->ag_physical, 4);

	/* Enable the GLTB and setup the control register. */
	switch (type) {
	case 0x71908086: /* 440LX/EX */
		pci_write_config(dev, AGP_INTEL_AGPCTRL, 0x2080, 4);
		break;
	case 0x71808086: /* 440BX */
		/*
		 * XXX: Should be 0xa080?  Bit 9 is undefined, and
		 * bit 13 being on and bit 15 being clear is illegal.
		 */
		pci_write_config(dev, AGP_INTEL_AGPCTRL, 0x2280, 4);
		break;
	default:
		value = pci_read_config(dev, AGP_INTEL_AGPCTRL, 4);
		pci_write_config(dev, AGP_INTEL_AGPCTRL, value | 0x80, 4);
	}

	/* Enable aperture accesses. */
	switch (type) {
	case 0x25008086: /* i820 */
	case 0x25018086: /* i820 */
		pci_write_config(dev, AGP_INTEL_I820_RDCR,
				 (pci_read_config(dev, AGP_INTEL_I820_RDCR, 1)
				  | (1 << 1)), 1);
		break;
	case 0x1a308086: /* i845 */
	case 0x25608086: /* i845G */
	case 0x33408086: /* i855 */
	case 0x35808086: /* i855GM */
	case 0x25708086: /* i865 */
	case 0x25788086: /* i875P */
		pci_write_config(dev, AGP_INTEL_I845_AGPM,
				 (pci_read_config(dev, AGP_INTEL_I845_AGPM, 1)
				  | (1 << 1)), 1);
		break;
	case 0x1a218086: /* i840 */
	case 0x25308086: /* i850 */
	case 0x25318086: /* i860 */
	case 0x255d8086: /* E7205 */
	case 0x25508086: /* E7505 */
		pci_write_config(dev, AGP_INTEL_MCHCFG,
				 (pci_read_config(dev, AGP_INTEL_MCHCFG, 2)
				  | (1 << 9)), 2);
		break;
	default: /* Intel Generic (maybe) */
		pci_write_config(dev, AGP_INTEL_NBXCFG,
				 (pci_read_config(dev, AGP_INTEL_NBXCFG, 4)
				  & ~(1 << 10)) | (1 << 9), 4);
	}

	/* Clear errors. */
	switch (type) {
	case 0x1a218086: /* i840 */
		pci_write_config(dev, AGP_INTEL_I8XX_ERRSTS, 0xc000, 2);
		break;
	case 0x25008086: /* i820 */
	case 0x25018086: /* i820 */
	case 0x1a308086: /* i845 */
	case 0x25608086: /* i845G */
	case 0x25308086: /* i850 */
	case 0x33408086: /* i855 */
	case 0x25318086: /* i860 */
	case 0x25708086: /* i865 */
	case 0x25788086: /* i875P */
	case 0x255d8086: /* E7205 */
	case 0x25508086: /* E7505 */
		pci_write_config(dev, AGP_INTEL_I8XX_ERRSTS, 0x00ff, 2);
		break;
	default: /* Intel Generic (maybe) */
		pci_write_config(dev, AGP_INTEL_ERRSTS + 1, 7, 1);
	}
}

static int
agp_intel_attach(device_t dev)
{
	struct agp_intel_softc *sc;
	struct agp_gatt *gatt;
	u_int32_t value;
	int error;

	sc = device_get_softc(dev);

	error = agp_generic_attach(dev);
	if (error)
		return (error);

	/* Determine maximum supported aperture size. */
	value = pci_read_config(dev, AGP_INTEL_APSIZE, 1);
	pci_write_config(dev, AGP_INTEL_APSIZE, MAX_APSIZE, 1);
	sc->aperture_mask = pci_read_config(dev, AGP_INTEL_APSIZE, 1) &
	    MAX_APSIZE;
	pci_write_config(dev, AGP_INTEL_APSIZE, value, 1);
	sc->current_aperture = sc->initial_aperture = AGP_GET_APERTURE(dev);

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
			return (ENOMEM);
		}
	}
	sc->gatt = gatt;

	agp_intel_commit_gatt(dev);

	return (0);
}

static int
agp_intel_detach(device_t dev)
{
	struct agp_intel_softc *sc;
	u_int32_t reg;

	sc = device_get_softc(dev);

	agp_free_cdev(dev);

	/* Disable aperture accesses. */
	switch (pci_get_devid(dev)) {
	case 0x25008086: /* i820 */
	case 0x25018086: /* i820 */
		reg = pci_read_config(dev, AGP_INTEL_I820_RDCR, 1) & ~(1 << 1);
		printf("%s: set RDCR to %02x\n", __func__, reg & 0xff);
		pci_write_config(dev, AGP_INTEL_I820_RDCR, reg, 1);
		break;
	case 0x1a308086: /* i845 */
	case 0x25608086: /* i845G */
	case 0x33408086: /* i855 */
	case 0x35808086: /* i855GM */
	case 0x25708086: /* i865 */
	case 0x25788086: /* i875P */
		reg = pci_read_config(dev, AGP_INTEL_I845_AGPM, 1) & ~(1 << 1);
		printf("%s: set AGPM to %02x\n", __func__, reg & 0xff);
		pci_write_config(dev, AGP_INTEL_I845_AGPM, reg, 1);
		break;
	case 0x1a218086: /* i840 */
	case 0x25308086: /* i850 */
	case 0x25318086: /* i860 */
	case 0x255d8086: /* E7205 */
	case 0x25508086: /* E7505 */
		reg = pci_read_config(dev, AGP_INTEL_MCHCFG, 2) & ~(1 << 9);
		printf("%s: set MCHCFG to %x04\n", __func__, reg & 0xffff);
		pci_write_config(dev, AGP_INTEL_MCHCFG, reg, 2);
		break;
	default: /* Intel Generic (maybe) */
		reg = pci_read_config(dev, AGP_INTEL_NBXCFG, 4) & ~(1 << 9);
		printf("%s: set NBXCFG to %08x\n", __func__, reg);
		pci_write_config(dev, AGP_INTEL_NBXCFG, reg, 4);
	}
	pci_write_config(dev, AGP_INTEL_ATTBASE, 0, 4);
	AGP_SET_APERTURE(dev, sc->initial_aperture);
	agp_free_gatt(sc->gatt);
	agp_free_res(dev);

	return (0);
}

static int
agp_intel_resume(device_t dev)
{
	struct agp_intel_softc *sc;
	sc = device_get_softc(dev);
	
	AGP_SET_APERTURE(dev, sc->current_aperture);
	agp_intel_commit_gatt(dev);
	return (bus_generic_resume(dev));
}

static u_int32_t
agp_intel_get_aperture(device_t dev)
{
	struct agp_intel_softc *sc;
	u_int32_t apsize;

	sc = device_get_softc(dev);

	apsize = pci_read_config(dev, AGP_INTEL_APSIZE, 1) & sc->aperture_mask;

	/*
	 * The size is determined by the number of low bits of
	 * register APBASE which are forced to zero. The low 22 bits
	 * are always forced to zero and each zero bit in the apsize
	 * field just read forces the corresponding bit in the 27:22
	 * to be zero. We calculate the aperture size accordingly.
	 */
	return ((((apsize ^ sc->aperture_mask) << 22) | ((1 << 22) - 1)) + 1);
}

static int
agp_intel_set_aperture(device_t dev, u_int32_t aperture)
{
	struct agp_intel_softc *sc;
	u_int32_t apsize;

	sc = device_get_softc(dev);

	/*
	 * Reverse the magic from get_aperture.
	 */
	apsize = ((aperture - 1) >> 22) ^ sc->aperture_mask;

	/*
	 * Double check for sanity.
	 */
	if ((((apsize ^ sc->aperture_mask) << 22) | ((1 << 22) - 1)) + 1 != aperture)
		return (EINVAL);

	sc->current_aperture = apsize;

	pci_write_config(dev, AGP_INTEL_APSIZE, apsize, 1);

	return (0);
}

static int
agp_intel_bind_page(device_t dev, vm_offset_t offset, vm_offset_t physical)
{
	struct agp_intel_softc *sc;

	sc = device_get_softc(dev);

	if (offset >= (sc->gatt->ag_entries << AGP_PAGE_SHIFT))
		return (EINVAL);

	sc->gatt->ag_virtual[offset >> AGP_PAGE_SHIFT] = physical | 0x17;
	return (0);
}

static int
agp_intel_unbind_page(device_t dev, vm_offset_t offset)
{
	struct agp_intel_softc *sc;

	sc = device_get_softc(dev);

	if (offset >= (sc->gatt->ag_entries << AGP_PAGE_SHIFT))
		return (EINVAL);

	sc->gatt->ag_virtual[offset >> AGP_PAGE_SHIFT] = 0;
	return (0);
}

static void
agp_intel_flush_tlb(device_t dev)
{
	u_int32_t val;

	val = pci_read_config(dev, AGP_INTEL_AGPCTRL, 4);
	pci_write_config(dev, AGP_INTEL_AGPCTRL, val & ~(1 << 7), 4);
	pci_write_config(dev, AGP_INTEL_AGPCTRL, val, 4);
}

static device_method_t agp_intel_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		agp_intel_probe),
	DEVMETHOD(device_attach,	agp_intel_attach),
	DEVMETHOD(device_detach,	agp_intel_detach),
	DEVMETHOD(device_shutdown,	bus_generic_shutdown),
	DEVMETHOD(device_suspend,	bus_generic_suspend),
	DEVMETHOD(device_resume,	agp_intel_resume),

	/* AGP interface */
	DEVMETHOD(agp_get_aperture,	agp_intel_get_aperture),
	DEVMETHOD(agp_set_aperture,	agp_intel_set_aperture),
	DEVMETHOD(agp_bind_page,	agp_intel_bind_page),
	DEVMETHOD(agp_unbind_page,	agp_intel_unbind_page),
	DEVMETHOD(agp_flush_tlb,	agp_intel_flush_tlb),
	DEVMETHOD(agp_enable,		agp_generic_enable),
	DEVMETHOD(agp_alloc_memory,	agp_generic_alloc_memory),
	DEVMETHOD(agp_free_memory,	agp_generic_free_memory),
	DEVMETHOD(agp_bind_memory,	agp_generic_bind_memory),
	DEVMETHOD(agp_unbind_memory,	agp_generic_unbind_memory),

	{ 0, 0 }
};

static driver_t agp_intel_driver = {
	"agp",
	agp_intel_methods,
	sizeof(struct agp_intel_softc),
};

static devclass_t agp_devclass;

DRIVER_MODULE(agp_intel, hostb, agp_intel_driver, agp_devclass, 0, 0);
MODULE_DEPEND(agp_intel, agp, 1, 1, 1);
MODULE_DEPEND(agp_intel, pci, 1, 1, 1);
