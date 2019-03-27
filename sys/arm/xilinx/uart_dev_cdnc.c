/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2005 M. Warner Losh
 * Copyright (c) 2005 Olivier Houchard
 * Copyright (c) 2012 Thomas Skibo
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
 * THIS SOFTWARE IS PROVIDED BY AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/* A driver for the Cadence AMBA UART as used by the Xilinx Zynq-7000.
 *
 * Reference: Zynq-7000 All Programmable SoC Technical Reference Manual.
 * (v1.4) November 16, 2012.  Xilinx doc UG585.  UART is covered in Ch. 19
 * and register definitions are in appendix B.33.
 */


#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/conf.h>
#include <sys/cons.h>
#include <sys/tty.h>
#include <machine/bus.h>

#include <dev/uart/uart.h>
#include <dev/uart/uart_cpu.h>
#include <dev/uart/uart_cpu_fdt.h>
#include <dev/uart/uart_bus.h>

#include "uart_if.h"

#define	UART_FIFO_SIZE	64

#define	RD4(bas, reg)		\
	bus_space_read_4((bas)->bst, (bas)->bsh, uart_regofs((bas), (reg)))
#define	WR4(bas, reg, value)	\
	bus_space_write_4((bas)->bst, (bas)->bsh, uart_regofs((bas), (reg)), \
			  (value))

/* Register definitions for Cadence UART Controller.
 */
#define CDNC_UART_CTRL_REG	0x00		/* Control Register. */
#define CDNC_UART_CTRL_REG_STOPBRK	(1<<8)
#define CDNC_UART_CTRL_REG_STARTBRK	(1<<7)
#define CDNC_UART_CTRL_REG_TORST	(1<<6)
#define CDNC_UART_CTRL_REG_TX_DIS	(1<<5)
#define CDNC_UART_CTRL_REG_TX_EN	(1<<4)
#define CDNC_UART_CTRL_REG_RX_DIS	(1<<3)
#define CDNC_UART_CTRL_REG_RX_EN	(1<<2)
#define CDNC_UART_CTRL_REG_TXRST	(1<<1)
#define CDNC_UART_CTRL_REG_RXRST	(1<<0)

#define CDNC_UART_MODE_REG	0x04		/* Mode Register. */
#define CDNC_UART_MODE_REG_CHMOD_R_LOOP	(3<<8)	/* [9:8] - channel mode */
#define CDNC_UART_MODE_REG_CHMOD_L_LOOP	(2<<8)
#define CDNC_UART_MODE_REG_CHMOD_AUTECHO (1<<8)
#define CDNC_UART_MODE_REG_STOP2	(2<<6)	/* [7:6] - stop bits */
#define CDNC_UART_MODE_REG_PAR_NONE	(4<<3)	/* [5:3] - parity type */
#define CDNC_UART_MODE_REG_PAR_MARK	(3<<3)
#define CDNC_UART_MODE_REG_PAR_SPACE	(2<<3)
#define CDNC_UART_MODE_REG_PAR_ODD	(1<<3)
#define CDNC_UART_MODE_REG_PAR_EVEN	(0<<3)
#define CDNC_UART_MODE_REG_6BIT		(3<<1)	/* [2:1] - character len */
#define CDNC_UART_MODE_REG_7BIT		(2<<1)
#define CDNC_UART_MODE_REG_8BIT		(0<<1)
#define CDNC_UART_MODE_REG_CLKSEL	(1<<0)

#define CDNC_UART_IEN_REG	0x08		/* Interrupt registers. */
#define CDNC_UART_IDIS_REG	0x0C
#define CDNC_UART_IMASK_REG	0x10
#define CDNC_UART_ISTAT_REG	0x14
#define CDNC_UART_INT_TXOVR		(1<<12)
#define CDNC_UART_INT_TXNRLYFUL		(1<<11)	/* tx "nearly" full */
#define CDNC_UART_INT_TXTRIG		(1<<10)
#define CDNC_UART_INT_DMSI		(1<<9)	/* delta modem status */
#define CDNC_UART_INT_RXTMOUT		(1<<8)
#define CDNC_UART_INT_PARITY		(1<<7)
#define CDNC_UART_INT_FRAMING		(1<<6)
#define CDNC_UART_INT_RXOVR		(1<<5)
#define CDNC_UART_INT_TXFULL		(1<<4)
#define CDNC_UART_INT_TXEMPTY		(1<<3)
#define CDNC_UART_INT_RXFULL		(1<<2)
#define CDNC_UART_INT_RXEMPTY		(1<<1)
#define CDNC_UART_INT_RXTRIG		(1<<0)
#define CDNC_UART_INT_ALL		0x1FFF

