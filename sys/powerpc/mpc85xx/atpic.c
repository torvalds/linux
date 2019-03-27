/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2009 Marcel Moolenaar
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/cpuset.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/rman.h>

#include <machine/bus.h>
#include <machine/intr_machdep.h>
#include <machine/pio.h>

#include <powerpc/mpc85xx/mpc85xx.h>

#include <dev/ic/i8259.h>

#include <isa/isareg.h>
#include <isa/isavar.h>

#include "pic_if.h"

#define	ATPIC_MASTER	0
#define	ATPIC_SLAVE	1

struct atpic_softc {
	device_t	sc_dev;

	/* I/O port resources for master & slave. */
	struct resource	*sc_res[2];
	int		sc_rid[2];

	/* Our "routing" interrupt */
	struct resource *sc_ires;
	void		*sc_icookie;
	int		sc_irid;

	int		sc_vector[16];
	uint8_t		sc_mask[2];
};

static int	atpic_isa_attach(device_t);
static void	atpic_isa_identify(driver_t *, device_t);
static int	atpic_isa_probe(device_t);

static void atpic_config(device_t, u_int, enum intr_trigger,
    enum intr_polarity);
static void atpic_dispatch(device_t, struct trapframe *);
static void atpic_enable(device_t, u_int, u_int);
static void atpic_eoi(device_t, u_int);
static void atpic_ipi(device_t, u_int);
static void atpic_mask(device_t, u_int);
static void atpic_unmask(device_t, u_int);

static void atpic_ofw_translate_code(device_t, u_int irq, int code,
    enum intr_trigger *trig, enum intr_polarity *pol);

static device_method_t atpic_isa_methods[] = {
	/* Device interface */
	DEVMETHOD(device_identify, 	atpic_isa_identify),
	DEVMETHOD(device_probe,		atpic_isa_probe),
	DEVMETHOD(device_attach,	atpic_isa_attach),

	/* PIC interface */
	DEVMETHOD(pic_config,		atpic_config),
	DEVMETHOD(pic_dispatch,		atpic_dispatch),
	DEVMETHOD(pic_enable,		atpic_enable),
	DEVMETHOD(pic_eoi,		atpic_eoi),
	DEVMETHOD(pic_ipi,		atpic_ipi),
	DEVMETHOD(pic_mask,		atpic_mask),
	DEVMETHOD(pic_unmask,		atpic_unmask),

	DEVMETHOD(pic_translate_code,	atpic_ofw_translate_code),

	{ 0, 0 },
};

static driver_t atpic_isa_driver = {
	"atpic",
	atpic_isa_methods,
	sizeof(struct atpic_softc)
};

static devclass_t atpic_devclass;

static struct isa_pnp_id atpic_ids[] = {
	{ 0x0000d041 /* PNP0000 */, "AT interrupt controller" },
	{ 0 }
};

DRIVER_MODULE(atpic, isa, atpic_isa_driver, atpic_devclass, 0, 0);
ISA_PNP_INFO(atpic_ids);

static __inline uint8_t
atpic_read(struct atpic_softc *sc, int icu, int ofs)
{
	uint8_t val;

	val = bus_read_1(sc->sc_res[icu], ofs);
	return (val);
}

static __inline void
atpic_write(struct atpic_softc *sc, int icu, int ofs, uint8_t val)
{

	bus_write_1(sc->sc_res[icu], ofs, val);
	bus_barrier(sc->sc_res[icu], ofs, 2 - ofs,
	    BUS_SPACE_BARRIER_READ|BUS_SPACE_BARRIER_WRITE);
}

static void
atpic_intr(void *arg)
{

	atpic_dispatch(arg, NULL);
}

