/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2011 Justin Hibbits
 * Copyright (c) 2009 Nathan Whitehorn
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
#include <sys/cpu.h>
#include <sys/kernel.h>
#include <sys/limits.h>
#include <sys/module.h>

#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/openfirm.h>

#include "cpufreq_if.h"
#include "powerpc/powermac/pmuvar.h"

struct pmufreq_softc {
	device_t dev;
	uint32_t minfreq;
	uint32_t maxfreq;
	uint32_t curfreq;
};

static void	pmufreq_identify(driver_t *driver, device_t parent);
static int	pmufreq_probe(device_t dev);
static int	pmufreq_attach(device_t dev);
static int	pmufreq_settings(device_t dev, struct cf_setting *sets, int *count);
static int	pmufreq_set(device_t dev, const struct cf_setting *set);
static int	pmufreq_get(device_t dev, struct cf_setting *set);
static int	pmufreq_type(device_t dev, int *type);

static device_method_t pmufreq_methods[] = {
	/* Device interface */
	DEVMETHOD(device_identify,	pmufreq_identify),
	DEVMETHOD(device_probe,		pmufreq_probe),
	DEVMETHOD(device_attach,	pmufreq_attach),

	/* cpufreq interface */
	DEVMETHOD(cpufreq_drv_set,	pmufreq_set),
	DEVMETHOD(cpufreq_drv_get,	pmufreq_get),
	DEVMETHOD(cpufreq_drv_type,	pmufreq_type),
	DEVMETHOD(cpufreq_drv_settings,	pmufreq_settings),

	{0, 0}
};

static driver_t pmufreq_driver = {
	"pmufreq",
	pmufreq_methods,
	sizeof(struct pmufreq_softc)
};

static devclass_t pmufreq_devclass;
DRIVER_MODULE(pmufreq, cpu, pmufreq_driver, pmufreq_devclass, 0, 0);

static void
pmufreq_identify(driver_t *driver, device_t parent)
{
	phandle_t node;
	uint32_t min_freq;

	node = ofw_bus_get_node(parent);
	if (OF_getprop(node, "min-clock-frequency", &min_freq, sizeof(min_freq)) == -1)
		return;

	/* Make sure we're not being doubly invoked. */
	if (device_find_child(parent, "pmufreq", -1) != NULL)
		return;

	/*
	 * We attach a child for every CPU since settings need to
	 * be performed on every CPU in the SMP case.
	 */
	if (BUS_ADD_CHILD(parent, 10, "pmufreq", -1) == NULL)
		device_printf(parent, "add pmufreq child failed\n");
}

static int
pmufreq_probe(device_t dev)
{
	struct pmufreq_softc *sc;
	phandle_t node;
	uint32_t min_freq;

	if (resource_disabled("pmufreq", 0))
		return (ENXIO);

	sc = device_get_softc(dev);
	node = ofw_bus_get_node(device_get_parent(dev));
	/*
	 * A scalable MPC7455 has min-clock-frequency/max-clock-frequency as OFW
	 * properties of the 'cpu' node.
	 */
	if (OF_getprop(node, "min-clock-frequency", &min_freq, sizeof(min_freq)) == -1)
		return (ENXIO);
	device_set_desc(dev, "PMU-based frequency scaling");
	return (0);
}

static int
pmufreq_attach(device_t dev)
{
	struct pmufreq_softc *sc;
	phandle_t node;

	sc = device_get_softc(dev);
	sc->dev = dev;

	node = ofw_bus_get_node(device_get_parent(dev));
	OF_getprop(node, "min-clock-frequency", &sc->minfreq, sizeof(sc->minfreq));
	OF_getprop(node, "max-clock-frequency", &sc->maxfreq, sizeof(sc->maxfreq));
	OF_getprop(node, "rounded-clock-frequency", &sc->curfreq, sizeof(sc->curfreq));
	sc->minfreq /= 1000000;
	sc->maxfreq /= 1000000;
	sc->curfreq /= 1000000;

	cpufreq_register(dev);
	return (0);
}

static int
pmufreq_settings(device_t dev, struct cf_setting *sets, int *count)
{
	struct pmufreq_softc *sc;

	sc = device_get_softc(dev);
	if (sets == NULL || count == NULL)
		return (EINVAL);
	if (*count < 2)
		return (E2BIG);

	/* Return a list of valid settings for this driver. */
	memset(sets, CPUFREQ_VAL_UNKNOWN, sizeof(*sets) * 2);

	sets[0].freq = sc->maxfreq; sets[0].dev = dev;
	sets[1].freq = sc->minfreq; sets[1].dev = dev;
	/* Set high latency for CPU frequency changes, it's a tedious process. */
	sets[0].lat = INT_MAX;
	sets[1].lat = INT_MAX;
	*count = 2;

	return (0);
}

static int
pmufreq_set(device_t dev, const struct cf_setting *set)
{
	struct pmufreq_softc *sc;
	int error, speed_sel;

	if (set == NULL)
		return (EINVAL);

	sc = device_get_softc(dev);

	if (set->freq == sc->maxfreq)
		speed_sel = 0;
	else
		speed_sel = 1;

	error = pmu_set_speed(speed_sel);
	if (error == 0)
		sc->curfreq = set->freq;

	return (error);
}

static int
pmufreq_get(device_t dev, struct cf_setting *set)
{
	struct pmufreq_softc *sc;

	if (set == NULL)
		return (EINVAL);
	sc = device_get_softc(dev);

	set->freq = sc->curfreq;
	set->dev = dev;

	return (0);
}

static int
pmufreq_type(device_t dev, int *type)
{

	if (type == NULL)
		return (EINVAL);

	*type = CPUFREQ_TYPE_ABSOLUTE;
	return (0);
}

