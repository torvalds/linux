/*-
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

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/proc.h>
#include <sys/rman.h>
#include <machine/bus.h>
#include <machine/intr.h>

#include <dev/fdt/fdt_intr.h>
#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>

#include "pic_if.h"

#define	NMI_IRQ_CTRL_REG	0x0
#define	 NMI_IRQ_LOW_LEVEL	0x0
#define	 NMI_IRQ_LOW_EDGE	0x1
#define	 NMI_IRQ_HIGH_LEVEL	0x2
#define	 NMI_IRQ_HIGH_EDGE	0x3
#define	NMI_IRQ_PENDING_REG	0x4
#define	 NMI_IRQ_ACK		(1U << 0)
#define	A20_NMI_IRQ_ENABLE_REG	0x8
#define	A31_NMI_IRQ_ENABLE_REG	0x34
#define	 NMI_IRQ_ENABLE		(1U << 0)

#define	R_NMI_IRQ_CTRL_REG	0x0c
#define	R_NMI_IRQ_PENDING_REG	0x10
#define	R_NMI_IRQ_ENABLE_REG	0x40

#define	SC_NMI_READ(_sc, _reg)		bus_read_4(_sc->res[0], _reg)
#define	SC_NMI_WRITE(_sc, _reg, _val)	bus_write_4(_sc->res[0], _reg, _val)

static struct resource_spec aw_nmi_res_spec[] = {
	{ SYS_RES_MEMORY,	0,	RF_ACTIVE },
	{ SYS_RES_IRQ,		0,	RF_ACTIVE },
	{ -1,			0,	0 }
};

struct aw_nmi_intr {
	struct intr_irqsrc	isrc;
	u_int			irq;
	enum intr_polarity	pol;
	enum intr_trigger	tri;
};

struct aw_nmi_reg_cfg {
	uint8_t			ctrl_reg;
	uint8_t			pending_reg;
	uint8_t			enable_reg;
};

struct aw_nmi_softc {
	device_t		dev;
	struct resource *	res[2];
	void *			intrcookie;
	struct aw_nmi_intr	intr;
	struct aw_nmi_reg_cfg *	cfg;
};

static struct aw_nmi_reg_cfg a20_nmi_cfg = {
	.ctrl_reg =	NMI_IRQ_CTRL_REG,
	.pending_reg =	NMI_IRQ_PENDING_REG,
	.enable_reg =	A20_NMI_IRQ_ENABLE_REG,
};

static struct aw_nmi_reg_cfg a31_nmi_cfg = {
	.ctrl_reg =	NMI_IRQ_CTRL_REG,
	.pending_reg =	NMI_IRQ_PENDING_REG,
	.enable_reg =	A31_NMI_IRQ_ENABLE_REG,
};

static struct aw_nmi_reg_cfg a83t_r_nmi_cfg = {
	.ctrl_reg =	R_NMI_IRQ_CTRL_REG,
	.pending_reg =	R_NMI_IRQ_PENDING_REG,
	.enable_reg =	R_NMI_IRQ_ENABLE_REG,
};

static struct ofw_compat_data compat_data[] = {
	{"allwinner,sun7i-a20-sc-nmi", (uintptr_t)&a20_nmi_cfg},
	{"allwinner,sun6i-a31-sc-nmi", (uintptr_t)&a31_nmi_cfg},
	{"allwinner,sun6i-a31-r-intc", (uintptr_t)&a83t_r_nmi_cfg},
	{"allwinner,sun8i-a83t-r-intc", (uintptr_t)&a83t_r_nmi_cfg},
	{NULL, 0},
};

static int
aw_nmi_intr(void *arg)
{
	struct aw_nmi_softc *sc;

	sc = arg;

	if (SC_NMI_READ(sc, sc->cfg->pending_reg) == 0) {
		device_printf(sc->dev, "Spurious interrupt\n");
		return (FILTER_HANDLED);
	}

	if (intr_isrc_dispatch(&sc->intr.isrc, curthread->td_intr_frame) != 0) {
		SC_NMI_WRITE(sc, sc->cfg->enable_reg, !NMI_IRQ_ENABLE);
		device_printf(sc->dev, "Stray interrupt, NMI disabled\n");
	}

	return (FILTER_HANDLED);
}

static void
aw_nmi_enable_intr(device_t dev, struct intr_irqsrc *isrc)
{
	struct aw_nmi_softc *sc;

	sc = device_get_softc(dev);

	SC_NMI_WRITE(sc, sc->cfg->enable_reg, NMI_IRQ_ENABLE);
}

static void
aw_nmi_disable_intr(device_t dev, struct intr_irqsrc *isrc)
{
	struct aw_nmi_softc *sc;

	sc = device_get_softc(dev);

	SC_NMI_WRITE(sc, sc->cfg->enable_reg, !NMI_IRQ_ENABLE);
}

static int
aw_nmi_map_fdt(device_t dev, u_int ncells, pcell_t *cells, u_int *irqp,
    enum intr_polarity *polp, enum intr_trigger *trigp)
{
	u_int irq, tripol;
	enum intr_polarity pol;
	enum intr_trigger trig;

	if (ncells != 2) {
		device_printf(dev, "Invalid #interrupt-cells\n");
		return (EINVAL);
	}

	irq = cells[0];
	if (irq != 0) {
		device_printf(dev, "Controller only support irq 0\n");
		return (EINVAL);
	}

	tripol = cells[1];

	switch (tripol) {
	case FDT_INTR_EDGE_RISING:
		trig = INTR_TRIGGER_EDGE;
		pol  = INTR_POLARITY_HIGH;
		break;
	case FDT_INTR_EDGE_FALLING:
		trig = INTR_TRIGGER_EDGE;
		pol  = INTR_POLARITY_LOW;
		break;
	case FDT_INTR_LEVEL_HIGH:
		trig = INTR_TRIGGER_LEVEL;
		pol  = INTR_POLARITY_HIGH;
		break;
	case FDT_INTR_LEVEL_LOW:
		trig = INTR_TRIGGER_LEVEL;
		pol  = INTR_POLARITY_LOW;
		break;
	default:
		device_printf(dev, "unsupported trigger/polarity 0x%2x\n",
		    tripol);
		return (ENOTSUP);
	}

	*irqp = irq;
	if (polp != NULL)
		*polp = pol;
	if (trigp != NULL)
		*trigp = trig;
	return (0);
}

static int
aw_nmi_map_intr(device_t dev, struct intr_map_data *data,
    struct intr_irqsrc **isrcp)
{
	struct intr_map_data_fdt *daf;
	struct aw_nmi_softc *sc;
	int error;
	u_int irq;

	if (data->type != INTR_MAP_DATA_FDT)
		return (ENOTSUP);

	sc = device_get_softc(dev);
	daf = (struct intr_map_data_fdt *)data;

	error = aw_nmi_map_fdt(dev, daf->ncells, daf->cells, &irq, NULL, NULL);
	if (error == 0)
		*isrcp = &sc->intr.isrc;

	return (error);
}

static int
aw_nmi_setup_intr(device_t dev, struct intr_irqsrc *isrc,
    struct resource *res, struct intr_map_data *data)
{
	struct intr_map_data_fdt *daf;
	struct aw_nmi_softc *sc;
	struct aw_nmi_intr *nmi_intr;
	int error, icfg;
	u_int irq;
	enum intr_trigger trig;
	enum intr_polarity pol;

	/* Get config for interrupt. */
	if (data == NULL || data->type != INTR_MAP_DATA_FDT)
		return (ENOTSUP);

	sc = device_get_softc(dev);
	nmi_intr = (struct aw_nmi_intr *)isrc;
	daf = (struct intr_map_data_fdt *)data;

	error = aw_nmi_map_fdt(dev, daf->ncells, daf->cells, &irq, &pol, &trig);
	if (error != 0)
		return (error);
	if (nmi_intr->irq != irq)
		return (EINVAL);

	/* Compare config if this is not first setup. */
	if (isrc->isrc_handlers != 0) {
		if (pol != nmi_intr->pol || trig != nmi_intr->tri)
			return (EINVAL);
		else
			return (0);
	}

	nmi_intr->pol = pol;
	nmi_intr->tri = trig;

	if (trig == INTR_TRIGGER_LEVEL) {
		if (pol == INTR_POLARITY_LOW)
			icfg = NMI_IRQ_LOW_LEVEL;
		else
			icfg = NMI_IRQ_HIGH_LEVEL;
	} else {
		if (pol == INTR_POLARITY_HIGH)
			icfg = NMI_IRQ_HIGH_EDGE;
		else
			icfg = NMI_IRQ_LOW_EDGE;
	}

	SC_NMI_WRITE(sc, sc->cfg->ctrl_reg, icfg);

	return (0);
}

