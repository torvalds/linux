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

#include <dev/ic/sab82532.h>

#include "uart_if.h"

#define	DEFAULT_RCLK	29491200

/*
 * NOTE: To allow us to read the baudrate divisor from the chip, we
 * copy the value written to the write-only BGR register to an unused
 * read-write register. We use TCR for that.
 */

static int
sab82532_delay(struct uart_bas *bas)
{
	int divisor, m, n;
	uint8_t bgr, ccr2;

	bgr = uart_getreg(bas, SAB_TCR);
	ccr2 = uart_getreg(bas, SAB_CCR2);
	n = (bgr & 0x3f) + 1;
	m = (bgr >> 6) | ((ccr2 >> 4) & 0xC);
	divisor = n * (1<<m);

	/* 1/10th the time to transmit 1 character (estimate). */
	return (16000000 * divisor / bas->rclk);
}

static int
sab82532_divisor(int rclk, int baudrate)
{
	int act_baud, act_div, divisor;
	int error, m, n;

	if (baudrate == 0)
		return (0);

	divisor = (rclk / (baudrate << 3) + 1) >> 1;
	if (divisor < 2 || divisor >= 1048576)
		return (0);

	/* Find the best (N+1,M) pair. */
	for (m = 1; m < 15; m++) {
		n = divisor / (1<<m);
		if (n < 1 || n > 63)
			continue;
		act_div = n * (1<<m);
		act_baud = rclk / (act_div << 4);

		/* 10 times error in percent: */
		error = ((act_baud - baudrate) * 2000 / baudrate + 1) >> 1;

		/* 3.0% maximum error tolerance: */
		if (error < -30 || error > 30)
			continue;

		/* Got it. */
		return ((n - 1) | (m << 6));
	}

	return (0);
}

static void
sab82532_flush(struct uart_bas *bas, int what)
{

	if (what & UART_FLUSH_TRANSMITTER) {
		while (uart_getreg(bas, SAB_STAR) & SAB_STAR_CEC)
			;
		uart_setreg(bas, SAB_CMDR, SAB_CMDR_XRES);
		uart_barrier(bas);
	}
	if (what & UART_FLUSH_RECEIVER) {
		while (uart_getreg(bas, SAB_STAR) & SAB_STAR_CEC)
			;
		uart_setreg(bas, SAB_CMDR, SAB_CMDR_RRES);
		uart_barrier(bas);
	}
}

static int
sab82532_param(struct uart_bas *bas, int baudrate, int databits, int stopbits,
    int parity)
{
	int divisor;
	uint8_t ccr2, dafo;

	if (databits >= 8)
		dafo = SAB_DAFO_CHL_CS8;
	else if (databits == 7)
		dafo = SAB_DAFO_CHL_CS7;
	else if (databits == 6)
		dafo = SAB_DAFO_CHL_CS6;
	else
		dafo = SAB_DAFO_CHL_CS5;
	if (stopbits > 1)
		dafo |= SAB_DAFO_STOP;
	switch (parity) {
	case UART_PARITY_EVEN:	dafo |= SAB_DAFO_PAR_EVEN; break;
	case UART_PARITY_MARK:	dafo |= SAB_DAFO_PAR_MARK; break;
	case UART_PARITY_NONE:	dafo |= SAB_DAFO_PAR_NONE; break;
	case UART_PARITY_ODD:	dafo |= SAB_DAFO_PAR_ODD; break;
	case UART_PARITY_SPACE:	dafo |= SAB_DAFO_PAR_SPACE; break;
	default:		return (EINVAL);
	}

	/* Set baudrate. */
	if (baudrate > 0) {
		divisor = sab82532_divisor(bas->rclk, baudrate);
		if (divisor == 0)
			return (EINVAL);
		uart_setreg(bas, SAB_BGR, divisor & 0xff);
		uart_barrier(bas);
		/* Allow reading the (n-1,m) tuple from the chip. */
		uart_setreg(bas, SAB_TCR, divisor & 0xff);
		uart_barrier(bas);
		ccr2 = uart_getreg(bas, SAB_CCR2);
		ccr2 &= ~(SAB_CCR2_BR9 | SAB_CCR2_BR8);
		ccr2 |= (divisor >> 2) & (SAB_CCR2_BR9 | SAB_CCR2_BR8);
		uart_setreg(bas, SAB_CCR2, ccr2);
		uart_barrier(bas);
	}

	uart_setreg(bas, SAB_DAFO, dafo);
	uart_barrier(bas);
	return (0);
}

