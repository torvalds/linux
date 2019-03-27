/*-
 * Copyright (c) 2018 Diane Bruce 
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

/*
 * Based on uart_dev_pl011.c
 * Copyright (c) 2012 Semihalf.
 * All rights reserved.
 */
/*
 * The mini Uart has the following features: 
 * - 7 or 8 bit operation. 
 * - 1 start and 1 stop bit. 
 * - No parities. 
 * - Break generation. 
 * - 8 symbols deep FIFOs for receive and transmit. 
 * - SW controlled RTS, SW readable CTS. 
 * - Auto flow control with programmable FIFO level. 
 * - 16550 like registers. 
 * - Baudrate derived from system clock. 
 * This is a mini UART and it does NOT have the following capabilities: 
 * - Break detection 
 * - Framing errors detection. 
 * - Parity bit 
 * - Receive Time-out interrupt 
 * - DCD, DSR, DTR or RI signals. 
 * The implemented UART is not a 16650 compatible UART However as far
 * as possible the first 8 control and status registers are laid out 
 * like a 16550 UART. All 16550 register bits which are not supported can
 * be written but will be ignored and read back as 0. All control bits
 * for simple UART receive/transmit operations are available.
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
#include <machine/pcpu.h>

#include <dev/uart/uart.h>
#include <dev/uart/uart_cpu.h>
#ifdef FDT
#include <dev/uart/uart_cpu_fdt.h>
#include <dev/ofw/ofw_bus.h>
#endif
#include <dev/uart/uart_bus.h>
#include "uart_if.h"

/* BCM2835 Micro UART registers and masks*/
#define	AUX_MU_IO_REG		0x00		/* I/O register */

/*
 * According to errata bits 1 and 2 are swapped,
 * Also bits 2 and 3 are required to enable interrupts.
 */
#define	AUX_MU_IER_REG		0x01
#define IER_RXENABLE		(1)
#define IER_TXENABLE		(1<<1)
#define IER_REQUIRED		(3<<2)
#define IER_MASK_ALL		(IER_TXENABLE|IER_RXENABLE)

#define	AUX_MU_IIR_REG		0x02
#define IIR_READY		(1)
#define IIR_TXREADY		(1<<1)
#define IIR_RXREADY		(1<<2)
#define IIR_CLEAR		(3<<1)

#define	AUX_MU_LCR_REG		0x03
#define LCR_WLEN7		(0)
#define LCR_WLEN8		(3)

#define AUX_MU_MCR_REG		0x04
#define AUX_MCR_RTS		(1<<1)
	
#define AUX_MU_LSR_REG		0x05
#define LSR_RXREADY		(1)
#define LSR_OVRRUN		(1<<1)
#define LSR_TXEMPTY		(1<<5)
#define LSR_TXIDLE		(1<<6)

#define AUX_MU_MSR_REG		0x06
#define MSR_CTS			(1<<5)

#define AUX_MU_SCRATCH_REG	0x07

#define AUX_MU_CNTL_REG		0x08
#define CNTL_RXENAB		(1)
#define CNTL_TXENAB		(1<<1)

#define AUX_MU_STAT_REG		0x09
#define STAT_TX_SA		(1<<1)
#define STAT_RX_SA		(1)

#define AUX_MU_BAUD_REG		0x0a

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
static int uart_mu_probe(struct uart_bas *bas);
static void uart_mu_init(struct uart_bas *bas, int, int, int, int);
static void uart_mu_term(struct uart_bas *bas);
static void uart_mu_putc(struct uart_bas *bas, int);
static int uart_mu_rxready(struct uart_bas *bas);
static int uart_mu_getc(struct uart_bas *bas, struct mtx *);

static struct uart_ops uart_mu_ops = {
	.probe = uart_mu_probe,
	.init = uart_mu_init,
	.term = uart_mu_term,
	.putc = uart_mu_putc,
	.rxready = uart_mu_rxready,
	.getc = uart_mu_getc,
};

static int
uart_mu_probe(struct uart_bas *bas)
{
	
	return (0);
}

/* 
 * According to the docs, the cpu clock is locked to 250Mhz when
 * the micro-uart is used 
 */
#define CPU_CLOCK	250000000

static void
uart_mu_param(struct uart_bas *bas, int baudrate, int databits, int stopbits,
    int parity)
{
	uint32_t line;
	uint32_t baud;
	
	/*
	 * Zero all settings to make sure
	 * UART is disabled and not configured
	 */
	line = 0x0;
	__uart_setreg(bas, AUX_MU_CNTL_REG, line);

	/* As I know UART is disabled I can setup the line */
	switch (databits) {
	case 7:
		line |= LCR_WLEN7;
		break;
	case 6:
	case 8:
	default:
		line |= LCR_WLEN8;
		break;
	}

	__uart_setreg(bas, AUX_MU_LCR_REG, line);

	/* See 2.2.1 BCM2835-ARM-Peripherals baudrate */
	if (baudrate != 0) {
		baud = CPU_CLOCK / (8 * baudrate);
		/* XXX	
		 *  baud = cpu_clock() / (8 * baudrate);
		 */
		__uart_setreg(bas, AUX_MU_BAUD_REG, ((uint32_t)(baud & 0xFFFF)));
	}
	
	/* re-enable UART */
	__uart_setreg(bas, AUX_MU_CNTL_REG, CNTL_RXENAB|CNTL_TXENAB);
}

