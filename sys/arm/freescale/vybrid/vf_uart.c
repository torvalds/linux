/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2013 Ruslan Bukin <br@bsdpad.com>
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

/*
 * Vybrid Family Universal Asynchronous Receiver/Transmitter
 * Chapter 49, Vybrid Reference Manual, Rev. 5, 07/2013
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "opt_ddb.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/conf.h>
#include <sys/kdb.h>
#include <machine/bus.h>

#include <dev/uart/uart.h>
#include <dev/uart/uart_cpu.h>
#include <dev/uart/uart_cpu_fdt.h>
#include <dev/uart/uart_bus.h>

#include "uart_if.h"

#define	UART_BDH	0x00	/* Baud Rate Registers: High */
#define	UART_BDL	0x01	/* Baud Rate Registers: Low */
#define	UART_C1		0x02	/* Control Register 1 */
#define	UART_C2		0x03	/* Control Register 2 */
#define	UART_S1		0x04	/* Status Register 1 */
#define	UART_S2		0x05	/* Status Register 2 */
#define	UART_C3		0x06	/* Control Register 3 */
#define	UART_D		0x07	/* Data Register */
#define	UART_MA1	0x08	/* Match Address Registers 1 */
#define	UART_MA2	0x09	/* Match Address Registers 2 */
#define	UART_C4		0x0A	/* Control Register 4 */
#define	UART_C5		0x0B	/* Control Register 5 */
#define	UART_ED		0x0C	/* Extended Data Register */
#define	UART_MODEM	0x0D	/* Modem Register */
#define	UART_IR		0x0E	/* Infrared Register */
#define	UART_PFIFO	0x10	/* FIFO Parameters */
#define	UART_CFIFO	0x11	/* FIFO Control Register */
#define	UART_SFIFO	0x12	/* FIFO Status Register */
#define	UART_TWFIFO	0x13	/* FIFO Transmit Watermark */
#define	UART_TCFIFO	0x14	/* FIFO Transmit Count */
#define	UART_RWFIFO	0x15	/* FIFO Receive Watermark */
#define	UART_RCFIFO	0x16	/* FIFO Receive Count */
#define	UART_C7816	0x18	/* 7816 Control Register */
#define	UART_IE7816	0x19	/* 7816 Interrupt Enable Register */
#define	UART_IS7816	0x1A	/* 7816 Interrupt Status Register */
#define	UART_WP7816T0	0x1B	/* 7816 Wait Parameter Register */
#define	UART_WP7816T1	0x1B	/* 7816 Wait Parameter Register */
#define	UART_WN7816	0x1C	/* 7816 Wait N Register */
#define	UART_WF7816	0x1D	/* 7816 Wait FD Register */
#define	UART_ET7816	0x1E	/* 7816 Error Threshold Register */
#define	UART_TL7816	0x1F	/* 7816 Transmit Length Register */
#define	UART_C6		0x21	/* CEA709.1-B Control Register 6 */
#define	UART_PCTH	0x22	/* CEA709.1-B Packet Cycle Time Counter High */
#define	UART_PCTL	0x23	/* CEA709.1-B Packet Cycle Time Counter Low */
#define	UART_B1T	0x24	/* CEA709.1-B Beta1 Timer */
#define	UART_SDTH	0x25	/* CEA709.1-B Secondary Delay Timer High */
#define	UART_SDTL	0x26	/* CEA709.1-B Secondary Delay Timer Low */
#define	UART_PRE	0x27	/* CEA709.1-B Preamble */
#define	UART_TPL	0x28	/* CEA709.1-B Transmit Packet Length */
#define	UART_IE		0x29	/* CEA709.1-B Interrupt Enable Register */
#define	UART_WB		0x2A	/* CEA709.1-B WBASE */
#define	UART_S3		0x2B	/* CEA709.1-B Status Register */
#define	UART_S4		0x2C	/* CEA709.1-B Status Register */
#define	UART_RPL	0x2D	/* CEA709.1-B Received Packet Length */
#define	UART_RPREL	0x2E	/* CEA709.1-B Received Preamble Length */
#define	UART_CPW	0x2F	/* CEA709.1-B Collision Pulse Width */
#define	UART_RIDT	0x30	/* CEA709.1-B Receive Indeterminate Time */
#define	UART_TIDT	0x31	/* CEA709.1-B Transmit Indeterminate Time */

