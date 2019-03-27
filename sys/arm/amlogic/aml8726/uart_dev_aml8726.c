/*-
 * Copyright 2013-2015 John Wehle <john@feith.com>
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
 * Amlogic aml8726 UART driver.
 *
 * The current implementation only targets features common to all
 * uarts.  For example ... though UART A as a 128 byte FIFO, the
 * others only have a 64 byte FIFO.
 *
 * Also, it's assumed that the USE_XTAL_CLK feature (available on
 * the aml8726-m6 and later) has not been activated.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/conf.h>
#include <sys/kernel.h>
#include <sys/sysctl.h>

#include <machine/bus.h>
#include <machine/cpu.h>

#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>

#include <dev/uart/uart.h>
#include <dev/uart/uart_cpu.h>
#include <dev/uart/uart_cpu_fdt.h>
#include <dev/uart/uart_bus.h>

#include <arm/amlogic/aml8726/aml8726_soc.h>
#include <arm/amlogic/aml8726/aml8726_uart.h>

#include "uart_if.h"

#undef	uart_getreg
#undef	uart_setreg

#define	uart_getreg(bas, reg)		\
    bus_space_read_4((bas)->bst, (bas)->bsh, reg)
#define	uart_setreg(bas, reg, value)	\
    bus_space_write_4((bas)->bst, (bas)->bsh, reg, value)

#define	SIGCHG(c, i, s, d)				\
	do {						\
		if (c) {				\
			i |= (i & s) ? s : s | d;	\
		} else {				\
			i = (i & s) ? (i & ~s) | d : i;	\
		}					\
	} while (0)

static int
aml8726_uart_divisor(int rclk, int baudrate)
{
	int actual_baud, divisor;
	int error;

	if (baudrate == 0)
		return (0);

	/* integer version of (rclk / baudrate + .5) */
	divisor = ((rclk << 1) + baudrate) / (baudrate << 1);
	if (divisor == 0)
		return (0);
	actual_baud = rclk / divisor;

	/* 10 times error in percent: */
	error = (((actual_baud - baudrate) * 2000) / baudrate + 1) >> 1;

	/* 3.0% maximum error tolerance: */
	if (error < -30 || error > 30)
		return (0);

	return (divisor);
}

static int
aml8726_uart_param(struct uart_bas *bas, int baudrate, int databits, int stopbits,
    int parity)
{
	uint32_t cr;
	uint32_t mr;
	uint32_t nbr;
	int divisor;

	cr = uart_getreg(bas, AML_UART_CONTROL_REG);

	cr &= ~(AML_UART_CONTROL_DB_MASK | AML_UART_CONTROL_SB_MASK |
	    AML_UART_CONTROL_P_MASK);

	switch (databits) {
	case 5:		cr |= AML_UART_CONTROL_5_DB; break;
	case 6:		cr |= AML_UART_CONTROL_6_DB; break;
	case 7:		cr |= AML_UART_CONTROL_7_DB; break;
	case 8:		cr |= AML_UART_CONTROL_8_DB; break;
	default:	return (EINVAL);
	}

	switch (stopbits) {
	case 1:		cr |= AML_UART_CONTROL_1_SB; break;
	case 2:		cr |= AML_UART_CONTROL_2_SB; break;
	default:	return (EINVAL);
	}

	switch (parity) {
	case UART_PARITY_EVEN:	cr |= AML_UART_CONTROL_P_EVEN;
				cr |= AML_UART_CONTROL_P_EN;
				break;

	case UART_PARITY_ODD:	cr |= AML_UART_CONTROL_P_ODD;
				cr |= AML_UART_CONTROL_P_EN;
				break;

	case UART_PARITY_NONE:	break;

	default:	return (EINVAL);
	}

	/* Set baudrate. */
	if (baudrate > 0 && bas->rclk != 0) {
		divisor = aml8726_uart_divisor(bas->rclk / 4, baudrate) - 1;

		switch (aml8726_soc_hw_rev) {
		case AML_SOC_HW_REV_M6:
		case AML_SOC_HW_REV_M8:
		case AML_SOC_HW_REV_M8B:
			if (divisor > (AML_UART_NEW_BAUD_RATE_MASK >>
			    AML_UART_NEW_BAUD_RATE_SHIFT))
				return (EINVAL);

			nbr = uart_getreg(bas, AML_UART_NEW_BAUD_REG);
			nbr &= ~(AML_UART_NEW_BAUD_USE_XTAL_CLK |
			    AML_UART_NEW_BAUD_RATE_MASK);
			nbr |= AML_UART_NEW_BAUD_RATE_EN |
			    (divisor << AML_UART_NEW_BAUD_RATE_SHIFT);
			uart_setreg(bas, AML_UART_NEW_BAUD_REG, nbr);

			divisor = 0;
			break;
		default:
			if (divisor > 0xffff)
				return (EINVAL);
			break;
		}

		cr &= ~AML_UART_CONTROL_BAUD_MASK;
		cr |= (divisor & AML_UART_CONTROL_BAUD_MASK);

		divisor >>= AML_UART_CONTROL_BAUD_WIDTH;

		mr = uart_getreg(bas, AML_UART_MISC_REG);
		mr &= ~(AML_UART_MISC_OLD_RX_BAUD |
		    AML_UART_MISC_BAUD_EXT_MASK);
		mr |= ((divisor << AML_UART_MISC_BAUD_EXT_SHIFT) &
		    AML_UART_MISC_BAUD_EXT_MASK);
		uart_setreg(bas, AML_UART_MISC_REG, mr);
	}

	uart_setreg(bas, AML_UART_CONTROL_REG, cr);
	uart_barrier(bas);

	return (0);
}

