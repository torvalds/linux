/*-
 * Copyright (c) 2015 Alexander Kabaev
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions, and the following disclaimer,
 *    without modification, immediately at the beginning of the file.
 * 2. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 */

#include "opt_platform.h"

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/kernel.h>
#include <sys/ktr.h>
#include <sys/module.h>
#include <sys/malloc.h>
#include <sys/rman.h>
#include <sys/pcpu.h>
#include <sys/proc.h>
#include <sys/cpuset.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/smp.h>
#include <sys/sched.h>
#include <machine/bus.h>
#include <machine/intr.h>
#include <machine/smp.h>

#include <dev/fdt/fdt_common.h>
#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>

#include <mips/ingenic/jz4780_regs.h>

#include "pic_if.h"

#define	JZ4780_NIRQS	64

static int jz4780_pic_intr(void *);

struct jz4780_pic_isrc {
	struct intr_irqsrc isrc;
	u_int  irq;
};

struct jz4780_pic_softc {
	device_t		pic_dev;
	void *                  pic_intrhand;
	struct resource *       pic_res[2];
	struct jz4780_pic_isrc  pic_irqs[JZ4780_NIRQS];
	uint32_t		nirqs;
};

#define	PIC_INTR_ISRC(sc, irq)	(&(sc)->pic_irqs[(irq)].isrc)

static struct resource_spec jz4780_pic_spec[] = {
	{ SYS_RES_MEMORY,	0,	RF_ACTIVE },	/* Registers */
	{ SYS_RES_IRQ,		0,	RF_ACTIVE },	/* Parent interrupt */
	{ -1, 0 }
};

static struct ofw_compat_data compat_data[] = {
	{"ingenic,jz4780-intc",	true},
	{NULL,			false}
};

#define	READ4(_sc, _reg)	bus_read_4((_sc)->pic_res[0], _reg)
#define	WRITE4(_sc, _reg, _val) bus_write_4((_sc)->pic_res[0], _reg, _val)

static int
jz4780_pic_probe(device_t dev)
{

	if (!ofw_bus_status_okay(dev))
		return (ENXIO);

	if (!ofw_bus_search_compatible(dev, compat_data)->ocd_data)
		return (ENXIO);
	device_set_desc(dev, "JZ4780 Interrupt Controller");
	return (BUS_PROBE_DEFAULT);
}

static inline void
pic_irq_unmask(struct jz4780_pic_softc *sc, u_int irq)
{
	if (irq < 32)
		WRITE4(sc, JZ_ICMCR0, (1u << irq));
	else
		WRITE4(sc, JZ_ICMCR1, (1u << (irq - 32)));
}

static inline void
pic_irq_mask(struct jz4780_pic_softc *sc, u_int irq)
{
	if (irq < 32)
		WRITE4(sc, JZ_ICMSR0, (1u << irq));
	else
		WRITE4(sc, JZ_ICMSR1, (1u << (irq - 32)));
}

static inline intptr_t
pic_xref(device_t dev)
{
	return (OF_xref_from_node(ofw_bus_get_node(dev)));
}

static int
jz4780_pic_register_isrcs(struct jz4780_pic_softc *sc)
{
	int error;
	uint32_t irq, i;
	struct intr_irqsrc *isrc;
	const char *name;

	name = device_get_nameunit(sc->pic_dev);
	for (irq = 0; irq < sc->nirqs; irq++) {
		sc->pic_irqs[irq].irq = irq;
		isrc = PIC_INTR_ISRC(sc, irq);
		error = intr_isrc_register(isrc, sc->pic_dev, 0, "%s,%d",
		    name, irq);
		if (error != 0) {
			for (i = 0; i < irq; i++)
				intr_isrc_deregister(PIC_INTR_ISRC(sc, irq));
			device_printf(sc->pic_dev, "%s failed", __func__);
			return (error);
		}
	}

	return (0);
}

static int
jz4780_pic_attach(device_t dev)
{
	struct jz4780_pic_softc *sc;
	intptr_t xref;

	xref = pic_xref(dev);

	sc = device_get_softc(dev);

	if (bus_alloc_resources(dev, jz4780_pic_spec, sc->pic_res)) {
		device_printf(dev, "could not allocate resources\n");
		return (ENXIO);
	}

	sc->pic_dev = dev;

	/* Set the number of interrupts */
	sc->nirqs = nitems(sc->pic_irqs);

	/* Mask all interrupts */
	WRITE4(sc, JZ_ICMR0, 0xFFFFFFFF);
	WRITE4(sc, JZ_ICMR1, 0xFFFFFFFF);

	/* Register the interrupts */
	if (jz4780_pic_register_isrcs(sc) != 0) {
		device_printf(dev, "could not register PIC ISRCs\n");
		goto cleanup;
	}

	/*
	 * Now, when everything is initialized, it's right time to
	 * register interrupt controller to interrupt framefork.
	 */
	if (intr_pic_register(dev, xref) == NULL) {
		device_printf(dev, "could not register PIC\n");
		goto cleanup;
	}

	if (bus_setup_intr(dev, sc->pic_res[1], INTR_TYPE_CLK,
	    jz4780_pic_intr, NULL, sc, &sc->pic_intrhand)) {
		device_printf(dev, "could not setup irq handler\n");
		intr_pic_deregister(dev, xref);
		goto cleanup;
	}

	return (0);

cleanup:
	bus_release_resources(dev, jz4780_pic_spec, sc->pic_res);

	return(ENXIO);
}

