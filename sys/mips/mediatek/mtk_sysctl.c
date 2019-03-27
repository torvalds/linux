/*-
 * Copyright (c) 2016 Stanislav Galabov.
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
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/interrupt.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/rman.h>
#include <sys/malloc.h>

#include <machine/fdt.h>

#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>

#include <mips/mediatek/mtk_sysctl.h>

#include <dev/fdt/fdt_common.h>

struct mtk_sysctl_softc {
	device_t	dev;
	struct resource	*mem_res;
	int		mem_rid;
	struct mtx	mtx;
};

static struct mtk_sysctl_softc *mtk_sysctl_sc = NULL;

static struct ofw_compat_data compat_data[] = {
	{ "ralink,rt2880-sysc",		1 },
	{ "ralink,rt3050-sysc",		1 },
	{ "ralink,rt3352-sysc",		1 },
	{ "ralink,rt3883-sysc",		1 },
	{ "ralink,rt5350-sysc",		1 },
	{ "ralink,mt7620a-sysc",	1 },
	{ "mtk,mt7621-sysc",		1 },

	/* Sentinel */
	{ NULL,				0 }
};

#define MTK_SYSCTL_LOCK(sc)		mtx_lock_spin(&(sc)->mtx)
#define MTK_SYSCTL_UNLOCK(sc)		mtx_unlock_spin(&(sc)->mtx)
#define MTK_SYSCTL_LOCK_INIT(sc)		\
    mtx_init(&(sc)->mtx, device_get_nameunit((sc)->dev),	\
    "mtk_sysctl", MTX_SPIN)
#define MTK_SYSCTL_LOCK_DESTROY(sc)	mtx_destroy(&(sc)->mtx)

static int
mtk_sysctl_probe(device_t dev)
{

	if (!ofw_bus_status_okay(dev))
		return (ENXIO);

	if (ofw_bus_search_compatible(dev, compat_data)->ocd_data == 0)
		return (ENXIO);

	device_set_desc(dev, "MTK System Controller");

	return (BUS_PROBE_DEFAULT);
}

static int mtk_sysctl_detach(device_t);

static int
mtk_sysctl_attach(device_t dev)
{
	struct mtk_sysctl_softc *sc = device_get_softc(dev);

	if (device_get_unit(dev) != 0 || mtk_sysctl_sc != NULL) {
		device_printf(dev, "Only one sysctl module supported\n");
		return (ENXIO);
	}

	mtk_sysctl_sc = sc;

	/* Map control/status registers. */
	sc->mem_rid = 0;
	sc->mem_res = bus_alloc_resource_any(dev, SYS_RES_MEMORY,
	    &sc->mem_rid, RF_ACTIVE);

	if (sc->mem_res == NULL) {
		device_printf(dev, "couldn't map memory\n");
		mtk_sysctl_detach(dev);
		return (ENXIO);
	}

	sc->dev = dev;

	MTK_SYSCTL_LOCK_INIT(sc);

	return (0);
}

static int
mtk_sysctl_detach(device_t dev)
{
	struct mtk_sysctl_softc *sc = device_get_softc(dev);

	if (sc->mem_res)
		bus_release_resource(dev, SYS_RES_MEMORY, sc->mem_rid,
		    sc->mem_res);

	MTK_SYSCTL_LOCK_DESTROY(sc);

	return(0);
}

uint32_t
mtk_sysctl_get(uint32_t reg)
{
	uint32_t val;

	MTK_SYSCTL_LOCK(mtk_sysctl_sc);
	val = bus_read_4(mtk_sysctl_sc->mem_res, reg);
	MTK_SYSCTL_UNLOCK(mtk_sysctl_sc);

	return (val);
}

void
mtk_sysctl_set(uint32_t reg, uint32_t val)
{

	MTK_SYSCTL_LOCK(mtk_sysctl_sc);
	bus_write_4(mtk_sysctl_sc->mem_res, reg, val);
	MTK_SYSCTL_UNLOCK(mtk_sysctl_sc);
}

void
mtk_sysctl_clr_set(uint32_t reg, uint32_t clr, uint32_t set)
{
	uint32_t val;

	MTK_SYSCTL_LOCK(mtk_sysctl_sc);
	val = bus_read_4(mtk_sysctl_sc->mem_res, reg);
	val &= ~(clr);
	val |= set;
	bus_write_4(mtk_sysctl_sc->mem_res, reg, val);
	MTK_SYSCTL_UNLOCK(mtk_sysctl_sc);
}

static device_method_t mtk_sysctl_methods[] = {
	DEVMETHOD(device_probe,		mtk_sysctl_probe),
	DEVMETHOD(device_attach,	mtk_sysctl_attach),
	DEVMETHOD(device_detach,	mtk_sysctl_detach),

	DEVMETHOD_END
};

static driver_t mtk_sysctl_driver = {
	"sysc",
	mtk_sysctl_methods,
	sizeof(struct mtk_sysctl_softc),
};
static devclass_t mtk_sysctl_devclass;

EARLY_DRIVER_MODULE(mtk_sysctl, simplebus, mtk_sysctl_driver,
    mtk_sysctl_devclass, 0, 0, BUS_PASS_INTERRUPT + BUS_PASS_ORDER_EARLY);
