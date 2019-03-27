/*-
 * Copyright (c) 2017 Semihalf.
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

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/bus.h>
#include <sys/conf.h>
#include <sys/kernel.h>
#include <sys/sysctl.h>
#include <sys/systm.h>

#include <machine/bus.h>

#include <dev/ofw/ofw_bus_subr.h>
#include <dev/uart/uart.h>
#include <dev/uart/uart_bus.h>
#include <dev/uart/uart_cpu.h>
#include <dev/uart/uart_cpu_fdt.h>

#include "uart_if.h"

#define	UART_RBR		0x00		/* Receiver Buffer */
#define	RBR_BRK_DET		(1 << 15)	/* Break Detect */
#define	RBR_FRM_ERR_DET		(1 << 14)	/* Frame Error Detect */
#define	RBR_PAR_ERR_DET		(1 << 13)	/* Parity Error Detect */
#define	RBR_OVR_ERR_DET		(1 << 12)	/* Overrun Error */

#define	UART_TSH		0x04		/* Transmitter Holding Register */

#define	UART_CTRL		0x08		/* Control Register */
#define	CTRL_SOFT_RST		(1 << 31)	/* Soft Reset */
#define	CTRL_TX_FIFO_RST	(1 << 15)	/* TX FIFO Reset */
#define	CTRL_RX_FIFO_RST	(1 << 14)	/* RX FIFO Reset */
#define	CTRL_ST_MIRR_EN		(1 << 13)	/* Status Mirror Enable */
#define	CTRL_LPBK_EN		(1 << 12)	/* Loopback Mode Enable */
#define	CTRL_SND_BRK_SEQ	(1 << 11)	/* Send Break Sequence */
#define	CTRL_PAR_EN		(1 << 10)	/* Parity Enable */
#define	CTRL_TWO_STOP		(1 << 9)	/* Two Stop Bits */
#define	CTRL_TX_HALF_INT	(1 << 8)	/* TX Half-Full Interrupt Enable */
#define	CTRL_RX_HALF_INT	(1 << 7)	/* RX Half-Full Interrupt Enable */
#define	CTRL_TX_EMPT_INT	(1 << 6)	/* TX Empty Interrupt Enable */
#define	CTRL_TX_RDY_INT		(1 << 5)	/* TX Ready Interrupt Enable */
#define	CTRL_RX_RDY_INT		(1 << 4)	/* RX Ready Interrupt Enable */
#define	CTRL_BRK_DET_INT	(1 << 3)	/* Break Detect Interrupt Enable */
#define	CTRL_FRM_ERR_INT	(1 << 2)	/* Frame Error Interrupt Enable */
#define	CTRL_PAR_ERR_INT	(1 << 1)	/* Parity Error Interrupt Enable */
#define	CTRL_OVR_ERR_INT	(1 << 0)	/* Overrun Error Interrupt Enable */
#define	CTRL_INTR_MASK		0x1ff
#define	CTRL_TX_IDLE_INT	CTRL_TX_RDY_INT
#define	CTRL_IPEND_MASK		(CTRL_OVR_ERR_INT | CTRL_BRK_DET_INT | \
    CTRL_RX_RDY_INT)

#define	UART_STAT		0x0c		/* Status Register */
#define	STAT_TX_FIFO_EMPT	(1 << 13)	/* TX FIFO Empty */
#define	STAT_RX_FIFO_EMPT	(1 << 12)	/* RX FIFO Empty */
#define	STAT_TX_FIFO_FULL	(1 << 11)	/* TX FIFO Full */
#define	STAT_TX_FIFO_HALF	(1 << 10)	/* TX FIFO Half Full */
#define	STAT_RX_TOGL		(1 << 9)	/* RX Toogled */
#define	STAT_RX_FIFO_FULL	(1 << 8)	/* RX FIFO Full */
#define	STAT_RX_FIFO_HALF	(1 << 7)	/* RX FIFO Half Full */
#define	STAT_TX_EMPT		(1 << 6)	/* TX Empty */
#define	STAT_TX_RDY		(1 << 5)	/* TX Ready */
#define	STAT_RX_RDY		(1 << 4)	/* RX Ready */
#define	STAT_BRK_DET		(1 << 3)	/* Break Detect */
#define	STAT_FRM_ERR		(1 << 2)	/* Frame Error */
#define	STAT_PAR_ERR		(1 << 1)	/* Parity Error */
#define	STAT_OVR_ERR		(1 << 0)	/* Overrun Error */
#define	STAT_TX_IDLE		STAT_TX_RDY
#define	STAT_TRANS_MASK		(STAT_OVR_ERR | STAT_BRK_DET | STAT_RX_RDY)

