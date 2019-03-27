/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2008 Marius Strobl <marius@FreeBSD.org>
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

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/resource.h>
#include <sys/rman.h>

#include <dev/ofw/ofw_bus.h>

#include <machine/bus.h>
#include <machine/resource.h>

#define	SCHPPM_NREG	1

#define	SCHPPM_ESTAR	0

#define	SCHPPM_ESTAR_CTRL		0x00
#define	SCHPPM_ESTAR_CTRL_1		0x00000001
#define	SCHPPM_ESTAR_CTRL_2		0x00000002
#define	SCHPPM_ESTAR_CTRL_32		0x00000020
#define	SCHPPM_ESTAR_CTRL_MASK						\
	(SCHPPM_ESTAR_CTRL_1 | SCHPPM_ESTAR_CTRL_2 | SCHPPM_ESTAR_CTRL_32)

static struct resource_spec schppm_res_spec[] = {
	{ SYS_RES_MEMORY, SCHPPM_ESTAR, RF_ACTIVE },
	{ -1, 0 }
};

struct schppm_softc {
	struct resource		*sc_res[SCHPPM_NREG];
};

#define	SCHPPM_READ(sc, reg, off)					\
	bus_read_8((sc)->sc_res[(reg)], (off))
#define	SCHPPM_WRITE(sc, reg, off, val)					\
	bus_write_8((sc)->sc_res[(reg)], (off), (val))

static device_probe_t schppm_probe;
static device_attach_t schppm_attach;

static device_method_t schppm_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		schppm_probe),
	DEVMETHOD(device_attach,	schppm_attach),

	DEVMETHOD_END
};

static devclass_t schppm_devclass;

DEFINE_CLASS_0(schppm, schppm_driver, schppm_methods,
    sizeof(struct schppm_softc));
DRIVER_MODULE(schppm, nexus, schppm_driver, schppm_devclass, 0, 0);

static int
schppm_probe(device_t dev)
{
	const char* compat;

	compat = ofw_bus_get_compat(dev);
	if (compat != NULL && strcmp(ofw_bus_get_name(dev), "ppm") == 0 &&
	    strcmp(compat, "gp2-ppm") == 0) {
		device_set_desc(dev, "Schizo power management");
		return (BUS_PROBE_DEFAULT);
	}
	return (ENXIO);
}

static int
schppm_attach(device_t dev)
{
	struct schppm_softc *sc;

	sc = device_get_softc(dev);
	if (bus_alloc_resources(dev, schppm_res_spec, sc->sc_res)) {
		device_printf(dev, "failed to allocate resources\n");
		bus_release_resources(dev, schppm_res_spec, sc->sc_res);
		return (ENXIO);
	}

	if (bootverbose) {
		device_printf(dev, "running at ");
		switch (SCHPPM_READ(sc, SCHPPM_ESTAR, SCHPPM_ESTAR_CTRL) &
		    SCHPPM_ESTAR_CTRL_MASK) {
		case SCHPPM_ESTAR_CTRL_1:
			printf("full");
			break;
		case SCHPPM_ESTAR_CTRL_2:
			printf("half");
			break;
		case SCHPPM_ESTAR_CTRL_32:
			printf("1/32");
			break;
		default:
			printf("unknown");
			break;
		}
		printf(" speed\n");
	}

	return (0);
}
