/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2013 Adrian Chadd <adrian@FreeBSD.org>
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

#include <mips/atheros/ar933x_uart.h>

#include "uart_if.h"

/*
 * Default system clock is 25MHz; see ar933x_chip.c for how
 * the startup process determines whether it's 25MHz or 40MHz.
 */
#define	DEFAULT_RCLK	(25 * 1000 * 1000)

#define	ar933x_getreg(bas, reg)           \
	bus_space_read_4((bas)->bst, (bas)->bsh, reg)
#define	ar933x_setreg(bas, reg, value)    \
	bus_space_write_4((bas)->bst, (bas)->bsh, reg, value)



static int
ar933x_drain(struct uart_bas *bas, int what)
{
	int limit;

	if (what & UART_DRAIN_TRANSMITTER) {
		limit = 10*1024;

		/* Loop over until the TX FIFO shows entirely clear */
		while (--limit) {
			if ((ar933x_getreg(bas, AR933X_UART_CS_REG)
			    & AR933X_UART_CS_TX_BUSY) == 0)
				break;
		}
		if (limit == 0) {
			return (EIO);
		}
	}

	if (what & UART_DRAIN_RECEIVER) {
		limit=10*4096;
		while (--limit) {

			/* XXX duplicated from ar933x_getc() */
			/* XXX TODO: refactor! */

			/* If there's nothing to read, stop! */
			if ((ar933x_getreg(bas, AR933X_UART_DATA_REG) &
			    AR933X_UART_DATA_RX_CSR) == 0) {
				break;
			}

			/* Read the top of the RX FIFO */
			(void) ar933x_getreg(bas, AR933X_UART_DATA_REG);

			/* Remove that entry from said RX FIFO */
			ar933x_setreg(bas, AR933X_UART_DATA_REG,
			    AR933X_UART_DATA_RX_CSR);

			uart_barrier(bas);
			DELAY(2);
		}
		if (limit == 0) {
			return (EIO);
		}
	}
	return (0);
}

/*
 * Calculate the baud from the given chip configuration parameters.
 */
static unsigned long
ar933x_uart_get_baud(unsigned int clk, unsigned int scale,
    unsigned int step)
{
	uint64_t t;
	uint32_t div;

	div = (2 << 16) * (scale + 1);
	t = clk;
	t *= step;
	t += (div / 2);
	t = t / div;

	return (t);
}

/*
 * Calculate the scale/step with the lowest possible deviation from
 * the target baudrate.
 */
static void
ar933x_uart_get_scale_step(struct uart_bas *bas, unsigned int baud,
    unsigned int *scale, unsigned int *step)
{
	unsigned int tscale;
	uint32_t clk;
	long min_diff;

	clk = bas->rclk;
	*scale = 0;
	*step = 0;

	min_diff = baud;
	for (tscale = 0; tscale < AR933X_UART_MAX_SCALE; tscale++) {
		uint64_t tstep;
		int diff;

		tstep = baud * (tscale + 1);
		tstep *= (2 << 16);
		tstep = tstep / clk;

		if (tstep > AR933X_UART_MAX_STEP)
			break;

		diff = abs(ar933x_uart_get_baud(clk, tscale, tstep) - baud);
		if (diff < min_diff) {
			min_diff = diff;
			*scale = tscale;
			*step = tstep;
		}
	}
}

static int
ar933x_param(struct uart_bas *bas, int baudrate, int databits, int stopbits,
    int parity)
{
	/* UART always 8 bits */

	/* UART always 1 stop bit */

	/* UART parity is controllable by bits 0:1, ignore for now */

	/* Set baudrate if required. */
	if (baudrate > 0) {
		uint32_t clock_scale, clock_step;

		/* Find the best fit for the given baud rate */
		ar933x_uart_get_scale_step(bas, baudrate, &clock_scale,
		    &clock_step);

		/*
		 * Program the clock register in its entirety - no need
		 * for Read-Modify-Write.
		 */
		ar933x_setreg(bas, AR933X_UART_CLOCK_REG,
		    ((clock_scale & AR933X_UART_CLOCK_SCALE_M)
		      << AR933X_UART_CLOCK_SCALE_S) |
		    (clock_step & AR933X_UART_CLOCK_STEP_M));
	}

	uart_barrier(bas);
	return (0);
}


