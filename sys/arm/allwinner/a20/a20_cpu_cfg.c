/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2013 Ganbold Tsagaankhuu <ganbold@freebsd.org>
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

/* CPU configuration module for Allwinner A20 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/malloc.h>
#include <sys/rman.h>
#include <sys/timeet.h>
#include <sys/timetc.h>
#include <sys/watchdog.h>
#include <machine/bus.h>
#include <machine/cpu.h>
#include <machine/intr.h>

#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>

#include <machine/bus.h>

#include "a20_cpu_cfg.h"

struct a20_cpu_cfg_softc {
	struct resource 	*res;
	bus_space_tag_t 	bst;
	bus_space_handle_t	bsh;
};

static struct a20_cpu_cfg_softc *a20_cpu_cfg_sc = NULL;

#define cpu_cfg_read_4(sc, reg) 	\
	bus_space_read_4((sc)->bst, (sc)->bsh, (reg))
#define cpu_cfg_write_4(sc, reg, val)	\
	bus_space_write_4((sc)->bst, (sc)->bsh, (reg), (val))

static int
a20_cpu_cfg_probe(device_t dev)
{

	if (!ofw_bus_status_okay(dev))
		return (ENXIO);

	if (ofw_bus_is_compatible(dev, "allwinner,sun7i-cpu-cfg")) {
		device_set_desc(dev, "A20 CPU Configuration Module");
		return(BUS_PROBE_DEFAULT);
	}

	return (ENXIO);
}

static int
a20_cpu_cfg_attach(device_t dev)
{
	struct a20_cpu_cfg_softc *sc = device_get_softc(dev);
	int rid = 0;

	if (a20_cpu_cfg_sc)
		return (ENXIO);

	sc->res = bus_alloc_resource_any(dev, SYS_RES_MEMORY, &rid, RF_ACTIVE);
	if (!sc->res) {
		device_printf(dev, "could not allocate resource\n");
		return (ENXIO);
	}

	sc->bst = rman_get_bustag(sc->res);
	sc->bsh = rman_get_bushandle(sc->res);

	a20_cpu_cfg_sc = sc;

	return (0);
}

static device_method_t a20_cpu_cfg_methods[] = {
	DEVMETHOD(device_probe, 	a20_cpu_cfg_probe),
	DEVMETHOD(device_attach,	a20_cpu_cfg_attach),
	{ 0, 0 }
};

static driver_t a20_cpu_cfg_driver = {
	"a20_cpu_cfg",
	a20_cpu_cfg_methods,
	sizeof(struct a20_cpu_cfg_softc),
};

static devclass_t a20_cpu_cfg_devclass;

EARLY_DRIVER_MODULE(a20_cpu_cfg, simplebus, a20_cpu_cfg_driver, a20_cpu_cfg_devclass, 0, 0,
    BUS_PASS_RESOURCE + BUS_PASS_ORDER_MIDDLE);

uint64_t
a20_read_counter64(void)
{
	uint32_t lo, hi;

	/* Latch counter, wait for it to be ready to read. */
	cpu_cfg_write_4(a20_cpu_cfg_sc, OSC24M_CNT64_CTRL_REG, CNT64_RL_EN);
	while (cpu_cfg_read_4(a20_cpu_cfg_sc, OSC24M_CNT64_CTRL_REG) & CNT64_RL_EN)
		continue;

	hi = cpu_cfg_read_4(a20_cpu_cfg_sc, OSC24M_CNT64_HIGH_REG);
	lo = cpu_cfg_read_4(a20_cpu_cfg_sc, OSC24M_CNT64_LOW_REG);

	return (((uint64_t)hi << 32) | lo);
}

