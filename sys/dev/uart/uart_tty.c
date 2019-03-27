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
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/conf.h>
#include <sys/cons.h>
#include <sys/fcntl.h>
#include <sys/interrupt.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/reboot.h>
#include <machine/bus.h>
#include <sys/rman.h>
#include <sys/tty.h>
#include <machine/resource.h>
#include <machine/stdarg.h>

#include <dev/uart/uart.h>
#include <dev/uart/uart_bus.h>
#include <dev/uart/uart_cpu.h>

#include "uart_if.h"

static cn_probe_t uart_cnprobe;
static cn_init_t uart_cninit;
static cn_init_t uart_cnresume;
static cn_term_t uart_cnterm;
static cn_getc_t uart_cngetc;
static cn_putc_t uart_cnputc;
static cn_grab_t uart_cngrab;
static cn_ungrab_t uart_cnungrab;

static tsw_open_t uart_tty_open;
static tsw_close_t uart_tty_close;
static tsw_outwakeup_t uart_tty_outwakeup;
static tsw_inwakeup_t uart_tty_inwakeup;
static tsw_ioctl_t uart_tty_ioctl;
static tsw_param_t uart_tty_param;
static tsw_modem_t uart_tty_modem;
static tsw_free_t uart_tty_free;
static tsw_busy_t uart_tty_busy;

CONSOLE_DRIVER(
	uart,
	.cn_resume = uart_cnresume,
);

static struct uart_devinfo uart_console;

static void
uart_cnprobe(struct consdev *cp)
{

	cp->cn_pri = CN_DEAD;

	KASSERT(uart_console.cookie == NULL, ("foo"));

	if (uart_cpu_getdev(UART_DEV_CONSOLE, &uart_console))
		return;

	if (uart_probe(&uart_console))
		return;

	strlcpy(cp->cn_name, uart_driver_name, sizeof(cp->cn_name));
	cp->cn_pri = (boothowto & RB_SERIAL) ? CN_REMOTE : CN_NORMAL;
	cp->cn_arg = &uart_console;
}

static void
uart_cninit(struct consdev *cp)
{
	struct uart_devinfo *di;

	/*
	 * Yedi trick: we need to be able to define cn_dev before we go
	 * single- or multi-user. The problem is that we don't know at
	 * this time what the device will be. Hence, we need to link from
	 * the uart_devinfo to the consdev that corresponds to it so that
	 * we can define cn_dev in uart_bus_attach() when we find the
	 * device during bus enumeration. That's when we'll know what the
	 * the unit number will be.
	 */
	di = cp->cn_arg;
	KASSERT(di->cookie == NULL, ("foo"));
	di->cookie = cp;
	di->type = UART_DEV_CONSOLE;
	uart_add_sysdev(di);
	uart_init(di);
}

static void
uart_cnresume(struct consdev *cp)
{

	uart_init(cp->cn_arg);
}

static void
uart_cnterm(struct consdev *cp)
{

	uart_term(cp->cn_arg);
}

static void
uart_cngrab(struct consdev *cp)
{

	uart_grab(cp->cn_arg);
}

static void
uart_cnungrab(struct consdev *cp)
{

	uart_ungrab(cp->cn_arg);
}

static void
uart_cnputc(struct consdev *cp, int c)
{

	uart_putc(cp->cn_arg, c);
}

static int
uart_cngetc(struct consdev *cp)
{

	return (uart_poll(cp->cn_arg));
}

static int
uart_tty_open(struct tty *tp)
{
	struct uart_softc *sc;

	sc = tty_softc(tp);

	if (sc == NULL || sc->sc_leaving)
		return (ENXIO);

	sc->sc_opened = 1;
	return (0);
}

static void
uart_tty_close(struct tty *tp)
{
	struct uart_softc *sc;

	sc = tty_softc(tp);
	if (sc == NULL || sc->sc_leaving || !sc->sc_opened)
		return;

	if (sc->sc_hwiflow)
		UART_IOCTL(sc, UART_IOCTL_IFLOW, 0);
	if (sc->sc_hwoflow)
		UART_IOCTL(sc, UART_IOCTL_OFLOW, 0);
	if (sc->sc_sysdev == NULL)
		UART_SETSIG(sc, SER_DDTR | SER_DRTS);

	wakeup(sc);
	sc->sc_opened = 0;
}

