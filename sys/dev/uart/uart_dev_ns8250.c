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

#include "opt_acpi.h"
#include "opt_platform.h"
#include "opt_uart.h"

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/conf.h>
#include <sys/kernel.h>
#include <sys/sysctl.h>
#include <machine/bus.h>

#ifdef FDT
#include <dev/fdt/fdt_common.h>
#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>
#endif

#include <dev/uart/uart.h>
#include <dev/uart/uart_cpu.h>
#ifdef FDT
#include <dev/uart/uart_cpu_fdt.h>
#endif
#include <dev/uart/uart_bus.h>
#include <dev/uart/uart_dev_ns8250.h>
#include <dev/uart/uart_ppstypes.h>
#ifdef DEV_ACPI
#include <dev/uart/uart_cpu_acpi.h>
#endif

#include <dev/ic/ns16550.h>

#include "uart_if.h"

#define	DEFAULT_RCLK	1843200

/*
 * Set the default baudrate tolerance to 3.0%.
 *
 * Some embedded boards have odd reference clocks (eg 25MHz)
 * and we need to handle higher variances in the target baud rate.
 */
#ifndef	UART_DEV_TOLERANCE_PCT
#define	UART_DEV_TOLERANCE_PCT	30
#endif	/* UART_DEV_TOLERANCE_PCT */

static int broken_txfifo = 0;
SYSCTL_INT(_hw, OID_AUTO, broken_txfifo, CTLFLAG_RWTUN,
	&broken_txfifo, 0, "UART FIFO has QEMU emulation bug");

/*
 * Clear pending interrupts. THRE is cleared by reading IIR. Data
 * that may have been received gets lost here.
 */
static void
ns8250_clrint(struct uart_bas *bas)
{
	uint8_t iir, lsr;

	iir = uart_getreg(bas, REG_IIR);
	while ((iir & IIR_NOPEND) == 0) {
		iir &= IIR_IMASK;
		if (iir == IIR_RLS) {
			lsr = uart_getreg(bas, REG_LSR);
			if (lsr & (LSR_BI|LSR_FE|LSR_PE))
				(void)uart_getreg(bas, REG_DATA);
		} else if (iir == IIR_RXRDY || iir == IIR_RXTOUT)
			(void)uart_getreg(bas, REG_DATA);
		else if (iir == IIR_MLSC)
			(void)uart_getreg(bas, REG_MSR);
		uart_barrier(bas);
		iir = uart_getreg(bas, REG_IIR);
	}
}

static int
ns8250_delay(struct uart_bas *bas)
{
	int divisor;
	u_char lcr;

	lcr = uart_getreg(bas, REG_LCR);
	uart_setreg(bas, REG_LCR, lcr | LCR_DLAB);
	uart_barrier(bas);
	divisor = uart_getreg(bas, REG_DLL) | (uart_getreg(bas, REG_DLH) << 8);
	uart_barrier(bas);
	uart_setreg(bas, REG_LCR, lcr);
	uart_barrier(bas);

	/* 1/10th the time to transmit 1 character (estimate). */
	if (divisor <= 134)
		return (16000000 * divisor / bas->rclk);
	return (16000 * divisor / (bas->rclk / 1000));
}

static int
ns8250_divisor(int rclk, int baudrate)
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

	/* enforce maximum error tolerance: */
	if (error < -UART_DEV_TOLERANCE_PCT || error > UART_DEV_TOLERANCE_PCT)
		return (0);

	return (divisor);
}

