/*-
 * Copyright (c) 2012 Ganbold Tsagaankhuu <ganbold@freebsd.org>
 * Copyright (c) 2016 Emmanuel Vadot <manu@freebsd.org>
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

#include "opt_platform.h"

#include <sys/types.h>
#include <sys/bus.h>
#include <sys/cpuset.h>
#include <sys/kernel.h>
#include <sys/ktr.h>
#include <sys/module.h>
#include <sys/mutex.h>
#include <sys/param.h>
#include <sys/pcpu.h>
#include <sys/proc.h>
#include <sys/rman.h>
#include <sys/smp.h>
#include <sys/systm.h>
#include <sys/sched.h>
#include <machine/bus.h>
#include <machine/intr.h>

#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>

#include "pic_if.h"

/**
 * Interrupt controller registers
 *
 */
#define	SW_INT_VECTOR_REG		0x00
#define	SW_INT_BASE_ADR_REG		0x04
#define	SW_INT_PROTECTION_REG		0x08
#define	SW_INT_NMI_CTRL_REG		0x0c

#define	SW_INT_IRQ_PENDING_REG0		0x10
#define	SW_INT_IRQ_PENDING_REG1		0x14
#define	SW_INT_IRQ_PENDING_REG2		0x18

#define	SW_INT_FIQ_PENDING_REG0		0x20
#define	SW_INT_FIQ_PENDING_REG1		0x24
#define	SW_INT_FIQ_PENDING_REG2		0x28

#define	SW_INT_SELECT_REG0		0x30
#define	SW_INT_SELECT_REG1		0x34
#define	SW_INT_SELECT_REG2		0x38

#define	SW_INT_ENABLE_REG0		0x40
#define	SW_INT_ENABLE_REG1		0x44
#define	SW_INT_ENABLE_REG2		0x48

#define	SW_INT_MASK_REG0		0x50
#define	SW_INT_MASK_REG1		0x54
#define	SW_INT_MASK_REG2		0x58

#define	SW_INT_IRQNO_ENMI		0

#define	A10_INTR_MAX_NIRQS		81

#define	SW_INT_IRQ_PENDING_REG(_b)	(0x10 + ((_b) * 4))
#define	SW_INT_FIQ_PENDING_REG(_b)	(0x20 + ((_b) * 4))
#define	SW_INT_SELECT_REG(_b)		(0x30 + ((_b) * 4))
#define	SW_INT_ENABLE_REG(_b)		(0x40 + ((_b) * 4))
#define	SW_INT_MASK_REG(_b)		(0x50 + ((_b) * 4))

struct a10_intr_irqsrc {
	struct intr_irqsrc	isrc;
	u_int			irq;
};

struct a10_aintc_softc {
	device_t		sc_dev;
	struct resource *	aintc_res;
	bus_space_tag_t		aintc_bst;
	bus_space_handle_t	aintc_bsh;
	struct mtx		mtx;
	struct a10_intr_irqsrc	isrcs[A10_INTR_MAX_NIRQS];
};

#define	aintc_read_4(sc, reg)						\
	bus_space_read_4(sc->aintc_bst, sc->aintc_bsh, reg)
#define	aintc_write_4(sc, reg, val)					\
	bus_space_write_4(sc->aintc_bst, sc->aintc_bsh, reg, val)

static __inline void
a10_intr_eoi(struct a10_aintc_softc *sc, u_int irq)
{

	if (irq != SW_INT_IRQNO_ENMI)
		return;
	mtx_lock_spin(&sc->mtx);
	aintc_write_4(sc, SW_INT_IRQ_PENDING_REG(0),
	    (1 << SW_INT_IRQNO_ENMI));
	mtx_unlock_spin(&sc->mtx);
}

static void
a10_intr_unmask(struct a10_aintc_softc *sc, u_int irq)
{
	uint32_t bit, block, value;

	bit = (irq % 32);
	block = (irq / 32);

	mtx_lock_spin(&sc->mtx);
	value = aintc_read_4(sc, SW_INT_ENABLE_REG(block));
	value |= (1 << bit);
	aintc_write_4(sc, SW_INT_ENABLE_REG(block), value);

	value = aintc_read_4(sc, SW_INT_MASK_REG(block));
	value &= ~(1 << bit);
	aintc_write_4(sc, SW_INT_MASK_REG(block), value);
	mtx_unlock_spin(&sc->mtx);
}

