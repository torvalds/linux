/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD AND BSD-2-Clause
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

/*
 * uart_dev_oct16550.c
 *
 * Derived from uart_dev_ns8250.c
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
 *
 *
 */


#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/conf.h>
#include <machine/bus.h>
#include <machine/pcpu.h>

#include <dev/uart/uart.h>
#include <dev/uart/uart_cpu.h>
#include <dev/uart/uart_bus.h>

#include <dev/ic/ns16550.h>

#include <mips/cavium/octeon_pcmap_regs.h>

#include <contrib/octeon-sdk/cvmx.h>

#include "uart_if.h"

/*
 * Clear pending interrupts. THRE is cleared by reading IIR. Data
 * that may have been received gets lost here.
 */
static void
oct16550_clrint (struct uart_bas *bas)
{
	uint8_t iir;

	iir = uart_getreg(bas, REG_IIR);
	while ((iir & IIR_NOPEND) == 0) {
		iir &= IIR_IMASK;
		if (iir == IIR_RLS)
			(void)uart_getreg(bas, REG_LSR);
		else if (iir == IIR_RXRDY || iir == IIR_RXTOUT)
			(void)uart_getreg(bas, REG_DATA);
		else if (iir == IIR_MLSC)
			(void)uart_getreg(bas, REG_MSR);
                else if (iir == IIR_BUSY)
                    	(void) uart_getreg(bas, REG_USR);
		uart_barrier(bas);
		iir = uart_getreg(bas, REG_IIR);
	}
}

static int delay_changed = 1;

static int
oct16550_delay (struct uart_bas *bas)
{
	int divisor;
	u_char lcr;
        static int delay = 0;

        if (!delay_changed) return delay;
        delay_changed = 0;
	lcr = uart_getreg(bas, REG_LCR);
	uart_setreg(bas, REG_LCR, lcr | LCR_DLAB);
	uart_barrier(bas);
	divisor = uart_getreg(bas, REG_DLL) | (uart_getreg(bas, REG_DLH) << 8);
	uart_barrier(bas);
	uart_setreg(bas, REG_LCR, lcr);
	uart_barrier(bas);
	
	if(!bas->rclk)
		return 10; /* return an approx delay value */

	/* 1/10th the time to transmit 1 character (estimate). */
	if (divisor <= 134)
		return (16000000 * divisor / bas->rclk);
	return (16000 * divisor / (bas->rclk / 1000));

}

static int
oct16550_divisor (int rclk, int baudrate)
{
	int actual_baud, divisor;
	int error;

	if (baudrate == 0)
		return (0);

	divisor = (rclk / (baudrate << 3) + 1) >> 1;
	if (divisor == 0 || divisor >= 65536)
		return (0);
	actual_baud = rclk / (divisor << 4);

	/* 10 times error in percent: */
	error = ((actual_baud - baudrate) * 2000 / baudrate + 1) >> 1;

	/* 3.0% maximum error tolerance: */
	if (error < -30 || error > 30)
		return (0);

	return (divisor);
}

static int
oct16550_drain (struct uart_bas *bas, int what)
{
	int delay, limit;

	delay = oct16550_delay(bas);

	if (what & UART_DRAIN_TRANSMITTER) {
		/*
		 * Pick an arbitrary high limit to avoid getting stuck in
		 * an infinite loop when the hardware is broken. Make the
		 * limit high enough to handle large FIFOs.
		 */
		limit = 10*10*10*1024;
		while ((uart_getreg(bas, REG_LSR) & LSR_TEMT) == 0 && --limit)
			DELAY(delay);
		if (limit == 0) {
			/* printf("oct16550: transmitter appears stuck... "); */
			return (0);
		}
	}

	if (what & UART_DRAIN_RECEIVER) {
		/*
		 * Pick an arbitrary high limit to avoid getting stuck in
		 * an infinite loop when the hardware is broken. Make the
		 * limit high enough to handle large FIFOs and integrated
		 * UARTs. The HP rx2600 for example has 3 UARTs on the
		 * management board that tend to get a lot of data send
		 * to it when the UART is first activated.
		 */
		limit=10*4096;
		while ((uart_getreg(bas, REG_LSR) & LSR_RXRDY) && --limit) {
			(void)uart_getreg(bas, REG_DATA);
			uart_barrier(bas);
			DELAY(delay << 2);
		}
		if (limit == 0) {
			/* printf("oct16550: receiver appears broken... "); */
			return (EIO);
		}
	}

	return (0);
}

