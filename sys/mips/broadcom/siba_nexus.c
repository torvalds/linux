/*-
 * Copyright (c) 2015-2016 Landon Fuller <landon@freebsd.org>
 * Copyright (c) 2007 Bruce M. Simpson.
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
#include <sys/kernel.h>
#include <sys/bus.h>
#include <sys/module.h>

#include <machine/bus.h>
#include <sys/rman.h>
#include <machine/resource.h>

#include <dev/bhnd/bhnd_ids.h>

#include <dev/bhnd/siba/sibareg.h>
#include <dev/bhnd/siba/sibavar.h>

#include "bcm_machdep.h"
#include "bcm_mipsvar.h"

#include "bhnd_nexusvar.h"

/*
 * Supports siba(4) attachment to a MIPS nexus bus.
 * 
 * Derived from Bruce M. Simpson' original siba(4) driver.
 */

_Static_assert(SIBA_MAX_INTR == BCM_MIPS_NINTR, "SIBA incompatible with "
    "generic NINTR");

static int
siba_nexus_probe(device_t dev)
{
	int error;

	if (bcm_get_platform()->cid.chip_type != BHND_CHIPTYPE_SIBA)
		return (ENXIO);

	if ((error = siba_probe(dev)) > 0)
		return (error);

	/* Set device description */
	bhnd_set_default_bus_desc(dev, &bcm_get_platform()->cid);

	return (BUS_PROBE_SPECIFIC);
}

static int
siba_nexus_attach(device_t dev)
{
	int error;

	/* Perform initial attach and enumerate our children. */
	if ((error = siba_attach(dev)))
		return (error);

	/* Delegate remainder to standard bhnd method implementation */
	if ((error = bhnd_generic_attach(dev)))
		goto failed;

	return (0);

failed:
	siba_detach(dev);
	return (error);
}

static device_method_t siba_nexus_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,			siba_nexus_probe),
	DEVMETHOD(device_attach,		siba_nexus_attach),

	DEVMETHOD_END
};

DEFINE_CLASS_2(bhnd, siba_nexus_driver, siba_nexus_methods,
    sizeof(struct siba_softc), bhnd_nexus_driver, siba_driver);

EARLY_DRIVER_MODULE(siba_nexus, nexus, siba_nexus_driver, bhnd_devclass, 0, 0,
    BUS_PASS_BUS + BUS_PASS_ORDER_MIDDLE);
