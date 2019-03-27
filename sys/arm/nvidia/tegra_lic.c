/*-
 * Copyright (c) 2016 Michal Meloun <mmel@FreeBSD.org>
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

/*
 * Local interrupt controller driver for Tegra SoCs.
 */
#include <sys/param.h>
#include <sys/module.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/conf.h>
#include <sys/kernel.h>
#include <sys/rman.h>

#include <machine/fdt.h>
#include <machine/intr.h>
#include <machine/resource.h>

#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>

#include "pic_if.h"

#define	LIC_VIRQ_CPU		0x00
#define	LIC_VIRQ_COP		0x04
#define	LIC_VFRQ_CPU		0x08
#define	LIC_VFRQ_COP		0x0c
#define	LIC_ISR			0x10
#define	LIC_FIR			0x14
#define	LIC_FIR_SET		0x18
#define	LIC_FIR_CLR		0x1c
#define	LIC_CPU_IER		0x20
#define	LIC_CPU_IER_SET		0x24
#define	LIC_CPU_IER_CLR		0x28
#define	LIC_CPU_IEP_CLASS	0x2C
#define	LIC_COP_IER		0x30
#define	LIC_COP_IER_SET		0x34
#define	LIC_COP_IER_CLR		0x38
#define	LIC_COP_IEP_CLASS	0x3c

#define	WR4(_sc, _b, _r, _v)	bus_write_4((_sc)->mem_res[_b], (_r), (_v))
#define	RD4(_sc, _b, _r)	bus_read_4((_sc)->mem_res[_b], (_r))

static struct resource_spec lic_spec[] = {
	{ SYS_RES_MEMORY,	0,	RF_ACTIVE },
	{ SYS_RES_MEMORY,	1,	RF_ACTIVE },
	{ SYS_RES_MEMORY,	2,	RF_ACTIVE },
	{ SYS_RES_MEMORY,	3,	RF_ACTIVE },
	{ SYS_RES_MEMORY,	4,	RF_ACTIVE },
	{ -1, 0 }
};

static struct ofw_compat_data compat_data[] = {
	{"nvidia,tegra124-ictlr", 	1},
	{NULL,				0}
};

struct tegra_lic_sc {
	device_t		dev;
	struct resource		*mem_res[nitems(lic_spec)];
	device_t		parent;
};

static int
tegra_lic_activate_intr(device_t dev, struct intr_irqsrc *isrc,
    struct resource *res, struct intr_map_data *data)
{
	struct tegra_lic_sc *sc = device_get_softc(dev);

	return (PIC_ACTIVATE_INTR(sc->parent, isrc, res, data));
}

static void
tegra_lic_disable_intr(device_t dev, struct intr_irqsrc *isrc)
{
	struct tegra_lic_sc *sc = device_get_softc(dev);

	PIC_DISABLE_INTR(sc->parent, isrc);
}

static void
tegra_lic_enable_intr(device_t dev, struct intr_irqsrc *isrc)
{
	struct tegra_lic_sc *sc = device_get_softc(dev);

	PIC_ENABLE_INTR(sc->parent, isrc);
}

static int
tegra_lic_map_intr(device_t dev, struct intr_map_data *data,
    struct intr_irqsrc **isrcp)
{
	struct tegra_lic_sc *sc = device_get_softc(dev);

	return (PIC_MAP_INTR(sc->parent, data, isrcp));
}

static int
tegra_lic_deactivate_intr(device_t dev, struct intr_irqsrc *isrc,
    struct resource *res, struct intr_map_data *data)
{
	struct tegra_lic_sc *sc = device_get_softc(dev);

	return (PIC_DEACTIVATE_INTR(sc->parent, isrc, res, data));
}

static int
tegra_lic_setup_intr(device_t dev, struct intr_irqsrc *isrc,
    struct resource *res, struct intr_map_data *data)
{
	struct tegra_lic_sc *sc = device_get_softc(dev);

	return (PIC_SETUP_INTR(sc->parent, isrc, res, data));
}

static int
tegra_lic_teardown_intr(device_t dev, struct intr_irqsrc *isrc,
    struct resource *res, struct intr_map_data *data)
{
	struct tegra_lic_sc *sc = device_get_softc(dev);

	return (PIC_TEARDOWN_INTR(sc->parent, isrc, res, data));
}

static void
tegra_lic_pre_ithread(device_t dev, struct intr_irqsrc *isrc)
{
	struct tegra_lic_sc *sc = device_get_softc(dev);

	PIC_PRE_ITHREAD(sc->parent, isrc);
}


