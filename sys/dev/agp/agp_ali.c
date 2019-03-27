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

struct agp_ali_softc {
	struct agp_softc agp;
	u_int32_t	initial_aperture; /* aperture size at startup */
	struct agp_gatt *gatt;
};

static const char*
agp_ali_match(device_t dev)
{
	if (pci_get_class(dev) != PCIC_BRIDGE
	    || pci_get_subclass(dev) != PCIS_BRIDGE_HOST)
		return NULL;

	if (agp_find_caps(dev) == 0)
		return NULL;

	switch (pci_get_devid(dev)) {
	case 0x167110b9:
		return ("Ali M1671 host to AGP bridge");
	case 0x154110b9:
		return ("Ali M1541 host to AGP bridge");
	case 0x162110b9:
		return ("Ali M1621 host to AGP bridge");
	}

	return NULL;
}

static int
agp_ali_probe(device_t dev)
{
	const char *desc;

	if (resource_disabled("agp", device_get_unit(dev)))
		return (ENXIO);
	desc = agp_ali_match(dev);
	if (desc) {
		device_set_desc(dev, desc);
		return BUS_PROBE_DEFAULT;
	}

	return ENXIO;
}

static int
agp_ali_attach(device_t dev)
{
	struct agp_ali_softc *sc = device_get_softc(dev);
	struct agp_gatt *gatt;
	int error;
	u_int32_t attbase;

	error = agp_generic_attach(dev);
	if (error)
		return error;

	sc->initial_aperture = AGP_GET_APERTURE(dev);
	if (sc->initial_aperture == 0) {
		device_printf(dev, "bad initial aperture size, disabling\n");
		return ENXIO;
	}

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

	/* Install the gatt. */
	attbase = pci_read_config(dev, AGP_ALI_ATTBASE, 4);
	pci_write_config(dev, AGP_ALI_ATTBASE, gatt->ag_physical |
	    (attbase & 0xfff), 4);
	
	/* Enable the TLB. */
	pci_write_config(dev, AGP_ALI_TLBCTRL, 0x10, 1);

	return 0;
}

static int
agp_ali_detach(device_t dev)
{
	struct agp_ali_softc *sc = device_get_softc(dev);
	u_int32_t attbase;

	agp_free_cdev(dev);

	/* Disable the TLB.. */
	pci_write_config(dev, AGP_ALI_TLBCTRL, 0x90, 1);

	/* Put the aperture back the way it started. */
	AGP_SET_APERTURE(dev, sc->initial_aperture);
	attbase = pci_read_config(dev, AGP_ALI_ATTBASE, 4);
	pci_write_config(dev, AGP_ALI_ATTBASE, attbase & 0xfff, 4);

	agp_free_gatt(sc->gatt);
	agp_free_res(dev);
	return 0;
}

#define M 1024*1024

static u_int32_t agp_ali_table[] = {
	0,			/* 0 - invalid */
	1,			/* 1 - invalid */
	2,			/* 2 - invalid */
	4*M,			/* 3 - invalid */
	8*M,			/* 4 - invalid */
	0,			/* 5 - invalid */
	16*M,			/* 6 - invalid */
	32*M,			/* 7 - invalid */
	64*M,			/* 8 - invalid */
	128*M,			/* 9 - invalid */
	256*M,			/* 10 - invalid */
};
#define	AGP_ALI_TABLE_SIZE nitems(agp_ali_table)

static u_int32_t
agp_ali_get_aperture(device_t dev)
{
	/*
	 * The aperture size is derived from the low bits of attbase.
	 * I'm not sure this is correct..
	 */
	int i = pci_read_config(dev, AGP_ALI_ATTBASE, 4) & 0xf;
	if (i >= AGP_ALI_TABLE_SIZE)
		return 0;
	return agp_ali_table[i];
}

static int
agp_ali_set_aperture(device_t dev, u_int32_t aperture)
{
	int i;
	u_int32_t attbase;

	for (i = 0; i < AGP_ALI_TABLE_SIZE; i++)
		if (agp_ali_table[i] == aperture)
			break;
	if (i == AGP_ALI_TABLE_SIZE)
		return EINVAL;

	attbase = pci_read_config(dev, AGP_ALI_ATTBASE, 4);
	pci_write_config(dev, AGP_ALI_ATTBASE, (attbase & ~0xf) | i, 4);
	return 0;
}

static int
agp_ali_bind_page(device_t dev, vm_offset_t offset, vm_offset_t physical)
{
	struct agp_ali_softc *sc = device_get_softc(dev);

	if (offset >= (sc->gatt->ag_entries << AGP_PAGE_SHIFT))
		return EINVAL;

	sc->gatt->ag_virtual[offset >> AGP_PAGE_SHIFT] = physical;
	return 0;
}

static int
agp_ali_unbind_page(device_t dev, vm_offset_t offset)
{
	struct agp_ali_softc *sc = device_get_softc(dev);

	if (offset >= (sc->gatt->ag_entries << AGP_PAGE_SHIFT))
		return EINVAL;

	sc->gatt->ag_virtual[offset >> AGP_PAGE_SHIFT] = 0;
	return 0;
}

static void
agp_ali_flush_tlb(device_t dev)
{
	pci_write_config(dev, AGP_ALI_TLBCTRL, 0x90, 1);
	pci_write_config(dev, AGP_ALI_TLBCTRL, 0x10, 1);
}

static device_method_t agp_ali_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		agp_ali_probe),
	DEVMETHOD(device_attach,	agp_ali_attach),
	DEVMETHOD(device_detach,	agp_ali_detach),
	DEVMETHOD(device_shutdown,	bus_generic_shutdown),
	DEVMETHOD(device_suspend,	bus_generic_suspend),
	DEVMETHOD(device_resume,	bus_generic_resume),

	/* AGP interface */
	DEVMETHOD(agp_get_aperture,	agp_ali_get_aperture),
	DEVMETHOD(agp_set_aperture,	agp_ali_set_aperture),
	DEVMETHOD(agp_bind_page,	agp_ali_bind_page),
	DEVMETHOD(agp_unbind_page,	agp_ali_unbind_page),
	DEVMETHOD(agp_flush_tlb,	agp_ali_flush_tlb),
	DEVMETHOD(agp_enable,		agp_generic_enable),
	DEVMETHOD(agp_alloc_memory,	agp_generic_alloc_memory),
	DEVMETHOD(agp_free_memory,	agp_generic_free_memory),
	DEVMETHOD(agp_bind_memory,	agp_generic_bind_memory),
	DEVMETHOD(agp_unbind_memory,	agp_generic_unbind_memory),

	{ 0, 0 }
};

static driver_t agp_ali_driver = {
	"agp",
	agp_ali_methods,
	sizeof(struct agp_ali_softc),
};

static devclass_t agp_devclass;

DRIVER_MODULE(agp_ali, hostb, agp_ali_driver, agp_devclass, 0, 0);
MODULE_DEPEND(agp_ali, agp, 1, 1, 1);
MODULE_DEPEND(agp_ali, pci, 1, 1, 1);
