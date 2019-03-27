/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2003 Marcel Moolenaar
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
#include <machine/bus.h>

#include <dev/uart/uart.h>
#include <dev/uart/uart_cpu.h>
#include <dev/uart/uart_bus.h>

#include <dev/ic/z8530.h>

#include "uart_if.h"

#define	DEFAULT_RCLK	307200

/* Hack! */
#ifdef __powerpc__
#define	UART_PCLK	0
#else
#define	UART_PCLK	MCB2_PCLK
#endif

/* Multiplexed I/O. */
static __inline void
uart_setmreg(struct uart_bas *bas, int reg, int val)
{

	uart_setreg(bas, REG_CTRL, reg);
	uart_barrier(bas);
	uart_setreg(bas, REG_CTRL, val);
}

static __inline uint8_t
uart_getmreg(struct uart_bas *bas, int reg)
{

	uart_setreg(bas, REG_CTRL, reg);
	uart_barrier(bas);
	return (uart_getreg(bas, REG_CTRL));
}

static int
z8530_divisor(int rclk, int baudrate)
{
	int act_baud, divisor, error;

	if (baudrate == 0)
		return (-1);

	divisor = (rclk + baudrate) / (baudrate << 1) - 2;
	if (divisor < 0 || divisor >= 65536)
		return (-1);
	act_baud = rclk / 2 / (divisor + 2);

	/* 10 times error in percent: */
	error = ((act_baud - baudrate) * 2000 / baudrate + 1) >> 1;

	/* 3.0% maximum error tolerance: */
	if (error < -30 || error > 30)
		return (-1);

	return (divisor);
}

static int
z8530_param(struct uart_bas *bas, int baudrate, int databits, int stopbits,
    int parity, uint8_t *tpcp)
{
	int divisor;
	uint8_t mpm, rpc, tpc;

	rpc = RPC_RXE;
	mpm = MPM_CM16;
	tpc = TPC_TXE | (*tpcp & (TPC_DTR | TPC_RTS));

	if (databits >= 8) {
		rpc |= RPC_RB8;
		tpc |= TPC_TB8;
	} else if (databits == 7) {
		rpc |= RPC_RB7;
		tpc |= TPC_TB7;
	} else if (databits == 6) {
		rpc |= RPC_RB6;
		tpc |= TPC_TB6;
	} else {
		rpc |= RPC_RB5;
		tpc |= TPC_TB5;
	}
	mpm |= (stopbits > 1) ? MPM_SB2 : MPM_SB1;
	switch (parity) {
	case UART_PARITY_EVEN:	mpm |= MPM_PE | MPM_EVEN; break;
	case UART_PARITY_NONE:	break;
	case UART_PARITY_ODD:	mpm |= MPM_PE; break;
	default:		return (EINVAL);
	}

	if (baudrate > 0) {
		divisor = z8530_divisor(bas->rclk, baudrate);
		if (divisor == -1)
			return (EINVAL);
	} else
		divisor = -1;

	uart_setmreg(bas, WR_MCB2, UART_PCLK);
	uart_barrier(bas);

	if (divisor >= 0) {
		uart_setmreg(bas, WR_TCL, divisor & 0xff);
		uart_barrier(bas);
		uart_setmreg(bas, WR_TCH, (divisor >> 8) & 0xff);
		uart_barrier(bas);
	}

	uart_setmreg(bas, WR_RPC, rpc);
	uart_barrier(bas);
	uart_setmreg(bas, WR_MPM, mpm);
	uart_barrier(bas);
	uart_setmreg(bas, WR_TPC, tpc);
	uart_barrier(bas);
	uart_setmreg(bas, WR_MCB2, UART_PCLK | MCB2_BRGE);
	uart_barrier(bas);
	*tpcp = tpc;
	return (0);
}

