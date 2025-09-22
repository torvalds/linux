/*	$OpenBSD: lpt.c,v 1.17 2025/06/25 20:28:09 miod Exp $ */
/*	$NetBSD: lpt.c,v 1.42 1996/10/21 22:41:14 thorpej Exp $	*/

/*
 * Copyright (c) 1993, 1994 Charles Hannum.
 * Copyright (c) 1990 William F. Jolitz, TeleMuse
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
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This software is a component of "386BSD" developed by 
 *	William F. Jolitz, TeleMuse.
 * 4. Neither the name of the developer nor the name "386BSD"
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS A COMPONENT OF 386BSD DEVELOPED BY WILLIAM F. JOLITZ 
 * AND IS INTENDED FOR RESEARCH AND EDUCATIONAL PURPOSES ONLY. THIS 
 * SOFTWARE SHOULD NOT BE CONSIDERED TO BE A COMMERCIAL PRODUCT. 
 * THE DEVELOPER URGES THAT USERS WHO REQUIRE A COMMERCIAL PRODUCT 
 * NOT MAKE USE OF THIS WORK.
 *
 * FOR USERS WHO WISH TO UNDERSTAND THE 386BSD SYSTEM DEVELOPED
 * BY WILLIAM F. JOLITZ, WE RECOMMEND THE USER STUDY WRITTEN 
 * REFERENCES SUCH AS THE  "PORTING UNIX TO THE 386" SERIES 
 * (BEGINNING JANUARY 1991 "DR. DOBBS JOURNAL", USA AND BEGINNING 
 * JUNE 1991 "UNIX MAGAZIN", GERMANY) BY WILLIAM F. JOLITZ AND 
 * LYNNE GREER JOLITZ, AS WELL AS OTHER BOOKS ON UNIX AND THE 
 * ON-LINE 386BSD USER MANUAL BEFORE USE. A BOOK DISCUSSING THE INTERNALS 
 * OF 386BSD ENTITLED "386BSD FROM THE INSIDE OUT" WILL BE AVAILABLE LATE 1992.
 *
 * THIS SOFTWARE IS PROVIDED BY THE DEVELOPER ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE DEVELOPER BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 * Device Driver for AT parallel printer port
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/malloc.h>
#include <sys/kernel.h>
#include <sys/uio.h>
#include <sys/device.h>
#include <sys/conf.h>
#include <sys/syslog.h>

#include <machine/bus.h>
#include <machine/intr.h>

#include <dev/ic/lptreg.h>
#include <dev/ic/lptvar.h>

#include "lpt.h"

#define	TIMEOUT		16000	/* wait up to 16 seconds for a ready */
#define	STEP		250	/* 1/4 seconds */

#define	LPTPRI		(PZERO+8)
#define	LPT_BSIZE	1024

#if !defined(DEBUG) || !defined(notdef)
#define LPRINTF(a)
#else
#define LPRINTF(a)	if (lptdebug) printf a
int lptdebug = 1;
#endif

/* XXX does not belong here */
cdev_decl(lpt);

struct cfdriver lpt_cd = {
	NULL, "lpt", DV_TTY
};

#define	LPTUNIT(s)	(minor(s) & 0x1f)
#define	LPTFLAGS(s)	(minor(s) & 0xe0)

#define	LPS_INVERT	(LPS_SELECT|LPS_NERR|LPS_NBSY|LPS_NACK)
#define	LPS_MASK	(LPS_SELECT|LPS_NERR|LPS_NBSY|LPS_NACK|LPS_NOPAPER)
#define	NOT_READY() \
    ((bus_space_read_1(sc->sc_iot, sc->sc_ioh, lpt_status) ^ LPS_INVERT) & LPS_MASK)
#define	NOT_READY_ERR() \
    lpt_not_ready(bus_space_read_1(sc->sc_iot, sc->sc_ioh, lpt_status), sc)