/*
 * Low-level UART interface.
 */
static int sab82532_probe(struct uart_bas *bas);
static void sab82532_init(struct uart_bas *bas, int, int, int, int);
static void sab82532_term(struct uart_bas *bas);
static void sab82532_putc(struct uart_bas *bas, int);
static int sab82532_rxready(struct uart_bas *bas);
static int sab82532_getc(struct uart_bas *bas, struct mtx *);

static struct uart_ops uart_sab82532_ops = {
	.probe = sab82532_probe,
	.init = sab82532_init,
	.term = sab82532_term,
	.putc = sab82532_putc,
	.rxready = sab82532_rxready,
	.getc = sab82532_getc,
};

static int
sab82532_probe(struct uart_bas *bas)
{

	return (0);
}

static void
sab82532_init(struct uart_bas *bas, int baudrate, int databits, int stopbits,
    int parity)
{
	uint8_t ccr0, pvr;

	if (bas->rclk == 0)
		bas->rclk = DEFAULT_RCLK;

	/*
	 * Set all pins, except the DTR pins (pin 1 and 2) to be inputs.
	 * Pin 4 is magical, meaning that I don't know what it does, but
	 * it too has to be set to output.
	 */
	uart_setreg(bas, SAB_PCR,
	    ~(SAB_PVR_DTR_A|SAB_PVR_DTR_B|SAB_PVR_MAGIC));
	uart_barrier(bas);
	/* Disable port interrupts. */
	uart_setreg(bas, SAB_PIM, 0xff);
	uart_barrier(bas);
	/* Interrupts are active low. */
	uart_setreg(bas, SAB_IPC, SAB_IPC_ICPL);
	uart_barrier(bas);
	/* Set DTR. */
	pvr = uart_getreg(bas, SAB_PVR);
	switch (bas->chan) {
	case 1:
		pvr &= ~SAB_PVR_DTR_A;
		break;
	case 2:
		pvr &= ~SAB_PVR_DTR_B;
		break;
	}
	uart_setreg(bas, SAB_PVR, pvr | SAB_PVR_MAGIC);
	uart_barrier(bas);

	/* power down */
	uart_setreg(bas, SAB_CCR0, 0);
	uart_barrier(bas);

	/* set basic configuration */
	ccr0 = SAB_CCR0_MCE|SAB_CCR0_SC_NRZ|SAB_CCR0_SM_ASYNC;
	uart_setreg(bas, SAB_CCR0, ccr0);
	uart_barrier(bas);
	uart_setreg(bas, SAB_CCR1, SAB_CCR1_ODS|SAB_CCR1_BCR|SAB_CCR1_CM_7);
	uart_barrier(bas);
	uart_setreg(bas, SAB_CCR2, SAB_CCR2_BDF|SAB_CCR2_SSEL|SAB_CCR2_TOE);
	uart_barrier(bas);
	uart_setreg(bas, SAB_CCR3, 0);
	uart_barrier(bas);
	uart_setreg(bas, SAB_CCR4, SAB_CCR4_MCK4|SAB_CCR4_EBRG|SAB_CCR4_ICD);
	uart_barrier(bas);
	uart_setreg(bas, SAB_MODE, SAB_MODE_FCTS|SAB_MODE_RTS|SAB_MODE_RAC);
	uart_barrier(bas);
	uart_setreg(bas, SAB_RFC, SAB_RFC_DPS|SAB_RFC_RFDF|
	    SAB_RFC_RFTH_32CHAR);
	uart_barrier(bas);

	sab82532_param(bas, baudrate, databits, stopbits, parity);

	/* Clear interrupts. */
	uart_setreg(bas, SAB_IMR0, (unsigned char)~SAB_IMR0_TCD);
	uart_setreg(bas, SAB_IMR1, 0xff);
	uart_barrier(bas);
	uart_getreg(bas, SAB_ISR0);
	uart_getreg(bas, SAB_ISR1);
	uart_barrier(bas);

	sab82532_flush(bas, UART_FLUSH_TRANSMITTER|UART_FLUSH_RECEIVER);

	/* Power up. */
	uart_setreg(bas, SAB_CCR0, ccr0|SAB_CCR0_PU);
	uart_barrier(bas);
}

