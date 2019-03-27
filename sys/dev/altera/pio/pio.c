/*-
 * Copyright (c) 2014 Ruslan Bukin <br@bsdpad.com>
 * All rights reserved.
 *
 * This software was developed by SRI International and the University of
 * Cambridge Computer Laboratory under DARPA/AFRL contract (FA8750-10-C-0237)
 * ("CTSRD"), as part of the DARPA CRASH research programme.
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
 * Altera PIO (Parallel IO) device driver
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

#include <dev/fdt/fdt_common.h>
#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>

#include <machine/bus.h>
#include <machine/fdt.h>
#include <machine/cpu.h>

#include <dev/altera/pio/pio.h>
#include "pio_if.h"

#define READ4(_sc, _reg) bus_read_4((_sc)->res[0], _reg)
#define READ2(_sc, _reg) bus_read_2((_sc)->res[0], _reg)
#define READ1(_sc, _reg) bus_read_1((_sc)->res[0], _reg)
#define WRITE4(_sc, _reg, _val) bus_write_4((_sc)->res[0], _reg, _val)
#define WRITE2(_sc, _reg, _val) bus_write_2((_sc)->res[0], _reg, _val)
#define WRITE1(_sc, _reg, _val) bus_write_1((_sc)->res[0], _reg, _val)

struct pio_softc {
	struct resource		*res[2];
	bus_space_tag_t		bst;
	bus_space_handle_t	bsh;
	device_t		dev;
	void			*ih;
};

static struct resource_spec pio_spec[] = {
	{ SYS_RES_MEMORY,	0,	RF_ACTIVE },
	{ SYS_RES_IRQ,		0,	RF_ACTIVE },
	{ -1, 0 }
};

static int
pio_setup_irq(device_t dev, void *intr_handler, void *ih_user)
{
	struct pio_softc *sc;

	sc = device_get_softc(dev);

	/* Setup interrupt handlers */
	if (bus_setup_intr(sc->dev, sc->res[1], INTR_TYPE_BIO | INTR_MPSAFE,
		NULL, intr_handler, ih_user, &sc->ih)) {
		device_printf(sc->dev, "Unable to setup intr\n");
		return (1);
	}

	return (0);
}

static int
pio_teardown_irq(device_t dev)
{
	struct pio_softc *sc;

	sc = device_get_softc(dev);

	bus_teardown_intr(sc->dev, sc->res[1], sc->ih);

	return (0);
}

static int
pio_read(device_t dev)
{
	struct pio_softc *sc;

	sc = device_get_softc(dev);

	return (READ4(sc, PIO_DATA));
}

static int
pio_set(device_t dev, int bit, int enable)
{
	struct pio_softc *sc;

	sc = device_get_softc(dev);

	if (enable)
		WRITE4(sc, PIO_OUTSET, bit);
	else
		WRITE4(sc, PIO_OUTCLR, bit);

	return (0);
}

static int
pio_configure(device_t dev, int dir, int mask)
{
	struct pio_softc *sc;

	sc = device_get_softc(dev);

	WRITE4(sc, PIO_INT_MASK, mask);
	WRITE4(sc, PIO_DIR, dir);

	return (0);
}

static int
pio_probe(device_t dev)
{

	if (!ofw_bus_status_okay(dev))
		return (ENXIO);

	if (!ofw_bus_is_compatible(dev, "altr,pio"))
		return (ENXIO);

	device_set_desc(dev, "Altera PIO");
	return (BUS_PROBE_DEFAULT);
}

static int
pio_attach(device_t dev)
{
	struct pio_softc *sc;
	struct fdt_ic *fic;
	phandle_t node;

	sc = device_get_softc(dev);
	sc->dev = dev;

	if (bus_alloc_resources(dev, pio_spec, sc->res)) {
		device_printf(dev, "could not allocate resources\n");
		return (ENXIO);
	}

	/* Memory interface */
	sc->bst = rman_get_bustag(sc->res[0]);
	sc->bsh = rman_get_bushandle(sc->res[0]);

	if ((node = ofw_bus_get_node(sc->dev)) == -1)
		return (ENXIO);

	fic = malloc(sizeof(*fic), M_DEVBUF, M_WAITOK|M_ZERO);
	fic->iph = node;
	fic->dev = dev;
	SLIST_INSERT_HEAD(&fdt_ic_list_head, fic, fdt_ics);

	return (0);
}

static device_method_t pio_methods[] = {
	DEVMETHOD(device_probe,		pio_probe),
	DEVMETHOD(device_attach,	pio_attach),

	/* pio_if.m */
	DEVMETHOD(pio_read,		pio_read),
	DEVMETHOD(pio_configure,	pio_configure),
	DEVMETHOD(pio_set,		pio_set),
	DEVMETHOD(pio_setup_irq,	pio_setup_irq),
	DEVMETHOD(pio_teardown_irq,	pio_teardown_irq),
	DEVMETHOD_END
};

static driver_t pio_driver = {
	"altera_pio",
	pio_methods,
	sizeof(struct pio_softc),
};

static devclass_t pio_devclass;

DRIVER_MODULE(altera_pio, simplebus, pio_driver, pio_devclass, 0, 0);
