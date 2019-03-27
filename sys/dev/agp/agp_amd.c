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
#include <vm/vm_extern.h>
#include <vm/vm_kern.h>
#include <vm/vm_object.h>
#include <vm/pmap.h>
#include <machine/bus.h>
#include <machine/resource.h>
#include <sys/rman.h>

MALLOC_DECLARE(M_AGP);

#define READ2(off)	bus_space_read_2(sc->bst, sc->bsh, off)
#define READ4(off)	bus_space_read_4(sc->bst, sc->bsh, off)
#define WRITE2(off,v)	bus_space_write_2(sc->bst, sc->bsh, off, v)
#define WRITE4(off,v)	bus_space_write_4(sc->bst, sc->bsh, off, v)

struct agp_amd_gatt {
	u_int32_t	ag_entries;
	u_int32_t      *ag_virtual;	/* virtual address of gatt */
	vm_offset_t     ag_physical;
	u_int32_t      *ag_vdir;	/* virtual address of page dir */
	vm_offset_t	ag_pdir;	/* physical address of page dir */
};

struct agp_amd_softc {
	struct agp_softc	agp;
	struct resource	       *regs;	/* memory mapped control registers */
	bus_space_tag_t		bst;	/* bus_space tag */
	bus_space_handle_t	bsh;	/* bus_space handle */
	u_int32_t		initial_aperture; /* aperture size at startup */
	struct agp_amd_gatt    *gatt;
};

static struct agp_amd_gatt *
agp_amd_alloc_gatt(device_t dev)
{
	u_int32_t apsize = AGP_GET_APERTURE(dev);
	u_int32_t entries = apsize >> AGP_PAGE_SHIFT;
	struct agp_amd_gatt *gatt;
	int i, npages, pdir_offset;

	if (bootverbose)
		device_printf(dev,
			      "allocating GATT for aperture of size %dM\n",
			      apsize / (1024*1024));

	gatt = malloc(sizeof(struct agp_amd_gatt), M_AGP, M_NOWAIT);
	if (!gatt)
		return 0;

	/*
	 * The AMD751 uses a page directory to map a non-contiguous
	 * gatt so we don't need to use kmem_alloc_contig.
	 * Allocate individual GATT pages and map them into the page
	 * directory.
	 */
	gatt->ag_entries = entries;
	gatt->ag_virtual = (void *)kmem_alloc_attr(entries * sizeof(u_int32_t),
	    M_NOWAIT | M_ZERO, 0, ~0, VM_MEMATTR_WRITE_COMBINING);
	if (!gatt->ag_virtual) {
		if (bootverbose)
			device_printf(dev, "allocation failed\n");
		free(gatt, M_AGP);
		return 0;
	}

	/*
	 * Allocate the page directory.
	 */
	gatt->ag_vdir = (void *)kmem_alloc_attr(AGP_PAGE_SIZE, M_NOWAIT |
	    M_ZERO, 0, ~0, VM_MEMATTR_WRITE_COMBINING);
	if (!gatt->ag_vdir) {
		if (bootverbose)
			device_printf(dev,
				      "failed to allocate page directory\n");
		kmem_free((vm_offset_t)gatt->ag_virtual, entries *
		    sizeof(u_int32_t));
		free(gatt, M_AGP);
		return 0;
	}

	gatt->ag_pdir = vtophys((vm_offset_t) gatt->ag_vdir);
	if(bootverbose)
		device_printf(dev, "gatt -> ag_pdir %#lx\n",
		    (u_long)gatt->ag_pdir);
	/*
	 * Allocate the gatt pages
	 */
	gatt->ag_entries = entries;
	if(bootverbose)
		device_printf(dev, "allocating GATT for %d AGP page entries\n", 
			gatt->ag_entries);

	gatt->ag_physical = vtophys((vm_offset_t) gatt->ag_virtual);

	/*
	 * Map the pages of the GATT into the page directory.
	 *
	 * The GATT page addresses are mapped into the directory offset by
	 * an amount dependent on the base address of the aperture. This
	 * is and offset into the page directory, not an offset added to
	 * the addresses of the gatt pages.
	 */

	pdir_offset = pci_read_config(dev, AGP_AMD751_APBASE, 4) >> 22;

	npages = ((entries * sizeof(u_int32_t) + AGP_PAGE_SIZE - 1)
		  >> AGP_PAGE_SHIFT);

	for (i = 0; i < npages; i++) {
		vm_offset_t va;
		vm_offset_t pa;

		va = ((vm_offset_t) gatt->ag_virtual) + i * AGP_PAGE_SIZE;
		pa = vtophys(va);
		gatt->ag_vdir[i + pdir_offset] = pa | 1;
	}

	return gatt;
}

