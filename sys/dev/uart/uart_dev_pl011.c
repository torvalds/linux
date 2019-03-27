/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2012 Semihalf.
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

#include "opt_acpi.h"
#include "opt_platform.h"

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/bus.h>

#include <machine/bus.h>
#include <machine/machdep.h>

#include <dev/uart/uart.h>
#include <dev/uart/uart_cpu.h>
#ifdef FDT
#include <dev/uart/uart_cpu_fdt.h>
#include <dev/ofw/ofw_bus.h>
#endif
#include <dev/uart/uart_bus.h>
#include "uart_if.h"

#ifdef DEV_ACPI
#include <dev/uart/uart_cpu_acpi.h>
#include <contrib/dev/acpica/include/acpi.h>
#include <contrib/dev/acpica/include/accommon.h>
#include <contrib/dev/acpica/include/actables.h>
#endif

#include <sys/kdb.h>

#ifdef __aarch64__
#define	IS_FDT	(arm64_bus_method == ARM64_BUS_FDT)
#elif defined(FDT)
#define	IS_FDT	1
#else
#error Unsupported configuration
#endif

/* PL011 UART registers and masks*/
#define	UART_DR		0x00		/* Data register */
#define	DR_FE		(1 << 8)	/* Framing error */
#define	DR_PE		(1 << 9)	/* Parity error */
#define	DR_BE		(1 << 10)	/* Break error */
#define	DR_OE		(1 << 11)	/* Overrun error */

#define	UART_FR		0x06		/* Flag register */
#define	FR_RXFE		(1 << 4)	/* Receive FIFO/reg empty */
#define	FR_TXFF		(1 << 5)	/* Transmit FIFO/reg full */
#define	FR_RXFF		(1 << 6)	/* Receive FIFO/reg full */
#define	FR_TXFE		(1 << 7)	/* Transmit FIFO/reg empty */

#define	UART_IBRD	0x09		/* Integer baud rate register */
#define	IBRD_BDIVINT	0xffff	/* Significant part of int. divisor value */

#define	UART_FBRD	0x0a		/* Fractional baud rate register */
#define	FBRD_BDIVFRAC	0x3f	/* Significant part of frac. divisor value */

#define	UART_LCR_H	0x0b		/* Line control register */
#define	LCR_H_WLEN8	(0x3 << 5)
#define	LCR_H_WLEN7	(0x2 << 5)
#define	LCR_H_WLEN6	(0x1 << 5)
#define	LCR_H_FEN	(1 << 4)	/* FIFO mode enable */
#define	LCR_H_STP2	(1 << 3)	/* 2 stop frames at the end */
#define	LCR_H_EPS	(1 << 2)	/* Even parity select */
#define	LCR_H_PEN	(1 << 1)	/* Parity enable */

#define	UART_CR		0x0c		/* Control register */
#define	CR_RXE		(1 << 9)	/* Receive enable */
#define	CR_TXE		(1 << 8)	/* Transmit enable */
#define	CR_UARTEN	(1 << 0)	/* UART enable */

#define	UART_IFLS	0x0d		/* FIFO level select register */
#define	IFLS_RX_SHIFT	3		/* RX level in bits [5:3] */
#define	IFLS_TX_SHIFT	0		/* TX level in bits [2:0] */
#define	IFLS_MASK	0x07		/* RX/TX level is 3 bits */
#define	IFLS_LVL_1_8th	0		/* Interrupt at 1/8 full */
#define	IFLS_LVL_2_8th	1		/* Interrupt at 1/4 full */
#define	IFLS_LVL_4_8th	2		/* Interrupt at 1/2 full */
#define	IFLS_LVL_6_8th	3		/* Interrupt at 3/4 full */
#define	IFLS_LVL_7_8th	4		/* Interrupt at 7/8 full */

#define	UART_IMSC	0x0e		/* Interrupt mask set/clear register */
#define	IMSC_MASK_ALL	0x7ff		/* Mask all interrupts */