#define	UART_CCR		0x10		/* Clock Control Register */
#define	CCR_BAUDRATE_DIV	0x3ff		/* Baud Rate Divisor */

#define	DEFAULT_RCLK		25804800
#define	ONE_FRAME_TIME		87

#define	stat_ipend_trans(i) (			\
	    (i & STAT_OVR_ERR) << 16 |		\
	    (i & STAT_BRK_DET) << 14 |		\
	    (i & STAT_RX_RDY) << 14)

/*
 * For debugging purposes
 */
#if 0
#ifdef EARLY_PRINTF
#if defined(SOCDEV_PA) && defined(SOCDEV_VA)
#define	UART_REG_OFFSET 0x12000
static void
uart_mvebu_early_putc(int c)
{
	volatile uint32_t *tsh;
	volatile uint32_t *stat;

	tsh = (uint32_t *)(SOCDEV_VA + UART_REG_OFFSET + UART_TSH);
	stat = (uint32_t *)(SOCDEV_VA + UART_REG_OFFSET + UART_STAT);

	while(!(*stat & STAT_TX_RDY))
		;

	*tsh = c & 0xff;
}

early_putc_t *early_putc = uart_mvebu_early_putc;
#endif
#endif
#endif

/*
 * Low-level UART interface.
 */
static int uart_mvebu_probe(struct uart_bas *);
static void uart_mvebu_init(struct uart_bas *, int, int, int, int);
static void uart_mvebu_putc(struct uart_bas *, int);
static int uart_mvebu_rxready(struct uart_bas *);
static int uart_mvebu_getc(struct uart_bas *, struct mtx *);

static struct uart_ops uart_mvebu_ops = {
	.probe = uart_mvebu_probe,
	.init = uart_mvebu_init,
	.term = NULL,
	.putc = uart_mvebu_putc,
	.rxready = uart_mvebu_rxready,
	.getc = uart_mvebu_getc,
};

static int
uart_mvebu_probe(struct uart_bas *bas)
{

	return (0);
}

static int
uart_mvebu_divisor(int rclk, int baudrate)
{
	int divisor;

	if (baudrate == 0)
		return (0);

	divisor = (rclk >> 4) / baudrate;
	if (divisor <= 1 || divisor >= 1024)
		return (0);

	return (divisor);
}

static int
uart_mvebu_param(struct uart_bas *bas, int baudrate, int databits, int stopbits,
    int parity)
{
	uint32_t ctrl = 0;
	uint32_t ccr;
	int divisor, ret = 0;

	/* Reset UART */
	ctrl = uart_getreg(bas, UART_CTRL);
	uart_setreg(bas, UART_CTRL, ctrl | CTRL_TX_FIFO_RST | CTRL_RX_FIFO_RST |
	    CTRL_LPBK_EN);
	uart_barrier(bas);

	switch (stopbits) {
	case 2:
		ctrl |= CTRL_TWO_STOP;
		break;
	case 1:
	default:
		ctrl &=~ CTRL_TWO_STOP;
	}

	switch (parity) {
	case 3: /* Even parity bit */
		ctrl |= CTRL_PAR_EN;
		break;
	default:
		ctrl &=~ CTRL_PAR_EN;
	}

	/* Set baudrate. */
	if (baudrate > 0) {
		divisor = uart_mvebu_divisor(bas->rclk, baudrate);
		if (divisor == 0) {
			ret = EINVAL;
		} else {
			ccr = uart_getreg(bas, UART_CCR);
			ccr &=~CCR_BAUDRATE_DIV;

			uart_setreg(bas, UART_CCR, ccr | divisor);
			uart_barrier(bas);
		}
	}

	/* Set mirroring of status bits */
	ctrl |= CTRL_ST_MIRR_EN;

	uart_setreg(bas, UART_CTRL, ctrl);
	uart_barrier(bas);

	return (ret);
}