/*
 * We can only flush UARTs with FIFOs. UARTs without FIFOs should be
 * drained. WARNING: this function clobbers the FIFO setting!
 */
static void
oct16550_flush (struct uart_bas *bas, int what)
{
	uint8_t fcr;

	fcr = FCR_ENABLE;
	if (what & UART_FLUSH_TRANSMITTER)
		fcr |= FCR_XMT_RST;
	if (what & UART_FLUSH_RECEIVER)
		fcr |= FCR_RCV_RST;
	uart_setreg(bas, REG_FCR, fcr);
	uart_barrier(bas);
}

static int
oct16550_param (struct uart_bas *bas, int baudrate, int databits, int stopbits,
    int parity)
{
	int divisor;
	uint8_t lcr;

	lcr = 0;
	if (databits >= 8)
		lcr |= LCR_8BITS;
	else if (databits == 7)
		lcr |= LCR_7BITS;
	else if (databits == 6)
		lcr |= LCR_6BITS;
	else
		lcr |= LCR_5BITS;
	if (stopbits > 1)
		lcr |= LCR_STOPB;
	lcr |= parity << 3;

	/* Set baudrate. */
	if (baudrate > 0) {
		divisor = oct16550_divisor(bas->rclk, baudrate);
		if (divisor == 0)
			return (EINVAL);
		uart_setreg(bas, REG_LCR, lcr | LCR_DLAB);
		uart_barrier(bas);
		uart_setreg(bas, REG_DLL, divisor & 0xff);
		uart_setreg(bas, REG_DLH, (divisor >> 8) & 0xff);
		uart_barrier(bas);
                delay_changed = 1;
	}

	/* Set LCR and clear DLAB. */
	uart_setreg(bas, REG_LCR, lcr);
	uart_barrier(bas);
	return (0);
}

/*
 * Low-level UART interface.
 */
static int oct16550_probe(struct uart_bas *bas);
static void oct16550_init(struct uart_bas *bas, int, int, int, int);
static void oct16550_term(struct uart_bas *bas);
static void oct16550_putc(struct uart_bas *bas, int);
static int oct16550_rxready(struct uart_bas *bas);
static int oct16550_getc(struct uart_bas *bas, struct mtx *);

struct uart_ops uart_oct16550_ops = {
	.probe = oct16550_probe,
	.init = oct16550_init,
	.term = oct16550_term,
	.putc = oct16550_putc,
	.rxready = oct16550_rxready,
	.getc = oct16550_getc,
};

static int
oct16550_probe (struct uart_bas *bas)
{
	u_char val;

	/* Check known 0 bits that don't depend on DLAB. */
	val = uart_getreg(bas, REG_IIR);
	if (val & 0x30)
		return (ENXIO);
	val = uart_getreg(bas, REG_MCR);
	if (val & 0xc0)
		return (ENXIO);
	val = uart_getreg(bas, REG_USR);
        if (val & 0xe0)
            	return (ENXIO);
	return (0);
}

static void
oct16550_init (struct uart_bas *bas, int baudrate, int databits, int stopbits,
    int parity)
{
	u_char	ier;

	oct16550_param(bas, baudrate, databits, stopbits, parity);

	/* Disable all interrupt sources. */
	ier = uart_getreg(bas, REG_IER) & 0x0;
	uart_setreg(bas, REG_IER, ier);
	uart_barrier(bas);

	/* Disable the FIFO (if present). */
//	uart_setreg(bas, REG_FCR, 0);
	uart_barrier(bas);

	/* Set RTS & DTR. */
	uart_setreg(bas, REG_MCR, MCR_RTS | MCR_DTR);
	uart_barrier(bas);

	oct16550_clrint(bas);
}

static void
oct16550_term (struct uart_bas *bas)
{

	/* Clear RTS & DTR. */
	uart_setreg(bas, REG_MCR, 0);
	uart_barrier(bas);
}

static inline void oct16550_wait_txhr_empty (struct uart_bas *bas, int limit, int delay)
{
    while (((uart_getreg(bas, REG_LSR) & LSR_THRE) == 0) &&
           ((uart_getreg(bas, REG_USR) & USR_TXFIFO_NOTFULL) == 0))
        DELAY(delay);
}