#define	UART_C2_TE	(1 << 3)	/* Transmitter Enable */
#define	UART_C2_TIE	(1 << 7)	/* Transmitter Interrupt Enable */
#define	UART_C2_RE	(1 << 2)	/* Receiver Enable */
#define	UART_C2_RIE	(1 << 5)	/* Receiver Interrupt Enable */
#define	UART_S1_TDRE	(1 << 7)	/* Transmit Data Register Empty Flag */
#define	UART_S1_RDRF	(1 << 5)	/* Receive Data Register Full Flag */
#define	UART_S2_LBKDIF	(1 << 7)	/* LIN Break Detect Interrupt Flag */

#define	UART_C4_BRFA	0x1f	/* Baud Rate Fine Adjust */
#define	UART_BDH_SBR	0x1f	/* UART Baud Rate Bits */

/*
 * Low-level UART interface.
 */
static int vf_uart_probe(struct uart_bas *bas);
static void vf_uart_init(struct uart_bas *bas, int, int, int, int);
static void vf_uart_term(struct uart_bas *bas);
static void vf_uart_putc(struct uart_bas *bas, int);
static int vf_uart_rxready(struct uart_bas *bas);
static int vf_uart_getc(struct uart_bas *bas, struct mtx *);

void uart_reinit(struct uart_softc *,int,int);

static struct uart_ops uart_vybrid_ops = {
	.probe = vf_uart_probe,
	.init = vf_uart_init,
	.term = vf_uart_term,
	.putc = vf_uart_putc,
	.rxready = vf_uart_rxready,
	.getc = vf_uart_getc,
};

static int
vf_uart_probe(struct uart_bas *bas)
{

	return (0);
}

static void
vf_uart_init(struct uart_bas *bas, int baudrate, int databits,
    int stopbits, int parity)
{

}

static void
vf_uart_term(struct uart_bas *bas)
{

}

static void
vf_uart_putc(struct uart_bas *bas, int c)
{

	while (!(uart_getreg(bas, UART_S1) & UART_S1_TDRE))
		;

	uart_setreg(bas, UART_D, c);
}

static int
vf_uart_rxready(struct uart_bas *bas)
{
	int usr1;

	usr1 = uart_getreg(bas, UART_S1);
	if (usr1 & UART_S1_RDRF) {
		return (1);
	}

	return (0);
}

static int
vf_uart_getc(struct uart_bas *bas, struct mtx *hwmtx)
{
	int c;

	uart_lock(hwmtx);

	while (!(uart_getreg(bas, UART_S1) & UART_S1_RDRF))
		;

	c = uart_getreg(bas, UART_D);
	uart_unlock(hwmtx);

	return (c & 0xff);
}

/*
 * High-level UART interface.
 */
struct vf_uart_softc {
	struct uart_softc base;
};

void
uart_reinit(struct uart_softc *sc, int clkspeed, int baud)
{
	struct uart_bas *bas;
	int sbr;
	int brfa;
	int reg;

	bas = &sc->sc_bas;
	if (!bas) {
		printf("Error: can't reconfigure bas\n");
		return;
	}

	uart_setreg(bas, UART_MODEM, 0x00);

	/*
	 * Disable transmitter and receiver
	 * for a while.
	 */
	reg = uart_getreg(bas, UART_C2);
	reg &= ~(UART_C2_RE | UART_C2_TE);
	uart_setreg(bas, UART_C2, 0x00);

	uart_setreg(bas, UART_C1, 0x00);

	sbr = (uint16_t) (clkspeed / (baud * 16));
	brfa = (clkspeed / baud) - (sbr * 16);

	reg = uart_getreg(bas, UART_BDH);
	reg &= ~UART_BDH_SBR;
	reg |= ((sbr & 0x1f00) >> 8);
	uart_setreg(bas, UART_BDH, reg);

	reg = sbr & 0x00ff;
	uart_setreg(bas, UART_BDL, reg);

	reg = uart_getreg(bas, UART_C4);
	reg &= ~UART_C4_BRFA;
	reg |= (brfa & UART_C4_BRFA);
	uart_setreg(bas, UART_C4, reg);

	reg = uart_getreg(bas, UART_C2);
	reg |= (UART_C2_RE | UART_C2_TE);
	uart_setreg(bas, UART_C2, reg);

}

