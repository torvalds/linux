/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2012 The FreeBSD Foundation
 * All rights reserved.
 *
 * This software was developed by Oleksandr Rybalko under sponsorship
 * from the FreeBSD Foundation.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1.	Redistributions of source code must retain the above copyright
 *	notice, this list of conditions and the following disclaimer.
 * 2.	Redistributions in binary form must reproduce the above copyright
 *	notice, this list of conditions and the following disclaimer in the
 *	documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
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

#include "opt_ddb.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/conf.h>
#include <sys/kdb.h>
#include <machine/bus.h>
#include <machine/fdt.h>

#include <dev/uart/uart.h>
#include <dev/uart/uart_cpu.h>
#include <dev/uart/uart_cpu_fdt.h>
#include <dev/uart/uart_bus.h>
#include <dev/uart/uart_dev_imx.h>
#include "uart_if.h"

#include <arm/freescale/imx/imx_ccmvar.h>

/*
 * The hardare FIFOs are 32 bytes.  We want an interrupt when there are 24 bytes
 * available to read or space for 24 more bytes to write.  While 8 bytes of
 * slack before over/underrun might seem excessive, the hardware can run at
 * 5mbps, which means 2uS per char, so at full speed 8 bytes provides only 16uS
 * to get into the interrupt handler and service the fifo.
 */
#define	IMX_FIFOSZ		32
#define	IMX_RXFIFO_LEVEL	24
#define	IMX_TXFIFO_LEVEL	24

/*
 * Low-level UART interface.
 */
static int imx_uart_probe(struct uart_bas *bas);
static void imx_uart_init(struct uart_bas *bas, int, int, int, int);
static void imx_uart_term(struct uart_bas *bas);
static void imx_uart_putc(struct uart_bas *bas, int);
static int imx_uart_rxready(struct uart_bas *bas);
static int imx_uart_getc(struct uart_bas *bas, struct mtx *);

static struct uart_ops uart_imx_uart_ops = {
	.probe = imx_uart_probe,
	.init = imx_uart_init,
	.term = imx_uart_term,
	.putc = imx_uart_putc,
	.rxready = imx_uart_rxready,
	.getc = imx_uart_getc,
};

#if 0 /* Handy when debugging. */
static void
dumpregs(struct uart_bas *bas, const char * msg)
{

	if (!bootverbose)
		return;
	printf("%s bsh 0x%08lx UCR1 0x%08x UCR2 0x%08x "
		"UCR3 0x%08x UCR4 0x%08x USR1 0x%08x USR2 0x%08x\n",
	    msg, bas->bsh,
	    GETREG(bas, REG(UCR1)), GETREG(bas, REG(UCR2)), 
	    GETREG(bas, REG(UCR3)), GETREG(bas, REG(UCR4)),
	    GETREG(bas, REG(USR1)), GETREG(bas, REG(USR2)));
}
#endif

static int
imx_uart_probe(struct uart_bas *bas)
{

	return (0);
}

static u_int
imx_uart_getbaud(struct uart_bas *bas)
{
	uint32_t rate, ubir, ubmr;
	u_int baud, blo, bhi, i;
	static const u_int predivs[] = {6, 5, 4, 3, 2, 1, 7, 1};
	static const u_int std_rates[] = {
		9600, 14400, 19200, 38400, 57600, 115200, 230400, 460800, 921600
	};

	/*
	 * Get the baud rate the hardware is programmed for, then search the
	 * table of standard baud rates for a number that's within 3% of the
	 * actual rate the hardware is programmed for.  It's more comforting to
	 * see that your console is running at 115200 than 114942.  Note that
	 * here we cannot make a simplifying assumption that the predivider and
	 * numerator are 1 (like we do when setting the baud rate), because we
	 * don't know what u-boot might have set up.
	 */
	i = (GETREG(bas, REG(UFCR)) & IMXUART_UFCR_RFDIV_MASK) >>
	    IMXUART_UFCR_RFDIV_SHIFT;
	rate = imx_ccm_uart_hz() / predivs[i];
	ubir = GETREG(bas, REG(UBIR)) + 1;
	ubmr = GETREG(bas, REG(UBMR)) + 1;
	baud = ((rate / 16 ) * ubir) / ubmr;

	blo = (baud * 100) / 103;
	bhi = (baud * 100) / 97;
	for (i = 0; i < nitems(std_rates); i++) {
		rate = std_rates[i];
		if (rate >= blo && rate <= bhi) {
			baud = rate;
			break;
		}
	}

	return (baud);
}

