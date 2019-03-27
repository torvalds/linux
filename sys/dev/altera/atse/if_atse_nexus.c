/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2012,2013 Bjoern A. Zeeb
 * All rights reserved.
 *
 * This software was developed by SRI International and the University of
 * Cambridge Computer Laboratory under DARPA/AFRL contract (FA8750-11-C-0249)
 * ("MRC2"), as part of the DARPA MRC research programme.
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

#include "opt_device_polling.h"

#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/bus.h>
#include <sys/module.h>
#include <sys/rman.h>
#include <sys/socket.h>
#include <sys/types.h>

#include <machine/bus.h>
#include <machine/resource.h>

#include <net/ethernet.h>
#include <net/if.h>
#include <net/if_media.h>
#include <net/if_var.h>

#include <dev/mii/mii.h>
#include <dev/mii/miivar.h>

#include <dev/altera/atse/if_atsereg.h>

/* "device miibus" required.  See GENERIC if you get errors here. */
#include "miibus_if.h"

MODULE_DEPEND(atse, ether, 1, 1, 1);
MODULE_DEPEND(atse, miibus, 1, 1, 1);

/*
 * Device routines for interacting with nexus (probe, attach, detach) & helpers.
 * XXX We should add suspend/resume later.
 */
static int __unused
atse_resource_int(device_t dev, const char *resname, int *v)
{
	int error;

	error = resource_int_value(device_get_name(dev), device_get_unit(dev),
	    resname, v);
	if (error != 0) {
		/* If it does not exist, we fail, so not ingoring ENOENT. */
		device_printf(dev, "could not fetch '%s' hint\n", resname);
		return (error);
	}

	return (0);
}

static int __unused
atse_resource_long(device_t dev, const char *resname, long *v)
{
	int error;

	error = resource_long_value(device_get_name(dev), device_get_unit(dev),
	    resname, v);
	if (error != 0) {
		/* If it does not exist, we fail, so not ingoring ENOENT. */
		device_printf(dev, "could not fetch '%s' hint\n", resname);
		return (error);
	}

	return (0);
}

static int
atse_probe_nexus(device_t dev)
{

	device_set_desc(dev, "Altera Triple-Speed Ethernet MegaCore");

	return (BUS_PROBE_NOWILDCARD);
}

static int
atse_attach_nexus(device_t dev)
{
	struct atse_softc *sc;
	int error;

	sc = device_get_softc(dev);
	sc->atse_dev = dev;
	sc->atse_unit = device_get_unit(dev);

	/* Avalon-MM, atse management register region. */
	sc->atse_mem_rid = 0;
	sc->atse_mem_res = bus_alloc_resource_any(dev, SYS_RES_MEMORY,
	    &sc->atse_mem_rid, RF_ACTIVE);
	if (sc->atse_mem_res == NULL) {
		device_printf(dev, "failed to map memory for ctrl region\n");
		return (ENXIO);
	}

	error = atse_attach(dev);
	if (error) {
		/* Cleanup. */
		atse_detach_resources(dev);
		return (error);
	}

	return (0);
}

static device_method_t atse_methods_nexus[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		atse_probe_nexus),
	DEVMETHOD(device_attach,	atse_attach_nexus),
	DEVMETHOD(device_detach,	atse_detach_dev),

	/* MII interface */
	DEVMETHOD(miibus_readreg,	atse_miibus_readreg),
	DEVMETHOD(miibus_writereg,	atse_miibus_writereg),
	DEVMETHOD(miibus_statchg,	atse_miibus_statchg),

	DEVMETHOD_END
};

static driver_t atse_driver_nexus = {
	"atse",
	atse_methods_nexus,
	sizeof(struct atse_softc)
};

DRIVER_MODULE(atse, nexus, atse_driver_nexus, atse_devclass, 0, 0);
DRIVER_MODULE(miibus, atse, miibus_driver, miibus_devclass, 0, 0);