static int
ns8250_drain(struct uart_bas *bas, int what)
{
	int delay, limit;

	delay = ns8250_delay(bas);

	if (what & UART_DRAIN_TRANSMITTER) {
		/*
		 * Pick an arbitrary high limit to avoid getting stuck in
		 * an infinite loop when the hardware is broken. Make the
		 * limit high enough to handle large FIFOs.
		 */
		limit = 10*1024;
		while ((uart_getreg(bas, REG_LSR) & LSR_TEMT) == 0 && --limit)
			DELAY(delay);
		if (limit == 0) {
			/* printf("ns8250: transmitter appears stuck... "); */
			return (EIO);
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
			/* printf("ns8250: receiver appears broken... "); */
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
ns8250_flush(struct uart_bas *bas, int what)
{
	uint8_t fcr;

	fcr = FCR_ENABLE;
#ifdef CPU_XBURST
	fcr |= FCR_UART_ON;
#endif
	if (what & UART_FLUSH_TRANSMITTER)
		fcr |= FCR_XMT_RST;
	if (what & UART_FLUSH_RECEIVER)
		fcr |= FCR_RCV_RST;
	uart_setreg(bas, REG_FCR, fcr);
	uart_barrier(bas);
}

static int
ns8250_param(struct uart_bas *bas, int baudrate, int databits, int stopbits,
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
		divisor = ns8250_divisor(bas->rclk, baudrate);
		if (divisor == 0)
			return (EINVAL);
		uart_setreg(bas, REG_LCR, lcr | LCR_DLAB);
		uart_barrier(bas);
		uart_setreg(bas, REG_DLL, divisor & 0xff);
		uart_setreg(bas, REG_DLH, (divisor >> 8) & 0xff);
		uart_barrier(bas);
	}

	/* Set LCR and clear DLAB. */
	uart_setreg(bas, REG_LCR, lcr);
	uart_barrier(bas);
	return (0);
}

/*
 * Low-level UART interface.
 */
static int ns8250_probe(struct uart_bas *bas);
static void ns8250_init(struct uart_bas *bas, int, int, int, int);
static void ns8250_term(struct uart_bas *bas);
static void ns8250_putc(struct uart_bas *bas, int);
static int ns8250_rxready(struct uart_bas *bas);
static int ns8250_getc(struct uart_bas *bas, struct mtx *);

struct uart_ops uart_ns8250_ops = {
	.probe = ns8250_probe,
	.init = ns8250_init,
	.term = ns8250_term,
	.putc = ns8250_putc,
	.rxready = ns8250_rxready,
	.getc = ns8250_getc,
};

static int
ns8250_probe(struct uart_bas *bas)
{
	u_char val;

#ifdef CPU_XBURST
	uart_setreg(bas, REG_FCR, FCR_UART_ON);
#endif

	/* Check known 0 bits that don't depend on DLAB. */
	val = uart_getreg(bas, REG_IIR);
	if (val & 0x30)
		return (ENXIO);
	/*
	 * Bit 6 of the MCR (= 0x40) appears to be 1 for the Sun1699
	 * chip, but otherwise doesn't seem to have a function. In
	 * other words, uart(4) works regardless. Ignore that bit so
	 * the probe succeeds.
	 */
	val = uart_getreg(bas, REG_MCR);
	if (val & 0xa0)
		return (ENXIO);

	return (0);
}

static void
ns8250_init(struct uart_bas *bas, int baudrate, int databits, int stopbits,
    int parity)
{
	u_char ier, val;

	if (bas->rclk == 0)
		bas->rclk = DEFAULT_RCLK;
	ns8250_param(bas, baudrate, databits, stopbits, parity);

	/* Disable all interrupt sources. */
	/*
	 * We use 0xe0 instead of 0xf0 as the mask because the XScale PXA
	 * UARTs split the receive time-out interrupt bit out separately as
	 * 0x10.  This gets handled by ier_mask and ier_rxbits below.
	 */
	ier = uart_getreg(bas, REG_IER) & 0xe0;
	uart_setreg(bas, REG_IER, ier);
	uart_barrier(bas);

	/* Disable the FIFO (if present). */
	val = 0;
#ifdef CPU_XBURST
	val |= FCR_UART_ON;
#endif
	uart_setreg(bas, REG_FCR, val);
	uart_barrier(bas);

	/* Set RTS & DTR. */
	uart_setreg(bas, REG_MCR, MCR_IE | MCR_RTS | MCR_DTR);
	uart_barrier(bas);

	ns8250_clrint(bas);
}

static void
ns8250_term(struct uart_bas *bas)
{

	/* Clear RTS & DTR. */
	uart_setreg(bas, REG_MCR, MCR_IE);
	uart_barrier(bas);
}

static void
ns8250_putc(struct uart_bas *bas, int c)
{
	int limit;

	limit = 250000;
	while ((uart_getreg(bas, REG_LSR) & LSR_THRE) == 0 && --limit)
		DELAY(4);
	uart_setreg(bas, REG_DATA, c);
	uart_barrier(bas);
}

static int
ns8250_rxready(struct uart_bas *bas)
{

	return ((uart_getreg(bas, REG_LSR) & LSR_RXRDY) != 0 ? 1 : 0);
}

static int
ns8250_getc(struct uart_bas *bas, struct mtx *hwmtx)
{
	int c;

	uart_lock(hwmtx);

	while ((uart_getreg(bas, REG_LSR) & LSR_RXRDY) == 0) {
		uart_unlock(hwmtx);
		DELAY(4);
		uart_lock(hwmtx);
	}

	c = uart_getreg(bas, REG_DATA);

	uart_unlock(hwmtx);

	return (c);
}

static kobj_method_t ns8250_methods[] = {
	KOBJMETHOD(uart_attach,		ns8250_bus_attach),
	KOBJMETHOD(uart_detach,		ns8250_bus_detach),
	KOBJMETHOD(uart_flush,		ns8250_bus_flush),
	KOBJMETHOD(uart_getsig,		ns8250_bus_getsig),
	KOBJMETHOD(uart_ioctl,		ns8250_bus_ioctl),
	KOBJMETHOD(uart_ipend,		ns8250_bus_ipend),
	KOBJMETHOD(uart_param,		ns8250_bus_param),
	KOBJMETHOD(uart_probe,		ns8250_bus_probe),
	KOBJMETHOD(uart_receive,	ns8250_bus_receive),
	KOBJMETHOD(uart_setsig,		ns8250_bus_setsig),
	KOBJMETHOD(uart_transmit,	ns8250_bus_transmit),
	KOBJMETHOD(uart_grab,		ns8250_bus_grab),
	KOBJMETHOD(uart_ungrab,		ns8250_bus_ungrab),
	{ 0, 0 }
};

struct uart_class uart_ns8250_class = {
	"ns8250",
	ns8250_methods,
	sizeof(struct ns8250_softc),
	.uc_ops = &uart_ns8250_ops,
	.uc_range = 8,
	.uc_rclk = DEFAULT_RCLK,
	.uc_rshift = 0
};

/*
 * XXX -- refactor out ACPI and FDT ifdefs
 */
#ifdef DEV_ACPI
static struct acpi_uart_compat_data acpi_compat_data[] = {
	{"AMD0020",	&uart_ns8250_class, 0, 2, 0, 48000000, UART_F_BUSY_DETECT, "AMD / Synopsys Designware UART"},
	{"AMDI0020", &uart_ns8250_class, 0, 2, 0, 48000000, UART_F_BUSY_DETECT, "AMD / Synopsys Designware UART"},
	{"PNP0500", &uart_ns8250_class, 0, 0, 0, 0, 0, "Standard PC COM port"},
	{"PNP0501", &uart_ns8250_class, 0, 0, 0, 0, 0, "16550A-compatible COM port"},
	{"PNP0502", &uart_ns8250_class, 0, 0, 0, 0, 0, "Multiport serial device (non-intelligent 16550)"},
	{"PNP0510", &uart_ns8250_class, 0, 0, 0, 0, 0, "Generic IRDA-compatible device"},
	{"PNP0511", &uart_ns8250_class, 0, 0, 0, 0, 0, "Generic IRDA-compatible device"},
	{"WACF004", &uart_ns8250_class, 0, 0, 0, 0, 0, "Wacom Tablet PC Screen"},
	{"WACF00E", &uart_ns8250_class, 0, 0, 0, 0, 0, "Wacom Tablet PC Screen 00e"},
	{"FUJ02E5", &uart_ns8250_class, 0, 0, 0, 0, 0, "Wacom Tablet at FuS Lifebook T"},
	{NULL, 			NULL, 0, 0 , 0, 0, 0, NULL},
};
UART_ACPI_CLASS_AND_DEVICE(acpi_compat_data);
#endif

#ifdef FDT
static struct ofw_compat_data compat_data[] = {
	{"ns16550",		(uintptr_t)&uart_ns8250_class},
	{"ns16550a",		(uintptr_t)&uart_ns8250_class},
	{NULL,			(uintptr_t)NULL},
};
UART_FDT_CLASS_AND_DEVICE(compat_data);
#endif

/* Use token-pasting to form SER_ and MSR_ named constants. */
#define	SER(sig)	SER_##sig
#define	SERD(sig)	SER_D##sig
#define	MSR(sig)	MSR_##sig
#define	MSRD(sig)	MSR_D##sig

/*
 * Detect signal changes using software delta detection.  The previous state of
 * the signals is in 'var' the new hardware state is in 'msr', and 'sig' is the
 * short name (DCD, CTS, etc) of the signal bit being processed; 'var' gets the
 * new state of both the signal and the delta bits.
 */
#define SIGCHGSW(var, msr, sig)					\
	if ((msr) & MSR(sig)) {					\
		if ((var & SER(sig)) == 0)			\
			var |= SERD(sig) | SER(sig);		\
	} else {						\
		if ((var & SER(sig)) != 0)			\
			var = SERD(sig) | (var & ~SER(sig));	\
	}

/*
 * Detect signal changes using the hardware msr delta bits.  This is currently
 * used only when PPS timing information is being captured using the "narrow
 * pulse" option.  With a narrow PPS pulse the signal may not still be asserted
 * by time the interrupt handler is invoked.  The hardware will latch the fact
 * that it changed in the delta bits.
 */
#define SIGCHGHW(var, msr, sig)					\
	if ((msr) & MSRD(sig)) {				\
		if (((msr) & MSR(sig)) != 0)			\
			var |= SERD(sig) | SER(sig);		\
		else						\
			var = SERD(sig) | (var & ~SER(sig));	\
	}

int
ns8250_bus_attach(struct uart_softc *sc)
{
	struct ns8250_softc *ns8250 = (struct ns8250_softc*)sc;
	struct uart_bas *bas;
	unsigned int ivar;
#ifdef FDT
	phandle_t node;
	pcell_t cell;
#endif

#ifdef FDT
	/* Check whether uart has a broken txfifo. */
	node = ofw_bus_get_node(sc->sc_dev);
	if ((OF_getencprop(node, "broken-txfifo", &cell, sizeof(cell))) > 0)
		broken_txfifo =  cell ? 1 : 0;
#endif

	bas = &sc->sc_bas;

	ns8250->busy_detect = bas->busy_detect;
	ns8250->mcr = uart_getreg(bas, REG_MCR);
	ns8250->fcr = FCR_ENABLE;
#ifdef CPU_XBURST
	ns8250->fcr |= FCR_UART_ON;
#endif
	if (!resource_int_value("uart", device_get_unit(sc->sc_dev), "flags",
	    &ivar)) {
		if (UART_FLAGS_FCR_RX_LOW(ivar)) 
			ns8250->fcr |= FCR_RX_LOW;
		else if (UART_FLAGS_FCR_RX_MEDL(ivar)) 
			ns8250->fcr |= FCR_RX_MEDL;
		else if (UART_FLAGS_FCR_RX_HIGH(ivar)) 
			ns8250->fcr |= FCR_RX_HIGH;
		else
			ns8250->fcr |= FCR_RX_MEDH;
	} else 
		ns8250->fcr |= FCR_RX_MEDH;
	
	/* Get IER mask */
	ivar = 0xf0;
	resource_int_value("uart", device_get_unit(sc->sc_dev), "ier_mask",
	    &ivar);
	ns8250->ier_mask = (uint8_t)(ivar & 0xff);
	
	/* Get IER RX interrupt bits */
	ivar = IER_EMSC | IER_ERLS | IER_ERXRDY;
	resource_int_value("uart", device_get_unit(sc->sc_dev), "ier_rxbits",
	    &ivar);
	ns8250->ier_rxbits = (uint8_t)(ivar & 0xff);
	
	uart_setreg(bas, REG_FCR, ns8250->fcr);
	uart_barrier(bas);
	ns8250_bus_flush(sc, UART_FLUSH_RECEIVER|UART_FLUSH_TRANSMITTER);

	if (ns8250->mcr & MCR_DTR)
		sc->sc_hwsig |= SER_DTR;
	if (ns8250->mcr & MCR_RTS)
		sc->sc_hwsig |= SER_RTS;
	ns8250_bus_getsig(sc);

	ns8250_clrint(bas);
	ns8250->ier = uart_getreg(bas, REG_IER) & ns8250->ier_mask;
	ns8250->ier |= ns8250->ier_rxbits;
	uart_setreg(bas, REG_IER, ns8250->ier);
	uart_barrier(bas);

	/*
	 * Timing of the H/W access was changed with r253161 of uart_core.c
	 * It has been observed that an ITE IT8513E would signal a break
	 * condition with pretty much every character it received, unless
	 * it had enough time to settle between ns8250_bus_attach() and
	 * ns8250_bus_ipend() -- which it accidentally had before r253161.
	 * It's not understood why the UART chip behaves this way and it
	 * could very well be that the DELAY make the H/W work in the same
	 * accidental manner as before. More analysis is warranted, but
	 * at least now we fixed a known regression.
	 */
	DELAY(200);
	return (0);
}

int
ns8250_bus_detach(struct uart_softc *sc)
{
	struct ns8250_softc *ns8250;
	struct uart_bas *bas;
	u_char ier;

	ns8250 = (struct ns8250_softc *)sc;
	bas = &sc->sc_bas;
	ier = uart_getreg(bas, REG_IER) & ns8250->ier_mask;
	uart_setreg(bas, REG_IER, ier);
	uart_barrier(bas);
	ns8250_clrint(bas);
	return (0);
}

int
ns8250_bus_flush(struct uart_softc *sc, int what)
{
	struct ns8250_softc *ns8250 = (struct ns8250_softc*)sc;
	struct uart_bas *bas;
	int error;

	bas = &sc->sc_bas;
	uart_lock(sc->sc_hwmtx);
	if (sc->sc_rxfifosz > 1) {
		ns8250_flush(bas, what);
		uart_setreg(bas, REG_FCR, ns8250->fcr);
		uart_barrier(bas);
		error = 0;
	} else
		error = ns8250_drain(bas, what);
	uart_unlock(sc->sc_hwmtx);
	return (error);
}

int
ns8250_bus_getsig(struct uart_softc *sc)
{
	uint32_t old, sig;
	uint8_t msr;

	/*
	 * The delta bits are reputed to be broken on some hardware, so use
	 * software delta detection by default.  Use the hardware delta bits
	 * when capturing PPS pulses which are too narrow for software detection
	 * to see the edges.  Hardware delta for RI doesn't work like the
	 * others, so always use software for it.  Other threads may be changing
	 * other (non-MSR) bits in sc_hwsig, so loop until it can successfully
	 * update without other changes happening.  Note that the SIGCHGxx()
	 * macros carefully preserve the delta bits when we have to loop several
	 * times and a signal transitions between iterations.
	 */
	do {
		old = sc->sc_hwsig;
		sig = old;
		uart_lock(sc->sc_hwmtx);
		msr = uart_getreg(&sc->sc_bas, REG_MSR);
		uart_unlock(sc->sc_hwmtx);
		if (sc->sc_pps_mode & UART_PPS_NARROW_PULSE) {
			SIGCHGHW(sig, msr, DSR);
			SIGCHGHW(sig, msr, CTS);
			SIGCHGHW(sig, msr, DCD);
		} else {
			SIGCHGSW(sig, msr, DSR);
			SIGCHGSW(sig, msr, CTS);
			SIGCHGSW(sig, msr, DCD);
		}
		SIGCHGSW(sig, msr, RI);
	} while (!atomic_cmpset_32(&sc->sc_hwsig, old, sig & ~SER_MASK_DELTA));
	return (sig);
}

int
ns8250_bus_ioctl(struct uart_softc *sc, int request, intptr_t data)
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

int
ns8250_bus_ipend(struct uart_softc *sc)
{
	struct uart_bas *bas;
	struct ns8250_softc *ns8250;
	int ipend;
	uint8_t iir, lsr;

	ns8250 = (struct ns8250_softc *)sc;
	bas = &sc->sc_bas;
	uart_lock(sc->sc_hwmtx);
	iir = uart_getreg(bas, REG_IIR);

	if (ns8250->busy_detect && (iir & IIR_BUSY) == IIR_BUSY) {
		(void)uart_getreg(bas, DW_REG_USR);
		uart_unlock(sc->sc_hwmtx);
		return (0);
	}
	if (iir & IIR_NOPEND) {
		uart_unlock(sc->sc_hwmtx);
		return (0);
	}
	ipend = 0;
	if (iir & IIR_RXRDY) {
		lsr = uart_getreg(bas, REG_LSR);
		if (lsr & LSR_OE)
			ipend |= SER_INT_OVERRUN;
		if (lsr & LSR_BI)
			ipend |= SER_INT_BREAK;
		if (lsr & LSR_RXRDY)
			ipend |= SER_INT_RXREADY;
	} else {
		if (iir & IIR_TXRDY) {
			ipend |= SER_INT_TXIDLE;
			uart_setreg(bas, REG_IER, ns8250->ier);
			uart_barrier(bas);
		} else
			ipend |= SER_INT_SIGCHG;
	}
	if (ipend == 0)
		ns8250_clrint(bas);
	uart_unlock(sc->sc_hwmtx);
	return (ipend);
}

int
ns8250_bus_param(struct uart_softc *sc, int baudrate, int databits,
    int stopbits, int parity)
{
	struct ns8250_softc *ns8250;
	struct uart_bas *bas;
	int error, limit;

	ns8250 = (struct ns8250_softc*)sc;
	bas = &sc->sc_bas;
	uart_lock(sc->sc_hwmtx);
	/*
	 * When using DW UART with BUSY detection it is necessary to wait
	 * until all serial transfers are finished before manipulating the
	 * line control. LCR will not be affected when UART is busy.
	 */
	if (ns8250->busy_detect != 0) {
		/*
		 * Pick an arbitrary high limit to avoid getting stuck in
		 * an infinite loop in case when the hardware is broken.
		 */
		limit = 10 * 1024;
		while (((uart_getreg(bas, DW_REG_USR) & USR_BUSY) != 0) &&
		    --limit)
			DELAY(4);

		if (limit <= 0) {
			/* UART appears to be stuck */
			uart_unlock(sc->sc_hwmtx);
			return (EIO);
		}
	}

	error = ns8250_param(bas, baudrate, databits, stopbits, parity);
	uart_unlock(sc->sc_hwmtx);
	return (error);
}

int
ns8250_bus_probe(struct uart_softc *sc)
{
	struct ns8250_softc *ns8250;
	struct uart_bas *bas;
	int count, delay, error, limit;
	uint8_t lsr, mcr, ier;
	uint8_t val;

	ns8250 = (struct ns8250_softc *)sc;
	bas = &sc->sc_bas;

	error = ns8250_probe(bas);
	if (error)
		return (error);

	mcr = MCR_IE;
	if (sc->sc_sysdev == NULL) {
		/* By using ns8250_init() we also set DTR and RTS. */
		ns8250_init(bas, 115200, 8, 1, UART_PARITY_NONE);
	} else
		mcr |= MCR_DTR | MCR_RTS;

	error = ns8250_drain(bas, UART_DRAIN_TRANSMITTER);
	if (error)
		return (error);

	/*
	 * Set loopback mode. This avoids having garbage on the wire and
	 * also allows us send and receive data. We set DTR and RTS to
	 * avoid the possibility that automatic flow-control prevents
	 * any data from being sent.
	 */
	uart_setreg(bas, REG_MCR, MCR_LOOPBACK | MCR_IE | MCR_DTR | MCR_RTS);
	uart_barrier(bas);

	/*
	 * Enable FIFOs. And check that the UART has them. If not, we're
	 * done. Since this is the first time we enable the FIFOs, we reset
	 * them.
	 */
	val = FCR_ENABLE;
#ifdef CPU_XBURST
	val |= FCR_UART_ON;
#endif
	uart_setreg(bas, REG_FCR, val);
	uart_barrier(bas);
	if (!(uart_getreg(bas, REG_IIR) & IIR_FIFO_MASK)) {
		/*
		 * NS16450 or INS8250. We don't bother to differentiate
		 * between them. They're too old to be interesting.
		 */
		uart_setreg(bas, REG_MCR, mcr);
		uart_barrier(bas);
		sc->sc_rxfifosz = sc->sc_txfifosz = 1;
		device_set_desc(sc->sc_dev, "8250 or 16450 or compatible");
		return (0);
	}

	val = FCR_ENABLE | FCR_XMT_RST | FCR_RCV_RST;
#ifdef CPU_XBURST
	val |= FCR_UART_ON;
#endif
	uart_setreg(bas, REG_FCR, val);
	uart_barrier(bas);

	count = 0;
	delay = ns8250_delay(bas);

	/* We have FIFOs. Drain the transmitter and receiver. */
	error = ns8250_drain(bas, UART_DRAIN_RECEIVER|UART_DRAIN_TRANSMITTER);
	if (error) {
		uart_setreg(bas, REG_MCR, mcr);
		val = 0;
#ifdef CPU_XBURST
		val |= FCR_UART_ON;
#endif
		uart_setreg(bas, REG_FCR, val);
		uart_barrier(bas);
		goto describe;
	}

	/*
	 * We should have a sufficiently clean "pipe" to determine the
	 * size of the FIFOs. We send as much characters as is reasonable
	 * and wait for the overflow bit in the LSR register to be
	 * asserted, counting the characters as we send them. Based on
	 * that count we know the FIFO size.
	 */
	do {
		uart_setreg(bas, REG_DATA, 0);
		uart_barrier(bas);
		count++;

		limit = 30;
		lsr = 0;
		/*
		 * LSR bits are cleared upon read, so we must accumulate
		 * them to be able to test LSR_OE below.
		 */
		while (((lsr |= uart_getreg(bas, REG_LSR)) & LSR_TEMT) == 0 &&
		    --limit)
			DELAY(delay);
		if (limit == 0) {
			ier = uart_getreg(bas, REG_IER) & ns8250->ier_mask;
			uart_setreg(bas, REG_IER, ier);
			uart_setreg(bas, REG_MCR, mcr);
			val = 0;
#ifdef CPU_XBURST
			val |= FCR_UART_ON;
#endif
			uart_setreg(bas, REG_FCR, val);
			uart_barrier(bas);
			count = 0;
			goto describe;
		}
	} while ((lsr & LSR_OE) == 0 && count < 260);
	count--;

	uart_setreg(bas, REG_MCR, mcr);

	/* Reset FIFOs. */
	ns8250_flush(bas, UART_FLUSH_RECEIVER|UART_FLUSH_TRANSMITTER);

 describe:
	if (count >= 14 && count <= 16) {
		sc->sc_rxfifosz = 16;
		device_set_desc(sc->sc_dev, "16550 or compatible");
	} else if (count >= 28 && count <= 32) {
		sc->sc_rxfifosz = 32;
		device_set_desc(sc->sc_dev, "16650 or compatible");
	} else if (count >= 56 && count <= 64) {
		sc->sc_rxfifosz = 64;
		device_set_desc(sc->sc_dev, "16750 or compatible");
	} else if (count >= 112 && count <= 128) {
		sc->sc_rxfifosz = 128;
		device_set_desc(sc->sc_dev, "16950 or compatible");
	} else if (count >= 224 && count <= 256) {
		sc->sc_rxfifosz = 256;
		device_set_desc(sc->sc_dev, "16x50 with 256 byte FIFO");
	} else {
		sc->sc_rxfifosz = 16;
		device_set_desc(sc->sc_dev,
		    "Non-standard ns8250 class UART with FIFOs");
	}

	/*
	 * Force the Tx FIFO size to 16 bytes for now. We don't program the
	 * Tx trigger. Also, we assume that all data has been sent when the
	 * interrupt happens.
	 */
	sc->sc_txfifosz = 16;

#if 0
	/*
	 * XXX there are some issues related to hardware flow control and
	 * it's likely that uart(4) is the cause. This basically needs more
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

int
ns8250_bus_receive(struct uart_softc *sc)
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
	while (lsr & LSR_RXRDY) {
		(void)uart_getreg(bas, REG_DATA);
		uart_barrier(bas);
		lsr = uart_getreg(bas, REG_LSR);
	}
	uart_unlock(sc->sc_hwmtx);
 	return (0);
}

int
ns8250_bus_setsig(struct uart_softc *sc, int sig)
{
	struct ns8250_softc *ns8250 = (struct ns8250_softc*)sc;
	struct uart_bas *bas;
	uint32_t new, old;

	bas = &sc->sc_bas;
	do {
		old = sc->sc_hwsig;
		new = old;
		if (sig & SER_DDTR) {
			new = (new & ~SER_DTR) | (sig & (SER_DTR | SER_DDTR));
		}
		if (sig & SER_DRTS) {
			new = (new & ~SER_RTS) | (sig & (SER_RTS | SER_DRTS));
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
	return (0);
}

int
ns8250_bus_transmit(struct uart_softc *sc)
{
	struct ns8250_softc *ns8250 = (struct ns8250_softc*)sc;
	struct uart_bas *bas;
	int i;

	bas = &sc->sc_bas;
	uart_lock(sc->sc_hwmtx);
	if (sc->sc_txdatasz > 1) {
		if ((uart_getreg(bas, REG_LSR) & LSR_TEMT) == 0)
			ns8250_drain(bas, UART_DRAIN_TRANSMITTER);
	} else {
		while ((uart_getreg(bas, REG_LSR) & LSR_THRE) == 0)
			DELAY(4);
	}
	for (i = 0; i < sc->sc_txdatasz; i++) {
		uart_setreg(bas, REG_DATA, sc->sc_txbuf[i]);
		uart_barrier(bas);
	}
	uart_setreg(bas, REG_IER, ns8250->ier | IER_ETXRDY);
	uart_barrier(bas);
	if (broken_txfifo)
		ns8250_drain(bas, UART_DRAIN_TRANSMITTER);
	else
		sc->sc_txbusy = 1;
	uart_unlock(sc->sc_hwmtx);
	if (broken_txfifo)
		uart_sched_softih(sc, SER_INT_TXIDLE);
	return (0);
}

void
ns8250_bus_grab(struct uart_softc *sc)
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
	uart_barrier(bas);
	uart_unlock(sc->sc_hwmtx);
}

void
ns8250_bus_ungrab(struct uart_softc *sc)
{
	struct ns8250_softc *ns8250 = (struct ns8250_softc*)sc;
	struct uart_bas *bas = &sc->sc_bas;

	/*
	 * Restore previous interrupt mask
	 */
	uart_lock(sc->sc_hwmtx);
	uart_setreg(bas, REG_IER, ns8250->ier);
	uart_barrier(bas);
	uart_unlock(sc->sc_hwmtx);
}
