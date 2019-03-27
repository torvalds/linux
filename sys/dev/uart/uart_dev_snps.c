/*-
 * Copyright (c) 2016 Jared McNeill <jmcneill@invisible.ca>
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <machine/bus.h>

#include <dev/uart/uart.h>
#include <dev/uart/uart_bus.h>
#include <dev/uart/uart_cpu_fdt.h>
#include <dev/uart/uart_dev_ns8250.h>

#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>

#ifdef EXT_RESOURCES
#include <dev/extres/clk/clk.h>
#include <dev/extres/hwreset/hwreset.h>
#endif

#include "uart_if.h"

struct snps_softc {
	struct ns8250_softc	ns8250;

#ifdef EXT_RESOURCES
	clk_t			baudclk;
	clk_t			apb_pclk;
	hwreset_t		reset;
#endif
};

/*
 * To use early printf on 64 bits Allwinner SoC, add to kernel config
 * options SOCDEV_PA=0x0
 * options SOCDEV_VA=0x40000000
 * options EARLY_PRINTF
 *
 * To use early printf on 32 bits Allwinner SoC, add to kernel config
 * options SOCDEV_PA=0x01C00000
 * options SOCDEV_VA=0x10000000
 * options EARLY_PRINTF
 *
 * remove the if 0
*/
#if 0
#ifdef EARLY_PRINTF
static void
uart_snps_early_putc(int c)
{
	volatile uint32_t *stat;
	volatile uint32_t *tx;

#ifdef ALLWINNER_64
	stat = (uint32_t *) (SOCDEV_VA + 0x1C2807C);
	tx = (uint32_t *) (SOCDEV_VA + 0x1C28000);
#endif
#ifdef ALLWINNER_32
	stat = (uint32_t *) (SOCDEV_VA + 0x2807C);
	tx = (uint32_t *) (SOCDEV_VA + 0x28000);
#endif

	while ((*stat & (1 << 2)) == 0)
		continue;
	*tx = c;
}
early_putc_t *early_putc = uart_snps_early_putc;
#endif /* EARLY_PRINTF */
#endif