#define CDNC_UART_BAUDGEN_REG	0x18
#define CDNC_UART_RX_TIMEO_REG	0x1C
#define CDNC_UART_RX_WATER_REG	0x20

#define CDNC_UART_MODEM_CTRL_REG 0x24
#define CDNC_UART_MODEM_CTRL_REG_FCM	(1<<5)	/* automatic flow control */
#define CDNC_UART_MODEM_CTRL_REG_RTS	(1<<1)
#define CDNC_UART_MODEM_CTRL_REG_DTR	(1<<0)

#define CDNC_UART_MODEM_STAT_REG 0x28
#define CDNC_UART_MODEM_STAT_REG_FCMS	(1<<8)	/* flow control mode (rw) */
#define CDNC_UART_MODEM_STAT_REG_DCD	(1<<7)
#define CDNC_UART_MODEM_STAT_REG_RI	(1<<6)
#define CDNC_UART_MODEM_STAT_REG_DSR	(1<<5)
#define CDNC_UART_MODEM_STAT_REG_CTS	(1<<4)
#define CDNC_UART_MODEM_STAT_REG_DDCD	(1<<3)	/* change in DCD (w1tc) */
#define CDNC_UART_MODEM_STAT_REG_TERI	(1<<2)	/* trail edge ring (w1tc) */
#define CDNC_UART_MODEM_STAT_REG_DDSR	(1<<1)	/* change in DSR (w1tc) */
#define CDNC_UART_MODEM_STAT_REG_DCTS	(1<<0)	/* change in CTS (w1tc) */

#define CDNC_UART_CHAN_STAT_REG	0x2C		/* Channel status register. */
#define CDNC_UART_CHAN_STAT_REG_TXNRLYFUL (1<<14) /* tx "nearly" full */
#define CDNC_UART_CHAN_STAT_REG_TXTRIG	(1<<13)
#define CDNC_UART_CHAN_STAT_REG_FDELT	(1<<12)
#define CDNC_UART_CHAN_STAT_REG_TXACTIVE (1<<11)
#define CDNC_UART_CHAN_STAT_REG_RXACTIVE (1<<10)
#define CDNC_UART_CHAN_STAT_REG_TXFULL	(1<<4)
#define CDNC_UART_CHAN_STAT_REG_TXEMPTY	(1<<3)
#define CDNC_UART_CHAN_STAT_REG_RXEMPTY	(1<<1)
#define CDNC_UART_CHAN_STAT_REG_RXTRIG	(1<<0)

#define CDNC_UART_FIFO		0x30		/* Data FIFO (tx and rx) */
#define CDNC_UART_BAUDDIV_REG	0x34
#define CDNC_UART_FLOWDEL_REG	0x38
#define CDNC_UART_TX_WATER_REG	0x44


/*
 * Low-level UART interface.
 */
static int cdnc_uart_probe(struct uart_bas *bas);
static void cdnc_uart_init(struct uart_bas *bas, int, int, int, int);
static void cdnc_uart_term(struct uart_bas *bas);
static void cdnc_uart_putc(struct uart_bas *bas, int);
static int cdnc_uart_rxready(struct uart_bas *bas);
static int cdnc_uart_getc(struct uart_bas *bas, struct mtx *mtx);

extern SLIST_HEAD(uart_devinfo_list, uart_devinfo) uart_sysdevs;

static struct uart_ops cdnc_uart_ops = {
	.probe = cdnc_uart_probe,
	.init = cdnc_uart_init,
	.term = cdnc_uart_term,
	.putc = cdnc_uart_putc,
	.rxready = cdnc_uart_rxready,
	.getc = cdnc_uart_getc,
};