static void
atpic_isa_identify(driver_t *drv, device_t parent)
{
	device_t child;

	child = BUS_ADD_CHILD(parent, ISA_ORDER_SENSITIVE, drv->name, -1);
	device_set_driver(child, drv);
	isa_set_logicalid(child, atpic_ids[0].ip_id);
	isa_set_vendorid(child, atpic_ids[0].ip_id);

	bus_set_resource(child, SYS_RES_IOPORT, ATPIC_MASTER, IO_ICU1, 2);
	bus_set_resource(child, SYS_RES_IOPORT, ATPIC_SLAVE, IO_ICU2, 2);

	/* ISA interrupts are routed through external interrupt 0. */
	bus_set_resource(child, SYS_RES_IRQ, 0, 16, 1);
}

static int
atpic_isa_probe(device_t dev)
{
	int res;

	res = ISA_PNP_PROBE(device_get_parent(dev), dev, atpic_ids);
	if (res > 0)
		return (res);

	device_set_desc(dev, "PC/AT compatible PIC");
	return (res);
}

static void
atpic_init(struct atpic_softc *sc, int icu)
{

	sc->sc_mask[icu] = 0xff - ((icu == ATPIC_MASTER) ? 4 : 0);

	atpic_write(sc, icu, 0, ICW1_RESET | ICW1_IC4);
	atpic_write(sc, icu, 1, (icu == ATPIC_SLAVE) ? 8 : 0);
	atpic_write(sc, icu, 1, (icu == ATPIC_SLAVE) ? 2 : 4);
	atpic_write(sc, icu, 1, ICW4_8086);
	atpic_write(sc, icu, 1, sc->sc_mask[icu]);
	atpic_write(sc, icu, 0, OCW3_SEL | OCW3_RR);
}

static int
atpic_isa_attach(device_t dev)
{
	struct atpic_softc *sc;
	int error;

	sc = device_get_softc(dev);
	sc->sc_dev = dev;

	error = ENXIO;

	sc->sc_rid[ATPIC_MASTER] = 0;
	sc->sc_res[ATPIC_MASTER] = bus_alloc_resource_any(dev, SYS_RES_IOPORT,
	    &sc->sc_rid[ATPIC_MASTER], RF_ACTIVE);
	if (sc->sc_res[ATPIC_MASTER] == NULL)
		goto fail;

	sc->sc_rid[ATPIC_SLAVE] = 1;
	sc->sc_res[ATPIC_SLAVE] = bus_alloc_resource_any(dev, SYS_RES_IOPORT,
	    &sc->sc_rid[ATPIC_SLAVE], RF_ACTIVE);
	if (sc->sc_res[ATPIC_SLAVE] == NULL)
		goto fail;

	sc->sc_irid = 0;
	sc->sc_ires = bus_alloc_resource_any(dev, SYS_RES_IRQ, &sc->sc_irid,
	    RF_ACTIVE);
	if (sc->sc_ires == NULL)
		goto fail;

	error = bus_setup_intr(dev, sc->sc_ires, INTR_TYPE_MISC | INTR_MPSAFE,
	    NULL, atpic_intr, dev, &sc->sc_icookie);
	if (error)
		goto fail;

	atpic_init(sc, ATPIC_SLAVE);
	atpic_init(sc, ATPIC_MASTER);

	powerpc_register_pic(dev, 0, 16, 0, TRUE);
	return (0);

 fail:
	if (sc->sc_ires != NULL)
		bus_release_resource(dev, SYS_RES_IRQ, sc->sc_irid,
		    sc->sc_ires);
	if (sc->sc_res[ATPIC_SLAVE] != NULL)
		bus_release_resource(dev, SYS_RES_IOPORT,
		    sc->sc_rid[ATPIC_SLAVE], sc->sc_res[ATPIC_SLAVE]);
	if (sc->sc_res[ATPIC_MASTER] != NULL)
		bus_release_resource(dev, SYS_RES_IOPORT,
		    sc->sc_rid[ATPIC_MASTER], sc->sc_res[ATPIC_MASTER]);
	return (error);
}


/*
 * PIC interface.
 */