static void
agp_amd_free_gatt(struct agp_amd_gatt *gatt)
{
	kmem_free((vm_offset_t)gatt->ag_vdir, AGP_PAGE_SIZE);
	kmem_free((vm_offset_t)gatt->ag_virtual, gatt->ag_entries *
	    sizeof(u_int32_t));
	free(gatt, M_AGP);
}

static const char*
agp_amd_match(device_t dev)
{
	if (pci_get_class(dev) != PCIC_BRIDGE
	    || pci_get_subclass(dev) != PCIS_BRIDGE_HOST)
		return NULL;

	if (agp_find_caps(dev) == 0)
		return NULL;

	switch (pci_get_devid(dev)) {
	case 0x70061022:
		return ("AMD 751 host to AGP bridge");
	case 0x700e1022:
		return ("AMD 761 host to AGP bridge");
	case 0x700c1022:
		return ("AMD 762 host to AGP bridge");
	}

	return NULL;
}

static int
agp_amd_probe(device_t dev)
{
	const char *desc;

	if (resource_disabled("agp", device_get_unit(dev)))
		return (ENXIO);
	desc = agp_amd_match(dev);
	if (desc) {
		device_set_desc(dev, desc);
		return BUS_PROBE_DEFAULT;
	}

	return ENXIO;
}

static int
agp_amd_attach(device_t dev)
{
	struct agp_amd_softc *sc = device_get_softc(dev);
	struct agp_amd_gatt *gatt;
	int error, rid;

	error = agp_generic_attach(dev);
	if (error)
		return error;

	rid = AGP_AMD751_REGISTERS;
	sc->regs = bus_alloc_resource_any(dev, SYS_RES_MEMORY, &rid,
					  RF_ACTIVE);
	if (!sc->regs) {
		agp_generic_detach(dev);
		return ENOMEM;
	}

	sc->bst = rman_get_bustag(sc->regs);
	sc->bsh = rman_get_bushandle(sc->regs);

	sc->initial_aperture = AGP_GET_APERTURE(dev);

	for (;;) {
		gatt = agp_amd_alloc_gatt(dev);
		if (gatt)
			break;

		/*
		 * Probably contigmalloc failure. Try reducing the
		 * aperture so that the gatt size reduces.
		 */
		if (AGP_SET_APERTURE(dev, AGP_GET_APERTURE(dev) / 2))
			return ENOMEM;
	}
	sc->gatt = gatt;

	/* Install the gatt. */
	WRITE4(AGP_AMD751_ATTBASE, gatt->ag_pdir);
	
	/* Enable synchronisation between host and agp. */
	pci_write_config(dev,
			 AGP_AMD751_MODECTRL,
			 AGP_AMD751_MODECTRL_SYNEN, 1);

	/* Set indexing mode for two-level and enable page dir cache */
	pci_write_config(dev,
			 AGP_AMD751_MODECTRL2,
			 AGP_AMD751_MODECTRL2_GPDCE, 1);

	/* Enable the TLB and flush */
	WRITE2(AGP_AMD751_STATUS,
	       READ2(AGP_AMD751_STATUS) | AGP_AMD751_STATUS_GCE);
	AGP_FLUSH_TLB(dev);

	return 0;
}

static int
agp_amd_detach(device_t dev)
{
	struct agp_amd_softc *sc = device_get_softc(dev);

	agp_free_cdev(dev);

	/* Disable the TLB.. */
	WRITE2(AGP_AMD751_STATUS,
	       READ2(AGP_AMD751_STATUS) & ~AGP_AMD751_STATUS_GCE);
	
	/* Disable host-agp sync */
	pci_write_config(dev, AGP_AMD751_MODECTRL, 0x00, 1);
	
	/* Clear the GATT base */
	WRITE4(AGP_AMD751_ATTBASE, 0);

	/* Put the aperture back the way it started. */
	AGP_SET_APERTURE(dev, sc->initial_aperture);

	agp_amd_free_gatt(sc->gatt);
	agp_free_res(dev);

	bus_release_resource(dev, SYS_RES_MEMORY,
			     AGP_AMD751_REGISTERS, sc->regs);

	return 0;
}