static void
sab82532_term(struct uart_bas *bas)
{
	uint8_t pvr;

	pvr = uart_getreg(bas, SAB_PVR);
	switch (bas->chan) {
	case 1:
		pvr |= SAB_PVR_DTR_A;
		break;
	case 2:
		pvr |= SAB_PVR_DTR_B;
		break;
	}
	uart_setreg(bas, SAB_PVR, pvr);
	uart_barrier(bas);
}

static void
sab82532_putc(struct uart_bas *bas, int c)
{
	int delay, limit;

	/* 1/10th the time to transmit 1 character (estimate). */
	delay = sab82532_delay(bas);

	limit = 20;
	while ((uart_getreg(bas, SAB_STAR) & SAB_STAR_TEC) && --limit)
		DELAY(delay);
	uart_setreg(bas, SAB_TIC, c);
	limit = 20;
	while ((uart_getreg(bas, SAB_STAR) & SAB_STAR_TEC) && --limit)
		DELAY(delay);
}

static int
sab82532_rxready(struct uart_bas *bas)
{

	return ((uart_getreg(bas, SAB_STAR) & SAB_STAR_RFNE) != 0 ? 1 : 0);
}

static int
sab82532_getc(struct uart_bas *bas, struct mtx *hwmtx)
{
	int c, delay;

	uart_lock(hwmtx);

	/* 1/10th the time to transmit 1 character (estimate). */
	delay = sab82532_delay(bas);

	while (!(uart_getreg(bas, SAB_STAR) & SAB_STAR_RFNE)) {
		uart_unlock(hwmtx);
		DELAY(delay);
		uart_lock(hwmtx);
	}

	while (uart_getreg(bas, SAB_STAR) & SAB_STAR_CEC)
		;
	uart_setreg(bas, SAB_CMDR, SAB_CMDR_RFRD);
	uart_barrier(bas);

	while (!(uart_getreg(bas, SAB_ISR0) & SAB_ISR0_TCD))
		DELAY(delay);

	c = uart_getreg(bas, SAB_RFIFO);
	uart_barrier(bas);

	/* Blow away everything left in the FIFO... */
	while (uart_getreg(bas, SAB_STAR) & SAB_STAR_CEC)
		;
	uart_setreg(bas, SAB_CMDR, SAB_CMDR_RMC);
	uart_barrier(bas);

	uart_unlock(hwmtx);

	return (c);
}

/*
 * High-level UART interface.
 */
struct sab82532_softc {
	struct uart_softc base;
};

static int sab82532_bus_attach(struct uart_softc *);
static int sab82532_bus_detach(struct uart_softc *);
static int sab82532_bus_flush(struct uart_softc *, int);
static int sab82532_bus_getsig(struct uart_softc *);
static int sab82532_bus_ioctl(struct uart_softc *, int, intptr_t);
static int sab82532_bus_ipend(struct uart_softc *);
static int sab82532_bus_param(struct uart_softc *, int, int, int, int);
static int sab82532_bus_probe(struct uart_softc *);
static int sab82532_bus_receive(struct uart_softc *);
static int sab82532_bus_setsig(struct uart_softc *, int);
static int sab82532_bus_transmit(struct uart_softc *);
static void sab82532_bus_grab(struct uart_softc *);
static void sab82532_bus_ungrab(struct uart_softc *);

static kobj_method_t sab82532_methods[] = {
	KOBJMETHOD(uart_attach,		sab82532_bus_attach),
	KOBJMETHOD(uart_detach,		sab82532_bus_detach),
	KOBJMETHOD(uart_flush,		sab82532_bus_flush),
	KOBJMETHOD(uart_getsig,		sab82532_bus_getsig),
	KOBJMETHOD(uart_ioctl,		sab82532_bus_ioctl),
	KOBJMETHOD(uart_ipend,		sab82532_bus_ipend),
	KOBJMETHOD(uart_param,		sab82532_bus_param),
	KOBJMETHOD(uart_probe,		sab82532_bus_probe),
	KOBJMETHOD(uart_receive,	sab82532_bus_receive),
	KOBJMETHOD(uart_setsig,		sab82532_bus_setsig),
	KOBJMETHOD(uart_transmit,	sab82532_bus_transmit),
	KOBJMETHOD(uart_grab,		sab82532_bus_grab),
	KOBJMETHOD(uart_ungrab,		sab82532_bus_ungrab),
	{ 0, 0 }
};

