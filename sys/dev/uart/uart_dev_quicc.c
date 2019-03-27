/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2006 Juniper Networks
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
#include <sys/endian.h>
#include <machine/bus.h>

#include <dev/ic/quicc.h>

#include <dev/uart/uart.h>
#include <dev/uart/uart_cpu.h>
#include <dev/uart/uart_bus.h>

#include "uart_if.h"

#define	DEFAULT_RCLK	((266000000 * 2) / 16)

#define	quicc_read2(bas, reg)		\
	bus_space_read_2((bas)->bst, (bas)->bsh, reg)
#define	quicc_read4(bas, reg)		\
	bus_space_read_4((bas)->bst, (bas)->bsh, reg)

#define	quicc_write2(bas, reg, val)	\
	bus_space_write_2((bas)->bst, (bas)->bsh, reg, val)
#define	quicc_write4(bas, reg, val)	\
	bus_space_write_4((bas)->bst, (bas)->bsh, reg, val)

static int
quicc_divisor(int rclk, int baudrate)
{
	int act_baud, divisor, error;

	if (baudrate == 0)
		return (-1);

	divisor = rclk / baudrate / 16;
	if (divisor > 4096)
		divisor = ((divisor >> 3) - 2) | 1;
	else if (divisor >= 0)
		divisor = (divisor - 1) << 1;
	if (divisor < 0 || divisor >= 8192)
		return (-1);
	act_baud = rclk / (((divisor >> 1) + 1) << ((divisor & 1) ? 8 : 4));

	/* 10 times error in percent: */
	error = ((act_baud - baudrate) * 2000 / baudrate + 1) >> 1;

	/* 3.0% maximum error tolerance: */
	if (error < -30 || error > 30)
		return (-1);

	return (divisor);
}

static int
quicc_param(struct uart_bas *bas, int baudrate, int databits, int stopbits,
    int parity)
{
	int divisor;
	uint16_t psmr;

	if (baudrate > 0) {
		divisor = quicc_divisor(bas->rclk, baudrate);
		if (divisor == -1)
			return (EINVAL);
		quicc_write4(bas, QUICC_REG_BRG(bas->chan - 1),
		    divisor | 0x10000);
	}

	psmr = 0;
	switch (databits) {
	case 5:		psmr |= 0x0000; break;
	case 6:		psmr |= 0x1000; break;
	case 7:		psmr |= 0x2000; break;
	case 8:		psmr |= 0x3000; break;
	default:	return (EINVAL);
	}
	switch (stopbits) {
	case 1:		psmr |= 0x0000; break;
	case 2:		psmr |= 0x4000; break;
	default:	return (EINVAL);
	}
	switch (parity) {
	case UART_PARITY_EVEN:	psmr |= 0x1a; break;
	case UART_PARITY_MARK:	psmr |= 0x1f; break;
	case UART_PARITY_NONE:	psmr |= 0x00; break;
	case UART_PARITY_ODD:	psmr |= 0x10; break;
	case UART_PARITY_SPACE:	psmr |= 0x15; break;
	default:		return (EINVAL);
	}
	quicc_write2(bas, QUICC_REG_SCC_PSMR(bas->chan - 1), psmr);
	return (0);
}

static void
quicc_setup(struct uart_bas *bas, int baudrate, int databits, int stopbits,
    int parity)
{

	if (bas->rclk == 0)
		bas->rclk = DEFAULT_RCLK;

	/*
	 * GSMR_L = 0x00028034
	 * GSMR_H = 0x00000020
	 */
	quicc_param(bas, baudrate, databits, stopbits, parity);

	quicc_write2(bas, QUICC_REG_SCC_SCCE(bas->chan - 1), ~0);
	quicc_write2(bas, QUICC_REG_SCC_SCCM(bas->chan - 1), 0x0027);
}

/*
 * Low-level UART interface.
 */
static int quicc_probe(struct uart_bas *bas);
static void quicc_init(struct uart_bas *bas, int, int, int, int);
static void quicc_term(struct uart_bas *bas);
static void quicc_putc(struct uart_bas *bas, int);
static int quicc_rxready(struct uart_bas *bas);
static int quicc_getc(struct uart_bas *bas, struct mtx *);