#define	UART_RIS	0x0f		/* Raw interrupt status register */
#define	UART_RXREADY	(1 << 4)	/* RX buffer full */
#define	UART_TXEMPTY	(1 << 5)	/* TX buffer empty */
#define	RIS_RTIM	(1 << 6)	/* Receive timeout */
#define	RIS_FE		(1 << 7)	/* Framing error interrupt status */
#define	RIS_PE		(1 << 8)	/* Parity error interrupt status */
#define	RIS_BE		(1 << 9)	/* Break error interrupt status */
#define	RIS_OE		(1 << 10)	/* Overrun interrupt status */

#define	UART_MIS	0x10		/* Masked interrupt status register */
#define	UART_ICR	0x11		/* Interrupt clear register */

#define	UART_PIDREG_0	0x3f8		/* Peripheral ID register 0 */
#define	UART_PIDREG_1	0x3f9		/* Peripheral ID register 1 */
#define	UART_PIDREG_2	0x3fa		/* Peripheral ID register 2 */
#define	UART_PIDREG_3	0x3fb		/* Peripheral ID register 3 */

/*
 * The hardware FIFOs are 16 bytes each on rev 2 and earlier hardware, 32 bytes
 * on rev 3 and later.  We configure them to interrupt when 3/4 full/empty.  For
 * RX we set the size to the full hardware capacity so that the uart core
 * allocates enough buffer space to hold a complete fifo full of incoming data.
 * For TX, we need to limit the size to the capacity we know will be available
 * when the interrupt occurs; uart_core will feed exactly that many bytes to
 * uart_pl011_bus_transmit() which must consume them all.
 */
#define	FIFO_RX_SIZE_R2	16
#define	FIFO_TX_SIZE_R2	12
#define	FIFO_RX_SIZE_R3	32
#define	FIFO_TX_SIZE_R3	24
#define	FIFO_IFLS_BITS	((IFLS_LVL_6_8th << IFLS_RX_SHIFT) | (IFLS_LVL_2_8th))

/*
 * FIXME: actual register size is SoC-dependent, we need to handle it
 */
#define	__uart_getreg(bas, reg)		\
	bus_space_read_4((bas)->bst, (bas)->bsh, uart_regofs(bas, reg))
#define	__uart_setreg(bas, reg, value)	\
	bus_space_write_4((bas)->bst, (bas)->bsh, uart_regofs(bas, reg), value)

/*
 * Low-level UART interface.
 */
static int uart_pl011_probe(struct uart_bas *bas);
static void uart_pl011_init(struct uart_bas *bas, int, int, int, int);
static void uart_pl011_term(struct uart_bas *bas);
static void uart_pl011_putc(struct uart_bas *bas, int);
static int uart_pl011_rxready(struct uart_bas *bas);
static int uart_pl011_getc(struct uart_bas *bas, struct mtx *);

static struct uart_ops uart_pl011_ops = {
	.probe = uart_pl011_probe,
	.init = uart_pl011_init,
	.term = uart_pl011_term,
	.putc = uart_pl011_putc,
	.rxready = uart_pl011_rxready,
	.getc = uart_pl011_getc,
};

static int
uart_pl011_probe(struct uart_bas *bas)
{

	return (0);
}

static void
uart_pl011_param(struct uart_bas *bas, int baudrate, int databits, int stopbits,
    int parity)
{
	uint32_t ctrl, line;
	uint32_t baud;

	/*
	 * Zero all settings to make sure
	 * UART is disabled and not configured
	 */
	ctrl = line = 0x0;
	__uart_setreg(bas, UART_CR, ctrl);

	/* As we know UART is disabled we may setup the line */
	switch (databits) {
	case 7:
		line |= LCR_H_WLEN7;
		break;
	case 6:
		line |= LCR_H_WLEN6;
		break;
	case 8:
	default:
		line |= LCR_H_WLEN8;
		break;
	}

	if (stopbits == 2)
		line |= LCR_H_STP2;
	else
		line &= ~LCR_H_STP2;

	if (parity)
		line |= LCR_H_PEN;
	else
		line &= ~LCR_H_PEN;
	line |= LCR_H_FEN;

	/* Configure the rest */
	ctrl |= (CR_RXE | CR_TXE | CR_UARTEN);

	if (bas->rclk != 0 && baudrate != 0) {
		baud = bas->rclk * 4 / baudrate;
		__uart_setreg(bas, UART_IBRD, ((uint32_t)(baud >> 6)) & IBRD_BDIVINT);
		__uart_setreg(bas, UART_FBRD, (uint32_t)(baud & 0x3F) & FBRD_BDIVFRAC);
	}

	/* Add config. to line before reenabling UART */
	__uart_setreg(bas, UART_LCR_H, (__uart_getreg(bas, UART_LCR_H) &
	    ~0xff) | line);

	/* Set rx and tx fifo levels. */
	__uart_setreg(bas, UART_IFLS, FIFO_IFLS_BITS);

	__uart_setreg(bas, UART_CR, ctrl);
}

