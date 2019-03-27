/*-
 * Copyright (C) 2018 Breno Leitao
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
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
#include <sys/kernel.h>
#include <sys/systm.h>
#include <sys/module.h>
#include <sys/types.h>
#include <sys/proc.h>

#include <vm/vm.h>
#include <vm/pmap.h>

#include <machine/bus.h>

#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>

#include "opt_md.h"

#ifdef MD_ROOT_MEM
extern u_char *mfs_root;
extern uint32_t mfs_root_size;
#else
#warning "MD_ROOT_MEM should be set to use ofw initrd as a md device"
#endif

/* bus entry points */
static int ofw_initrd_probe(device_t dev);
static int ofw_initrd_attach(device_t dev);
static void ofw_initrd_identify(driver_t *driver, device_t parent);

struct ofw_initrd_softc {
	device_t	sc_dev;
	vm_paddr_t	start;
	vm_paddr_t	end;
};

static int
ofw_initrd_probe(device_t dev)
{
	phandle_t chosen;

	/* limit this device to one unit */
	if (device_get_unit(dev) != 0)
		return (ENXIO);

	chosen = OF_finddevice("/chosen");
	if (chosen <= 0) {
		return (ENXIO);
	}

	if (!OF_hasprop(chosen, "linux,initrd-start") ||
	    !OF_hasprop(chosen, "linux,initrd-end"))
		return (ENXIO);

	device_set_desc(dev, "OFW initrd memregion loader");
	return (BUS_PROBE_DEFAULT);
}

static int
ofw_initrd_attach(device_t dev)
{
	struct ofw_initrd_softc *sc;
	vm_paddr_t start, end;
	phandle_t chosen;
	pcell_t cell[2];
	ssize_t size;

	sc = device_get_softc(dev);

	chosen = OF_finddevice("/chosen");
	if (chosen <= 0) {
		device_printf(dev, "/chosen not found\n");
		return (ENXIO);
	}

	size = OF_getencprop(chosen, "linux,initrd-start", cell, sizeof(cell));
	if (size == 4)
		start = cell[0];
	else if (size == 8)
		start = (uint64_t)cell[0] << 32 | cell[1];
	else {
		device_printf(dev, "Wrong linux,initrd-start size\n");
		return (ENXIO);
	}

	size = OF_getencprop(chosen, "linux,initrd-end", cell, sizeof(cell));
	if (size == 4)
		end = cell[0];
	else if (size == 8)
		end = (uint64_t)cell[0] << 32 | cell[1];
	else{
		device_printf(dev, "Wrong linux,initrd-end size\n");
		return (ENXIO);
	}

	if (end - start > 0) {
		mfs_root = (u_char *) PHYS_TO_DMAP(start);
		mfs_root_size = end - start;

		return (0);
	}

	return (ENXIO);
}

static void
ofw_initrd_identify(driver_t *driver, device_t parent)
{
	if (device_find_child(parent, "initrd", -1) != NULL)
		return;

	if (BUS_ADD_CHILD(parent, 10, "initrd", -1) == NULL)
		device_printf(parent, "add ofw_initrd child failed\n");
}

static device_method_t ofw_initrd_methods[] = {
	DEVMETHOD(device_identify,	ofw_initrd_identify),
	DEVMETHOD(device_probe,		ofw_initrd_probe),
	DEVMETHOD(device_attach,	ofw_initrd_attach),
	DEVMETHOD_END
};

static driver_t ofw_initrd_driver = {
        "ofw_initrd",
        ofw_initrd_methods,
        sizeof(struct ofw_initrd_softc)
};

static devclass_t ofw_initrd_devclass;

DRIVER_MODULE(ofw_initrd, ofwbus, ofw_initrd_driver, ofw_initrd_devclass,
    NULL, NULL);