static void
a10_intr_mask(struct a10_aintc_softc *sc, u_int irq)
{
	uint32_t bit, block, value;

	bit = (irq % 32);
	block = (irq / 32);

	mtx_lock_spin(&sc->mtx);
	value = aintc_read_4(sc, SW_INT_ENABLE_REG(block));
	value &= ~(1 << bit);
	aintc_write_4(sc, SW_INT_ENABLE_REG(block), value);

	value = aintc_read_4(sc, SW_INT_MASK_REG(block));
	value |= (1 << bit);
	aintc_write_4(sc, SW_INT_MASK_REG(block), value);
	mtx_unlock_spin(&sc->mtx);
}

static int
a10_pending_irq(struct a10_aintc_softc *sc)
{
	uint32_t value;
	int i, b;

	for (i = 0; i < 3; i++) {
		value = aintc_read_4(sc, SW_INT_IRQ_PENDING_REG(i));
		if (value == 0)
			continue;
		for (b = 0; b < 32; b++)
			if (value & (1 << b)) {
				return (i * 32 + b);
			}
	}

	return (-1);
}

static int
a10_intr(void *arg)
{
	struct a10_aintc_softc *sc = arg;
	u_int irq;

	irq = a10_pending_irq(sc);
	if (irq == -1 || irq > A10_INTR_MAX_NIRQS) {
		device_printf(sc->sc_dev, "Spurious interrupt %d\n", irq);
		return (FILTER_HANDLED);
	}

	while (irq != -1) {
		if (irq > A10_INTR_MAX_NIRQS) {
			device_printf(sc->sc_dev, "Spurious interrupt %d\n",
			    irq);
			return (FILTER_HANDLED);
		}
		if (intr_isrc_dispatch(&sc->isrcs[irq].isrc,
		    curthread->td_intr_frame) != 0) {
			a10_intr_mask(sc, irq);
			a10_intr_eoi(sc, irq);
			device_printf(sc->sc_dev,
			    "Stray interrupt %d disabled\n", irq);
		}

		arm_irq_memory_barrier(irq);
		irq = a10_pending_irq(sc);
	}

	return (FILTER_HANDLED);
}

static int
a10_intr_pic_attach(struct a10_aintc_softc *sc)
{
	struct intr_pic *pic;
	int error;
	uint32_t irq;
	const char *name;
	intptr_t xref;

	name = device_get_nameunit(sc->sc_dev);
	for (irq = 0; irq < A10_INTR_MAX_NIRQS; irq++) {
		sc->isrcs[irq].irq = irq;

		error = intr_isrc_register(&sc->isrcs[irq].isrc,
		    sc->sc_dev, 0, "%s,%u", name, irq);
		if (error != 0)
			return (error);
	}

	xref = OF_xref_from_node(ofw_bus_get_node(sc->sc_dev));
	pic = intr_pic_register(sc->sc_dev, xref);
	if (pic == NULL)
		return (ENXIO);

	return (intr_pic_claim_root(sc->sc_dev, xref, a10_intr, sc, 0));
}

static void
a10_intr_enable_intr(device_t dev, struct intr_irqsrc *isrc)
{
	struct a10_aintc_softc *sc;
	u_int irq = ((struct a10_intr_irqsrc *)isrc)->irq;

	sc = device_get_softc(dev);
	arm_irq_memory_barrier(irq);
	a10_intr_unmask(sc, irq);
}

static void
a10_intr_disable_intr(device_t dev, struct intr_irqsrc *isrc)
{
	struct a10_aintc_softc *sc;
	u_int irq = ((struct a10_intr_irqsrc *)isrc)->irq;

	sc = device_get_softc(dev);
	a10_intr_mask(sc, irq);
}

static int
a10_intr_map_intr(device_t dev, struct intr_map_data *data,
    struct intr_irqsrc **isrcp)
{
	struct intr_map_data_fdt *daf;
	struct a10_aintc_softc *sc;

	if (data->type != INTR_MAP_DATA_FDT)
		return (ENOTSUP);

