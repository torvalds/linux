/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2012-2017 Oleksandr Tymoshenko <gonzo@freebsd.org>
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
#include <sys/ktr.h>
#include <sys/module.h>
#include <sys/proc.h>
#include <sys/rman.h>
#include <machine/bus.h>
#include <machine/intr.h>

#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>

#include "pic_if.h"

#ifdef  DEBUG
#define dprintf(fmt, args...) printf(fmt, ##args)
#else
#define dprintf(fmt, args...)
#endif

#define	SIC_STATUS	0x00
#define	SIC_RAWSTAT	0x04
#define	SIC_ENABLE	0x08
#define	SIC_ENSET	0x08
#define	SIC_ENCLR	0x0C
#define	SIC_SOFTINTSET	0x10
#define	SIC_SOFTINTCLR	0x14
#define	SIC_PICENABLE	0x20
#define	SIC_PICENSET	0x20
#define	SIC_PICENCLR	0x24

#define	SIC_NIRQS	32

struct versatile_sic_irqsrc {
	struct intr_irqsrc		isrc;
	u_int				irq;
};

struct versatile_sic_softc {
	device_t		dev;
	struct mtx		mtx;
	struct resource *	mem_res;
	struct resource *	irq_res;
	void			*intrh;
	struct versatile_sic_irqsrc	isrcs[SIC_NIRQS];
};

#define	SIC_LOCK(_sc) mtx_lock_spin(&(_sc)->mtx)
#define	SIC_UNLOCK(_sc) mtx_unlock_spin(&(_sc)->mtx)

#define	SIC_READ_4(sc, reg)			\
    bus_read_4(sc->mem_res, (reg))
#define	SIC_WRITE_4(sc, reg, val)		\
    bus_write_4(sc->mem_res, (reg), (val))

/*
 * Driver stuff
 */
static int versatile_sic_probe(device_t);
static int versatile_sic_attach(device_t);
static int versatile_sic_detach(device_t);

static void
versatile_sic_disable_intr(device_t dev, struct intr_irqsrc *isrc)
{
	struct versatile_sic_softc *sc;
	struct versatile_sic_irqsrc *src;

	sc = device_get_softc(dev);
	src = (struct versatile_sic_irqsrc *)isrc;

	SIC_LOCK(sc);
	SIC_WRITE_4(sc, SIC_ENCLR, (1 << src->irq));
	SIC_UNLOCK(sc);
}

static void
versatile_sic_enable_intr(device_t dev, struct intr_irqsrc *isrc)
{
	struct versatile_sic_softc *sc;
	struct versatile_sic_irqsrc *src;

	sc = device_get_softc(dev);
	src = (struct versatile_sic_irqsrc *)isrc;

	SIC_LOCK(sc);
	SIC_WRITE_4(sc, SIC_ENSET, (1 << src->irq));
	SIC_UNLOCK(sc);
}

static int
versatile_sic_map_intr(device_t dev, struct intr_map_data *data,
    struct intr_irqsrc **isrcp)
{
	struct intr_map_data_fdt *daf;
	struct versatile_sic_softc *sc;

	if (data->type != INTR_MAP_DATA_FDT)
		return (ENOTSUP);

	daf = (struct intr_map_data_fdt *)data;
	if (daf->ncells != 1 || daf->cells[0] >= SIC_NIRQS)
		return (EINVAL);

	sc = device_get_softc(dev);
	*isrcp = &sc->isrcs[daf->cells[0]].isrc;
	return (0);
}

static void
versatile_sic_pre_ithread(device_t dev, struct intr_irqsrc *isrc)
{
	versatile_sic_disable_intr(dev, isrc);
}

static void
versatile_sic_post_ithread(device_t dev, struct intr_irqsrc *isrc)
{
	struct versatile_sic_irqsrc *src;

	src = (struct versatile_sic_irqsrc *)isrc;
	arm_irq_memory_barrier(src->irq);
	versatile_sic_enable_intr(dev, isrc);
}

static void
versatile_sic_post_filter(device_t dev, struct intr_irqsrc *isrc)
{
	struct versatile_sic_irqsrc *src;

	src = (struct versatile_sic_irqsrc *)isrc;
	arm_irq_memory_barrier(src->irq);
}

static int
versatile_sic_setup_intr(device_t dev, struct intr_irqsrc *isrc,
    struct resource *res, struct intr_map_data *data)
{

	return (0);
}