static struct uart_ops uart_quicc_ops = {
	.probe = quicc_probe,
	.init = quicc_init,
	.term = quicc_term,
	.putc = quicc_putc,
	.rxready = quicc_rxready,
	.getc = quicc_getc,
};

static int
quicc_probe(struct uart_bas *bas)
{

	return (0);
}

static void
quicc_init(struct uart_bas *bas, int baudrate, int databits, int stopbits,
    int parity)
{

	quicc_setup(bas, baudrate, databits, stopbits, parity);
}

static void
quicc_term(struct uart_bas *bas)
{
}

static void
quicc_putc(struct uart_bas *bas, int c)
{
	int unit;
	uint16_t toseq;

	unit = bas->chan - 1;
	while (quicc_read2(bas, QUICC_PRAM_SCC_UART_TOSEQ(unit)) & 0x2000)
		DELAY(10);

	toseq = 0x2000 | (c & 0xff);
	quicc_write2(bas, QUICC_PRAM_SCC_UART_TOSEQ(unit), toseq);
}

static int
quicc_rxready(struct uart_bas *bas)
{
	uint16_t rb;

	rb = quicc_read2(bas, QUICC_PRAM_SCC_RBASE(bas->chan - 1));
	return ((quicc_read2(bas, rb) & 0x8000) ? 0 : 1);
}

static int
quicc_getc(struct uart_bas *bas, struct mtx *hwmtx)
{
	volatile char *buf;
	int c;
	uint16_t rb, sc;

	uart_lock(hwmtx);

	rb = quicc_read2(bas, QUICC_PRAM_SCC_RBASE(bas->chan - 1));

	while ((sc = quicc_read2(bas, rb)) & 0x8000) {
		uart_unlock(hwmtx);
		DELAY(4);
		uart_lock(hwmtx);
	}

	buf = (void *)(uintptr_t)quicc_read4(bas, rb + 4);
	c = *buf;
	quicc_write2(bas, rb, sc | 0x8000);

	uart_unlock(hwmtx);

	return (c);
}

/*
 * High-level UART interface.
 */
struct quicc_softc {
	struct uart_softc base;
};

static int quicc_bus_attach(struct uart_softc *);
static int quicc_bus_detach(struct uart_softc *);
static int quicc_bus_flush(struct uart_softc *, int);
static int quicc_bus_getsig(struct uart_softc *);
static int quicc_bus_ioctl(struct uart_softc *, int, intptr_t);
static int quicc_bus_ipend(struct uart_softc *);
static int quicc_bus_param(struct uart_softc *, int, int, int, int);
static int quicc_bus_probe(struct uart_softc *);
static int quicc_bus_receive(struct uart_softc *);
static int quicc_bus_setsig(struct uart_softc *, int);
static int quicc_bus_transmit(struct uart_softc *);
static void quicc_bus_grab(struct uart_softc *);
static void quicc_bus_ungrab(struct uart_softc *);

static kobj_method_t quicc_methods[] = {
	KOBJMETHOD(uart_attach,		quicc_bus_attach),
	KOBJMETHOD(uart_detach,		quicc_bus_detach),
	KOBJMETHOD(uart_flush,		quicc_bus_flush),
	KOBJMETHOD(uart_getsig,		quicc_bus_getsig),
	KOBJMETHOD(uart_ioctl,		quicc_bus_ioctl),
	KOBJMETHOD(uart_ipend,		quicc_bus_ipend),
	KOBJMETHOD(uart_param,		quicc_bus_param),
	KOBJMETHOD(uart_probe,		quicc_bus_probe),
	KOBJMETHOD(uart_receive,	quicc_bus_receive),
	KOBJMETHOD(uart_setsig,		quicc_bus_setsig),
	KOBJMETHOD(uart_transmit,	quicc_bus_transmit),
	KOBJMETHOD(uart_grab,		quicc_bus_grab),
	KOBJMETHOD(uart_ungrab,		quicc_bus_ungrab),
	{ 0, 0 }
};