/*
 * Low-level UART interface.
 */

static int
aml8726_uart_probe(struct uart_bas *bas)
{

	return (0);
}

static void
aml8726_uart_init(struct uart_bas *bas, int baudrate, int databits, int stopbits,
    int parity)
{
	uint32_t cr;
	uint32_t mr;

	(void)aml8726_uart_param(bas, baudrate, databits, stopbits, parity);

	cr = uart_getreg(bas, AML_UART_CONTROL_REG);
	/* Disable all interrupt sources. */
	cr &= ~(AML_UART_CONTROL_TX_INT_EN | AML_UART_CONTROL_RX_INT_EN);
	/* Reset the transmitter and receiver. */
	cr |= (AML_UART_CONTROL_TX_RST | AML_UART_CONTROL_RX_RST);
	/* Enable the transmitter and receiver. */
	cr |= (AML_UART_CONTROL_TX_EN | AML_UART_CONTROL_RX_EN);
	uart_setreg(bas, AML_UART_CONTROL_REG, cr);
	uart_barrier(bas);

	/* Clear RX FIFO level for generating interrupts. */
	mr = uart_getreg(bas, AML_UART_MISC_REG);
	mr &= ~AML_UART_MISC_RECV_IRQ_CNT_MASK;
	uart_setreg(bas, AML_UART_MISC_REG, mr);
	uart_barrier(bas);

	/* Ensure the reset bits are clear. */
	cr &= ~(AML_UART_CONTROL_TX_RST | AML_UART_CONTROL_RX_RST);
	uart_setreg(bas, AML_UART_CONTROL_REG, cr);
	uart_barrier(bas);
}

static void
aml8726_uart_term(struct uart_bas *bas)
{
}

static void
aml8726_uart_putc(struct uart_bas *bas, int c)
{

	while ((uart_getreg(bas, AML_UART_STATUS_REG) &
	    AML_UART_STATUS_TX_FIFO_FULL) != 0)
		cpu_spinwait();

	uart_setreg(bas, AML_UART_WFIFO_REG, c);
	uart_barrier(bas);
}

static int
aml8726_uart_rxready(struct uart_bas *bas)
{

	return ((uart_getreg(bas, AML_UART_STATUS_REG) &
	    AML_UART_STATUS_RX_FIFO_EMPTY) == 0 ? 1 : 0);
}

static int
aml8726_uart_getc(struct uart_bas *bas, struct mtx *hwmtx)
{
	int c;

	uart_lock(hwmtx);

	while ((uart_getreg(bas, AML_UART_STATUS_REG) &
	    AML_UART_STATUS_RX_FIFO_EMPTY) != 0) {
		uart_unlock(hwmtx);
		DELAY(4);
		uart_lock(hwmtx);
	}

	c = uart_getreg(bas, AML_UART_RFIFO_REG) & 0xff;

	uart_unlock(hwmtx);

	return (c);
}

