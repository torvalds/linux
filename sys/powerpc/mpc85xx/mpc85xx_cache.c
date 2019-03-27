/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2018 Justin Hibbits
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
 * $FreeBSD$
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/bus.h>
#include <sys/kernel.h>
#include <sys/module.h>

#include <machine/bus.h>

#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>

/*
 * From the P1022 manual, sequence for writing to L2CTL is:
 * - mbar
 * - isync
 * - write
 * - read
 * - mbar
 */
#define	L2_CTL		0x0
#define	  L2CTL_L2E	  0x80000000
#define	  L2CTL_L2I	  0x40000000
struct mpc85xx_cache_softc {
	struct resource	*sc_mem;
};

static struct ofw_compat_data compats[] = {
    {"fsl,8540-l2-cache-controller", 1},
    {"fsl,8541-l2-cache-controller", 1},
    {"fsl,8544-l2-cache-controller", 1},
    {"fsl,8548-l2-cache-controller", 1},
    {"fsl,8555-l2-cache-controller", 1},
    {"fsl,8568-l2-cache-controller", 1},
    {"fsl,b4420-l2-cache-controller", 1},
    {"fsl,b4860-l2-cache-controller", 1},
    {"fsl,bsc9131-l2-cache-controller", 1},
    {"fsl,bsc9132-l2-cache-controller", 1},
    {"fsl,c293-l2-cache-controller", 1},
    {"fsl,mpc8536-l2-cache-controller", 1},
    {"fsl,mpc8540-l2-cache-controller", 1},
    {"fsl,mpc8541-l2-cache-controller", 1},
    {"fsl,mpc8544-l2-cache-controller", 1},
    {"fsl,mpc8548-l2-cache-controller", 1},
    {"fsl,mpc8555-l2-cache-controller", 1},
    {"fsl,mpc8560-l2-cache-controller", 1},
    {"fsl,mpc8568-l2-cache-controller", 1},
    {"fsl,mpc8569-l2-cache-controller", 1},
    {"fsl,mpc8572-l2-cache-controller", 1},
    {"fsl,p1010-l2-cache-controller", 1},
    {"fsl,p1011-l2-cache-controller", 1},
    {"fsl,p1012-l2-cache-controller", 1},
    {"fsl,p1013-l2-cache-controller", 1},
    {"fsl,p1014-l2-cache-controller", 1},
    {"fsl,p1015-l2-cache-controller", 1},
    {"fsl,p1016-l2-cache-controller", 1},
    {"fsl,p1020-l2-cache-controller", 1},
    {"fsl,p1021-l2-cache-controller", 1},
    {"fsl,p1022-l2-cache-controller", 1},
    {"fsl,p1023-l2-cache-controller", 1},
    {"fsl,p1024-l2-cache-controller", 1},
    {"fsl,p1025-l2-cache-controller", 1},
    {"fsl,p2010-l2-cache-controller", 1},
    {"fsl,p2020-l2-cache-controller", 1},
    {"fsl,t2080-l2-cache-controller", 1},
    {"fsl,t4240-l2-cache-controller", 1},
    {0, 0}
};

static int
mpc85xx_cache_probe(device_t dev)
{

	if (ofw_bus_search_compatible(dev, compats)->ocd_str == NULL)
		return (ENXIO);

	device_set_desc(dev, "MPC85xx L2 cache");
	return (0);
}

static int
mpc85xx_cache_attach(device_t dev)
{
	struct mpc85xx_cache_softc *sc = device_get_softc(dev);
	int rid;
	int cache_line_size, cache_size;

	/* Map registers. */
	rid = 0;
	sc->sc_mem = bus_alloc_resource_any(dev,
		     SYS_RES_MEMORY, &rid, RF_ACTIVE);
	if (sc->sc_mem == NULL)
		return (ENOMEM);

	/* Enable cache and flash invalidate. */
	__asm __volatile ("mbar; isync" ::: "memory");
	bus_write_4(sc->sc_mem, L2_CTL, L2CTL_L2E | L2CTL_L2I);
	bus_read_4(sc->sc_mem, L2_CTL);
	__asm __volatile ("mbar" ::: "memory");

	cache_line_size = 0;
	cache_size = 0;
	OF_getencprop(ofw_bus_get_node(dev), "cache-size", &cache_size,
	    sizeof(cache_size));
	OF_getencprop(ofw_bus_get_node(dev), "cache-line-size",
	    &cache_line_size, sizeof(cache_line_size));

	if (cache_line_size != 0 && cache_size != 0)
		device_printf(dev,
		    "L2 cache size: %dKB, cache line size: %d bytes\n",
		    cache_size / 1024, cache_line_size);

	return (0);
}

static device_method_t mpc85xx_cache_methods[] = {
	/* device methods */
	DEVMETHOD(device_probe, 	mpc85xx_cache_probe),
	DEVMETHOD(device_attach, 	mpc85xx_cache_attach),

	DEVMETHOD_END
};

static driver_t mpc85xx_cache_driver = {
	"cache",
	mpc85xx_cache_methods,
	sizeof(struct mpc85xx_cache_softc),
};
static devclass_t mpc85xx_cache_devclass;

EARLY_DRIVER_MODULE(mpc85xx_cache, simplebus, mpc85xx_cache_driver,
    mpc85xx_cache_devclass, NULL, NULL,
    BUS_PASS_RESOURCE + BUS_PASS_ORDER_MIDDLE);
