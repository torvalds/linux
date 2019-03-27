/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2011-2012, 2016 Robert N. M. Watson
 * All rights reserved.
 *
 * This software was developed by SRI International and the University of
 * Cambridge Computer Laboratory under DARPA/AFRL contract (FA8750-10-C-0237)
 * ("CTSRD"), as part of the DARPA CRASH research programme.
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
#include <sys/cons.h>
#include <sys/endian.h>
#include <sys/kdb.h>
#include <sys/rman.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/reboot.h>
#include <sys/sysctl.h>
#include <sys/tty.h>

#include <ddb/ddb.h>

#include <machine/atomic.h>
#include <machine/bus.h>

#include <dev/altera/jtag_uart/altera_jtag_uart.h>

/*
 * If one of the Altera JTAG UARTs is currently the system console, register
 * it here.
 */
static struct altera_jtag_uart_softc	*aju_cons_sc;

static tsw_outwakeup_t	aju_outwakeup;
static void		aju_ac_callout(void *);
static void		aju_io_callout(void *);

static struct ttydevsw aju_ttydevsw = {
	.tsw_flags	= TF_NOPREFIX,
	.tsw_outwakeup	= aju_outwakeup,
};

/*
 * When polling for the AC bit, the number of times we have to not see it
 * before assuming JTAG has disappeared on us.  By default, four seconds.
 */
#define	AJU_JTAG_MAXMISS		20

/*
 * Polling intervals for input/output and JTAG connection events.
 */
#define	AJU_IO_POLLINTERVAL		(hz/100)
#define	AJU_AC_POLLINTERVAL		(hz/5)

/*
 * Statistics on JTAG removal events when sending, for debugging purposes
 * only.
 */
static u_int aju_jtag_vanished;
SYSCTL_UINT(_debug, OID_AUTO, aju_jtag_vanished, CTLFLAG_RW,
    &aju_jtag_vanished, 0, "Number of times JTAG has vanished");

static u_int aju_jtag_appeared;
SYSCTL_UINT(_debug, OID_AUTO, aju_jtag_appeared, CTLFLAG_RW,
    &aju_jtag_appeared, 0, "Number of times JTAG has appeared");

SYSCTL_INT(_debug, OID_AUTO, aju_cons_jtag_present, CTLFLAG_RW,
    &aju_cons_jtag_present, 0, "JTAG console present flag");

SYSCTL_UINT(_debug, OID_AUTO, aju_cons_jtag_missed, CTLFLAG_RW,
    &aju_cons_jtag_missed, 0, "JTAG console missed counter");

/*
 * Interrupt-related statistics.
 */
static u_int aju_intr_readable_enabled;
SYSCTL_UINT(_debug, OID_AUTO, aju_intr_readable_enabled, CTLFLAG_RW,
    &aju_intr_readable_enabled, 0, "Number of times read interrupt enabled");

static u_int aju_intr_writable_disabled;
SYSCTL_UINT(_debug, OID_AUTO, aju_intr_writable_disabled, CTLFLAG_RW,
    &aju_intr_writable_disabled, 0,
    "Number of times write interrupt disabled");

static u_int aju_intr_writable_enabled;
SYSCTL_UINT(_debug, OID_AUTO, aju_intr_writable_enabled, CTLFLAG_RW,
    &aju_intr_writable_enabled, 0,
    "Number of times write interrupt enabled");

static u_int aju_intr_disabled;
SYSCTL_UINT(_debug, OID_AUTO, aju_intr_disabled, CTLFLAG_RW,
    &aju_intr_disabled, 0, "Number of times write interrupt disabled");

static u_int aju_intr_read_count;
SYSCTL_UINT(_debug, OID_AUTO, aju_intr_read_count, CTLFLAG_RW,
    &aju_intr_read_count, 0, "Number of times read interrupt fired");

