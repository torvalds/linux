/*-
 * Copyright (c) 2015 Stanislav Galabov
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

#include "pic_if.h"

#define	MTK_NIRQS	32

#define MTK_IRQ0STAT	0x0000
#define MTK_IRQ1STAT	0x0004
#define MTK_INTTYPE	0x0020
#define MTK_INTRAW	0x0030
#define MTK_INTENA	0x0034
#define MTK_INTDIS	0x0038

static int mtk_pic_intr(void *);

struct mtk_pic_irqsrc {
	struct intr_irqsrc	isrc;
	u_int			irq;
};

struct mtk_pic_softc {
	device_t		pic_dev;
	void *                  pic_intrhand;
	struct resource *       pic_res[2];
	struct mtk_pic_irqsrc	pic_irqs[MTK_NIRQS];
	struct mtx		mutex;
	uint32_t		nirqs;
};

#define PIC_INTR_ISRC(sc, irq)	(&(sc)->pic_irqs[(irq)].isrc)

static struct resource_spec mtk_pic_spec[] = {
	{ SYS_RES_MEMORY,	0,	RF_ACTIVE },	/* Registers */
	{ SYS_RES_IRQ,		0,	RF_ACTIVE },	/* Parent interrupt 1 */
//	{ SYS_RES_IRQ,		1,	RF_ACTIVE },	/* Parent interrupt 2 */
	{ -1, 0 }
};

static struct ofw_compat_data compat_data[] = {
	{ "ralink,rt2880-intc",  1 },
	{ "ralink,rt3050-intc",  1 },
	{ "ralink,rt3352-intc",  1 },
	{ "ralink,rt3883-intc",  1 },
	{ "ralink,rt5350-intc",  1 },
	{ "ralink,mt7620a-intc", 1 },
	{ NULL,				0 }
};

#define	READ4(_sc, _reg)	bus_read_4((_sc)->pic_res[0], _reg)
#define	WRITE4(_sc, _reg, _val) bus_write_4((_sc)->pic_res[0], _reg, _val)

static int
mtk_pic_probe(device_t dev)
{

	if (!ofw_bus_status_okay(dev))
		return (ENXIO);

	if (ofw_bus_search_compatible(dev, compat_data)->ocd_data == 0)
		return (ENXIO);

	device_set_desc(dev, "MTK Interrupt Controller (v2)");
	return (BUS_PROBE_DEFAULT);
}

static inline void
pic_irq_unmask(struct mtk_pic_softc *sc, u_int irq)
{

	WRITE4(sc, MTK_INTENA, (1u << (irq)));
}

static inline void
pic_irq_mask(struct mtk_pic_softc *sc, u_int irq)
{

	WRITE4(sc, MTK_INTDIS, (1u << (irq)));
}

static inline intptr_t
pic_xref(device_t dev)
{
	return (OF_xref_from_node(ofw_bus_get_node(dev)));
}

static int
mtk_pic_register_isrcs(struct mtk_pic_softc *sc)
{
	int error;
	uint32_t irq;
	struct intr_irqsrc *isrc;
	const char *name;

	name = device_get_nameunit(sc->pic_dev);
	for (irq = 0; irq < sc->nirqs; irq++) {
		sc->pic_irqs[irq].irq = irq;
		isrc = PIC_INTR_ISRC(sc, irq);
		error = intr_isrc_register(isrc, sc->pic_dev, 0, "%s", name);
		if (error != 0) {
			/* XXX call intr_isrc_deregister */
			device_printf(sc->pic_dev, "%s failed", __func__);
			return (error);
		}
	}

	return (0);
}

static int
mtk_pic_attach(device_t dev)
{
	struct mtk_pic_softc *sc;
	intptr_t xref = pic_xref(dev);

	sc = device_get_softc(dev);

	if (bus_alloc_resources(dev, mtk_pic_spec, sc->pic_res)) {
		device_printf(dev, "could not allocate resources\n");
		return (ENXIO);
	}

	sc->pic_dev = dev;

	/* Initialize mutex */
	mtx_init(&sc->mutex, "PIC lock", "", MTX_SPIN);

	/* Set the number of interrupts */
	sc->nirqs = nitems(sc->pic_irqs);

	/* Mask all interrupts */
	WRITE4(sc, MTK_INTDIS, 0x7FFFFFFF);

	/* But enable interrupt generation/masking */
	WRITE4(sc, MTK_INTENA, 0x80000000);

	/* Set all interrupts to type 0 */
	WRITE4(sc, MTK_INTTYPE, 0x00000000);

	/* Register the interrupts */
	if (mtk_pic_register_isrcs(sc) != 0) {
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
	    mtk_pic_intr, NULL, sc, &sc->pic_intrhand)) {
		device_printf(dev, "could not setup irq handler\n");
		intr_pic_deregister(dev, xref);
		goto cleanup;
	}
	return (0);

