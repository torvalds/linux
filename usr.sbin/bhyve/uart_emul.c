/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2012 NetApp, Inc.
 * Copyright (c) 2013 Neel Natu <neel@freebsd.org>
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
 * THIS SOFTWARE IS PROVIDED BY NETAPP, INC ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL NETAPP, INC OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/types.h>
#include <dev/ic/ns16550.h>
#ifndef WITHOUT_CAPSICUM
#include <sys/capsicum.h>
#include <capsicum_helpers.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <termios.h>
#include <unistd.h>
#include <stdbool.h>
#include <string.h>
#include <pthread.h>
#include <sysexits.h>

#include "mevent.h"
#include "uart_emul.h"

#define	COM1_BASE      	0x3F8
#define	COM1_IRQ	4
#define	COM2_BASE      	0x2F8
#define	COM2_IRQ	3

#define	DEFAULT_RCLK	1843200
#define	DEFAULT_BAUD	9600

#define	FCR_RX_MASK	0xC0

#define	MCR_OUT1	0x04
#define	MCR_OUT2	0x08

#define	MSR_DELTA_MASK	0x0f

#ifndef REG_SCR
#define	REG_SCR		com_scr
#endif

#define	FIFOSZ	16

static bool uart_stdio;		/* stdio in use for i/o */
static struct termios tio_stdio_orig;

static struct {
	int	baseaddr;
	int	irq;
	bool	inuse;
} uart_lres[] = {
	{ COM1_BASE, COM1_IRQ, false},
	{ COM2_BASE, COM2_IRQ, false},
};

#define	UART_NLDEVS	(sizeof(uart_lres) / sizeof(uart_lres[0]))

struct fifo {
	uint8_t	buf[FIFOSZ];
	int	rindex;		/* index to read from */
	int	windex;		/* index to write to */
	int	num;		/* number of characters in the fifo */
	int	size;		/* size of the fifo */
};

struct ttyfd {
	bool	opened;
	int	fd;		/* tty device file descriptor */
	struct termios tio_orig, tio_new;    /* I/O Terminals */
};

struct uart_softc {
	pthread_mutex_t mtx;	/* protects all softc elements */
	uint8_t	data;		/* Data register (R/W) */
	uint8_t ier;		/* Interrupt enable register (R/W) */
	uint8_t lcr;		/* Line control register (R/W) */
	uint8_t mcr;		/* Modem control register (R/W) */
	uint8_t lsr;		/* Line status register (R/W) */
	uint8_t msr;		/* Modem status register (R/W) */
	uint8_t fcr;		/* FIFO control register (W) */
	uint8_t scr;		/* Scratch register (R/W) */

	uint8_t dll;		/* Baudrate divisor latch LSB */
	uint8_t dlh;		/* Baudrate divisor latch MSB */

	struct fifo rxfifo;
	struct mevent *mev;

	struct ttyfd tty;
	bool	thre_int_pending;	/* THRE interrupt pending */

	void	*arg;
	uart_intr_func_t intr_assert;
	uart_intr_func_t intr_deassert;
};

static void uart_drain(int fd, enum ev_type ev, void *arg);

static void
ttyclose(void)
{

	tcsetattr(STDIN_FILENO, TCSANOW, &tio_stdio_orig);
}

static void
ttyopen(struct ttyfd *tf)
{

	tcgetattr(tf->fd, &tf->tio_orig);

	tf->tio_new = tf->tio_orig;
	cfmakeraw(&tf->tio_new);
	tf->tio_new.c_cflag |= CLOCAL;
	tcsetattr(tf->fd, TCSANOW, &tf->tio_new);

	if (tf->fd == STDIN_FILENO) {
		tio_stdio_orig = tf->tio_orig;
		atexit(ttyclose);
	}
}

static int
ttyread(struct ttyfd *tf)
{
	unsigned char rb;

	if (read(tf->fd, &rb, 1) == 1)
		return (rb);
	else
		return (-1);
}

static void
ttywrite(struct ttyfd *tf, unsigned char wb)
{

	(void)write(tf->fd, &wb, 1);
}

static void
rxfifo_reset(struct uart_softc *sc, int size)
{
	char flushbuf[32];
	struct fifo *fifo;
	ssize_t nread;
	int error;

	fifo = &sc->rxfifo;
	bzero(fifo, sizeof(struct fifo));
	fifo->size = size;

	if (sc->tty.opened) {
		/*
		 * Flush any unread input from the tty buffer.
		 */
		while (1) {
			nread = read(sc->tty.fd, flushbuf, sizeof(flushbuf));
			if (nread != sizeof(flushbuf))
				break;
		}

		/*
		 * Enable mevent to trigger when new characters are available
		 * on the tty fd.
		 */
		error = mevent_enable(sc->mev);
		assert(error == 0);
	}
}

