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

#define	INTC_PENDING_BASIC	0x00
#define	INTC_PENDING_BANK1	0x04
#define	INTC_PENDING_BANK2	0x08
#define	INTC_FIQ_CONTROL	0x0C
#define	INTC_ENABLE_BANK1	0x10
#define	INTC_ENABLE_BANK2	0x14
#define	INTC_ENABLE_BASIC	0x18
#define	INTC_DISABLE_BANK1	0x1C
#define	INTC_DISABLE_BANK2	0x20
#define	INTC_DISABLE_BASIC	0x24

#define INTC_PENDING_BASIC_ARM		0x0000FF
#define INTC_PENDING_BASIC_GPU1_PEND	0x000100
#define INTC_PENDING_BASIC_GPU2_PEND	0x000200
#define INTC_PENDING_BASIC_GPU1_7	0x000400
#define INTC_PENDING_BASIC_GPU1_9	0x000800
#define INTC_PENDING_BASIC_GPU1_10	0x001000
#define INTC_PENDING_BASIC_GPU1_18	0x002000
#define INTC_PENDING_BASIC_GPU1_19	0x004000
#define INTC_PENDING_BASIC_GPU2_21	0x008000
#define INTC_PENDING_BASIC_GPU2_22	0x010000
#define INTC_PENDING_BASIC_GPU2_23	0x020000
#define INTC_PENDING_BASIC_GPU2_24	0x040000
#define INTC_PENDING_BASIC_GPU2_25	0x080000
#define INTC_PENDING_BASIC_GPU2_30	0x100000
#define INTC_PENDING_BASIC_MASK		0x1FFFFF

#define INTC_PENDING_BASIC_GPU1_MASK	(INTC_PENDING_BASIC_GPU1_7 |	\
					 INTC_PENDING_BASIC_GPU1_9 |	\
					 INTC_PENDING_BASIC_GPU1_10 |	\
					 INTC_PENDING_BASIC_GPU1_18 |	\
					 INTC_PENDING_BASIC_GPU1_19)

#define INTC_PENDING_BASIC_GPU2_MASK	(INTC_PENDING_BASIC_GPU2_21 |	\
					 INTC_PENDING_BASIC_GPU2_22 |	\
					 INTC_PENDING_BASIC_GPU2_23 |	\
					 INTC_PENDING_BASIC_GPU2_24 |	\
					 INTC_PENDING_BASIC_GPU2_25 |	\
					 INTC_PENDING_BASIC_GPU2_30)

#define INTC_PENDING_BANK1_MASK (~((1 << 7) | (1 << 9) | (1 << 10) | \
    (1 << 18) | (1 << 19)))
#define INTC_PENDING_BANK2_MASK (~((1 << 21) | (1 << 22) | (1 << 23) | \
    (1 << 24) | (1 << 25) | (1 << 30)))

#define	BANK1_START	8
#define	BANK1_END	(BANK1_START + 32 - 1)
#define	BANK2_START	(BANK1_START + 32)
#define	BANK2_END	(BANK2_START + 32 - 1)

#define	IS_IRQ_BASIC(n)	(((n) >= 0) && ((n) < BANK1_START))
#define	IS_IRQ_BANK1(n)	(((n) >= BANK1_START) && ((n) <= BANK1_END))
#define	IS_IRQ_BANK2(n)	(((n) >= BANK2_START) && ((n) <= BANK2_END))
#define	IRQ_BANK1(n)	((n) - BANK1_START)
#define	IRQ_BANK2(n)	((n) - BANK2_START)

#ifdef  DEBUG
#define dprintf(fmt, args...) printf(fmt, ##args)
#else
#define dprintf(fmt, args...)
#endif

#define BCM_INTC_NIRQS		72	/* 8 + 32 + 32 */

struct bcm_intc_irqsrc {
	struct intr_irqsrc	bii_isrc;
	u_int			bii_irq;
	uint16_t		bii_disable_reg;
	uint16_t		bii_enable_reg;
	uint32_t		bii_mask;
};