static int
aw_nmi_teardown_intr(device_t dev, struct intr_irqsrc *isrc,
    struct resource *res, struct intr_map_data *data)
{
	struct aw_nmi_softc *sc;

	sc = device_get_softc(dev);

	if (isrc->isrc_handlers == 0) {
		sc->intr.pol = INTR_POLARITY_CONFORM;
		sc->intr.tri = INTR_TRIGGER_CONFORM;

		SC_NMI_WRITE(sc, sc->cfg->enable_reg, !NMI_IRQ_ENABLE);
	}

	return (0);
}

static void
aw_nmi_pre_ithread(device_t dev, struct intr_irqsrc *isrc)
{
	struct aw_nmi_softc *sc;

	sc = device_get_softc(dev);
	aw_nmi_disable_intr(dev, isrc);
	SC_NMI_WRITE(sc, sc->cfg->pending_reg, NMI_IRQ_ACK);
}

static void
aw_nmi_post_ithread(device_t dev, struct intr_irqsrc *isrc)
{

	arm_irq_memory_barrier(0);
	aw_nmi_enable_intr(dev, isrc);
}

static void
aw_nmi_post_filter(device_t dev, struct intr_irqsrc *isrc)
{
	struct aw_nmi_softc *sc;

	sc = device_get_softc(dev);

	arm_irq_memory_barrier(0);
	SC_NMI_WRITE(sc, sc->cfg->pending_reg, NMI_IRQ_ACK);
}