int	lpt_not_ready(u_int8_t, struct lpt_softc *);
void	lptwakeup(void *arg);
int	lptpushbytes(struct lpt_softc *);

/*
 * Internal routine to lptprobe to do port tests of one byte value.
 */
int
lpt_port_test(bus_space_tag_t iot, bus_space_handle_t ioh, bus_addr_t base,
    bus_size_t off, u_int8_t data, u_int8_t mask)
{
	int timeout;
	u_int8_t temp;

	data &= mask;
	bus_space_write_1(iot, ioh, off, data);
	timeout = 1000;
	do {
		delay(10);
		temp = bus_space_read_1(iot, ioh, off) & mask;
	} while (temp != data && --timeout);
	LPRINTF(("lpt: port=0x%x out=0x%x in=0x%x timeout=%d\n", base + off,
	    data, temp, timeout));
	return (temp == data);
}

void
lpt_attach_common(struct lpt_softc *sc)
{
	printf("\n");

	bus_space_write_1(sc->sc_iot, sc->sc_ioh, lpt_control, LPC_NINIT);

	timeout_set(&sc->sc_wakeup_tmo, lptwakeup, sc);
}

/*
 * Reset the printer, then wait until it's selected and not busy.
 */
int
lptopen(dev_t dev, int flag, int mode, struct proc *p)
{
	int unit = LPTUNIT(dev);
	u_int8_t flags = LPTFLAGS(dev);
	struct lpt_softc *sc;
	u_int8_t control;
	int error;
	int spin;

	if (unit >= lpt_cd.cd_ndevs)
		return ENXIO;
	sc = lpt_cd.cd_devs[unit];
	if (!sc)
		return ENXIO;

	sc->sc_flags = (sc->sc_flags & LPT_POLLED) | flags;
	if ((sc->sc_flags & (LPT_POLLED|LPT_NOINTR)) == LPT_POLLED)
		return ENXIO;

#ifdef DIAGNOSTIC
	if (sc->sc_state)
		printf("%s: stat=0x%x not zero\n", sc->sc_dev.dv_xname,
		    sc->sc_state);
#endif

	if (sc->sc_state)
		return EBUSY;

	sc->sc_state = LPT_INIT;
	LPRINTF(("%s: open: flags=0x%x\n", sc->sc_dev.dv_xname, flags));

	if ((flags & LPT_NOPRIME) == 0) {
		/* assert INIT for 100 usec to start up printer */
		bus_space_write_1(sc->sc_iot, sc->sc_ioh, lpt_control, LPC_SELECT);
		delay(100);
	}

	control = LPC_SELECT | LPC_NINIT;
	bus_space_write_1(sc->sc_iot, sc->sc_ioh, lpt_control, control);

	/* wait till ready (printer running diagnostics) */
	for (spin = 0; NOT_READY_ERR(); spin += STEP) {
		if (spin >= TIMEOUT) {
			sc->sc_state = 0;
			return EBUSY;
		}

		/* wait 1/4 second, give up if we get a signal */
		error = tsleep_nsec(sc, LPTPRI | PCATCH, "lptopen",
		    MSEC_TO_NSEC(STEP));
		if (sc->sc_state == 0)
			return (EIO);
		if (error != EWOULDBLOCK) {
			sc->sc_state = 0;
			return error;
		}
	}

	if ((flags & LPT_NOINTR) == 0)
		control |= LPC_IENABLE;
	if (flags & LPT_AUTOLF)
		control |= LPC_AUTOLF;
	sc->sc_control = control;
	bus_space_write_1(sc->sc_iot, sc->sc_ioh, lpt_control, control);

	sc->sc_inbuf = malloc(LPT_BSIZE, M_DEVBUF, M_WAITOK);
	sc->sc_count = 0;
	sc->sc_state = LPT_OPEN;

	if ((sc->sc_flags & LPT_NOINTR) == 0)
		lptwakeup(sc);

	LPRINTF(("%s: opened\n", sc->sc_dev.dv_xname));
	return 0;
}