static int
rxfifo_available(struct uart_softc *sc)
{
	struct fifo *fifo;

	fifo = &sc->rxfifo;
	return (fifo->num < fifo->size);
}

static int
rxfifo_putchar(struct uart_softc *sc, uint8_t ch)
{
	struct fifo *fifo;
	int error;

	fifo = &sc->rxfifo;

	if (fifo->num < fifo->size) {
		fifo->buf[fifo->windex] = ch;
		fifo->windex = (fifo->windex + 1) % fifo->size;
		fifo->num++;
		if (!rxfifo_available(sc)) {
			if (sc->tty.opened) {
				/*
				 * Disable mevent callback if the FIFO is full.
				 */
				error = mevent_disable(sc->mev);
				assert(error == 0);
			}
		}
		return (0);
	} else
		return (-1);
}

static int
rxfifo_getchar(struct uart_softc *sc)
{
	struct fifo *fifo;
	int c, error, wasfull;

	wasfull = 0;
	fifo = &sc->rxfifo;
	if (fifo->num > 0) {
		if (!rxfifo_available(sc))
			wasfull = 1;
		c = fifo->buf[fifo->rindex];
		fifo->rindex = (fifo->rindex + 1) % fifo->size;
		fifo->num--;
		if (wasfull) {
			if (sc->tty.opened) {
				error = mevent_enable(sc->mev);
				assert(error == 0);
			}
		}
		return (c);
	} else
		return (-1);
}

static int
rxfifo_numchars(struct uart_softc *sc)
{
	struct fifo *fifo = &sc->rxfifo;

	return (fifo->num);
}

static void
uart_opentty(struct uart_softc *sc)
{

	ttyopen(&sc->tty);
	sc->mev = mevent_add(sc->tty.fd, EVF_READ, uart_drain, sc);
	assert(sc->mev != NULL);
}

static uint8_t
modem_status(uint8_t mcr)
{
	uint8_t msr;

	if (mcr & MCR_LOOPBACK) {
		/*
		 * In the loopback mode certain bits from the MCR are
		 * reflected back into MSR.
		 */
		msr = 0;
		if (mcr & MCR_RTS)
			msr |= MSR_CTS;
		if (mcr & MCR_DTR)
			msr |= MSR_DSR;
		if (mcr & MCR_OUT1)
			msr |= MSR_RI;
		if (mcr & MCR_OUT2)
			msr |= MSR_DCD;
	} else {
		/*
		 * Always assert DCD and DSR so tty open doesn't block
		 * even if CLOCAL is turned off.
		 */
		msr = MSR_DCD | MSR_DSR;
	}
	assert((msr & MSR_DELTA_MASK) == 0);

	return (msr);
}

/*
 * The IIR returns a prioritized interrupt reason:
 * - receive data available
 * - transmit holding register empty
 * - modem status change
 *
 * Return an interrupt reason if one is available.
 */
static int
uart_intr_reason(struct uart_softc *sc)
{

	if ((sc->lsr & LSR_OE) != 0 && (sc->ier & IER_ERLS) != 0)
		return (IIR_RLS);
	else if (rxfifo_numchars(sc) > 0 && (sc->ier & IER_ERXRDY) != 0)
		return (IIR_RXTOUT);
	else if (sc->thre_int_pending && (sc->ier & IER_ETXRDY) != 0)
		return (IIR_TXRDY);
	else if ((sc->msr & MSR_DELTA_MASK) != 0 && (sc->ier & IER_EMSC) != 0)
		return (IIR_MLSC);
	else
		return (IIR_NOPEND);
}

static void
uart_reset(struct uart_softc *sc)
{
	uint16_t divisor;

	divisor = DEFAULT_RCLK / DEFAULT_BAUD / 16;
	sc->dll = divisor;
	sc->dlh = divisor >> 16;
	sc->msr = modem_status(sc->mcr);

	rxfifo_reset(sc, 1);	/* no fifo until enabled by software */
}

/*
 * Toggle the COM port's intr pin depending on whether or not we have an
 * interrupt condition to report to the processor.
 */
static void
uart_toggle_intr(struct uart_softc *sc)
{
	uint8_t intr_reason;

	intr_reason = uart_intr_reason(sc);

	if (intr_reason == IIR_NOPEND)
		(*sc->intr_deassert)(sc->arg);
	else
		(*sc->intr_assert)(sc->arg);
}

