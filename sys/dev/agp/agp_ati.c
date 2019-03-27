/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2005 Eric Anholt
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
 *
 * Based on reading the Linux 2.6.8.1 driver by Dave Jones.
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
#include <vm/vm_extern.h>
#include <vm/vm_kern.h>
#include <vm/vm_object.h>
#include <vm/pmap.h>
#include <machine/bus.h>
#include <machine/resource.h>
#include <sys/rman.h>

MALLOC_DECLARE(M_AGP);

#define READ4(off)	bus_space_read_4(sc->bst, sc->bsh, off)
#define WRITE4(off,v)	bus_space_write_4(sc->bst, sc->bsh, off, v)

struct agp_ati_softc {
	struct agp_softc agp;
	struct resource *regs;	/* memory mapped control registers */
	bus_space_tag_t bst;	/* bus_space tag */
	bus_space_handle_t bsh;	/* bus_space handle */
	u_int32_t	initial_aperture; /* aperture size at startup */
	char		is_rs300;

	/* The GATT */
	u_int32_t	ag_entries;
	u_int32_t      *ag_virtual;	/* virtual address of gatt */
	u_int32_t      *ag_vdir;	/* virtual address of page dir */
	vm_offset_t	ag_pdir;	/* physical address of page dir */
};


static const char*
agp_ati_match(device_t dev)
{
	if (pci_get_class(dev) != PCIC_BRIDGE ||
	    pci_get_subclass(dev) != PCIS_BRIDGE_HOST)
		return NULL;

	if (agp_find_caps(dev) == 0)
		return NULL;

	switch (pci_get_devid(dev)) {
	case 0xcab01002:
		return ("ATI RS100 AGP bridge");
	case 0xcab21002:
		return ("ATI RS200 AGP bridge");
	case 0xcbb21002:
		return ("ATI RS200M AGP bridge");
	case 0xcab31002:
		return ("ATI RS250 AGP bridge");
	case 0x58301002:
		return ("ATI RS300_100 AGP bridge");
	case 0x58311002:
		return ("ATI RS300_133 AGP bridge");
	case 0x58321002:
		return ("ATI RS300_166 AGP bridge");
	case 0x58331002:
		return ("ATI RS300_200 AGP bridge");
	}

	return NULL;
}

static int
agp_ati_probe(device_t dev)
{
	const char *desc;

	desc = agp_ati_match(dev);
	if (desc) {
		device_set_desc(dev, desc);
		return 0;
	}

	return ENXIO;
}

static int
agp_ati_alloc_gatt(device_t dev)
{
	struct agp_ati_softc *sc = device_get_softc(dev);
	u_int32_t apsize = AGP_GET_APERTURE(dev);
	u_int32_t entries = apsize >> AGP_PAGE_SHIFT;
	u_int32_t apbase_offset;
	int i;

	/* Alloc the GATT -- pointers to pages of AGP memory */
	sc->ag_entries = entries;
	sc->ag_virtual = (void *)kmem_alloc_attr(entries * sizeof(u_int32_t),
	    M_NOWAIT | M_ZERO, 0, ~0, VM_MEMATTR_WRITE_COMBINING);
	if (sc->ag_virtual == NULL) {
		if (bootverbose)
			device_printf(dev, "GATT allocation failed\n");
		return ENOMEM;
	}

	/* Alloc the page directory -- pointers to each page of the GATT */
	sc->ag_vdir = (void *)kmem_alloc_attr(AGP_PAGE_SIZE, M_NOWAIT | M_ZERO,
	    0, ~0, VM_MEMATTR_WRITE_COMBINING);
	if (sc->ag_vdir == NULL) {
		if (bootverbose)
			device_printf(dev, "pagedir allocation failed\n");
		kmem_free((vm_offset_t)sc->ag_virtual, entries *
		    sizeof(u_int32_t));
		return ENOMEM;
	}
	sc->ag_pdir = vtophys((vm_offset_t)sc->ag_vdir);

	apbase_offset = pci_read_config(dev, AGP_APBASE, 4) >> 22;
	/* Fill in the pagedir's pointers to GATT pages */
	for (i = 0; i < sc->ag_entries / 1024; i++) {
		vm_offset_t va;
		vm_offset_t pa;

		va = ((vm_offset_t)sc->ag_virtual) + i * AGP_PAGE_SIZE;
		pa = vtophys(va);
		sc->ag_vdir[apbase_offset + i] = pa | 1;
	}

	return 0;
}