int
lpt_not_ready(u_int8_t status, struct lpt_softc *sc)
{
	u_int8_t new;

	status = (status ^ LPS_INVERT) & LPS_MASK;
	new = status & ~sc->sc_laststatus;
	sc->sc_laststatus = status;

	if (new & LPS_SELECT)
		log(LOG_NOTICE, "%s: offline\n", sc->sc_dev.dv_xname);
	else if (new & LPS_NOPAPER)
		log(LOG_NOTICE, "%s: out of paper\n", sc->sc_dev.dv_xname);
	else if (new & LPS_NERR)
		log(LOG_NOTICE, "%s: output error\n", sc->sc_dev.dv_xname);

	return status;
}

void
lptwakeup(void *arg)
{
	struct lpt_softc *sc = arg;
	int s;

	s = spltty();
	lptintr(sc);
	splx(s);

	if (sc->sc_state != 0)
		timeout_add_msec(&sc->sc_wakeup_tmo, STEP);
}

/*
 * Close the device, and free the local line buffer.
 */
int
lptclose(dev_t dev, int flag, int mode, struct proc *p)
{
	int unit = LPTUNIT(dev);
	struct lpt_softc *sc = lpt_cd.cd_devs[unit];
	bus_space_tag_t iot = sc->sc_iot;
	bus_space_handle_t ioh = sc->sc_ioh;

	if (sc->sc_count)
		(void) lptpushbytes(sc);

	if ((sc->sc_flags & LPT_NOINTR) == 0)
		timeout_del(&sc->sc_wakeup_tmo);

	bus_space_write_1(iot, ioh, lpt_control, LPC_NINIT);
	sc->sc_state = 0;
	bus_space_write_1(iot, ioh, lpt_control, LPC_NINIT);
	free(sc->sc_inbuf, M_DEVBUF, LPT_BSIZE);

	LPRINTF(("%s: closed\n", sc->sc_dev.dv_xname));
	return 0;
}

int
lptpushbytes(struct lpt_softc *sc)
{
	bus_space_tag_t iot = sc->sc_iot;
	bus_space_handle_t ioh = sc->sc_ioh;
	int error;

	if (sc->sc_flags & LPT_NOINTR) {
		int msecs, spin;
		u_int8_t control = sc->sc_control;

		while (sc->sc_count > 0) {
			spin = 0;
			if (sc->sc_state == 0)
				return (EIO);
			while (NOT_READY()) {
				if (++spin < sc->sc_spinmax)
					continue;
				msecs = 0;
				/* adapt busy-wait algorithm */
				sc->sc_spinmax++;
				while (NOT_READY_ERR()) {
					/* exponential backoff */
					msecs = msecs + msecs + 10;
					if (msecs > TIMEOUT)
						msecs = TIMEOUT;
					error = tsleep_nsec(sc,
					    LPTPRI | PCATCH, "lptpsh",
					    MSEC_TO_NSEC(msecs));
					if (sc->sc_state == 0)
						error = EIO;
					if (error != EWOULDBLOCK)
						return error;
				}
				break;
			}

			bus_space_write_1(iot, ioh, lpt_data, *sc->sc_cp++);
			bus_space_write_1(iot, ioh, lpt_control,
			    control | LPC_STROBE);
			sc->sc_count--;
			bus_space_write_1(iot, ioh, lpt_control, control);

			/* adapt busy-wait algorithm */
			if (spin*2 + 16 < sc->sc_spinmax)
				sc->sc_spinmax--;
		}
	} else {
		int s;

		while (sc->sc_count > 0) {
			/* if the printer is ready for a char, give it one */
			if ((sc->sc_state & LPT_OBUSY) == 0) {
				LPRINTF(("%s: write %d\n", sc->sc_dev.dv_xname,
				    sc->sc_count));
				s = spltty();
				(void) lptintr(sc);
				splx(s);
			}
			if (sc->sc_state == 0)
				return (EIO);
			error = tsleep_nsec(sc, LPTPRI | PCATCH,
			    "lptwrite2", INFSLP);
			if (sc->sc_state == 0)
				error = EIO;
			if (error)
				return error;
		}
	}
	return 0;
}