static int
z8530_setup(struct uart_bas *bas, int baudrate, int databits, int stopbits,
    int parity)
{
	uint8_t tpc;

	if (bas->rclk == 0)
		bas->rclk = DEFAULT_RCLK;

	/* Assume we don't need to perform a full hardware reset. */
	switch (bas->chan) {
	case 1:
		uart_setmreg(bas, WR_MIC, MIC_NV | MIC_CRA);
		break;
	case 2:
		uart_setmreg(bas, WR_MIC, MIC_NV | MIC_CRB);
		break;
	}
	uart_barrier(bas);
	/* Set clock sources. */
	uart_setmreg(bas, WR_CMC, CMC_RC_BRG | CMC_TC_BRG);
	uart_setmreg(bas, WR_MCB2, UART_PCLK);
	uart_barrier(bas);
	/* Set data encoding. */
	uart_setmreg(bas, WR_MCB1, MCB1_NRZ);
	uart_barrier(bas);

	tpc = TPC_DTR | TPC_RTS;
	z8530_param(bas, baudrate, databits, stopbits, parity, &tpc);
	return (int)tpc;
}

/*
 * Low-level UART interface.
 */
static int z8530_probe(struct uart_bas *bas);
static void z8530_init(struct uart_bas *bas, int, int, int, int);
static void z8530_term(struct uart_bas *bas);
static void z8530_putc(struct uart_bas *bas, int);
static int z8530_rxready(struct uart_bas *bas);
static int z8530_getc(struct uart_bas *bas, struct mtx *);

static struct uart_ops uart_z8530_ops = {
	.probe = z8530_probe,
	.init = z8530_init,
	.term = z8530_term,
	.putc = z8530_putc,
	.rxready = z8530_rxready,
	.getc = z8530_getc,
};

static int
z8530_probe(struct uart_bas *bas)
{

	return (0);
}

static void
z8530_init(struct uart_bas *bas, int baudrate, int databits, int stopbits,
    int parity)
{

	z8530_setup(bas, baudrate, databits, stopbits, parity);
}

static void
z8530_term(struct uart_bas *bas)
{
}

static void
z8530_putc(struct uart_bas *bas, int c)
{

	while (!(uart_getreg(bas, REG_CTRL) & BES_TXE))
		;
	uart_setreg(bas, REG_DATA, c);
	uart_barrier(bas);
}

static int
z8530_rxready(struct uart_bas *bas)
{

	return ((uart_getreg(bas, REG_CTRL) & BES_RXA) != 0 ? 1 : 0);
}

static int
z8530_getc(struct uart_bas *bas, struct mtx *hwmtx)
{
	int c;

	uart_lock(hwmtx);

	while (!(uart_getreg(bas, REG_CTRL) & BES_RXA)) {
		uart_unlock(hwmtx);
		DELAY(10);
		uart_lock(hwmtx);
	}

	c = uart_getreg(bas, REG_DATA);

	uart_unlock(hwmtx);

	return (c);
}

/*
 * High-level UART interface.
 */
struct z8530_softc {
	struct uart_softc base;
	uint8_t	tpc;
	uint8_t	txidle;
};

static int z8530_bus_attach(struct uart_softc *);
static int z8530_bus_detach(struct uart_softc *);
static int z8530_bus_flush(struct uart_softc *, int);
static int z8530_bus_getsig(struct uart_softc *);
static int z8530_bus_ioctl(struct uart_softc *, int, intptr_t);
static int z8530_bus_ipend(struct uart_softc *);
static int z8530_bus_param(struct uart_softc *, int, int, int, int);
static int z8530_bus_probe(struct uart_softc *);
static int z8530_bus_receive(struct uart_softc *);
static int z8530_bus_setsig(struct uart_softc *, int);
static int z8530_bus_transmit(struct uart_softc *);
static void z8530_bus_grab(struct uart_softc *);
static void z8530_bus_ungrab(struct uart_softc *);

