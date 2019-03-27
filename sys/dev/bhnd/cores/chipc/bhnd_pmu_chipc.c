/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2016 Landon Fuller <landon@landonf.org>
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

/*
 * ChipCommon attachment support for the bhnd(4) PMU driver.
 * 
 * Supports non-AOB ("Always-on Bus") devices that map the PMU register blocks
 * via the ChipCommon core, rather than vending a distinct PMU core on the
 * bhnd bus.
 */

#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/bus.h>
#include <sys/limits.h>
#include <sys/malloc.h>
#include <sys/module.h>
#include <sys/systm.h>

#include <dev/bhnd/bhnd.h>

#include <dev/bhnd/cores/pmu/bhnd_pmuvar.h>
#include <dev/bhnd/cores/pmu/bhnd_pmureg.h>

#include "bhnd_chipc_if.h"
#include "bhnd_pmu_if.h"

#include "chipcvar.h"

static int
bhnd_pmu_chipc_probe(device_t dev)
{
	struct bhnd_pmu_softc	*sc;
	struct chipc_caps	*ccaps;
	struct chipc_softc	*chipc_sc;
	device_t		 chipc;
	char			 desc[34];
	int			 error;
	uint32_t		 pcaps;
	uint8_t			 rev;

	sc = device_get_softc(dev);

	/* Look for chipc parent */
	chipc = device_get_parent(dev);
	if (device_get_devclass(chipc) != devclass_find("bhnd_chipc"))
		return (ENXIO);

	/* Check the chipc PMU capability flag. */
	ccaps = BHND_CHIPC_GET_CAPS(chipc);
	if (!ccaps->pmu)
		return (ENXIO);

	/* Delegate to common driver implementation */
	if ((error = bhnd_pmu_probe(dev)) > 0)
		return (error);

	/* Fetch PMU capability flags */
	chipc_sc = device_get_softc(chipc);
	pcaps = bhnd_bus_read_4(chipc_sc->core, BHND_PMU_CAP);

	/* Set description */
	rev = BHND_PMU_GET_BITS(pcaps, BHND_PMU_CAP_REV);
	snprintf(desc, sizeof(desc), "Broadcom ChipCommon PMU, rev %hhu", rev);
	device_set_desc_copy(dev, desc);

	return (BUS_PROBE_NOWILDCARD);
}

static int
bhnd_pmu_chipc_attach(device_t dev)
{
	struct chipc_softc	*chipc_sc;
	struct bhnd_resource	*r;

	/* Fetch core registers from ChipCommon parent */
	chipc_sc = device_get_softc(device_get_parent(dev));
	r = chipc_sc->core;

	return (bhnd_pmu_attach(dev, r));
}

static device_method_t bhnd_pmu_chipc_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,			bhnd_pmu_chipc_probe),
	DEVMETHOD(device_attach,		bhnd_pmu_chipc_attach),

	DEVMETHOD_END
};

DEFINE_CLASS_1(bhnd_pmu, bhnd_pmu_chipc_driver, bhnd_pmu_chipc_methods,
    sizeof(struct bhnd_pmu_softc), bhnd_pmu_driver);
EARLY_DRIVER_MODULE(bhnd_pmu_chipc, bhnd_chipc, bhnd_pmu_chipc_driver,
    bhnd_pmu_devclass, NULL, NULL, BUS_PASS_TIMER + BUS_PASS_ORDER_MIDDLE);

MODULE_DEPEND(bhnd_pmu_chipc, bhnd, 1, 1, 1);
MODULE_VERSION(bhnd_pmu_chipc, 1);