struct bcm_intc_softc {
	device_t		sc_dev;
	struct resource *	intc_res;
	bus_space_tag_t		intc_bst;
	bus_space_handle_t	intc_bsh;
	struct resource *	intc_irq_res;
	void *			intc_irq_hdl;
	struct bcm_intc_irqsrc	intc_isrcs[BCM_INTC_NIRQS];
};

static struct ofw_compat_data compat_data[] = {
	{"broadcom,bcm2835-armctrl-ic",		1},
	{"brcm,bcm2835-armctrl-ic",		1},
	{"brcm,bcm2836-armctrl-ic",		1},
	{NULL,					0}
};

static struct bcm_intc_softc *bcm_intc_sc = NULL;

#define	intc_read_4(_sc, reg)		\
    bus_space_read_4((_sc)->intc_bst, (_sc)->intc_bsh, (reg))
#define	intc_write_4(_sc, reg, val)		\
    bus_space_write_4((_sc)->intc_bst, (_sc)->intc_bsh, (reg), (val))

static inline void
bcm_intc_isrc_mask(struct bcm_intc_softc *sc, struct bcm_intc_irqsrc *bii)
{

	intc_write_4(sc, bii->bii_disable_reg,  bii->bii_mask);
}

static inline void
bcm_intc_isrc_unmask(struct bcm_intc_softc *sc, struct bcm_intc_irqsrc *bii)
{

	intc_write_4(sc, bii->bii_enable_reg,  bii->bii_mask);
}

static inline int
bcm2835_intc_active_intr(struct bcm_intc_softc *sc)
{
	uint32_t pending, pending_gpu;

	pending = intc_read_4(sc, INTC_PENDING_BASIC) & INTC_PENDING_BASIC_MASK;
	if (pending == 0)
		return (-1);
	if (pending & INTC_PENDING_BASIC_ARM)
		return (ffs(pending) - 1);
	if (pending & INTC_PENDING_BASIC_GPU1_MASK) {
		if (pending & INTC_PENDING_BASIC_GPU1_7)
			return (BANK1_START + 7);
		if (pending & INTC_PENDING_BASIC_GPU1_9)
			return (BANK1_START + 9);
		if (pending & INTC_PENDING_BASIC_GPU1_10)
			return (BANK1_START + 10);
		if (pending & INTC_PENDING_BASIC_GPU1_18)
			return (BANK1_START + 18);
		if (pending & INTC_PENDING_BASIC_GPU1_19)
			return (BANK1_START + 19);
	}
	if (pending & INTC_PENDING_BASIC_GPU2_MASK) {
		if (pending & INTC_PENDING_BASIC_GPU2_21)
			return (BANK2_START + 21);
		if (pending & INTC_PENDING_BASIC_GPU2_22)
			return (BANK2_START + 22);
		if (pending & INTC_PENDING_BASIC_GPU2_23)
			return (BANK2_START + 23);
		if (pending & INTC_PENDING_BASIC_GPU2_24)
			return (BANK2_START + 24);
		if (pending & INTC_PENDING_BASIC_GPU2_25)
			return (BANK2_START + 25);
		if (pending & INTC_PENDING_BASIC_GPU2_30)
			return (BANK2_START + 30);
	}
	if (pending & INTC_PENDING_BASIC_GPU1_PEND) {
		pending_gpu = intc_read_4(sc, INTC_PENDING_BANK1);
		pending_gpu &= INTC_PENDING_BANK1_MASK;
		if (pending_gpu != 0)
			return (BANK1_START + ffs(pending_gpu) - 1);
	}
	if (pending & INTC_PENDING_BASIC_GPU2_PEND) {
		pending_gpu = intc_read_4(sc, INTC_PENDING_BANK2);
		pending_gpu &= INTC_PENDING_BANK2_MASK;
		if (pending_gpu != 0)
			return (BANK2_START + ffs(pending_gpu) - 1);
	}
	return (-1);	/* It shouldn't end here, but it's hardware. */
}

