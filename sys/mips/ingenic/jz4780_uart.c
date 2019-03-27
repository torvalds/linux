/*-
 * Copyright (c) 2013 Ian Lepore
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

#include "opt_platform.h"

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/conf.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/sysctl.h>
#include <machine/bus.h>

#include <dev/extres/clk/clk.h>
#include <dev/fdt/fdt_common.h>
#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>
#include <dev/uart/uart.h>
#include <dev/uart/uart_cpu.h>
#include <dev/uart/uart_cpu_fdt.h>
#include <dev/uart/uart_bus.h>
#include <dev/uart/uart_dev_ns8250.h>
#include <dev/ic/ns16550.h>
#include "uart_if.h"

/*
 * High-level UART interface.
 */
struct jz4780_uart_softc {
	struct ns8250_softc ns8250_base;
	clk_t		clk_mod;
	clk_t		clk_baud;
};

static int
jz4780_bus_attach(struct uart_softc *sc)
{
	struct ns8250_softc *ns8250;
	struct uart_bas *bas;
	int rv;

	ns8250 = (struct ns8250_softc *)sc;
	bas = &sc->sc_bas;

	rv = ns8250_bus_attach(sc);
	if (rv != 0)
		return (0);

	/* Configure uart to use extra IER_RXTMOUT bit */
	ns8250->ier_rxbits = IER_RXTMOUT | IER_EMSC | IER_ERLS | IER_ERXRDY;
	ns8250->ier_mask = ~(ns8250->ier_rxbits);
	ns8250->ier = uart_getreg(bas, REG_IER) & ns8250->ier_mask;
	ns8250->ier |= ns8250->ier_rxbits;
	uart_setreg(bas, REG_IER, ns8250->ier);
	uart_barrier(bas);
	return (0);
}

static kobj_method_t jz4780_uart_methods[] = {
	KOBJMETHOD(uart_probe,		ns8250_bus_probe),
	KOBJMETHOD(uart_attach,		jz4780_bus_attach),
	KOBJMETHOD(uart_detach,		ns8250_bus_detach),
	KOBJMETHOD(uart_flush,		ns8250_bus_flush),
	KOBJMETHOD(uart_getsig,		ns8250_bus_getsig),
	KOBJMETHOD(uart_ioctl,		ns8250_bus_ioctl),
	KOBJMETHOD(uart_ipend,		ns8250_bus_ipend),
	KOBJMETHOD(uart_param,		ns8250_bus_param),
	KOBJMETHOD(uart_receive,	ns8250_bus_receive),
	KOBJMETHOD(uart_setsig,		ns8250_bus_setsig),
	KOBJMETHOD(uart_transmit,	ns8250_bus_transmit),
	KOBJMETHOD(uart_grab,		ns8250_bus_grab),
	KOBJMETHOD(uart_ungrab,		ns8250_bus_ungrab),
	KOBJMETHOD_END
};

static struct uart_class jz4780_uart_class = {
	"jz4780_uart_class",
	jz4780_uart_methods,
	sizeof(struct jz4780_uart_softc),
	.uc_ops = &uart_ns8250_ops,
	.uc_range = 8,
	.uc_rclk = 0,
};

/* Compatible devices. */
static struct ofw_compat_data compat_data[] = {
	{"ingenic,jz4780-uart", (uintptr_t)&jz4780_uart_class},
	{NULL,			(uintptr_t)NULL},
};

UART_FDT_CLASS(compat_data);

/*
 * UART Driver interface.
 */
static int
jz4780_uart_get_shift(device_t dev)
{
	phandle_t node;
	pcell_t shift;

	node = ofw_bus_get_node(dev);
	if ((OF_getencprop(node, "reg-shift", &shift, sizeof(shift))) <= 0)
		shift = 2;
	return ((int)shift);
}

static int
jz4780_uart_probe(device_t dev)
{
	struct jz4780_uart_softc *sc;
	uint64_t freq;
	int shift;
	int rv;
	const struct ofw_compat_data *cd;

	sc = device_get_softc(dev);
	if (!ofw_bus_status_okay(dev))
		return (ENXIO);
	cd = ofw_bus_search_compatible(dev, compat_data);
	if (cd->ocd_data == 0)
		return (ENXIO);

	/* Figure out clock setup */
	rv = clk_get_by_ofw_name(dev, 0, "module", &sc->clk_mod);
	if (rv != 0) {
		device_printf(dev, "Cannot get UART clock: %d\n", rv);
		return (ENXIO);
	}
	rv = clk_enable(sc->clk_mod);
	if (rv != 0) {
		device_printf(dev, "Cannot enable UART clock: %d\n", rv);
		return (ENXIO);
	}
	rv = clk_get_by_ofw_name(dev, 0, "baud", &sc->clk_baud);
	if (rv != 0) {
		device_printf(dev, "Cannot get UART clock: %d\n", rv);
		return (ENXIO);
	}
	rv = clk_enable(sc->clk_baud);
	if (rv != 0) {
		device_printf(dev, "Cannot enable UART clock: %d\n", rv);
		return (ENXIO);
	}
	rv = clk_get_freq(sc->clk_baud, &freq);
	if (rv != 0) {
		device_printf(dev, "Cannot determine UART clock frequency: %d\n", rv);
		return (ENXIO);
	}

	if (bootverbose)
		device_printf(dev, "got UART clock: %lld\n", freq);
	sc->ns8250_base.base.sc_class = (struct uart_class *)cd->ocd_data;
	shift = jz4780_uart_get_shift(dev);
	return (uart_bus_probe(dev, shift, 0, (int)freq, 0, 0, 0));
}

static int
jz4780_uart_detach(device_t dev)
{
	struct jz4780_uart_softc *sc;
	int rv;

	rv = uart_bus_detach(dev);
	if (rv != 0)
		return (rv);

	sc = device_get_softc(dev);
	if (sc->clk_mod != NULL) {
		clk_release(sc->clk_mod);
	}
	if (sc->clk_baud != NULL) {
		clk_release(sc->clk_baud);
	}
	return (0);
}

static device_method_t jz4780_uart_bus_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		jz4780_uart_probe),
	DEVMETHOD(device_attach,	uart_bus_attach),
	DEVMETHOD(device_detach,	jz4780_uart_detach),
	{ 0, 0 }
};

static driver_t jz4780_uart_driver = {
	uart_driver_name,
	jz4780_uart_bus_methods,
	sizeof(struct jz4780_uart_softc),
};

DRIVER_MODULE(jz4780_uart, simplebus,  jz4780_uart_driver, uart_devclass,
    0, 0);
