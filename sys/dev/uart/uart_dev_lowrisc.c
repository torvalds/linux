/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2018 Ruslan Bukin <br@bsdpad.com>
 * All rights reserved.
 *
 * This software was developed by SRI International and the University of
 * Cambridge Computer Laboratory (Department of Computer Science and
 * Technology) under DARPA contract HR0011-18-C-0016 ("ECATS"), as part of the
 * DARPA SSITH research programme.
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

#include "opt_ddb.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/conf.h>
#include <sys/kdb.h>
#include <machine/bus.h>
#include <machine/sbi.h>

#include <dev/uart/uart.h>
#include <dev/uart/uart_cpu.h>
#include <dev/uart/uart_cpu_fdt.h>
#include <dev/uart/uart_bus.h>
#include <dev/uart/uart_dev_lowrisc.h>

#include "uart_if.h"

#define	DEFAULT_BAUD_RATE	115200

/*
 * Low-level UART interface.
 */
static int lowrisc_uart_probe(struct uart_bas *bas);
static void lowrisc_uart_init(struct uart_bas *bas, int, int, int, int);
static void lowrisc_uart_term(struct uart_bas *bas);
static void lowrisc_uart_putc(struct uart_bas *bas, int);
static int lowrisc_uart_rxready(struct uart_bas *bas);
static int lowrisc_uart_getc(struct uart_bas *bas, struct mtx *);

static struct uart_ops uart_lowrisc_uart_ops = {
	.probe = lowrisc_uart_probe,
	.init = lowrisc_uart_init,
	.term = lowrisc_uart_term,
	.putc = lowrisc_uart_putc,
	.rxready = lowrisc_uart_rxready,
	.getc = lowrisc_uart_getc,
};

static int
lowrisc_uart_probe(struct uart_bas *bas)
{

	return (0);
}

static u_int
lowrisc_uart_getbaud(struct uart_bas *bas)
{

	return (DEFAULT_BAUD_RATE);
}

static void
lowrisc_uart_init(struct uart_bas *bas, int baudrate, int databits, 
    int stopbits, int parity)
{

	/* TODO */
}

static void
lowrisc_uart_term(struct uart_bas *bas)
{

	/* TODO */
}

static void
lowrisc_uart_putc(struct uart_bas *bas, int c)
{

	while (GETREG(bas, UART_DR) & DR_TX_FIFO_FULL)
		;

	SETREG(bas, UART_DR, c);
}

static int
lowrisc_uart_rxready(struct uart_bas *bas)
{

	if (GETREG(bas, UART_DR) & DR_RX_FIFO_EMPTY)
		return (0);

	return (1);
}

static int
lowrisc_uart_getc(struct uart_bas *bas, struct mtx *hwmtx)
{
	uint32_t reg;

	uart_lock(hwmtx);
	SETREG(bas, UART_INT_STATUS, INT_STATUS_ACK);
	reg = GETREG(bas, UART_DR);
	uart_unlock(hwmtx);

	return (reg & 0xff);
}

/*
 * High-level UART interface.
 */
struct lowrisc_uart_softc {
	struct uart_softc base;
};

static int lowrisc_uart_bus_attach(struct uart_softc *);
static int lowrisc_uart_bus_detach(struct uart_softc *);
static int lowrisc_uart_bus_flush(struct uart_softc *, int);
static int lowrisc_uart_bus_getsig(struct uart_softc *);
static int lowrisc_uart_bus_ioctl(struct uart_softc *, int, intptr_t);
static int lowrisc_uart_bus_ipend(struct uart_softc *);
static int lowrisc_uart_bus_param(struct uart_softc *, int, int, int, int);
static int lowrisc_uart_bus_probe(struct uart_softc *);
static int lowrisc_uart_bus_receive(struct uart_softc *);
static int lowrisc_uart_bus_setsig(struct uart_softc *, int);
static int lowrisc_uart_bus_transmit(struct uart_softc *);
static void lowrisc_uart_bus_grab(struct uart_softc *);
static void lowrisc_uart_bus_ungrab(struct uart_softc *);

static kobj_method_t lowrisc_uart_methods[] = {
	KOBJMETHOD(uart_attach,		lowrisc_uart_bus_attach),
	KOBJMETHOD(uart_detach,		lowrisc_uart_bus_detach),
	KOBJMETHOD(uart_flush,		lowrisc_uart_bus_flush),
	KOBJMETHOD(uart_getsig,		lowrisc_uart_bus_getsig),
	KOBJMETHOD(uart_ioctl,		lowrisc_uart_bus_ioctl),
	KOBJMETHOD(uart_ipend,		lowrisc_uart_bus_ipend),
	KOBJMETHOD(uart_param,		lowrisc_uart_bus_param),
	KOBJMETHOD(uart_probe,		lowrisc_uart_bus_probe),
	KOBJMETHOD(uart_receive,	lowrisc_uart_bus_receive),
	KOBJMETHOD(uart_setsig,		lowrisc_uart_bus_setsig),
	KOBJMETHOD(uart_transmit,	lowrisc_uart_bus_transmit),
	KOBJMETHOD(uart_grab,		lowrisc_uart_bus_grab),
	KOBJMETHOD(uart_ungrab,		lowrisc_uart_bus_ungrab),
	{ 0, 0 }
};

static struct uart_class uart_lowrisc_class = {
	"lowrisc",
	lowrisc_uart_methods,
	sizeof(struct lowrisc_uart_softc),
	.uc_ops = &uart_lowrisc_uart_ops,
	.uc_range = 0x100,
	.uc_rclk = 12500000, /* TODO: get value from clock manager */
	.uc_rshift = 0
};