static u_int aju_intr_write_count;
SYSCTL_UINT(_debug, OID_AUTO, aju_intr_write_count, CTLFLAG_RW,
    &aju_intr_write_count, 0, "Number of times write interrupt fired");

/*
 * Low-level read and write register routines; the Altera UART is little
 * endian, so we byte swap 32-bit reads and writes.
 */
static inline uint32_t
aju_data_read(struct altera_jtag_uart_softc *sc)
{

	return (le32toh(bus_read_4(sc->ajus_mem_res,
	    ALTERA_JTAG_UART_DATA_OFF)));
}

static inline void
aju_data_write(struct altera_jtag_uart_softc *sc, uint32_t v)
{

	bus_write_4(sc->ajus_mem_res, ALTERA_JTAG_UART_DATA_OFF, htole32(v));
}

static inline uint32_t
aju_control_read(struct altera_jtag_uart_softc *sc)
{

	return (le32toh(bus_read_4(sc->ajus_mem_res,
	    ALTERA_JTAG_UART_CONTROL_OFF)));
}

static inline void
aju_control_write(struct altera_jtag_uart_softc *sc, uint32_t v)
{

	bus_write_4(sc->ajus_mem_res, ALTERA_JTAG_UART_CONTROL_OFF,
	    htole32(v));
}

/*
 * Slightly higher-level routines aware of buffering and flow control.
 */
static inline int
aju_writable(struct altera_jtag_uart_softc *sc)
{

	return ((aju_control_read(sc) &
	    ALTERA_JTAG_UART_CONTROL_WSPACE) != 0);
}

static inline int
aju_readable(struct altera_jtag_uart_softc *sc)
{
	uint32_t v;

	AJU_LOCK_ASSERT(sc);

	if (*sc->ajus_buffer_validp)
		return (1);
	v = aju_data_read(sc);
	if ((v & ALTERA_JTAG_UART_DATA_RVALID) != 0) {
		*sc->ajus_buffer_validp = 1;
		*sc->ajus_buffer_datap = (v & ALTERA_JTAG_UART_DATA_DATA);
		return (1);
	}
	return (0);
}

static char
aju_read(struct altera_jtag_uart_softc *sc)
{

	AJU_LOCK_ASSERT(sc);

	while (!aju_readable(sc));
	*sc->ajus_buffer_validp = 0;
	return (*sc->ajus_buffer_datap);
}

/*
 * Routines for enabling and disabling interrupts for read and write.
 */
static void
aju_intr_readable_enable(struct altera_jtag_uart_softc *sc)
{
	uint32_t v;

	AJU_LOCK_ASSERT(sc);

	atomic_add_int(&aju_intr_readable_enabled, 1);
	v = aju_control_read(sc);
	v |= ALTERA_JTAG_UART_CONTROL_RE;
	aju_control_write(sc, v);
}

static void
aju_intr_writable_enable(struct altera_jtag_uart_softc *sc)
{
	uint32_t v;

	AJU_LOCK_ASSERT(sc);

	atomic_add_int(&aju_intr_writable_enabled, 1);
	v = aju_control_read(sc);
	v |= ALTERA_JTAG_UART_CONTROL_WE;
	aju_control_write(sc, v);
}

static void
aju_intr_writable_disable(struct altera_jtag_uart_softc *sc)
{
	uint32_t v;

	AJU_LOCK_ASSERT(sc);

	atomic_add_int(&aju_intr_writable_disabled, 1);
	v = aju_control_read(sc);
	v &= ~ALTERA_JTAG_UART_CONTROL_WE;
	aju_control_write(sc, v);
}

static void
aju_intr_disable(struct altera_jtag_uart_softc *sc)
{
	uint32_t v;

	AJU_LOCK_ASSERT(sc);

	atomic_add_int(&aju_intr_disabled, 1);
	v = aju_control_read(sc);
	v &= ~(ALTERA_JTAG_UART_CONTROL_RE | ALTERA_JTAG_UART_CONTROL_WE);
	aju_control_write(sc, v);
}