struct uart_ops aml8726_uart_ops = {
	.probe = aml8726_uart_probe,
	.init = aml8726_uart_init,
	.term = aml8726_uart_term,
	.putc = aml8726_uart_putc,
	.rxready = aml8726_uart_rxready,
	.getc = aml8726_uart_getc,
};

static unsigned int
aml8726_uart_bus_clk(phandle_t node)
{
	pcell_t prop;
	ssize_t len;
	phandle_t clk_node;

	len = OF_getencprop(node, "clocks", &prop, sizeof(prop));
	if ((len / sizeof(prop)) != 1 || prop == 0 ||
	    (clk_node = OF_node_from_xref(prop)) == 0)
		return (0);

	len = OF_getencprop(clk_node, "clock-frequency", &prop, sizeof(prop));
	if ((len / sizeof(prop)) != 1 || prop == 0)
		return (0);

	return ((unsigned int)prop);
}

static int
aml8726_uart_bus_probe(struct uart_softc *sc)
{
	int error;

	error = aml8726_uart_probe(&sc->sc_bas);
	if (error)
		return (error);

	sc->sc_rxfifosz = 64;
	sc->sc_txfifosz = 64;
	sc->sc_hwiflow = 1;
	sc->sc_hwoflow = 1;

	device_set_desc(sc->sc_dev, "Amlogic aml8726 UART");

	return (0);
}

static int
aml8726_uart_bus_getsig(struct uart_softc *sc)
{
	uint32_t new, old, sig;

	/*
	 * Treat DSR, DCD, and CTS as always on.
	 */

	do {
		old = sc->sc_hwsig;
		sig = old;
		SIGCHG(1, sig, SER_DSR, SER_DDSR);
		SIGCHG(1, sig, SER_DCD, SER_DDCD);
		SIGCHG(1, sig, SER_CTS, SER_DCTS);
		new = sig & ~SER_MASK_DELTA;
	} while (!atomic_cmpset_32(&sc->sc_hwsig, old, new));

	return (sig);
}

static int
aml8726_uart_bus_setsig(struct uart_softc *sc, int sig)
{
	uint32_t new, old;

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

	return (0);
}

static int
aml8726_uart_bus_attach(struct uart_softc *sc)
{
	struct uart_bas *bas;
	uint32_t cr;
	uint32_t mr;

	bas = &sc->sc_bas;

	bas->rclk = aml8726_uart_bus_clk(ofw_bus_get_node(sc->sc_dev));

	if (bas->rclk == 0) {
		device_printf(sc->sc_dev, "missing clocks attribute in FDT\n");
		return (ENXIO);
	}

	cr = uart_getreg(bas, AML_UART_CONTROL_REG);
	/* Disable all interrupt sources. */
	cr &= ~(AML_UART_CONTROL_TX_INT_EN | AML_UART_CONTROL_RX_INT_EN);
	/* Ensure the reset bits are clear. */
	cr &= ~(AML_UART_CONTROL_TX_RST | AML_UART_CONTROL_RX_RST);

	/*
	 * Reset the transmitter and receiver only if not acting as a
	 * console, otherwise it means that:
	 *
	 * 1) aml8726_uart_init was already called which did the reset
	 *
	 * 2) there may be console bytes sitting in the transmit fifo
	 */
	if (sc->sc_sysdev != NULL && sc->sc_sysdev->type == UART_DEV_CONSOLE)
		;
	else
		cr |= (AML_UART_CONTROL_TX_RST | AML_UART_CONTROL_RX_RST);

	/* Default to two wire mode. */
	cr |= AML_UART_CONTROL_TWO_WIRE_EN;
	/* Enable the transmitter and receiver. */
	cr |= (AML_UART_CONTROL_TX_EN | AML_UART_CONTROL_RX_EN);
	/* Reset error bits. */
	cr |= AML_UART_CONTROL_CLR_ERR;
	uart_setreg(bas, AML_UART_CONTROL_REG, cr);
	uart_barrier(bas);

	/* Set FIFO levels for generating interrupts. */
	mr = uart_getreg(bas, AML_UART_MISC_REG);
	mr &= ~AML_UART_MISC_XMIT_IRQ_CNT_MASK;
	mr |= (0 << AML_UART_MISC_XMIT_IRQ_CNT_SHIFT);
	mr &= ~AML_UART_MISC_RECV_IRQ_CNT_MASK;
	mr |= (1 << AML_UART_MISC_RECV_IRQ_CNT_SHIFT);
	uart_setreg(bas, AML_UART_MISC_REG, mr);
	uart_barrier(bas);

	aml8726_uart_bus_getsig(sc);

	/* Ensure the reset bits are clear. */
	cr &= ~(AML_UART_CONTROL_TX_RST | AML_UART_CONTROL_RX_RST);
	cr &= ~AML_UART_CONTROL_CLR_ERR;
	/* Enable the receive interrupt. */
	cr |= AML_UART_CONTROL_RX_INT_EN;
	uart_setreg(bas, AML_UART_CONTROL_REG, cr);
	uart_barrier(bas);

	return (0);
}

