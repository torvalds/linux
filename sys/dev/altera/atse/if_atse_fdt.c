/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2013 Bjoern A. Zeeb
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

#include <sys/param.h>
#include <sys/bus.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/rman.h>
#include <sys/socket.h>
#include <sys/systm.h>

#include <machine/bus.h>
#include <machine/resource.h>

#include <net/ethernet.h>
#include <net/if.h>
#include <net/if_media.h>
#include <net/if_var.h>

#include <dev/mii/mii.h>
#include <dev/mii/miivar.h>

#include <dev/fdt/fdt_common.h>
#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>

#include <dev/altera/atse/if_atsereg.h>

/* "device miibus" required.  See GENERIC if you get errors here. */
#include "miibus_if.h"

static int
atse_probe_fdt(device_t dev)
{

	if (!ofw_bus_status_okay(dev))
		return (ENXIO);

	if (!ofw_bus_is_compatible(dev, "altera,atse")) {
       		return (ENXIO);
	}

	device_set_desc(dev, "Altera Triple-Speed Ethernet MegaCore");

	return (BUS_PROBE_DEFAULT);
}

static int
atse_attach_fdt(device_t dev)
{
	struct atse_softc *sc;
	int error;

	sc = device_get_softc(dev);
	sc->atse_dev = dev;
	sc->atse_unit = device_get_unit(dev);

	/*
	 * FDT has the list of our resources.  Given we are using multiple
	 * memory regions and possibly multiple interrupts, we need to attach
	 * them in the order specified in .dts:
	 * MAC, RX and RXC FIFO, TX and TXC FIFO; RX INTR, TX INTR.
	 */

	/* MAC: Avalon-MM, atse management register region. */
	sc->atse_mem_rid = 0;
	sc->atse_mem_res = bus_alloc_resource_any(dev, SYS_RES_MEMORY,
	    &sc->atse_mem_rid, RF_ACTIVE);
	if (sc->atse_mem_res == NULL) {
		device_printf(dev, "failed to map memory for ctrl region\n");
		/* Cleanup. */
		atse_detach_resources(dev);

		return (ENXIO);
	}
	if (bootverbose)
		device_printf(sc->atse_dev, "MAC ctrl region at mem %p-%p\n",
		    (void *)rman_get_start(sc->atse_mem_res),
		    (void *)(rman_get_start(sc->atse_mem_res) +
		    rman_get_size(sc->atse_mem_res)));

	error = atse_attach(dev);
	if (error) {
		/* Cleanup. */
		atse_detach_resources(dev);

		return (error);
	}

	return (0);
}

static device_method_t atse_methods_fdt[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		atse_probe_fdt),
	DEVMETHOD(device_attach,	atse_attach_fdt),
	DEVMETHOD(device_detach,	atse_detach_dev),

	/* MII interface */
	DEVMETHOD(miibus_readreg,	atse_miibus_readreg),
	DEVMETHOD(miibus_writereg,	atse_miibus_writereg),
	DEVMETHOD(miibus_statchg,	atse_miibus_statchg),

	DEVMETHOD_END
};

static driver_t atse_driver_fdt = {
	"atse",
	atse_methods_fdt,
	sizeof(struct atse_softc)
};

DRIVER_MODULE(atse, simplebus, atse_driver_fdt, atse_devclass, 0, 0);
DRIVER_MODULE(miibus, atse, miibus_driver, miibus_devclass, 0, 0);