/*
 * The actual work of checking for, and handling, available reads.  This is
 * used in both polled and interrupt-driven modes, as JTAG UARTs may be hooked
 * up with, or without, IRQs allocated.
 */
static void
aju_handle_input(struct altera_jtag_uart_softc *sc, struct tty *tp)
{
	int c;

	tty_lock_assert(tp, MA_OWNED);
	AJU_LOCK_ASSERT(sc);

	while (aju_readable(sc)) {
		c = aju_read(sc);
		AJU_UNLOCK(sc);
#ifdef KDB
		if (sc->ajus_flags & ALTERA_JTAG_UART_FLAG_CONSOLE)
			kdb_alt_break(c, &sc->ajus_alt_break_state);
#endif
		ttydisc_rint(tp, c, 0);
		AJU_LOCK(sc);
	}
	AJU_UNLOCK(sc);
	ttydisc_rint_done(tp);
	AJU_LOCK(sc);
}

/*
 * Send output to the UART until either there's none left to send, or we run
 * out of room and need to await an interrupt so that we can start sending
 * again.
 *
 * XXXRW: It would be nice to query WSPACE at the beginning and write to the
 * FIFO in bugger chunks.
 */
static void
aju_handle_output(struct altera_jtag_uart_softc *sc, struct tty *tp)
{
	uint32_t v;
	uint8_t ch;

	tty_lock_assert(tp, MA_OWNED);
	AJU_LOCK_ASSERT(sc);

	AJU_UNLOCK(sc);
	while (ttydisc_getc_poll(tp) != 0) {
		AJU_LOCK(sc);
		if (*sc->ajus_jtag_presentp == 0) {
			/*
			 * If JTAG is not present, then we will drop this
			 * character instead of perhaps scheduling an
			 * interrupt to let us know when there is buffer
			 * space.  Otherwise we might get a write interrupt
			 * later even though we aren't interested in sending
			 * anymore.  Loop to drain TTY-layer buffer.
			 */
			AJU_UNLOCK(sc);
			if (ttydisc_getc(tp, &ch, sizeof(ch)) !=
			    sizeof(ch))
				panic("%s: ttydisc_getc", __func__);
			continue;
		}
		v = aju_control_read(sc);
		if ((v & ALTERA_JTAG_UART_CONTROL_WSPACE) == 0) {
			if (sc->ajus_irq_res != NULL &&
			    (v & ALTERA_JTAG_UART_CONTROL_WE) == 0)
				aju_intr_writable_enable(sc);
			return;
		}
		AJU_UNLOCK(sc);
		if (ttydisc_getc(tp, &ch, sizeof(ch)) != sizeof(ch))
			panic("%s: ttydisc_getc 2", __func__);
		AJU_LOCK(sc);

		/*
		 * XXXRW: There is a slight race here in which we test for
		 * writability, drop the lock, get the character from the tty
		 * layer, re-acquire the lock, and then write.  It's possible
		 * for other code -- specifically, the low-level console -- to
		 * have* written in the mean time, which might mean that there
		 * is no longer space.  The BERI memory bus will cause this
		 * write to block, wedging the processor until space is
		 * available -- which could be a while if JTAG is not
		 * attached!
		 *
		 * The 'easy' fix is to drop the character if WSPACE has
		 * become unset.  Not sure what the 'hard' fix is.
		 */
		aju_data_write(sc, ch);
		AJU_UNLOCK(sc);
	}
	AJU_LOCK(sc);

	/*
	 * If interrupts are configured, and there's no data to write, but we
	 * had previously enabled write interrupts, disable them now.
	 */
	v = aju_control_read(sc);
	if (sc->ajus_irq_res != NULL && (v & ALTERA_JTAG_UART_CONTROL_WE) != 0)
		aju_intr_writable_disable(sc);
}