static int
jz4780_pic_intr(void *arg)
{
	struct jz4780_pic_softc *sc = arg;
	struct intr_irqsrc *isrc;
	struct thread *td;
	uint32_t i, intr;

	td = curthread;
	/* Workaround: do not inflate intr nesting level */
	td->td_intr_nesting_level--;

	intr = READ4(sc, JZ_ICPR0);
	while ((i = fls(intr)) != 0) {
		i--;
		intr &= ~(1u << i);

		isrc = PIC_INTR_ISRC(sc, i);
		if (intr_isrc_dispatch(isrc, curthread->td_intr_frame) != 0) {
			device_printf(sc->pic_dev, "Stray interrupt %u detected\n", i);
			pic_irq_mask(sc, i);
			continue;
		}
	}

	KASSERT(i == 0, ("all interrupts handled"));

	intr = READ4(sc, JZ_ICPR1);
	while ((i = fls(intr)) != 0) {
		i--;
		intr &= ~(1u << i);
		i += 32;

		isrc = PIC_INTR_ISRC(sc, i);
		if (intr_isrc_dispatch(isrc, curthread->td_intr_frame) != 0) {
			device_printf(sc->pic_dev, "Stray interrupt %u detected\n", i);
			pic_irq_mask(sc, i);
			continue;
		}
	}

	KASSERT(i == 0, ("all interrupts handled"));
	td->td_intr_nesting_level++;

	return (FILTER_HANDLED);
}

static int
jz4780_pic_map_intr(device_t dev, struct intr_map_data *data,
        struct intr_irqsrc **isrcp)
{
#ifdef FDT
	struct jz4780_pic_softc *sc;
	struct intr_map_data_fdt *daf;

	sc = device_get_softc(dev);
	daf = (struct intr_map_data_fdt *)data;

	if (data == NULL || data->type != INTR_MAP_DATA_FDT ||
	    daf->ncells != 1 || daf->cells[0] >= sc->nirqs)
		return (EINVAL);

	*isrcp = PIC_INTR_ISRC(sc, daf->cells[0]);
	return (0);
#else
	return (EINVAL);
#endif
}

static void
jz4780_pic_enable_intr(device_t dev, struct intr_irqsrc *isrc)
{
	struct jz4780_pic_isrc *pic_isrc;

	pic_isrc = (struct jz4780_pic_isrc *)isrc;
	pic_irq_unmask(device_get_softc(dev), pic_isrc->irq);
}

static void
jz4780_pic_disable_intr(device_t dev, struct intr_irqsrc *isrc)
{
	struct jz4780_pic_isrc *pic_isrc;

	pic_isrc = (struct jz4780_pic_isrc *)isrc;
	pic_irq_mask(device_get_softc(dev), pic_isrc->irq);
}

static void
jz4780_pic_pre_ithread(device_t dev, struct intr_irqsrc *isrc)
{

	jz4780_pic_disable_intr(dev, isrc);
}

static void
jz4780_pic_post_ithread(device_t dev, struct intr_irqsrc *isrc)
{

	jz4780_pic_enable_intr(dev, isrc);
}

static device_method_t jz4780_pic_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		jz4780_pic_probe),
	DEVMETHOD(device_attach,	jz4780_pic_attach),
	/* Interrupt controller interface */
	DEVMETHOD(pic_enable_intr,	jz4780_pic_enable_intr),
	DEVMETHOD(pic_disable_intr,	jz4780_pic_disable_intr),
	DEVMETHOD(pic_map_intr,		jz4780_pic_map_intr),
	DEVMETHOD(pic_post_ithread,	jz4780_pic_post_ithread),
	DEVMETHOD(pic_pre_ithread,	jz4780_pic_pre_ithread),
	{ 0, 0 }
};

static driver_t jz4780_pic_driver = {
	"intc",
	jz4780_pic_methods,
	sizeof(struct jz4780_pic_softc),
};

static devclass_t jz4780_pic_devclass;

EARLY_DRIVER_MODULE(intc, ofwbus, jz4780_pic_driver, jz4780_pic_devclass, 0, 0,
    BUS_PASS_INTERRUPT + BUS_PASS_ORDER_MIDDLE);