static int
aw_nmi_probe(device_t dev)
{

	if (!ofw_bus_status_okay(dev))
		return (ENXIO);

	if (ofw_bus_search_compatible(dev, compat_data)->ocd_data == 0)
		return (ENXIO);
	device_set_desc(dev, "Allwinner NMI Controller");
	return (BUS_PROBE_DEFAULT);
}

static int
aw_nmi_attach(device_t dev)
{
	struct aw_nmi_softc *sc;
	phandle_t xref;

	sc = device_get_softc(dev);
	sc->dev = dev;
	sc->cfg = (struct aw_nmi_reg_cfg *)
	    ofw_bus_search_compatible(dev, compat_data)->ocd_data;

	if (bus_alloc_resources(dev, aw_nmi_res_spec, sc->res) != 0) {
		device_printf(dev, "can't allocate device resources\n");
		return (ENXIO);
	}
	if ((bus_setup_intr(dev, sc->res[1], INTR_TYPE_MISC,
	    aw_nmi_intr, NULL, sc, &sc->intrcookie))) {
		device_printf(dev, "unable to register interrupt handler\n");
		bus_release_resources(dev, aw_nmi_res_spec, sc->res);
		return (ENXIO);
	}

	/* Disable and clear interrupts */
	SC_NMI_WRITE(sc, sc->cfg->enable_reg, !NMI_IRQ_ENABLE);
	SC_NMI_WRITE(sc, sc->cfg->pending_reg, NMI_IRQ_ACK);

	xref = OF_xref_from_node(ofw_bus_get_node(dev));
	/* Register our isrc */
	sc->intr.irq = 0;
	sc->intr.pol = INTR_POLARITY_CONFORM;
	sc->intr.tri = INTR_TRIGGER_CONFORM;
	if (intr_isrc_register(&sc->intr.isrc, sc->dev, 0, "%s,%u",
	      device_get_nameunit(sc->dev), sc->intr.irq) != 0)
		goto error;

	if (intr_pic_register(dev, (intptr_t)xref) == NULL) {
		device_printf(dev, "could not register pic\n");
		goto error;
	}
	return (0);

error:
	bus_teardown_intr(dev, sc->res[1], sc->intrcookie);
	bus_release_resources(dev, aw_nmi_res_spec, sc->res);
	return (ENXIO);
}

static device_method_t aw_nmi_methods[] = {
	DEVMETHOD(device_probe,		aw_nmi_probe),
	DEVMETHOD(device_attach,	aw_nmi_attach),

	/* Interrupt controller interface */
	DEVMETHOD(pic_disable_intr,	aw_nmi_disable_intr),
	DEVMETHOD(pic_enable_intr,	aw_nmi_enable_intr),
	DEVMETHOD(pic_map_intr,		aw_nmi_map_intr),
	DEVMETHOD(pic_setup_intr,	aw_nmi_setup_intr),
	DEVMETHOD(pic_teardown_intr,	aw_nmi_teardown_intr),
	DEVMETHOD(pic_post_filter,	aw_nmi_post_filter),
	DEVMETHOD(pic_post_ithread,	aw_nmi_post_ithread),
	DEVMETHOD(pic_pre_ithread,	aw_nmi_pre_ithread),

	{0, 0},
};

static driver_t aw_nmi_driver = {
	"aw_nmi",
	aw_nmi_methods,
	sizeof(struct aw_nmi_softc),
};

static devclass_t aw_nmi_devclass;

EARLY_DRIVER_MODULE(aw_nmi, simplebus, aw_nmi_driver,
    aw_nmi_devclass, 0, 0, BUS_PASS_INTERRUPT + BUS_PASS_ORDER_LATE);
