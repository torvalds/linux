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
 * Vybrid Family Timing Controller (TCON)
 * Chapter 58, Vybrid Reference Manual, Rev. 5, 07/2013
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

#define	TCON0_CTRL1	0x00
#define	TCON_BYPASS	(1 << 29)

struct tcon_softc {
	struct resource		*res[1];
	bus_space_tag_t		bst;
	bus_space_handle_t	bsh;
};

struct tcon_softc *tcon_sc;

static struct resource_spec tcon_spec[] = {
	{ SYS_RES_MEMORY,	0,	RF_ACTIVE },
	{ -1, 0 }
};

uint32_t
tcon_bypass(void)
{
	struct tcon_softc *sc;

	if (tcon_sc == NULL)
		return (1);

	sc = tcon_sc;

	WRITE4(tcon_sc, TCON0_CTRL1, TCON_BYPASS);

	return (0);
}

static int
tcon_probe(device_t dev)
{

	if (!ofw_bus_status_okay(dev))
		return (ENXIO);

	if (!ofw_bus_is_compatible(dev, "fsl,mvf600-tcon"))
		return (ENXIO);

	device_set_desc(dev, "Vybrid Family Timing Controller (TCON)");
	return (BUS_PROBE_DEFAULT);
}

static int
tcon_attach(device_t dev)
{
	struct tcon_softc *sc;

	sc = device_get_softc(dev);

	if (bus_alloc_resources(dev, tcon_spec, sc->res)) {
		device_printf(dev, "could not allocate resources\n");
		return (ENXIO);
	}

	/* Memory interface */
	sc->bst = rman_get_bustag(sc->res[0]);
	sc->bsh = rman_get_bushandle(sc->res[0]);

	tcon_sc = sc;

	return (0);
}

static device_method_t tcon_methods[] = {
	DEVMETHOD(device_probe,		tcon_probe),
	DEVMETHOD(device_attach,	tcon_attach),
	{ 0, 0 }
};

static driver_t tcon_driver = {
	"tcon",
	tcon_methods,
	sizeof(struct tcon_softc),
};

static devclass_t tcon_devclass;

DRIVER_MODULE(tcon, simplebus, tcon_driver, tcon_devclass, 0, 0);
