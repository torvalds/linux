/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2006 Wojciech A. Koszek <wkoszek@FreeBSD.org>
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
 * $Id$
 */
/*
 * Skeleton of this file was based on respective code for ARM
 * code written by Olivier Houchard.
 */

/*
 * XXXMIPS: This file is hacked from arm/... . XXXMIPS here means this file is
 * experimental and was written for MIPS32 port.
 */
#include "opt_uart.h"

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/conf.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <machine/bus.h>
#include <sys/rman.h>
#include <machine/resource.h>

#include <dev/pci/pcivar.h>

#include <dev/uart/uart.h>
#include <dev/uart/uart_bus.h>
#include <dev/uart/uart_cpu.h>

#include <mips/cavium/octeon_pcmap_regs.h>

#include <contrib/octeon-sdk/cvmx.h>

#include "uart_if.h"

extern struct uart_class uart_oct16550_class;

static int uart_octeon_probe(device_t dev);

static device_method_t uart_octeon_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe, uart_octeon_probe),
	DEVMETHOD(device_attach, uart_bus_attach),
	DEVMETHOD(device_detach, uart_bus_detach),
	{0, 0}
};

static driver_t uart_octeon_driver = {
	uart_driver_name,
	uart_octeon_methods,
	sizeof(struct uart_softc),
};

extern 
SLIST_HEAD(uart_devinfo_list, uart_devinfo) uart_sysdevs;

static int
uart_octeon_probe(device_t dev)
{
	struct uart_softc *sc;
	int unit;

	unit = device_get_unit(dev);
	sc = device_get_softc(dev);
	sc->sc_class = &uart_oct16550_class;

	/*
	 * We inherit the settings from the systme console.  Note, the bst
	 * bad bus_space_map are bogus here, but obio doesn't yet support
	 * them, it seems.
	 */
	sc->sc_sysdev = SLIST_FIRST(&uart_sysdevs);
	bcopy(&sc->sc_sysdev->bas, &sc->sc_bas, sizeof(sc->sc_bas));
	sc->sc_bas.bst = uart_bus_space_mem;
	/*
	 * XXX
	 * RBR isn't really a great base address.
	 */
	if (bus_space_map(sc->sc_bas.bst, CVMX_MIO_UARTX_RBR(0),
	    uart_getrange(sc->sc_class), 0, &sc->sc_bas.bsh) != 0)
		return (ENXIO);
	return (uart_bus_probe(dev, sc->sc_bas.regshft, 0, 0, 0, unit, 0));
}

DRIVER_MODULE(uart, obio, uart_octeon_driver, uart_devclass, 0, 0);
