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

#include <dev/altera/avgen/altera_avgen.h>

static int
altera_avgen_nexus_probe(device_t dev)
{

	device_set_desc(dev, "Generic Altera Avalon device attachment");
	return (BUS_PROBE_NOWILDCARD);
}

static int
altera_avgen_nexus_attach(device_t dev)
{
	struct altera_avgen_softc *sc;
	const char *str_fileio, *str_geomio, *str_mmapio;
	const char *str_devname;
	int devunit, error;

	sc = device_get_softc(dev);
	sc->avg_dev = dev;
	sc->avg_unit = device_get_unit(dev);

	/*
	 * Query non-standard hints to find out what operations are permitted
	 * on the device, and whether it is cached.
	 */
	str_fileio = NULL;
	str_geomio = NULL;
	str_mmapio = NULL;
	str_devname = NULL;
	devunit = -1;
	sc->avg_width = 1;
	error = resource_int_value(device_get_name(dev), device_get_unit(dev),
	    ALTERA_AVALON_STR_WIDTH, &sc->avg_width);
	if (error != 0 && error != ENOENT) {
		device_printf(dev, "invalid %s\n", ALTERA_AVALON_STR_WIDTH);
		return (error);
	}
	(void)resource_string_value(device_get_name(dev),
	    device_get_unit(dev), ALTERA_AVALON_STR_FILEIO, &str_fileio);
	(void)resource_string_value(device_get_name(dev),
	    device_get_unit(dev), ALTERA_AVALON_STR_GEOMIO, &str_geomio);
	(void)resource_string_value(device_get_name(dev),
	    device_get_unit(dev), ALTERA_AVALON_STR_MMAPIO, &str_mmapio);
	(void)resource_string_value(device_get_name(dev),
	    device_get_unit(dev), ALTERA_AVALON_STR_DEVNAME, &str_devname);
	(void)resource_int_value(device_get_name(dev), device_get_unit(dev),
	    ALTERA_AVALON_STR_DEVUNIT, &devunit);

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
	return (error);
}

static int
altera_avgen_nexus_detach(device_t dev)
{
	struct altera_avgen_softc *sc;

	sc = device_get_softc(dev);
	altera_avgen_detach(sc);
	bus_release_resource(dev, SYS_RES_MEMORY, sc->avg_rid, sc->avg_res);
	return (0);
}

static device_method_t altera_avgen_nexus_methods[] = {
	DEVMETHOD(device_probe,		altera_avgen_nexus_probe),
	DEVMETHOD(device_attach,	altera_avgen_nexus_attach),
	DEVMETHOD(device_detach,	altera_avgen_nexus_detach),
	{ 0, 0 }
};

static driver_t altera_avgen_nexus_driver = {
	"altera_avgen",
	altera_avgen_nexus_methods,
	sizeof(struct altera_avgen_softc),
};

DRIVER_MODULE(avgen, nexus, altera_avgen_nexus_driver, altera_avgen_devclass,
    0, 0);
