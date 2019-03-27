/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2004 Jason L. Wright (jason@thought.net)
 * Copyright (c) 2005 Marius Strobl <marius@FreeBSD.org>
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 *	from: OpenBSD: clkbrd.c,v 1.5 2004/10/01 18:18:49 jason Exp
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/resource.h>
#include <sys/rman.h>

#include <dev/led/led.h>
#include <dev/ofw/ofw_bus.h>

#include <machine/bus.h>
#include <machine/resource.h>

#include <sparc64/fhc/clkbrdreg.h>

#define	CLKBRD_NREG	3

#define	CLKBRD_CF	0
#define	CLKBRD_CLK	1
#define	CLKBRD_CLKVER	2

struct clkbrd_softc {
	device_t		sc_dev;
	struct resource		*sc_res[CLKBRD_NREG];
	int			sc_rid[CLKBRD_NREG];
	bus_space_tag_t		sc_bt[CLKBRD_NREG];
	bus_space_handle_t	sc_bh[CLKBRD_NREG];
	uint8_t			sc_clk_ctrl;
	struct cdev		*sc_led_dev;
	int			sc_flags;
#define	CLKBRD_HAS_CLKVER	(1 << 0)
};

static devclass_t clkbrd_devclass;

static device_probe_t clkbrd_probe;
static device_attach_t clkbrd_attach;
static device_detach_t clkbrd_detach;

static void clkbrd_free_resources(struct clkbrd_softc *);
static void clkbrd_led_func(void *, int);

static device_method_t clkbrd_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		clkbrd_probe),
	DEVMETHOD(device_attach,	clkbrd_attach),
	DEVMETHOD(device_detach,	clkbrd_detach),

        { 0, 0 }
};

static driver_t clkbrd_driver = {
        "clkbrd",
        clkbrd_methods,
        sizeof(struct clkbrd_softc),
};

DRIVER_MODULE(clkbrd, fhc, clkbrd_driver, clkbrd_devclass, 0, 0);

static int
clkbrd_probe(device_t dev)
{

	if (strcmp(ofw_bus_get_name(dev), "clock-board") == 0) {
		device_set_desc(dev, "Clock Board");
		return (0);
	}
	return (ENXIO);
}

static int
clkbrd_attach(device_t dev)
{
	struct clkbrd_softc *sc;
	int i, slots;
	uint8_t r;

	sc = device_get_softc(dev);
	sc->sc_dev = dev;

	for (i = CLKBRD_CF; i <= CLKBRD_CLKVER; i++) {
		sc->sc_rid[i] = i;
		sc->sc_res[i] = bus_alloc_resource_any(sc->sc_dev,
		    SYS_RES_MEMORY, &sc->sc_rid[i], RF_ACTIVE);
		if (sc->sc_res[i] == NULL) {
			if (i != CLKBRD_CLKVER) {
				device_printf(sc->sc_dev,
				    "could not allocate resource %d\n", i);
				goto fail;
			}
			continue;
		}
		sc->sc_bt[i] = rman_get_bustag(sc->sc_res[i]);
		sc->sc_bh[i] = rman_get_bushandle(sc->sc_res[i]);
		if (i == CLKBRD_CLKVER)
			sc->sc_flags |= CLKBRD_HAS_CLKVER;
	}

	slots = 4;
	r = bus_space_read_1(sc->sc_bt[CLKBRD_CLK], sc->sc_bh[CLKBRD_CLK],
	    CLK_STS1);
	switch (r & CLK_STS1_SLOTS_MASK) {
	case CLK_STS1_SLOTS_16:
		slots = 16;
		break;
	case CLK_STS1_SLOTS_8:
		slots = 8;
		break;
	case CLK_STS1_SLOTS_4:
		if (sc->sc_flags & CLKBRD_HAS_CLKVER) {
			r = bus_space_read_1(sc->sc_bt[CLKBRD_CLKVER],
			    sc->sc_bh[CLKBRD_CLKVER], CLKVER_SLOTS);
			if (r != 0 &&
			    (r & CLKVER_SLOTS_MASK) == CLKVER_SLOTS_PLUS)
				slots = 5;
		}
	}

	device_printf(sc->sc_dev, "Sun Enterprise Exx00 machine: %d slots\n",
	    slots);

	sc->sc_clk_ctrl = bus_space_read_1(sc->sc_bt[CLKBRD_CLK],
	    sc->sc_bh[CLKBRD_CLK], CLK_CTRL);
	sc->sc_led_dev = led_create(clkbrd_led_func, sc, "clockboard");

	return (0);

 fail:
	clkbrd_free_resources(sc);

	return (ENXIO);
}

static int
clkbrd_detach(device_t dev)
{
	struct clkbrd_softc *sc;

	sc = device_get_softc(dev);

	led_destroy(sc->sc_led_dev);
	bus_space_write_1(sc->sc_bt[CLKBRD_CLK], sc->sc_bh[CLKBRD_CLK],
	    CLK_CTRL, sc->sc_clk_ctrl);
	bus_space_read_1(sc->sc_bt[CLKBRD_CLK], sc->sc_bh[CLKBRD_CLK],
	    CLK_CTRL);
	clkbrd_free_resources(sc);

	return (0);
}

static void
clkbrd_free_resources(struct clkbrd_softc *sc)
{
	int i;

	for (i = CLKBRD_CF; i <= CLKBRD_CLKVER; i++)
		if (sc->sc_res[i] != NULL)
			bus_release_resource(sc->sc_dev, SYS_RES_MEMORY,
			    sc->sc_rid[i], sc->sc_res[i]);
}

static void
clkbrd_led_func(void *arg, int onoff)
{
	struct clkbrd_softc *sc;
	uint8_t r;

	sc = (struct clkbrd_softc *)arg;

	r = bus_space_read_1(sc->sc_bt[CLKBRD_CLK], sc->sc_bh[CLKBRD_CLK],
	    CLK_CTRL);
	if (onoff)
		r |= CLK_CTRL_RLED;
	else
		r &= ~CLK_CTRL_RLED;
	bus_space_write_1(sc->sc_bt[CLKBRD_CLK], sc->sc_bh[CLKBRD_CLK],
	    CLK_CTRL, r);
	bus_space_read_1(sc->sc_bt[CLKBRD_CLK], sc->sc_bh[CLKBRD_CLK],
	    CLK_CTRL);
}
