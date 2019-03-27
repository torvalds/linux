/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2012, Adrian Chadd <adrian@FreeBSD.org>
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

#include <dev/uart/uart.h>
#include <dev/uart/uart_cpu.h>
#include <dev/uart/uart_bus.h>

#include <mips/atheros/ar71xxreg.h>
#include <mips/atheros/ar71xx_cpudef.h>

#include <mips/atheros/uart_dev_ar933x.h>
#ifdef	EARLY_PRINTF
#include <mips/atheros/ar933x_uart.h>
#endif

#include "uart_if.h"

static int uart_ar933x_probe(device_t dev);
extern struct uart_class uart_ar933x_uart_class;

static device_method_t uart_ar933x_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		uart_ar933x_probe),
	DEVMETHOD(device_attach,	uart_bus_attach),
	DEVMETHOD(device_detach,	uart_bus_detach),
	{ 0, 0 }
};

static driver_t uart_ar933x_driver = {
	uart_driver_name,
	uart_ar933x_methods,
	sizeof(struct uart_softc),
};

extern SLIST_HEAD(uart_devinfo_list, uart_devinfo) uart_sysdevs;

static int
uart_ar933x_probe(device_t dev)
{
	struct uart_softc *sc;
	uint64_t freq;

	freq = ar71xx_uart_freq();

	sc = device_get_softc(dev);
	sc->sc_sysdev = SLIST_FIRST(&uart_sysdevs);
	sc->sc_class = &uart_ar933x_class;
	bcopy(&sc->sc_sysdev->bas, &sc->sc_bas, sizeof(sc->sc_bas));
	sc->sc_sysdev->bas.regshft = 0;
	sc->sc_sysdev->bas.bst = mips_bus_space_generic;
	sc->sc_sysdev->bas.bsh = MIPS_PHYS_TO_KSEG1(AR71XX_UART_ADDR);
	sc->sc_bas.regshft = 0;
	sc->sc_bas.bst = mips_bus_space_generic;
	sc->sc_bas.bsh = MIPS_PHYS_TO_KSEG1(AR71XX_UART_ADDR);

	return (uart_bus_probe(dev, 2, 0, freq, 0, 0, 0));
}

/*
 * Assume the UART is setup by the bootloader and just echo that.
 */
#if	defined(EARLY_PRINTF)
static void
ar933x_early_putc(int c)
{
	int i = 1000;

	/* Wait until FIFO is clear */
	while ((i > 0) && (ATH_READ_REG(AR71XX_UART_ADDR + AR933X_UART_CS_REG) &
	     AR933X_UART_CS_TX_BUSY))
		i--;

	/* Write it out */
	ATH_WRITE_REG(AR71XX_UART_ADDR + AR933X_UART_DATA_REG,
	    (c  & 0xff)| AR933X_UART_DATA_TX_CSR);
}
early_putc_t *early_putc = ar933x_early_putc;
#endif	/* EARLY_PRINTF */

DRIVER_MODULE(uart, apb, uart_ar933x_driver, uart_devclass, 0, 0);