static void
uart_mu_init(struct uart_bas *bas, int baudrate, int databits, int stopbits,
    int parity)
{

	/* Mask all interrupts */
	__uart_setreg(bas, AUX_MU_IER_REG, 0);
	uart_mu_param(bas, baudrate, databits, stopbits, parity);
}

static void
uart_mu_term(struct uart_bas *bas)
{
}

static void
uart_mu_putc(struct uart_bas *bas, int c)
{

	/* Wait when TX FIFO full. Push character otherwise. */
	while ((__uart_getreg(bas, AUX_MU_LSR_REG) & LSR_TXEMPTY) == 0)
		;
	__uart_setreg(bas, AUX_MU_IO_REG, c & 0xff);
}

static int
uart_mu_rxready(struct uart_bas *bas)
{

	return ((__uart_getreg(bas, AUX_MU_LSR_REG) & LSR_RXREADY) != 0);
}

static int
uart_mu_getc(struct uart_bas *bas, struct mtx *hwmtx)
{
	int c;

	while(!uart_mu_rxready(bas))
		;
	c = __uart_getreg(bas, AUX_MU_IO_REG) & 0xff;
	return (c);
}

/*
 * High-level UART interface.
 */
struct uart_mu_softc {
	struct uart_softc	bas;
  	uint16_t		aux_ier; /* Interrupt mask */
};

static int uart_mu_bus_attach(struct uart_softc *);
static int uart_mu_bus_detach(struct uart_softc *);
static int uart_mu_bus_flush(struct uart_softc *, int);
static int uart_mu_bus_getsig(struct uart_softc *);
static int uart_mu_bus_ioctl(struct uart_softc *, int, intptr_t);
static int uart_mu_bus_ipend(struct uart_softc *);
static int uart_mu_bus_param(struct uart_softc *, int, int, int, int);
static int uart_mu_bus_probe(struct uart_softc *);
static int uart_mu_bus_receive(struct uart_softc *);
static int uart_mu_bus_setsig(struct uart_softc *, int);
static int uart_mu_bus_transmit(struct uart_softc *);
static void uart_mu_bus_grab(struct uart_softc *);
static void uart_mu_bus_ungrab(struct uart_softc *);

static kobj_method_t uart_mu_methods[] = {
	KOBJMETHOD(uart_attach,		uart_mu_bus_attach),
	KOBJMETHOD(uart_detach,		uart_mu_bus_detach),
	KOBJMETHOD(uart_flush,		uart_mu_bus_flush),
	KOBJMETHOD(uart_getsig,		uart_mu_bus_getsig),
	KOBJMETHOD(uart_ioctl,		uart_mu_bus_ioctl),
	KOBJMETHOD(uart_ipend,		uart_mu_bus_ipend),
	KOBJMETHOD(uart_param,		uart_mu_bus_param),
	KOBJMETHOD(uart_probe,		uart_mu_bus_probe),
	KOBJMETHOD(uart_receive,	uart_mu_bus_receive),
	KOBJMETHOD(uart_setsig,		uart_mu_bus_setsig),
	KOBJMETHOD(uart_transmit,	uart_mu_bus_transmit),
	KOBJMETHOD(uart_grab,		uart_mu_bus_grab),
	KOBJMETHOD(uart_ungrab,		uart_mu_bus_ungrab),

	{ 0, 0 }
};

static struct uart_class uart_mu_class = {
	"aux-uart",
	uart_mu_methods,
	sizeof(struct uart_mu_softc),
	.uc_ops = &uart_mu_ops,
	.uc_range = 0x48,
	.uc_rclk = 0,
	.uc_rshift = 2
};

#ifdef FDT
static struct ofw_compat_data fdt_compat_data[] = {
	{"brcm,bcm2835-aux-uart" , (uintptr_t)&uart_mu_class},
	{NULL,			   (uintptr_t)NULL},
};
UART_FDT_CLASS_AND_DEVICE(fdt_compat_data);
#endif

static int
uart_mu_bus_attach(struct uart_softc *sc)
{
	struct uart_mu_softc *psc;
	struct uart_bas *bas;

	psc = (struct uart_mu_softc *)sc;
	bas = &sc->sc_bas;
	/* Clear interrupts */
	__uart_setreg(bas, AUX_MU_IIR_REG, IIR_CLEAR);
	/* Enable interrupts */
	psc->aux_ier = (IER_RXENABLE|IER_TXENABLE|IER_REQUIRED);
	__uart_setreg(bas, AUX_MU_IER_REG, psc->aux_ier);
	sc->sc_txbusy = 0;
	
	return (0);
}

static int
uart_mu_bus_detach(struct uart_softc *sc)
{

	return (0);
}

static int
uart_mu_bus_flush(struct uart_softc *sc, int what)
{

	return (0);
}

