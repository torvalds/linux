/*-
 * Copyright (c) 2016 Stanislav Galabov
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

#define	MTK_NIRQS	64	/* We'll only use 64 for now */

#define MTK_INTPOL		0x0100
#define MTK_INTTRIG		0x0180
#define MTK_INTDIS		0x0300
#define MTK_INTENA		0x0380
#define MTK_INTMASK		0x0400
#define MTK_INTSTAT		0x0480
#define MTK_MAPPIN(_i)		(0x0500 + (4 * (_i)))
#define MTK_MAPVPE(_i, _v)	(0x2000 + (32 * (_i)) + (((_v) / 32) * 4))

#define MTK_INTPOL_POS		1
#define MTK_INTPOL_NEG		0
#define MTK_INTTRIG_EDGE	1
#define MTK_INTTRIG_LEVEL	0
#define MTK_PIN_BITS(_i)	((1 << 31) | (_i))
#define MTK_VPE_BITS(_v)	(1 << ((_v) % 32))

static int mtk_gic_intr(void *);

struct mtk_gic_irqsrc {
	struct intr_irqsrc	isrc;
	u_int			irq;
};

struct mtk_gic_softc {
	device_t		gic_dev;
	void *                  gic_intrhand;
	struct resource *       gic_res[2];
	struct mtk_gic_irqsrc	gic_irqs[MTK_NIRQS];
	struct mtx		mutex;
	uint32_t		nirqs;
};

#define GIC_INTR_ISRC(sc, irq)	(&(sc)->gic_irqs[(irq)].isrc)

static struct resource_spec mtk_gic_spec[] = {
	{ SYS_RES_MEMORY,	0,	RF_ACTIVE },	/* Registers */
	{ -1, 0 }
};

static struct ofw_compat_data compat_data[] = {
	{ "mti,gic",	1 },
	{ NULL,		0 }
};

#define READ4(_sc, _reg)	bus_read_4((_sc)->gic_res[0], (_reg))
#define WRITE4(_sc, _reg, _val)	bus_write_4((_sc)->gic_res[0], (_reg), (_val))

static int
mtk_gic_probe(device_t dev)
{

	if (!ofw_bus_status_okay(dev))
		return (ENXIO);

	if (ofw_bus_search_compatible(dev, compat_data)->ocd_data == 0)
		return (ENXIO);

	device_set_desc(dev, "MTK Interrupt Controller (GIC)");
	return (BUS_PROBE_DEFAULT);
}

static inline void
gic_irq_unmask(struct mtk_gic_softc *sc, u_int irq)
{

	WRITE4(sc, MTK_INTENA, (1u << (irq)));
}

static inline void
gic_irq_mask(struct mtk_gic_softc *sc, u_int irq)
{

	WRITE4(sc, MTK_INTDIS, (1u << (irq)));
}

static inline intptr_t
gic_xref(device_t dev)
{

	return (OF_xref_from_node(ofw_bus_get_node(dev)));
}

static int
mtk_gic_register_isrcs(struct mtk_gic_softc *sc)
{
	int error;
	uint32_t irq;
	struct intr_irqsrc *isrc;
	const char *name;

	name = device_get_nameunit(sc->gic_dev);
	for (irq = 0; irq < sc->nirqs; irq++) {
		sc->gic_irqs[irq].irq = irq;
		isrc = GIC_INTR_ISRC(sc, irq);
		error = intr_isrc_register(isrc, sc->gic_dev, 0, "%s", name);
		if (error != 0) {
			/* XXX call intr_isrc_deregister */
			device_printf(sc->gic_dev, "%s failed", __func__);
			return (error);
		}
	}

	return (0);
}