static void
uart_pl011_init(struct uart_bas *bas, int baudrate, int databits, int stopbits,
    int parity)
{
	/* Mask all interrupts */
	__uart_setreg(bas, UART_IMSC, __uart_getreg(bas, UART_IMSC) &
	    ~IMSC_MASK_ALL);

	uart_pl011_param(bas, baudrate, databits, stopbits, parity);
}

static void
uart_pl011_term(struct uart_bas *bas)
{
}

static void
uart_pl011_putc(struct uart_bas *bas, int c)
{

	/* Wait when TX FIFO full. Push character otherwise. */
	while (__uart_getreg(bas, UART_FR) & FR_TXFF)
		;
	__uart_setreg(bas, UART_DR, c & 0xff);
}

static int
uart_pl011_rxready(struct uart_bas *bas)
{

	return !(__uart_getreg(bas, UART_FR) & FR_RXFE);
}

static int
uart_pl011_getc(struct uart_bas *bas, struct mtx *hwmtx)
{
	int c;

	while (!uart_pl011_rxready(bas))
		;
	c = __uart_getreg(bas, UART_DR) & 0xff;

	return (c);
}

/*
 * High-level UART interface.
 */
struct uart_pl011_softc {
	struct uart_softc	base;
	uint16_t		imsc; /* Interrupt mask */
};

static int uart_pl011_bus_attach(struct uart_softc *);
static int uart_pl011_bus_detach(struct uart_softc *);
static int uart_pl011_bus_flush(struct uart_softc *, int);
static int uart_pl011_bus_getsig(struct uart_softc *);
static int uart_pl011_bus_ioctl(struct uart_softc *, int, intptr_t);
static int uart_pl011_bus_ipend(struct uart_softc *);
static int uart_pl011_bus_param(struct uart_softc *, int, int, int, int);
static int uart_pl011_bus_probe(struct uart_softc *);
static int uart_pl011_bus_receive(struct uart_softc *);
static int uart_pl011_bus_setsig(struct uart_softc *, int);
static int uart_pl011_bus_transmit(struct uart_softc *);
static void uart_pl011_bus_grab(struct uart_softc *);
static void uart_pl011_bus_ungrab(struct uart_softc *);

static kobj_method_t uart_pl011_methods[] = {
	KOBJMETHOD(uart_attach,		uart_pl011_bus_attach),
	KOBJMETHOD(uart_detach,		uart_pl011_bus_detach),
	KOBJMETHOD(uart_flush,		uart_pl011_bus_flush),
	KOBJMETHOD(uart_getsig,		uart_pl011_bus_getsig),
	KOBJMETHOD(uart_ioctl,		uart_pl011_bus_ioctl),
	KOBJMETHOD(uart_ipend,		uart_pl011_bus_ipend),
	KOBJMETHOD(uart_param,		uart_pl011_bus_param),
	KOBJMETHOD(uart_probe,		uart_pl011_bus_probe),
	KOBJMETHOD(uart_receive,	uart_pl011_bus_receive),
	KOBJMETHOD(uart_setsig,		uart_pl011_bus_setsig),
	KOBJMETHOD(uart_transmit,	uart_pl011_bus_transmit),
	KOBJMETHOD(uart_grab,		uart_pl011_bus_grab),
	KOBJMETHOD(uart_ungrab,		uart_pl011_bus_ungrab),

	{ 0, 0 }
};

static struct uart_class uart_pl011_class = {
	"uart_pl011",
	uart_pl011_methods,
	sizeof(struct uart_pl011_softc),
	.uc_ops = &uart_pl011_ops,
	.uc_range = 0x48,
	.uc_rclk = 0,
	.uc_rshift = 2
};


