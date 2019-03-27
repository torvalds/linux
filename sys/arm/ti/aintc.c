/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2012 Damjan Marion <dmarion@Freebsd.org>
 * All rights reserved.
 *
 * Based on OMAP3 INTC code by Ben Gray
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

#include "opt_platform.h"

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

#define INTC_REVISION		0x00
#define INTC_SYSCONFIG		0x10
#define INTC_SYSSTATUS		0x14
#define INTC_SIR_IRQ		0x40
#define INTC_CONTROL		0x48
#define INTC_THRESHOLD		0x68
#define INTC_MIR_CLEAR(x)	(0x88 + ((x) * 0x20))
#define INTC_MIR_SET(x)		(0x8C + ((x) * 0x20))
#define INTC_ISR_SET(x)		(0x90 + ((x) * 0x20))
#define INTC_ISR_CLEAR(x)	(0x94 + ((x) * 0x20))

#define INTC_SIR_SPURIOUS_MASK	0xffffff80
#define INTC_SIR_ACTIVE_MASK	0x7f

#define INTC_NIRQS	128

struct ti_aintc_irqsrc {
	struct intr_irqsrc	tai_isrc;
	u_int			tai_irq;
};

struct ti_aintc_softc {
	device_t		sc_dev;
	struct resource *	aintc_res[3];
	bus_space_tag_t		aintc_bst;
	bus_space_handle_t	aintc_bsh;
	uint8_t			ver;
	struct ti_aintc_irqsrc	aintc_isrcs[INTC_NIRQS];
};

static struct resource_spec ti_aintc_spec[] = {
	{ SYS_RES_MEMORY,	0,	RF_ACTIVE },
	{ -1, 0 }
};

#define	aintc_read_4(_sc, reg)		\
    bus_space_read_4((_sc)->aintc_bst, (_sc)->aintc_bsh, (reg))
#define	aintc_write_4(_sc, reg, val)		\
    bus_space_write_4((_sc)->aintc_bst, (_sc)->aintc_bsh, (reg), (val))

/* List of compatible strings for FDT tree */
static struct ofw_compat_data compat_data[] = {
	{"ti,am33xx-intc",	1},
	{"ti,omap2-intc",	1},
	{NULL,		 	0},
};

static inline void
ti_aintc_irq_eoi(struct ti_aintc_softc *sc)
{

	aintc_write_4(sc, INTC_CONTROL, 1);
}

static inline void
ti_aintc_irq_mask(struct ti_aintc_softc *sc, u_int irq)
{

	aintc_write_4(sc, INTC_MIR_SET(irq >> 5), (1UL << (irq & 0x1F)));
}

static inline void
ti_aintc_irq_unmask(struct ti_aintc_softc *sc, u_int irq)
{

	aintc_write_4(sc, INTC_MIR_CLEAR(irq >> 5), (1UL << (irq & 0x1F)));
}

static int
ti_aintc_intr(void *arg)
{
	uint32_t irq;
	struct ti_aintc_softc *sc = arg;

	/* Get active interrupt */
	irq = aintc_read_4(sc, INTC_SIR_IRQ);
	if ((irq & INTC_SIR_SPURIOUS_MASK) != 0) {
		device_printf(sc->sc_dev,
		    "Spurious interrupt detected (0x%08x)\n", irq);
		ti_aintc_irq_eoi(sc);
		return (FILTER_HANDLED);
	}

	/* Only level-sensitive interrupts detection is supported. */
	irq &= INTC_SIR_ACTIVE_MASK;
	if (intr_isrc_dispatch(&sc->aintc_isrcs[irq].tai_isrc,
	    curthread->td_intr_frame) != 0) {
		ti_aintc_irq_mask(sc, irq);
		ti_aintc_irq_eoi(sc);
		device_printf(sc->sc_dev, "Stray irq %u disabled\n", irq);
	}

	arm_irq_memory_barrier(irq); /* XXX */
	return (FILTER_HANDLED);
}

static void
ti_aintc_enable_intr(device_t dev, struct intr_irqsrc *isrc)
{
	u_int irq = ((struct ti_aintc_irqsrc *)isrc)->tai_irq;
	struct ti_aintc_softc *sc = device_get_softc(dev);

	arm_irq_memory_barrier(irq);
	ti_aintc_irq_unmask(sc, irq);
}

static void
ti_aintc_disable_intr(device_t dev, struct intr_irqsrc *isrc)
{
	u_int irq = ((struct ti_aintc_irqsrc *)isrc)->tai_irq;
	struct ti_aintc_softc *sc = device_get_softc(dev);

	ti_aintc_irq_mask(sc, irq);
}

static int
ti_aintc_map_intr(device_t dev, struct intr_map_data *data,
    struct intr_irqsrc **isrcp)
{
	struct intr_map_data_fdt *daf;
	struct ti_aintc_softc *sc;

