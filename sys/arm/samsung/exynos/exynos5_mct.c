/*-
 * Copyright (c) 2013 Ruslan Bukin <br@bsdpad.com>
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

/*
 * This module just enables Exynos MCT, so ARMv7 Generic Timer will works
 */

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

#define	MCT_CTRL_START		(1 << 8)
#define	MCT_CTRL		(0x240)
#define	MCT_WRITE_STAT		(0x24C)

struct arm_tmr_softc {
	struct resource		*tmr_res[1];
	bus_space_tag_t		bst;
	bus_space_handle_t	bsh;
};

static struct resource_spec arm_tmr_spec[] = {
	{ SYS_RES_MEMORY,       0,      RF_ACTIVE },    /* Timer registers */
	{ -1, 0 }
};

static int
arm_tmr_probe(device_t dev)
{

	if (!ofw_bus_status_okay(dev))
		return (ENXIO);

	if (!ofw_bus_is_compatible(dev, "exynos,mct"))
		return (ENXIO);

	device_set_desc(dev, "Exynos MPCore Timer");
	return (BUS_PROBE_DEFAULT);
}

static int
arm_tmr_attach(device_t dev)
{
	struct arm_tmr_softc *sc;
	int reg, i;
	int mask;

	sc = device_get_softc(dev);

	if (bus_alloc_resources(dev, arm_tmr_spec, sc->tmr_res)) {
		device_printf(dev, "could not allocate resources\n");
		return (ENXIO);
	}

	/* Timer interface */
	sc->bst = rman_get_bustag(sc->tmr_res[0]);
	sc->bsh = rman_get_bushandle(sc->tmr_res[0]);

	reg = bus_space_read_4(sc->bst, sc->bsh, MCT_CTRL);
	reg |= MCT_CTRL_START;
	bus_space_write_4(sc->bst, sc->bsh, MCT_CTRL, reg);

	mask = (1 << 16);

	/* Wait 10 times until written value is applied */
	for (i = 0; i < 10; i++) {
		reg = bus_space_read_4(sc->bst, sc->bsh, MCT_WRITE_STAT);
		if (reg & mask) {
			bus_space_write_4(sc->bst, sc->bsh,
			    MCT_WRITE_STAT, mask);
			return (0);
		}
		cpufunc_nullop();
	}

	/* NOTREACHED */

	panic("Can't enable timer\n");
}

static device_method_t arm_tmr_methods[] = {
	DEVMETHOD(device_probe,		arm_tmr_probe),
	DEVMETHOD(device_attach,	arm_tmr_attach),
	{ 0, 0 }
};

static driver_t arm_tmr_driver = {
	"mct",
	arm_tmr_methods,
	sizeof(struct arm_tmr_softc),
};

static devclass_t arm_tmr_devclass;

DRIVER_MODULE(mct, simplebus, arm_tmr_driver, arm_tmr_devclass, 0, 0);