static void
aju_outwakeup(struct tty *tp)
{
	struct altera_jtag_uart_softc *sc = tty_softc(tp);

	tty_lock_assert(tp, MA_OWNED);

	AJU_LOCK(sc);
	aju_handle_output(sc, tp);
	AJU_UNLOCK(sc);
}

static void
aju_io_callout(void *arg)
{
	struct altera_jtag_uart_softc *sc = arg;
	struct tty *tp = sc->ajus_ttyp;

	tty_lock(tp);
	AJU_LOCK(sc);

	/*
	 * It would be convenient if we could share code with aju_intr() here
	 * by testing the control register for ALTERA_JTAG_UART_CONTROL_RI and
	 * ALTERA_JTAG_UART_CONTROL_WI.  Unfortunately, it's not clear that
	 * this is supported, so do all the work to poll for both input and
	 * output.
	 */
	aju_handle_input(sc, tp);
	aju_handle_output(sc, tp);

	/*
	 * Reschedule next poll attempt.  There's some argument that we should
	 * do adaptive polling based on the expectation of I/O: is something
	 * pending in the output buffer, or have we recently had input, but we
	 * don't.
	 */
	callout_reset(&sc->ajus_io_callout, AJU_IO_POLLINTERVAL,
	    aju_io_callout, sc);
	AJU_UNLOCK(sc);
	tty_unlock(tp);
}

static void
aju_ac_callout(void *arg)
{
	struct altera_jtag_uart_softc *sc = arg;
	struct tty *tp = sc->ajus_ttyp;
	uint32_t v;

	tty_lock(tp);
	AJU_LOCK(sc);
	v = aju_control_read(sc);
	if (v & ALTERA_JTAG_UART_CONTROL_AC) {
		v &= ~ALTERA_JTAG_UART_CONTROL_AC;
		aju_control_write(sc, v);
		if (*sc->ajus_jtag_presentp == 0) {
			*sc->ajus_jtag_presentp = 1;
			atomic_add_int(&aju_jtag_appeared, 1);
			aju_handle_output(sc, tp);
		}

		/* Any hit eliminates all recent misses. */
		*sc->ajus_jtag_missedp = 0;
	} else if (*sc->ajus_jtag_presentp != 0) {
		/*
		 * If we've exceeded our tolerance for misses, mark JTAG as
		 * disconnected and drain output.  Otherwise, bump the miss
		 * counter.
		 */
		if (*sc->ajus_jtag_missedp > AJU_JTAG_MAXMISS) {
			*sc->ajus_jtag_presentp = 0;
			atomic_add_int(&aju_jtag_vanished, 1);
			aju_handle_output(sc, tp);
		} else
			(*sc->ajus_jtag_missedp)++;
	}
	callout_reset(&sc->ajus_ac_callout, AJU_AC_POLLINTERVAL,
	    aju_ac_callout, sc);
	AJU_UNLOCK(sc);
	tty_unlock(tp);
}

static void
aju_intr(void *arg)
{
	struct altera_jtag_uart_softc *sc = arg;
	struct tty *tp = sc->ajus_ttyp;
	uint32_t v;

	tty_lock(tp);
	AJU_LOCK(sc);
	v = aju_control_read(sc);
	if (v & ALTERA_JTAG_UART_CONTROL_RI) {
		atomic_add_int(&aju_intr_read_count, 1);
		aju_handle_input(sc, tp);
	}
	if (v & ALTERA_JTAG_UART_CONTROL_WI) {
		atomic_add_int(&aju_intr_write_count, 1);
		aju_handle_output(sc, tp);
	}
	AJU_UNLOCK(sc);
	tty_unlock(tp);
}

