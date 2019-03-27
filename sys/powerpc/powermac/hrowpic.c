/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright 2003 by Peter Grehan. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
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
 *
 * $FreeBSD$
 */

/*
 * A driver for the PIC found in the Heathrow/Paddington MacIO chips.
 * This was superseded by an OpenPIC in the Keylargo and beyond 
 * MacIO versions.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/module.h>
#include <sys/bus.h>
#include <sys/conf.h>
#include <sys/kernel.h>
#include <sys/rman.h>

#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/openfirm.h>

#include <machine/bus.h>
#include <machine/intr_machdep.h>
#include <machine/md_var.h>
#include <machine/pio.h>
#include <machine/resource.h>

#include <vm/vm.h>
#include <vm/pmap.h>

#include <powerpc/powermac/hrowpicvar.h>

#include "pic_if.h"

/*
 * MacIO interface
 */
static int	hrowpic_probe(device_t);
static int	hrowpic_attach(device_t);

static void	hrowpic_dispatch(device_t, struct trapframe *);
static void	hrowpic_enable(device_t, u_int, u_int, void **);
static void	hrowpic_eoi(device_t, u_int, void *);
static void	hrowpic_ipi(device_t, u_int);
static void	hrowpic_mask(device_t, u_int, void *);
static void	hrowpic_unmask(device_t, u_int, void *);

static device_method_t  hrowpic_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,         hrowpic_probe),
	DEVMETHOD(device_attach,        hrowpic_attach),

	/* PIC interface */
	DEVMETHOD(pic_dispatch,		hrowpic_dispatch),
	DEVMETHOD(pic_enable,		hrowpic_enable),
	DEVMETHOD(pic_eoi,		hrowpic_eoi),
	DEVMETHOD(pic_ipi,		hrowpic_ipi),
	DEVMETHOD(pic_mask,		hrowpic_mask),
	DEVMETHOD(pic_unmask,		hrowpic_unmask),

	{ 0, 0 },
};

static driver_t hrowpic_driver = {
	"hrowpic",
	hrowpic_methods,
	sizeof(struct hrowpic_softc)
};

static devclass_t hrowpic_devclass;

DRIVER_MODULE(hrowpic, macio, hrowpic_driver, hrowpic_devclass, 0, 0);

static uint32_t
hrowpic_read_reg(struct hrowpic_softc *sc, u_int reg, u_int bank)
{
	if (bank == HPIC_PRIMARY)
		reg += HPIC_1ST_OFFSET;

	return (bus_space_read_4(sc->sc_bt, sc->sc_bh, reg));
}

static void
hrowpic_write_reg(struct hrowpic_softc *sc, u_int reg, u_int bank,
    uint32_t val)
{

	if (bank == HPIC_PRIMARY)
		reg += HPIC_1ST_OFFSET;

	bus_space_write_4(sc->sc_bt, sc->sc_bh, reg, val);

	/* XXX Issue a read to force the write to complete. */
	bus_space_read_4(sc->sc_bt, sc->sc_bh, reg);
}

static int
hrowpic_probe(device_t dev)
{
	const char *type = ofw_bus_get_type(dev);

	/*
	 * OpenPIC cells have a type of "open-pic", so this
	 * is sufficient to identify a Heathrow cell
	 */
	if (strcmp(type, "interrupt-controller") != 0)
		return (ENXIO);

	/*
	 * The description was already printed out in the nexus
	 * probe, so don't do it again here
	 */
	device_set_desc(dev, "Heathrow MacIO interrupt controller");
	return (0);
}