#ifdef FDT
static struct ofw_compat_data fdt_compat_data[] = {
	{"arm,pl011",		(uintptr_t)&uart_pl011_class},
	{NULL,			(uintptr_t)NULL},
};
UART_FDT_CLASS_AND_DEVICE(fdt_compat_data);
#endif

#ifdef DEV_ACPI
static struct acpi_uart_compat_data acpi_compat_data[] = {
	{"ARMH0011", &uart_pl011_class, ACPI_DBG2_ARM_PL011, 2, 0, 0, 0, "uart plo11"},
	{"ARMH0011", &uart_pl011_class, ACPI_DBG2_ARM_SBSA_GENERIC, 2, 0, 0, 0, "uart plo11"},
	{NULL, NULL, 0, 0, 0, 0, 0, NULL},
};
UART_ACPI_CLASS_AND_DEVICE(acpi_compat_data);
#endif

static int
uart_pl011_bus_attach(struct uart_softc *sc)
{
	struct uart_pl011_softc *psc;
	struct uart_bas *bas;

	psc = (struct uart_pl011_softc *)sc;
	bas = &sc->sc_bas;

	/* Enable interrupts */
	psc->imsc = (UART_RXREADY | RIS_RTIM | UART_TXEMPTY);
	__uart_setreg(bas, UART_IMSC, psc->imsc);

	/* Clear interrupts */
	__uart_setreg(bas, UART_ICR, IMSC_MASK_ALL);

	return (0);
}

static int
uart_pl011_bus_detach(struct uart_softc *sc)
{

	return (0);
}

static int
uart_pl011_bus_flush(struct uart_softc *sc, int what)
{

	return (0);
}

static int
uart_pl011_bus_getsig(struct uart_softc *sc)
{

	return (0);
}

static int
uart_pl011_bus_ioctl(struct uart_softc *sc, int request, intptr_t data)
{
	int error;

	error = 0;
	uart_lock(sc->sc_hwmtx);
	switch (request) {
	case UART_IOCTL_BREAK:
		break;
	case UART_IOCTL_BAUD:
		*(int*)data = 115200;
		break;
	default:
		error = EINVAL;
		break;
	}
	uart_unlock(sc->sc_hwmtx);

	return (error);
}

static int
uart_pl011_bus_ipend(struct uart_softc *sc)
{
	struct uart_pl011_softc *psc;
	struct uart_bas *bas;
	uint32_t ints;
	int ipend;

	psc = (struct uart_pl011_softc *)sc;
	bas = &sc->sc_bas;

	uart_lock(sc->sc_hwmtx);
	ints = __uart_getreg(bas, UART_MIS);
	ipend = 0;

	if (ints & (UART_RXREADY | RIS_RTIM))
		ipend |= SER_INT_RXREADY;
	if (ints & RIS_BE)
		ipend |= SER_INT_BREAK;
	if (ints & RIS_OE)
		ipend |= SER_INT_OVERRUN;
	if (ints & UART_TXEMPTY) {
		if (sc->sc_txbusy)
			ipend |= SER_INT_TXIDLE;

		/* Disable TX interrupt */
		__uart_setreg(bas, UART_IMSC, psc->imsc & ~UART_TXEMPTY);
	}

	uart_unlock(sc->sc_hwmtx);

	return (ipend);
}

static int
uart_pl011_bus_param(struct uart_softc *sc, int baudrate, int databits,
    int stopbits, int parity)
{

	uart_lock(sc->sc_hwmtx);
	uart_pl011_param(&sc->sc_bas, baudrate, databits, stopbits, parity);
	uart_unlock(sc->sc_hwmtx);

	return (0);
}

