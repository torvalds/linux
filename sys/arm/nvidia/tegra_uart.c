/*-
 * Copyright (c) 2016 Michal Meloun <mmel@FreeBSD.org>
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


/*
 * UART driver for Tegra SoCs.
 */
#include "opt_platform.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/conf.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/sysctl.h>
#include <machine/bus.h>

#include <dev/extres/clk/clk.h>
#include <dev/extres/hwreset/hwreset.h>
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
struct tegra_softc {
	struct ns8250_softc 	ns8250_base;
	clk_t			clk;
	hwreset_t		reset;
};

/*
 * UART class interface.
 */
static int
tegra_uart_attach(struct uart_softc *sc)
{
	int rv;
	struct ns8250_softc *ns8250 = (struct ns8250_softc*)sc;
	struct uart_bas *bas = &sc->sc_bas;

	rv = ns8250_bus_attach(sc);
	if (rv != 0)
		return (rv);

	ns8250->ier_rxbits = 0x1d;
	ns8250->ier_mask = 0xc0;
	ns8250->ier = uart_getreg(bas, REG_IER) & ns8250->ier_mask;
	ns8250->ier |= ns8250->ier_rxbits;
	uart_setreg(bas, REG_IER, ns8250->ier);
	uart_barrier(bas);
	return (0);
}

static void
tegra_uart_grab(struct uart_softc *sc)
{
	struct uart_bas *bas = &sc->sc_bas;
	struct ns8250_softc *ns8250 = (struct ns8250_softc*)sc;
	u_char ier;

	/*
	 * turn off all interrupts to enter polling mode. Leave the
	 * saved mask alone. We'll restore whatever it was in ungrab.
	 * All pending interrupt signals are reset when IER is set to 0.
	 */
	uart_lock(sc->sc_hwmtx);
	ier = uart_getreg(bas, REG_IER);
	uart_setreg(bas, REG_IER, ier & ns8250->ier_mask);
	uart_setreg(bas, REG_FCR, 0);
	uart_barrier(bas);
	uart_unlock(sc->sc_hwmtx);
}

static void
tegra_uart_ungrab(struct uart_softc *sc)
{
	struct ns8250_softc *ns8250 = (struct ns8250_softc*)sc;
	struct uart_bas *bas = &sc->sc_bas;

	/*
	 * Restore previous interrupt mask
	 */
	uart_lock(sc->sc_hwmtx);
	uart_setreg(bas, REG_FCR, ns8250->fcr);
	uart_setreg(bas, REG_IER, ns8250->ier);
	uart_barrier(bas);
	uart_unlock(sc->sc_hwmtx);
}

static kobj_method_t tegra_methods[] = {
	KOBJMETHOD(uart_probe,		ns8250_bus_probe),
	KOBJMETHOD(uart_attach,		tegra_uart_attach),
	KOBJMETHOD(uart_detach,		ns8250_bus_detach),
	KOBJMETHOD(uart_flush,		ns8250_bus_flush),
	KOBJMETHOD(uart_getsig,		ns8250_bus_getsig),
	KOBJMETHOD(uart_ioctl,		ns8250_bus_ioctl),
	KOBJMETHOD(uart_ipend,		ns8250_bus_ipend),
	KOBJMETHOD(uart_param,		ns8250_bus_param),
	KOBJMETHOD(uart_receive,	ns8250_bus_receive),
	KOBJMETHOD(uart_setsig,		ns8250_bus_setsig),
	KOBJMETHOD(uart_transmit,	ns8250_bus_transmit),
	KOBJMETHOD(uart_grab,		tegra_uart_grab),
	KOBJMETHOD(uart_ungrab,		tegra_uart_ungrab),
	KOBJMETHOD_END
};

static struct uart_class tegra_uart_class = {
	"tegra class",
	tegra_methods,
	sizeof(struct tegra_softc),
	.uc_ops = &uart_ns8250_ops,
	.uc_range = 8,
	.uc_rclk = 0,
};

/* Compatible devices. */
static struct ofw_compat_data compat_data[] = {
	{"nvidia,tegra124-uart", (uintptr_t)&tegra_uart_class},
	{NULL,			(uintptr_t)NULL},
};

UART_FDT_CLASS(compat_data);

/*
 * UART Driver interface.
 */
static int
uart_fdt_get_shift1(phandle_t node)
{
	pcell_t shift;

	if ((OF_getencprop(node, "reg-shift", &shift, sizeof(shift))) <= 0)
		shift = 2;
	return ((int)shift);
}

static int
tegra_uart_probe(device_t dev)
{
	struct tegra_softc *sc;
	phandle_t node;
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
	sc->ns8250_base.base.sc_class = (struct uart_class *)cd->ocd_data;

	rv = hwreset_get_by_ofw_name(dev, 0, "serial", &sc->reset);
	if (rv != 0) {
		device_printf(dev, "Cannot get 'serial' reset\n");
		return (ENXIO);
	}
	rv = hwreset_deassert(sc->reset);
	if (rv != 0) {
		device_printf(dev, "Cannot unreset 'serial' reset\n");
		return (ENXIO);
	}

	node = ofw_bus_get_node(dev);
	shift = uart_fdt_get_shift1(node);
	rv = clk_get_by_ofw_index(dev, 0, 0, &sc->clk);
	if (rv != 0) {
		device_printf(dev, "Cannot get UART clock: %d\n", rv);
		return (ENXIO);
	}
	rv = clk_enable(sc->clk);
	if (rv != 0) {
		device_printf(dev, "Cannot enable UART clock: %d\n", rv);
		return (ENXIO);
	}
	rv = clk_get_freq(sc->clk, &freq);
	if (rv != 0) {
		device_printf(dev, "Cannot enable UART clock: %d\n", rv);
		return (ENXIO);
	}
	return (uart_bus_probe(dev, shift, 0, (int)freq, 0, 0, 0));
}

static int
tegra_uart_detach(device_t dev)
{
	struct tegra_softc *sc;

	sc = device_get_softc(dev);
	if (sc->clk != NULL) {
		clk_release(sc->clk);
	}

	return (uart_bus_detach(dev));
}

static device_method_t tegra_uart_bus_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		tegra_uart_probe),
	DEVMETHOD(device_attach,	uart_bus_attach),
	DEVMETHOD(device_detach,	tegra_uart_detach),
	{ 0, 0 }
};

static driver_t tegra_uart_driver = {
	uart_driver_name,
	tegra_uart_bus_methods,
	sizeof(struct tegra_softc),
};

DRIVER_MODULE(tegra_uart, simplebus,  tegra_uart_driver, uart_devclass,
    0, 0);