static int
aml8726_uart_bus_detach(struct uart_softc *sc)
{
	struct uart_bas *bas;
	uint32_t cr;
	uint32_t mr;

	bas = &sc->sc_bas;

	/* Disable all interrupt sources. */
	cr = uart_getreg(bas, AML_UART_CONTROL_REG);
	cr &= ~(AML_UART_CONTROL_TX_INT_EN | AML_UART_CONTROL_RX_INT_EN);
	uart_setreg(bas, AML_UART_CONTROL_REG, cr);
	uart_barrier(bas);

	/* Clear RX FIFO level for generating interrupts. */
	mr = uart_getreg(bas, AML_UART_MISC_REG);
	mr &= ~AML_UART_MISC_RECV_IRQ_CNT_MASK;
	uart_setreg(bas, AML_UART_MISC_REG, mr);
	uart_barrier(bas);

	return (0);
}

static int
aml8726_uart_bus_flush(struct uart_softc *sc, int what)
{
	struct uart_bas *bas;
	uint32_t cr;

	bas = &sc->sc_bas;
	uart_lock(sc->sc_hwmtx);

	cr = uart_getreg(bas, AML_UART_CONTROL_REG);
	if (what & UART_FLUSH_TRANSMITTER)
		cr |= AML_UART_CONTROL_TX_RST;
	if (what & UART_FLUSH_RECEIVER)
		cr |= AML_UART_CONTROL_RX_RST;
	uart_setreg(bas, AML_UART_CONTROL_REG, cr);
	uart_barrier(bas);

	/* Ensure the reset bits are clear. */
	cr &= ~(AML_UART_CONTROL_TX_RST | AML_UART_CONTROL_RX_RST);
	uart_setreg(bas, AML_UART_CONTROL_REG, cr);
	uart_barrier(bas);

	uart_unlock(sc->sc_hwmtx);

	return (0);
}

static int
aml8726_uart_bus_ioctl(struct uart_softc *sc, int request, intptr_t data)
{
	struct uart_bas *bas;
	int baudrate, divisor, error;
	uint32_t cr, mr, nbr;

	bas = &sc->sc_bas;
	uart_lock(sc->sc_hwmtx);

	error = 0;
	switch (request) {
	case UART_IOCTL_BAUD:
		cr = uart_getreg(bas, AML_UART_CONTROL_REG);
		cr &= AML_UART_CONTROL_BAUD_MASK;

		mr = uart_getreg(bas, AML_UART_MISC_REG);
		mr &= AML_UART_MISC_BAUD_EXT_MASK;

		divisor = ((mr >> AML_UART_MISC_BAUD_EXT_SHIFT) <<
		    AML_UART_CONTROL_BAUD_WIDTH) | cr;

		switch (aml8726_soc_hw_rev) {
		case AML_SOC_HW_REV_M6:
		case AML_SOC_HW_REV_M8:
		case AML_SOC_HW_REV_M8B:
			nbr = uart_getreg(bas, AML_UART_NEW_BAUD_REG);
			if ((nbr & AML_UART_NEW_BAUD_RATE_EN) != 0) {
				divisor = (nbr & AML_UART_NEW_BAUD_RATE_MASK) >>
				    AML_UART_NEW_BAUD_RATE_SHIFT;
			}
			break;
		default:
			break;
		}

		baudrate = bas->rclk / 4 / (divisor + 1);
		if (baudrate > 0)
			*(int*)data = baudrate;
		else
			error = ENXIO;
		break;

	case UART_IOCTL_IFLOW:
	case UART_IOCTL_OFLOW:
		cr = uart_getreg(bas, AML_UART_CONTROL_REG);
		if (data)
			cr &= ~AML_UART_CONTROL_TWO_WIRE_EN;
		else
			cr |= AML_UART_CONTROL_TWO_WIRE_EN;
		uart_setreg(bas, AML_UART_CONTROL_REG, cr);
		break;

	default:
		error = EINVAL;
		break;
	}

	uart_unlock(sc->sc_hwmtx);

	return (error);
}