struct uart_class uart_quicc_class = {
	"quicc",
	quicc_methods,
	sizeof(struct quicc_softc),
	.uc_ops = &uart_quicc_ops,
	.uc_range = 2,
	.uc_rclk = DEFAULT_RCLK,
	.uc_rshift = 0
};

#define	SIGCHG(c, i, s, d)				\
	if (c) {					\
		i |= (i & s) ? s : s | d;		\
	} else {					\
		i = (i & s) ? (i & ~s) | d : i;		\
	}

static int
quicc_bus_attach(struct uart_softc *sc)
{
	struct uart_bas *bas;
	struct uart_devinfo *di;
	uint16_t st, rb;

	bas = &sc->sc_bas;
	if (sc->sc_sysdev != NULL) {
		di = sc->sc_sysdev;
		quicc_param(bas, di->baudrate, di->databits, di->stopbits,
		    di->parity);
	} else {
		quicc_setup(bas, 9600, 8, 1, UART_PARITY_NONE);
	}

	/* Enable interrupts on the receive buffer. */
	rb = quicc_read2(bas, QUICC_PRAM_SCC_RBASE(bas->chan - 1));
	st = quicc_read2(bas, rb);
	quicc_write2(bas, rb, st | 0x9000);

	(void)quicc_bus_getsig(sc);

	return (0);
}

static int
quicc_bus_detach(struct uart_softc *sc)
{

	return (0);
}

static int
quicc_bus_flush(struct uart_softc *sc, int what)
{

	return (0);
}

static int
quicc_bus_getsig(struct uart_softc *sc)
{
	uint32_t new, old, sig;
	uint32_t dummy;

	do {
		old = sc->sc_hwsig;
		sig = old;
		uart_lock(sc->sc_hwmtx);
		/* XXX SIGNALS */
		dummy = 0;
		uart_unlock(sc->sc_hwmtx);
		SIGCHG(dummy, sig, SER_CTS, SER_DCTS);
		SIGCHG(dummy, sig, SER_DCD, SER_DDCD);
		SIGCHG(dummy, sig, SER_DSR, SER_DDSR);
		new = sig & ~SER_MASK_DELTA;
	} while (!atomic_cmpset_32(&sc->sc_hwsig, old, new));
	return (sig);
}

static int
quicc_bus_ioctl(struct uart_softc *sc, int request, intptr_t data)
{
	struct uart_bas *bas;
	uint32_t brg;
	int baudrate, error;

	bas = &sc->sc_bas;
	error = 0;
	uart_lock(sc->sc_hwmtx);
	switch (request) {
	case UART_IOCTL_BREAK:
		break;
	case UART_IOCTL_BAUD:
		brg = quicc_read4(bas, QUICC_REG_BRG(bas->chan - 1)) & 0x1fff;
		brg = (brg & 1) ? (brg + 1) << 3 : (brg + 2) >> 1;
		baudrate = bas->rclk / (brg * 16);
		*(int*)data = baudrate;
		break;
	default:
		error = EINVAL;
		break;
	}
	uart_unlock(sc->sc_hwmtx);
	return (error);
}

static int
quicc_bus_ipend(struct uart_softc *sc)
{
	struct uart_bas *bas;
	int ipend;
	uint16_t scce;

	bas = &sc->sc_bas;
	ipend = 0;

	uart_lock(sc->sc_hwmtx);
	scce = quicc_read2(bas, QUICC_REG_SCC_SCCE(bas->chan - 1));
	quicc_write2(bas, QUICC_REG_SCC_SCCE(bas->chan - 1), ~0);
	uart_unlock(sc->sc_hwmtx);
	if (scce & 0x0001)
		ipend |= SER_INT_RXREADY;
	if (scce & 0x0002)
		ipend |= SER_INT_TXIDLE;
	if (scce & 0x0004)
		ipend |= SER_INT_OVERRUN;
	if (scce & 0x0020)
		ipend |= SER_INT_BREAK;
	/* XXX SIGNALS */
	return (ipend);
}