static u_int32_t
agp_amd_get_aperture(device_t dev)
{
	int vas;

	/*
	 * The aperture size is equal to 32M<<vas.
	 */
	vas = (pci_read_config(dev, AGP_AMD751_APCTRL, 1) & 0x06) >> 1;
	return (32*1024*1024) << vas;
}

static int
agp_amd_set_aperture(device_t dev, u_int32_t aperture)
{
	int vas;

	/*
	 * Check for a power of two and make sure its within the
	 * programmable range.
	 */
	if (aperture & (aperture - 1)
	    || aperture < 32*1024*1024
	    || aperture > 2U*1024*1024*1024)
		return EINVAL;

	vas = ffs(aperture / 32*1024*1024) - 1;
	
	/* 
	 * While the size register is bits 1-3 of APCTRL, bit 0 must be
	 * set for the size value to be 'valid'
	 */
	pci_write_config(dev, AGP_AMD751_APCTRL,
			 (((pci_read_config(dev, AGP_AMD751_APCTRL, 1) & ~0x06)
			  | ((vas << 1) | 1))), 1);

	return 0;
}

static int
agp_amd_bind_page(device_t dev, vm_offset_t offset, vm_offset_t physical)
{
	struct agp_amd_softc *sc = device_get_softc(dev);

	if (offset >= (sc->gatt->ag_entries << AGP_PAGE_SHIFT))
		return EINVAL;

	sc->gatt->ag_virtual[offset >> AGP_PAGE_SHIFT] = physical | 1;
	return 0;
}

static int
agp_amd_unbind_page(device_t dev, vm_offset_t offset)
{
	struct agp_amd_softc *sc = device_get_softc(dev);

	if (offset >= (sc->gatt->ag_entries << AGP_PAGE_SHIFT))
		return EINVAL;

	sc->gatt->ag_virtual[offset >> AGP_PAGE_SHIFT] = 0;
	return 0;
}

static void
agp_amd_flush_tlb(device_t dev)
{
	struct agp_amd_softc *sc = device_get_softc(dev);

	/* Set the cache invalidate bit and wait for the chipset to clear */
	WRITE4(AGP_AMD751_TLBCTRL, 1);
	do {
		DELAY(1);
	} while (READ4(AGP_AMD751_TLBCTRL));
}

static device_method_t agp_amd_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		agp_amd_probe),
	DEVMETHOD(device_attach,	agp_amd_attach),
	DEVMETHOD(device_detach,	agp_amd_detach),
	DEVMETHOD(device_shutdown,	bus_generic_shutdown),
	DEVMETHOD(device_suspend,	bus_generic_suspend),
	DEVMETHOD(device_resume,	bus_generic_resume),

	/* AGP interface */
	DEVMETHOD(agp_get_aperture,	agp_amd_get_aperture),
	DEVMETHOD(agp_set_aperture,	agp_amd_set_aperture),
	DEVMETHOD(agp_bind_page,	agp_amd_bind_page),
	DEVMETHOD(agp_unbind_page,	agp_amd_unbind_page),
	DEVMETHOD(agp_flush_tlb,	agp_amd_flush_tlb),
	DEVMETHOD(agp_enable,		agp_generic_enable),
	DEVMETHOD(agp_alloc_memory,	agp_generic_alloc_memory),
	DEVMETHOD(agp_free_memory,	agp_generic_free_memory),
	DEVMETHOD(agp_bind_memory,	agp_generic_bind_memory),
	DEVMETHOD(agp_unbind_memory,	agp_generic_unbind_memory),

	{ 0, 0 }
};

static driver_t agp_amd_driver = {
	"agp",
	agp_amd_methods,
	sizeof(struct agp_amd_softc),
};

static devclass_t agp_devclass;

DRIVER_MODULE(agp_amd, hostb, agp_amd_driver, agp_devclass, 0, 0);
MODULE_DEPEND(agp_amd, agp, 1, 1, 1);
MODULE_DEPEND(agp_amd, pci, 1, 1, 1);