static void
oct16550_putc (struct uart_bas *bas, int c)
{
	int delay;

	/* 1/10th the time to transmit 1 character (estimate). */
	delay = oct16550_delay(bas);
        oct16550_wait_txhr_empty(bas, 100, delay);
	uart_setreg(bas, REG_DATA, c);
	uart_barrier(bas);
        oct16550_wait_txhr_empty(bas, 100, delay);
}

static int
oct16550_rxready (struct uart_bas *bas)
{

	return ((uart_getreg(bas, REG_LSR) & LSR_RXRDY) != 0 ? 1 : 0);
}

static int
oct16550_getc (struct uart_bas *bas, struct mtx *hwmtx)
{
	int c, delay;

	uart_lock(hwmtx);

	/* 1/10th the time to transmit 1 character (estimate). */
	delay = oct16550_delay(bas);

	while ((uart_getreg(bas, REG_LSR) & LSR_RXRDY) == 0) {
		uart_unlock(hwmtx);
		DELAY(delay);
		uart_lock(hwmtx);
	}

	c = uart_getreg(bas, REG_DATA);

	uart_unlock(hwmtx);

	return (c);
}

/*
 * High-level UART interface.
 */
struct oct16550_softc {
	struct uart_softc base;
	uint8_t		fcr;
	uint8_t		ier;
	uint8_t		mcr;
};

static int oct16550_bus_attach(struct uart_softc *);
static int oct16550_bus_detach(struct uart_softc *);
static int oct16550_bus_flush(struct uart_softc *, int);
static int oct16550_bus_getsig(struct uart_softc *);
static int oct16550_bus_ioctl(struct uart_softc *, int, intptr_t);
static int oct16550_bus_ipend(struct uart_softc *);
static int oct16550_bus_param(struct uart_softc *, int, int, int, int);
static int oct16550_bus_probe(struct uart_softc *);
static int oct16550_bus_receive(struct uart_softc *);
static int oct16550_bus_setsig(struct uart_softc *, int);
static int oct16550_bus_transmit(struct uart_softc *);
static void oct16550_bus_grab(struct uart_softc *);
static void oct16550_bus_ungrab(struct uart_softc *);

static kobj_method_t oct16550_methods[] = {
	KOBJMETHOD(uart_attach,		oct16550_bus_attach),
	KOBJMETHOD(uart_detach,		oct16550_bus_detach),
	KOBJMETHOD(uart_flush,		oct16550_bus_flush),
	KOBJMETHOD(uart_getsig,		oct16550_bus_getsig),
	KOBJMETHOD(uart_ioctl,		oct16550_bus_ioctl),
	KOBJMETHOD(uart_ipend,		oct16550_bus_ipend),
	KOBJMETHOD(uart_param,		oct16550_bus_param),
	KOBJMETHOD(uart_probe,		oct16550_bus_probe),
	KOBJMETHOD(uart_receive,	oct16550_bus_receive),
	KOBJMETHOD(uart_setsig,		oct16550_bus_setsig),
	KOBJMETHOD(uart_transmit,	oct16550_bus_transmit),
	KOBJMETHOD(uart_grab,		oct16550_bus_grab),
	KOBJMETHOD(uart_ungrab,		oct16550_bus_ungrab),
	{ 0, 0 }
};

struct uart_class uart_oct16550_class = {
	"oct16550 class",
	oct16550_methods,
	sizeof(struct oct16550_softc),
	.uc_ops = &uart_oct16550_ops,
	.uc_range = 8 << 3,
	.uc_rclk = 0,
	.uc_rshift = 0
};

#define	SIGCHG(c, i, s, d)				\
	if (c) {					\
		i |= (i & s) ? s : s | d;		\
	} else {					\
		i = (i & s) ? (i & ~s) | d : i;		\
	}

