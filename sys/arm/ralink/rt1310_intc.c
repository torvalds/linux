/*-
 * Copyright (c) 2010 Jakub Wojciech Klama <jceel@FreeBSD.org>
 * Copyright (c) 2015 Hiroki Mori
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
 *
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "opt_platform.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/types.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/module.h>
#include <sys/proc.h>
#include <sys/rman.h>
#include <vm/vm.h>
#include <vm/vm_kern.h>
#include <vm/pmap.h>
#include <vm/vm_page.h>
#include <vm/vm_extern.h>

#define	_ARM32_BUS_DMA_PRIVATE
#include <machine/bus.h>
#include <machine/intr.h>

#include <dev/fdt/fdt_common.h>
#include <dev/ofw/openfirm.h>

#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>

#include <arm/ralink/rt1310reg.h>

#define	INTC_NIRQS	32

#include "pic_if.h"

struct rt1310_irqsrc {
	struct intr_irqsrc      ri_isrc;
	u_int                   ri_irq;
};

struct rt1310_intc_softc {
	device_t dev;
	struct resource *	ri_res;
	bus_space_tag_t		ri_bst;
	bus_space_handle_t	ri_bsh;
	struct rt1310_irqsrc	ri_isrcs[INTC_NIRQS];
};

static int rt1310_intc_probe(device_t);
static int rt1310_intc_attach(device_t);
static int rt1310_pic_attach(struct rt1310_intc_softc *sc);

static struct rt1310_intc_softc *intc_softc = NULL;

#define	intc_read_4(_sc, _reg)		\
    bus_space_read_4((_sc)->ri_bst, (_sc)->ri_bsh, (_reg))
#define	intc_write_4(_sc, _reg, _val)		\
    bus_space_write_4((_sc)->ri_bst, (_sc)->ri_bsh, (_reg), (_val))

struct rt1310_irqdef {
	u_int                   ri_trig;
	u_int                   ri_prio;
};

struct rt1310_irqdef irqdef[INTC_NIRQS] = {
	{RT_INTC_TRIG_HIGH_LVL, 2},	/* 0 */
	{RT_INTC_TRIG_HIGH_LVL, 2},
	{RT_INTC_TRIG_HIGH_LVL, 2},
	{RT_INTC_TRIG_HIGH_LVL, 1},
	{RT_INTC_TRIG_HIGH_LVL, 2},
	{RT_INTC_TRIG_HIGH_LVL, 1},
	{RT_INTC_TRIG_HIGH_LVL, 1},
	{RT_INTC_TRIG_HIGH_LVL, 1},
	{RT_INTC_TRIG_HIGH_LVL, 1},	/* 8 */
	{RT_INTC_TRIG_HIGH_LVL, 1},
	{RT_INTC_TRIG_HIGH_LVL, 2},
	{RT_INTC_TRIG_LOW_LVL, 2},
	{RT_INTC_TRIG_LOW_LVL, 2},
	{RT_INTC_TRIG_LOW_LVL, 4},
	{RT_INTC_TRIG_HIGH_LVL, 2},
	{RT_INTC_TRIG_HIGH_LVL, 2},
	{RT_INTC_TRIG_HIGH_LVL, 2},	/* 16 */
	{RT_INTC_TRIG_HIGH_LVL, 2},
	{RT_INTC_TRIG_LOW_LVL, 2},
	{RT_INTC_TRIG_LOW_LVL, 2},
	{RT_INTC_TRIG_LOW_LVL, 2},
	{RT_INTC_TRIG_LOW_LVL, 2},
	{RT_INTC_TRIG_NEG_EDGE, 2},
	{RT_INTC_TRIG_HIGH_LVL, 3},
	{RT_INTC_TRIG_HIGH_LVL, 2},	/* 24 */
	{RT_INTC_TRIG_POS_EDGE, 2},
	{RT_INTC_TRIG_POS_EDGE, 2},
	{RT_INTC_TRIG_HIGH_LVL, 2},
	{RT_INTC_TRIG_HIGH_LVL, 2},
	{RT_INTC_TRIG_POS_EDGE, 2},
	{RT_INTC_TRIG_POS_EDGE, 3},
	{RT_INTC_TRIG_POS_EDGE, 3},
};

static int
rt1310_intc_probe(device_t dev)
{
	if (!ofw_bus_status_okay(dev))
		return (ENXIO);

	if (!ofw_bus_is_compatible_strict(dev, "rt,pic"))
		return (ENXIO);

	device_set_desc(dev, "RT1310 Interrupt Controller");
	return (BUS_PROBE_DEFAULT);
}

static int
rt1310_intc_attach(device_t dev)
{
	struct rt1310_intc_softc *sc = device_get_softc(dev);
	int rid = 0;
	int i;

	if (intc_softc)
		return (ENXIO);

	sc->dev = dev;

	sc->ri_res = bus_alloc_resource_any(dev, SYS_RES_MEMORY, &rid, 
	    RF_ACTIVE);
	if (!sc->ri_res) {
		device_printf(dev, "could not alloc resources\n");
		return (ENXIO);
	}

	sc->ri_bst = rman_get_bustag(sc->ri_res);
	sc->ri_bsh = rman_get_bushandle(sc->ri_res);
	intc_softc = sc;
	rt1310_pic_attach(sc);

	intc_write_4(sc, RT_INTC_IECR, 0);
	intc_write_4(sc, RT_INTC_ICCR, ~0);

	for (i = 0; i <= INTC_NIRQS; ++i) {
		intc_write_4(sc, RT_INTC_SCR0+i*4, 
			(irqdef[i].ri_trig << RT_INTC_TRIG_SHIF) | 
			irqdef[i].ri_prio);
		intc_write_4(sc, RT_INTC_SVR0+i*4, i);
	}

	/* Clear interrupt status registers and disable all interrupts */
	intc_write_4(sc, RT_INTC_ICCR, ~0);
	intc_write_4(sc, RT_INTC_IMR, 0);
	return (0);
}

