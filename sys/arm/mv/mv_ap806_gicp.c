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

#define	MV_AP806_GICP_MAX_NIRQS	207

struct mv_ap806_gicp_softc {
	device_t		dev;
	device_t		parent;
	struct resource		*res;

	ssize_t			spi_ranges_cnt;
	uint32_t		*spi_ranges;
};

static struct ofw_compat_data compat_data[] = {
	{"marvell,ap806-gicp", 1},
	{NULL,             0}
};

#define	RD4(sc, reg)		bus_read_4((sc)->res, (reg))
#define	WR4(sc, reg, val)	bus_write_4((sc)->res, (reg), (val))

static int
mv_ap806_gicp_probe(device_t dev)
{

	if (!ofw_bus_status_okay(dev))
		return (ENXIO);

	if (ofw_bus_search_compatible(dev, compat_data)->ocd_data == 0)
		return (ENXIO);

	device_set_desc(dev, "Marvell GICP");
	return (BUS_PROBE_DEFAULT);
}

static int
mv_ap806_gicp_attach(device_t dev)
{
	struct mv_ap806_gicp_softc *sc;
	phandle_t node, xref, intr_parent;

	sc = device_get_softc(dev);
	sc->dev = dev;
	node = ofw_bus_get_node(dev);

	/* Look for our parent */
	if ((intr_parent = ofw_bus_find_iparent(node)) == 0) {
		device_printf(dev, "Cannot find our parent interrupt controller\n");
		return (ENXIO);
	}
	if ((sc->parent = OF_device_from_xref(intr_parent)) == NULL) {
		device_printf(dev, "cannot find parent interrupt controller device\n");
		return (ENXIO);
	}

	sc->spi_ranges_cnt = OF_getencprop_alloc(node, "marvell,spi-ranges",
	    (void **)&sc->spi_ranges);

	xref = OF_xref_from_node(node);
	if (intr_pic_register(dev, xref) == NULL) {
		device_printf(dev, "Cannot register GICP\n");
		return (ENXIO);
	}

	OF_device_register_xref(xref, dev);

	return (0);
}

static int
mv_ap806_gicp_detach(device_t dev)
{

	return (EBUSY);
}

static int
mv_ap806_gicp_activate_intr(device_t dev, struct intr_irqsrc *isrc,
    struct resource *res, struct intr_map_data *data)
{
	struct mv_ap806_gicp_softc *sc;

	sc = device_get_softc(dev);

	return (PIC_ACTIVATE_INTR(sc->parent, isrc, res, data));
}

static void
mv_ap806_gicp_enable_intr(device_t dev, struct intr_irqsrc *isrc)
{
	struct mv_ap806_gicp_softc *sc;

	sc = device_get_softc(dev);

	PIC_ENABLE_INTR(sc->parent, isrc);
}

static void
mv_ap806_gicp_disable_intr(device_t dev, struct intr_irqsrc *isrc)
{
	struct mv_ap806_gicp_softc *sc;

	sc = device_get_softc(dev);

	PIC_DISABLE_INTR(sc->parent, isrc);
}

static int
mv_ap806_gicp_map_intr(device_t dev, struct intr_map_data *data,
    struct intr_irqsrc **isrcp)
{
	struct mv_ap806_gicp_softc *sc;
	struct intr_map_data_fdt *daf;
	uint32_t group, irq_num, irq_type;
	int i;

	sc = device_get_softc(dev);

	if (data->type != INTR_MAP_DATA_FDT)
		return (ENOTSUP);

	daf = (struct intr_map_data_fdt *)data;
	if (daf->ncells != 3 || daf->cells[0] >= MV_AP806_GICP_MAX_NIRQS)
		return (EINVAL);

	group = daf->cells[0];
	irq_num = daf->cells[1];
	irq_type = daf->cells[2];

