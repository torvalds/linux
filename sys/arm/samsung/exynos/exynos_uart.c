/*
 * Copyright (c) 2003 Marcel Moolenaar
 * Copyright (c) 2007-2009 Andrew Turner
 * Copyright (c) 2013 Ruslan Bukin <br@bsdpad.com>
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
#include <sys/cons.h>
#include <sys/tty.h>
#include <sys/rman.h>
#include <machine/bus.h>
#include <machine/intr.h>

#include <dev/uart/uart.h>
#include <dev/uart/uart_cpu.h>
#include <dev/uart/uart_cpu_fdt.h>
#include <dev/uart/uart_bus.h>

#include <arm/samsung/exynos/exynos_uart.h>

#include "uart_if.h"

#define	DEF_CLK		100000000

static int sscomspeed(long, long);
static int exynos4210_uart_param(struct uart_bas *, int, int, int, int);

/*
 * Low-level UART interface.
 */
static int exynos4210_probe(struct uart_bas *bas);
static void exynos4210_init(struct uart_bas *bas, int, int, int, int);
static void exynos4210_term(struct uart_bas *bas);
static void exynos4210_putc(struct uart_bas *bas, int);
static int exynos4210_rxready(struct uart_bas *bas);
static int exynos4210_getc(struct uart_bas *bas, struct mtx *mtx);

extern SLIST_HEAD(uart_devinfo_list, uart_devinfo) uart_sysdevs;

static int
sscomspeed(long speed, long frequency)
{
	int x;

	if (speed <= 0 || frequency <= 0)
		return (-1);
	x = (frequency / 16) / speed;
	return (x-1);
}

static int
exynos4210_uart_param(struct uart_bas *bas, int baudrate, int databits,
    int stopbits, int parity)
{
	int brd, ulcon;

	ulcon = 0;

	switch(databits) {
	case 5:
		ulcon |= ULCON_LENGTH_5;
		break;
	case 6:
		ulcon |= ULCON_LENGTH_6;
		break;
	case 7:
		ulcon |= ULCON_LENGTH_7;
		break;
	case 8:
		ulcon |= ULCON_LENGTH_8;
		break;
	default:
		return (EINVAL);
	}

	switch (parity) {
	case UART_PARITY_NONE:
		ulcon |= ULCON_PARITY_NONE;
		break;
	case UART_PARITY_ODD:
		ulcon |= ULCON_PARITY_ODD;
		break;
	case UART_PARITY_EVEN:
		ulcon |= ULCON_PARITY_EVEN;
		break;
	case UART_PARITY_MARK:
	case UART_PARITY_SPACE:
	default:
		return (EINVAL);
	}

	if (stopbits == 2)
		ulcon |= ULCON_STOP;

	uart_setreg(bas, SSCOM_ULCON, ulcon);

	brd = sscomspeed(baudrate, bas->rclk);
	uart_setreg(bas, SSCOM_UBRDIV, brd);

	return (0);
}

struct uart_ops uart_exynos4210_ops = {
	.probe = exynos4210_probe,
	.init = exynos4210_init,
	.term = exynos4210_term,
	.putc = exynos4210_putc,
	.rxready = exynos4210_rxready,
	.getc = exynos4210_getc,
};

static int
exynos4210_probe(struct uart_bas *bas)
{

	return (0);
}

static void
exynos4210_init(struct uart_bas *bas, int baudrate, int databits, int stopbits,
    int parity)
{

	if (bas->rclk == 0)
		bas->rclk = DEF_CLK;

	KASSERT(bas->rclk != 0, ("exynos4210_init: Invalid rclk"));

	uart_setreg(bas, SSCOM_UCON, 0);
	uart_setreg(bas, SSCOM_UFCON,
	    UFCON_TXTRIGGER_8 | UFCON_RXTRIGGER_8 |
	    UFCON_TXFIFO_RESET | UFCON_RXFIFO_RESET |
	    UFCON_FIFO_ENABLE);
	exynos4210_uart_param(bas, baudrate, databits, stopbits, parity);

	/* Enable UART. */
	uart_setreg(bas, SSCOM_UCON, UCON_TXMODE_INT | UCON_RXMODE_INT |
	    UCON_TOINT);
	uart_setreg(bas, SSCOM_UMCON, UMCON_RTS);
}

static void
exynos4210_term(struct uart_bas *bas)
{
	/* XXX */
}

static void
exynos4210_putc(struct uart_bas *bas, int c)
{

	while ((bus_space_read_4(bas->bst, bas->bsh, SSCOM_UFSTAT) &
		UFSTAT_TXFULL) == UFSTAT_TXFULL)
		continue;

	uart_setreg(bas, SSCOM_UTXH, c);
}

static int
exynos4210_rxready(struct uart_bas *bas)
{

	return ((uart_getreg(bas, SSCOM_UTRSTAT) & UTRSTAT_RXREADY) ==
	    UTRSTAT_RXREADY);
}

static int
exynos4210_getc(struct uart_bas *bas, struct mtx *mtx)
{
	int utrstat;

	utrstat = bus_space_read_1(bas->bst, bas->bsh, SSCOM_UTRSTAT);
	while (!(utrstat & UTRSTAT_RXREADY)) {
		utrstat = bus_space_read_1(bas->bst, bas->bsh, SSCOM_UTRSTAT);
		continue;
	}

	return (bus_space_read_1(bas->bst, bas->bsh, SSCOM_URXH));
}

