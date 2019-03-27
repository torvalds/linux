/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2014 Ruslan Bukin <br@bsdpad.com>
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
 * Vybrid Family Direct Memory Access Multiplexer (DMAMUX)
 * Chapter 22, Vybrid Reference Manual, Rev. 5, 07/2013
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

#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>

#include <machine/bus.h>
#include <machine/cpu.h>
#include <machine/intr.h>

#include <arm/freescale/vybrid/vf_common.h>
#include <arm/freescale/vybrid/vf_dmamux.h>

#define	DMAMUX_CHCFG(n)		(0x1 * n)	/* Channels 0-15 Cfg Reg */
#define	CHCFG_ENBL		(1 << 7)	/* Channel Enable */
#define	CHCFG_TRIG		(1 << 6)	/* Channel Trigger Enable */
#define	CHCFG_SOURCE_MASK	0x3f		/* Channel Source (Slot) */
#define	CHCFG_SOURCE_SHIFT	0

struct dmamux_softc {
	struct resource		*res[4];
	bus_space_tag_t		bst[4];
	bus_space_handle_t	bsh[4];
};

struct dmamux_softc *dmamux_sc;

static struct resource_spec dmamux_spec[] = {
	{ SYS_RES_MEMORY,	0,	RF_ACTIVE }, /* DMAMUX0 */
	{ SYS_RES_MEMORY,	1,	RF_ACTIVE }, /* DMAMUX1 */
	{ SYS_RES_MEMORY,	2,	RF_ACTIVE }, /* DMAMUX2 */
	{ SYS_RES_MEMORY,	3,	RF_ACTIVE }, /* DMAMUX3 */
	{ -1, 0 }
};

static int
dmamux_probe(device_t dev)
{

	if (!ofw_bus_status_okay(dev))
		return (ENXIO);

	if (!ofw_bus_is_compatible(dev, "fsl,mvf600-dmamux"))
		return (ENXIO);

	device_set_desc(dev, "Vybrid Family Direct Memory Access Multiplexer");
	return (BUS_PROBE_DEFAULT);
}

int
dmamux_configure(int mux, int source, int channel, int enable)
{
	struct dmamux_softc *sc;
	int reg;

	sc = dmamux_sc;

	MUX_WRITE1(sc, mux, DMAMUX_CHCFG(channel), 0x0);

	reg = 0;
	if (enable)
		reg |= (CHCFG_ENBL);

	reg &= ~(CHCFG_SOURCE_MASK << CHCFG_SOURCE_SHIFT);
	reg |= (source << CHCFG_SOURCE_SHIFT);

	MUX_WRITE1(sc, mux, DMAMUX_CHCFG(channel), reg);

	return (0);
}

static int
dmamux_attach(device_t dev)
{
	struct dmamux_softc *sc;
	int i;

	sc = device_get_softc(dev);

	if (bus_alloc_resources(dev, dmamux_spec, sc->res)) {
		device_printf(dev, "could not allocate resources\n");
		return (ENXIO);
	}

	/* Memory interface */
	for (i = 0; i < 4; i++) {
		sc->bst[i] = rman_get_bustag(sc->res[i]);
		sc->bsh[i] = rman_get_bushandle(sc->res[i]);
	}

	dmamux_sc = sc;

	return (0);
}

static device_method_t dmamux_methods[] = {
	DEVMETHOD(device_probe,		dmamux_probe),
	DEVMETHOD(device_attach,	dmamux_attach),
	{ 0, 0 }
};

static driver_t dmamux_driver = {
	"dmamux",
	dmamux_methods,
	sizeof(struct dmamux_softc),
};

static devclass_t dmamux_devclass;

DRIVER_MODULE(dmamux, simplebus, dmamux_driver, dmamux_devclass, 0, 0);