#ifdef FDT
static int
uart_pl011_bus_hwrev_fdt(struct uart_softc *sc)
{
	pcell_t node;
	uint32_t periphid;

	/*
	 * The FIFO sizes vary depending on hardware; rev 2 and below have 16
	 * byte FIFOs, rev 3 and up are 32 byte.  The hardware rev is in the
	 * primecell periphid register, but we get a bit of drama, as always,
	 * with the bcm2835 (rpi), which claims to be rev 3, but has 16 byte
	 * FIFOs.  We check for both the old freebsd-historic and the proper
	 * bindings-defined compatible strings for bcm2835, and also check the
	 * workaround the linux drivers use for rpi3, which is to override the
	 * primecell periphid register value with a property.
	 */
	if (ofw_bus_is_compatible(sc->sc_dev, "brcm,bcm2835-pl011") ||
	    ofw_bus_is_compatible(sc->sc_dev, "broadcom,bcm2835-uart")) {
		return (2);
	} else {
		node = ofw_bus_get_node(sc->sc_dev);
		if (OF_getencprop(node, "arm,primecell-periphid", &periphid,
		    sizeof(periphid)) > 0) {
			return ((periphid >> 20) & 0x0f);
		}
	}

	return (-1);
}
#endif

static int
uart_pl011_bus_probe(struct uart_softc *sc)
{
	int hwrev;

	hwrev = -1;
#ifdef FDT
	if (IS_FDT)
		hwrev = uart_pl011_bus_hwrev_fdt(sc);
#endif
	if (hwrev < 0)
		hwrev = __uart_getreg(&sc->sc_bas, UART_PIDREG_2) >> 4;

	if (hwrev <= 2) {
		sc->sc_rxfifosz = FIFO_RX_SIZE_R2;
		sc->sc_txfifosz = FIFO_TX_SIZE_R2;
	} else {
		sc->sc_rxfifosz = FIFO_RX_SIZE_R3;
		sc->sc_txfifosz = FIFO_TX_SIZE_R3;
	}

	device_set_desc(sc->sc_dev, "PrimeCell UART (PL011)");

	return (0);
}

static int
uart_pl011_bus_receive(struct uart_softc *sc)
{
	struct uart_bas *bas;
	uint32_t ints, xc;
	int rx;

	bas = &sc->sc_bas;
	uart_lock(sc->sc_hwmtx);

	for (;;) {
		ints = __uart_getreg(bas, UART_FR);
		if (ints & FR_RXFE)
			break;
		if (uart_rx_full(sc)) {
			sc->sc_rxbuf[sc->sc_rxput] = UART_STAT_OVERRUN;
			break;
		}

		xc = __uart_getreg(bas, UART_DR);
		rx = xc & 0xff;

		if (xc & DR_FE)
			rx |= UART_STAT_FRAMERR;
		if (xc & DR_PE)
			rx |= UART_STAT_PARERR;

		uart_rx_put(sc, rx);
	}

	uart_unlock(sc->sc_hwmtx);

	return (0);
}

static int
uart_pl011_bus_setsig(struct uart_softc *sc, int sig)
{

	return (0);
}

static int
uart_pl011_bus_transmit(struct uart_softc *sc)
{
	struct uart_pl011_softc *psc;
	struct uart_bas *bas;
	int i;

	psc = (struct uart_pl011_softc *)sc;
	bas = &sc->sc_bas;
	uart_lock(sc->sc_hwmtx);

	for (i = 0; i < sc->sc_txdatasz; i++) {
		__uart_setreg(bas, UART_DR, sc->sc_txbuf[i]);
		uart_barrier(bas);
	}

	/* Mark busy and enable TX interrupt */
	sc->sc_txbusy = 1;
	__uart_setreg(bas, UART_IMSC, psc->imsc);

	uart_unlock(sc->sc_hwmtx);

	return (0);
}

static void
uart_pl011_bus_grab(struct uart_softc *sc)
{
	struct uart_pl011_softc *psc;
	struct uart_bas *bas;

	psc = (struct uart_pl011_softc *)sc;
	bas = &sc->sc_bas;

	/* Disable interrupts on switch to polling */
	uart_lock(sc->sc_hwmtx);
	__uart_setreg(bas, UART_IMSC, psc->imsc & ~IMSC_MASK_ALL);
	uart_unlock(sc->sc_hwmtx);
}

static void
uart_pl011_bus_ungrab(struct uart_softc *sc)
{
	struct uart_pl011_softc *psc;
	struct uart_bas *bas;

	psc = (struct uart_pl011_softc *)sc;
	bas = &sc->sc_bas;

	/* Switch to using interrupts while not grabbed */
	uart_lock(sc->sc_hwmtx);
	__uart_setreg(bas, UART_IMSC, psc->imsc);
	uart_unlock(sc->sc_hwmtx);
}