/* 
 * Copy a line from user space to a local buffer, then call putc to get the
 * chars moved to the output queue.
 */
int
lptwrite(dev_t dev, struct uio *uio, int flags)
{
	struct lpt_softc *sc = lpt_cd.cd_devs[LPTUNIT(dev)];
	size_t n;
	int error = 0;

	while ((n = ulmin(LPT_BSIZE, uio->uio_resid)) != 0) {
		sc->sc_cp = sc->sc_inbuf;
		error = uiomove(sc->sc_cp, n, uio);
		if (error != 0)
			return error;
		sc->sc_count = n;
		error = lptpushbytes(sc);
		if (error) {
			/*
			 * Return accurate residual if interrupted or timed
			 * out.
			 */
			uio->uio_resid += sc->sc_count;
			sc->sc_count = 0;
			return error;
		}
	}
	return 0;
}

/*
 * Handle printer interrupts which occur when the printer is ready to accept
 * another char.
 */
int
lptintr(void *arg)
{
	struct lpt_softc *sc = arg;
	bus_space_tag_t iot = sc->sc_iot;
	bus_space_handle_t ioh = sc->sc_ioh;

	if (((sc->sc_state & LPT_OPEN) == 0 && sc->sc_count == 0) ||
	    (sc->sc_flags & LPT_NOINTR))
		return 0;

	/* is printer online and ready for output */
	if (NOT_READY() && NOT_READY_ERR())
		return -1;

	if (sc->sc_count) {
		u_int8_t control = sc->sc_control;
		/* send char */
		bus_space_write_1(iot, ioh, lpt_data, *sc->sc_cp++);
		delay (50);
		bus_space_write_1(iot, ioh, lpt_control, control | LPC_STROBE);
		sc->sc_count--;
		bus_space_write_1(iot, ioh, lpt_control, control);
		sc->sc_state |= LPT_OBUSY;
	} else
		sc->sc_state &= ~LPT_OBUSY;

	if (sc->sc_count == 0) {
		/* none, wake up the top half to get more */
		wakeup((caddr_t)sc);
	}

	return 1;
}

int
lpt_activate(struct device *self, int act)
{
	struct lpt_softc *sc = (struct lpt_softc *)self;

	switch (act) {
	case DVACT_SUSPEND:
		timeout_del(&sc->sc_wakeup_tmo);
		break;
	case DVACT_RESUME:
		bus_space_write_1(sc->sc_iot, sc->sc_ioh, lpt_control, LPC_NINIT);

		if (sc->sc_state) {
			int spin;

			if ((sc->sc_flags & LPT_NOPRIME) == 0) {
				/* assert INIT for 100 usec to start up printer */
				bus_space_write_1(sc->sc_iot, sc->sc_ioh,
				    lpt_control, LPC_SELECT);
				delay(100);
			}
			
			bus_space_write_1(sc->sc_iot, sc->sc_ioh, lpt_control,
			    LPC_SELECT | LPC_NINIT);

			/* wait till ready (printer running diagnostics) */
			for (spin = 0; NOT_READY_ERR(); spin += STEP) {
				if (spin >= TIMEOUT) {
					sc->sc_state = 0;
					goto fail;
				}

				/* wait 1/4 second, give up if we get a signal */
				delay(STEP * 1000);
			}

			bus_space_write_1(sc->sc_iot, sc->sc_ioh,
			    lpt_control, sc->sc_control);
			wakeup(sc);
		}
fail:
		break;
	}
 
	return (0);
}