static int
mtk_gic_attach(device_t dev)
{
	struct mtk_gic_softc *sc;
	intptr_t xref = gic_xref(dev);
	int i;

	sc = device_get_softc(dev);

	if (bus_alloc_resources(dev, mtk_gic_spec, sc->gic_res)) {
		device_printf(dev, "could not allocate resources\n");
		return (ENXIO);
	}

	sc->gic_dev = dev;

	/* Initialize mutex */
	mtx_init(&sc->mutex, "PIC lock", "", MTX_SPIN);

	/* Set the number of interrupts */
	sc->nirqs = nitems(sc->gic_irqs);

	/* Mask all interrupts */
	WRITE4(sc, MTK_INTDIS, 0xFFFFFFFF);

	/* All interrupts are of type level */
	WRITE4(sc, MTK_INTTRIG, 0x00000000);

	/* All interrupts are of positive polarity */
	WRITE4(sc, MTK_INTPOL, 0xFFFFFFFF);

	/*
	 * Route all interrupts to pin 0 on VPE 0;
	 */
	for (i = 0; i < 32; i++) {
		WRITE4(sc, MTK_MAPPIN(i), MTK_PIN_BITS(0));
		WRITE4(sc, MTK_MAPVPE(i, 0), MTK_VPE_BITS(0));
	}

	/* Register the interrupts */
	if (mtk_gic_register_isrcs(sc) != 0) {
		device_printf(dev, "could not register GIC ISRCs\n");
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

	cpu_establish_hardintr("gic", mtk_gic_intr, NULL, sc, 0, INTR_TYPE_CLK,
	    NULL);

	return (0);

cleanup:
	bus_release_resources(dev, mtk_gic_spec, sc->gic_res);
	return(ENXIO);
}

static int
mtk_gic_intr(void *arg)
{
	struct mtk_gic_softc *sc = arg;
	struct thread *td;
	uint32_t i, intr;

	td = curthread;
	/* Workaround: do not inflate intr nesting level */
	td->td_intr_nesting_level--;

	intr = READ4(sc, MTK_INTSTAT) & READ4(sc, MTK_INTMASK);
	while ((i = fls(intr)) != 0) {
		i--;
		intr &= ~(1u << i);

		if (intr_isrc_dispatch(GIC_INTR_ISRC(sc, i),
		    curthread->td_intr_frame) != 0) {
			device_printf(sc->gic_dev,
				"Stray interrupt %u detected\n", i);
			gic_irq_mask(sc, i);
			continue;
		}
	}

	KASSERT(i == 0, ("all interrupts handled"));

	td->td_intr_nesting_level++;

	return (FILTER_HANDLED);
}

static int
mtk_gic_map_intr(device_t dev, struct intr_map_data *data,
    struct intr_irqsrc **isrcp)
{
#ifdef FDT
	struct intr_map_data_fdt *daf;
	struct mtk_gic_softc *sc;

	if (data->type != INTR_MAP_DATA_FDT)
		return (ENOTSUP);

	sc = device_get_softc(dev);
	daf = (struct intr_map_data_fdt *)data;

	if (daf->ncells != 3 || daf->cells[1] >= sc->nirqs)
		return (EINVAL);

	*isrcp = GIC_INTR_ISRC(sc, daf->cells[1]);
	return (0);
#else
	return (ENOTSUP);
#endif
}

static void
mtk_gic_enable_intr(device_t dev, struct intr_irqsrc *isrc)
{
	u_int irq;

	irq = ((struct mtk_gic_irqsrc *)isrc)->irq;
	gic_irq_unmask(device_get_softc(dev), irq);
}

static void
mtk_gic_disable_intr(device_t dev, struct intr_irqsrc *isrc)
{
	u_int irq;

	irq = ((struct mtk_gic_irqsrc *)isrc)->irq;
	gic_irq_mask(device_get_softc(dev), irq);
}

static void
mtk_gic_pre_ithread(device_t dev, struct intr_irqsrc *isrc)
{

	mtk_gic_disable_intr(dev, isrc);
}

static void
mtk_gic_post_ithread(device_t dev, struct intr_irqsrc *isrc)
{

	mtk_gic_enable_intr(dev, isrc);
}

static void
mtk_gic_post_filter(device_t dev, struct intr_irqsrc *isrc)
{
}

#ifdef SMP
static int
mtk_gic_bind(device_t dev, struct intr_irqsrc *isrc)
{
	return (EOPNOTSUPP);
}

static void
mtk_gic_init_secondary(device_t dev)
{
}

static void
mtk_gic_ipi_send(device_t dev, struct intr_irqsrc *isrc, cpuset_t cpus)
{
}
#endif

static device_method_t mtk_gic_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		mtk_gic_probe),
	DEVMETHOD(device_attach,	mtk_gic_attach),
	/* Interrupt controller interface */
	DEVMETHOD(pic_disable_intr,	mtk_gic_disable_intr),
	DEVMETHOD(pic_enable_intr,	mtk_gic_enable_intr),
	DEVMETHOD(pic_map_intr,		mtk_gic_map_intr),
	DEVMETHOD(pic_post_filter,	mtk_gic_post_filter),
	DEVMETHOD(pic_post_ithread,	mtk_gic_post_ithread),
	DEVMETHOD(pic_pre_ithread,	mtk_gic_pre_ithread),
#ifdef SMP
	DEVMETHOD(pic_bind,		mtk_gic_bind),
	DEVMETHOD(pic_init_secondary,	mtk_gic_init_secondary),
	DEVMETHOD(pic_ipi_send,		mtk_gic_ipi_send),
#endif
	{ 0, 0 }
};

static driver_t mtk_gic_driver = {
	"intc",
	mtk_gic_methods,
	sizeof(struct mtk_gic_softc),
};

static devclass_t mtk_gic_devclass;

EARLY_DRIVER_MODULE(intc_gic, simplebus, mtk_gic_driver, mtk_gic_devclass, 0, 0,
    BUS_PASS_INTERRUPT + BUS_PASS_ORDER_MIDDLE);