static void
uart_tty_outwakeup(struct tty *tp)
{
	struct uart_softc *sc;

	sc = tty_softc(tp);
	if (sc == NULL || sc->sc_leaving)
		return;

	if (sc->sc_txbusy)
		return;

	/*
	 * Respect RTS/CTS (output) flow control if enabled and not already
	 * handled by hardware.
	 */
	if ((tp->t_termios.c_cflag & CCTS_OFLOW) && !sc->sc_hwoflow &&
	    !(sc->sc_hwsig & SER_CTS))
		return;

	sc->sc_txdatasz = ttydisc_getc(tp, sc->sc_txbuf, sc->sc_txfifosz);
	if (sc->sc_txdatasz != 0)
		UART_TRANSMIT(sc);
}

static void
uart_tty_inwakeup(struct tty *tp)
{
	struct uart_softc *sc;

	sc = tty_softc(tp);
	if (sc == NULL || sc->sc_leaving)
		return;

	if (sc->sc_isquelch) {
		if ((tp->t_termios.c_cflag & CRTS_IFLOW) && !sc->sc_hwiflow)
			UART_SETSIG(sc, SER_DRTS|SER_RTS);
		sc->sc_isquelch = 0;
		uart_sched_softih(sc, SER_INT_RXREADY);
	}
}

static int
uart_tty_ioctl(struct tty *tp, u_long cmd, caddr_t data,
    struct thread *td __unused)
{
	struct uart_softc *sc;

	sc = tty_softc(tp);

	switch (cmd) {
	case TIOCSBRK:
		UART_IOCTL(sc, UART_IOCTL_BREAK, 1);
		return (0);
	case TIOCCBRK:
		UART_IOCTL(sc, UART_IOCTL_BREAK, 0);
		return (0);
	default:
		return pps_ioctl(cmd, data, &sc->sc_pps);
	}
}

static int
uart_tty_param(struct tty *tp, struct termios *t)
{
	struct uart_softc *sc;
	int databits, parity, stopbits;

	sc = tty_softc(tp);
	if (sc == NULL || sc->sc_leaving)
		return (ENODEV);
	if (t->c_ispeed != t->c_ospeed && t->c_ospeed != 0)
		return (EINVAL);
	if (t->c_ospeed == 0) {
		UART_SETSIG(sc, SER_DDTR | SER_DRTS);
		return (0);
	}
	switch (t->c_cflag & CSIZE) {
	case CS5:	databits = 5; break;
	case CS6:	databits = 6; break;
	case CS7:	databits = 7; break;
	default:	databits = 8; break;
	}
	stopbits = (t->c_cflag & CSTOPB) ? 2 : 1;
	if (t->c_cflag & PARENB)
		parity = (t->c_cflag & PARODD) ? UART_PARITY_ODD :
		    UART_PARITY_EVEN;
	else
		parity = UART_PARITY_NONE;
	if (UART_PARAM(sc, t->c_ospeed, databits, stopbits, parity) != 0)
		return (EINVAL);
	UART_SETSIG(sc, SER_DDTR | SER_DTR);
	/* Set input flow control state. */
	if (!sc->sc_hwiflow) {
		if ((t->c_cflag & CRTS_IFLOW) && sc->sc_isquelch)
			UART_SETSIG(sc, SER_DRTS);
		else
			UART_SETSIG(sc, SER_DRTS | SER_RTS);
	} else
		UART_IOCTL(sc, UART_IOCTL_IFLOW, (t->c_cflag & CRTS_IFLOW));
	/* Set output flow control state. */
	if (sc->sc_hwoflow)
		UART_IOCTL(sc, UART_IOCTL_OFLOW, (t->c_cflag & CCTS_OFLOW));

	return (0);
}

static int
uart_tty_modem(struct tty *tp, int biton, int bitoff)
{
	struct uart_softc *sc;

	sc = tty_softc(tp);
	if (biton != 0 || bitoff != 0)
		UART_SETSIG(sc, SER_DELTA(bitoff | biton) | biton);
	return (sc->sc_hwsig);
}

