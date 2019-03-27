/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2017 Ruslan Bukin <br@bsdpad.com>
 * Copyright (c) 2013 SRI International
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
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include "opt_platform.h"

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/module.h>
#include <sys/mutex.h>
#include <sys/proc.h>
#include <sys/systm.h>
#include <sys/bus.h>

#include <machine/bus.h>
#include <machine/intr.h>

#ifdef SMP
#include <mips/beri/beri_mp.h>
#endif

#include <dev/fdt/fdt_common.h>
#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>

#include "pic_if.h"

#define	BP_NUM_HARD_IRQS	5
#define	BP_NUM_IRQS		32
/* We use hard irqs 15-31 as soft */
#define	BP_FIRST_SOFT		16

#define	BP_CFG_IRQ_S		0
#define	BP_CFG_IRQ_M		(0xf << BP_CFG_IRQ_S)
#define	BP_CFG_TID_S		8
#define	BP_CFG_TID_M		(0x7FFFFF << BP_CFG_TID_S)
#define	BP_CFG_ENABLE		(1 << 31)

enum {
	BP_CFG,
	BP_IP_READ,
	BP_IP_SET,
	BP_IP_CLEAR
};

struct beripic_softc;

struct beri_pic_isrc {
	struct intr_irqsrc	isrc;
	u_int			irq;
	uint32_t		mips_hard_irq;
};

struct hirq {
	uint32_t		irq;
	struct beripic_softc	*sc;
};

struct beripic_softc {
	device_t		dev;
	uint32_t		nirqs;
	struct beri_pic_isrc	irqs[BP_NUM_IRQS];
	struct resource		*res[4 + BP_NUM_HARD_IRQS];
	void			*ih[BP_NUM_HARD_IRQS];
	struct hirq		hirq[BP_NUM_HARD_IRQS];
	uint8_t			mips_hard_irq_idx;
};

static struct resource_spec beri_pic_spec[] = {
	{ SYS_RES_MEMORY,	0,	RF_ACTIVE },
	{ SYS_RES_MEMORY,	1,	RF_ACTIVE },
	{ SYS_RES_MEMORY,	2,	RF_ACTIVE },
	{ SYS_RES_MEMORY,	3,	RF_ACTIVE },
	{ SYS_RES_IRQ,		0,	RF_ACTIVE },
	{ SYS_RES_IRQ,		1,	RF_ACTIVE },
	{ SYS_RES_IRQ,		2,	RF_ACTIVE },
	{ SYS_RES_IRQ,		3,	RF_ACTIVE },
	{ SYS_RES_IRQ,		4,	RF_ACTIVE },
	{ -1, 0 }
};

static int
beri_pic_intr(void *arg)
{
	struct beripic_softc *sc;
	struct intr_irqsrc *isrc;
	struct hirq *h;
	uint64_t intr;
	uint64_t reg;
	int i;

	h = arg;
	sc = h->sc;

	intr = bus_read_8(sc->res[BP_IP_READ], 0);
	while ((i = fls(intr)) != 0) {
		i--;
		intr &= ~(1u << i);

		isrc = &sc->irqs[i].isrc;

		reg = bus_read_8(sc->res[BP_CFG], i * 8);
		if ((reg & BP_CFG_IRQ_M) != h->irq) {
			continue;
		}
		if ((reg & (BP_CFG_ENABLE)) == 0) {
			continue;
		}

		if (intr_isrc_dispatch(isrc, curthread->td_intr_frame) != 0) {
			device_printf(sc->dev, "Stray interrupt %u detected\n", i);
		}

		bus_write_8(sc->res[BP_IP_CLEAR], 0, (1 << i));
	}

	return (FILTER_HANDLED);
}

static int
beripic_probe(device_t dev)
{

	if (!ofw_bus_status_okay(dev))
		return (ENXIO);

	if (!ofw_bus_is_compatible(dev, "sri-cambridge,beri-pic"))
		return (ENXIO);
		
	device_set_desc(dev, "BERI Programmable Interrupt Controller");

	return (BUS_PROBE_DEFAULT);
}

static int
beripic_attach(device_t dev)
{
	struct beripic_softc *sc;
	struct beri_pic_isrc *pic_isrc;
	const char *name;
	struct intr_irqsrc *isrc;
	intptr_t xref;
	uint32_t unit;
	int err;
	int i;

	sc = device_get_softc(dev);
	sc->dev = dev;

	if (bus_alloc_resources(dev, beri_pic_spec, sc->res)) {
		device_printf(dev, "could not allocate resources\n");
		return (ENXIO);
	}

	xref = OF_xref_from_node(ofw_bus_get_node(dev));
	name = device_get_nameunit(dev);
	unit = device_get_unit(dev);
	sc->nirqs = BP_NUM_IRQS;

	for (i = 0; i < sc->nirqs; i++) {
		sc->irqs[i].irq = i;
		isrc = &sc->irqs[i].isrc;

		/* Assign mips hard irq number. */
		pic_isrc = (struct beri_pic_isrc *)isrc;
		pic_isrc->mips_hard_irq = sc->mips_hard_irq_idx++;
		/* Last IRQ is used for IPIs. */
		if (sc->mips_hard_irq_idx >= (BP_NUM_HARD_IRQS - 1)) {
			sc->mips_hard_irq_idx = 0;
		}

		err = intr_isrc_register(isrc, sc->dev,
		    0, "pic%d,%d", unit, i);
		bus_write_8(sc->res[BP_CFG], i * 8, 0);
	}

	/*
	 * Now, when everything is initialized, it's right time to
	 * register interrupt controller to interrupt framefork.
	 */
	if (intr_pic_register(dev, xref) == NULL) {
		device_printf(dev, "could not register PIC\n");
		return (ENXIO);
	}

	/* Last IRQ is used for IPIs. */
	for (i = 0; i < (BP_NUM_HARD_IRQS - 1); i++) {
		sc->hirq[i].sc = sc;
		sc->hirq[i].irq = i;
		if (bus_setup_intr(dev, sc->res[4+i], INTR_TYPE_CLK,
		    beri_pic_intr, NULL, &sc->hirq[i], sc->ih[i])) {
			device_printf(dev, "could not setup irq handler\n");
			intr_pic_deregister(dev, xref);
			return (ENXIO);
		}
	}

	return (0);
}