#define	SIGCHG(c, i, s, d)				\
	if (c) {					\
		i |= (i & s) ? s : s | d;		\
	} else {					\
		i = (i & s) ? (i & ~s) | d : i;		\
	}

static int
cdnc_uart_probe(struct uart_bas *bas)
{

	return (0);
}

static int
cdnc_uart_set_baud(struct uart_bas *bas, int baudrate)
{
	uint32_t baudgen, bauddiv;
	uint32_t best_bauddiv, best_baudgen, best_error;
	uint32_t baud_out, err;

	best_bauddiv = 0;
	best_baudgen = 0;
	best_error = ~0;

	/* Try all possible bauddiv values and pick best match. */
	for (bauddiv = 4; bauddiv <= 255; bauddiv++) {
		baudgen = (bas->rclk + (baudrate * (bauddiv + 1)) / 2) /
			(baudrate * (bauddiv + 1));
		if (baudgen < 1 || baudgen > 0xffff)
			continue;

		baud_out = bas->rclk / (baudgen * (bauddiv + 1));
		err = baud_out > baudrate ?
			baud_out - baudrate : baudrate - baud_out;

		if (err < best_error) {
			best_error = err;
			best_bauddiv = bauddiv;
			best_baudgen = baudgen;
		}
	}

	if (best_bauddiv > 0) {
		WR4(bas, CDNC_UART_BAUDDIV_REG, best_bauddiv);
		WR4(bas, CDNC_UART_BAUDGEN_REG, best_baudgen);
		return (0);
	} else
		return (-1); /* out of range */
}

static int
cdnc_uart_set_params(struct uart_bas *bas, int baudrate, int databits,
		      int stopbits, int parity)
{
	uint32_t mode_reg_value = 0;

	switch (databits) {
	case 6:
		mode_reg_value |= CDNC_UART_MODE_REG_6BIT;
		break;
	case 7:
		mode_reg_value |= CDNC_UART_MODE_REG_7BIT;
		break;
	case 8:
	default:
		mode_reg_value |= CDNC_UART_MODE_REG_8BIT;
		break;
	}

	if (stopbits == 2)
		mode_reg_value |= CDNC_UART_MODE_REG_STOP2;

	switch (parity) {
	case UART_PARITY_MARK:
		mode_reg_value |= CDNC_UART_MODE_REG_PAR_MARK;
		break;
	case UART_PARITY_SPACE:
		mode_reg_value |= CDNC_UART_MODE_REG_PAR_SPACE;
		break;
	case UART_PARITY_ODD:
		mode_reg_value |= CDNC_UART_MODE_REG_PAR_ODD;
		break;
	case UART_PARITY_EVEN:
		mode_reg_value |= CDNC_UART_MODE_REG_PAR_EVEN;
		break;
	case UART_PARITY_NONE:
	default:
		mode_reg_value |= CDNC_UART_MODE_REG_PAR_NONE;
		break;		
	}

	WR4(bas, CDNC_UART_MODE_REG, mode_reg_value);

	if (baudrate > 0 && cdnc_uart_set_baud(bas, baudrate) < 0)
		return (EINVAL);

	return(0);
}

static void
cdnc_uart_hw_init(struct uart_bas *bas)
{

	/* Reset RX and TX. */
	WR4(bas, CDNC_UART_CTRL_REG,
	    CDNC_UART_CTRL_REG_RXRST | CDNC_UART_CTRL_REG_TXRST);

	/* Interrupts all off. */
	WR4(bas, CDNC_UART_IDIS_REG, CDNC_UART_INT_ALL);
	WR4(bas, CDNC_UART_ISTAT_REG, CDNC_UART_INT_ALL);

	/* Clear delta bits. */
	WR4(bas, CDNC_UART_MODEM_STAT_REG,
	    CDNC_UART_MODEM_STAT_REG_DDCD | CDNC_UART_MODEM_STAT_REG_TERI |
	    CDNC_UART_MODEM_STAT_REG_DDSR | CDNC_UART_MODEM_STAT_REG_DCTS);

	/* RX FIFO water level, stale timeout */
	WR4(bas, CDNC_UART_RX_WATER_REG, UART_FIFO_SIZE/2);
	WR4(bas, CDNC_UART_RX_TIMEO_REG, 10);

	/* TX FIFO water level (not used.) */
	WR4(bas, CDNC_UART_TX_WATER_REG, UART_FIFO_SIZE/2);

	/* Bring RX and TX online. */
	WR4(bas, CDNC_UART_CTRL_REG,
	    CDNC_UART_CTRL_REG_RX_EN | CDNC_UART_CTRL_REG_TX_EN |
	    CDNC_UART_CTRL_REG_TORST | CDNC_UART_CTRL_REG_STOPBRK);

	/* Set DTR and RTS. */
	WR4(bas, CDNC_UART_MODEM_CTRL_REG, CDNC_UART_MODEM_CTRL_REG_DTR |
	    CDNC_UART_MODEM_CTRL_REG_RTS);
}

