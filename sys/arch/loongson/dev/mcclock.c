/*	$OpenBSD: mcclock.c,v 1.6 2022/10/15 14:58:54 jsg Exp $	*/
/*	$NetBSD: mcclock.c,v 1.4 1996/10/13 02:59:41 christos Exp $	*/

/*
 * Copyright (c) 1994, 1995, 1996 Carnegie-Mellon University.
 * All rights reserved.
 *
 * Author: Chris G. Demetriou
 *
 * Permission to use, copy, modify and distribute this software and
 * its documentation is hereby granted, provided that both the copyright
 * notice and this permission notice appear in all copies of the
 * software, derivative works or modified versions, and any portions
 * thereof, and that both notices appear in supporting documentation.
 *
 * CARNEGIE MELLON ALLOWS FREE USE OF THIS SOFTWARE IN ITS "AS IS"
 * CONDITION.  CARNEGIE MELLON DISCLAIMS ANY LIABILITY OF ANY KIND
 * FOR ANY DAMAGES WHATSOEVER RESULTING FROM THE USE OF THIS SOFTWARE.
 *
 * Carnegie Mellon requests users of this software to return to
 *
 *  Software Distribution Coordinator  or  Software.Distribution@CS.CMU.EDU
 *  School of Computer Science
 *  Carnegie Mellon University
 *  Pittsburgh PA 15213-3890
 *
 * any improvements or extensions that they make and grant Carnegie the
 * rights to redistribute these changes.
 */

#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/systm.h>
#include <sys/device.h>

#include <dev/clock_subr.h>
#include <dev/ic/mc146818reg.h>

#include <loongson/dev/mcclockvar.h>

struct cfdriver mcclock_cd = {
	NULL, "mcclock", DV_DULL,
};

int	mcclock_gettime(struct todr_chip_handle *, struct timeval *);
int	mcclock_settime(struct todr_chip_handle *, struct timeval *);

#define	mc146818_write(dev, reg, datum)					\
	    (*(dev)->sc_busfns->mc_bf_write)(dev, reg, datum)
#define	mc146818_read(dev, reg)						\
	    (*(dev)->sc_busfns->mc_bf_read)(dev, reg)

void
mcclock_attach(struct mcclock_softc *sc, const struct mcclock_busfns *busfns)
{

	printf(": mc146818 or compatible\n");

	sc->sc_busfns = busfns;

	/* Turn interrupts off, just in case. */
	mc146818_write(sc, MC_REGB, MC_REGB_BINARY | MC_REGB_24HR);
	mc146818_write(sc, MC_REGA, MC_BASE_32_KHz | MC_RATE_NONE);

	sc->sc_todr.cookie = sc;
	sc->sc_todr.todr_gettime = mcclock_gettime;
	sc->sc_todr.todr_settime = mcclock_settime;
	sc->sc_todr.todr_quality = 0;
	todr_attach(&sc->sc_todr);
}

/*
 * Get the time of day, based on the clock's value and/or the base value.
 */
int
mcclock_gettime(struct todr_chip_handle *handle, struct timeval *tv)
{
	struct clock_ymdhms dt;
	struct mcclock_softc *sc = handle->cookie;
	mc_todregs regs;
	int s;

	s = splclock();
	MC146818_GETTOD(sc, &regs)
	splx(s);

	dt.dt_sec = regs[MC_SEC];
	dt.dt_min = regs[MC_MIN];
	dt.dt_hour = regs[MC_HOUR];
	dt.dt_day = regs[MC_DOM];
	dt.dt_mon = regs[MC_MONTH];
	dt.dt_year = regs[MC_YEAR] + 2000;

	tv->tv_sec = clock_ymdhms_to_secs(&dt);
	tv->tv_usec = 0;
	return 0;
}

/*
 * Reset the TODR based on the time value.
 */
int
mcclock_settime(struct todr_chip_handle *handle, struct timeval *tv)
{
	struct clock_ymdhms dt;
	struct mcclock_softc *sc = handle->cookie;
	mc_todregs regs;
	int s;

	clock_secs_to_ymdhms(tv->tv_sec, &dt);

	s = splclock();
	MC146818_GETTOD(sc, &regs);
	splx(s);

	regs[MC_SEC] = dt.dt_sec;
	regs[MC_MIN] = dt.dt_min;
	regs[MC_HOUR] = dt.dt_hour;
	regs[MC_DOW] = dt.dt_wday + 1;
	regs[MC_DOM] = dt.dt_day;
	regs[MC_MONTH] = dt.dt_mon;
	regs[MC_YEAR] = dt.dt_year % 100;

	s = splclock();
	MC146818_PUTTOD(sc, &regs);
	splx(s);

	return 0;
}