static int
uart_mu_bus_getsig(struct uart_softc *sc)
{

	return (0);
}

static int
uart_mu_bus_ioctl(struct uart_softc *sc, int request, intptr_t data)
{
	struct uart_bas *bas;
	int error;

	bas = &sc->sc_bas;
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
uart_mu_bus_ipend(struct uart_softc *sc)
{
	struct uart_mu_softc *psc;
	struct uart_bas *bas;
	uint32_t ints;
	int ipend;
	
	psc = (struct uart_mu_softc *)sc;
	bas = &sc->sc_bas;

	uart_lock(sc->sc_hwmtx);
	ints = __uart_getreg(bas, AUX_MU_IIR_REG);
	ipend = 0;

	/*
	 * According to docs only one of IIR_RXREADY
	 * or IIR_TXREADY are valid eg. Only one or the other.
	 */
	if (ints & IIR_RXREADY) {
		ipend |= SER_INT_RXREADY;
	} else if (ints & IIR_TXREADY) {
		if (__uart_getreg(bas, AUX_MU_LSR_REG) & LSR_TXIDLE) {
			if (sc->sc_txbusy)
				ipend |= SER_INT_TXIDLE;

			/* Disable TX interrupt */
			__uart_setreg(bas, AUX_MU_IER_REG,
				      psc->aux_ier & ~IER_TXENABLE);
		}
	}

	uart_unlock(sc->sc_hwmtx);

	return (ipend);
}

static int
uart_mu_bus_param(struct uart_softc *sc, int baudrate, int databits,
    int stopbits, int parity)
{

	uart_lock(sc->sc_hwmtx);
	uart_mu_param(&sc->sc_bas, baudrate, databits, stopbits, parity);
	uart_unlock(sc->sc_hwmtx);

	return (0);
}

static int
uart_mu_bus_probe(struct uart_softc *sc)
{

	/* MU always has 8 byte deep fifo */
	sc->sc_rxfifosz = 8;
	sc->sc_txfifosz = 8;
	device_set_desc(sc->sc_dev, "BCM2835 Mini-UART");

	return (0);
}

static int
uart_mu_bus_receive(struct uart_softc *sc)
{
	struct uart_mu_softc *psc;
	struct uart_bas *bas;
	uint32_t lsr, xc;
	int rx;
	
	bas = &sc->sc_bas;
	uart_lock(sc->sc_hwmtx);
	psc = (struct uart_mu_softc *)sc;
	
	lsr = __uart_getreg(bas, AUX_MU_LSR_REG);
	while (lsr & LSR_RXREADY) {
		xc = __uart_getreg(bas, AUX_MU_IO_REG);
		rx = xc & 0xff;
		if (uart_rx_full(sc)) {
			sc->sc_rxbuf[sc->sc_rxput] = UART_STAT_OVERRUN;
			break;
		}
		uart_rx_put(sc, rx);
		lsr = __uart_getreg(bas, AUX_MU_LSR_REG);
	}
	uart_unlock(sc->sc_hwmtx);

	return (0);
}

static int
uart_mu_bus_setsig(struct uart_softc *sc, int sig)
{

	return (0);
}

static int
uart_mu_bus_transmit(struct uart_softc *sc)
{
	struct uart_mu_softc *psc;
	struct uart_bas *bas;
	int i;
	
	psc = (struct uart_mu_softc *)sc;
	bas = &sc->sc_bas;
	uart_lock(sc->sc_hwmtx);

  	for (i = 0; i < sc->sc_txdatasz; i++) {
		__uart_setreg(bas, AUX_MU_IO_REG, sc->sc_txbuf[i] & 0xff);
		uart_barrier(bas);
	}

	/* Mark busy and enable TX interrupt */
	sc->sc_txbusy = 1;
	__uart_setreg(bas, AUX_MU_IER_REG, psc->aux_ier);
		
	uart_unlock(sc->sc_hwmtx);

	return (0);
}

static void
uart_mu_bus_grab(struct uart_softc *sc)
{
	struct uart_mu_softc *psc;
	struct uart_bas *bas;

	psc = (struct uart_mu_softc *)sc;
	bas = &sc->sc_bas;

	/* Disable interrupts on switch to polling */
	uart_lock(sc->sc_hwmtx);
	__uart_setreg(bas, AUX_MU_IER_REG, psc->aux_ier &~IER_MASK_ALL);
	uart_unlock(sc->sc_hwmtx);
}

static void
uart_mu_bus_ungrab(struct uart_softc *sc)
{
	struct uart_mu_softc *psc;
	struct uart_bas *bas;

	psc = (struct uart_mu_softc *)sc;
	bas = &sc->sc_bas;

	/* Switch to using interrupts while not grabbed */
	uart_lock(sc->sc_hwmtx);
	__uart_setreg(bas, AUX_MU_CNTL_REG, CNTL_RXENAB|CNTL_TXENAB);
	__uart_setreg(bas, AUX_MU_IER_REG, psc->aux_ier);
	uart_unlock(sc->sc_hwmtx);
}