static void
atpic_config(device_t dev, u_int irq, enum intr_trigger trig,
    enum intr_polarity pol)
{
}

static void
atpic_dispatch(device_t dev, struct trapframe *tf)
{
	struct atpic_softc *sc;
	uint8_t irq;

	sc = device_get_softc(dev);
	atpic_write(sc, ATPIC_MASTER, 0, OCW3_SEL | OCW3_P);
	irq = atpic_read(sc, ATPIC_MASTER, 0);
	atpic_write(sc, ATPIC_MASTER, 0, OCW3_SEL | OCW3_RR);
	if ((irq & 0x80) == 0)
		return;

	if (irq == 0x82) {
		atpic_write(sc, ATPIC_SLAVE, 0, OCW3_SEL | OCW3_P);
		irq = atpic_read(sc, ATPIC_SLAVE, 0) + 8;
		atpic_write(sc, ATPIC_SLAVE, 0, OCW3_SEL | OCW3_RR);
		if ((irq & 0x80) == 0)
			return;
	}

	powerpc_dispatch_intr(sc->sc_vector[irq & 0x0f], tf);
}

static void
atpic_enable(device_t dev, u_int irq, u_int vector)
{
	struct atpic_softc *sc;

	sc = device_get_softc(dev);
	sc->sc_vector[irq] = vector;
	atpic_unmask(dev, irq);
}

static void
atpic_eoi(device_t dev, u_int irq)
{
	struct atpic_softc *sc;

	sc = device_get_softc(dev);
	if (irq > 7)
		atpic_write(sc, ATPIC_SLAVE, 0, OCW2_EOI);
	atpic_write(sc, ATPIC_MASTER, 0, OCW2_EOI);
}

static void
atpic_ipi(device_t dev, u_int cpu)
{
	/* No SMP support. */
}

static void
atpic_mask(device_t dev, u_int irq)
{
	struct atpic_softc *sc;

	sc = device_get_softc(dev);
	if (irq > 7) {
		sc->sc_mask[ATPIC_SLAVE] |= 1 << (irq - 8);
		atpic_write(sc, ATPIC_SLAVE, 1, sc->sc_mask[ATPIC_SLAVE]);
	} else {
		sc->sc_mask[ATPIC_MASTER] |= 1 << irq;
		atpic_write(sc, ATPIC_MASTER, 1, sc->sc_mask[ATPIC_MASTER]);
	}
}

static void
atpic_unmask(device_t dev, u_int irq)
{
	struct atpic_softc *sc;

	sc = device_get_softc(dev);
	if (irq > 7) {
		sc->sc_mask[ATPIC_SLAVE] &= ~(1 << (irq - 8));
		atpic_write(sc, ATPIC_SLAVE, 1, sc->sc_mask[ATPIC_SLAVE]);
	} else {
		sc->sc_mask[ATPIC_MASTER] &= ~(1 << irq);
		atpic_write(sc, ATPIC_MASTER, 1, sc->sc_mask[ATPIC_MASTER]);
	}
}

static void
atpic_ofw_translate_code(device_t dev, u_int irq, int code,
    enum intr_trigger *trig, enum intr_polarity *pol)
{
	switch (code) {
	case 0:
		/* Active L level */
		*trig = INTR_TRIGGER_LEVEL;
		*pol = INTR_POLARITY_LOW;
		break;
	case 1:
		/* Active H level */
		*trig = INTR_TRIGGER_LEVEL;
		*pol = INTR_POLARITY_HIGH;
		break;
	case 2:
		/* H to L edge */
		*trig = INTR_TRIGGER_EDGE;
		*pol = INTR_POLARITY_LOW;
		break;
	case 3:
		/* L to H edge */
		*trig = INTR_TRIGGER_EDGE;
		*pol = INTR_POLARITY_HIGH;
		break;
	default:
		*trig = INTR_TRIGGER_CONFORM;
		*pol = INTR_POLARITY_CONFORM;
	}
}
