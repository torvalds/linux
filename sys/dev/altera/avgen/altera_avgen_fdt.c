/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2012-2013, 2016 Robert N. M. Watson
 * All rights reserved.
 *
 * This software was developed by SRI International and the University of
 * Cambridge Computer Laboratory under DARPA/AFRL contract (FA8750-10-C-0237)
 * ("CTSRD"), as part of the DARPA CRASH research programme.
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
#include <sys/bus.h>
#include <sys/condvar.h>
#include <sys/conf.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/module.h>
#include <sys/mutex.h>
#include <sys/rman.h>
#include <sys/stat.h>
#include <sys/systm.h>
#include <sys/uio.h>

#include <machine/bus.h>
#include <machine/resource.h>

#include <vm/vm.h>

#include <dev/fdt/fdt_common.h>
#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>

#include <dev/altera/avgen/altera_avgen.h>

static int
altera_avgen_fdt_probe(device_t dev)
{

	if (!ofw_bus_status_okay(dev))
		return (ENXIO);

	if (ofw_bus_is_compatible(dev, "sri-cambridge,avgen")) {
		device_set_desc(dev, "Generic Altera Avalon device attachment");
		return (BUS_PROBE_DEFAULT);
	}
	return (ENXIO);
}

static int
altera_avgen_fdt_attach(device_t dev)
{
	struct altera_avgen_softc *sc;
	char *str_fileio, *str_geomio, *str_mmapio;
	char *str_devname;
	phandle_t node;
	pcell_t cell;
	int devunit, error;

	sc = device_get_softc(dev);
	sc->avg_dev = dev;
	sc->avg_unit = device_get_unit(dev);

	/*
	 * Query driver-specific OpenFirmware properties to determine how to
	 * expose the device via /dev.
	 */
	str_fileio = NULL;
	str_geomio = NULL;
	str_mmapio = NULL;
	str_devname = NULL;
	devunit = -1;
	sc->avg_width = 1;
	node = ofw_bus_get_node(dev);
	if (OF_getprop(node, "sri-cambridge,width", &cell, sizeof(cell)) > 0)
		sc->avg_width = cell;
	(void)OF_getprop_alloc(node, "sri-cambridge,fileio",
	    (void **)&str_fileio);
	(void)OF_getprop_alloc(node, "sri-cambridge,geomio",
	    (void **)&str_geomio);
	(void)OF_getprop_alloc(node, "sri-cambridge,mmapio",
	    (void **)&str_mmapio);
	(void)OF_getprop_alloc(node,  "sri-cambridge,devname",
	    (void **)&str_devname);
	if (OF_getprop(node, "sri-cambridge,devunit", &cell, sizeof(cell)) > 0)
		devunit = cell;

	/* Memory allocation and checking. */
	sc->avg_rid = 0;
	sc->avg_res = bus_alloc_resource_any(dev, SYS_RES_MEMORY,
	    &sc->avg_rid, RF_ACTIVE);
	if (sc->avg_res == NULL) {
		device_printf(dev, "couldn't map memory\n");
		return (ENXIO);
	}
	error = altera_avgen_attach(sc, str_fileio, str_geomio, str_mmapio,
	    str_devname, devunit);
	if (error != 0)
		bus_release_resource(dev, SYS_RES_MEMORY, sc->avg_rid,
		    sc->avg_res);
	if (str_fileio != NULL)
		OF_prop_free(str_fileio);
	if (str_geomio != NULL)
		OF_prop_free(str_geomio);
	if (str_mmapio != NULL)
		OF_prop_free(str_mmapio);
	if (str_devname != NULL)
		OF_prop_free(str_devname);
	return (error);
}

static int
altera_avgen_fdt_detach(device_t dev)
{
	struct altera_avgen_softc *sc;

	sc = device_get_softc(dev);
	altera_avgen_detach(sc);
	bus_release_resource(dev, SYS_RES_MEMORY, sc->avg_rid, sc->avg_res);
	return (0);
}

static device_method_t altera_avgen_fdt_methods[] = {
	DEVMETHOD(device_probe,		altera_avgen_fdt_probe),
	DEVMETHOD(device_attach,	altera_avgen_fdt_attach),
	DEVMETHOD(device_detach,	altera_avgen_fdt_detach),
	{ 0, 0 }
};

static driver_t altera_avgen_fdt_driver = {
	"altera_avgen",
	altera_avgen_fdt_methods,
	sizeof(struct altera_avgen_softc),
};

DRIVER_MODULE(avgen, simplebus, altera_avgen_fdt_driver,
    altera_avgen_devclass, 0, 0);