static void
beri_pic_enable_intr(device_t dev, struct intr_irqsrc *isrc)
{
	struct beri_pic_isrc *pic_isrc;
	struct beripic_softc *sc;
	uint64_t reg;

	sc = device_get_softc(dev);
	pic_isrc = (struct beri_pic_isrc *)isrc;

	reg = BP_CFG_ENABLE;
	reg |= (pic_isrc->mips_hard_irq << BP_CFG_IRQ_S);
	bus_write_8(sc->res[BP_CFG], pic_isrc->irq * 8, reg);
}

static void
beri_pic_disable_intr(device_t dev, struct intr_irqsrc *isrc)
{
	struct beri_pic_isrc *pic_isrc;
	struct beripic_softc *sc;
	uint64_t reg;

	sc = device_get_softc(dev);
	pic_isrc = (struct beri_pic_isrc *)isrc;

	reg = bus_read_8(sc->res[BP_CFG], pic_isrc->irq * 8);
	reg &= ~BP_CFG_ENABLE;
	bus_write_8(sc->res[BP_CFG], pic_isrc->irq * 8, reg);
}

static int
beri_pic_map_intr(device_t dev, struct intr_map_data *data,
    struct intr_irqsrc **isrcp)
{
	struct beripic_softc *sc;
	struct intr_map_data_fdt *daf;
	uint32_t irq;

	sc = device_get_softc(dev);
	daf = (struct intr_map_data_fdt *)data;

	if (data == NULL || data->type != INTR_MAP_DATA_FDT ||
	    daf->ncells != 1 || daf->cells[0] >= sc->nirqs)
		return (EINVAL);

	irq = daf->cells[0];

	*isrcp = &sc->irqs[irq].isrc;

	return (0);
}

static void
beri_pic_post_ithread(device_t dev, struct intr_irqsrc *isrc)
{

	beri_pic_enable_intr(dev, isrc);
}

static void
beri_pic_pre_ithread(device_t dev, struct intr_irqsrc *isrc)
{

	beri_pic_disable_intr(dev, isrc);
}

#ifdef SMP
void
beripic_setup_ipi(device_t dev, u_int tid, u_int ipi_irq)
{
	struct beripic_softc *sc;
	uint64_t reg;

	sc = device_get_softc(dev);

	reg = (BP_CFG_ENABLE);
	reg |= (ipi_irq << BP_CFG_IRQ_S);
	reg |= (tid << BP_CFG_TID_S);
	bus_write_8(sc->res[BP_CFG], ((BP_FIRST_SOFT + tid) * 8), reg);
}

void
beripic_send_ipi(device_t dev, u_int tid)
{
	struct beripic_softc *sc;
	uint64_t bit;

	sc = device_get_softc(dev);

	bit = (BP_FIRST_SOFT + tid);
	KASSERT(bit < BP_NUM_IRQS, ("tid (%d) to large\n", tid));

	bus_write_8(sc->res[BP_IP_SET], 0x0, (1 << bit));
}

void
beripic_clear_ipi(device_t dev, u_int tid)
{
	struct beripic_softc *sc;
	uint64_t bit;

	sc = device_get_softc(dev);

	bit = (BP_FIRST_SOFT + tid);
	KASSERT(bit < BP_NUM_IRQS, ("tid (%d) to large\n", tid));

	bus_write_8(sc->res[BP_IP_CLEAR], 0x0, (1 << bit));
}
#endif

static device_method_t beripic_fdt_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		beripic_probe),
	DEVMETHOD(device_attach,	beripic_attach),

	/* Interrupt controller interface */
	DEVMETHOD(pic_enable_intr,	beri_pic_enable_intr),
	DEVMETHOD(pic_disable_intr,	beri_pic_disable_intr),
	DEVMETHOD(pic_map_intr,		beri_pic_map_intr),
	DEVMETHOD(pic_post_ithread,	beri_pic_post_ithread),
	DEVMETHOD(pic_pre_ithread,	beri_pic_pre_ithread),

	DEVMETHOD_END
};

devclass_t beripic_devclass;

static driver_t beripic_driver = {
	"beripic",
	beripic_fdt_methods,
	sizeof(struct beripic_softc)
};

EARLY_DRIVER_MODULE(beripic, ofwbus, beripic_driver, beripic_devclass, 0, 0,
    BUS_PASS_INTERRUPT + BUS_PASS_ORDER_MIDDLE);