	if (data->type != INTR_MAP_DATA_FDT)
		return (ENOTSUP);

	daf = (struct intr_map_data_fdt *)data;
	if (daf->ncells != 1 || daf->cells[0] >= INTC_NIRQS)
		return (EINVAL);

	sc = device_get_softc(dev);
	*isrcp = &sc->aintc_isrcs[daf->cells[0]].tai_isrc;
	return (0);
}

static void
ti_aintc_pre_ithread(device_t dev, struct intr_irqsrc *isrc)
{
	u_int irq = ((struct ti_aintc_irqsrc *)isrc)->tai_irq;
	struct ti_aintc_softc *sc = device_get_softc(dev);

	ti_aintc_irq_mask(sc, irq);
	ti_aintc_irq_eoi(sc);
}

static void
ti_aintc_post_ithread(device_t dev, struct intr_irqsrc *isrc)
{

	ti_aintc_enable_intr(dev, isrc);
}

static void
ti_aintc_post_filter(device_t dev, struct intr_irqsrc *isrc)
{

	ti_aintc_irq_eoi(device_get_softc(dev));
}

static int
ti_aintc_pic_attach(struct ti_aintc_softc *sc)
{
	struct intr_pic *pic;
	int error;
	uint32_t irq;
	const char *name;
	intptr_t xref;

	name = device_get_nameunit(sc->sc_dev);
	for (irq = 0; irq < INTC_NIRQS; irq++) {
		sc->aintc_isrcs[irq].tai_irq = irq;

		error = intr_isrc_register(&sc->aintc_isrcs[irq].tai_isrc,
		    sc->sc_dev, 0, "%s,%u", name, irq);
		if (error != 0)
			return (error);
	}

	xref = OF_xref_from_node(ofw_bus_get_node(sc->sc_dev));
	pic = intr_pic_register(sc->sc_dev, xref);
	if (pic == NULL)
		return (ENXIO);

	return (intr_pic_claim_root(sc->sc_dev, xref, ti_aintc_intr, sc, 0));
}

static int
ti_aintc_probe(device_t dev)
{
	if (!ofw_bus_status_okay(dev))
		return (ENXIO);

	if (ofw_bus_search_compatible(dev, compat_data)->ocd_data == 0)
		return (ENXIO);

	device_set_desc(dev, "TI AINTC Interrupt Controller");
	return (BUS_PROBE_DEFAULT);
}

static int
ti_aintc_attach(device_t dev)
{
	struct		ti_aintc_softc *sc = device_get_softc(dev);
	uint32_t x;

	sc->sc_dev = dev;

	if (bus_alloc_resources(dev, ti_aintc_spec, sc->aintc_res)) {
		device_printf(dev, "could not allocate resources\n");
		return (ENXIO);
	}

	sc->aintc_bst = rman_get_bustag(sc->aintc_res[0]);
	sc->aintc_bsh = rman_get_bushandle(sc->aintc_res[0]);

	x = aintc_read_4(sc, INTC_REVISION);
	device_printf(dev, "Revision %u.%u\n",(x >> 4) & 0xF, x & 0xF);

	/* SoftReset */
	aintc_write_4(sc, INTC_SYSCONFIG, 2);

	/* Wait for reset to complete */
	while(!(aintc_read_4(sc, INTC_SYSSTATUS) & 1));

	/*Set Priority Threshold */
	aintc_write_4(sc, INTC_THRESHOLD, 0xFF);

	if (ti_aintc_pic_attach(sc) != 0) {
		device_printf(dev, "could not attach PIC\n");
		return (ENXIO);
	}
	return (0);
}

static device_method_t ti_aintc_methods[] = {
	DEVMETHOD(device_probe,		ti_aintc_probe),
	DEVMETHOD(device_attach,	ti_aintc_attach),

	DEVMETHOD(pic_disable_intr,	ti_aintc_disable_intr),
	DEVMETHOD(pic_enable_intr,	ti_aintc_enable_intr),
	DEVMETHOD(pic_map_intr,		ti_aintc_map_intr),
	DEVMETHOD(pic_post_filter,	ti_aintc_post_filter),
	DEVMETHOD(pic_post_ithread,	ti_aintc_post_ithread),
	DEVMETHOD(pic_pre_ithread,	ti_aintc_pre_ithread),

	{ 0, 0 }
};

static driver_t ti_aintc_driver = {
	"ti_aintc",
	ti_aintc_methods,
	sizeof(struct ti_aintc_softc),
};

static devclass_t ti_aintc_devclass;

EARLY_DRIVER_MODULE(ti_aintc, simplebus, ti_aintc_driver, ti_aintc_devclass,
    0, 0, BUS_PASS_INTERRUPT + BUS_PASS_ORDER_MIDDLE);
SIMPLEBUS_PNP_INFO(compat_data);