static void
imx_uart_init(struct uart_bas *bas, int baudrate, int databits, 
    int stopbits, int parity)
{
	uint32_t baseclk, reg;

        /* Enable the device and the RX/TX channels. */
	SET(bas, REG(UCR1), FLD(UCR1, UARTEN));
	SET(bas, REG(UCR2), FLD(UCR2, RXEN) | FLD(UCR2, TXEN));

	if (databits == 7)
		DIS(bas, UCR2, WS);
	else
		ENA(bas, UCR2, WS);

	if (stopbits == 2)
		ENA(bas, UCR2, STPB);
	else
		DIS(bas, UCR2, STPB);

	switch (parity) {
	case UART_PARITY_ODD:
		DIS(bas, UCR2, PROE);
		ENA(bas, UCR2, PREN);
		break;
	case UART_PARITY_EVEN:
		ENA(bas, UCR2, PROE);
		ENA(bas, UCR2, PREN);
		break;
	case UART_PARITY_MARK:
	case UART_PARITY_SPACE:
                /* FALLTHROUGH: Hardware doesn't support mark/space. */
	case UART_PARITY_NONE:
	default:
		DIS(bas, UCR2, PREN);
		break;
	}

	/*
	 * The hardware has an extremely flexible baud clock: it allows setting
	 * both the numerator and denominator of the divider, as well as a
	 * separate pre-divider.  We simplify the problem of coming up with a
	 * workable pair of numbers by assuming a pre-divider and numerator of
	 * one because our base clock is so fast we can reach virtually any
	 * reasonable speed with a simple divisor.  The numerator value actually
	 * includes the 16x over-sampling (so a value of 16 means divide by 1);
	 * the register value is the numerator-1, so we have a hard-coded 15.
	 * Note that a quirk of the hardware requires that both UBIR and UBMR be
	 * set back to back in order for the change to take effect.
	 */
	if (baudrate > 0) {
		baseclk = imx_ccm_uart_hz();
		reg = GETREG(bas, REG(UFCR));
		reg = (reg & ~IMXUART_UFCR_RFDIV_MASK) | IMXUART_UFCR_RFDIV_DIV1;
		SETREG(bas, REG(UFCR), reg);
		SETREG(bas, REG(UBIR), 15);
		SETREG(bas, REG(UBMR), (baseclk / baudrate) - 1);
	}

	/*
	 * Program the tx lowater and rx hiwater levels at which fifo-service
	 * interrupts are signaled.  The tx value is interpetted as "when there
	 * are only this many bytes remaining" (not "this many free").
	 */
	reg = GETREG(bas, REG(UFCR));
	reg &= ~(IMXUART_UFCR_TXTL_MASK | IMXUART_UFCR_RXTL_MASK);
	reg |= (IMX_FIFOSZ - IMX_TXFIFO_LEVEL) << IMXUART_UFCR_TXTL_SHIFT;
	reg |= IMX_RXFIFO_LEVEL << IMXUART_UFCR_RXTL_SHIFT;
	SETREG(bas, REG(UFCR), reg);
}

static void
imx_uart_term(struct uart_bas *bas)
{

}

static void
imx_uart_putc(struct uart_bas *bas, int c)
{

	while (!(IS(bas, USR1, TRDY)))
		;
	SETREG(bas, REG(UTXD), c);
}

static int
imx_uart_rxready(struct uart_bas *bas)
{

	return ((IS(bas, USR2, RDR)) ? 1 : 0);
}

static int
imx_uart_getc(struct uart_bas *bas, struct mtx *hwmtx)
{
	int c;

	uart_lock(hwmtx);
	while (!(IS(bas, USR2, RDR)))
		;

	c = GETREG(bas, REG(URXD));
	uart_unlock(hwmtx);
#if defined(KDB)
	if (c & FLD(URXD, BRK)) {
		if (kdb_break())
			return (0);
	}
#endif
	return (c & 0xff);
}

/*
 * High-level UART interface.
 */
struct imx_uart_softc {
	struct uart_softc base;
};

