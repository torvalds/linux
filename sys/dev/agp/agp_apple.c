/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2010 Nathan Whitehorn
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

#include <machine/resource.h>

#include <dev/agp/agppriv.h>
#include <dev/agp/agpreg.h>
#include <dev/pci/pcivar.h>
#include <dev/pci/pcireg.h>

#include <vm/vm.h>
#include <vm/vm_object.h>
#include <vm/pmap.h>

#define UNIN_AGP_GART_BASE	0x8c
#define UNIN_AGP_BASE_ADDR	0x90
#define UNIN_AGP_GART_CONTROL	0x94

#define UNIN_AGP_GART_INVAL	0x00000001
#define UNIN_AGP_GART_ENABLE	0x00000100
#define UNIN_AGP_GART_2XRESET	0x00010000
#define UNIN_AGP_U3_GART_PERFRD	0x00080000

struct agp_apple_softc {
	struct agp_softc agp;
	uint32_t	aperture;
	struct agp_gatt *gatt;
	int		u3;
	int		needs_2x_reset;
};

static int
agp_apple_probe(device_t dev)
{

	if (resource_disabled("agp", device_get_unit(dev)))
		return (ENXIO);

	if (pci_get_class(dev) != PCIC_BRIDGE
	    || pci_get_subclass(dev) != PCIS_BRIDGE_HOST)
		return (ENXIO);

	if (agp_find_caps(dev) == 0)
		return (ENXIO);

	if (pci_get_class(dev) != PCIC_BRIDGE
	    || pci_get_subclass(dev) != PCIS_BRIDGE_HOST)
		return (ENXIO);

	switch (pci_get_devid(dev)) {
	case 0x0020106b:
	case 0x0027106b:
		device_set_desc(dev, "Apple UniNorth AGP Bridge");
		return (BUS_PROBE_DEFAULT);
	case 0x002d106b:
		device_set_desc(dev, "Apple UniNorth 1.5 AGP Bridge");
		return (BUS_PROBE_DEFAULT);
	case 0x0034106b:
		device_set_desc(dev, "Apple UniNorth 2 AGP Bridge");
		return (BUS_PROBE_DEFAULT);
	case 0x004b106b:
	case 0x0058106b:
	case 0x0059106b:
		device_set_desc(dev, "Apple U3 AGP Bridge");
		return (BUS_PROBE_DEFAULT);
	case 0x0066106b:
		device_set_desc(dev, "Apple Intrepid AGP Bridge");
		return (BUS_PROBE_DEFAULT);
	}

	return (ENXIO);
}

static int
agp_apple_attach(device_t dev)
{
	struct agp_apple_softc *sc = device_get_softc(dev);
	int error;

	/* Record quirks */
	sc->needs_2x_reset = 0;
	sc->u3 = 0;
	switch (pci_get_devid(dev)) {
	case 0x0020106b:
	case 0x0027106b:
		sc->needs_2x_reset = 1;
		break;
	case 0x004b106b:
	case 0x0058106b:
	case 0x0059106b:
		sc->u3 = 1;
		break;
	}

	/* Set the aperture bus address base (must be 0) */
	pci_write_config(dev, UNIN_AGP_BASE_ADDR, 0, 4);
	agp_set_aperture_resource(dev, -1);

	error = agp_generic_attach(dev);
	if (error)
		return (error);

	sc->aperture = 256*1024*1024;

	for (sc->aperture = 256*1024*1024; sc->aperture >= 4*1024*1024;
	    sc->aperture /= 2) {
		sc->gatt = agp_alloc_gatt(dev);
		if (sc->gatt)
			break;
	}
	if (sc->aperture < 4*1024*1024) {
		agp_generic_detach(dev);
		return ENOMEM;
	}

	/* Install the gatt. */
	AGP_SET_APERTURE(dev, sc->aperture);

	/* XXX: U3 scratch page? */
	
	/* Enable the aperture and TLB. */
	AGP_FLUSH_TLB(dev);

	return (0);
}

static int
agp_apple_detach(device_t dev)
{
	struct agp_apple_softc *sc = device_get_softc(dev);

	agp_free_cdev(dev);

	/* Disable the aperture and TLB */
	pci_write_config(dev, UNIN_AGP_GART_CONTROL, UNIN_AGP_GART_INVAL, 4);
	pci_write_config(dev, UNIN_AGP_GART_CONTROL, 0, 4);

	if (sc->needs_2x_reset) {
		pci_write_config(dev, UNIN_AGP_GART_CONTROL,
		    UNIN_AGP_GART_2XRESET, 4);
		pci_write_config(dev, UNIN_AGP_GART_CONTROL, 0, 4);
	}

	AGP_SET_APERTURE(dev, 0);

	agp_free_gatt(sc->gatt);
	agp_free_res(dev);
	return 0;
}