static int
versatile_sic_filter(void *arg)
{
	struct versatile_sic_softc *sc;
	struct intr_irqsrc *isrc;
	uint32_t i, interrupts;

	sc = arg;
	SIC_LOCK(sc);
	interrupts = SIC_READ_4(sc, SIC_STATUS);
	SIC_UNLOCK(sc);
	for (i = 0; interrupts != 0; i++, interrupts >>= 1) {
		if ((interrupts & 0x1) == 0)
			continue;
		isrc = &sc->isrcs[i].isrc;
		if (intr_isrc_dispatch(isrc, curthread->td_intr_frame) != 0) {
			versatile_sic_disable_intr(sc->dev, isrc);
			versatile_sic_post_filter(sc->dev, isrc);
			device_printf(sc->dev, "Stray irq %u disabled\n", i);
		}
	}

	return (FILTER_HANDLED);
}

static int
versatile_sic_probe(device_t dev)
{

	if (!ofw_bus_status_okay(dev))
		return (ENXIO);

	if (!ofw_bus_is_compatible(dev, "arm,versatile-sic"))
		return (ENXIO);
	device_set_desc(dev, "ARM Versatile SIC");
	return (BUS_PROBE_DEFAULT);
}

static int
versatile_sic_attach(device_t dev)
{
	struct		versatile_sic_softc *sc = device_get_softc(dev);
	int		rid, error;
	uint32_t	irq;
	const char	*name;
	struct		versatile_sic_irqsrc *isrcs;

	sc->dev = dev;
	mtx_init(&sc->mtx, device_get_nameunit(dev), "sic",
	    MTX_SPIN);

	/* Request memory resources */
	rid = 0;
	sc->mem_res = bus_alloc_resource_any(dev, SYS_RES_MEMORY, &rid,
	    RF_ACTIVE);
	if (sc->mem_res == NULL) {
		device_printf(dev, "Error: could not allocate memory resources\n");
		return (ENXIO);
	}

	/* Request memory resources */
	rid = 0;
	sc->irq_res = bus_alloc_resource_any(dev, SYS_RES_IRQ, &rid,
	    RF_ACTIVE);
	if (sc->irq_res == NULL) {
		device_printf(dev, "could not allocate IRQ resources\n");
		versatile_sic_detach(dev);
		return (ENXIO);
	}

	if ((bus_setup_intr(dev, sc->irq_res, INTR_TYPE_MISC,
	    versatile_sic_filter, NULL, sc, &sc->intrh))) {
		device_printf(dev,
		    "unable to register interrupt handler\n");
		versatile_sic_detach(dev);
		return (ENXIO);
	}

	/* Disable all interrupts on SIC */
	SIC_WRITE_4(sc, SIC_ENCLR, 0xffffffff);

	/* PIC attachment */
	isrcs = sc->isrcs;
	name = device_get_nameunit(sc->dev);
	for (irq = 0; irq < SIC_NIRQS; irq++) {
		isrcs[irq].irq = irq;
		error = intr_isrc_register(&isrcs[irq].isrc, sc->dev,
		    0, "%s,%u", name, irq);
		if (error != 0)
			return (error);
	}

	intr_pic_register(dev, OF_xref_from_node(ofw_bus_get_node(dev)));

	return (0);
}

static int
versatile_sic_detach(device_t dev)
{
	struct		versatile_sic_softc *sc;

	sc = device_get_softc(dev);

	if (sc->intrh)
		bus_teardown_intr(dev, sc->irq_res, sc->intrh);

	if (sc->mem_res == NULL)
		bus_release_resource(dev, SYS_RES_MEMORY,
			rman_get_rid(sc->mem_res), sc->mem_res);

	if (sc->irq_res == NULL)
		bus_release_resource(dev, SYS_RES_IRQ,
			rman_get_rid(sc->irq_res), sc->irq_res);

	mtx_destroy(&sc->mtx);

	return (0);

}

static device_method_t versatile_sic_methods[] = {
	DEVMETHOD(device_probe,		versatile_sic_probe),
	DEVMETHOD(device_attach,	versatile_sic_attach),
	DEVMETHOD(device_detach,	versatile_sic_detach),

	DEVMETHOD(pic_disable_intr,	versatile_sic_disable_intr),
	DEVMETHOD(pic_enable_intr,	versatile_sic_enable_intr),
	DEVMETHOD(pic_map_intr,		versatile_sic_map_intr),
	DEVMETHOD(pic_post_filter,	versatile_sic_post_filter),
	DEVMETHOD(pic_post_ithread,	versatile_sic_post_ithread),
	DEVMETHOD(pic_pre_ithread,	versatile_sic_pre_ithread),
	DEVMETHOD(pic_setup_intr,	versatile_sic_setup_intr),

	DEVMETHOD_END
};

static driver_t versatile_sic_driver = {
	"sic",
	versatile_sic_methods,
	sizeof(struct versatile_sic_softc),
};

static devclass_t versatile_sic_devclass;

DRIVER_MODULE(sic, simplebus, versatile_sic_driver, versatile_sic_devclass, 0, 0);
