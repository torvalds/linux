/*-
 * Copyright (c) 2015,2016 Annapurna Labs Ltd. and affiliates
 * All rights reserved.
 *
 * Developed by Semihalf.
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

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/bus.h>
#include <sys/rman.h>
#include <sys/conf.h>
#include <sys/resource.h>

#include <machine/bus.h>
#include <dev/ofw/ofw_bus_subr.h>

#include "al_serdes.h"
#include "alpine_serdes.h"

#define SERDES_NUM_GROUPS	5

static void *serdes_base;
static uint32_t serdes_grp_offset[] = {0, 0x400, 0x800, 0xc00, 0x2000};

static struct alpine_serdes_eth_group_mode {
	struct mtx			lock;
	enum alpine_serdes_eth_mode	mode;
	bool				mode_set;
} alpine_serdes_eth_group_mode[SERDES_NUM_GROUPS];

static int al_serdes_probe(device_t dev);
static int al_serdes_attach(device_t dev);
static int al_serdes_detach(device_t dev);

static struct resource_spec al_serdes_spec[] = {
	{ SYS_RES_MEMORY,	0,	RF_ACTIVE },
	{ -1, 0 }
};

struct al_serdes_softc {
	struct resource *res;
};

static device_method_t al_serdes_methods[] = {
	DEVMETHOD(device_probe,		al_serdes_probe),
	DEVMETHOD(device_attach,	al_serdes_attach),
	DEVMETHOD(device_detach,	al_serdes_detach),

	DEVMETHOD_END
};

static driver_t al_serdes_driver = {
	"serdes",
	al_serdes_methods,
	sizeof(struct al_serdes_softc)
};

static devclass_t al_serdes_devclass;

DRIVER_MODULE(al_serdes, simplebus, al_serdes_driver,
    al_serdes_devclass, 0, 0);
DRIVER_MODULE(al_serdes, ofwbus, al_serdes_driver,
    al_serdes_devclass, 0, 0);

static int
al_serdes_probe(device_t dev)
{

	if (!ofw_bus_status_okay(dev))
		return (ENXIO);

	if (!ofw_bus_is_compatible(dev, "annapurna-labs,al-serdes"))
		return (ENXIO);

	device_set_desc(dev, "Alpine Serdes");

	return (BUS_PROBE_DEFAULT);
}

static int
al_serdes_attach(device_t dev)
{
	struct al_serdes_softc *sc;
	int err;

	sc = device_get_softc(dev);

	err = bus_alloc_resources(dev, al_serdes_spec, &sc->res);
	if (err != 0) {
		device_printf(dev, "could not allocate resources\n");
		return (err);
	}

	/* Initialize Serdes group locks and mode */
	for (int i = 0; i < nitems(alpine_serdes_eth_group_mode); i++) {
		mtx_init(&alpine_serdes_eth_group_mode[i].lock, "AlSerdesMtx",
		    NULL, MTX_DEF);
		alpine_serdes_eth_group_mode[i].mode_set = false;
	}

	serdes_base = (void *)rman_get_bushandle(sc->res);

	return (0);
}

static int
al_serdes_detach(device_t dev)
{
	struct al_serdes_softc *sc;

	sc = device_get_softc(dev);

	bus_release_resources(dev, al_serdes_spec, &sc->res);

	for (int i = 0; i < nitems(alpine_serdes_eth_group_mode); i++) {
		mtx_destroy(&alpine_serdes_eth_group_mode[i].lock);
		alpine_serdes_eth_group_mode[i].mode_set = false;
	}

	return (0);
}

void *
alpine_serdes_resource_get(uint32_t group)
{
	void *base;

	base = NULL;
	if (group >= SERDES_NUM_GROUPS)
		return (NULL);

	if (serdes_base != NULL)
		base = (void *)((uintptr_t)serdes_base +
		    serdes_grp_offset[group]);

	return (base);
}

int
alpine_serdes_eth_mode_set(uint32_t group, enum alpine_serdes_eth_mode mode)
{
	struct alpine_serdes_eth_group_mode *group_mode;

	group_mode = &alpine_serdes_eth_group_mode[group];

	if (serdes_base == NULL)
		return (EINVAL);

	if (group >= SERDES_NUM_GROUPS)
		return (EINVAL);

	mtx_lock(&group_mode->lock);

	if (!group_mode->mode_set || (group_mode->mode != mode)) {
		struct al_serdes_grp_obj obj;

		al_serdes_handle_grp_init(alpine_serdes_resource_get(group),
		    group, &obj);

		if (mode == ALPINE_SERDES_ETH_MODE_SGMII)
			obj.mode_set_sgmii(&obj);
		else
			obj.mode_set_kr(&obj);

		group_mode->mode = mode;
		group_mode->mode_set = true;
	}

	mtx_unlock(&group_mode->lock);

	return (0);
}

void
alpine_serdes_eth_group_lock(uint32_t group)
{
	struct alpine_serdes_eth_group_mode *group_mode;

	group_mode = &alpine_serdes_eth_group_mode[group];

	if (mtx_initialized(&group_mode->lock) == 0)
		return;

	mtx_lock(&group_mode->lock);
}

void
alpine_serdes_eth_group_unlock(uint32_t group)
{
	struct alpine_serdes_eth_group_mode *group_mode;

	group_mode = &alpine_serdes_eth_group_mode[group];

	if (mtx_initialized(&group_mode->lock) == 0)
		return;

	mtx_unlock(&group_mode->lock);
}