static int
quicc_bus_param(struct uart_softc *sc, int baudrate, int databits,
    int stopbits, int parity)
{
	int error;

	uart_lock(sc->sc_hwmtx);
	error = quicc_param(&sc->sc_bas, baudrate, databits, stopbits,
	    parity);
	uart_unlock(sc->sc_hwmtx);
	return (error);
}

static int
quicc_bus_probe(struct uart_softc *sc)
{
	char buf[80];
	int error;

	error = quicc_probe(&sc->sc_bas);
	if (error)
		return (error);

	sc->sc_rxfifosz = 1;
	sc->sc_txfifosz = 1;

	snprintf(buf, sizeof(buf), "quicc, channel %d", sc->sc_bas.chan);
	device_set_desc_copy(sc->sc_dev, buf);
	return (0);
}

static int
quicc_bus_receive(struct uart_softc *sc)
{
	struct uart_bas *bas;
	volatile char *buf;
	uint16_t st, rb;

	bas = &sc->sc_bas;
	uart_lock(sc->sc_hwmtx);
	rb = quicc_read2(bas, QUICC_PRAM_SCC_RBASE(bas->chan - 1));
	st = quicc_read2(bas, rb);
	buf = (void *)(uintptr_t)quicc_read4(bas, rb + 4);
	uart_rx_put(sc, *buf);
	quicc_write2(bas, rb, st | 0x9000);
	uart_unlock(sc->sc_hwmtx);
	return (0);
}

static int
quicc_bus_setsig(struct uart_softc *sc, int sig)
{
	struct uart_bas *bas;
	uint32_t new, old;

	bas = &sc->sc_bas;
	do {
		old = sc->sc_hwsig;
		new = old;
		if (sig & SER_DDTR) {
			SIGCHG(sig & SER_DTR, new, SER_DTR,
			    SER_DDTR);
		}
		if (sig & SER_DRTS) {
			SIGCHG(sig & SER_RTS, new, SER_RTS,
			    SER_DRTS);
		}
	} while (!atomic_cmpset_32(&sc->sc_hwsig, old, new));

	uart_lock(sc->sc_hwmtx);
	/* XXX SIGNALS */
	uart_unlock(sc->sc_hwmtx);
	return (0);
}

static int
quicc_bus_transmit(struct uart_softc *sc)
{
	volatile char *buf;
	struct uart_bas *bas;
	uint16_t st, tb;

	bas = &sc->sc_bas;
	uart_lock(sc->sc_hwmtx);
	tb = quicc_read2(bas, QUICC_PRAM_SCC_TBASE(bas->chan - 1));
	st = quicc_read2(bas, tb);
	buf = (void *)(uintptr_t)quicc_read4(bas, tb + 4);
	*buf = sc->sc_txbuf[0];
	quicc_write2(bas, tb + 2, 1);
	quicc_write2(bas, tb, st | 0x9000);
	sc->sc_txbusy = 1;
	uart_unlock(sc->sc_hwmtx);
	return (0);
}

static void
quicc_bus_grab(struct uart_softc *sc)
{
	struct uart_bas *bas;
	uint16_t st, rb;

	/* Disable interrupts on the receive buffer. */
	bas = &sc->sc_bas;
	uart_lock(sc->sc_hwmtx);
	rb = quicc_read2(bas, QUICC_PRAM_SCC_RBASE(bas->chan - 1));
	st = quicc_read2(bas, rb);
	quicc_write2(bas, rb, st & ~0x9000);
	uart_unlock(sc->sc_hwmtx);
}

static void
quicc_bus_ungrab(struct uart_softc *sc)
{
	struct uart_bas *bas;
	uint16_t st, rb;

	/* Enable interrupts on the receive buffer. */
	bas = &sc->sc_bas;
	uart_lock(sc->sc_hwmtx);
	rb = quicc_read2(bas, QUICC_PRAM_SCC_RBASE(bas->chan - 1));
	st = quicc_read2(bas, rb);
	quicc_write2(bas, rb, st | 0x9000);
	uart_unlock(sc->sc_hwmtx);
}