static int
aml8726_uart_bus_ipend(struct uart_softc *sc)
{
	struct uart_bas *bas;
	int ipend;
	uint32_t sr;
	uint32_t cr;

	bas = &sc->sc_bas;
	uart_lock(sc->sc_hwmtx);

	ipend = 0;
	sr = uart_getreg(bas, AML_UART_STATUS_REG);
	cr = uart_getreg(bas, AML_UART_CONTROL_REG);

	if ((sr & AML_UART_STATUS_RX_FIFO_OVERFLOW) != 0)
		ipend |= SER_INT_OVERRUN;

	if ((sr & AML_UART_STATUS_TX_FIFO_EMPTY) != 0 &&
	    (cr & AML_UART_CONTROL_TX_INT_EN) != 0) {
		ipend |= SER_INT_TXIDLE;

		cr &= ~AML_UART_CONTROL_TX_INT_EN;
		uart_setreg(bas, AML_UART_CONTROL_REG, cr);
		uart_barrier(bas);
	}

	if ((sr & AML_UART_STATUS_RX_FIFO_EMPTY) == 0)
		ipend |= SER_INT_RXREADY;

	uart_unlock(sc->sc_hwmtx);

	return (ipend);
}

static int
aml8726_uart_bus_param(struct uart_softc *sc, int baudrate, int databits,
    int stopbits, int parity)
{
	struct uart_bas *bas;
	int error;

	bas = &sc->sc_bas;
	uart_lock(sc->sc_hwmtx);

	error = aml8726_uart_param(bas, baudrate, databits, stopbits, parity);

	uart_unlock(sc->sc_hwmtx);

	return (error);
}

static int
aml8726_uart_bus_receive(struct uart_softc *sc)
{
	struct uart_bas *bas;
	int xc;
	uint32_t sr;

	bas = &sc->sc_bas;
	uart_lock(sc->sc_hwmtx);

	sr = uart_getreg(bas, AML_UART_STATUS_REG);
	while ((sr & AML_UART_STATUS_RX_FIFO_EMPTY) == 0) {
		if (uart_rx_full(sc)) {
			sc->sc_rxbuf[sc->sc_rxput] = UART_STAT_OVERRUN;
			break;
		}
		xc = uart_getreg(bas, AML_UART_RFIFO_REG) & 0xff;
		if (sr & AML_UART_STATUS_FRAME_ERR)
			xc |= UART_STAT_FRAMERR;
		if (sr & AML_UART_STATUS_PARITY_ERR)
			xc |= UART_STAT_PARERR;
		uart_rx_put(sc, xc);
		sr = uart_getreg(bas, AML_UART_STATUS_REG);
	}
	/* Discard everything left in the RX FIFO. */
	while ((sr & AML_UART_STATUS_RX_FIFO_EMPTY) == 0) {
		(void)uart_getreg(bas, AML_UART_RFIFO_REG);
		sr = uart_getreg(bas, AML_UART_STATUS_REG);
	}
	/* Reset error bits */
	if ((sr & (AML_UART_STATUS_FRAME_ERR | AML_UART_STATUS_PARITY_ERR)) != 0) {
		uart_setreg(bas, AML_UART_CONTROL_REG,
		    (uart_getreg(bas, AML_UART_CONTROL_REG) |
		    AML_UART_CONTROL_CLR_ERR));
		uart_barrier(bas);
		uart_setreg(bas, AML_UART_CONTROL_REG,
		    (uart_getreg(bas, AML_UART_CONTROL_REG) &
		    ~AML_UART_CONTROL_CLR_ERR));
		uart_barrier(bas);
	}

	uart_unlock(sc->sc_hwmtx);

	return (0);
}

