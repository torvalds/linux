/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2012, 2013 The FreeBSD Foundation
 * All rights reserved.
 *
 * This software was developed by Oleksandr Rybalko under sponsorship
 * from the FreeBSD Foundation.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1.	Redistributions of source code must retain the above copyright
 *	notice, this list of conditions and the following disclaimer.
 * 2.	Redistributions in binary form must reproduce the above copyright
 *	notice, this list of conditions and the following disclaimer in the
 *	documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
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
#include <sys/rman.h>
#include <sys/proc.h>
#include <sys/lock.h>
#include <sys/mutex.h>

#include <machine/bus.h>
#include <machine/intr.h>

#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>

#include <arm/freescale/imx/imx51_tzicreg.h>

#include "pic_if.h"

#define	TZIC_NIRQS	128

struct tzic_irqsrc {
	struct intr_irqsrc	isrc;
	u_int			irq;
};

struct tzic_softc {
	device_t	   dev;
	struct resource    *tzicregs;
	struct tzic_irqsrc isrcs[TZIC_NIRQS];
};

static struct tzic_softc *tzic_sc;

static inline uint32_t
tzic_read_4(struct tzic_softc *sc, int reg)
{

	return (bus_read_4(sc->tzicregs, reg));
}

static inline void
tzic_write_4(struct tzic_softc *sc, int reg, uint32_t val)
{

    bus_write_4(sc->tzicregs, reg, val);
}

static inline void
tzic_irq_eoi(struct tzic_softc *sc)
{

	tzic_write_4(sc, TZIC_PRIOMASK, 0xff);
}

static inline void
tzic_irq_mask(struct tzic_softc *sc, u_int irq)
{

	tzic_write_4(sc, TZIC_ENCLEAR(irq >> 5), (1u << (irq & 0x1f)));
}

static inline void
tzic_irq_unmask(struct tzic_softc *sc, u_int irq)
{

	tzic_write_4(sc, TZIC_ENSET(irq >> 5), (1u << (irq & 0x1f)));
}

static int
tzic_intr(void *arg)
{
	struct tzic_softc *sc = arg;
	int b, i, irq;
	uint32_t pending;

	/* Get active interrupt */
	for (i = 0; i < TZIC_NIRQS / 32; ++i) {
		pending = tzic_read_4(sc, TZIC_PND(i));
		if ((b = 31 - __builtin_clz(pending)) < 0)
			continue;
		irq = i * 32 + b;
		tzic_write_4(sc, TZIC_PRIOMASK, 0);
		if (intr_isrc_dispatch(&sc->isrcs[irq].isrc,
		    curthread->td_intr_frame) != 0) {
			tzic_irq_mask(sc, irq);
			tzic_irq_eoi(sc);
			arm_irq_memory_barrier(irq);
			if (bootverbose) {
				device_printf(sc->dev, 
				    "Stray irq %u disabled\n", irq);
			}
		}
		return (FILTER_HANDLED);
	}

	if (bootverbose)
		device_printf(sc->dev, "Spurious interrupt detected\n");

	return (FILTER_HANDLED);
}

static void
tzic_enable_intr(device_t dev, struct intr_irqsrc *isrc)
{
	u_int irq = ((struct tzic_irqsrc *)isrc)->irq;
	struct tzic_softc *sc = device_get_softc(dev);

	arm_irq_memory_barrier(irq);
	tzic_irq_unmask(sc, irq);
}

static void
tzic_disable_intr(device_t dev, struct intr_irqsrc *isrc)
{
	u_int irq = ((struct tzic_irqsrc *)isrc)->irq;
	struct tzic_softc *sc = device_get_softc(dev);

	tzic_irq_mask(sc, irq);
}

static int
tzic_map_intr(device_t dev, struct intr_map_data *data,
    struct intr_irqsrc **isrcp)
{
	struct intr_map_data_fdt *daf;
	struct tzic_softc *sc;

	if (data->type != INTR_MAP_DATA_FDT)
		return (ENOTSUP);

	daf = (struct intr_map_data_fdt *)data;
	if (daf->ncells != 1 || daf->cells[0] >= TZIC_NIRQS)
		return (EINVAL);

	sc = device_get_softc(dev);
	*isrcp = &sc->isrcs[daf->cells[0]].isrc;

	return (0);
}