static int
oct16550_bus_attach (struct uart_softc *sc)
{
	struct oct16550_softc *oct16550 = (struct oct16550_softc*)sc;
	struct uart_bas *bas;
        int unit;

        unit = device_get_unit(sc->sc_dev);
	bas = &sc->sc_bas;

        oct16550_drain(bas, UART_DRAIN_TRANSMITTER);
	oct16550->mcr = uart_getreg(bas, REG_MCR);
	oct16550->fcr = FCR_ENABLE | FCR_RX_HIGH;
	uart_setreg(bas, REG_FCR, oct16550->fcr);
	uart_barrier(bas);
	oct16550_bus_flush(sc, UART_FLUSH_RECEIVER|UART_FLUSH_TRANSMITTER);

	if (oct16550->mcr & MCR_DTR)
		sc->sc_hwsig |= SER_DTR;
	if (oct16550->mcr & MCR_RTS)
		sc->sc_hwsig |= SER_RTS;
	oct16550_bus_getsig(sc);

	oct16550_clrint(bas);
	oct16550->ier = uart_getreg(bas, REG_IER) & 0xf0;
	oct16550->ier |= IER_EMSC | IER_ERLS | IER_ERXRDY;
	uart_setreg(bas, REG_IER, oct16550->ier);
	uart_barrier(bas);

	return (0);
}

static int
oct16550_bus_detach (struct uart_softc *sc)
{
	struct uart_bas *bas;
	u_char ier;

	bas = &sc->sc_bas;
	ier = uart_getreg(bas, REG_IER) & 0xf0;
	uart_setreg(bas, REG_IER, ier);
	uart_barrier(bas);
	oct16550_clrint(bas);
	return (0);
}

static int
oct16550_bus_flush (struct uart_softc *sc, int what)
{
	struct oct16550_softc *oct16550 = (struct oct16550_softc*)sc;
	struct uart_bas *bas;
	int error;

	bas = &sc->sc_bas;
	uart_lock(sc->sc_hwmtx);
	if (sc->sc_rxfifosz > 1) {
		oct16550_flush(bas, what);
		uart_setreg(bas, REG_FCR, oct16550->fcr);
		uart_barrier(bas);
		error = 0;
	} else
		error = oct16550_drain(bas, what);
	uart_unlock(sc->sc_hwmtx);
	return (error);
}

static int
oct16550_bus_getsig (struct uart_softc *sc)
{
	uint32_t new, old, sig;
	uint8_t msr;

	do {
		old = sc->sc_hwsig;
		sig = old;
		uart_lock(sc->sc_hwmtx);
		msr = uart_getreg(&sc->sc_bas, REG_MSR);
		uart_unlock(sc->sc_hwmtx);
		SIGCHG(msr & MSR_DSR, sig, SER_DSR, SER_DDSR);
		SIGCHG(msr & MSR_CTS, sig, SER_CTS, SER_DCTS);
		SIGCHG(msr & MSR_DCD, sig, SER_DCD, SER_DDCD);
		SIGCHG(msr & MSR_RI,  sig, SER_RI,  SER_DRI);
		new = sig & ~SER_MASK_DELTA;
	} while (!atomic_cmpset_32(&sc->sc_hwsig, old, new));
	return (sig);
}

static int
oct16550_bus_ioctl (struct uart_softc *sc, int request, intptr_t data)
{
	struct uart_bas *bas;
	int baudrate, divisor, error;
	uint8_t efr, lcr;

	bas = &sc->sc_bas;
	error = 0;
	uart_lock(sc->sc_hwmtx);
	switch (request) {
	case UART_IOCTL_BREAK:
		lcr = uart_getreg(bas, REG_LCR);
		if (data)
			lcr |= LCR_SBREAK;
		else
			lcr &= ~LCR_SBREAK;
		uart_setreg(bas, REG_LCR, lcr);
		uart_barrier(bas);
		break;
	case UART_IOCTL_IFLOW:
		lcr = uart_getreg(bas, REG_LCR);
		uart_barrier(bas);
		uart_setreg(bas, REG_LCR, 0xbf);
		uart_barrier(bas);
		efr = uart_getreg(bas, REG_EFR);
		if (data)
			efr |= EFR_RTS;
		else
			efr &= ~EFR_RTS;
		uart_setreg(bas, REG_EFR, efr);
		uart_barrier(bas);
		uart_setreg(bas, REG_LCR, lcr);
		uart_barrier(bas);
		break;
	case UART_IOCTL_OFLOW:
		lcr = uart_getreg(bas, REG_LCR);
		uart_barrier(bas);
		uart_setreg(bas, REG_LCR, 0xbf);
		uart_barrier(bas);
		efr = uart_getreg(bas, REG_EFR);
		if (data)
			efr |= EFR_CTS;
		else
			efr &= ~EFR_CTS;
		uart_setreg(bas, REG_EFR, efr);
		uart_barrier(bas);
		uart_setreg(bas, REG_LCR, lcr);
		uart_barrier(bas);
		break;
	case UART_IOCTL_BAUD:
		lcr = uart_getreg(bas, REG_LCR);
		uart_setreg(bas, REG_LCR, lcr | LCR_DLAB);
		uart_barrier(bas);
		divisor = uart_getreg(bas, REG_DLL) |
		    (uart_getreg(bas, REG_DLH) << 8);
		uart_barrier(bas);
		uart_setreg(bas, REG_LCR, lcr);
		uart_barrier(bas);
		baudrate = (divisor > 0) ? bas->rclk / divisor / 16 : 0;
                delay_changed = 1;
		if (baudrate > 0)
			*(int*)data = baudrate;
		else
			error = ENXIO;
		break;
	default:
		error = EINVAL;
		break;
	}
	uart_unlock(sc->sc_hwmtx);
	return (error);
}