static void
uart_drain(int fd, enum ev_type ev, void *arg)
{
	struct uart_softc *sc;
	int ch;

	sc = arg;	

	assert(fd == sc->tty.fd);
	assert(ev == EVF_READ);
	
	/*
	 * This routine is called in the context of the mevent thread
	 * to take out the softc lock to protect against concurrent
	 * access from a vCPU i/o exit
	 */
	pthread_mutex_lock(&sc->mtx);

	if ((sc->mcr & MCR_LOOPBACK) != 0) {
		(void) ttyread(&sc->tty);
	} else {
		while (rxfifo_available(sc) &&
		       ((ch = ttyread(&sc->tty)) != -1)) {
			rxfifo_putchar(sc, ch);
		}
		uart_toggle_intr(sc);
	}

	pthread_mutex_unlock(&sc->mtx);
}

void
uart_write(struct uart_softc *sc, int offset, uint8_t value)
{
	int fifosz;
	uint8_t msr;

	pthread_mutex_lock(&sc->mtx);

	/*
	 * Take care of the special case DLAB accesses first
	 */
	if ((sc->lcr & LCR_DLAB) != 0) {
		if (offset == REG_DLL) {
			sc->dll = value;
			goto done;
		}
		
		if (offset == REG_DLH) {
			sc->dlh = value;
			goto done;
		}
	}

        switch (offset) {
	case REG_DATA:
		if (sc->mcr & MCR_LOOPBACK) {
			if (rxfifo_putchar(sc, value) != 0)
				sc->lsr |= LSR_OE;
		} else if (sc->tty.opened) {
			ttywrite(&sc->tty, value);
		} /* else drop on floor */
		sc->thre_int_pending = true;
		break;
	case REG_IER:
		/* Set pending when IER_ETXRDY is raised (edge-triggered). */
		if ((sc->ier & IER_ETXRDY) == 0 && (value & IER_ETXRDY) != 0)
			sc->thre_int_pending = true;
		/*
		 * Apply mask so that bits 4-7 are 0
		 * Also enables bits 0-3 only if they're 1
		 */
		sc->ier = value & 0x0F;
		break;
		case REG_FCR:
			/*
			 * When moving from FIFO and 16450 mode and vice versa,
			 * the FIFO contents are reset.
			 */
			if ((sc->fcr & FCR_ENABLE) ^ (value & FCR_ENABLE)) {
				fifosz = (value & FCR_ENABLE) ? FIFOSZ : 1;
				rxfifo_reset(sc, fifosz);
			}

			/*
			 * The FCR_ENABLE bit must be '1' for the programming
			 * of other FCR bits to be effective.
			 */
			if ((value & FCR_ENABLE) == 0) {
				sc->fcr = 0;
			} else {
				if ((value & FCR_RCV_RST) != 0)
					rxfifo_reset(sc, FIFOSZ);

				sc->fcr = value &
					 (FCR_ENABLE | FCR_DMA | FCR_RX_MASK);
			}
			break;
		case REG_LCR:
			sc->lcr = value;
			break;
		case REG_MCR:
			/* Apply mask so that bits 5-7 are 0 */
			sc->mcr = value & 0x1F;
			msr = modem_status(sc->mcr);

			/*
			 * Detect if there has been any change between the
			 * previous and the new value of MSR. If there is
			 * then assert the appropriate MSR delta bit.
			 */
			if ((msr & MSR_CTS) ^ (sc->msr & MSR_CTS))
				sc->msr |= MSR_DCTS;
			if ((msr & MSR_DSR) ^ (sc->msr & MSR_DSR))
				sc->msr |= MSR_DDSR;
			if ((msr & MSR_DCD) ^ (sc->msr & MSR_DCD))
				sc->msr |= MSR_DDCD;
			if ((sc->msr & MSR_RI) != 0 && (msr & MSR_RI) == 0)
				sc->msr |= MSR_TERI;

			/*
			 * Update the value of MSR while retaining the delta
			 * bits.
			 */
			sc->msr &= MSR_DELTA_MASK;
			sc->msr |= msr;
			break;
		case REG_LSR:
			/*
			 * Line status register is not meant to be written to
			 * during normal operation.
			 */
			break;
		case REG_MSR:
			/*
			 * As far as I can tell MSR is a read-only register.
			 */
			break;
		case REG_SCR:
			sc->scr = value;
			break;
		default:
			break;
	}

done:
	uart_toggle_intr(sc);
	pthread_mutex_unlock(&sc->mtx);
}