static int exynos4210_bus_probe(struct uart_softc *sc);
static int exynos4210_bus_attach(struct uart_softc *sc);
static int exynos4210_bus_flush(struct uart_softc *, int);
static int exynos4210_bus_getsig(struct uart_softc *);
static int exynos4210_bus_ioctl(struct uart_softc *, int, intptr_t);
static int exynos4210_bus_ipend(struct uart_softc *);
static int exynos4210_bus_param(struct uart_softc *, int, int, int, int);
static int exynos4210_bus_receive(struct uart_softc *);
static int exynos4210_bus_setsig(struct uart_softc *, int);
static int exynos4210_bus_transmit(struct uart_softc *);

static kobj_method_t exynos4210_methods[] = {
	KOBJMETHOD(uart_probe,		exynos4210_bus_probe),
	KOBJMETHOD(uart_attach, 	exynos4210_bus_attach),
	KOBJMETHOD(uart_flush,		exynos4210_bus_flush),
	KOBJMETHOD(uart_getsig,		exynos4210_bus_getsig),
	KOBJMETHOD(uart_ioctl,		exynos4210_bus_ioctl),
	KOBJMETHOD(uart_ipend,		exynos4210_bus_ipend),
	KOBJMETHOD(uart_param,		exynos4210_bus_param),
	KOBJMETHOD(uart_receive,	exynos4210_bus_receive),
	KOBJMETHOD(uart_setsig,		exynos4210_bus_setsig),
	KOBJMETHOD(uart_transmit,	exynos4210_bus_transmit),

	{0, 0 }
};

int
exynos4210_bus_probe(struct uart_softc *sc)
{

	sc->sc_txfifosz = 16;
	sc->sc_rxfifosz = 16;

	return (0);
}

static int
exynos4210_bus_attach(struct uart_softc *sc)
{

	sc->sc_hwiflow = 0;
	sc->sc_hwoflow = 0;

	return (0);
}

static int
exynos4210_bus_transmit(struct uart_softc *sc)
{
	int i;
	int reg;

	uart_lock(sc->sc_hwmtx);

	for (i = 0; i < sc->sc_txdatasz; i++) {
		exynos4210_putc(&sc->sc_bas, sc->sc_txbuf[i]);
		uart_barrier(&sc->sc_bas);
	}

	sc->sc_txbusy = 1;

	uart_unlock(sc->sc_hwmtx);

	/* unmask TX interrupt */
	reg = bus_space_read_4(sc->sc_bas.bst, sc->sc_bas.bsh, SSCOM_UINTM);
	reg &= ~(1 << 2);
	bus_space_write_4(sc->sc_bas.bst, sc->sc_bas.bsh, SSCOM_UINTM, reg);

	return (0);
}

static int
exynos4210_bus_setsig(struct uart_softc *sc, int sig)
{

	return (0);
}

static int
exynos4210_bus_receive(struct uart_softc *sc)
{
	struct uart_bas *bas;

	bas = &sc->sc_bas;
	while (bus_space_read_4(bas->bst, bas->bsh,
		SSCOM_UFSTAT) & UFSTAT_RXCOUNT)
		uart_rx_put(sc, uart_getreg(&sc->sc_bas, SSCOM_URXH));

	return (0);
}

static int
exynos4210_bus_param(struct uart_softc *sc, int baudrate, int databits,
    int stopbits, int parity)
{
	int error;

	if (sc->sc_bas.rclk == 0)
		sc->sc_bas.rclk = DEF_CLK;

	KASSERT(sc->sc_bas.rclk != 0, ("exynos4210_init: Invalid rclk"));

	uart_lock(sc->sc_hwmtx);
	error = exynos4210_uart_param(&sc->sc_bas, baudrate, databits, stopbits,
	    parity);
	uart_unlock(sc->sc_hwmtx);

	return (error);
}

static int
exynos4210_bus_ipend(struct uart_softc *sc)
{
	uint32_t ints;
	uint32_t txempty, rxready;
	int reg;
	int ipend;

	uart_lock(sc->sc_hwmtx);
	ints = bus_space_read_4(sc->sc_bas.bst, sc->sc_bas.bsh, SSCOM_UINTP);
	bus_space_write_4(sc->sc_bas.bst, sc->sc_bas.bsh, SSCOM_UINTP, ints);

	txempty = (1 << 2);
	rxready = (1 << 0);

	ipend = 0;
	if ((ints & txempty) > 0) {
		if (sc->sc_txbusy != 0)
			ipend |= SER_INT_TXIDLE;

		/* mask TX interrupt */
		reg = bus_space_read_4(sc->sc_bas.bst, sc->sc_bas.bsh,
		    SSCOM_UINTM);
		reg |= (1 << 2);
		bus_space_write_4(sc->sc_bas.bst, sc->sc_bas.bsh,
		    SSCOM_UINTM, reg);
	}

	if ((ints & rxready) > 0) {
		ipend |= SER_INT_RXREADY;
	}

	uart_unlock(sc->sc_hwmtx);
	return (ipend);
}

static int
exynos4210_bus_flush(struct uart_softc *sc, int what)
{

	return (0);
}

static int
exynos4210_bus_getsig(struct uart_softc *sc)
{

	return (0);
}

static int
exynos4210_bus_ioctl(struct uart_softc *sc, int request, intptr_t data)
{

	return (EINVAL);
}

static struct uart_class uart_exynos4210_class = {
	"exynos4210 class",
	exynos4210_methods,
	1,
	.uc_ops = &uart_exynos4210_ops,
	.uc_range = 8,
	.uc_rclk = 0,
	.uc_rshift = 0
};

static struct ofw_compat_data compat_data[] = {
	{"exynos",		(uintptr_t)&uart_exynos4210_class},
	{NULL,			(uintptr_t)NULL},
};
UART_FDT_CLASS_AND_DEVICE(compat_data);