static int
agp_ati_attach(device_t dev)
{
	struct agp_ati_softc *sc = device_get_softc(dev);
	int error, rid;
	u_int32_t temp;
	u_int32_t apsize_reg, agpmode_reg;

	error = agp_generic_attach(dev);
	if (error)
		return error;

	switch (pci_get_devid(dev)) {
	case 0xcab01002: /* ATI RS100 AGP bridge */
	case 0xcab21002: /* ATI RS200 AGP bridge */
	case 0xcbb21002: /* ATI RS200M AGP bridge */
	case 0xcab31002: /* ATI RS250 AGP bridge */
		sc->is_rs300 = 0;
		apsize_reg = ATI_RS100_APSIZE;
		agpmode_reg = ATI_RS100_IG_AGPMODE;
		break;
	case 0x58301002: /* ATI RS300_100 AGP bridge */
	case 0x58311002: /* ATI RS300_133 AGP bridge */
	case 0x58321002: /* ATI RS300_166 AGP bridge */
	case 0x58331002: /* ATI RS300_200 AGP bridge */
		sc->is_rs300 = 1;
		apsize_reg = ATI_RS300_APSIZE;
		agpmode_reg = ATI_RS300_IG_AGPMODE;
		break;
	default:
		/* Unknown chipset */
		return EINVAL;
	}

	rid = ATI_GART_MMADDR;
	sc->regs = bus_alloc_resource_any(dev, SYS_RES_MEMORY, &rid, RF_ACTIVE);
	if (!sc->regs) {
		agp_generic_detach(dev);
		return ENOMEM;
	}

	sc->bst = rman_get_bustag(sc->regs);
	sc->bsh = rman_get_bushandle(sc->regs);

	sc->initial_aperture = AGP_GET_APERTURE(dev);

	for (;;) {
		if (agp_ati_alloc_gatt(dev) == 0)
			break;

		/*
		 * Probably contigmalloc failure. Try reducing the
		 * aperture so that the gatt size reduces.
		 */
		if (AGP_SET_APERTURE(dev, AGP_GET_APERTURE(dev) / 2))
			return ENOMEM;
	}

	temp = pci_read_config(dev, apsize_reg, 4);
	pci_write_config(dev, apsize_reg, temp | 1, 4);

	pci_write_config(dev, agpmode_reg, 0x20000, 4);

	WRITE4(ATI_GART_FEATURE_ID, 0x00060000);

	temp = pci_read_config(dev, 4, 4);	/* XXX: Magic reg# */
	pci_write_config(dev, 4, temp | (1 << 14), 4);

	WRITE4(ATI_GART_BASE, sc->ag_pdir);

	AGP_FLUSH_TLB(dev);

	return 0;
}

static int
agp_ati_detach(device_t dev)
{
	struct agp_ati_softc *sc = device_get_softc(dev);
	u_int32_t apsize_reg, temp;

	agp_free_cdev(dev);

	if (sc->is_rs300)
		apsize_reg = ATI_RS300_APSIZE;
	else
		apsize_reg = ATI_RS100_APSIZE;

	/* Clear the GATT base */
	WRITE4(ATI_GART_BASE, 0);

	/* Put the aperture back the way it started. */
	AGP_SET_APERTURE(dev, sc->initial_aperture);

	temp = pci_read_config(dev, apsize_reg, 4);
	pci_write_config(dev, apsize_reg, temp & ~1, 4);

	kmem_free((vm_offset_t)sc->ag_vdir, AGP_PAGE_SIZE);
	kmem_free((vm_offset_t)sc->ag_virtual, sc->ag_entries *
	    sizeof(u_int32_t));

	bus_release_resource(dev, SYS_RES_MEMORY, ATI_GART_MMADDR, sc->regs);
	agp_free_res(dev);

	return 0;
}