static int vf_uart_bus_attach(struct uart_softc *);
static int vf_uart_bus_detach(struct uart_softc *);
static int vf_uart_bus_flush(struct uart_softc *, int);
static int vf_uart_bus_getsig(struct uart_softc *);
static int vf_uart_bus_ioctl(struct uart_softc *, int, intptr_t);
static int vf_uart_bus_ipend(struct uart_softc *);
static int vf_uart_bus_param(struct uart_softc *, int, int, int, int);
static int vf_uart_bus_probe(struct uart_softc *);
static int vf_uart_bus_receive(struct uart_softc *);
static int vf_uart_bus_setsig(struct uart_softc *, int);
static int vf_uart_bus_transmit(struct uart_softc *);

static kobj_method_t vf_uart_methods[] = {
	KOBJMETHOD(uart_attach,		vf_uart_bus_attach),
	KOBJMETHOD(uart_detach,		vf_uart_bus_detach),
	KOBJMETHOD(uart_flush,		vf_uart_bus_flush),
	KOBJMETHOD(uart_getsig,		vf_uart_bus_getsig),
	KOBJMETHOD(uart_ioctl,		vf_uart_bus_ioctl),
	KOBJMETHOD(uart_ipend,		vf_uart_bus_ipend),
	KOBJMETHOD(uart_param,		vf_uart_bus_param),
	KOBJMETHOD(uart_probe,		vf_uart_bus_probe),
	KOBJMETHOD(uart_receive,	vf_uart_bus_receive),
	KOBJMETHOD(uart_setsig,		vf_uart_bus_setsig),
	KOBJMETHOD(uart_transmit,	vf_uart_bus_transmit),
	{ 0, 0 }
};

static struct uart_class uart_vybrid_class = {
	"vybrid",
	vf_uart_methods,
	sizeof(struct vf_uart_softc),
	.uc_ops = &uart_vybrid_ops,
	.uc_range = 0x100,
	.uc_rclk = 24000000, /* TODO: get value from CCM */
	.uc_rshift = 0
};

static struct ofw_compat_data compat_data[] = {
	{"fsl,mvf600-uart",	(uintptr_t)&uart_vybrid_class},
	{NULL,			(uintptr_t)NULL},
};
UART_FDT_CLASS_AND_DEVICE(compat_data);

static int
vf_uart_bus_attach(struct uart_softc *sc)
{
	struct uart_bas *bas;
	int reg;

	bas = &sc->sc_bas;

	sc->sc_hwiflow = 0;
	sc->sc_hwoflow = 0;

	uart_reinit(sc, 66000000, 115200);

	reg = uart_getreg(bas, UART_C2);
	if (sc->sc_sysdev != NULL && sc->sc_sysdev->type == UART_DEV_CONSOLE) {
		reg &= ~UART_C2_RIE;
	} else {
		reg |= UART_C2_RIE;
	}
	uart_setreg(bas, UART_C2, reg);

	return (0);
}

static int
vf_uart_bus_detach(struct uart_softc *sc)
{

	/* TODO */
	return (0);
}

static int
vf_uart_bus_flush(struct uart_softc *sc, int what)
{

	/* TODO */
	return (0);
}

static int
vf_uart_bus_getsig(struct uart_softc *sc)
{

	/* TODO */
	return (0);
}