static int
oct16550_bus_ipend(struct uart_softc *sc)
{
	struct uart_bas *bas;
	int ipend = 0;
	uint8_t iir, lsr;

	bas = &sc->sc_bas;
	uart_lock(sc->sc_hwmtx);

	iir = uart_getreg(bas, REG_IIR) & IIR_IMASK;
	if (iir != IIR_NOPEND) {

            	if (iir == IIR_RLS) {
                    	lsr = uart_getreg(bas, REG_LSR);
                        if (lsr & LSR_OE)
                            	ipend |= SER_INT_OVERRUN;
                        if (lsr & LSR_BI)
                            	ipend |= SER_INT_BREAK;
                        if (lsr & LSR_RXRDY)
                    		ipend |= SER_INT_RXREADY;

                } else if (iir == IIR_RXRDY) {
                    	ipend |= SER_INT_RXREADY;

                } else if (iir == IIR_RXTOUT) {
                    	ipend |= SER_INT_RXREADY;

                } else if (iir == IIR_TXRDY) {
                    	ipend |= SER_INT_TXIDLE;

                } else if (iir == IIR_MLSC) {
                    	ipend |= SER_INT_SIGCHG;

                } else if (iir == IIR_BUSY) {
                    	(void) uart_getreg(bas, REG_USR);
                }
	}
	uart_unlock(sc->sc_hwmtx);

	return (ipend);
}

static int
oct16550_bus_param (struct uart_softc *sc, int baudrate, int databits,
    int stopbits, int parity)
{
	struct uart_bas *bas;
	int error;

	bas = &sc->sc_bas;
	uart_lock(sc->sc_hwmtx);
	error = oct16550_param(bas, baudrate, databits, stopbits, parity);
	uart_unlock(sc->sc_hwmtx);
	return (error);
}

static int
oct16550_bus_probe (struct uart_softc *sc)
{
	struct uart_bas *bas;
	int error;

	bas = &sc->sc_bas;
	bas->rclk = uart_oct16550_class.uc_rclk = cvmx_clock_get_rate(CVMX_CLOCK_SCLK);

	error = oct16550_probe(bas);
	if (error) {
		return (error);
        }

	uart_setreg(bas, REG_MCR, (MCR_DTR | MCR_RTS));

	/*
	 * Enable FIFOs. And check that the UART has them. If not, we're
	 * done. Since this is the first time we enable the FIFOs, we reset
	 * them.
	 */
        oct16550_drain(bas, UART_DRAIN_TRANSMITTER);
#define ENABLE_OCTEON_FIFO 1
#ifdef ENABLE_OCTEON_FIFO
	uart_setreg(bas, REG_FCR, FCR_ENABLE | FCR_XMT_RST | FCR_RCV_RST);
#endif
	uart_barrier(bas);

	oct16550_flush(bas, UART_FLUSH_RECEIVER|UART_FLUSH_TRANSMITTER);

        if (device_get_unit(sc->sc_dev)) {
            	device_set_desc(sc->sc_dev, "Octeon-16550 channel 1");
        } else {
            	device_set_desc(sc->sc_dev, "Octeon-16550 channel 0");
        }
#ifdef ENABLE_OCTEON_FIFO
	sc->sc_rxfifosz = 64;
	sc->sc_txfifosz = 64;
#else
	sc->sc_rxfifosz = 1;
	sc->sc_txfifosz = 1;
#endif


#if 0
	/*
	 * XXX there are some issues related to hardware flow control and
	 * it's likely that uart(4) is the cause. This basicly needs more
	 * investigation, but we avoid using for hardware flow control
	 * until then.
	 */
	/* 16650s or higher have automatic flow control. */
	if (sc->sc_rxfifosz > 16) {
		sc->sc_hwiflow = 1;
		sc->sc_hwoflow = 1;
	}
#endif

	return (0);
}