static void
tegra_lic_post_ithread(device_t dev, struct intr_irqsrc *isrc)
{
	struct tegra_lic_sc *sc = device_get_softc(dev);

	PIC_POST_ITHREAD(sc->parent, isrc);
}

static void
tegra_lic_post_filter(device_t dev, struct intr_irqsrc *isrc)
{
	struct tegra_lic_sc *sc = device_get_softc(dev);

	PIC_POST_FILTER(sc->parent, isrc);
}

#ifdef SMP
static int
tegra_lic_bind_intr(device_t dev, struct intr_irqsrc *isrc)
{
	struct tegra_lic_sc *sc = device_get_softc(dev);

	return (PIC_BIND_INTR(sc->parent, isrc));
}
#endif

static int
tegra_lic_probe(device_t dev)
{
	if (!ofw_bus_status_okay(dev))
		return (ENXIO);

	if (ofw_bus_search_compatible(dev, compat_data)->ocd_data == 0)
		return (ENXIO);

	return (BUS_PROBE_DEFAULT);
}

static int
tegra_lic_attach(device_t dev)
{
	struct tegra_lic_sc *sc;
	phandle_t node;
	phandle_t parent_xref;
	int i, rv;

	sc = device_get_softc(dev);
	sc->dev = dev;
	node = ofw_bus_get_node(dev);

	rv = OF_getencprop(node, "interrupt-parent", &parent_xref,
	    sizeof(parent_xref));
	if (rv <= 0) {
		device_printf(dev, "Cannot read parent node property\n");
		goto fail;
	}
	sc->parent = OF_device_from_xref(parent_xref);
	if (sc->parent == NULL) {
		device_printf(dev, "Cannott find parent controller\n");
		goto fail;
	}

	if (bus_alloc_resources(dev, lic_spec, sc->mem_res)) {
		device_printf(dev, "Cannott allocate resources\n");
		goto fail;
	}

	/* Disable all interrupts, route all to irq */
	for (i = 0; i < nitems(lic_spec); i++) {
		if (sc->mem_res[i] == NULL)
			continue;
		WR4(sc, i, LIC_CPU_IER_CLR, 0xFFFFFFFF);
		WR4(sc, i, LIC_CPU_IEP_CLASS, 0);
	}


	if (intr_pic_register(dev, OF_xref_from_node(node)) == NULL) {
		device_printf(dev, "Cannot register PIC\n");
		goto fail;
	}
	return (0);

fail:
	bus_release_resources(dev, lic_spec, sc->mem_res);
	return (ENXIO);
}

static int
tegra_lic_detach(device_t dev)
{
	struct tegra_lic_sc *sc;
	int i;

	sc = device_get_softc(dev);
	for (i = 0; i < nitems(lic_spec); i++) {
		if (sc->mem_res[i] == NULL)
			continue;
		bus_release_resource(dev, SYS_RES_MEMORY, i,
		    sc->mem_res[i]);
	}
	return (0);
}

static device_method_t tegra_lic_methods[] = {
	DEVMETHOD(device_probe,		tegra_lic_probe),
	DEVMETHOD(device_attach,	tegra_lic_attach),
	DEVMETHOD(device_detach,	tegra_lic_detach),

	/* Interrupt controller interface */
	DEVMETHOD(pic_activate_intr,	tegra_lic_activate_intr),
	DEVMETHOD(pic_disable_intr,	tegra_lic_disable_intr),
	DEVMETHOD(pic_enable_intr,	tegra_lic_enable_intr),
	DEVMETHOD(pic_map_intr,		tegra_lic_map_intr),
	DEVMETHOD(pic_deactivate_intr,	tegra_lic_deactivate_intr),
	DEVMETHOD(pic_setup_intr,	tegra_lic_setup_intr),
	DEVMETHOD(pic_teardown_intr,	tegra_lic_teardown_intr),
	DEVMETHOD(pic_pre_ithread,	tegra_lic_pre_ithread),
	DEVMETHOD(pic_post_ithread,	tegra_lic_post_ithread),
	DEVMETHOD(pic_post_filter,	tegra_lic_post_filter),
#ifdef SMP
	DEVMETHOD(pic_bind_intr,	tegra_lic_bind_intr),
#endif
	DEVMETHOD_END
};

devclass_t tegra_lic_devclass;
static DEFINE_CLASS_0(lic, tegra_lic_driver, tegra_lic_methods,
    sizeof(struct tegra_lic_sc));
EARLY_DRIVER_MODULE(tegra_lic, simplebus, tegra_lic_driver, tegra_lic_devclass,
    NULL, NULL, BUS_PASS_INTERRUPT + BUS_PASS_ORDER_MIDDLE + 1);