static int
vf_uart_bus_ioctl(struct uart_softc *sc, int request, intptr_t data)
{
	struct uart_bas *bas;
	int error;

	bas = &sc->sc_bas;
	error = 0;
	uart_lock(sc->sc_hwmtx);
	switch (request) {
	case UART_IOCTL_BREAK:
	/* TODO */
		break;
	case UART_IOCTL_BAUD:
	/* TODO */
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
vf_uart_bus_ipend(struct uart_softc *sc)
{
	struct uart_bas *bas;
	int ipend;
	uint32_t usr1, usr2;
	int reg;
	int sfifo;

	bas = &sc->sc_bas;
	ipend = 0;

	uart_lock(sc->sc_hwmtx);

	usr1 = uart_getreg(bas, UART_S1);
	usr2 = uart_getreg(bas, UART_S2);
	sfifo = uart_getreg(bas, UART_SFIFO);

	/* ack usr2 */
	uart_setreg(bas, UART_S2, usr2);

	if (usr1 & UART_S1_TDRE) {
		reg = uart_getreg(bas, UART_C2);
		reg &= ~(UART_C2_TIE);
		uart_setreg(bas, UART_C2, reg);

		if (sc->sc_txbusy != 0) {
			ipend |= SER_INT_TXIDLE;
		}
	}

	if (usr1 & UART_S1_RDRF) {
		reg = uart_getreg(bas, UART_C2);
		reg &= ~(UART_C2_RIE);
		uart_setreg(bas, UART_C2, reg);

		ipend |= SER_INT_RXREADY;
	}

	if (usr2 & UART_S2_LBKDIF) {
		ipend |= SER_INT_BREAK;
	}

	uart_unlock(sc->sc_hwmtx);

	return (ipend);
}

static int
vf_uart_bus_param(struct uart_softc *sc, int baudrate, int databits,
    int stopbits, int parity)
{

	uart_lock(sc->sc_hwmtx);
	vf_uart_init(&sc->sc_bas, baudrate, databits, stopbits, parity);
	uart_unlock(sc->sc_hwmtx);

	return (0);
}

static int
vf_uart_bus_probe(struct uart_softc *sc)
{
	int error;

	error = vf_uart_probe(&sc->sc_bas);
	if (error)
		return (error);

	sc->sc_rxfifosz = 1;
	sc->sc_txfifosz = 1;

	device_set_desc(sc->sc_dev, "Vybrid Family UART");
	return (0);
}

static int
vf_uart_bus_receive(struct uart_softc *sc)
{
	struct uart_bas *bas;
	int reg;
	int c;

	bas = &sc->sc_bas;
	uart_lock(sc->sc_hwmtx);

	/* Read FIFO */
	while (uart_getreg(bas, UART_S1) & UART_S1_RDRF) {
		if (uart_rx_full(sc)) {
		/* No space left in input buffer */
			sc->sc_rxbuf[sc->sc_rxput] = UART_STAT_OVERRUN;
			break;
		}

		c = uart_getreg(bas, UART_D);
		uart_rx_put(sc, c);
	}

	/* Reenable Data Ready interrupt */
	reg = uart_getreg(bas, UART_C2);
	reg |= (UART_C2_RIE);
	uart_setreg(bas, UART_C2, reg);

	uart_unlock(sc->sc_hwmtx);
	return (0);
}

static int
vf_uart_bus_setsig(struct uart_softc *sc, int sig)
{
	struct uart_bas *bas;
	int reg;

	/* TODO: implement (?) */

	/* XXX workaround to have working console on mount prompt */
	/* Enable RX interrupt */
	bas = &sc->sc_bas;
	if (sc->sc_sysdev != NULL && sc->sc_sysdev->type == UART_DEV_CONSOLE) {
		reg = uart_getreg(bas, UART_C2);
		reg |= (UART_C2_RIE);
		uart_setreg(bas, UART_C2, reg);
	}

	return (0);
}

static int
vf_uart_bus_transmit(struct uart_softc *sc)
{
	struct uart_bas *bas = &sc->sc_bas;
	int i;
	int reg;

	bas = &sc->sc_bas;
	uart_lock(sc->sc_hwmtx);

	/* Fill TX FIFO */
	for (i = 0; i < sc->sc_txdatasz; i++) {
		uart_setreg(bas, UART_D, sc->sc_txbuf[i] & 0xff);
		uart_barrier(&sc->sc_bas);
	}

	sc->sc_txbusy = 1;

	/* Call me when ready */
	reg = uart_getreg(bas, UART_C2);
	reg |= (UART_C2_TIE);
	uart_setreg(bas, UART_C2, reg);

	uart_unlock(sc->sc_hwmtx);

	return (0);
}