static int imx_uart_bus_attach(struct uart_softc *);
static int imx_uart_bus_detach(struct uart_softc *);
static int imx_uart_bus_flush(struct uart_softc *, int);
static int imx_uart_bus_getsig(struct uart_softc *);
static int imx_uart_bus_ioctl(struct uart_softc *, int, intptr_t);
static int imx_uart_bus_ipend(struct uart_softc *);
static int imx_uart_bus_param(struct uart_softc *, int, int, int, int);
static int imx_uart_bus_probe(struct uart_softc *);
static int imx_uart_bus_receive(struct uart_softc *);
static int imx_uart_bus_setsig(struct uart_softc *, int);
static int imx_uart_bus_transmit(struct uart_softc *);
static void imx_uart_bus_grab(struct uart_softc *);
static void imx_uart_bus_ungrab(struct uart_softc *);

static kobj_method_t imx_uart_methods[] = {
	KOBJMETHOD(uart_attach,		imx_uart_bus_attach),
	KOBJMETHOD(uart_detach,		imx_uart_bus_detach),
	KOBJMETHOD(uart_flush,		imx_uart_bus_flush),
	KOBJMETHOD(uart_getsig,		imx_uart_bus_getsig),
	KOBJMETHOD(uart_ioctl,		imx_uart_bus_ioctl),
	KOBJMETHOD(uart_ipend,		imx_uart_bus_ipend),
	KOBJMETHOD(uart_param,		imx_uart_bus_param),
	KOBJMETHOD(uart_probe,		imx_uart_bus_probe),
	KOBJMETHOD(uart_receive,	imx_uart_bus_receive),
	KOBJMETHOD(uart_setsig,		imx_uart_bus_setsig),
	KOBJMETHOD(uart_transmit,	imx_uart_bus_transmit),
	KOBJMETHOD(uart_grab,		imx_uart_bus_grab),
	KOBJMETHOD(uart_ungrab,		imx_uart_bus_ungrab),
	{ 0, 0 }
};

static struct uart_class uart_imx_class = {
	"imx",
	imx_uart_methods,
	sizeof(struct imx_uart_softc),
	.uc_ops = &uart_imx_uart_ops,
	.uc_range = 0x100,
	.uc_rclk = 24000000, /* TODO: get value from CCM */
	.uc_rshift = 0
};

static struct ofw_compat_data compat_data[] = {
	{"fsl,imx6q-uart",	(uintptr_t)&uart_imx_class},
	{"fsl,imx53-uart",	(uintptr_t)&uart_imx_class},
	{"fsl,imx51-uart",	(uintptr_t)&uart_imx_class},
	{"fsl,imx31-uart",	(uintptr_t)&uart_imx_class},
	{"fsl,imx27-uart",	(uintptr_t)&uart_imx_class},
	{"fsl,imx25-uart",	(uintptr_t)&uart_imx_class},
	{"fsl,imx21-uart",	(uintptr_t)&uart_imx_class},
	{NULL,			(uintptr_t)NULL},
};
UART_FDT_CLASS_AND_DEVICE(compat_data);

#define	SIGCHG(c, i, s, d)				\
	if (c) {					\
		i |= (i & s) ? s : s | d;		\
	} else {					\
		i = (i & s) ? (i & ~s) | d : i;		\
	}