static u_int32_t
agp_ati_get_aperture(device_t dev)
{
	struct agp_ati_softc *sc = device_get_softc(dev);
	int size_value;

	if (sc->is_rs300)
		size_value = pci_read_config(dev, ATI_RS300_APSIZE, 4);
	else
		size_value = pci_read_config(dev, ATI_RS100_APSIZE, 4);

	size_value = (size_value & 0x0000000e) >> 1;
	size_value = (32 * 1024 * 1024) << size_value;

	return size_value;
}

static int
agp_ati_set_aperture(device_t dev, u_int32_t aperture)
{
	struct agp_ati_softc *sc = device_get_softc(dev);
	int size_value;
	u_int32_t apsize_reg;

	if (sc->is_rs300)
		apsize_reg = ATI_RS300_APSIZE;
	else
		apsize_reg = ATI_RS100_APSIZE;

	size_value = pci_read_config(dev, apsize_reg, 4);

	size_value &= ~0x0000000e;
	size_value |= (ffs(aperture / (32 * 1024 * 1024)) - 1) << 1;

	pci_write_config(dev, apsize_reg, size_value, 4);

	return 0;
}

static int
agp_ati_bind_page(device_t dev, vm_offset_t offset, vm_offset_t physical)
{
	struct agp_ati_softc *sc = device_get_softc(dev);

	if (offset >= (sc->ag_entries << AGP_PAGE_SHIFT))
		return EINVAL;

	sc->ag_virtual[offset >> AGP_PAGE_SHIFT] = physical | 1;

	return 0;
}

static int
agp_ati_unbind_page(device_t dev, vm_offset_t offset)
{
	struct agp_ati_softc *sc = device_get_softc(dev);

	if (offset >= (sc->ag_entries << AGP_PAGE_SHIFT))
		return EINVAL;

	sc->ag_virtual[offset >> AGP_PAGE_SHIFT] = 0;
	return 0;
}

static void
agp_ati_flush_tlb(device_t dev)
{
	struct agp_ati_softc *sc = device_get_softc(dev);

	/* Set the cache invalidate bit and wait for the chipset to clear */
	WRITE4(ATI_GART_CACHE_CNTRL, 1);
	(void)READ4(ATI_GART_CACHE_CNTRL);
}

static device_method_t agp_ati_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		agp_ati_probe),
	DEVMETHOD(device_attach,	agp_ati_attach),
	DEVMETHOD(device_detach,	agp_ati_detach),
	DEVMETHOD(device_shutdown,	bus_generic_shutdown),
	DEVMETHOD(device_suspend,	bus_generic_suspend),
	DEVMETHOD(device_resume,	bus_generic_resume),

	/* AGP interface */
	DEVMETHOD(agp_get_aperture,	agp_ati_get_aperture),
	DEVMETHOD(agp_set_aperture,	agp_ati_set_aperture),
	DEVMETHOD(agp_bind_page,	agp_ati_bind_page),
	DEVMETHOD(agp_unbind_page,	agp_ati_unbind_page),
	DEVMETHOD(agp_flush_tlb,	agp_ati_flush_tlb),
	DEVMETHOD(agp_enable,		agp_generic_enable),
	DEVMETHOD(agp_alloc_memory,	agp_generic_alloc_memory),
	DEVMETHOD(agp_free_memory,	agp_generic_free_memory),
	DEVMETHOD(agp_bind_memory,	agp_generic_bind_memory),
	DEVMETHOD(agp_unbind_memory,	agp_generic_unbind_memory),

	{ 0, 0 }
};

static driver_t agp_ati_driver = {
	"agp",
	agp_ati_methods,
	sizeof(struct agp_ati_softc),
};

static devclass_t agp_devclass;

DRIVER_MODULE(agp_ati, hostb, agp_ati_driver, agp_devclass, 0, 0);
MODULE_DEPEND(agp_ati, agp, 1, 1, 1);
MODULE_DEPEND(agp_ati, pci, 1, 1, 1);
