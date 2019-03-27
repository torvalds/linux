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
 *
 * $FreeBSD$
 */

#ifndef _DEV_UART_BUS_H_
#define _DEV_UART_BUS_H_

#ifndef KLD_MODULE
#include "opt_uart.h"
#endif

#include <sys/serial.h>
#include <sys/timepps.h>

/* Drain and flush targets. */
#define	UART_DRAIN_RECEIVER	0x0001
#define	UART_DRAIN_TRANSMITTER	0x0002
#define	UART_FLUSH_RECEIVER	UART_DRAIN_RECEIVER
#define	UART_FLUSH_TRANSMITTER	UART_DRAIN_TRANSMITTER

/* Received character status bits. */
#define	UART_STAT_BREAK		0x0100
#define	UART_STAT_FRAMERR	0x0200
#define	UART_STAT_OVERRUN	0x0400
#define	UART_STAT_PARERR	0x0800

/* UART_IOCTL() requests */
#define	UART_IOCTL_BREAK	1
#define	UART_IOCTL_IFLOW	2
#define	UART_IOCTL_OFLOW	3
#define	UART_IOCTL_BAUD		4

/* UART quirk flags */
#define	UART_F_BUSY_DETECT	0x1

/*
 * UART class & instance (=softc)
 */
struct uart_class {
	KOBJ_CLASS_FIELDS;
	struct uart_ops *uc_ops;	/* Low-level console operations. */
	u_int	uc_range;		/* Bus space address range. */
	u_int	uc_rclk;		/* Default rclk for this device. */
	u_int	uc_rshift;		/* Default regshift for this device. */
	u_int	uc_riowidth;		/* Default reg io width for this device. */
};

struct uart_softc {
	KOBJ_FIELDS;
	struct uart_class *sc_class;
	struct uart_bas	sc_bas;
	device_t	sc_dev;

	struct mtx	sc_hwmtx_s;	/* Spinlock protecting hardware. */
	struct mtx	*sc_hwmtx;

	struct resource	*sc_rres;	/* Register resource. */
	int		sc_rrid;
	int		sc_rtype;	/* SYS_RES_{IOPORT|MEMORY}. */
	struct resource *sc_ires;	/* Interrupt resource. */
	void		*sc_icookie;
	int		sc_irid;
	struct callout	sc_timer;

	int		sc_callout:1;	/* This UART is opened for callout. */
	int		sc_fastintr:1;	/* This UART uses fast interrupts. */
	int		sc_hwiflow:1;	/* This UART has HW input flow ctl. */
	int		sc_hwoflow:1;	/* This UART has HW output flow ctl. */
	int		sc_leaving:1;	/* This UART is going away. */
	int		sc_opened:1;	/* This UART is open for business. */
	int		sc_polled:1;	/* This UART has no interrupts. */
	int		sc_txbusy:1;	/* This UART is transmitting. */
	int		sc_isquelch:1;	/* This UART has input squelched. */
	int		sc_testintr:1;	/* This UART is under int. testing. */

	struct uart_devinfo *sc_sysdev;	/* System device (or NULL). */

	int		sc_altbrk;	/* State for alt break sequence. */
	uint32_t	sc_hwsig;	/* Signal state. Used by HW driver. */

	/* Receiver data. */
	uint16_t	*sc_rxbuf;
	int		sc_rxbufsz;
	int		sc_rxput;
	int		sc_rxget;
	int		sc_rxfifosz;	/* Size of RX FIFO. */

	/* Transmitter data. */
	uint8_t		*sc_txbuf;
	int		sc_txdatasz;
	int		sc_txfifosz;	/* Size of TX FIFO and buffer. */

	/* Pulse capturing support (PPS). */
	struct pps_state sc_pps;
	int		 sc_pps_mode;
	sbintime_t	 sc_pps_captime;

	/* Upper layer data. */
	void		*sc_softih;
	uint32_t	sc_ttypend;
	union {
		/* TTY specific data. */
		struct {
			struct tty *tp;
		} u_tty;
		/* Keyboard specific data. */
		struct {
		} u_kbd;
	} sc_u;
};

extern devclass_t uart_devclass;
extern const char uart_driver_name[];

int uart_bus_attach(device_t dev);
int uart_bus_detach(device_t dev);
int uart_bus_resume(device_t dev);
serdev_intr_t *uart_bus_ihand(device_t dev, int ipend);
int uart_bus_ipend(device_t dev);
int uart_bus_probe(device_t dev, int regshft, int regiowidth, int rclk, int rid, int chan, int quirks);
int uart_bus_sysdev(device_t dev);

void uart_sched_softih(struct uart_softc *, uint32_t);

int uart_tty_attach(struct uart_softc *);
int uart_tty_detach(struct uart_softc *);
struct mtx *uart_tty_getlock(struct uart_softc *);
void uart_tty_intr(void *arg);

/*
 * Receive buffer operations.
 */
static __inline int
uart_rx_empty(struct uart_softc *sc)
{

	return ((sc->sc_rxget == sc->sc_rxput) ? 1 : 0);
}

static __inline int
uart_rx_full(struct uart_softc *sc)
{

	return ((sc->sc_rxput + 1 < sc->sc_rxbufsz) ?
	    (sc->sc_rxput + 1 == sc->sc_rxget) : (sc->sc_rxget == 0));
}

static __inline int
uart_rx_get(struct uart_softc *sc)
{
	int ptr, xc;

	ptr = sc->sc_rxget;
	if (ptr == sc->sc_rxput)
		return (-1);
	xc = sc->sc_rxbuf[ptr++];
	sc->sc_rxget = (ptr < sc->sc_rxbufsz) ? ptr : 0;
	return (xc);
}

static __inline int
uart_rx_next(struct uart_softc *sc)
{
	int ptr;

	ptr = sc->sc_rxget;
	if (ptr == sc->sc_rxput)
		return (-1);
	ptr += 1;
	sc->sc_rxget = (ptr < sc->sc_rxbufsz) ? ptr : 0;
	return (0);
}

static __inline int
uart_rx_peek(struct uart_softc *sc)
{
	int ptr;

	ptr = sc->sc_rxget;
	return ((ptr == sc->sc_rxput) ? -1 : sc->sc_rxbuf[ptr]);
}

static __inline int
uart_rx_put(struct uart_softc *sc, int xc)
{
	int ptr;

	ptr = (sc->sc_rxput + 1 < sc->sc_rxbufsz) ? sc->sc_rxput + 1 : 0;
	if (ptr == sc->sc_rxget)
		return (ENOSPC);
	sc->sc_rxbuf[sc->sc_rxput] = xc;
	sc->sc_rxput = ptr;
	return (0);
}

#endif /* _DEV_UART_BUS_H_ */