static int
imx_uart_bus_attach(struct uart_softc *sc)
{
	struct uart_bas *bas;
	struct uart_devinfo *di;

	bas = &sc->sc_bas;
	if (sc->sc_sysdev != NULL) {
		di = sc->sc_sysdev;
		imx_uart_init(bas, di->baudrate, di->databits, di->stopbits,
		    di->parity);
	} else {
		imx_uart_init(bas, 115200, 8, 1, 0);
	}

	(void)imx_uart_bus_getsig(sc);

	/* Clear all pending interrupts. */
	SETREG(bas, REG(USR1), 0xffff);
	SETREG(bas, REG(USR2), 0xffff);

	DIS(bas, UCR4, DREN);
	ENA(bas, UCR1, RRDYEN);
	DIS(bas, UCR1, IDEN);
	DIS(bas, UCR3, RXDSEN);
	ENA(bas, UCR2, ATEN);
	DIS(bas, UCR1, TXMPTYEN);
	DIS(bas, UCR1, TRDYEN);
	DIS(bas, UCR4, TCEN);
	DIS(bas, UCR4, OREN);
	ENA(bas, UCR4, BKEN);
	DIS(bas, UCR4, WKEN);
	DIS(bas, UCR1, ADEN);
	DIS(bas, UCR3, ACIEN);
	DIS(bas, UCR2, ESCI);
	DIS(bas, UCR4, ENIRI);
	DIS(bas, UCR3, AIRINTEN);
	DIS(bas, UCR3, AWAKEN);
	DIS(bas, UCR3, FRAERREN);
	DIS(bas, UCR3, PARERREN);
	DIS(bas, UCR1, RTSDEN);
	DIS(bas, UCR2, RTSEN);
	DIS(bas, UCR3, DTREN);
	DIS(bas, UCR3, RI);
	DIS(bas, UCR3, DCD);
	DIS(bas, UCR3, DTRDEN);
	ENA(bas, UCR2, IRTS);
	ENA(bas, UCR3, RXDMUXSEL);

	return (0);
}

static int
imx_uart_bus_detach(struct uart_softc *sc)
{

	SETREG(&sc->sc_bas, REG(UCR4), 0);

	return (0);
}

static int
imx_uart_bus_flush(struct uart_softc *sc, int what)
{

	/* TODO */
	return (0);
}

static int
imx_uart_bus_getsig(struct uart_softc *sc)
{
	uint32_t new, old, sig;
	uint8_t bes;

	do {
		old = sc->sc_hwsig;
		sig = old;
		uart_lock(sc->sc_hwmtx);
		bes = GETREG(&sc->sc_bas, REG(USR2));
		uart_unlock(sc->sc_hwmtx);
		/* XXX: chip can show delta */
		SIGCHG(bes & FLD(USR2, DCDIN), sig, SER_DCD, SER_DDCD);
		new = sig & ~SER_MASK_DELTA;
	} while (!atomic_cmpset_32(&sc->sc_hwsig, old, new));

	return (sig);
}

static int
imx_uart_bus_ioctl(struct uart_softc *sc, int request, intptr_t data)
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
		*(u_int*)data = imx_uart_getbaud(bas);
		break;
	default:
		error = EINVAL;
		break;
	}
	uart_unlock(sc->sc_hwmtx);

	return (error);
}

static int
imx_uart_bus_ipend(struct uart_softc *sc)
{
	struct uart_bas *bas;
	int ipend;
	uint32_t usr1, usr2;
	uint32_t ucr1, ucr2, ucr4;

	bas = &sc->sc_bas;
	ipend = 0;

	uart_lock(sc->sc_hwmtx);

	/* Read pending interrupts */
	usr1 = GETREG(bas, REG(USR1));
	usr2 = GETREG(bas, REG(USR2));
	/* ACK interrupts */
	SETREG(bas, REG(USR1), usr1);
	SETREG(bas, REG(USR2), usr2);

	ucr1 = GETREG(bas, REG(UCR1));
	ucr2 = GETREG(bas, REG(UCR2));
	ucr4 = GETREG(bas, REG(UCR4));

	/* If we have reached tx low-water, we can tx some more now. */
	if ((usr1 & FLD(USR1, TRDY)) && (ucr1 & FLD(UCR1, TRDYEN))) {
		DIS(bas, UCR1, TRDYEN);
		ipend |= SER_INT_TXIDLE;
	}

	/*
	 * If we have reached the rx high-water, or if there are bytes in the rx
	 * fifo and no new data has arrived for 8 character periods (aging
	 * timer), we have input data to process.
	 */
	if (((usr1 & FLD(USR1, RRDY)) && (ucr1 & FLD(UCR1, RRDYEN))) || 
	    ((usr1 & FLD(USR1, AGTIM)) && (ucr2 & FLD(UCR2, ATEN)))) {
		DIS(bas, UCR1, RRDYEN);
		DIS(bas, UCR2, ATEN);
		ipend |= SER_INT_RXREADY;
	}

	/* A break can come in at any time, it never gets disabled. */
	if ((usr2 & FLD(USR2, BRCD)) && (ucr4 & FLD(UCR4, BKEN)))
		ipend |= SER_INT_BREAK;

	uart_unlock(sc->sc_hwmtx);

	return (ipend);
}