static void
uart_mvebu_init(struct uart_bas *bas, int baudrate, int databits, int stopbits,
    int parity)
{
	/* Set default frequency */
	bas->rclk = DEFAULT_RCLK;

	/* Mask interrupts */
	uart_setreg(bas, UART_CTRL, uart_getreg(bas, UART_CTRL) &
	    ~CTRL_INTR_MASK);
	uart_barrier(bas);

	uart_mvebu_param(bas, baudrate, databits, stopbits, parity);
}

static void
uart_mvebu_putc(struct uart_bas *bas, int c)
{
	while (uart_getreg(bas, UART_STAT) & STAT_TX_FIFO_FULL)
		;
	uart_setreg(bas, UART_TSH, c & 0xff);
}

static int
uart_mvebu_rxready(struct uart_bas *bas)
{
	if (uart_getreg(bas, UART_STAT) & STAT_RX_RDY)
		return 1;
	return 0;
}

static int
uart_mvebu_getc(struct uart_bas *bas, struct mtx *hwmtx)
{
	int c;

	uart_lock(hwmtx);
	while (!(uart_getreg(bas, UART_STAT) & STAT_RX_RDY))
		;

	c = uart_getreg(bas, UART_RBR) & 0xff;
	uart_unlock(hwmtx);

	return c;
}

/*
 * UART driver methods implementation.
 */
struct uart_mvebu_softc {
	struct uart_softc base;
	uint16_t intrm;
};

static int uart_mvebu_bus_attach(struct uart_softc *);
static int uart_mvebu_bus_detach(struct uart_softc *);
static int uart_mvebu_bus_flush(struct uart_softc *, int);
static int uart_mvebu_bus_getsig(struct uart_softc *);
static int uart_mvebu_bus_ioctl(struct uart_softc *, int, intptr_t);
static int uart_mvebu_bus_ipend(struct uart_softc *);
static int uart_mvebu_bus_param(struct uart_softc *, int, int, int, int);
static int uart_mvebu_bus_probe(struct uart_softc *);
static int uart_mvebu_bus_receive(struct uart_softc *);
static int uart_mvebu_bus_setsig(struct uart_softc *, int);
static int uart_mvebu_bus_transmit(struct uart_softc *);
static void uart_mvebu_bus_grab(struct uart_softc *);
static void uart_mvebu_bus_ungrab(struct uart_softc *);

static kobj_method_t uart_mvebu_methods[] = {
	KOBJMETHOD(uart_attach,		uart_mvebu_bus_attach),
	KOBJMETHOD(uart_detach,		uart_mvebu_bus_detach),
	KOBJMETHOD(uart_flush,		uart_mvebu_bus_flush),
	KOBJMETHOD(uart_getsig,		uart_mvebu_bus_getsig),
	KOBJMETHOD(uart_ioctl,		uart_mvebu_bus_ioctl),
	KOBJMETHOD(uart_ipend,		uart_mvebu_bus_ipend),
	KOBJMETHOD(uart_param,		uart_mvebu_bus_param),
	KOBJMETHOD(uart_probe,		uart_mvebu_bus_probe),
	KOBJMETHOD(uart_receive,	uart_mvebu_bus_receive),
	KOBJMETHOD(uart_setsig,		uart_mvebu_bus_setsig),
	KOBJMETHOD(uart_transmit,	uart_mvebu_bus_transmit),
	KOBJMETHOD(uart_grab,		uart_mvebu_bus_grab),
	KOBJMETHOD(uart_ungrab,		uart_mvebu_bus_ungrab),
	{ 0, 0 }
};

struct uart_class uart_mvebu_class = {
	"mvebu-uart",
	uart_mvebu_methods,
	sizeof(struct uart_mvebu_softc),
	.uc_ops = &uart_mvebu_ops,
	.uc_range = 0x14,
	.uc_rclk = DEFAULT_RCLK,
	.uc_rshift = 0,
	.uc_riowidth = 4
};

static struct ofw_compat_data compat_data[] = {
	{"marvell,armada-3700-uart",	(uintptr_t)&uart_mvebu_class},
	{NULL,				(uintptr_t)NULL},
};
UART_FDT_CLASS_AND_DEVICE(compat_data);