/*
 * Initialize this device for use as a console.
 */
static void
cdnc_uart_init(struct uart_bas *bas, int baudrate, int databits, int stopbits,
	      int parity)
{

	/* Initialize hardware. */
	cdnc_uart_hw_init(bas);

	/* Set baudrate, parameters. */
	(void)cdnc_uart_set_params(bas, baudrate, databits, stopbits, parity);
}

/*
 * Free resources now that we're no longer the console.  This appears to
 * be never called, and I'm unsure quite what to do if I am called.
 */
static void
cdnc_uart_term(struct uart_bas *bas)
{

	/* XXX */
}

/*
 * Put a character of console output (so we do it here polling rather than
 * interrutp driven).
 */
static void
cdnc_uart_putc(struct uart_bas *bas, int c)
{

	/* Wait for room. */
	while ((RD4(bas,CDNC_UART_CHAN_STAT_REG) &
		CDNC_UART_CHAN_STAT_REG_TXFULL) != 0)
		;

	WR4(bas, CDNC_UART_FIFO, c);

	while ((RD4(bas,CDNC_UART_CHAN_STAT_REG) &
		CDNC_UART_CHAN_STAT_REG_TXEMPTY) == 0)
		;
}

/*
 * Check for a character available.
 */
static int
cdnc_uart_rxready(struct uart_bas *bas)
{

	return ((RD4(bas, CDNC_UART_CHAN_STAT_REG) &
		 CDNC_UART_CHAN_STAT_REG_RXEMPTY) == 0);
}

/*
 * Block waiting for a character.
 */
static int
cdnc_uart_getc(struct uart_bas *bas, struct mtx *mtx)
{
	int c;

	uart_lock(mtx);

	while ((RD4(bas, CDNC_UART_CHAN_STAT_REG) &
		CDNC_UART_CHAN_STAT_REG_RXEMPTY) != 0) {
		uart_unlock(mtx);
		DELAY(4);
		uart_lock(mtx);
	}
	
	c = RD4(bas, CDNC_UART_FIFO);
	
	uart_unlock(mtx);

	c &= 0xff;
	return (c);
}

/*****************************************************************************/
/*
 * High-level UART interface.
 */

static int cdnc_uart_bus_probe(struct uart_softc *sc);
static int cdnc_uart_bus_attach(struct uart_softc *sc);
static int cdnc_uart_bus_flush(struct uart_softc *, int);
static int cdnc_uart_bus_getsig(struct uart_softc *);
static int cdnc_uart_bus_ioctl(struct uart_softc *, int, intptr_t);
static int cdnc_uart_bus_ipend(struct uart_softc *);
static int cdnc_uart_bus_param(struct uart_softc *, int, int, int, int);
static int cdnc_uart_bus_receive(struct uart_softc *);
static int cdnc_uart_bus_setsig(struct uart_softc *, int);
static int cdnc_uart_bus_transmit(struct uart_softc *);
static void cdnc_uart_bus_grab(struct uart_softc *);
static void cdnc_uart_bus_ungrab(struct uart_softc *);

