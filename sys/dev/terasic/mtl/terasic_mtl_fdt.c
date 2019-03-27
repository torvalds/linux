/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2012-2013 Robert N. M. Watson
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
#include <sys/consio.h>				/* struct vt_mode */
#include <sys/fbio.h>				/* video_adapter_t */
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/module.h>
#include <sys/mutex.h>
#include <sys/rman.h>
#include <sys/systm.h>

#include <machine/bus.h>
#include <machine/resource.h>

#include <dev/fdt/fdt_common.h>
#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>

#include <dev/terasic/mtl/terasic_mtl.h>

#include "fb_if.h"

static int
terasic_mtl_fdt_probe(device_t dev)
{

	if (!ofw_bus_status_okay(dev))
		return (ENXIO);

	if (ofw_bus_is_compatible(dev, "sri-cambridge,mtl")) {
		device_set_desc(dev, "Terasic Multi-touch LCD (MTL)");
		return (BUS_PROBE_DEFAULT);
	}
        return (ENXIO);
}

static int
terasic_mtl_fdt_attach(device_t dev)
{
	struct terasic_mtl_softc *sc;
	int error;

	sc = device_get_softc(dev);
	sc->mtl_dev = dev;
	sc->mtl_unit = device_get_unit(dev);

	/*
	 * FDT allows multiple memory resources to be defined for a device;
	 * query them in the order registers, pixel buffer, text buffer.
	 * However, we need to sanity-check that they are page-aligned and
	 * page-sized, so we may still abort.
	 */
	sc->mtl_reg_rid = 0;
	sc->mtl_reg_res = bus_alloc_resource_any(dev, SYS_RES_MEMORY,
	    &sc->mtl_reg_rid, RF_ACTIVE);
	if (sc->mtl_reg_res == NULL) {
		device_printf(dev, "couldn't map register memory\n");
		error = ENXIO;
		goto error;
	}
	if (rman_get_start(sc->mtl_reg_res) % PAGE_SIZE != 0) {
		device_printf(dev, "improper register address\n");
		error = ENXIO;
		goto error;
	}
	if (rman_get_size(sc->mtl_reg_res) % PAGE_SIZE != 0) {
		device_printf(dev, "improper register size\n");
		error = ENXIO;
		goto error;
	}
	device_printf(sc->mtl_dev, "registers at mem %p-%p\n",
            (void *)rman_get_start(sc->mtl_reg_res),
	    (void *)(rman_get_start(sc->mtl_reg_res) +
	      rman_get_size(sc->mtl_reg_res)));

	sc->mtl_pixel_rid = 1;
	sc->mtl_pixel_res = bus_alloc_resource_any(dev, SYS_RES_MEMORY,
	    &sc->mtl_pixel_rid, RF_ACTIVE);
	if (sc->mtl_pixel_res == NULL) {
		device_printf(dev, "couldn't map pixel memory\n");
		error = ENXIO;
		goto error;
	}
	if (rman_get_start(sc->mtl_pixel_res) % PAGE_SIZE != 0) {
		device_printf(dev, "improper pixel address\n");
		error = ENXIO;
		goto error;
	}
	if (rman_get_size(sc->mtl_pixel_res) % PAGE_SIZE != 0) {
		device_printf(dev, "improper pixel size\n");
		error = ENXIO;
		goto error;
	}
	device_printf(sc->mtl_dev, "pixel frame buffer at mem %p-%p\n",
            (void *)rman_get_start(sc->mtl_pixel_res),
	    (void *)(rman_get_start(sc->mtl_pixel_res) +
	      rman_get_size(sc->mtl_pixel_res)));

	sc->mtl_text_rid = 2;
	sc->mtl_text_res = bus_alloc_resource_any(dev, SYS_RES_MEMORY,
	    &sc->mtl_text_rid, RF_ACTIVE);
	if (sc->mtl_text_res == NULL) {
		device_printf(dev, "couldn't map text memory\n");
		error = ENXIO;
		goto error;
	}
	if (rman_get_start(sc->mtl_text_res) % PAGE_SIZE != 0) {
		device_printf(dev, "improper text address\n");
		error = ENXIO;
		goto error;
	}
	if (rman_get_size(sc->mtl_text_res) % PAGE_SIZE != 0) {
		device_printf(dev, "improper text size\n");
		error = ENXIO;
		goto error;
	}
	device_printf(sc->mtl_dev, "text frame buffer at mem %p-%p\n",
            (void *)rman_get_start(sc->mtl_text_res),
	    (void *)(rman_get_start(sc->mtl_text_res) +
	      rman_get_size(sc->mtl_text_res)));

	error = terasic_mtl_attach(sc);
	if (error == 0)
		return (0);
error:
	if (sc->mtl_text_res != NULL)
		bus_release_resource(dev, SYS_RES_MEMORY, sc->mtl_text_rid,
		    sc->mtl_text_res);
	if (sc->mtl_pixel_res != NULL)
		bus_release_resource(dev, SYS_RES_MEMORY, sc->mtl_pixel_rid,
		    sc->mtl_pixel_res);
	if (sc->mtl_reg_res != NULL)
		bus_release_resource(dev, SYS_RES_MEMORY, sc->mtl_reg_rid,
		    sc->mtl_reg_res);
	return (error);
}

static int
terasic_mtl_fdt_detach(device_t dev)
{
	struct terasic_mtl_softc *sc;

	sc = device_get_softc(dev);
	terasic_mtl_detach(sc);
	bus_release_resource(dev, SYS_RES_MEMORY, sc->mtl_text_rid,
	    sc->mtl_text_res);
	bus_release_resource(dev, SYS_RES_MEMORY, sc->mtl_pixel_rid,
	    sc->mtl_pixel_res);
	bus_release_resource(dev, SYS_RES_MEMORY, sc->mtl_reg_rid,
	    sc->mtl_reg_res);
	return (0);
}

static struct fb_info *
terasic_mtl_fb_getinfo(device_t dev)
{
        struct terasic_mtl_softc *sc;

        sc = device_get_softc(dev);
        return (&sc->mtl_fb_info);
}

static device_method_t terasic_mtl_fdt_methods[] = {
	DEVMETHOD(device_probe,		terasic_mtl_fdt_probe),
	DEVMETHOD(device_attach,	terasic_mtl_fdt_attach),
	DEVMETHOD(device_detach,	terasic_mtl_fdt_detach),
	DEVMETHOD(fb_getinfo,		terasic_mtl_fb_getinfo),
	{ 0, 0 }
};

static driver_t terasic_mtl_fdt_driver = {
	"terasic_mtl",
	terasic_mtl_fdt_methods,
	sizeof(struct terasic_mtl_softc),
};

DRIVER_MODULE(mtl, simplebus, terasic_mtl_fdt_driver, terasic_mtl_devclass, 0,
    0);