int
altera_jtag_uart_attach(struct altera_jtag_uart_softc *sc)
{
	struct tty *tp;
	int error;

	AJU_LOCK_INIT(sc);

	/*
	 * XXXRW: Currently, we detect the console solely based on it using a
	 * reserved address, and borrow console-level locks and buffer if so.
	 * Is there a better way?
	 */
	if (rman_get_start(sc->ajus_mem_res) == BERI_UART_BASE) {
		sc->ajus_lockp = &aju_cons_lock;
		sc->ajus_buffer_validp = &aju_cons_buffer_valid;
		sc->ajus_buffer_datap = &aju_cons_buffer_data;
		sc->ajus_jtag_presentp = &aju_cons_jtag_present;
		sc->ajus_jtag_missedp = &aju_cons_jtag_missed;
		sc->ajus_flags |= ALTERA_JTAG_UART_FLAG_CONSOLE;
	} else {
		sc->ajus_lockp = &sc->ajus_lock;
		sc->ajus_buffer_validp = &sc->ajus_buffer_valid;
		sc->ajus_buffer_datap = &sc->ajus_buffer_data;
		sc->ajus_jtag_presentp = &sc->ajus_jtag_present;
		sc->ajus_jtag_missedp = &sc->ajus_jtag_missed;
	}

	/*
	 * Disable interrupts regardless of whether or not we plan to use
	 * them.  We will register an interrupt handler now if they will be
	 * used, but not re-enable intil later once the remainder of the tty
	 * layer is properly initialised, as we're not ready for input yet.
	 */
	AJU_LOCK(sc);
	aju_intr_disable(sc);
	AJU_UNLOCK(sc);
	if (sc->ajus_irq_res != NULL) {
		error = bus_setup_intr(sc->ajus_dev, sc->ajus_irq_res,
		    INTR_ENTROPY | INTR_TYPE_TTY | INTR_MPSAFE, NULL,
		    aju_intr, sc, &sc->ajus_irq_cookie);
		if (error) {
			device_printf(sc->ajus_dev,
			    "could not activate interrupt\n");
			AJU_LOCK_DESTROY(sc);
			return (error);
		}
	}
	tp = sc->ajus_ttyp = tty_alloc(&aju_ttydevsw, sc);
	if (sc->ajus_flags & ALTERA_JTAG_UART_FLAG_CONSOLE) {
		aju_cons_sc = sc;
		tty_init_console(tp, 0);
	}
	tty_makedev(tp, NULL, "%s%d", AJU_TTYNAME, sc->ajus_unit);

	/*
	 * If we will be using interrupts, enable them now; otherwise, start
	 * polling.  From this point onwards, input can arrive.
	 */
	if (sc->ajus_irq_res != NULL) {
		AJU_LOCK(sc);
		aju_intr_readable_enable(sc);
		AJU_UNLOCK(sc);
	} else {
		callout_init(&sc->ajus_io_callout, 1);
		callout_reset(&sc->ajus_io_callout, AJU_IO_POLLINTERVAL,
		    aju_io_callout, sc);
	}
	callout_init(&sc->ajus_ac_callout, 1);
	callout_reset(&sc->ajus_ac_callout, AJU_AC_POLLINTERVAL,
	    aju_ac_callout, sc);
	return (0);
}

void
altera_jtag_uart_detach(struct altera_jtag_uart_softc *sc)
{
	struct tty *tp = sc->ajus_ttyp;

	/*
	 * If we're using interrupts, disable and release the interrupt
	 * handler now.  Otherwise drain the polling timeout.
	 */
	if (sc->ajus_irq_res != NULL) {
		AJU_LOCK(sc);
		aju_intr_disable(sc);
		AJU_UNLOCK(sc);
		bus_teardown_intr(sc->ajus_dev, sc->ajus_irq_res,
		    sc->ajus_irq_cookie);
	} else
		callout_drain(&sc->ajus_io_callout);
	callout_drain(&sc->ajus_ac_callout);
	if (sc->ajus_flags & ALTERA_JTAG_UART_FLAG_CONSOLE)
		aju_cons_sc = NULL;
	tty_lock(tp);
	tty_rel_gone(tp);
	AJU_LOCK_DESTROY(sc);
}