static kobj_method_t cdnc_uart_bus_methods[] = {
	KOBJMETHOD(uart_probe,		cdnc_uart_bus_probe),
	KOBJMETHOD(uart_attach, 	cdnc_uart_bus_attach),
	KOBJMETHOD(uart_flush,		cdnc_uart_bus_flush),
	KOBJMETHOD(uart_getsig,		cdnc_uart_bus_getsig),
	KOBJMETHOD(uart_ioctl,		cdnc_uart_bus_ioctl),
	KOBJMETHOD(uart_ipend,		cdnc_uart_bus_ipend),
	KOBJMETHOD(uart_param,		cdnc_uart_bus_param),
	KOBJMETHOD(uart_receive,	cdnc_uart_bus_receive),
	KOBJMETHOD(uart_setsig,		cdnc_uart_bus_setsig),
	KOBJMETHOD(uart_transmit,	cdnc_uart_bus_transmit),
	KOBJMETHOD(uart_grab,		cdnc_uart_bus_grab),
	KOBJMETHOD(uart_ungrab,		cdnc_uart_bus_ungrab),
	
	KOBJMETHOD_END
};

int
cdnc_uart_bus_probe(struct uart_softc *sc)
{

	sc->sc_txfifosz = UART_FIFO_SIZE;
	sc->sc_rxfifosz = UART_FIFO_SIZE;
	sc->sc_hwiflow = 0;
	sc->sc_hwoflow = 0;

	device_set_desc(sc->sc_dev, "Cadence UART");

	return (0);
}

static int
cdnc_uart_bus_attach(struct uart_softc *sc)
{
	struct uart_bas *bas = &sc->sc_bas;
	struct uart_devinfo *di;

	if (sc->sc_sysdev != NULL) {
		di = sc->sc_sysdev;
		(void)cdnc_uart_set_params(bas, di->baudrate, di->databits,
					   di->stopbits, di->parity);
	} else
		cdnc_uart_hw_init(bas);

	(void)cdnc_uart_bus_getsig(sc);

	/* Enable interrupts. */
	WR4(bas, CDNC_UART_IEN_REG,
	    CDNC_UART_INT_RXTRIG | CDNC_UART_INT_RXTMOUT |
	    CDNC_UART_INT_TXOVR | CDNC_UART_INT_RXOVR |
	    CDNC_UART_INT_DMSI);

	return (0);
}

static int
cdnc_uart_bus_transmit(struct uart_softc *sc)
{
	int i;
	struct uart_bas *bas = &sc->sc_bas;

	uart_lock(sc->sc_hwmtx);

	/* Clear sticky TXEMPTY status bit. */
	WR4(bas, CDNC_UART_ISTAT_REG, CDNC_UART_INT_TXEMPTY);

	for (i = 0; i < sc->sc_txdatasz; i++)
		WR4(bas, CDNC_UART_FIFO, sc->sc_txbuf[i]);

	/* Enable TX empty interrupt. */
	WR4(bas, CDNC_UART_IEN_REG, CDNC_UART_INT_TXEMPTY);
	sc->sc_txbusy = 1;

	uart_unlock(sc->sc_hwmtx);

	return (0);
}

static int
cdnc_uart_bus_setsig(struct uart_softc *sc, int sig)
{
	struct uart_bas *bas = &sc->sc_bas;
	uint32_t new, old, modem_ctrl;

	do {
		old = sc->sc_hwsig;
		new = old;
		if (sig & SER_DDTR) {
			SIGCHG(sig & SER_DTR, new, SER_DTR, SER_DDTR);
		}
		if (sig & SER_DRTS) {
			SIGCHG(sig & SER_RTS, new, SER_RTS, SER_DRTS);
		}
	} while (!atomic_cmpset_32(&sc->sc_hwsig, old, new));
	uart_lock(sc->sc_hwmtx);
	modem_ctrl = RD4(bas, CDNC_UART_MODEM_CTRL_REG) &
		~(CDNC_UART_MODEM_CTRL_REG_DTR | CDNC_UART_MODEM_CTRL_REG_RTS);
	if ((new & SER_DTR) != 0)
		modem_ctrl |= CDNC_UART_MODEM_CTRL_REG_DTR;
	if ((new & SER_RTS) != 0)
		modem_ctrl |= CDNC_UART_MODEM_CTRL_REG_RTS;
	WR4(bas, CDNC_UART_MODEM_CTRL_REG, modem_ctrl);

	uart_unlock(sc->sc_hwmtx);
	return (0);
}

