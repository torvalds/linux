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

struct agp_sis_softc {
	struct agp_softc agp;
	u_int32_t	initial_aperture; /* aperture size at startup */
	struct agp_gatt *gatt;
};

static const char*
agp_sis_match(device_t dev)
{
	if (pci_get_class(dev) != PCIC_BRIDGE
	    || pci_get_subclass(dev) != PCIS_BRIDGE_HOST)
		return NULL;

	if (agp_find_caps(dev) == 0)
		return NULL;

	switch (pci_get_devid(dev)) {
	case 0x00011039:
		return ("SiS 5591 host to AGP bridge");
	case 0x05301039:
		return ("SiS 530 host to AGP bridge");
	case 0x05401039:
		return ("SiS 540 host to AGP bridge");
	case 0x05501039:
		return ("SiS 550 host to AGP bridge");
	case 0x06201039:
		return ("SiS 620 host to AGP bridge");
	case 0x06301039:
		return ("SiS 630 host to AGP bridge");
	case 0x06451039:
		return ("SiS 645 host to AGP bridge");
	case 0x06461039:
		return ("SiS 645DX host to AGP bridge");
	case 0x06481039:
		return ("SiS 648 host to AGP bridge");
	case 0x06501039:
		return ("SiS 650 host to AGP bridge");
	case 0x06511039:
		return ("SiS 651 host to AGP bridge");
	case 0x06551039:
		return ("SiS 655 host to AGP bridge");
	case 0x06611039:
		return ("SiS 661 host to AGP bridge");
	case 0x07301039:
		return ("SiS 730 host to AGP bridge");
	case 0x07351039:
		return ("SiS 735 host to AGP bridge");
	case 0x07401039:
		return ("SiS 740 host to AGP bridge");
	case 0x07411039:
		return ("SiS 741 host to AGP bridge");
	case 0x07451039:
		return ("SiS 745 host to AGP bridge");
	case 0x07461039:
		return ("SiS 746 host to AGP bridge");
	}

	return NULL;
}

static int
agp_sis_probe(device_t dev)
{
	const char *desc;

	if (resource_disabled("agp", device_get_unit(dev)))
		return (ENXIO);
	desc = agp_sis_match(dev);
	if (desc) {
		device_set_desc(dev, desc);
		return BUS_PROBE_DEFAULT;
	}

	return ENXIO;
}

static int
agp_sis_attach(device_t dev)
{
	struct agp_sis_softc *sc = device_get_softc(dev);
	struct agp_gatt *gatt;
	int error;

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

	/* Install the gatt. */
	pci_write_config(dev, AGP_SIS_ATTBASE, gatt->ag_physical, 4);
	
	/* Enable the aperture. */
	pci_write_config(dev, AGP_SIS_WINCTRL,
			 pci_read_config(dev, AGP_SIS_WINCTRL, 1) | 3, 1);

	/*
	 * Enable the TLB and make it automatically invalidate entries
	 * when the GATT is written.
	 */
	pci_write_config(dev, AGP_SIS_TLBCTRL, 0x05, 1);

	return 0;
}

static int
agp_sis_detach(device_t dev)
{
	struct agp_sis_softc *sc = device_get_softc(dev);

	agp_free_cdev(dev);

	/* Disable the aperture.. */
	pci_write_config(dev, AGP_SIS_WINCTRL,
			 pci_read_config(dev, AGP_SIS_WINCTRL, 1) & ~3, 1);

	/* and the TLB. */
	pci_write_config(dev, AGP_SIS_TLBCTRL, 0, 1);

	/* Put the aperture back the way it started. */
	AGP_SET_APERTURE(dev, sc->initial_aperture);

	agp_free_gatt(sc->gatt);
	agp_free_res(dev);
	return 0;
}

static u_int32_t
agp_sis_get_aperture(device_t dev)
{
	int gws;

	/*
	 * The aperture size is equal to 4M<<gws.
	 */
	gws = (pci_read_config(dev, AGP_SIS_WINCTRL, 1) & 0x70) >> 4;
	return (4*1024*1024) << gws;
}

static int
agp_sis_set_aperture(device_t dev, u_int32_t aperture)
{
	int gws;

	/*
	 * Check for a power of two and make sure its within the
	 * programmable range.
	 */
	if (aperture & (aperture - 1)
	    || aperture < 4*1024*1024
	    || aperture > 256*1024*1024)
		return EINVAL;

	gws = ffs(aperture / 4*1024*1024) - 1;
	
	pci_write_config(dev, AGP_SIS_WINCTRL,
			 ((pci_read_config(dev, AGP_SIS_WINCTRL, 1) & ~0x70)
			  | gws << 4), 1);

	return 0;
}

static int
agp_sis_bind_page(device_t dev, vm_offset_t offset, vm_offset_t physical)
{
	struct agp_sis_softc *sc = device_get_softc(dev);

	if (offset >= (sc->gatt->ag_entries << AGP_PAGE_SHIFT))
		return EINVAL;

	sc->gatt->ag_virtual[offset >> AGP_PAGE_SHIFT] = physical;
	return 0;
}

static int
agp_sis_unbind_page(device_t dev, vm_offset_t offset)
{
	struct agp_sis_softc *sc = device_get_softc(dev);

	if (offset >= (sc->gatt->ag_entries << AGP_PAGE_SHIFT))
		return EINVAL;

	sc->gatt->ag_virtual[offset >> AGP_PAGE_SHIFT] = 0;
	return 0;
}

static void
agp_sis_flush_tlb(device_t dev)
{
	pci_write_config(dev, AGP_SIS_TLBFLUSH, 0x02, 1);
}

static device_method_t agp_sis_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		agp_sis_probe),
	DEVMETHOD(device_attach,	agp_sis_attach),
	DEVMETHOD(device_detach,	agp_sis_detach),
	DEVMETHOD(device_shutdown,	bus_generic_shutdown),
	DEVMETHOD(device_suspend,	bus_generic_suspend),
	DEVMETHOD(device_resume,	bus_generic_resume),

	/* AGP interface */
	DEVMETHOD(agp_get_aperture,	agp_sis_get_aperture),
	DEVMETHOD(agp_set_aperture,	agp_sis_set_aperture),
	DEVMETHOD(agp_bind_page,	agp_sis_bind_page),
	DEVMETHOD(agp_unbind_page,	agp_sis_unbind_page),
	DEVMETHOD(agp_flush_tlb,	agp_sis_flush_tlb),
	DEVMETHOD(agp_enable,		agp_generic_enable),
	DEVMETHOD(agp_alloc_memory,	agp_generic_alloc_memory),
	DEVMETHOD(agp_free_memory,	agp_generic_free_memory),
	DEVMETHOD(agp_bind_memory,	agp_generic_bind_memory),
	DEVMETHOD(agp_unbind_memory,	agp_generic_unbind_memory),

	{ 0, 0 }
};

static driver_t agp_sis_driver = {
	"agp",
	agp_sis_methods,
	sizeof(struct agp_sis_softc),
};

static devclass_t agp_devclass;

DRIVER_MODULE(agp_sis, hostb, agp_sis_driver, agp_devclass, 0, 0);
MODULE_DEPEND(agp_sis, agp, 1, 1, 1);
MODULE_DEPEND(agp_sis, pci, 1, 1, 1);
