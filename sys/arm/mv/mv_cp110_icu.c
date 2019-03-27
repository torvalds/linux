/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2018 Rubicon Communications, LLC (Netgate)
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
 *
 * $FreeBSD$
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>

#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/rman.h>
#include <sys/lock.h>
#include <sys/mutex.h>

#include <machine/bus.h>
#include <machine/resource.h>
#include <machine/intr.h>

#include <dev/fdt/simplebus.h>

#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>

#include "pic_if.h"

#define	ICU_GRP_NSR		0x0
#define	ICU_GRP_SR		0x1
#define	ICU_GRP_SEI		0x4
#define	ICU_GRP_REI		0x5

#define	ICU_SETSPI_NSR_AL	0x10
#define	ICU_SETSPI_NSR_AH	0x14
#define	ICU_CLRSPI_NSR_AL	0x18
#define	ICU_CLRSPI_NSR_AH	0x1c
#define	ICU_INT_CFG(x)	(0x100 + (x) * 4)
#define	 ICU_INT_ENABLE		(1 << 24)
#define	 ICU_INT_EDGE		(1 << 28)
#define	 ICU_INT_GROUP_SHIFT	29
#define	 ICU_INT_MASK		0x3ff

#define	MV_CP110_ICU_MAX_NIRQS	207

struct mv_cp110_icu_softc {
	device_t		dev;
	device_t		parent;
	struct resource		*res;
};

static struct resource_spec mv_cp110_icu_res_spec[] = {
	{ SYS_RES_MEMORY,	0,	RF_ACTIVE | RF_SHAREABLE },
	{ -1, 0 }
};

static struct ofw_compat_data compat_data[] = {
	{"marvell,cp110-icu", 1},
	{NULL,             0}
};

#define	RD4(sc, reg)		bus_read_4((sc)->res, (reg))
#define	WR4(sc, reg, val)	bus_write_4((sc)->res, (reg), (val))

static int
mv_cp110_icu_probe(device_t dev)
{

	if (!ofw_bus_status_okay(dev))
		return (ENXIO);

	if (ofw_bus_search_compatible(dev, compat_data)->ocd_data == 0)
		return (ENXIO);

	device_set_desc(dev, "Marvell Interrupt Consolidation Unit");
	return (BUS_PROBE_DEFAULT);
}

static int
mv_cp110_icu_attach(device_t dev)
{
	struct mv_cp110_icu_softc *sc;
	phandle_t node, msi_parent;

	sc = device_get_softc(dev);
	sc->dev = dev;
	node = ofw_bus_get_node(dev);

	if (OF_getencprop(node, "msi-parent", &msi_parent,
	    sizeof(phandle_t)) <= 0) {
		device_printf(dev, "cannot find msi-parent property\n");
		return (ENXIO);
	}

	if ((sc->parent = OF_device_from_xref(msi_parent)) == NULL) {
		device_printf(dev, "cannot find msi-parent device\n");
		return (ENXIO);
	}
	if (bus_alloc_resources(dev, mv_cp110_icu_res_spec, &sc->res) != 0) {
		device_printf(dev, "cannot allocate resources for device\n");
		return (ENXIO);
	}

	if (intr_pic_register(dev, OF_xref_from_node(node)) == NULL) {
		device_printf(dev, "Cannot register ICU\n");
		goto fail;
	}
	return (0);

fail:
	bus_release_resources(dev, mv_cp110_icu_res_spec, &sc->res);
	return (ENXIO);
}

static int
mv_cp110_icu_detach(device_t dev)
{

	return (EBUSY);
}

static int
mv_cp110_icu_activate_intr(device_t dev, struct intr_irqsrc *isrc,
    struct resource *res, struct intr_map_data *data)
{
	struct mv_cp110_icu_softc *sc;

	sc = device_get_softc(dev);

	return (PIC_ACTIVATE_INTR(sc->parent, isrc, res, data));
}

static void
mv_cp110_icu_enable_intr(device_t dev, struct intr_irqsrc *isrc)
{
	struct mv_cp110_icu_softc *sc;

	sc = device_get_softc(dev);

	PIC_ENABLE_INTR(sc->parent, isrc);
}

static void
mv_cp110_icu_disable_intr(device_t dev, struct intr_irqsrc *isrc)
{
	struct mv_cp110_icu_softc *sc;

	sc = device_get_softc(dev);

	PIC_DISABLE_INTR(sc->parent, isrc);
}