static int
cdnc_uart_bus_receive(struct uart_softc *sc)
{
	struct uart_bas *bas = &sc->sc_bas;
	uint32_t status;
	int c, c_status = 0;

	uart_lock(sc->sc_hwmtx);

	/* Check for parity or framing errors and clear the status bits. */
	status = RD4(bas, CDNC_UART_ISTAT_REG);
	if ((status & (CDNC_UART_INT_FRAMING | CDNC_UART_INT_PARITY)) != 0) {
		WR4(bas, CDNC_UART_ISTAT_REG,
		    status & (CDNC_UART_INT_FRAMING | CDNC_UART_INT_PARITY));
		if ((status & CDNC_UART_INT_PARITY) != 0)
			c_status |= UART_STAT_PARERR;
		if ((status & CDNC_UART_INT_FRAMING) != 0)
			c_status |= UART_STAT_FRAMERR;
	}

	while ((RD4(bas, CDNC_UART_CHAN_STAT_REG) &
		CDNC_UART_CHAN_STAT_REG_RXEMPTY) == 0) {
		c = RD4(bas, CDNC_UART_FIFO) & 0xff;
#ifdef KDB
		/* Detect break and drop into debugger. */
		if (c == 0 && (c_status & UART_STAT_FRAMERR) != 0 &&
		    sc->sc_sysdev != NULL &&
		    sc->sc_sysdev->type == UART_DEV_CONSOLE) {
			kdb_break();
			WR4(bas, CDNC_UART_ISTAT_REG, CDNC_UART_INT_FRAMING);
		}
#endif
		uart_rx_put(sc, c | c_status);
	}

	uart_unlock(sc->sc_hwmtx);

	return (0);
}

static int
cdnc_uart_bus_param(struct uart_softc *sc, int baudrate, int databits,
		   int stopbits, int parity)
{

	return (cdnc_uart_set_params(&sc->sc_bas, baudrate,
				    databits, stopbits, parity));
}

static int
cdnc_uart_bus_ipend(struct uart_softc *sc)
{
	int ipend = 0;
	struct uart_bas *bas = &sc->sc_bas;
	uint32_t istatus;

	uart_lock(sc->sc_hwmtx);

	istatus = RD4(bas, CDNC_UART_ISTAT_REG);

	/* Clear interrupt bits. */
	WR4(bas, CDNC_UART_ISTAT_REG, istatus &
	    (CDNC_UART_INT_RXTRIG | CDNC_UART_INT_RXTMOUT |
	     CDNC_UART_INT_TXOVR | CDNC_UART_INT_RXOVR |
	     CDNC_UART_INT_TXEMPTY | CDNC_UART_INT_DMSI));

	/* Receive data. */
	if ((istatus & (CDNC_UART_INT_RXTRIG | CDNC_UART_INT_RXTMOUT)) != 0)
		ipend |= SER_INT_RXREADY;

	/* Transmit fifo empty. */
	if (sc->sc_txbusy && (istatus & CDNC_UART_INT_TXEMPTY) != 0) {
		/* disable txempty interrupt. */
		WR4(bas, CDNC_UART_IDIS_REG, CDNC_UART_INT_TXEMPTY);
		ipend |= SER_INT_TXIDLE;
	}

	/* TX Overflow. */
	if ((istatus & CDNC_UART_INT_TXOVR) != 0)
		ipend |= SER_INT_OVERRUN;

	/* RX Overflow. */
	if ((istatus & CDNC_UART_INT_RXOVR) != 0)
		ipend |= SER_INT_OVERRUN;

	/* Modem signal change. */
	if ((istatus & CDNC_UART_INT_DMSI) != 0) {
		WR4(bas, CDNC_UART_MODEM_STAT_REG,
		    CDNC_UART_MODEM_STAT_REG_DDCD |
		    CDNC_UART_MODEM_STAT_REG_TERI |
		    CDNC_UART_MODEM_STAT_REG_DDSR |
		    CDNC_UART_MODEM_STAT_REG_DCTS);
		ipend |= SER_INT_SIGCHG;
	}

	uart_unlock(sc->sc_hwmtx);
	return (ipend);
}

static int
cdnc_uart_bus_flush(struct uart_softc *sc, int what)
{

	return (0);
}