	/* Map the interrupt number to spi number */
	for (i = 0; i < sc->spi_ranges_cnt / 2; i += 2) {
		if (irq_num < sc->spi_ranges[i + 1]) {
			irq_num += sc->spi_ranges[i];
			break;
		}

		irq_num -= sc->spi_ranges[i];
	}

	daf->cells[1] = irq_num - 32;

	return (PIC_MAP_INTR(sc->parent, data, isrcp));
}

static int
mv_ap806_gicp_deactivate_intr(device_t dev, struct intr_irqsrc *isrc,
    struct resource *res, struct intr_map_data *data)
{
	struct mv_ap806_gicp_softc *sc;

	sc = device_get_softc(dev);

	return (PIC_DEACTIVATE_INTR(sc->parent, isrc, res, data));
}

static int
mv_ap806_gicp_setup_intr(device_t dev, struct intr_irqsrc *isrc,
    struct resource *res, struct intr_map_data *data)
{
	struct mv_ap806_gicp_softc *sc;

	sc = device_get_softc(dev);

	return (PIC_SETUP_INTR(sc->parent, isrc, res, data));
}

static int
mv_ap806_gicp_teardown_intr(device_t dev, struct intr_irqsrc *isrc,
    struct resource *res, struct intr_map_data *data)
{
	struct mv_ap806_gicp_softc *sc;

	sc = device_get_softc(dev);

	return (PIC_TEARDOWN_INTR(sc->parent, isrc, res, data));
}

static void
mv_ap806_gicp_pre_ithread(device_t dev, struct intr_irqsrc *isrc)
{
	struct mv_ap806_gicp_softc *sc;

	sc = device_get_softc(dev);

	PIC_PRE_ITHREAD(sc->parent, isrc);
}

static void
mv_ap806_gicp_post_ithread(device_t dev, struct intr_irqsrc *isrc)
{
	struct mv_ap806_gicp_softc *sc;

	sc = device_get_softc(dev);

	PIC_POST_ITHREAD(sc->parent, isrc);
}

static void
mv_ap806_gicp_post_filter(device_t dev, struct intr_irqsrc *isrc)
{
	struct mv_ap806_gicp_softc *sc;

	sc = device_get_softc(dev);

	PIC_POST_FILTER(sc->parent, isrc);
}

static device_method_t mv_ap806_gicp_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		mv_ap806_gicp_probe),
	DEVMETHOD(device_attach,	mv_ap806_gicp_attach),
	DEVMETHOD(device_detach,	mv_ap806_gicp_detach),

	/* Interrupt controller interface */
	DEVMETHOD(pic_activate_intr,	mv_ap806_gicp_activate_intr),
	DEVMETHOD(pic_disable_intr,	mv_ap806_gicp_disable_intr),
	DEVMETHOD(pic_enable_intr,	mv_ap806_gicp_enable_intr),
	DEVMETHOD(pic_map_intr,		mv_ap806_gicp_map_intr),
	DEVMETHOD(pic_deactivate_intr,	mv_ap806_gicp_deactivate_intr),
	DEVMETHOD(pic_setup_intr,	mv_ap806_gicp_setup_intr),
	DEVMETHOD(pic_teardown_intr,	mv_ap806_gicp_teardown_intr),
	DEVMETHOD(pic_post_filter,	mv_ap806_gicp_post_filter),
	DEVMETHOD(pic_post_ithread,	mv_ap806_gicp_post_ithread),
	DEVMETHOD(pic_pre_ithread,	mv_ap806_gicp_pre_ithread),

	DEVMETHOD_END
};

static devclass_t mv_ap806_gicp_devclass;

static driver_t mv_ap806_gicp_driver = {
	"mv_ap806_gicp",
	mv_ap806_gicp_methods,
	sizeof(struct mv_ap806_gicp_softc),
};

EARLY_DRIVER_MODULE(mv_ap806_gicp, simplebus, mv_ap806_gicp_driver,
    mv_ap806_gicp_devclass, 0, 0, BUS_PASS_INTERRUPT + BUS_PASS_ORDER_MIDDLE);