static int
aml8726_uart_bus_transmit(struct uart_softc *sc)
{
	struct uart_bas *bas;
	int i;
	uint32_t cr;

	bas = &sc->sc_bas;
	uart_lock(sc->sc_hwmtx);

	/*
	 * Wait for sufficient space since aml8726_uart_putc
	 * may have been called after SER_INT_TXIDLE occurred.
	 */
	while ((uart_getreg(bas, AML_UART_STATUS_REG) &
	    AML_UART_STATUS_TX_FIFO_EMPTY) == 0)
		cpu_spinwait();

	for (i = 0; i < sc->sc_txdatasz; i++) {
		uart_setreg(bas, AML_UART_WFIFO_REG, sc->sc_txbuf[i]);
		uart_barrier(bas);
	}

	sc->sc_txbusy = 1;

	cr = uart_getreg(bas, AML_UART_CONTROL_REG);
	cr |= AML_UART_CONTROL_TX_INT_EN;
	uart_setreg(bas, AML_UART_CONTROL_REG, cr);
	uart_barrier(bas);

	uart_unlock(sc->sc_hwmtx);

	return (0);
}

static void
aml8726_uart_bus_grab(struct uart_softc *sc)
{
	struct uart_bas *bas;
	uint32_t cr;

	/*
	 * Disable the receive interrupt to avoid a race between
	 * aml8726_uart_getc and aml8726_uart_bus_receive which
	 * can trigger:
	 *
	 *   panic: bad stray interrupt
	 *
	 * due to the RX FIFO receiving a character causing an
	 * interrupt which gets serviced after aml8726_uart_getc
	 * has been called (meaning the RX FIFO is now empty).
	 */

	bas = &sc->sc_bas;
	uart_lock(sc->sc_hwmtx);

	cr = uart_getreg(bas, AML_UART_CONTROL_REG);
	cr &= ~AML_UART_CONTROL_RX_INT_EN;
	uart_setreg(bas, AML_UART_CONTROL_REG, cr);
	uart_barrier(bas);

	uart_unlock(sc->sc_hwmtx);
}

static void
aml8726_uart_bus_ungrab(struct uart_softc *sc)
{
	struct uart_bas *bas;
	uint32_t cr;
	uint32_t mr;

	/*
	 * The RX FIFO level being set indicates that the device
	 * is currently attached meaning the receive interrupt
	 * should be enabled.
	 */

	bas = &sc->sc_bas;
	uart_lock(sc->sc_hwmtx);

	mr = uart_getreg(bas, AML_UART_MISC_REG);
	mr &= AML_UART_MISC_RECV_IRQ_CNT_MASK;

	if (mr != 0) {
		cr = uart_getreg(bas, AML_UART_CONTROL_REG);
		cr |= AML_UART_CONTROL_RX_INT_EN;
		uart_setreg(bas, AML_UART_CONTROL_REG, cr);
		uart_barrier(bas);
	}

	uart_unlock(sc->sc_hwmtx);
}

static kobj_method_t aml8726_uart_methods[] = {
	KOBJMETHOD(uart_probe,		aml8726_uart_bus_probe),
	KOBJMETHOD(uart_attach,		aml8726_uart_bus_attach),
	KOBJMETHOD(uart_detach,		aml8726_uart_bus_detach),
	KOBJMETHOD(uart_flush,		aml8726_uart_bus_flush),
	KOBJMETHOD(uart_getsig,		aml8726_uart_bus_getsig),
	KOBJMETHOD(uart_setsig,		aml8726_uart_bus_setsig),
	KOBJMETHOD(uart_ioctl,		aml8726_uart_bus_ioctl),
	KOBJMETHOD(uart_ipend,		aml8726_uart_bus_ipend),
	KOBJMETHOD(uart_param,		aml8726_uart_bus_param),
	KOBJMETHOD(uart_receive,	aml8726_uart_bus_receive),
	KOBJMETHOD(uart_transmit,	aml8726_uart_bus_transmit),
	KOBJMETHOD(uart_grab,		aml8726_uart_bus_grab),
	KOBJMETHOD(uart_ungrab,		aml8726_uart_bus_ungrab),
	{ 0, 0 }
};

struct uart_class uart_aml8726_class = {
	"uart",
	aml8726_uart_methods,
	sizeof(struct uart_softc),
	.uc_ops = &aml8726_uart_ops,
	.uc_range = 24,
	.uc_rclk = 0,
	.uc_rshift = 0
};

static struct ofw_compat_data compat_data[] = {
	{ "amlogic,meson-uart",		(uintptr_t)&uart_aml8726_class },
	{ NULL,				(uintptr_t)NULL }
};
UART_FDT_CLASS_AND_DEVICE(compat_data);