static void
rt1310_enable_intr(device_t dev, struct intr_irqsrc *isrc)
{
	u_int irq;
	unsigned int value;
	struct rt1310_intc_softc *sc;

	sc = intc_softc;
	irq = ((struct rt1310_irqsrc *)isrc)->ri_irq;

	value = intc_read_4(sc, RT_INTC_IECR);

	value |= (1 << irq);

	intc_write_4(sc, RT_INTC_IMR, value);
	intc_write_4(sc, RT_INTC_IECR, value);
}

static void
rt1310_disable_intr(device_t dev, struct intr_irqsrc *isrc)
{
	u_int irq;
	unsigned int value;
	struct rt1310_intc_softc *sc;

	sc = intc_softc;
	irq = ((struct rt1310_irqsrc *)isrc)->ri_irq;

	/* Clear bit in ER register */
	value = intc_read_4(sc, RT_INTC_IECR);
	value &= ~(1 << irq);
	intc_write_4(sc, RT_INTC_IECR, value);
	intc_write_4(sc, RT_INTC_IMR, value);

	intc_write_4(sc, RT_INTC_ICCR, 1 << irq);
}

static int
rt1310_map_intr(device_t dev, struct intr_map_data *data,
    struct intr_irqsrc **isrcp)
{
	struct intr_map_data_fdt *daf;
	struct rt1310_intc_softc *sc;

	if (data->type != INTR_MAP_DATA_FDT)
		return (ENOTSUP);

	daf = (struct intr_map_data_fdt *)data;

	if (daf->ncells != 1 || daf->cells[0] >= INTC_NIRQS)
		return (EINVAL);

	sc = device_get_softc(dev);
	*isrcp = &sc->ri_isrcs[daf->cells[0]].ri_isrc;
	return (0);
}

static void
rt1310_pre_ithread(device_t dev, struct intr_irqsrc *isrc)
{
	arm_irq_memory_barrier(0);
	rt1310_disable_intr(dev, isrc);
}

static void
rt1310_post_ithread(device_t dev, struct intr_irqsrc *isrc)
{
	arm_irq_memory_barrier(0);
	rt1310_enable_intr(dev, isrc);
}

static void
rt1310_post_filter(device_t dev, struct intr_irqsrc *isrc)
{
	u_int irq;
	struct rt1310_intc_softc *sc;

	arm_irq_memory_barrier(0);
	sc = intc_softc;
	irq = ((struct rt1310_irqsrc *)isrc)->ri_irq;
	
   	intc_write_4(sc, RT_INTC_ICCR, 1 << irq);
}

static int
rt1310_intr(void *arg)
{
	uint32_t irq;
	struct rt1310_intc_softc *sc = arg;

	irq = ffs(intc_read_4(sc, RT_INTC_IPR)) - 1;

	if (intr_isrc_dispatch(&sc->ri_isrcs[irq].ri_isrc,
	    curthread->td_intr_frame) != 0) {
	      	intc_write_4(sc, RT_INTC_ICCR, 1 << irq);
		device_printf(sc->dev, "Stray irq %u disabled\n", irq);
	}

	arm_irq_memory_barrier(0);

	return (FILTER_HANDLED);
}

static int
rt1310_pic_attach(struct rt1310_intc_softc *sc)
{
	struct intr_pic *pic;
	int error;
	uint32_t irq;
	const char *name;
	intptr_t xref;

	name = device_get_nameunit(sc->dev);
	for (irq = 0; irq < INTC_NIRQS; irq++) {
		sc->ri_isrcs[irq].ri_irq = irq;

		error = intr_isrc_register(&sc->ri_isrcs[irq].ri_isrc,
		    sc->dev, 0, "%s,%u", name, irq);
		if (error != 0)
			return (error);
	}

	xref = OF_xref_from_node(ofw_bus_get_node(sc->dev));
	pic = intr_pic_register(sc->dev, xref);
	if (pic == NULL)
		return (ENXIO);

	return (intr_pic_claim_root(sc->dev, xref, rt1310_intr, sc, 0));
}

struct fdt_fixup_entry fdt_fixup_table[] = {
	{ NULL, NULL }
};

static device_method_t rt1310_intc_methods[] = {
	DEVMETHOD(device_probe,		rt1310_intc_probe),
	DEVMETHOD(device_attach,	rt1310_intc_attach),
	DEVMETHOD(pic_disable_intr,	rt1310_disable_intr),
	DEVMETHOD(pic_enable_intr,	rt1310_enable_intr),
	DEVMETHOD(pic_map_intr,		rt1310_map_intr),
	DEVMETHOD(pic_post_filter,	rt1310_post_filter),
	DEVMETHOD(pic_post_ithread,	rt1310_post_ithread),
	DEVMETHOD(pic_pre_ithread,	rt1310_pre_ithread),
	{ 0, 0 }
};

static driver_t rt1310_intc_driver = {
	"pic",
	rt1310_intc_methods,
	sizeof(struct rt1310_intc_softc),
};

static devclass_t rt1310_intc_devclass;

EARLY_DRIVER_MODULE(pic, simplebus, rt1310_intc_driver, rt1310_intc_devclass, 0, 0, BUS_PASS_INTERRUPT);
