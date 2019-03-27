/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2012-2017 Oleksandr Tymoshenko <gonzo@bluezbox.com>
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

#define	VICIRQSTATUS	0x000
#define	VICFIQSTATUS	0x004
#define	VICRAWINTR	0x008
#define	VICINTSELECT	0x00C
#define	VICINTENABLE	0x010
#define	VICINTENCLEAR	0x014
#define	VICSOFTINT	0x018
#define	VICSOFTINTCLEAR	0x01C
#define	VICPROTECTION	0x020
#define	VICPERIPHID	0xFE0
#define	VICPRIMECELLID	0xFF0

#define	VIC_NIRQS	32

struct pl190_intc_irqsrc {
	struct intr_irqsrc		isrc;
	u_int				irq;
};

struct pl190_intc_softc {
	device_t		dev;
	struct mtx		mtx;
	struct resource *	intc_res;
	struct pl190_intc_irqsrc	isrcs[VIC_NIRQS];
};

#define	INTC_VIC_READ_4(sc, reg)		\
    bus_read_4(sc->intc_res, (reg))
#define	INTC_VIC_WRITE_4(sc, reg, val)		\
    bus_write_4(sc->intc_res, (reg), (val))

#define	VIC_LOCK(_sc) mtx_lock_spin(&(_sc)->mtx)
#define	VIC_UNLOCK(_sc) mtx_unlock_spin(&(_sc)->mtx)

static inline void
pl190_intc_irq_dispatch(struct pl190_intc_softc *sc, u_int irq,
    struct trapframe *tf)
{
	struct pl190_intc_irqsrc *src;

	src = &sc->isrcs[irq];
	if (intr_isrc_dispatch(&src->isrc, tf) != 0)
		device_printf(sc->dev, "Stray irq %u detected\n", irq);
}

static int
pl190_intc_intr(void *arg)
{
	struct pl190_intc_softc *sc;
	u_int cpu;
	uint32_t num, pending;
	struct trapframe *tf;

	sc = arg;
	cpu = PCPU_GET(cpuid);
	tf = curthread->td_intr_frame;

	VIC_LOCK(sc);
	pending = INTC_VIC_READ_4(sc, VICIRQSTATUS);
	VIC_UNLOCK(sc);
	for (num = 0 ; num < VIC_NIRQS; num++) {
		if (pending & (1 << num))
			pl190_intc_irq_dispatch(sc, num, tf);
	}

	return (FILTER_HANDLED);
}

static void
pl190_intc_disable_intr(device_t dev, struct intr_irqsrc *isrc)
{
	struct pl190_intc_softc *sc;
	struct pl190_intc_irqsrc *src;

	sc = device_get_softc(dev);
	src = (struct pl190_intc_irqsrc *)isrc;

	VIC_LOCK(sc);
	INTC_VIC_WRITE_4(sc, VICINTENCLEAR, (1 << src->irq));
	VIC_UNLOCK(sc);
}

static void
pl190_intc_enable_intr(device_t dev, struct intr_irqsrc *isrc)
{
	struct pl190_intc_softc *sc;
	struct pl190_intc_irqsrc *src;

	sc = device_get_softc(dev);
	src = (struct pl190_intc_irqsrc *)isrc;

	VIC_LOCK(sc);
	INTC_VIC_WRITE_4(sc, VICINTENABLE, (1 << src->irq));
	VIC_UNLOCK(sc);
}

static int
pl190_intc_map_intr(device_t dev, struct intr_map_data *data,
    struct intr_irqsrc **isrcp)
{
	struct intr_map_data_fdt *daf;
	struct pl190_intc_softc *sc;

	if (data->type != INTR_MAP_DATA_FDT)
		return (ENOTSUP);

	daf = (struct intr_map_data_fdt *)data;
	if (daf->ncells != 1 || daf->cells[0] >= VIC_NIRQS)
		return (EINVAL);

	sc = device_get_softc(dev);
	*isrcp = &sc->isrcs[daf->cells[0]].isrc;
	return (0);
}

static void
pl190_intc_pre_ithread(device_t dev, struct intr_irqsrc *isrc)
{
	pl190_intc_disable_intr(dev, isrc);
}

static void
pl190_intc_post_ithread(device_t dev, struct intr_irqsrc *isrc)
{
	struct pl190_intc_irqsrc *src;

	src = (struct pl190_intc_irqsrc *)isrc;
	pl190_intc_enable_intr(dev, isrc);
	arm_irq_memory_barrier(src->irq);
}