cleanup:
	bus_release_resources(dev, mtk_pic_spec, sc->pic_res);
	return(ENXIO);
}

static int
mtk_pic_intr(void *arg)
{
	struct mtk_pic_softc *sc = arg;
	struct thread *td;
	uint32_t i, intr;

	td = curthread;
	/* Workaround: do not inflate intr nesting level */
	td->td_intr_nesting_level--;

#ifdef _notyet_
	intr = READ4(sc, MTK_IRQ1STAT);
	while ((i = fls(intr)) != 0) {
		i--;
		intr &= ~(1u << i);

		if (intr_isrc_dispatch(PIC_INTR_ISRC(sc, i),
		    curthread->td_intr_frame) != 0) {
			device_printf(sc->pic_dev,
			    "Stray interrupt %u detected\n", i);
			pic_irq_mask(sc, i);
			continue;
		}
	}

	KASSERT(i == 0, ("all interrupts handled"));
#endif

	intr = READ4(sc, MTK_IRQ0STAT);

	while ((i = fls(intr)) != 0) {
		i--;
		intr &= ~(1u << i);

		if (intr_isrc_dispatch(PIC_INTR_ISRC(sc, i),
		    curthread->td_intr_frame) != 0) {
			device_printf(sc->pic_dev,
				"Stray interrupt %u detected\n", i);
			pic_irq_mask(sc, i);
			continue;
		}
	}

	KASSERT(i == 0, ("all interrupts handled"));

	td->td_intr_nesting_level++;

	return (FILTER_HANDLED);
}

static int
mtk_pic_map_intr(device_t dev, struct intr_map_data *data,
    struct intr_irqsrc **isrcp)
{
#ifdef FDT
	struct intr_map_data_fdt *daf;
	struct mtk_pic_softc *sc;

	if (data->type != INTR_MAP_DATA_FDT)
		return (ENOTSUP);

	sc = device_get_softc(dev);
	daf = (struct intr_map_data_fdt *)data;

	if (daf->ncells != 1 || daf->cells[0] >= sc->nirqs)
		return (EINVAL);

	*isrcp = PIC_INTR_ISRC(sc, daf->cells[0]);
	return (0);
#else
	return (ENOTSUP);
#endif
}

static void
mtk_pic_enable_intr(device_t dev, struct intr_irqsrc *isrc)
{
	u_int irq;

	irq = ((struct mtk_pic_irqsrc *)isrc)->irq;
	pic_irq_unmask(device_get_softc(dev), irq);
}

static void
mtk_pic_disable_intr(device_t dev, struct intr_irqsrc *isrc)
{
	u_int irq;

	irq = ((struct mtk_pic_irqsrc *)isrc)->irq;
	pic_irq_mask(device_get_softc(dev), irq);
}

static void
mtk_pic_pre_ithread(device_t dev, struct intr_irqsrc *isrc)
{

	mtk_pic_disable_intr(dev, isrc);
}

static void
mtk_pic_post_ithread(device_t dev, struct intr_irqsrc *isrc)
{

	mtk_pic_enable_intr(dev, isrc);
}

static void
mtk_pic_post_filter(device_t dev, struct intr_irqsrc *isrc)
{
}

static device_method_t mtk_pic_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		mtk_pic_probe),
	DEVMETHOD(device_attach,	mtk_pic_attach),
	/* Interrupt controller interface */
	DEVMETHOD(pic_disable_intr,	mtk_pic_disable_intr),
	DEVMETHOD(pic_enable_intr,	mtk_pic_enable_intr),
	DEVMETHOD(pic_map_intr,		mtk_pic_map_intr),
	DEVMETHOD(pic_post_filter,	mtk_pic_post_filter),
	DEVMETHOD(pic_post_ithread,	mtk_pic_post_ithread),
	DEVMETHOD(pic_pre_ithread,	mtk_pic_pre_ithread),
	{ 0, 0 }
};

static driver_t mtk_pic_driver = {
	"intc",
	mtk_pic_methods,
	sizeof(struct mtk_pic_softc),
};

static devclass_t mtk_pic_devclass;

EARLY_DRIVER_MODULE(intc_v1, simplebus, mtk_pic_driver, mtk_pic_devclass, 0, 0,
    BUS_PASS_INTERRUPT + BUS_PASS_ORDER_MIDDLE);
