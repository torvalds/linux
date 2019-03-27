/*-
 * Copyright (c) 2012 Semihalf.
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
#include <sys/kernel.h>
#include <sys/bus.h>
#include <sys/module.h>

#include <dev/fdt/simplebus.h>
#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>

#include <contrib/ncsw/inc/ncsw_ext.h>
#include <contrib/ncsw/inc/enet_ext.h>

#include "fman.h"

#define	FFMAN_DEVSTR	"Freescale Frame Manager"

static int	fman_fdt_probe(device_t dev);

static device_method_t fman_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		fman_fdt_probe),
	DEVMETHOD(device_attach,	fman_attach),
	DEVMETHOD(device_detach,	fman_detach),

	DEVMETHOD(device_shutdown,	fman_shutdown),
	DEVMETHOD(device_suspend,	fman_suspend),
	DEVMETHOD(device_resume,	fman_resume_dev),

	DEVMETHOD(bus_alloc_resource,	fman_alloc_resource),
	DEVMETHOD(bus_activate_resource,	fman_activate_resource),
	DEVMETHOD(bus_release_resource,	fman_release_resource),
	{ 0, 0 }
};

DEFINE_CLASS_1(fman, fman_driver, fman_methods,
    sizeof(struct fman_softc), simplebus_driver);
static devclass_t fman_devclass;
EARLY_DRIVER_MODULE(fman, simplebus, fman_driver, fman_devclass, 0, 0,
    BUS_PASS_BUS + BUS_PASS_ORDER_MIDDLE);


static int
fman_fdt_probe(device_t dev)
{

	if (!ofw_bus_is_compatible(dev, "fsl,fman"))
		return (ENXIO);

	device_set_desc(dev, FFMAN_DEVSTR);

	return (BUS_PROBE_DEFAULT);
}

uint32_t
fman_get_clock(struct fman_softc *sc)
{
	device_t dev;
	phandle_t node;
	pcell_t fman_clock;

	dev = sc->sc_base.dev;
	node = ofw_bus_get_node(dev);

	if ((OF_getprop(node, "clock-frequency", &fman_clock,
	    sizeof(fman_clock)) <= 0) || (fman_clock == 0)) {
		device_printf(dev, "could not acquire correct frequency "
		    "from DTS\n");

		return (0);
	}

	return ((uint32_t)fman_clock);
}