static void
pl190_intc_post_filter(device_t dev, struct intr_irqsrc *isrc)
{
	struct pl190_intc_irqsrc *src;

	src = (struct pl190_intc_irqsrc *)isrc;
	arm_irq_memory_barrier(src->irq);
}

static int
pl190_intc_setup_intr(device_t dev, struct intr_irqsrc *isrc,
    struct resource *res, struct intr_map_data *data)
{

	return (0);
}

static int
pl190_intc_probe(device_t dev)
{

	if (!ofw_bus_status_okay(dev))
		return (ENXIO);

	if (!ofw_bus_is_compatible(dev, "arm,versatile-vic"))
		return (ENXIO);
	device_set_desc(dev, "ARM PL190 VIC");
	return (BUS_PROBE_DEFAULT);
}

static int
pl190_intc_attach(device_t dev)
{
	struct		pl190_intc_softc *sc;
	uint32_t	id;
	int		i, rid;
	struct		pl190_intc_irqsrc *isrcs;
	struct intr_pic *pic;
	int		error;
	uint32_t	irq;
	const char	*name;
	phandle_t	xref;

	sc = device_get_softc(dev);
	sc->dev = dev;
	mtx_init(&sc->mtx, device_get_nameunit(dev), "pl190",
	    MTX_SPIN);

	/* Request memory resources */
	rid = 0;
	sc->intc_res = bus_alloc_resource_any(dev, SYS_RES_MEMORY, &rid,
	    RF_ACTIVE);
	if (sc->intc_res == NULL) {
		device_printf(dev, "Error: could not allocate memory resources\n");
		return (ENXIO);
	}

	/*
	 * All interrupts should use IRQ line
	 */
	INTC_VIC_WRITE_4(sc, VICINTSELECT, 0x00000000);
	/* Disable all interrupts */
	INTC_VIC_WRITE_4(sc, VICINTENCLEAR, 0xffffffff);

	id = 0;
	for (i = 3; i >= 0; i--) {
		id = (id << 8) |
		     (INTC_VIC_READ_4(sc, VICPERIPHID + i*4) & 0xff);
	}

	device_printf(dev, "Peripheral ID: %08x\n", id);

	id = 0;
	for (i = 3; i >= 0; i--) {
		id = (id << 8) |
		     (INTC_VIC_READ_4(sc, VICPRIMECELLID + i*4) & 0xff);
	}

	device_printf(dev, "PrimeCell ID: %08x\n", id);

	/* PIC attachment */
	isrcs = sc->isrcs;
	name = device_get_nameunit(sc->dev);
	for (irq = 0; irq < VIC_NIRQS; irq++) {
		isrcs[irq].irq = irq;
		error = intr_isrc_register(&isrcs[irq].isrc, sc->dev,
		    0, "%s,%u", name, irq);
		if (error != 0)
			return (error);
	}

	xref = OF_xref_from_node(ofw_bus_get_node(sc->dev));
	pic = intr_pic_register(sc->dev, xref);
	if (pic == NULL)
		return (ENXIO);

	return (intr_pic_claim_root(sc->dev, xref, pl190_intc_intr, sc, 0));
}

static device_method_t pl190_intc_methods[] = {
	DEVMETHOD(device_probe,		pl190_intc_probe),
	DEVMETHOD(device_attach,	pl190_intc_attach),

	DEVMETHOD(pic_disable_intr,	pl190_intc_disable_intr),
	DEVMETHOD(pic_enable_intr,	pl190_intc_enable_intr),
	DEVMETHOD(pic_map_intr,		pl190_intc_map_intr),
	DEVMETHOD(pic_post_filter,	pl190_intc_post_filter),
	DEVMETHOD(pic_post_ithread,	pl190_intc_post_ithread),
	DEVMETHOD(pic_pre_ithread,	pl190_intc_pre_ithread),
	DEVMETHOD(pic_setup_intr,	pl190_intc_setup_intr),

	DEVMETHOD_END
};

static driver_t pl190_intc_driver = {
	"intc",
	pl190_intc_methods,
	sizeof(struct pl190_intc_softc),
};

static devclass_t pl190_intc_devclass;

EARLY_DRIVER_MODULE(intc, simplebus, pl190_intc_driver, pl190_intc_devclass,
    0, 0, BUS_PASS_INTERRUPT + BUS_PASS_ORDER_MIDDLE);