static int
oct16550_bus_receive (struct uart_softc *sc)
{
	struct uart_bas *bas;
	int xc;
	uint8_t lsr;

	bas = &sc->sc_bas;
	uart_lock(sc->sc_hwmtx);
	lsr = uart_getreg(bas, REG_LSR);

	while (lsr & LSR_RXRDY) {
		if (uart_rx_full(sc)) {
			sc->sc_rxbuf[sc->sc_rxput] = UART_STAT_OVERRUN;
			break;
		}
		xc = uart_getreg(bas, REG_DATA);
		if (lsr & LSR_FE)
			xc |= UART_STAT_FRAMERR;
		if (lsr & LSR_PE)
			xc |= UART_STAT_PARERR;
		uart_rx_put(sc, xc);
		lsr = uart_getreg(bas, REG_LSR);
	}
	/* Discard everything left in the Rx FIFO. */
        /*
         * First do a dummy read/discard anyway, in case the UART was lying to us.
         * This problem was seen on board, when IIR said RBR, but LSR said no RXRDY
         * Results in a stuck ipend loop.
         */
        (void)uart_getreg(bas, REG_DATA);
	while (lsr & LSR_RXRDY) {
		(void)uart_getreg(bas, REG_DATA);
		uart_barrier(bas);
		lsr = uart_getreg(bas, REG_LSR);
	}
	uart_unlock(sc->sc_hwmtx);
 	return (0);
}

static int
oct16550_bus_setsig (struct uart_softc *sc, int sig)
{
	struct oct16550_softc *oct16550 = (struct oct16550_softc*)sc;
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
	oct16550->mcr &= ~(MCR_DTR|MCR_RTS);
	if (new & SER_DTR)
		oct16550->mcr |= MCR_DTR;
	if (new & SER_RTS)
		oct16550->mcr |= MCR_RTS;
	uart_setreg(bas, REG_MCR, oct16550->mcr);
	uart_barrier(bas);
	uart_unlock(sc->sc_hwmtx);
	return (0);
}

static int
oct16550_bus_transmit (struct uart_softc *sc)
{
	struct oct16550_softc *oct16550 = (struct oct16550_softc*)sc;
	struct uart_bas *bas;
	int i;

	bas = &sc->sc_bas;
	uart_lock(sc->sc_hwmtx);
#ifdef NO_UART_INTERRUPTS
        for (i = 0; i < sc->sc_txdatasz; i++) {
            oct16550_putc(bas, sc->sc_txbuf[i]);
        }
#else

        oct16550_wait_txhr_empty(bas, 100, oct16550_delay(bas));
	uart_setreg(bas, REG_IER, oct16550->ier | IER_ETXRDY);
	uart_barrier(bas);

	for (i = 0; i < sc->sc_txdatasz; i++) {
		uart_setreg(bas, REG_DATA, sc->sc_txbuf[i]);
		uart_barrier(bas);
	}
	sc->sc_txbusy = 1;
#endif
	uart_unlock(sc->sc_hwmtx);
	return (0);
}

static void
oct16550_bus_grab(struct uart_softc *sc)
{
	struct uart_bas *bas = &sc->sc_bas;

	/*
	 * turn off all interrupts to enter polling mode. Leave the
	 * saved mask alone. We'll restore whatever it was in ungrab.
	 * All pending interupt signals are reset when IER is set to 0.
	 */
	uart_lock(sc->sc_hwmtx);
	uart_setreg(bas, REG_IER, 0);
	uart_barrier(bas);
	uart_unlock(sc->sc_hwmtx);
}

static void
oct16550_bus_ungrab(struct uart_softc *sc)
{
	struct oct16550_softc *oct16550 = (struct oct16550_softc*)sc;
	struct uart_bas *bas = &sc->sc_bas;

	/*
	 * Restore previous interrupt mask
	 */
	uart_lock(sc->sc_hwmtx);
	uart_setreg(bas, REG_IER, oct16550->ier);
	uart_barrier(bas);
	uart_unlock(sc->sc_hwmtx);
}