static int
cdnc_uart_bus_getsig(struct uart_softc *sc)
{
	struct uart_bas *bas = &sc->sc_bas;
	uint32_t new, old, sig;
	uint8_t modem_status;

	do {
		old = sc->sc_hwsig;
		sig = old;
		uart_lock(sc->sc_hwmtx);
		modem_status = RD4(bas, CDNC_UART_MODEM_STAT_REG);
		uart_unlock(sc->sc_hwmtx);
		SIGCHG(modem_status & CDNC_UART_MODEM_STAT_REG_DSR,
		       sig, SER_DSR, SER_DDSR);
		SIGCHG(modem_status & CDNC_UART_MODEM_STAT_REG_CTS,
		       sig, SER_CTS, SER_DCTS);
		SIGCHG(modem_status & CDNC_UART_MODEM_STAT_REG_DCD,
		       sig, SER_DCD, SER_DDCD);
		SIGCHG(modem_status & CDNC_UART_MODEM_STAT_REG_RI,
		       sig, SER_RI,  SER_DRI);
		new = sig & ~SER_MASK_DELTA;
	} while (!atomic_cmpset_32(&sc->sc_hwsig, old, new));
	return (sig);
}

static int
cdnc_uart_bus_ioctl(struct uart_softc *sc, int request, intptr_t data)
{
	struct uart_bas *bas = &sc->sc_bas;
	uint32_t uart_ctrl, modem_ctrl;
	int error = 0;

	uart_lock(sc->sc_hwmtx);

	switch (request) {
	case UART_IOCTL_BREAK:
		uart_ctrl = RD4(bas, CDNC_UART_CTRL_REG);
		if (data) {
			uart_ctrl |= CDNC_UART_CTRL_REG_STARTBRK;
			uart_ctrl &= ~CDNC_UART_CTRL_REG_STOPBRK;
		} else {
			uart_ctrl |= CDNC_UART_CTRL_REG_STOPBRK;
			uart_ctrl &= ~CDNC_UART_CTRL_REG_STARTBRK;
		}
		WR4(bas, CDNC_UART_CTRL_REG, uart_ctrl);
		break;
	case UART_IOCTL_IFLOW:
		modem_ctrl = RD4(bas, CDNC_UART_MODEM_CTRL_REG);
		if (data)
			modem_ctrl |= CDNC_UART_MODEM_CTRL_REG_RTS;
		else
			modem_ctrl &= ~CDNC_UART_MODEM_CTRL_REG_RTS;
		WR4(bas, CDNC_UART_MODEM_CTRL_REG, modem_ctrl);
		break;
	default:
		error = EINVAL;
		break;
	}

	uart_unlock(sc->sc_hwmtx);

	return (error);
}

static void
cdnc_uart_bus_grab(struct uart_softc *sc)
{

	/* Enable interrupts. */
	WR4(&sc->sc_bas, CDNC_UART_IEN_REG,
	    CDNC_UART_INT_TXOVR | CDNC_UART_INT_RXOVR |
	    CDNC_UART_INT_DMSI);
}

static void
cdnc_uart_bus_ungrab(struct uart_softc *sc)
{

	/* Enable interrupts. */
	WR4(&sc->sc_bas, CDNC_UART_IEN_REG,
	    CDNC_UART_INT_RXTRIG | CDNC_UART_INT_RXTMOUT |
	    CDNC_UART_INT_TXOVR | CDNC_UART_INT_RXOVR |
	    CDNC_UART_INT_DMSI);
}

static struct uart_class uart_cdnc_class = {
	"cdnc_uart",
	cdnc_uart_bus_methods,
	sizeof(struct uart_softc),
	.uc_ops = &cdnc_uart_ops,
	.uc_range = 8
};

static struct ofw_compat_data compat_data[] = {
	{"cadence,uart",	(uintptr_t)&uart_cdnc_class},
	{"cdns,uart-r1p12",	(uintptr_t)&uart_cdnc_class},
	{"xlnx,xuartps",	(uintptr_t)&uart_cdnc_class},
	{NULL,			(uintptr_t)NULL},
};
UART_FDT_CLASS_AND_DEVICE(compat_data);
