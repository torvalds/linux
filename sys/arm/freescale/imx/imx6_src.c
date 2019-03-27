/*-
 * Copyright (c) 2015 Oleksandr Tymoshenko <gonzo@freebsd.org>
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

/*
 * System Reset Control for iMX6
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/bus.h>
#include <sys/rman.h>

#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>

#include <machine/bus.h>

#include <arm/freescale/imx/imx6_src.h>

#define	SRC_SCR		0
#define		SW_IPU1_RST	(1 << 3)

struct src_softc {
	device_t	dev;
	struct resource	*mem_res;
};

static struct src_softc *src_sc;

static inline uint32_t
RD4(struct src_softc *sc, bus_size_t off)
{

	return (bus_read_4(sc->mem_res, off));
}

static inline void
WR4(struct src_softc *sc, bus_size_t off, uint32_t val)
{

	bus_write_4(sc->mem_res, off, val);
}

int
src_reset_ipu(void)
{
	uint32_t reg;
	int timeout = 10000;

	if (src_sc == NULL)
		return (-1);

	reg = RD4(src_sc, SRC_SCR);
	reg |= SW_IPU1_RST;
	WR4(src_sc, SRC_SCR, reg);

	while (timeout-- > 0) {
		reg = RD4(src_sc, SRC_SCR);
		if (reg & SW_IPU1_RST)
			DELAY(1);
		else
			break;
	}

	if (timeout < 0)
		return (-1);
	else
		return (0);
}

static int
src_detach(device_t dev)
{
	struct src_softc *sc;

	sc = device_get_softc(dev);

	if (sc->mem_res != NULL)
		bus_release_resource(dev, SYS_RES_MEMORY, 0, sc->mem_res);

	return (0);
}

static int
src_attach(device_t dev)
{
	struct src_softc *sc;
	int err, rid;

	sc = device_get_softc(dev);
	err = 0;

	/* Allocate bus_space resources. */
	rid = 0;
	sc->mem_res = bus_alloc_resource_any(dev, SYS_RES_MEMORY, &rid,
	    RF_ACTIVE);
	if (sc->mem_res == NULL) {
		device_printf(dev, "Cannot allocate memory resources\n");
		err = ENXIO;
		goto out;
	}

	src_sc = sc;

	err = 0;

out:

	if (err != 0)
		src_detach(dev);

	return (err);
}

static int
src_probe(device_t dev)
{

        if ((ofw_bus_is_compatible(dev, "fsl,imx6q-src") == 0) &&
            (ofw_bus_is_compatible(dev, "fsl,imx6-src") == 0))
		return (ENXIO);

	device_set_desc(dev, "Freescale i.MX6 System Reset Controller");

	return (BUS_PROBE_DEFAULT);
}

static device_method_t src_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,  src_probe),
	DEVMETHOD(device_attach, src_attach),
	DEVMETHOD(device_detach, src_detach),

	DEVMETHOD_END
};

static driver_t src_driver = {
	"src",
	src_methods,
	sizeof(struct src_softc)
};

static devclass_t src_devclass;

DRIVER_MODULE(src, simplebus, src_driver, src_devclass, 0, 0);