	daf = (struct intr_map_data_fdt *)data;
	if (daf->ncells != 1 || daf->cells[0] >= A10_INTR_MAX_NIRQS)
		return (EINVAL);

	sc = device_get_softc(dev);
	*isrcp = &sc->isrcs[daf->cells[0]].isrc;
	return (0);
}

static void
a10_intr_pre_ithread(device_t dev, struct intr_irqsrc *isrc)
{
	struct a10_aintc_softc *sc = device_get_softc(dev);
	u_int irq = ((struct a10_intr_irqsrc *)isrc)->irq;

	a10_intr_mask(sc, irq);
	a10_intr_eoi(sc, irq);
}

static void
a10_intr_post_ithread(device_t dev, struct intr_irqsrc *isrc)
{

	a10_intr_enable_intr(dev, isrc);
}

static void
a10_intr_post_filter(device_t dev, struct intr_irqsrc *isrc)
{
	struct a10_aintc_softc *sc = device_get_softc(dev);
	u_int irq = ((struct a10_intr_irqsrc *)isrc)->irq;

	a10_intr_eoi(sc, irq);
}

static int
a10_aintc_probe(device_t dev)
{

	if (!ofw_bus_status_okay(dev))
		return (ENXIO);

	if (!ofw_bus_is_compatible(dev, "allwinner,sun4i-a10-ic"))
		return (ENXIO);
	device_set_desc(dev, "A10 AINTC Interrupt Controller");
	return (BUS_PROBE_DEFAULT);
}

static int
a10_aintc_attach(device_t dev)
{
	struct a10_aintc_softc *sc = device_get_softc(dev);
	int rid = 0;
	int i;
	sc->sc_dev = dev;

	sc->aintc_res = bus_alloc_resource_any(dev, SYS_RES_MEMORY,
	    &rid, RF_ACTIVE);
	if (!sc->aintc_res) {
		device_printf(dev, "could not allocate resource\n");
		goto error;
	}

	sc->aintc_bst = rman_get_bustag(sc->aintc_res);
	sc->aintc_bsh = rman_get_bushandle(sc->aintc_res);

	mtx_init(&sc->mtx, "A10 AINTC lock", "", MTX_SPIN);

	/* Disable & clear all interrupts */
	for (i = 0; i < 3; i++) {
		aintc_write_4(sc, SW_INT_ENABLE_REG(i), 0);
		aintc_write_4(sc, SW_INT_MASK_REG(i), 0xffffffff);
	}
	/* enable protection mode*/
	aintc_write_4(sc, SW_INT_PROTECTION_REG, 0x01);

	/* config the external interrupt source type*/
	aintc_write_4(sc, SW_INT_NMI_CTRL_REG, 0x00);

	if (a10_intr_pic_attach(sc) != 0) {
		device_printf(dev, "could not attach PIC\n");
		return (ENXIO);
	}

	return (0);

error:
	bus_release_resource(dev, SYS_RES_MEMORY, rid,
	    sc->aintc_res);
	return (ENXIO);
}

static device_method_t a10_aintc_methods[] = {
	DEVMETHOD(device_probe,		a10_aintc_probe),
	DEVMETHOD(device_attach,	a10_aintc_attach),

	/* Interrupt controller interface */
	DEVMETHOD(pic_disable_intr,	a10_intr_disable_intr),
	DEVMETHOD(pic_enable_intr,	a10_intr_enable_intr),
	DEVMETHOD(pic_map_intr,		a10_intr_map_intr),
	DEVMETHOD(pic_post_filter,	a10_intr_post_filter),
	DEVMETHOD(pic_post_ithread,	a10_intr_post_ithread),
	DEVMETHOD(pic_pre_ithread,	a10_intr_pre_ithread),

	{ 0, 0 }
};

static driver_t a10_aintc_driver = {
	"aintc",
	a10_aintc_methods,
	sizeof(struct a10_aintc_softc),
};

static devclass_t a10_aintc_devclass;

EARLY_DRIVER_MODULE(aintc, simplebus, a10_aintc_driver, a10_aintc_devclass, 0, 0,
    BUS_PASS_INTERRUPT + BUS_PASS_ORDER_FIRST);
