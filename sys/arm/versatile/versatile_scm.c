/*
 * Copyright (c) 2017 Oleksandr Tymoshenko <gonzo@freebsd.org>
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

/*
 * SCM - System Control Module
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

#include "versatile_scm.h"

struct versatile_scm_softc {
	device_t		sc_dev;
	struct resource *	sc_mem_res;
};

static struct versatile_scm_softc *versatile_scm_sc;

#define	versatile_scm_read_4(sc, reg)		\
    bus_read_4((sc)->sc_mem_res, (reg))
#define	versatile_scm_write_4(sc, reg, val)		\
    bus_write_4((sc)->sc_mem_res, (reg), (val))

static int
versatile_scm_probe(device_t dev)
{
	if (!ofw_bus_status_okay(dev))
		return (ENXIO);

	if (!ofw_bus_is_compatible(dev, "syscon"))
		return (ENXIO);

	if (versatile_scm_sc) {
		return (EEXIST);
	}

	device_set_desc(dev, "Versatile Control Module");
	return (BUS_PROBE_DEFAULT);
}

static int
versatile_scm_attach(device_t dev)
{
	struct versatile_scm_softc *sc;
	int rid;

	sc = device_get_softc(dev);
	sc->sc_dev = dev;

	rid = 0;
	sc->sc_mem_res = bus_alloc_resource_any(dev, SYS_RES_MEMORY, &rid, RF_ACTIVE);

	if (sc->sc_mem_res == NULL) {
		device_printf(dev, "could not allocate memory resources\n");
		return (ENXIO);
	}

	versatile_scm_sc = sc;

	return (0);
}

int
versatile_scm_reg_read_4(uint32_t reg, uint32_t *val)
{
	if (!versatile_scm_sc)
		return (ENXIO);

	*val = versatile_scm_read_4(versatile_scm_sc, reg);
	return (0);
}

int
versatile_scm_reg_write_4(uint32_t reg, uint32_t val)
{
	if (!versatile_scm_sc)
		return (ENXIO);

	versatile_scm_write_4(versatile_scm_sc, reg, val);
	return (0);
}

static device_method_t versatile_scm_methods[] = {
	DEVMETHOD(device_probe,		versatile_scm_probe),
	DEVMETHOD(device_attach,	versatile_scm_attach),

	DEVMETHOD_END
};

static driver_t versatile_scm_driver = {
	"scm",
	versatile_scm_methods,
	sizeof(struct versatile_scm_softc),
};

static devclass_t versatile_scm_devclass;

EARLY_DRIVER_MODULE(versatile_scm, simplebus, versatile_scm_driver, versatile_scm_devclass, 0, 0,
    BUS_PASS_BUS + BUS_PASS_ORDER_MIDDLE);