static struct ofw_compat_data compat_data[] = {
	{"lowrisc-fake",	(uintptr_t)&uart_lowrisc_class},
	{NULL,			(uintptr_t)NULL},
};
UART_FDT_CLASS_AND_DEVICE(compat_data);

static int
lowrisc_uart_bus_attach(struct uart_softc *sc)
{
	struct uart_bas *bas;
	struct uart_devinfo *di;

	bas = &sc->sc_bas;
	if (sc->sc_sysdev != NULL) {
		di = sc->sc_sysdev;
		lowrisc_uart_init(bas, di->baudrate, di->databits, di->stopbits,
		    di->parity);
	} else
		lowrisc_uart_init(bas, DEFAULT_BAUD_RATE, 8, 1, 0);

	(void)lowrisc_uart_bus_getsig(sc);

	/* TODO: clear all pending interrupts. */

	return (0);
}

static int
lowrisc_uart_bus_detach(struct uart_softc *sc)
{

	/* TODO */

	return (0);
}

static int
lowrisc_uart_bus_flush(struct uart_softc *sc, int what)
{

	/* TODO */

	return (0);
}

static int
lowrisc_uart_bus_getsig(struct uart_softc *sc)
{

	/* TODO */

	return (0);
}

static int
lowrisc_uart_bus_ioctl(struct uart_softc *sc, int request, intptr_t data)
{
	struct uart_bas *bas;
	int error;

	bas = &sc->sc_bas;
	error = 0;
	uart_lock(sc->sc_hwmtx);
	switch (request) {
	case UART_IOCTL_BREAK:
		/* TODO */
		break;
	case UART_IOCTL_BAUD:
		*(u_int*)data = lowrisc_uart_getbaud(bas);
		break;
	default:
		error = EINVAL;
		break;
	}
	uart_unlock(sc->sc_hwmtx);

	return (error);
}

static int
lowrisc_uart_bus_ipend(struct uart_softc *sc)
{
	struct uart_bas *bas;
	int ipend;

	bas = &sc->sc_bas;

	ipend = 0;

	uart_lock(sc->sc_hwmtx);
	if ((GETREG(bas, UART_DR) & DR_RX_FIFO_EMPTY) == 0)
		ipend |= SER_INT_RXREADY;
	SETREG(bas, UART_INT_STATUS, INT_STATUS_ACK);
	uart_unlock(sc->sc_hwmtx);

	return (ipend);
}

static int
lowrisc_uart_bus_param(struct uart_softc *sc, int baudrate, int databits,
    int stopbits, int parity)
{

	uart_lock(sc->sc_hwmtx);
	lowrisc_uart_init(&sc->sc_bas, baudrate, databits, stopbits, parity);
	uart_unlock(sc->sc_hwmtx);

	return (0);
}

static int
lowrisc_uart_bus_probe(struct uart_softc *sc)
{
	int error;

	error = lowrisc_uart_probe(&sc->sc_bas);
	if (error)
		return (error);

	/*
	 * On input we can read up to the full fifo size at once.  On output, we
	 * want to write only as much as the programmed tx low water level,
	 * because that's all we can be certain we have room for in the fifo
	 * when we get a tx-ready interrupt.
	 */
	sc->sc_rxfifosz = 2048;
	sc->sc_txfifosz = 2048;

	device_set_desc(sc->sc_dev, "lowRISC UART");

	return (0);
}

static int
lowrisc_uart_bus_receive(struct uart_softc *sc)
{
	struct uart_bas *bas;
	uint32_t reg;

	bas = &sc->sc_bas;

	uart_lock(sc->sc_hwmtx);

	do {
		if (uart_rx_full(sc)) {
			/* No space left in the input buffer */
			sc->sc_rxbuf[sc->sc_rxput] = UART_STAT_OVERRUN;
			break;
		}
		reg = GETREG(bas, UART_DR);
		SETREG(bas, UART_INT_STATUS, INT_STATUS_ACK);
		uart_rx_put(sc, reg & 0xff);
	} while ((reg & DR_RX_FIFO_EMPTY) == 0);

	uart_unlock(sc->sc_hwmtx);

	return (0);
}

static int
lowrisc_uart_bus_setsig(struct uart_softc *sc, int sig)
{

	return (0);
}

static int
lowrisc_uart_bus_transmit(struct uart_softc *sc)
{
	struct uart_bas *bas;
	int i;

	bas = &sc->sc_bas;

	uart_lock(sc->sc_hwmtx);
	for (i = 0; i < sc->sc_txdatasz; i++) {
		while (GETREG(bas, UART_DR) & DR_TX_FIFO_FULL)
			;
		SETREG(bas, UART_DR, sc->sc_txbuf[i] & 0xff);
	}
	uart_unlock(sc->sc_hwmtx);

	return (0);
}

static void
lowrisc_uart_bus_grab(struct uart_softc *sc)
{
	struct uart_bas *bas;

	bas = &sc->sc_bas;

	uart_lock(sc->sc_hwmtx);
	/* TODO */
	uart_unlock(sc->sc_hwmtx);
}

static void
lowrisc_uart_bus_ungrab(struct uart_softc *sc)
{
	struct uart_bas *bas;

	bas = &sc->sc_bas;

	uart_lock(sc->sc_hwmtx);
	/* TODO */
	uart_unlock(sc->sc_hwmtx);
}