static int
uart_mvebu_bus_attach(struct uart_softc *sc)
{
	struct uart_bas *bas;
	int ctrl;

	bas = &sc->sc_bas;
	uart_lock(sc->sc_hwmtx);

	ctrl = uart_getreg(bas, UART_CTRL);

	/* Enable interrupts */
	ctrl &=~ CTRL_INTR_MASK;
	ctrl |= CTRL_IPEND_MASK;

	/* Set interrupts */
	uart_setreg(bas, UART_CTRL, ctrl);
	uart_barrier(bas);

	uart_unlock(sc->sc_hwmtx);

	return (0);
}

static int
uart_mvebu_bus_detach(struct uart_softc *sc)
{

	return (0);
}

static int
uart_mvebu_bus_flush(struct uart_softc *sc, int what)
{
	struct uart_bas *bas;
	int ctrl, ret = 0;

	bas = &sc->sc_bas;
	uart_lock(sc->sc_hwmtx);
	ctrl = uart_getreg(bas, UART_CTRL);

	switch (what) {
	case UART_FLUSH_RECEIVER:
		uart_setreg(bas, UART_CTRL, ctrl | CTRL_RX_FIFO_RST);
		uart_barrier(bas);
		break;

	case UART_FLUSH_TRANSMITTER:
		uart_setreg(bas, UART_CTRL, ctrl | CTRL_TX_FIFO_RST);
		uart_barrier(bas);
		break;

	default:
		ret = EINVAL;
		break;
	}

	/* Back to normal operation */
	if (!ret) {
		uart_setreg(bas, UART_CTRL, ctrl);
		uart_barrier(bas);
	}

	uart_unlock(sc->sc_hwmtx);
	return (ret);
}

static int
uart_mvebu_bus_getsig(struct uart_softc *sc)
{

	return (0);
}

static int
uart_mvebu_bus_ioctl(struct uart_softc *sc, int request, intptr_t data)
{
	struct uart_bas *bas;
	int ctrl, ret = 0;
	int divisor, baudrate;

	bas = &sc->sc_bas;
	uart_lock(sc->sc_hwmtx);
	switch (request) {
	case UART_IOCTL_BREAK:
		ctrl = uart_getreg(bas, UART_CTRL);
		if (data)
			ctrl |= CTRL_SND_BRK_SEQ;
		else
			ctrl &=~ CTRL_SND_BRK_SEQ;
		uart_setreg(bas, UART_CTRL, ctrl);
		uart_barrier(bas);
		break;

	case UART_IOCTL_BAUD:
		divisor = uart_getreg(bas, UART_CCR) & CCR_BAUDRATE_DIV;
		baudrate = bas->rclk/(divisor * 16);
		*(int *)data = baudrate;
		break;

	default:
		ret = ENOTTY;
		break;
	}
	uart_unlock(sc->sc_hwmtx);

	return (ret);
}

static int
uart_mvebu_bus_ipend(struct uart_softc *sc)
{
	struct uart_bas *bas;
	int ipend, ctrl, ret = 0;

	bas = &sc->sc_bas;
	uart_lock(sc->sc_hwmtx);
	ipend = uart_getreg(bas, UART_STAT);
	ctrl = uart_getreg(bas, UART_CTRL);

	if (((ipend & STAT_TX_IDLE) == STAT_TX_IDLE) &&
	    (ctrl & CTRL_TX_IDLE_INT) == CTRL_TX_IDLE_INT) {
		/* Disable TX IDLE Interrupt generation */
		uart_setreg(bas, UART_CTRL, ctrl & ~CTRL_TX_IDLE_INT);
		uart_barrier(bas);

		/* SER_INT_TXIDLE means empty TX FIFO. Wait until it cleans */
		while(!(uart_getreg(bas, UART_STAT) & STAT_TX_FIFO_EMPT))
			DELAY(ONE_FRAME_TIME/2);

		ret |= SER_INT_TXIDLE;
	}

	ret |= stat_ipend_trans(ipend & STAT_TRANS_MASK);

	uart_unlock(sc->sc_hwmtx);

	return (ret);
}

static int
uart_mvebu_bus_param(struct uart_softc *sc, int baudrate, int databits,
    int stopbits, int parity)
{
	int ret;

	uart_lock(sc->sc_hwmtx);
	ret = uart_mvebu_param(&sc->sc_bas, baudrate, databits, stopbits, parity);
	uart_unlock(sc->sc_hwmtx);

	return (ret);
}