/*
 * Low-level UART interface.
 */
static int ar933x_probe(struct uart_bas *bas);
static void ar933x_init(struct uart_bas *bas, int, int, int, int);
static void ar933x_term(struct uart_bas *bas);
static void ar933x_putc(struct uart_bas *bas, int);
static int ar933x_rxready(struct uart_bas *bas);
static int ar933x_getc(struct uart_bas *bas, struct mtx *);

static struct uart_ops uart_ar933x_ops = {
	.probe = ar933x_probe,
	.init = ar933x_init,
	.term = ar933x_term,
	.putc = ar933x_putc,
	.rxready = ar933x_rxready,
	.getc = ar933x_getc,
};

static int
ar933x_probe(struct uart_bas *bas)
{

	/* We always know this will be here */
	return (0);
}

static void
ar933x_init(struct uart_bas *bas, int baudrate, int databits, int stopbits,
    int parity)
{
	uint32_t reg;

	/* Setup default parameters */
	ar933x_param(bas, baudrate, databits, stopbits, parity);

	/* XXX Force enable UART in case it was disabled */

	/* Disable all interrupts */
	ar933x_setreg(bas, AR933X_UART_INT_EN_REG, 0x00000000);

	/* Disable the host interrupt */
	reg = ar933x_getreg(bas, AR933X_UART_CS_REG);
	reg &= ~AR933X_UART_CS_HOST_INT_EN;
	ar933x_setreg(bas, AR933X_UART_CS_REG, reg);

	uart_barrier(bas);

	/* XXX Set RTS/DTR? */
}

/*
 * Detach from console.
 */
static void
ar933x_term(struct uart_bas *bas)
{

	/* XXX TODO */
}

static void
ar933x_putc(struct uart_bas *bas, int c)
{
	int limit;

	limit = 250000;

	/* Wait for space in the TX FIFO */
	while ( ((ar933x_getreg(bas, AR933X_UART_DATA_REG) &
	    AR933X_UART_DATA_TX_CSR) == 0) && --limit)
		DELAY(4);

	/* Write the actual byte */
	ar933x_setreg(bas, AR933X_UART_DATA_REG,
	    (c & 0xff) | AR933X_UART_DATA_TX_CSR);
}

static int
ar933x_rxready(struct uart_bas *bas)
{

	/* Wait for a character to come ready */
	return (!!(ar933x_getreg(bas, AR933X_UART_DATA_REG)
	    & AR933X_UART_DATA_RX_CSR));
}

static int
ar933x_getc(struct uart_bas *bas, struct mtx *hwmtx)
{
	int c;

	uart_lock(hwmtx);

	/* Wait for a character to come ready */
	while ((ar933x_getreg(bas, AR933X_UART_DATA_REG) &
	    AR933X_UART_DATA_RX_CSR) == 0) {
		uart_unlock(hwmtx);
		DELAY(4);
		uart_lock(hwmtx);
	}

	/* Read the top of the RX FIFO */
	c = ar933x_getreg(bas, AR933X_UART_DATA_REG) & 0xff;

	/* Remove that entry from said RX FIFO */
	ar933x_setreg(bas, AR933X_UART_DATA_REG, AR933X_UART_DATA_RX_CSR);

	uart_unlock(hwmtx);

	return (c);
}

/*
 * High-level UART interface.
 */
struct ar933x_softc {
	struct uart_softc base;

	uint32_t	u_ier;
};

static int ar933x_bus_attach(struct uart_softc *);
static int ar933x_bus_detach(struct uart_softc *);
static int ar933x_bus_flush(struct uart_softc *, int);
static int ar933x_bus_getsig(struct uart_softc *);
static int ar933x_bus_ioctl(struct uart_softc *, int, intptr_t);
static int ar933x_bus_ipend(struct uart_softc *);
static int ar933x_bus_param(struct uart_softc *, int, int, int, int);
static int ar933x_bus_probe(struct uart_softc *);
static int ar933x_bus_receive(struct uart_softc *);
static int ar933x_bus_setsig(struct uart_softc *, int);
static int ar933x_bus_transmit(struct uart_softc *);
static void ar933x_bus_grab(struct uart_softc *);
static void ar933x_bus_ungrab(struct uart_softc *);

