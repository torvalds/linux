/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2015-2016 Landon Fuller <landon@landonf.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer,
 *    without modification.
 * 2. Redistributions in binary form must reproduce at minimum a disclaimer
 *    similar to the "NO WARRANTY" disclaimer below ("Disclaimer") and any
 *    redistribution must be conditioned upon including a substantially
 *    similar Disclaimer requirement for further binary redistribution.
 *
 * NO WARRANTY
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF NONINFRINGEMENT, MERCHANTIBILITY
 * AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
 * THE COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR SPECIAL, EXEMPLARY,
 * OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
 * IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGES.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/bus.h>
#include <sys/module.h>
#include <sys/mutex.h>
#include <sys/systm.h>

#include <machine/bus.h>
#include <machine/resource.h>

#include <dev/bhnd/bhnd.h>

#include "bhnd_pmureg.h"
#include "bhnd_pmuvar.h"

/*
 * PMU core driver.
 */

/* Supported device identifiers */
static const struct bhnd_device bhnd_pmucore_devices[] = {
	BHND_DEVICE(BCM, PMU, NULL, NULL),

	BHND_DEVICE_END
};

static int
bhnd_pmu_core_probe(device_t dev)
{
	const struct bhnd_device	*id;
	int				 error;

	id = bhnd_device_lookup(dev, bhnd_pmucore_devices,
	     sizeof(bhnd_pmucore_devices[0]));
	if (id == NULL)
		return (ENXIO);

	/* Delegate to common driver implementation */
	if ((error = bhnd_pmu_probe(dev)) > 0)
		return (error);

	bhnd_set_default_core_desc(dev);
	return (BUS_PROBE_DEFAULT);
}

static int
bhnd_pmu_core_attach(device_t dev)
{
	struct bhnd_pmu_softc	*sc;
	struct bhnd_resource	*res;
	int			 error;
	int			 rid;

	sc = device_get_softc(dev);

	/* Allocate register block */
	rid = 0;
	res = bhnd_alloc_resource_any(dev, SYS_RES_MEMORY, &rid, RF_ACTIVE);
	if (res == NULL) {
		device_printf(dev, "failed to allocate resources\n");
		return (ENXIO);
	}

	/* Allocate our per-core PMU state */
	if ((error = bhnd_alloc_pmu(dev))) {
		device_printf(sc->dev, "failed to allocate PMU state: %d\n",
		    error);

		return (error);
	}

	/* Delegate to common driver implementation */
	if ((error = bhnd_pmu_attach(dev, res))) {
		bhnd_release_resource(dev, SYS_RES_MEMORY, rid, res);
		return (error);
	}

	sc->rid = rid;
	return (0);
}

static int
bhnd_pmu_core_detach(device_t dev)
{
	struct bhnd_pmu_softc	*sc;
	int			 error;

	sc = device_get_softc(dev);
	
	/* Delegate to common driver implementation */
	if ((error = bhnd_pmu_detach(dev)))
		return (error);

	bhnd_release_resource(dev, SYS_RES_MEMORY, sc->rid, sc->res);
	return (0);
}

static device_method_t bhnd_pmucore_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		bhnd_pmu_core_probe),
	DEVMETHOD(device_attach,	bhnd_pmu_core_attach),
	DEVMETHOD(device_detach,	bhnd_pmu_core_detach),

	DEVMETHOD_END
};

DEFINE_CLASS_1(bhnd_pmu, bhnd_pmucore_driver, bhnd_pmucore_methods,
    sizeof(struct bhnd_pmu_softc), bhnd_pmu_driver);
EARLY_DRIVER_MODULE(bhnd_pmu, bhnd, bhnd_pmucore_driver, bhnd_pmu_devclass,
    NULL, NULL, BUS_PASS_TIMER + BUS_PASS_ORDER_MIDDLE);

MODULE_DEPEND(bhnd_pmu_core, bhnd_pmu, 1, 1, 1);
MODULE_VERSION(bhnd_pmu_core, 1);