static void
tzic_pre_ithread(device_t dev, struct intr_irqsrc *isrc)
{
	struct tzic_softc *sc = device_get_softc(dev);

	tzic_irq_mask(sc, ((struct tzic_irqsrc *)isrc)->irq);
	tzic_irq_eoi(sc);
}

static void
tzic_post_ithread(device_t dev, struct intr_irqsrc *isrc)
{

	tzic_enable_intr(dev, isrc);
}

static void
tzic_post_filter(device_t dev, struct intr_irqsrc *isrc)
{

	tzic_irq_eoi(device_get_softc(dev));
}

static int
tzic_pic_attach(struct tzic_softc *sc)
{
	struct intr_pic *pic;
	const char *name;
	intptr_t xref;
	int error;
	u_int irq;

	name = device_get_nameunit(sc->dev);
	for (irq = 0; irq < TZIC_NIRQS; irq++) {
		sc->isrcs[irq].irq = irq;
		error = intr_isrc_register(&sc->isrcs[irq].isrc,
		    sc->dev, 0, "%s,%u", name, irq);
		if (error != 0)
			return (error);
	}

	xref = OF_xref_from_node(ofw_bus_get_node(sc->dev));
	pic = intr_pic_register(sc->dev, xref);
	if (pic == NULL)
		return (ENXIO);

	return (intr_pic_claim_root(sc->dev, xref, tzic_intr, sc, 0));
}

static int
tzic_probe(device_t dev)
{

	if (!ofw_bus_status_okay(dev))
		return (ENXIO);

	if (ofw_bus_is_compatible(dev, "fsl,tzic")) {
		device_set_desc(dev, "TrustZone Interrupt Controller");
		return (BUS_PROBE_DEFAULT);
	}
	return (ENXIO);
}

static int
tzic_attach(device_t dev)
{
	struct tzic_softc *sc = device_get_softc(dev);
	int i;

	if (tzic_sc)
		return (ENXIO);
	tzic_sc = sc;
	sc->dev = dev;

	i = 0;
	sc->tzicregs = bus_alloc_resource_any(dev, SYS_RES_MEMORY, &i,
	    RF_ACTIVE);
	if (sc->tzicregs == NULL) {
		device_printf(dev, "could not allocate resources\n");
		return (ENXIO);
	}

	/* route all interrupts to IRQ.  secure interrupts are for FIQ */
	for (i = 0; i < 4; i++)
		tzic_write_4(sc, TZIC_INTSEC(i), 0xffffffff);

	/* disable all interrupts */
	for (i = 0; i < 4; i++)
		tzic_write_4(sc, TZIC_ENCLEAR(i), 0xffffffff);

	/* Set all interrupts to priority 0 (max). */
	for (i = 0; i < 128 / 4; ++i)
		tzic_write_4(sc, TZIC_PRIORITY(i), 0);

	/*
	 * Set priority mask to lowest (unmasked) prio, set synchronizer to
	 * low-latency mode (as opposed to low-power), enable the controller.
	 */
	tzic_write_4(sc, TZIC_PRIOMASK, 0xff);
	tzic_write_4(sc, TZIC_SYNCCTRL, 0);
	tzic_write_4(sc, TZIC_INTCNTL, INTCNTL_NSEN_MASK|INTCNTL_NSEN|INTCNTL_EN);

	/* Register as a root pic. */
	if (tzic_pic_attach(sc) != 0) {
		device_printf(dev, "could not attach PIC\n");
		return (ENXIO);
	}

	return (0);
}

static device_method_t tzic_methods[] = {
	DEVMETHOD(device_probe,		tzic_probe),
	DEVMETHOD(device_attach,	tzic_attach),

	DEVMETHOD(pic_disable_intr,	tzic_disable_intr),
	DEVMETHOD(pic_enable_intr,	tzic_enable_intr),
	DEVMETHOD(pic_map_intr,		tzic_map_intr),
	DEVMETHOD(pic_post_filter,	tzic_post_filter),
	DEVMETHOD(pic_post_ithread,	tzic_post_ithread),
	DEVMETHOD(pic_pre_ithread,	tzic_pre_ithread),

	DEVMETHOD_END
};

static driver_t tzic_driver = {
	"tzic",
	tzic_methods,
	sizeof(struct tzic_softc),
};

static devclass_t tzic_devclass;

EARLY_DRIVER_MODULE(tzic, ofwbus, tzic_driver, tzic_devclass, 0, 0,
    BUS_PASS_INTERRUPT);
