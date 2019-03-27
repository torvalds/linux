/*-
 * SPDX-License-Identifier: BSD-4-Clause
 *
 * Copyright (c) 2010
 *	Ben Gray <ben.r.gray@gmail.com>.
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
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by Ben Gray.
 * 4. The name of the company nor the name of the author may be used to
 *    endorse or promote products derived from this software without specific
 *    prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY BEN GRAY ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL BEN GRAY BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/**
 *	SCM - System Control Module
 *
 *	Hopefully in the end this module will contain a bunch of utility functions
 *	for configuring and querying the general system control registers, but for
 *	now it only does pin(pad) multiplexing.
 *
 *	This is different from the GPIO module in that it is used to configure the
 *	pins between modules not just GPIO input/output.
 *
 *	This file contains the generic top level driver, however it relies on chip
 *	specific settings and therefore expects an array of ti_scm_padconf structs
 *	call ti_padconf_devmap to be located somewhere in the kernel.
 *
 */
#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/bus.h>
#include <sys/resource.h>
#include <sys/rman.h>
#include <sys/lock.h>
#include <sys/mutex.h>

#include <machine/bus.h>
#include <machine/resource.h>

#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>
#include <dev/fdt/fdt_pinctrl.h>

#include "ti_scm.h"
#include "ti_cpuid.h"

static struct resource_spec ti_scm_res_spec[] = {
	{ SYS_RES_MEMORY,	0,	RF_ACTIVE },	/* Control memory window */
	{ -1, 0 }
};

static struct ti_scm_softc *ti_scm_sc;

#define	ti_scm_read_4(sc, reg)		\
    bus_space_read_4((sc)->sc_bst, (sc)->sc_bsh, (reg))
#define	ti_scm_write_4(sc, reg, val)		\
    bus_space_write_4((sc)->sc_bst, (sc)->sc_bsh, (reg), (val))

/*
 * Device part of OMAP SCM driver
 */
static int
ti_scm_probe(device_t dev)
{

	if (!ti_soc_is_supported())
		return (ENXIO);

	if (!ofw_bus_status_okay(dev))
		return (ENXIO);

	if (!ofw_bus_is_compatible(dev, "syscon"))
		return (ENXIO);

	if (ti_scm_sc) {
		return (EEXIST);
	}

	device_set_desc(dev, "TI Control Module");
	return (BUS_PROBE_DEFAULT);
}

/**
 *	ti_scm_attach - attaches the timer to the simplebus
 *	@dev: new device
 *
 *	Reserves memory and interrupt resources, stores the softc structure
 *	globally and registers both the timecount and eventtimer objects.
 *
 *	RETURNS
 *	Zero on success or ENXIO if an error occuried.
 */
static int
ti_scm_attach(device_t dev)
{
	struct ti_scm_softc *sc = device_get_softc(dev);

	sc->sc_dev = dev;

	if (bus_alloc_resources(dev, ti_scm_res_spec, sc->sc_res)) {
		device_printf(dev, "could not allocate resources\n");
		return (ENXIO);
	}

	/* Global timer interface */
	sc->sc_bst = rman_get_bustag(sc->sc_res[0]);
	sc->sc_bsh = rman_get_bushandle(sc->sc_res[0]);

	ti_scm_sc = sc;

	/* Attach platform extensions, if any. */
	bus_generic_probe(dev);

	return (bus_generic_attach(dev));
}

int
ti_scm_reg_read_4(uint32_t reg, uint32_t *val)
{
	if (!ti_scm_sc)
		return (ENXIO);

	*val = ti_scm_read_4(ti_scm_sc, reg);
	return (0);
}

int
ti_scm_reg_write_4(uint32_t reg, uint32_t val)
{
	if (!ti_scm_sc)
		return (ENXIO);

	ti_scm_write_4(ti_scm_sc, reg, val);
	return (0);
}


static device_method_t ti_scm_methods[] = {
	DEVMETHOD(device_probe,		ti_scm_probe),
	DEVMETHOD(device_attach,	ti_scm_attach),

	{ 0, 0 }
};

static driver_t ti_scm_driver = {
	"ti_scm",
	ti_scm_methods,
	sizeof(struct ti_scm_softc),
};

static devclass_t ti_scm_devclass;

EARLY_DRIVER_MODULE(ti_scm, simplebus, ti_scm_driver, ti_scm_devclass, 0, 0,
    BUS_PASS_BUS + BUS_PASS_ORDER_MIDDLE);