static kobj_method_t ar933x_methods[] = {
	KOBJMETHOD(uart_attach,		ar933x_bus_attach),
	KOBJMETHOD(uart_detach,		ar933x_bus_detach),
	KOBJMETHOD(uart_flush,		ar933x_bus_flush),
	KOBJMETHOD(uart_getsig,		ar933x_bus_getsig),
	KOBJMETHOD(uart_ioctl,		ar933x_bus_ioctl),
	KOBJMETHOD(uart_ipend,		ar933x_bus_ipend),
	KOBJMETHOD(uart_param,		ar933x_bus_param),
	KOBJMETHOD(uart_probe,		ar933x_bus_probe),
	KOBJMETHOD(uart_receive,	ar933x_bus_receive),
	KOBJMETHOD(uart_setsig,		ar933x_bus_setsig),
	KOBJMETHOD(uart_transmit,	ar933x_bus_transmit),
	KOBJMETHOD(uart_grab,		ar933x_bus_grab),
	KOBJMETHOD(uart_ungrab,		ar933x_bus_ungrab),
	{ 0, 0 }
};

struct uart_class uart_ar933x_class = {
	"ar933x",
	ar933x_methods,
	sizeof(struct ar933x_softc),
	.uc_ops = &uart_ar933x_ops,
	.uc_range = 8,
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
ar933x_bus_attach(struct uart_softc *sc)
{
	struct ar933x_softc *u = (struct ar933x_softc *)sc;
	struct uart_bas *bas = &sc->sc_bas;
	uint32_t reg;

	/* XXX TODO: flush transmitter */

	/*
	 * Setup initial interrupt notifications.
	 *
	 * XXX for now, just RX FIFO valid.
	 * Later on (when they're handled), also handle
	 * RX errors/overflow.
	 */
	u->u_ier = AR933X_UART_INT_RX_VALID;

	/* Enable RX interrupts to kick-start things */
	ar933x_setreg(bas, AR933X_UART_INT_EN_REG, u->u_ier);

	/* Enable the host interrupt now */
	reg = ar933x_getreg(bas, AR933X_UART_CS_REG);
	reg |= AR933X_UART_CS_HOST_INT_EN;
	ar933x_setreg(bas, AR933X_UART_CS_REG, reg);

	return (0);
}

static int
ar933x_bus_detach(struct uart_softc *sc)
{
	struct uart_bas *bas = &sc->sc_bas;
	uint32_t reg;

	/* Disable all interrupts */
	ar933x_setreg(bas, AR933X_UART_INT_EN_REG, 0x00000000);

	/* Disable the host interrupt */
	reg = ar933x_getreg(bas, AR933X_UART_CS_REG);
	reg &= ~AR933X_UART_CS_HOST_INT_EN;
	ar933x_setreg(bas, AR933X_UART_CS_REG, reg);
	uart_barrier(bas);

	return (0);
}

static int
ar933x_bus_flush(struct uart_softc *sc, int what)
{
	struct uart_bas *bas;

	bas = &sc->sc_bas;
	uart_lock(sc->sc_hwmtx);
	ar933x_drain(bas, what);
	uart_unlock(sc->sc_hwmtx);

	return (0);
}

static int
ar933x_bus_getsig(struct uart_softc *sc)
{
	uint32_t sig = sc->sc_hwsig;

	/*
	 * For now, let's just return that DSR/DCD/CTS is asserted.
	 */
	SIGCHG(1, sig, SER_DSR, SER_DDSR);
	SIGCHG(1, sig, SER_CTS, SER_DCTS);
	SIGCHG(1, sig, SER_DCD, SER_DDCD);
	SIGCHG(1, sig,  SER_RI,  SER_DRI);

	sc->sc_hwsig = sig & ~SER_MASK_DELTA;

	return (sig);
}

/*
 * XXX TODO: actually implement the rest of this!
 */
static int
ar933x_bus_ioctl(struct uart_softc *sc, int request, intptr_t data)
{
	int error = 0;

	/* XXX lock */
	switch (request) {
	case UART_IOCTL_BREAK:
	case UART_IOCTL_IFLOW:
	case UART_IOCTL_OFLOW:
		break;
	case UART_IOCTL_BAUD:
		*(int*)data = 115200;
		break;
	default:
		error = EINVAL;
		break;
	}

	/* XXX unlock */

	return (error);
}

/*
 * Bus interrupt handler.
 *
 * For now, system interrupts are disabled.
 * So this is just called from a callout in uart_core.c
 * to poll various state.
 */
static int
ar933x_bus_ipend(struct uart_softc *sc)
{
	struct ar933x_softc *u = (struct ar933x_softc *)sc;
	struct uart_bas *bas = &sc->sc_bas;
	int ipend = 0;
	uint32_t isr;

	uart_lock(sc->sc_hwmtx);

	/*
	 * Fetch/ACK the ISR status.
	 */
	isr = ar933x_getreg(bas, AR933X_UART_INT_REG);
	ar933x_setreg(bas, AR933X_UART_INT_REG, isr);
	uart_barrier(bas);

	/*
	 * RX ready - notify upper layer.
	 */
	if (isr & AR933X_UART_INT_RX_VALID) {
		ipend |= SER_INT_RXREADY;
	}

	/*
	 * If we get this interrupt, we should disable
	 * it from the interrupt mask and inform the uart
	 * driver appropriately.
	 *
	 * We can't keep setting SER_INT_TXIDLE or SER_INT_SIGCHG
	 * all the time or IO stops working.  So we will always
	 * clear this interrupt if we get it, then we only signal
	 * the upper layer if we were doing active TX in the
	 * first place.
	 *
	 * Also, the name is misleading.  This actually means
	 * "the FIFO is almost empty."  So if we just write some
	 * more data to the FIFO without checking whether it can
	 * take said data, we'll overflow the thing.
	 *
	 * Unfortunately the FreeBSD uart device has no concept of
	 * partial UART writes - it expects that the whole buffer
	 * is written to the hardware.  Thus for now, ar933x_bus_transmit()
	 * will wait for the FIFO to finish draining before it pushes
	 * more frames into it.
	 */
	if (isr & AR933X_UART_INT_TX_EMPTY) {
		/*
		 * Update u_ier to disable TX notifications; update hardware
		 */
		u->u_ier &= ~AR933X_UART_INT_TX_EMPTY;
		ar933x_setreg(bas, AR933X_UART_INT_EN_REG, u->u_ier);
		uart_barrier(bas);
	}

	/*
	 * Only signal TX idle if we're not busy transmitting.
	 *
	 * XXX I never get _out_ of txbusy? Debug that!
	 */
	if (sc->sc_txbusy) {
		if (isr & AR933X_UART_INT_TX_EMPTY) {
			ipend |= SER_INT_TXIDLE;
		} else {
			ipend |= SER_INT_SIGCHG;
		}
	}

	uart_unlock(sc->sc_hwmtx);
	return (ipend);
}

static int
ar933x_bus_param(struct uart_softc *sc, int baudrate, int databits,
    int stopbits, int parity)
{
	struct uart_bas *bas;
	int error;

	bas = &sc->sc_bas;
	uart_lock(sc->sc_hwmtx);
	error = ar933x_param(bas, baudrate, databits, stopbits, parity);
	uart_unlock(sc->sc_hwmtx);
	return (error);
}

static int
ar933x_bus_probe(struct uart_softc *sc)
{
	struct uart_bas *bas;
	int error;

	bas = &sc->sc_bas;

	error = ar933x_probe(bas);
	if (error)
		return (error);

	/* Reset FIFOs. */
	ar933x_drain(bas, UART_FLUSH_RECEIVER|UART_FLUSH_TRANSMITTER);

	/* XXX TODO: actually find out what the FIFO depth is! */
	sc->sc_rxfifosz = 16;
	sc->sc_txfifosz = 16;

	return (0);
}

static int
ar933x_bus_receive(struct uart_softc *sc)
{
	struct uart_bas *bas = &sc->sc_bas;
	int xc;

	uart_lock(sc->sc_hwmtx);

	/* Loop over until we are full, or no data is available */
	while (ar933x_rxready(bas)) {
		if (uart_rx_full(sc)) {
			sc->sc_rxbuf[sc->sc_rxput] = UART_STAT_OVERRUN;
			break;
		}

		/* Read the top of the RX FIFO */
		xc = ar933x_getreg(bas, AR933X_UART_DATA_REG) & 0xff;

		/* Remove that entry from said RX FIFO */
		ar933x_setreg(bas, AR933X_UART_DATA_REG,
		    AR933X_UART_DATA_RX_CSR);
		uart_barrier(bas);

		/* XXX frame, parity error */
		uart_rx_put(sc, xc);
	}

	/*
	 * XXX TODO: Discard everything left in the Rx FIFO?
	 * XXX only if we've hit an overrun condition?
	 */

	uart_unlock(sc->sc_hwmtx);

	return (0);
}

static int
ar933x_bus_setsig(struct uart_softc *sc, int sig)
{
#if 0
	struct ar933x_softc *ns8250 = (struct ar933x_softc*)sc;
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
	ns8250->mcr &= ~(MCR_DTR|MCR_RTS);
	if (new & SER_DTR)
		ns8250->mcr |= MCR_DTR;
	if (new & SER_RTS)
		ns8250->mcr |= MCR_RTS;
	uart_setreg(bas, REG_MCR, ns8250->mcr);
	uart_barrier(bas);
	uart_unlock(sc->sc_hwmtx);
#endif
	return (0);
}

/*
 * Write the current transmit buffer to the TX FIFO.
 *
 * Unfortunately the FreeBSD uart device has no concept of
 * partial UART writes - it expects that the whole buffer
 * is written to the hardware.  Thus for now, this will wait for
 * the FIFO to finish draining before it pushes more frames into it.
 *
 * If non-blocking operation is truely needed here, either
 * the FreeBSD uart device will need to handle partial writes
 * in xxx_bus_transmit(), or we'll need to do TX FIFO buffering
 * of our own here.
 */
static int
ar933x_bus_transmit(struct uart_softc *sc)
{
	struct uart_bas *bas = &sc->sc_bas;
	struct ar933x_softc *u = (struct ar933x_softc *)sc;
	int i;

	uart_lock(sc->sc_hwmtx);

	/* Wait for the FIFO to be clear - see above */
	while (ar933x_getreg(bas, AR933X_UART_CS_REG) &
	    AR933X_UART_CS_TX_BUSY)
		;

	/*
	 * Write some data!
	 */
	for (i = 0; i < sc->sc_txdatasz; i++) {
		/* Write the TX data */
		ar933x_setreg(bas, AR933X_UART_DATA_REG,
		    (sc->sc_txbuf[i] & 0xff) | AR933X_UART_DATA_TX_CSR);
		uart_barrier(bas);
	}

	/*
	 * Now that we're transmitting, get interrupt notification
	 * when the FIFO is (almost) empty - see above.
	 */
	u->u_ier |= AR933X_UART_INT_TX_EMPTY;
	ar933x_setreg(bas, AR933X_UART_INT_EN_REG, u->u_ier);
	uart_barrier(bas);

	/*
	 * Inform the upper layer that we are presently transmitting
	 * data to the hardware; this will be cleared when the
	 * TXIDLE interrupt occurs.
	 */
	sc->sc_txbusy = 1;
	uart_unlock(sc->sc_hwmtx);

	return (0);
}

static void
ar933x_bus_grab(struct uart_softc *sc)
{
	struct uart_bas *bas = &sc->sc_bas;
	uint32_t reg;

	/* Disable the host interrupt now */
	uart_lock(sc->sc_hwmtx);
	reg = ar933x_getreg(bas, AR933X_UART_CS_REG);
	reg &= ~AR933X_UART_CS_HOST_INT_EN;
	ar933x_setreg(bas, AR933X_UART_CS_REG, reg);
	uart_unlock(sc->sc_hwmtx);
}

static void
ar933x_bus_ungrab(struct uart_softc *sc)
{
	struct uart_bas *bas = &sc->sc_bas;
	uint32_t reg;

	/* Enable the host interrupt now */
	uart_lock(sc->sc_hwmtx);
	reg = ar933x_getreg(bas, AR933X_UART_CS_REG);
	reg |= AR933X_UART_CS_HOST_INT_EN;
	ar933x_setreg(bas, AR933X_UART_CS_REG, reg);
	uart_unlock(sc->sc_hwmtx);
}