static int
imx_uart_bus_param(struct uart_softc *sc, int baudrate, int databits,
    int stopbits, int parity)
{

	uart_lock(sc->sc_hwmtx);
	imx_uart_init(&sc->sc_bas, baudrate, databits, stopbits, parity);
	uart_unlock(sc->sc_hwmtx);
	return (0);
}

static int
imx_uart_bus_probe(struct uart_softc *sc)
{
	int error;

	error = imx_uart_probe(&sc->sc_bas);
	if (error)
		return (error);

	/*
	 * On input we can read up to the full fifo size at once.  On output, we
	 * want to write only as much as the programmed tx low water level,
	 * because that's all we can be certain we have room for in the fifo
	 * when we get a tx-ready interrupt.
	 */
	sc->sc_rxfifosz = IMX_FIFOSZ;
	sc->sc_txfifosz = IMX_TXFIFO_LEVEL;

	device_set_desc(sc->sc_dev, "Freescale i.MX UART");
	return (0);
}

static int
imx_uart_bus_receive(struct uart_softc *sc)
{
	struct uart_bas *bas;
	int xc, out;

	bas = &sc->sc_bas;
	uart_lock(sc->sc_hwmtx);

	/*
	 * Empty the rx fifo.  We get the RRDY interrupt when IMX_RXFIFO_LEVEL
	 * (the rx high-water level) is reached, but we set sc_rxfifosz to the
	 * full hardware fifo size, so we can safely process however much is
	 * there, not just the highwater size.
	 */
	while (IS(bas, USR2, RDR)) {
		if (uart_rx_full(sc)) {
			/* No space left in input buffer */
			sc->sc_rxbuf[sc->sc_rxput] = UART_STAT_OVERRUN;
			break;
		}
		xc = GETREG(bas, REG(URXD));
		out = xc & 0x000000ff;
		if (xc & FLD(URXD, FRMERR))
			out |= UART_STAT_FRAMERR;
		if (xc & FLD(URXD, PRERR))
			out |= UART_STAT_PARERR;
		if (xc & FLD(URXD, OVRRUN))
			out |= UART_STAT_OVERRUN;
		if (xc & FLD(URXD, BRK))
			out |= UART_STAT_BREAK;

		uart_rx_put(sc, out);
	}
	ENA(bas, UCR1, RRDYEN);
	ENA(bas, UCR2, ATEN);

	uart_unlock(sc->sc_hwmtx);
	return (0);
}

static int
imx_uart_bus_setsig(struct uart_softc *sc, int sig)
{

	return (0);
}

static int
imx_uart_bus_transmit(struct uart_softc *sc)
{
	struct uart_bas *bas = &sc->sc_bas;
	int i;

	bas = &sc->sc_bas;
	uart_lock(sc->sc_hwmtx);

	/*
	 * Fill the tx fifo.  The uart core puts at most IMX_TXFIFO_LEVEL bytes
	 * into the txbuf (because that's what sc_txfifosz is set to), and
	 * because we got the TRDY (low-water reached) interrupt we know at
	 * least that much space is available in the fifo.
	 */
	for (i = 0; i < sc->sc_txdatasz; i++) {
		SETREG(bas, REG(UTXD), sc->sc_txbuf[i] & 0xff);
	}
	sc->sc_txbusy = 1;
	ENA(bas, UCR1, TRDYEN);

	uart_unlock(sc->sc_hwmtx);

	return (0);
}

static void
imx_uart_bus_grab(struct uart_softc *sc)
{
	struct uart_bas *bas = &sc->sc_bas;

	bas = &sc->sc_bas;
	uart_lock(sc->sc_hwmtx);
	DIS(bas, UCR1, RRDYEN);
	DIS(bas, UCR2, ATEN);
	uart_unlock(sc->sc_hwmtx);
}

static void
imx_uart_bus_ungrab(struct uart_softc *sc)
{
	struct uart_bas *bas = &sc->sc_bas;

	bas = &sc->sc_bas;
	uart_lock(sc->sc_hwmtx);
	ENA(bas, UCR1, RRDYEN);
	ENA(bas, UCR2, ATEN);
	uart_unlock(sc->sc_hwmtx);
}