static int
bcm2835_intc_intr(void *arg)
{
	int irq, num;
	struct bcm_intc_softc *sc = arg;

	for (num = 0; ; num++) {
		irq = bcm2835_intc_active_intr(sc);
		if (irq == -1)
			break;
		if (intr_isrc_dispatch(&sc->intc_isrcs[irq].bii_isrc,
		    curthread->td_intr_frame) != 0) {
			bcm_intc_isrc_mask(sc, &sc->intc_isrcs[irq]);
			device_printf(sc->sc_dev, "Stray irq %u disabled\n",
			    irq);
		}
		arm_irq_memory_barrier(0); /* XXX */
	}
	if (num == 0)
		device_printf(sc->sc_dev, "Spurious interrupt detected\n");

	return (FILTER_HANDLED);
}

static void
bcm_intc_enable_intr(device_t dev, struct intr_irqsrc *isrc)
{
	struct bcm_intc_irqsrc *bii = (struct bcm_intc_irqsrc *)isrc;

	arm_irq_memory_barrier(bii->bii_irq);
	bcm_intc_isrc_unmask(device_get_softc(dev), bii);
}

static void
bcm_intc_disable_intr(device_t dev, struct intr_irqsrc *isrc)
{

	bcm_intc_isrc_mask(device_get_softc(dev),
	    (struct bcm_intc_irqsrc *)isrc);
}

static int
bcm_intc_map_intr(device_t dev, struct intr_map_data *data,
    struct intr_irqsrc **isrcp)
{
	u_int irq;
	struct intr_map_data_fdt *daf;
	struct bcm_intc_softc *sc;
	bool valid;

	if (data->type != INTR_MAP_DATA_FDT)
		return (ENOTSUP);

	daf = (struct intr_map_data_fdt *)data;
	if (daf->ncells == 1)
		irq = daf->cells[0];
	else if (daf->ncells == 2) {
		valid = true;
		switch (daf->cells[0]) {
		case 0:
			irq = daf->cells[1];
			if (irq >= BANK1_START)
				valid = false;
			break;
		case 1:
			irq = daf->cells[1] + BANK1_START;
			if (irq > BANK1_END)
				valid = false;
			break;
		case 2:
			irq = daf->cells[1] + BANK2_START;
			if (irq > BANK2_END)
				valid = false;
			break;
		default:
			valid = false;
			break;
		}

		if (!valid) {
			device_printf(dev,
			    "invalid IRQ config: bank=%d, irq=%d\n",
			    daf->cells[0], daf->cells[1]);
			return (EINVAL);
		}
	}
	else
		return (EINVAL);

	if (irq >= BCM_INTC_NIRQS)
		return (EINVAL);

	sc = device_get_softc(dev);
	*isrcp = &sc->intc_isrcs[irq].bii_isrc;
	return (0);
}

static void
bcm_intc_pre_ithread(device_t dev, struct intr_irqsrc *isrc)
{

	bcm_intc_disable_intr(dev, isrc);
}

static void
bcm_intc_post_ithread(device_t dev, struct intr_irqsrc *isrc)
{

	bcm_intc_enable_intr(dev, isrc);
}

static void
bcm_intc_post_filter(device_t dev, struct intr_irqsrc *isrc)
{
}

static int
bcm_intc_pic_register(struct bcm_intc_softc *sc, intptr_t xref)
{
	struct bcm_intc_irqsrc *bii;
	int error;
	uint32_t irq;
	const char *name;

	name = device_get_nameunit(sc->sc_dev);
	for (irq = 0; irq < BCM_INTC_NIRQS; irq++) {
		bii = &sc->intc_isrcs[irq];
		bii->bii_irq = irq;
		if (IS_IRQ_BASIC(irq)) {
			bii->bii_disable_reg = INTC_DISABLE_BASIC;
			bii->bii_enable_reg = INTC_ENABLE_BASIC;
			bii->bii_mask = 1 << irq;
		} else if (IS_IRQ_BANK1(irq)) {
			bii->bii_disable_reg = INTC_DISABLE_BANK1;
			bii->bii_enable_reg = INTC_ENABLE_BANK1;
			bii->bii_mask = 1 << IRQ_BANK1(irq);
		} else if (IS_IRQ_BANK2(irq)) {
			bii->bii_disable_reg = INTC_DISABLE_BANK2;
			bii->bii_enable_reg = INTC_ENABLE_BANK2;
			bii->bii_mask = 1 << IRQ_BANK2(irq);
		} else
			return (ENXIO);

		error = intr_isrc_register(&bii->bii_isrc, sc->sc_dev, 0,
		    "%s,%u", name, irq);
		if (error != 0)
			return (error);
	}
	if (intr_pic_register(sc->sc_dev, xref) == NULL)
		return (ENXIO);

	return (0);
}