static kobj_method_t snps_methods[] = {
	KOBJMETHOD(uart_probe,		ns8250_bus_probe),
	KOBJMETHOD(uart_attach,		ns8250_bus_attach),
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

struct uart_class uart_snps_class = {
	"snps",
	snps_methods,
	sizeof(struct snps_softc),
	.uc_ops = &uart_ns8250_ops,
	.uc_range = 8,
	.uc_rclk = 0,
};

static struct ofw_compat_data compat_data[] = {
	{ "snps,dw-apb-uart",		(uintptr_t)&uart_snps_class },
	{ "marvell,armada-38x-uart",	(uintptr_t)&uart_snps_class },
	{ NULL,				(uintptr_t)NULL }
};
UART_FDT_CLASS(compat_data);

#ifdef EXT_RESOURCES
static int
snps_get_clocks(device_t dev, clk_t *baudclk, clk_t *apb_pclk)
{

	*baudclk = NULL;
	*apb_pclk = NULL;

	/* Baud clock is either named "baudclk", or there is a single
	 * unnamed clock.
	 */
	if (clk_get_by_ofw_name(dev, 0, "baudclk", baudclk) != 0 &&
	    clk_get_by_ofw_index(dev, 0, 0, baudclk) != 0)
		return (ENOENT);

	/* APB peripheral clock is optional */
	(void)clk_get_by_ofw_name(dev, 0, "apb_pclk", apb_pclk);

	return (0);
}
#endif

static int
snps_probe(device_t dev)
{
	struct snps_softc *sc;
	struct uart_class *uart_class;
	phandle_t node;
	uint32_t shift, iowidth, clock;
	uint64_t freq;
	int error;
#ifdef EXT_RESOURCES
	clk_t baudclk, apb_pclk;
	hwreset_t reset;
#endif

	if (!ofw_bus_status_okay(dev))
		return (ENXIO);

	uart_class = (struct uart_class *)ofw_bus_search_compatible(dev,
	    compat_data)->ocd_data;
	if (uart_class == NULL)
		return (ENXIO);

	freq = 0;
	sc = device_get_softc(dev);
	sc->ns8250.base.sc_class = uart_class;

	node = ofw_bus_get_node(dev);
	if (OF_getencprop(node, "reg-shift", &shift, sizeof(shift)) <= 0)
		shift = 0;
	if (OF_getencprop(node, "reg-io-width", &iowidth, sizeof(iowidth)) <= 0)
		iowidth = 1;
	if (OF_getencprop(node, "clock-frequency", &clock, sizeof(clock)) <= 0)
		clock = 0;

#ifdef EXT_RESOURCES
	if (hwreset_get_by_ofw_idx(dev, 0, 0, &reset) == 0) {
		error = hwreset_deassert(reset);
		if (error != 0) {
			device_printf(dev, "cannot de-assert reset\n");
			return (error);
		}
	}

	if (snps_get_clocks(dev, &baudclk, &apb_pclk) == 0) {
		error = clk_enable(baudclk);
		if (error != 0) {
			device_printf(dev, "cannot enable baud clock\n");
			return (error);
		}
		if (apb_pclk != NULL) {
			error = clk_enable(apb_pclk);
			if (error != 0) {
				device_printf(dev,
				    "cannot enable peripheral clock\n");
				return (error);
			}
		}

		if (clock == 0) {
			error = clk_get_freq(baudclk, &freq);
			if (error != 0) {
				device_printf(dev, "cannot get frequency\n");
				return (error);
			}
			clock = (uint32_t)freq;
		}
	}
#endif

	if (bootverbose && clock == 0)
		device_printf(dev, "could not determine frequency\n");

	error = uart_bus_probe(dev, (int)shift, (int)iowidth, (int)clock, 0, 0, UART_F_BUSY_DETECT);
	if (error != 0)
		return (error);

#ifdef EXT_RESOURCES
	/* XXX uart_bus_probe has changed the softc, so refresh it */
	sc = device_get_softc(dev);

	/* Store clock and reset handles for detach */
	sc->baudclk = baudclk;
	sc->apb_pclk = apb_pclk;
	sc->reset = reset;
#endif

	return (0);
}

static int
snps_detach(device_t dev)
{
#ifdef EXT_RESOURCES
	struct snps_softc *sc;
	clk_t baudclk, apb_pclk;
	hwreset_t reset;
#endif
	int error;

#ifdef EXT_RESOURCES
	sc = device_get_softc(dev);
	baudclk = sc->baudclk;
	apb_pclk = sc->apb_pclk;
	reset = sc->reset;
#endif

	error = uart_bus_detach(dev);
	if (error != 0)
		return (error);

#ifdef EXT_RESOURCES
	if (reset != NULL) {
		error = hwreset_assert(reset);
		if (error != 0) {
			device_printf(dev, "cannot assert reset\n");
			return (error);
		}
		hwreset_release(reset);
	}
	if (apb_pclk != NULL) {
		error = clk_release(apb_pclk);
		if (error != 0) {
			device_printf(dev, "cannot release peripheral clock\n");
			return (error);
		}
	}
	if (baudclk != NULL) {
		error = clk_release(baudclk);
		if (error != 0) {
			device_printf(dev, "cannot release baud clock\n");
			return (error);
		}
	}
#endif

	return (0);
}

static device_method_t snps_bus_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		snps_probe),
	DEVMETHOD(device_attach,	uart_bus_attach),
	DEVMETHOD(device_detach, 	snps_detach),
	DEVMETHOD_END
};

static driver_t snps_uart_driver = {
	uart_driver_name,
	snps_bus_methods,
	sizeof(struct snps_softc)
};

DRIVER_MODULE(uart_snps, simplebus, snps_uart_driver, uart_devclass, 0, 0);