struct uart_class uart_sab82532_class = {
	"sab82532",
	sab82532_methods,
	sizeof(struct sab82532_softc),
	.uc_ops = &uart_sab82532_ops,
	.uc_range = 64,
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
sab82532_bus_attach(struct uart_softc *sc)
{
	struct uart_bas *bas;
	uint8_t imr0, imr1;

	bas = &sc->sc_bas;
	if (sc->sc_sysdev == NULL)
		sab82532_init(bas, 9600, 8, 1, UART_PARITY_NONE);

	imr0 = SAB_IMR0_TCD|SAB_IMR0_TIME|SAB_IMR0_CDSC|SAB_IMR0_RFO|
	    SAB_IMR0_RPF;
	uart_setreg(bas, SAB_IMR0, 0xff & ~imr0);
	imr1 = SAB_IMR1_BRKT|SAB_IMR1_ALLS|SAB_IMR1_CSC;
	uart_setreg(bas, SAB_IMR1, 0xff & ~imr1);
	uart_barrier(bas);

	if (sc->sc_sysdev == NULL)
		sab82532_bus_setsig(sc, SER_DDTR|SER_DRTS);
	(void)sab82532_bus_getsig(sc);
	return (0);
}

static int
sab82532_bus_detach(struct uart_softc *sc)
{
	struct uart_bas *bas;

	bas = &sc->sc_bas;
	uart_setreg(bas, SAB_IMR0, 0xff);
	uart_setreg(bas, SAB_IMR1, 0xff);
	uart_barrier(bas);
	uart_getreg(bas, SAB_ISR0);
	uart_getreg(bas, SAB_ISR1);
	uart_barrier(bas);
	uart_setreg(bas, SAB_CCR0, 0);
	uart_barrier(bas);
	return (0);
}

static int
sab82532_bus_flush(struct uart_softc *sc, int what)
{

	uart_lock(sc->sc_hwmtx);
	sab82532_flush(&sc->sc_bas, what);
	uart_unlock(sc->sc_hwmtx);
	return (0);
}

static int
sab82532_bus_getsig(struct uart_softc *sc)
{
	struct uart_bas *bas;
	uint32_t new, old, sig;
	uint8_t pvr, star, vstr;

	bas = &sc->sc_bas;
	do {
		old = sc->sc_hwsig;
		sig = old;
		uart_lock(sc->sc_hwmtx);
		star = uart_getreg(bas, SAB_STAR);
		SIGCHG(star & SAB_STAR_CTS, sig, SER_CTS, SER_DCTS);
		vstr = uart_getreg(bas, SAB_VSTR);
		SIGCHG(vstr & SAB_VSTR_CD, sig, SER_DCD, SER_DDCD);
		pvr = ~uart_getreg(bas, SAB_PVR);
		switch (bas->chan) {
		case 1:
			pvr &= SAB_PVR_DSR_A;
			break;
		case 2:
			pvr &= SAB_PVR_DSR_B;
			break;
		}
		SIGCHG(pvr, sig, SER_DSR, SER_DDSR);
		uart_unlock(sc->sc_hwmtx);
		new = sig & ~SER_MASK_DELTA;
	} while (!atomic_cmpset_32(&sc->sc_hwsig, old, new));
	return (sig);
}

static int
sab82532_bus_ioctl(struct uart_softc *sc, int request, intptr_t data)
{
	struct uart_bas *bas;
	uint8_t dafo, mode;
	int error;

	bas = &sc->sc_bas;
	error = 0;
	uart_lock(sc->sc_hwmtx);
	switch (request) {
	case UART_IOCTL_BREAK:
		dafo = uart_getreg(bas, SAB_DAFO);
		if (data)
			dafo |= SAB_DAFO_XBRK;
		else
			dafo &= ~SAB_DAFO_XBRK;
		uart_setreg(bas, SAB_DAFO, dafo);
		uart_barrier(bas);
		break;
	case UART_IOCTL_IFLOW:
		mode = uart_getreg(bas, SAB_MODE);
		if (data) {
			mode &= ~SAB_MODE_RTS;
			mode |= SAB_MODE_FRTS;
		} else {
			mode |= SAB_MODE_RTS;
			mode &= ~SAB_MODE_FRTS;
		}
		uart_setreg(bas, SAB_MODE, mode);
		uart_barrier(bas);
		break;
	case UART_IOCTL_OFLOW:
		mode = uart_getreg(bas, SAB_MODE);
		if (data)
			mode &= ~SAB_MODE_FCTS;
		else
			mode |= SAB_MODE_FCTS;
		uart_setreg(bas, SAB_MODE, mode);
		uart_barrier(bas);
		break;
	default:
		error = EINVAL;
		break;
	}
	uart_unlock(sc->sc_hwmtx);
	return (error);
}

static int
sab82532_bus_ipend(struct uart_softc *sc)
{
	struct uart_bas *bas;
	int ipend;
	uint8_t isr0, isr1;

	bas = &sc->sc_bas;
	uart_lock(sc->sc_hwmtx);
	isr0 = uart_getreg(bas, SAB_ISR0);
	isr1 = uart_getreg(bas, SAB_ISR1);
	uart_barrier(bas);
	if (isr0 & SAB_ISR0_TIME) {
		while (uart_getreg(bas, SAB_STAR) & SAB_STAR_CEC)
			;
		uart_setreg(bas, SAB_CMDR, SAB_CMDR_RFRD);
		uart_barrier(bas);
	}
	uart_unlock(sc->sc_hwmtx);

	ipend = 0;
	if (isr1 & SAB_ISR1_BRKT)
		ipend |= SER_INT_BREAK;
	if (isr0 & SAB_ISR0_RFO)
		ipend |= SER_INT_OVERRUN;
	if (isr0 & (SAB_ISR0_TCD|SAB_ISR0_RPF))
		ipend |= SER_INT_RXREADY;
	if ((isr0 & SAB_ISR0_CDSC) || (isr1 & SAB_ISR1_CSC))
		ipend |= SER_INT_SIGCHG;
	if (isr1 & SAB_ISR1_ALLS)
		ipend |= SER_INT_TXIDLE;

	return (ipend);
}

static int
sab82532_bus_param(struct uart_softc *sc, int baudrate, int databits,
    int stopbits, int parity)
{
	struct uart_bas *bas;
	int error;

	bas = &sc->sc_bas;
	uart_lock(sc->sc_hwmtx);
	error = sab82532_param(bas, baudrate, databits, stopbits, parity);
	uart_unlock(sc->sc_hwmtx);
	return (error);
}

static int
sab82532_bus_probe(struct uart_softc *sc)
{
	char buf[80];
	const char *vstr;
	int error;
	char ch;

	error = sab82532_probe(&sc->sc_bas);
	if (error)
		return (error);

	sc->sc_rxfifosz = 32;
	sc->sc_txfifosz = 32;

	ch = sc->sc_bas.chan - 1 + 'A';

	switch (uart_getreg(&sc->sc_bas, SAB_VSTR) & SAB_VSTR_VMASK) {
	case SAB_VSTR_V_1:
		vstr = "v1";
		break;
	case SAB_VSTR_V_2:
		vstr = "v2";
		break;
	case SAB_VSTR_V_32:
		vstr = "v3.2";
		sc->sc_hwiflow = 0;	/* CTS doesn't work with RFC:RFDF. */
		sc->sc_hwoflow = 1;
		break;
	default:
		vstr = "v4?";
		break;
	}

	snprintf(buf, sizeof(buf), "SAB 82532 %s, channel %c", vstr, ch);
	device_set_desc_copy(sc->sc_dev, buf);
	return (0);
}

static int
sab82532_bus_receive(struct uart_softc *sc)
{
	struct uart_bas *bas;
	int i, rbcl, xc;
	uint8_t s;

	bas = &sc->sc_bas;
	uart_lock(sc->sc_hwmtx);
	if (uart_getreg(bas, SAB_STAR) & SAB_STAR_RFNE) {
		rbcl = uart_getreg(bas, SAB_RBCL) & 31;
		if (rbcl == 0)
			rbcl = 32;
		for (i = 0; i < rbcl; i += 2) {
			if (uart_rx_full(sc)) {
				sc->sc_rxbuf[sc->sc_rxput] = UART_STAT_OVERRUN;
				break;
			}
			xc = uart_getreg(bas, SAB_RFIFO);
			s = uart_getreg(bas, SAB_RFIFO + 1);
			if (s & SAB_RSTAT_FE)
				xc |= UART_STAT_FRAMERR;
			if (s & SAB_RSTAT_PE)
				xc |= UART_STAT_PARERR;
			uart_rx_put(sc, xc);
		}
	}

	while (uart_getreg(bas, SAB_STAR) & SAB_STAR_CEC)
		;
	uart_setreg(bas, SAB_CMDR, SAB_CMDR_RMC);
	uart_barrier(bas);
	uart_unlock(sc->sc_hwmtx);
	return (0);
}

static int
sab82532_bus_setsig(struct uart_softc *sc, int sig)
{
	struct uart_bas *bas;
	uint32_t new, old;
	uint8_t mode, pvr;

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
	/* Set DTR pin. */
	pvr = uart_getreg(bas, SAB_PVR);
	switch (bas->chan) {
	case 1:
		if (new & SER_DTR)
			pvr &= ~SAB_PVR_DTR_A;
		else
			pvr |= SAB_PVR_DTR_A;
		break;
	case 2:
		if (new & SER_DTR)
			pvr &= ~SAB_PVR_DTR_B;
		else
			pvr |= SAB_PVR_DTR_B;
		break;
	}
	uart_setreg(bas, SAB_PVR, pvr);

	/* Set RTS pin. */
	mode = uart_getreg(bas, SAB_MODE);
	if (new & SER_RTS)
		mode &= ~SAB_MODE_FRTS;
	else
		mode |= SAB_MODE_FRTS;
	uart_setreg(bas, SAB_MODE, mode);
	uart_barrier(bas);
	uart_unlock(sc->sc_hwmtx);
	return (0);
}

static int
sab82532_bus_transmit(struct uart_softc *sc)
{
	struct uart_bas *bas;
	int i;

	bas = &sc->sc_bas;
	uart_lock(sc->sc_hwmtx);
	while (!(uart_getreg(bas, SAB_STAR) & SAB_STAR_XFW))
		;
	for (i = 0; i < sc->sc_txdatasz; i++)
		uart_setreg(bas, SAB_XFIFO + i, sc->sc_txbuf[i]);
	uart_barrier(bas);
	while (uart_getreg(bas, SAB_STAR) & SAB_STAR_CEC)
		;
	uart_setreg(bas, SAB_CMDR, SAB_CMDR_XF);
	sc->sc_txbusy = 1;
	uart_unlock(sc->sc_hwmtx);
	return (0);
}

static void
sab82532_bus_grab(struct uart_softc *sc)
{
	struct uart_bas *bas;
	uint8_t imr0;

	bas = &sc->sc_bas;
	imr0 = SAB_IMR0_TIME|SAB_IMR0_CDSC|SAB_IMR0_RFO; /* No TCD or RPF */
	uart_lock(sc->sc_hwmtx);
	uart_setreg(bas, SAB_IMR0, 0xff & ~imr0);
	uart_barrier(bas);
	uart_unlock(sc->sc_hwmtx);
}

static void
sab82532_bus_ungrab(struct uart_softc *sc)
{
	struct uart_bas *bas;
	uint8_t imr0;

	bas = &sc->sc_bas;
	imr0 = SAB_IMR0_TCD|SAB_IMR0_TIME|SAB_IMR0_CDSC|SAB_IMR0_RFO|
	    SAB_IMR0_RPF;
	uart_lock(sc->sc_hwmtx);
	uart_setreg(bas, SAB_IMR0, 0xff & ~imr0);
	uart_barrier(bas);
	uart_unlock(sc->sc_hwmtx);
}
