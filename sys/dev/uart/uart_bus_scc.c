/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2004-2006 Marcel Moolenaar
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
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
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

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

#include <dev/scc/scc_bus.h>

#include <dev/uart/uart.h>
#include <dev/uart/uart_bus.h>

static int uart_scc_attach(device_t dev);
static int uart_scc_probe(device_t dev);

static device_method_t uart_scc_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		uart_scc_probe),
	DEVMETHOD(device_attach,	uart_scc_attach),
	DEVMETHOD(device_detach,	uart_bus_detach),
	/* Serdev interface */
	DEVMETHOD(serdev_ihand,		uart_bus_ihand),
	DEVMETHOD(serdev_sysdev,	uart_bus_sysdev),
	{ 0, 0 }
};

static driver_t uart_scc_driver = {
	uart_driver_name,
	uart_scc_methods,
	sizeof(struct uart_softc),
};

static int
uart_scc_attach(device_t dev)
{
	device_t parent;
	struct uart_softc *sc;
	uintptr_t mtx;

	parent = device_get_parent(dev);
	sc = device_get_softc(dev);

	if (BUS_READ_IVAR(parent, dev, SCC_IVAR_HWMTX, &mtx))
		return (ENXIO);
	sc->sc_hwmtx = (struct mtx *)(void *)mtx;
	return (uart_bus_attach(dev));
}

static int
uart_scc_probe(device_t dev)
{
	device_t parent;
	struct uart_softc *sc;
	uintptr_t ch, cl, md, rs;

	parent = device_get_parent(dev);
	sc = device_get_softc(dev);

	if (BUS_READ_IVAR(parent, dev, SCC_IVAR_MODE, &md) ||
	    BUS_READ_IVAR(parent, dev, SCC_IVAR_CLASS, &cl))
		return (ENXIO);
	if (md != SCC_MODE_ASYNC)
		return (ENXIO);
	switch (cl) {
	case SCC_CLASS_QUICC:
		sc->sc_class = &uart_quicc_class;
		break;
	case SCC_CLASS_SAB82532:
		sc->sc_class = &uart_sab82532_class;
		break;
	case SCC_CLASS_Z8530:
		sc->sc_class = &uart_z8530_class;
		break;
	default:
		return (ENXIO);
	}
	if (BUS_READ_IVAR(parent, dev, SCC_IVAR_CHANNEL, &ch) ||
	    BUS_READ_IVAR(parent, dev, SCC_IVAR_CLOCK, &cl) ||
	    BUS_READ_IVAR(parent, dev, SCC_IVAR_REGSHFT, &rs))
		return (ENXIO);

	return (uart_bus_probe(dev, rs, 0, cl, 0, ch, 0));
}

DRIVER_MODULE(uart, scc, uart_scc_driver, uart_devclass, 0, 0);