static kobj_method_t z8530_methods[] = {
	KOBJMETHOD(uart_attach,		z8530_bus_attach),
	KOBJMETHOD(uart_detach,		z8530_bus_detach),
	KOBJMETHOD(uart_flush,		z8530_bus_flush),
	KOBJMETHOD(uart_getsig,		z8530_bus_getsig),
	KOBJMETHOD(uart_ioctl,		z8530_bus_ioctl),
	KOBJMETHOD(uart_ipend,		z8530_bus_ipend),
	KOBJMETHOD(uart_param,		z8530_bus_param),
	KOBJMETHOD(uart_probe,		z8530_bus_probe),
	KOBJMETHOD(uart_receive,	z8530_bus_receive),
	KOBJMETHOD(uart_setsig,		z8530_bus_setsig),
	KOBJMETHOD(uart_transmit,	z8530_bus_transmit),
	KOBJMETHOD(uart_grab,		z8530_bus_grab),
	KOBJMETHOD(uart_ungrab,		z8530_bus_ungrab),
	{ 0, 0 }
};

struct uart_class uart_z8530_class = {
	"z8530",
	z8530_methods,
	sizeof(struct z8530_softc),
	.uc_ops = &uart_z8530_ops,
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
z8530_bus_attach(struct uart_softc *sc)
{
	struct z8530_softc *z8530 = (struct z8530_softc*)sc;
	struct uart_bas *bas;
	struct uart_devinfo *di;

	bas = &sc->sc_bas;
	if (sc->sc_sysdev != NULL) {
		di = sc->sc_sysdev;
		z8530->tpc = TPC_DTR|TPC_RTS;
		z8530_param(bas, di->baudrate, di->databits, di->stopbits,
		    di->parity, &z8530->tpc);
	} else {
		z8530->tpc = z8530_setup(bas, 9600, 8, 1, UART_PARITY_NONE);
		z8530->tpc &= ~(TPC_DTR|TPC_RTS);
	}
	z8530->txidle = 1;	/* Report SER_INT_TXIDLE. */

	(void)z8530_bus_getsig(sc);

	uart_setmreg(bas, WR_IC, IC_BRK | IC_CTS | IC_DCD);
	uart_barrier(bas);
	uart_setmreg(bas, WR_IDT, IDT_XIE | IDT_TIE | IDT_RIA);
	uart_barrier(bas);
	uart_setmreg(bas, WR_IV, 0);
	uart_barrier(bas);
	uart_setmreg(bas, WR_TPC, z8530->tpc);
	uart_barrier(bas);
	uart_setmreg(bas, WR_MIC, MIC_NV | MIC_MIE);
	uart_barrier(bas);
	return (0);
}

static int
z8530_bus_detach(struct uart_softc *sc)
{

	return (0);
}

static int
z8530_bus_flush(struct uart_softc *sc, int what)
{

	return (0);
}

static int
z8530_bus_getsig(struct uart_softc *sc)
{
	uint32_t new, old, sig;
	uint8_t bes;

	do {
		old = sc->sc_hwsig;
		sig = old;
		uart_lock(sc->sc_hwmtx);
		bes = uart_getmreg(&sc->sc_bas, RR_BES);
		uart_unlock(sc->sc_hwmtx);
		SIGCHG(bes & BES_CTS, sig, SER_CTS, SER_DCTS);
		SIGCHG(bes & BES_DCD, sig, SER_DCD, SER_DDCD);
		SIGCHG(bes & BES_SYNC, sig, SER_DSR, SER_DDSR);
		new = sig & ~SER_MASK_DELTA;
	} while (!atomic_cmpset_32(&sc->sc_hwsig, old, new));
	return (sig);
}

static int
z8530_bus_ioctl(struct uart_softc *sc, int request, intptr_t data)
{
	struct z8530_softc *z8530 = (struct z8530_softc*)sc;
	struct uart_bas *bas;
	int baudrate, divisor, error;

	bas = &sc->sc_bas;
	error = 0;
	uart_lock(sc->sc_hwmtx);
	switch (request) {
	case UART_IOCTL_BREAK:
		if (data)
			z8530->tpc |= TPC_BRK;
		else
			z8530->tpc &= ~TPC_BRK;
		uart_setmreg(bas, WR_TPC, z8530->tpc);
		uart_barrier(bas);
		break;
	case UART_IOCTL_BAUD:
		divisor = uart_getmreg(bas, RR_TCH);
		divisor = (divisor << 8) | uart_getmreg(bas, RR_TCL);
		baudrate = bas->rclk / 2 / (divisor + 2);
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
z8530_bus_ipend(struct uart_softc *sc)
{
	struct z8530_softc *z8530 = (struct z8530_softc*)sc;
	struct uart_bas *bas;
	int ipend;
	uint32_t sig;
	uint8_t bes, ip, iv, src;

	bas = &sc->sc_bas;
	ipend = 0;

	uart_lock(sc->sc_hwmtx);
	switch (bas->chan) {
	case 1:
		ip = uart_getmreg(bas, RR_IP);
		break;
	case 2:	/* XXX hack!!! */
		iv = uart_getmreg(bas, RR_IV) & 0x0E;
		switch (iv) {
		case IV_TEB:	ip = IP_TIA; break;
		case IV_XSB:	ip = IP_SIA; break;
		case IV_RAB:	ip = IP_RIA; break;
		default:	ip = 0; break;
		}
		break;
	default:
		ip = 0;
		break;
	}

	if (ip & IP_RIA)
		ipend |= SER_INT_RXREADY;

	if (ip & IP_TIA) {
		uart_setreg(bas, REG_CTRL, CR_RSTTXI);
		uart_barrier(bas);
		if (z8530->txidle) {
			ipend |= SER_INT_TXIDLE;
			z8530->txidle = 0;	/* Mask SER_INT_TXIDLE. */
		}
	}

	if (ip & IP_SIA) {
		uart_setreg(bas, REG_CTRL, CR_RSTXSI);
		uart_barrier(bas);
		bes = uart_getmreg(bas, RR_BES);
		if (bes & BES_BRK)
			ipend |= SER_INT_BREAK;
		sig = sc->sc_hwsig;
		SIGCHG(bes & BES_CTS, sig, SER_CTS, SER_DCTS);
		SIGCHG(bes & BES_DCD, sig, SER_DCD, SER_DDCD);
		SIGCHG(bes & BES_SYNC, sig, SER_DSR, SER_DDSR);
		if (sig & SER_MASK_DELTA)
			ipend |= SER_INT_SIGCHG;
		src = uart_getmreg(bas, RR_SRC);
		if (src & SRC_OVR) {
			uart_setreg(bas, REG_CTRL, CR_RSTERR);
			uart_barrier(bas);
			ipend |= SER_INT_OVERRUN;
		}
	}

	if (ipend) {
		uart_setreg(bas, REG_CTRL, CR_RSTIUS);
		uart_barrier(bas);
	}

	uart_unlock(sc->sc_hwmtx);

	return (ipend);
}

static int
z8530_bus_param(struct uart_softc *sc, int baudrate, int databits,
    int stopbits, int parity)
{
	struct z8530_softc *z8530 = (struct z8530_softc*)sc;
	int error;

	uart_lock(sc->sc_hwmtx);
	error = z8530_param(&sc->sc_bas, baudrate, databits, stopbits, parity,
	    &z8530->tpc);
	uart_unlock(sc->sc_hwmtx);
	return (error);
}

static int
z8530_bus_probe(struct uart_softc *sc)
{
	char buf[80];
	int error;
	char ch;

	error = z8530_probe(&sc->sc_bas);
	if (error)
		return (error);

	sc->sc_rxfifosz = 3;
	sc->sc_txfifosz = 1;

	ch = sc->sc_bas.chan - 1 + 'A';

	snprintf(buf, sizeof(buf), "z8530, channel %c", ch);
	device_set_desc_copy(sc->sc_dev, buf);
	return (0);
}

static int
z8530_bus_receive(struct uart_softc *sc)
{
	struct uart_bas *bas;
	int xc;
	uint8_t bes, src;

	bas = &sc->sc_bas;
	uart_lock(sc->sc_hwmtx);
	bes = uart_getmreg(bas, RR_BES);
	while (bes & BES_RXA) {
		if (uart_rx_full(sc)) {
			sc->sc_rxbuf[sc->sc_rxput] = UART_STAT_OVERRUN;
			break;
		}
		xc = uart_getreg(bas, REG_DATA);
		uart_barrier(bas);
		src = uart_getmreg(bas, RR_SRC);
		if (src & SRC_FE)
			xc |= UART_STAT_FRAMERR;
		if (src & SRC_PE)
			xc |= UART_STAT_PARERR;
		if (src & SRC_OVR)
			xc |= UART_STAT_OVERRUN;
		uart_rx_put(sc, xc);
		if (src & (SRC_FE | SRC_PE | SRC_OVR)) {
			uart_setreg(bas, REG_CTRL, CR_RSTERR);
			uart_barrier(bas);
		}
		bes = uart_getmreg(bas, RR_BES);
	}
	/* Discard everything left in the Rx FIFO. */
	while (bes & BES_RXA) {
		(void)uart_getreg(bas, REG_DATA);
		uart_barrier(bas);
		src = uart_getmreg(bas, RR_SRC);
		if (src & (SRC_FE | SRC_PE | SRC_OVR)) {
			uart_setreg(bas, REG_CTRL, CR_RSTERR);
			uart_barrier(bas);
		}
		bes = uart_getmreg(bas, RR_BES);
	}
	uart_unlock(sc->sc_hwmtx);
	return (0);
}

static int
z8530_bus_setsig(struct uart_softc *sc, int sig)
{
	struct z8530_softc *z8530 = (struct z8530_softc*)sc;
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
	if (new & SER_DTR)
		z8530->tpc |= TPC_DTR;
	else
		z8530->tpc &= ~TPC_DTR;
	if (new & SER_RTS)
		z8530->tpc |= TPC_RTS;
	else
		z8530->tpc &= ~TPC_RTS;
	uart_setmreg(bas, WR_TPC, z8530->tpc);
	uart_barrier(bas);
	uart_unlock(sc->sc_hwmtx);
	return (0);
}

static int
z8530_bus_transmit(struct uart_softc *sc)
{
	struct z8530_softc *z8530 = (struct z8530_softc*)sc;
	struct uart_bas *bas;

	bas = &sc->sc_bas;
	uart_lock(sc->sc_hwmtx);
	while (!(uart_getmreg(bas, RR_BES) & BES_TXE))
		;
	uart_setreg(bas, REG_DATA, sc->sc_txbuf[0]);
	uart_barrier(bas);
	sc->sc_txbusy = 1;
	z8530->txidle = 1;	/* Report SER_INT_TXIDLE again. */
	uart_unlock(sc->sc_hwmtx);
	return (0);
}

static void
z8530_bus_grab(struct uart_softc *sc)
{
	struct uart_bas *bas;

	bas = &sc->sc_bas;
	uart_lock(sc->sc_hwmtx);
	uart_setmreg(bas, WR_IDT, IDT_XIE | IDT_TIE);
	uart_barrier(bas);
	uart_unlock(sc->sc_hwmtx);
}

static void
z8530_bus_ungrab(struct uart_softc *sc)
{
	struct uart_bas *bas;

	bas = &sc->sc_bas;
	uart_lock(sc->sc_hwmtx);
	uart_setmreg(bas, WR_IDT, IDT_XIE | IDT_TIE | IDT_RIA);
	uart_barrier(bas);
	uart_unlock(sc->sc_hwmtx);
}