static int
hrowpic_attach(device_t dev)
{
	struct hrowpic_softc *sc;

	sc = device_get_softc(dev);
	sc->sc_dev = dev;

	sc->sc_rrid = 0;
	sc->sc_rres = bus_alloc_resource_any(dev, SYS_RES_MEMORY, &sc->sc_rrid,
	    RF_ACTIVE);

	if (sc->sc_rres == NULL) {
		device_printf(dev, "Could not alloc mem resource!\n");
		return (ENXIO);
	}

	sc->sc_bt = rman_get_bustag(sc->sc_rres);
	sc->sc_bh = rman_get_bushandle(sc->sc_rres);

	/*
	 * Disable all interrupt sources and clear outstanding interrupts
	 */
	hrowpic_write_reg(sc, HPIC_ENABLE, HPIC_PRIMARY, 0);
	hrowpic_write_reg(sc, HPIC_CLEAR,  HPIC_PRIMARY, 0xffffffff);
	hrowpic_write_reg(sc, HPIC_ENABLE, HPIC_SECONDARY, 0);
	hrowpic_write_reg(sc, HPIC_CLEAR,  HPIC_SECONDARY, 0xffffffff);

	powerpc_register_pic(dev, ofw_bus_get_node(dev), 64, 0, FALSE);
	return (0);
}

/*
 * Local routines
 */

static void
hrowpic_toggle_irq(struct hrowpic_softc *sc, int irq, int enable)
{
	u_int roffset;
	u_int rbit;

	KASSERT((irq > 0) && (irq <= HROWPIC_IRQMAX), ("en irq out of range"));

	/*
	 * Humor the SMP layer if it wants to set up an IPI handler.
	 */
	if (irq == HROWPIC_IRQMAX)
		return;

	/*
	 * Calculate prim/sec register bank for the IRQ, update soft copy,
	 * and enable the IRQ as an interrupt source
	 */
	roffset = HPIC_INT_TO_BANK(irq);
	rbit = HPIC_INT_TO_REGBIT(irq);

	if (enable)
		sc->sc_softreg[roffset] |= (1 << rbit);
	else
		sc->sc_softreg[roffset] &= ~(1 << rbit);
		
	hrowpic_write_reg(sc, HPIC_ENABLE, roffset, sc->sc_softreg[roffset]);
}

/*
 * PIC I/F methods.
 */

static void
hrowpic_dispatch(device_t dev, struct trapframe *tf)
{
	struct hrowpic_softc *sc;
	uint64_t mask;
	uint32_t reg;
	u_int irq;

	sc = device_get_softc(dev);
	while (1) {
		mask = hrowpic_read_reg(sc, HPIC_STATUS, HPIC_SECONDARY);
		reg = hrowpic_read_reg(sc, HPIC_STATUS, HPIC_PRIMARY);
		mask = (mask << 32) | reg;
		if (mask == 0)
			break;

		irq = 0;
		while (irq < HROWPIC_IRQMAX) {
			if (mask & 1)
				powerpc_dispatch_intr(sc->sc_vector[irq], tf);
			mask >>= 1;
			irq++;
		}
	}
}

static void
hrowpic_enable(device_t dev, u_int irq, u_int vector, void **priv __unused)
{
	struct hrowpic_softc *sc;

	sc = device_get_softc(dev);
	sc->sc_vector[irq] = vector;
	hrowpic_toggle_irq(sc, irq, 1);
}

static void
hrowpic_eoi(device_t dev, u_int irq, void *priv __unused)
{
	struct hrowpic_softc *sc;
	int bank;

	sc = device_get_softc(dev);
	bank = (irq >= 32) ? HPIC_SECONDARY : HPIC_PRIMARY ;
	hrowpic_write_reg(sc, HPIC_CLEAR, bank, 1U << (irq & 0x1f));
}

static void
hrowpic_ipi(device_t dev, u_int irq)
{
	/* No SMP support. */
}

static void
hrowpic_mask(device_t dev, u_int irq, void *priv __unused)
{
	struct hrowpic_softc *sc;

	sc = device_get_softc(dev);
	hrowpic_toggle_irq(sc, irq, 0);
}

static void
hrowpic_unmask(device_t dev, u_int irq, void *priv __unused)
{
	struct hrowpic_softc *sc;

	sc = device_get_softc(dev);
	hrowpic_toggle_irq(sc, irq, 1);
}