uint8_t
uart_read(struct uart_softc *sc, int offset)
{
	uint8_t iir, intr_reason, reg;

	pthread_mutex_lock(&sc->mtx);

	/*
	 * Take care of the special case DLAB accesses first
	 */
	if ((sc->lcr & LCR_DLAB) != 0) {
		if (offset == REG_DLL) {
			reg = sc->dll;
			goto done;
		}
		
		if (offset == REG_DLH) {
			reg = sc->dlh;
			goto done;
		}
	}

	switch (offset) {
	case REG_DATA:
		reg = rxfifo_getchar(sc);
		break;
	case REG_IER:
		reg = sc->ier;
		break;
	case REG_IIR:
		iir = (sc->fcr & FCR_ENABLE) ? IIR_FIFO_MASK : 0;

		intr_reason = uart_intr_reason(sc);
			
		/*
		 * Deal with side effects of reading the IIR register
		 */
		if (intr_reason == IIR_TXRDY)
			sc->thre_int_pending = false;

		iir |= intr_reason;

		reg = iir;
		break;
	case REG_LCR:
		reg = sc->lcr;
		break;
	case REG_MCR:
		reg = sc->mcr;
		break;
	case REG_LSR:
		/* Transmitter is always ready for more data */
		sc->lsr |= LSR_TEMT | LSR_THRE;

		/* Check for new receive data */
		if (rxfifo_numchars(sc) > 0)
			sc->lsr |= LSR_RXRDY;
		else
			sc->lsr &= ~LSR_RXRDY;

		reg = sc->lsr;

		/* The LSR_OE bit is cleared on LSR read */
		sc->lsr &= ~LSR_OE;
		break;
	case REG_MSR:
		/*
		 * MSR delta bits are cleared on read
		 */
		reg = sc->msr;
		sc->msr &= ~MSR_DELTA_MASK;
		break;
	case REG_SCR:
		reg = sc->scr;
		break;
	default:
		reg = 0xFF;
		break;
	}

done:
	uart_toggle_intr(sc);
	pthread_mutex_unlock(&sc->mtx);

	return (reg);
}

int
uart_legacy_alloc(int which, int *baseaddr, int *irq)
{

	if (which < 0 || which >= UART_NLDEVS || uart_lres[which].inuse)
		return (-1);

	uart_lres[which].inuse = true;
	*baseaddr = uart_lres[which].baseaddr;
	*irq = uart_lres[which].irq;

	return (0);
}

struct uart_softc *
uart_init(uart_intr_func_t intr_assert, uart_intr_func_t intr_deassert,
    void *arg)
{
	struct uart_softc *sc;

	sc = calloc(1, sizeof(struct uart_softc));

	sc->arg = arg;
	sc->intr_assert = intr_assert;
	sc->intr_deassert = intr_deassert;

	pthread_mutex_init(&sc->mtx, NULL);

	uart_reset(sc);

	return (sc);
}

static int
uart_tty_backend(struct uart_softc *sc, const char *opts)
{
	int fd;
	int retval;

	retval = -1;

	fd = open(opts, O_RDWR | O_NONBLOCK);
	if (fd > 0 && isatty(fd)) {
		sc->tty.fd = fd;
		sc->tty.opened = true;
		retval = 0;
	}

	return (retval);
}

int
uart_set_backend(struct uart_softc *sc, const char *opts)
{
	int retval;
#ifndef WITHOUT_CAPSICUM
	cap_rights_t rights;
	cap_ioctl_t cmds[] = { TIOCGETA, TIOCSETA, TIOCGWINSZ };
#endif

	retval = -1;

	if (opts == NULL)
		return (0);

	if (strcmp("stdio", opts) == 0) {
		if (!uart_stdio) {
			sc->tty.fd = STDIN_FILENO;
			sc->tty.opened = true;
			uart_stdio = true;
			retval = 0;
		}
	} else if (uart_tty_backend(sc, opts) == 0) {
		retval = 0;
	}

	/* Make the backend file descriptor non-blocking */
	if (retval == 0)
		retval = fcntl(sc->tty.fd, F_SETFL, O_NONBLOCK);

	if (retval == 0) {
#ifndef WITHOUT_CAPSICUM
		cap_rights_init(&rights, CAP_EVENT, CAP_IOCTL, CAP_READ,
		    CAP_WRITE);
		if (caph_rights_limit(sc->tty.fd, &rights) == -1)
			errx(EX_OSERR, "Unable to apply rights for sandbox");
		if (caph_ioctls_limit(sc->tty.fd, cmds, nitems(cmds)) == -1)
			errx(EX_OSERR, "Unable to apply rights for sandbox");
		if (!uart_stdio) {
			if (caph_limit_stdin() == -1)
				errx(EX_OSERR,
				    "Unable to apply rights for sandbox");
		}
#endif
		uart_opentty(sc);
	}

	return (retval);
}