static int
uart_mvebu_bus_probe(struct uart_softc *sc)
{
	if (!ofw_bus_status_okay(sc->sc_dev))
		return (ENXIO);

	if (!ofw_bus_search_compatible(sc->sc_dev, compat_data)->ocd_data)
		return (ENXIO);

	device_set_desc(sc->sc_dev, "Marvell Armada 3700 UART");

	sc->sc_txfifosz = 32;
	sc->sc_rxfifosz = 64;
	sc->sc_hwiflow = 0;
	sc->sc_hwoflow = 0;

	return (0);
}

int
uart_mvebu_bus_receive(struct uart_softc *sc)
{
	struct uart_bas *bas;
	uint32_t xc;
	int rx, er;

	bas = &sc->sc_bas;
	uart_lock(sc->sc_hwmtx);

	while (!(uart_getreg(bas, UART_STAT) & STAT_RX_FIFO_EMPT)) {
		if (uart_rx_full(sc)) {
			sc->sc_rxbuf[sc->sc_rxput] = UART_STAT_OVERRUN;
			break;
		}

		xc = uart_getreg(bas, UART_RBR);
		rx = xc & 0xff;
		er = xc & 0xf000;
		/*
		 * Formula which translates marvell error bits
		 * Only valid when CTRL_ST_MIRR_EN is set
		 */
		er = (er & RBR_BRK_DET) >> 7 |
		    (er & RBR_FRM_ERR_DET) >> 5 |
		    (er & RBR_PAR_ERR_DET) >> 2 |
		    (er & RBR_OVR_ERR_DET) >> 2;

		uart_rx_put(sc, rx | er);
		uart_barrier(bas);
	}
	/*
	 * uart_if.m says that receive interrupt
	 * should be cleared, so we need to reset
	 * RX FIFO
	 */

	if (!(uart_getreg(bas, UART_STAT) & STAT_RX_FIFO_EMPT)) {
		uart_mvebu_bus_flush(sc, UART_FLUSH_RECEIVER);
	}

	uart_unlock(sc->sc_hwmtx);
	return (0);
}

static int
uart_mvebu_bus_setsig(struct uart_softc *sc, int sig)
{
	/* Not supported by hardware */
	return (0);
}

int
uart_mvebu_bus_transmit(struct uart_softc *sc)
{
	struct uart_bas *bas;
	int i, ctrl;

	bas = &sc->sc_bas;
	uart_lock(sc->sc_hwmtx);

	/* Turn off all interrupts during send */
	ctrl = uart_getreg(bas, UART_CTRL);
	uart_setreg(bas, UART_CTRL, ctrl & ~CTRL_INTR_MASK);
	uart_barrier(bas);

	for (i = 0; i < sc->sc_txdatasz; i++) {
		uart_setreg(bas, UART_TSH, sc->sc_txbuf[i] & 0xff);
		uart_barrier(bas);
	}

	/*
	 * Make sure that interrupt is generated
	 * when FIFO can get more data.
	 */
	uart_setreg(bas, UART_CTRL, ctrl | CTRL_TX_IDLE_INT);
	uart_barrier(bas);

	/* Mark busy */
	sc->sc_txbusy = 1;

	uart_unlock(sc->sc_hwmtx);
	return (0);
}

static void
uart_mvebu_bus_grab(struct uart_softc *sc)
{
	struct uart_mvebu_softc *msc = (struct uart_mvebu_softc *)sc;
	struct uart_bas *bas = &sc->sc_bas;
	uint32_t ctrl;

	/* Mask all interrupts */
	uart_lock(sc->sc_hwmtx);
	ctrl = uart_getreg(bas, UART_CTRL);
	msc->intrm = ctrl & CTRL_INTR_MASK;
	uart_setreg(bas, UART_CTRL, ctrl & ~CTRL_INTR_MASK);
	uart_barrier(bas);
	uart_unlock(sc->sc_hwmtx);
}

static void
uart_mvebu_bus_ungrab(struct uart_softc *sc)
{
	struct uart_mvebu_softc *msc = (struct uart_mvebu_softc *)sc;
	struct uart_bas *bas = &sc->sc_bas;
	uint32_t ctrl;

	/* Restore interrupts */
	uart_lock(sc->sc_hwmtx);
	ctrl = uart_getreg(bas, UART_CTRL) & ~CTRL_INTR_MASK;
	uart_setreg(bas, UART_CTRL, ctrl | msc->intrm);
	uart_barrier(bas);
	uart_unlock(sc->sc_hwmtx);
}