void
uart_tty_intr(void *arg)
{
	struct uart_softc *sc = arg;
	struct tty *tp;
	int c, err = 0, pend, sig, xc;

	if (sc->sc_leaving)
		return;

	pend = atomic_readandclear_32(&sc->sc_ttypend);
	if (!(pend & SER_INT_MASK))
		return;

	tp = sc->sc_u.u_tty.tp;
	tty_lock(tp);

	if (pend & SER_INT_RXREADY) {
		while (!uart_rx_empty(sc) && !sc->sc_isquelch) {
			xc = uart_rx_peek(sc);
			c = xc & 0xff;
			if (xc & UART_STAT_FRAMERR)
				err |= TRE_FRAMING;
			if (xc & UART_STAT_OVERRUN)
				err |= TRE_OVERRUN;
			if (xc & UART_STAT_PARERR)
				err |= TRE_PARITY;
			if (ttydisc_rint(tp, c, err) != 0) {
				sc->sc_isquelch = 1;
				if ((tp->t_termios.c_cflag & CRTS_IFLOW) &&
				    !sc->sc_hwiflow)
					UART_SETSIG(sc, SER_DRTS);
			} else
				uart_rx_next(sc);
		}
	}

	if (pend & SER_INT_BREAK)
		ttydisc_rint(tp, 0, TRE_BREAK);

	if (pend & SER_INT_SIGCHG) {
		sig = pend & SER_INT_SIGMASK;
		if (sig & SER_DDCD)
			ttydisc_modem(tp, sig & SER_DCD);
		if (sig & SER_DCTS)
			uart_tty_outwakeup(tp);
	}

	if (pend & SER_INT_TXIDLE)
		uart_tty_outwakeup(tp);
	ttydisc_rint_done(tp);
	tty_unlock(tp);
}

static void
uart_tty_free(void *arg __unused)
{

	/*
	 * XXX: uart(4) could reuse the device unit number before it is
	 * being freed by the TTY layer. We should use this hook to free
	 * the device unit number, but unfortunately newbus does not
	 * seem to support such a construct.
	 */
}

static bool
uart_tty_busy(struct tty *tp)
{
	struct uart_softc *sc;

	sc = tty_softc(tp);
	if (sc == NULL || sc->sc_leaving)
                return (FALSE);

	return (sc->sc_txbusy);
}

static struct ttydevsw uart_tty_class = {
	.tsw_flags	= TF_INITLOCK|TF_CALLOUT,
	.tsw_open	= uart_tty_open,
	.tsw_close	= uart_tty_close,
	.tsw_outwakeup	= uart_tty_outwakeup,
	.tsw_inwakeup	= uart_tty_inwakeup,
	.tsw_ioctl	= uart_tty_ioctl,
	.tsw_param	= uart_tty_param,
	.tsw_modem	= uart_tty_modem,
	.tsw_free	= uart_tty_free,
	.tsw_busy	= uart_tty_busy,
};

int
uart_tty_attach(struct uart_softc *sc)
{
	struct tty *tp;
	int unit;

	sc->sc_u.u_tty.tp = tp = tty_alloc(&uart_tty_class, sc);

	unit = device_get_unit(sc->sc_dev);

	if (sc->sc_sysdev != NULL && sc->sc_sysdev->type == UART_DEV_CONSOLE) {
		sprintf(((struct consdev *)sc->sc_sysdev->cookie)->cn_name,
		    "ttyu%r", unit);
		tty_init_console(tp, sc->sc_sysdev->baudrate);
	}

	swi_add(&tty_intr_event, uart_driver_name, uart_tty_intr, sc, SWI_TTY,
	    INTR_TYPE_TTY, &sc->sc_softih);

	tty_makedev(tp, NULL, "u%r", unit);

	return (0);
}

int
uart_tty_detach(struct uart_softc *sc)
{
	struct tty *tp;

	tp = sc->sc_u.u_tty.tp;

	tty_lock(tp);
	swi_remove(sc->sc_softih);
	tty_rel_gone(tp);

	return (0);
}

struct mtx *
uart_tty_getlock(struct uart_softc *sc)
{

	if (sc->sc_u.u_tty.tp != NULL)
		return (tty_getlock(sc->sc_u.u_tty.tp));
	else
		return (NULL);
}