static int
mv_cp110_icu_map_intr(device_t dev, struct intr_map_data *data,
    struct intr_irqsrc **isrcp)
{
	struct mv_cp110_icu_softc *sc;
	struct intr_map_data_fdt *daf;
	uint32_t reg;

	sc = device_get_softc(dev);

	if (data->type != INTR_MAP_DATA_FDT)
		return (ENOTSUP);

	daf = (struct intr_map_data_fdt *)data;
	if (daf->ncells != 3 || daf->cells[0] >= MV_CP110_ICU_MAX_NIRQS)
		return (EINVAL);

	reg = RD4(sc, ICU_INT_CFG(daf->cells[1]));

	if ((reg & ICU_INT_ENABLE) == 0) {
		reg |= ICU_INT_ENABLE;
		WR4(sc, ICU_INT_CFG(daf->cells[1]), reg);
	}

	daf->cells[1] = reg & ICU_INT_MASK;
	return (PIC_MAP_INTR(sc->parent, data, isrcp));
}

static int
mv_cp110_icu_deactivate_intr(device_t dev, struct intr_irqsrc *isrc,
    struct resource *res, struct intr_map_data *data)
{
	struct mv_cp110_icu_softc *sc;

	sc = device_get_softc(dev);

	return (PIC_DEACTIVATE_INTR(sc->parent, isrc, res, data));
}

static int
mv_cp110_icu_setup_intr(device_t dev, struct intr_irqsrc *isrc,
    struct resource *res, struct intr_map_data *data)
{
	struct mv_cp110_icu_softc *sc;

	sc = device_get_softc(dev);

	return (PIC_SETUP_INTR(sc->parent, isrc, res, data));
}

static int
mv_cp110_icu_teardown_intr(device_t dev, struct intr_irqsrc *isrc,
    struct resource *res, struct intr_map_data *data)
{
	struct mv_cp110_icu_softc *sc;

	sc = device_get_softc(dev);

	return (PIC_TEARDOWN_INTR(sc->parent, isrc, res, data));
}

static void
mv_cp110_icu_pre_ithread(device_t dev, struct intr_irqsrc *isrc)
{
	struct mv_cp110_icu_softc *sc;

	sc = device_get_softc(dev);

	PIC_PRE_ITHREAD(sc->parent, isrc);
}

static void
mv_cp110_icu_post_ithread(device_t dev, struct intr_irqsrc *isrc)
{
	struct mv_cp110_icu_softc *sc;

	sc = device_get_softc(dev);

	PIC_POST_ITHREAD(sc->parent, isrc);
}

static void
mv_cp110_icu_post_filter(device_t dev, struct intr_irqsrc *isrc)
{
	struct mv_cp110_icu_softc *sc;

	sc = device_get_softc(dev);

	PIC_POST_FILTER(sc->parent, isrc);
}

static device_method_t mv_cp110_icu_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		mv_cp110_icu_probe),
	DEVMETHOD(device_attach,	mv_cp110_icu_attach),
	DEVMETHOD(device_detach,	mv_cp110_icu_detach),

	/* Interrupt controller interface */
	DEVMETHOD(pic_activate_intr,	mv_cp110_icu_activate_intr),
	DEVMETHOD(pic_disable_intr,	mv_cp110_icu_disable_intr),
	DEVMETHOD(pic_enable_intr,	mv_cp110_icu_enable_intr),
	DEVMETHOD(pic_map_intr,		mv_cp110_icu_map_intr),
	DEVMETHOD(pic_deactivate_intr,	mv_cp110_icu_deactivate_intr),
	DEVMETHOD(pic_setup_intr,	mv_cp110_icu_setup_intr),
	DEVMETHOD(pic_teardown_intr,	mv_cp110_icu_teardown_intr),
	DEVMETHOD(pic_post_filter,	mv_cp110_icu_post_filter),
	DEVMETHOD(pic_post_ithread,	mv_cp110_icu_post_ithread),
	DEVMETHOD(pic_pre_ithread,	mv_cp110_icu_pre_ithread),

	DEVMETHOD_END
};

static devclass_t mv_cp110_icu_devclass;

static driver_t mv_cp110_icu_driver = {
	"mv_cp110_icu",
	mv_cp110_icu_methods,
	sizeof(struct mv_cp110_icu_softc),
};

EARLY_DRIVER_MODULE(mv_cp110_icu, simplebus, mv_cp110_icu_driver,
    mv_cp110_icu_devclass, 0, 0, BUS_PASS_INTERRUPT + BUS_PASS_ORDER_LAST);