static uint32_t
agp_apple_get_aperture(device_t dev)
{
	struct agp_apple_softc *sc = device_get_softc(dev);

	return (sc->aperture);
}

static int
agp_apple_set_aperture(device_t dev, uint32_t aperture)
{
	struct agp_apple_softc *sc = device_get_softc(dev);

	/*
	 * Check for a multiple of 4 MB and make sure it is within the
	 * programmable range.
	 */
	if (aperture % (4*1024*1024)
	    || aperture < 4*1024*1024
	    || aperture > ((sc->u3) ? 512 : 256)*1024*1024)
		return EINVAL;

	/* The aperture value is a multiple of 4 MB */
	aperture /= (4*1024*1024);

	pci_write_config(dev, UNIN_AGP_GART_BASE,
	    (sc->gatt->ag_physical & 0xfffff000) | aperture, 4);
	
	return (0);
}

static int
agp_apple_bind_page(device_t dev, vm_offset_t offset, vm_offset_t physical)
{
	struct agp_apple_softc *sc = device_get_softc(dev);

	if (offset >= (sc->gatt->ag_entries << AGP_PAGE_SHIFT))
		return EINVAL;

	sc->gatt->ag_virtual[offset >> AGP_PAGE_SHIFT] = physical;
	return (0);
}

static int
agp_apple_unbind_page(device_t dev, vm_offset_t offset)
{
	struct agp_apple_softc *sc = device_get_softc(dev);

	if (offset >= (sc->gatt->ag_entries << AGP_PAGE_SHIFT))
		return EINVAL;

	sc->gatt->ag_virtual[offset >> AGP_PAGE_SHIFT] = 0;
	return (0);
}

static void
agp_apple_flush_tlb(device_t dev)
{
	struct agp_apple_softc *sc = device_get_softc(dev);
	uint32_t cntrl = UNIN_AGP_GART_ENABLE;

	if (sc->u3)
		cntrl |= UNIN_AGP_U3_GART_PERFRD;

	pci_write_config(dev, UNIN_AGP_GART_CONTROL,
	    cntrl | UNIN_AGP_GART_INVAL, 4);
	pci_write_config(dev, UNIN_AGP_GART_CONTROL, cntrl, 4);

	if (sc->needs_2x_reset) {
		pci_write_config(dev, UNIN_AGP_GART_CONTROL,
		    cntrl | UNIN_AGP_GART_2XRESET, 4);
		pci_write_config(dev, UNIN_AGP_GART_CONTROL, cntrl, 4);
	}
}

static device_method_t agp_apple_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		agp_apple_probe),
	DEVMETHOD(device_attach,	agp_apple_attach),
	DEVMETHOD(device_detach,	agp_apple_detach),
	DEVMETHOD(device_shutdown,	bus_generic_shutdown),
	DEVMETHOD(device_suspend,	bus_generic_suspend),
	DEVMETHOD(device_resume,	bus_generic_resume),

	/* AGP interface */
	DEVMETHOD(agp_get_aperture,	agp_apple_get_aperture),
	DEVMETHOD(agp_set_aperture,	agp_apple_set_aperture),
	DEVMETHOD(agp_bind_page,	agp_apple_bind_page),
	DEVMETHOD(agp_unbind_page,	agp_apple_unbind_page),
	DEVMETHOD(agp_flush_tlb,	agp_apple_flush_tlb),
	DEVMETHOD(agp_enable,		agp_generic_enable),
	DEVMETHOD(agp_alloc_memory,	agp_generic_alloc_memory),
	DEVMETHOD(agp_free_memory,	agp_generic_free_memory),
	DEVMETHOD(agp_bind_memory,	agp_generic_bind_memory),
	DEVMETHOD(agp_unbind_memory,	agp_generic_unbind_memory),

	{ 0, 0 }
};

static driver_t agp_apple_driver = {
	"agp",
	agp_apple_methods,
	sizeof(struct agp_apple_softc),
};

static devclass_t agp_devclass;

DRIVER_MODULE(agp_apple, hostb, agp_apple_driver, agp_devclass, 0, 0);
MODULE_DEPEND(agp_apple, agp, 1, 1, 1);
MODULE_DEPEND(agp_apple, pci, 1, 1, 1);
