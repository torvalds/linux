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

/*
 * ChipCommon SPROM driver.
 */

#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/bus.h>
#include <sys/limits.h>
#include <sys/malloc.h>
#include <sys/module.h>
#include <sys/systm.h>

#include <dev/bhnd/bhnd.h>
#include <dev/bhnd/nvram/bhnd_nvram.h>
#include <dev/bhnd/nvram/bhnd_spromvar.h>

#include <dev/bhnd/cores/chipc/chipc.h>

#include "bhnd_nvram_if.h"

#define	CHIPC_VALID_SPROM_SRC(_src)	\
	((_src) == BHND_NVRAM_SRC_SPROM || (_src) == BHND_NVRAM_SRC_OTP)

static int
chipc_sprom_probe(device_t dev)
{
	struct chipc_caps	*caps;
	device_t		 chipc;
	int			 error;

	chipc = device_get_parent(dev);
	caps = BHND_CHIPC_GET_CAPS(chipc);

	/* Only match on SPROM/OTP devices */
	if (!CHIPC_VALID_SPROM_SRC(caps->nvram_src))
		return (ENXIO);

	/* Defer to default driver implementation */
	if ((error = bhnd_sprom_probe(dev)) > 0)
		return (error);

	return (BUS_PROBE_NOWILDCARD);
}

static int
chipc_sprom_attach(device_t dev)
{
	struct chipc_caps	*caps;
	device_t		 chipc;
	int			 error;

	chipc = device_get_parent(dev);
	caps = BHND_CHIPC_GET_CAPS(chipc);

	/* Request that ChipCommon enable access to SPROM hardware before
	 * delegating attachment (and SPROM parsing) to the common driver */
	if ((error = BHND_CHIPC_ENABLE_SPROM(chipc)))
		return (error);

	error = bhnd_sprom_attach(dev, caps->sprom_offset);
	BHND_CHIPC_DISABLE_SPROM(chipc);
	return (error);
}

static device_method_t chipc_sprom_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,			chipc_sprom_probe),
	DEVMETHOD(device_attach,		chipc_sprom_attach),
	DEVMETHOD_END
};

DEFINE_CLASS_1(bhnd_nvram, chipc_sprom_driver, chipc_sprom_methods, sizeof(struct bhnd_sprom_softc), bhnd_sprom_driver);
DRIVER_MODULE(bhnd_chipc_sprom, bhnd_chipc, chipc_sprom_driver, bhnd_nvram_devclass, NULL, NULL);

MODULE_DEPEND(bhnd_chipc_sprom, bhnd, 1, 1, 1);
MODULE_DEPEND(bhnd_chipc_sprom, bhnd_chipc, 1, 1, 1);
MODULE_DEPEND(bhnd_chipc_sprom, bhnd_sprom, 1, 1, 1);
MODULE_VERSION(bhnd_chipc_sprom, 1);