static int
bcm_intc_probe(device_t dev)
{

	if (!ofw_bus_status_okay(dev))
		return (ENXIO);

	if (ofw_bus_search_compatible(dev, compat_data)->ocd_data == 0)
		return (ENXIO);

	device_set_desc(dev, "BCM2835 Interrupt Controller");
	return (BUS_PROBE_DEFAULT);
}

static int
bcm_intc_attach(device_t dev)
{
	struct		bcm_intc_softc *sc = device_get_softc(dev);
	int		rid = 0;
	intptr_t	xref;
	sc->sc_dev = dev;

	if (bcm_intc_sc)
		return (ENXIO);

	sc->intc_res = bus_alloc_resource_any(dev, SYS_RES_MEMORY, &rid, RF_ACTIVE);
	if (sc->intc_res == NULL) {
		device_printf(dev, "could not allocate memory resource\n");
		return (ENXIO);
	}

	xref = OF_xref_from_node(ofw_bus_get_node(dev));
	if (bcm_intc_pic_register(sc, xref) != 0) {
		bus_release_resource(dev, SYS_RES_MEMORY, 0, sc->intc_res);
		device_printf(dev, "could not register PIC\n");
		return (ENXIO);
	}

	rid = 0;
	sc->intc_irq_res = bus_alloc_resource_any(dev, SYS_RES_IRQ, &rid,
	    RF_ACTIVE);
	if (sc->intc_irq_res == NULL) {
		if (intr_pic_claim_root(dev, xref, bcm2835_intc_intr, sc, 0) != 0) {
			/* XXX clean up */
			device_printf(dev, "could not set PIC as a root\n");
			return (ENXIO);
		}
	} else {
		if (bus_setup_intr(dev, sc->intc_irq_res, INTR_TYPE_CLK,
		    bcm2835_intc_intr, NULL, sc, &sc->intc_irq_hdl)) {
			/* XXX clean up */
			device_printf(dev, "could not setup irq handler\n");
			return (ENXIO);
		}
	}
	sc->intc_bst = rman_get_bustag(sc->intc_res);
	sc->intc_bsh = rman_get_bushandle(sc->intc_res);

	bcm_intc_sc = sc;

	return (0);
}

static device_method_t bcm_intc_methods[] = {
	DEVMETHOD(device_probe,		bcm_intc_probe),
	DEVMETHOD(device_attach,	bcm_intc_attach),

	DEVMETHOD(pic_disable_intr,	bcm_intc_disable_intr),
	DEVMETHOD(pic_enable_intr,	bcm_intc_enable_intr),
	DEVMETHOD(pic_map_intr,		bcm_intc_map_intr),
	DEVMETHOD(pic_post_filter,	bcm_intc_post_filter),
	DEVMETHOD(pic_post_ithread,	bcm_intc_post_ithread),
	DEVMETHOD(pic_pre_ithread,	bcm_intc_pre_ithread),

	{ 0, 0 }
};

static driver_t bcm_intc_driver = {
	"intc",
	bcm_intc_methods,
	sizeof(struct bcm_intc_softc),
};

static devclass_t bcm_intc_devclass;

EARLY_DRIVER_MODULE(intc, simplebus, bcm_intc_driver, bcm_intc_devclass,
    0, 0, BUS_PASS_INTERRUPT + BUS_PASS_ORDER_MIDDLE);
