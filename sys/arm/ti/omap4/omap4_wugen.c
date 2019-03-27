/*-
 * Copyright (c) 2016 Svatopluk Kraus
 * Copyright (c) 2016 Michal Meloun
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

#include <sys/param.h>
#include <sys/bus.h>
#include <sys/conf.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/rman.h>
#include <sys/systm.h>

#include <machine/fdt.h>
#include <machine/intr.h>
#include <machine/resource.h>

#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>

#include "pic_if.h"

static struct ofw_compat_data compat_data[] = {
	{"ti,omap4-wugen-mpu", 	1},
	{NULL,			0}
};

struct omap4_wugen_sc {
	device_t		sc_dev;
	struct resource		*sc_mem_res;
	device_t		sc_parent;
};

static int
omap4_wugen_activate_intr(device_t dev, struct intr_irqsrc *isrc,
    struct resource *res, struct intr_map_data *data)
{
	struct omap4_wugen_sc *sc = device_get_softc(dev);

	return (PIC_ACTIVATE_INTR(sc->sc_parent, isrc, res, data));
}

static void
omap4_wugen_disable_intr(device_t dev, struct intr_irqsrc *isrc)
{
	struct omap4_wugen_sc *sc = device_get_softc(dev);

	PIC_DISABLE_INTR(sc->sc_parent, isrc);
}

static void
omap4_wugen_enable_intr(device_t dev, struct intr_irqsrc *isrc)
{
	struct omap4_wugen_sc *sc = device_get_softc(dev);

	PIC_ENABLE_INTR(sc->sc_parent, isrc);
}

static int
omap4_wugen_map_intr(device_t dev, struct intr_map_data *data,
    struct intr_irqsrc **isrcp)
{
	struct omap4_wugen_sc *sc = device_get_softc(dev);

	return (PIC_MAP_INTR(sc->sc_parent, data, isrcp));
}

static int
omap4_wugen_deactivate_intr(device_t dev, struct intr_irqsrc *isrc,
    struct resource *res, struct intr_map_data *data)
{
	struct omap4_wugen_sc *sc = device_get_softc(dev);

	return (PIC_DEACTIVATE_INTR(sc->sc_parent, isrc, res, data));
}

static int
omap4_wugen_setup_intr(device_t dev, struct intr_irqsrc *isrc,
    struct resource *res, struct intr_map_data *data)
{
	struct omap4_wugen_sc *sc = device_get_softc(dev);

	return (PIC_SETUP_INTR(sc->sc_parent, isrc, res, data));
}

static int
omap4_wugen_teardown_intr(device_t dev, struct intr_irqsrc *isrc,
    struct resource *res, struct intr_map_data *data)
{
	struct omap4_wugen_sc *sc = device_get_softc(dev);

	return (PIC_TEARDOWN_INTR(sc->sc_parent, isrc, res, data));
}

static void
omap4_wugen_pre_ithread(device_t dev, struct intr_irqsrc *isrc)
{
	struct omap4_wugen_sc *sc = device_get_softc(dev);

	PIC_PRE_ITHREAD(sc->sc_parent, isrc);
}


static void
omap4_wugen_post_ithread(device_t dev, struct intr_irqsrc *isrc)
{
	struct omap4_wugen_sc *sc = device_get_softc(dev);

	PIC_POST_ITHREAD(sc->sc_parent, isrc);
}

static void
omap4_wugen_post_filter(device_t dev, struct intr_irqsrc *isrc)
{
	struct omap4_wugen_sc *sc = device_get_softc(dev);

	PIC_POST_FILTER(sc->sc_parent, isrc);
}

#ifdef SMP
static int
omap4_wugen_bind_intr(device_t dev, struct intr_irqsrc *isrc)
{
	struct omap4_wugen_sc *sc = device_get_softc(dev);

	return (PIC_BIND_INTR(sc->sc_parent, isrc));
}
#endif

static int
omap4_wugen_probe(device_t dev)
{

	if (!ofw_bus_status_okay(dev))
		return (ENXIO);

	if (ofw_bus_search_compatible(dev, compat_data)->ocd_data == 0)
		return (ENXIO);

	return (BUS_PROBE_DEFAULT);
}

static int
omap4_wugen_detach(device_t dev)
{
	struct omap4_wugen_sc *sc;

	sc = device_get_softc(dev);
	if (sc->sc_mem_res != NULL) {
		bus_release_resource(dev, SYS_RES_MEMORY, 0, sc->sc_mem_res);
		sc->sc_mem_res = NULL;
	}
	return (0);
}

static int
omap4_wugen_attach(device_t dev)
{
	struct omap4_wugen_sc *sc;
	phandle_t node;
	phandle_t parent_xref;
	int rid, rv;

	sc = device_get_softc(dev);
	sc->sc_dev = dev;
	node = ofw_bus_get_node(dev);

	rv = OF_getencprop(node, "interrupt-parent", &parent_xref,
	    sizeof(parent_xref));
	if (rv <= 0) {
		device_printf(dev, "can't read parent node property\n");
		goto fail;
	}
	sc->sc_parent = OF_device_from_xref(parent_xref);
	if (sc->sc_parent == NULL) {
		device_printf(dev, "can't find parent controller\n");
		goto fail;
	}

	rid = 0;
	sc->sc_mem_res = bus_alloc_resource_any(dev, SYS_RES_MEMORY, &rid,
	    RF_ACTIVE);
	if (sc->sc_mem_res == NULL) {
		device_printf(dev, "can't allocate resources\n");
		return (ENXIO);
	}

	if (intr_pic_register(dev, OF_xref_from_node(node)) == NULL) {
		device_printf(dev, "can't register PIC\n");
		goto fail;
	}
	return (0);

fail:
	omap4_wugen_detach(dev);
	return (ENXIO);
}

static device_method_t omap4_wugen_methods[] = {
	DEVMETHOD(device_probe,		omap4_wugen_probe),
	DEVMETHOD(device_attach,	omap4_wugen_attach),
	DEVMETHOD(device_detach,	omap4_wugen_detach),

	/* Interrupt controller interface */
	DEVMETHOD(pic_activate_intr,	omap4_wugen_activate_intr),
	DEVMETHOD(pic_disable_intr,	omap4_wugen_disable_intr),
	DEVMETHOD(pic_enable_intr,	omap4_wugen_enable_intr),
	DEVMETHOD(pic_map_intr,		omap4_wugen_map_intr),
	DEVMETHOD(pic_deactivate_intr,	omap4_wugen_deactivate_intr),
	DEVMETHOD(pic_setup_intr,	omap4_wugen_setup_intr),
	DEVMETHOD(pic_teardown_intr,	omap4_wugen_teardown_intr),
	DEVMETHOD(pic_pre_ithread,	omap4_wugen_pre_ithread),
	DEVMETHOD(pic_post_ithread,	omap4_wugen_post_ithread),
	DEVMETHOD(pic_post_filter,	omap4_wugen_post_filter),
#ifdef SMP
	DEVMETHOD(pic_bind_intr,	omap4_wugen_bind_intr),
#endif
	DEVMETHOD_END
};
devclass_t omap4_wugen_devclass;
DEFINE_CLASS_0(omap4_wugen, omap4_wugen_driver, omap4_wugen_methods,
    sizeof(struct omap4_wugen_sc));
EARLY_DRIVER_MODULE(omap4_wugen, simplebus, omap4_wugen_driver,
    omap4_wugen_devclass, NULL, NULL,
    BUS_PASS_INTERRUPT + BUS_PASS_ORDER_MIDDLE + 1);
