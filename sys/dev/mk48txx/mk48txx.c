/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2000 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Paul Kranenburg.
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
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 *	$NetBSD: mk48txx.c,v 1.25 2008/04/28 20:23:50 martin Exp $
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

/*
 * Mostek MK48T02, MK48T08, MK48T18, MK48T37 and MK48T59 time-of-day chip
 * subroutines
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/clock.h>
#include <sys/eventhandler.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/rman.h>
#include <sys/watchdog.h>

#include <machine/bus.h>

#include <dev/mk48txx/mk48txxreg.h>
#include <dev/mk48txx/mk48txxvar.h>

#include "clock_if.h"

static uint8_t	mk48txx_def_nvrd(device_t dev, int off);
static void	mk48txx_def_nvwr(device_t dev, int off, uint8_t v);
static void	mk48txx_watchdog(void *arg, u_int cmd, int *error);

static const struct {
	const char *name;
	bus_size_t nvramsz;
	bus_size_t clkoff;
	u_int flags;
#define	MK48TXX_EXT_REGISTERS	1	/* Has extended register set. */
} mk48txx_models[] = {
	{ "mk48t02", MK48T02_CLKSZ, MK48T02_CLKOFF, 0 },
	{ "mk48t08", MK48T08_CLKSZ, MK48T08_CLKOFF, 0 },
	{ "mk48t18", MK48T18_CLKSZ, MK48T18_CLKOFF, 0 },
	{ "mk48t37", MK48T37_CLKSZ, MK48T37_CLKOFF, MK48TXX_EXT_REGISTERS },
	{ "mk48t59", MK48T59_CLKSZ, MK48T59_CLKOFF, MK48TXX_EXT_REGISTERS },
};

int
mk48txx_attach(device_t dev)
{
	struct mk48txx_softc *sc;
	int i;
	uint8_t wday;

	sc = device_get_softc(dev);

	if (mtx_initialized(&sc->sc_mtx) == 0) {
		device_printf(dev, "%s: mutex not initialized\n", __func__);
		return (ENXIO);
	}

	device_printf(dev, "model %s", sc->sc_model);
	i = sizeof(mk48txx_models) / sizeof(mk48txx_models[0]);
	while (--i >= 0) {
		if (strcmp(sc->sc_model, mk48txx_models[i].name) == 0) {
			break;
		}
	}
	if (i < 0) {
		device_printf(dev, " (unsupported)\n");
		return (ENXIO);
	}
	printf("\n");
	sc->sc_nvramsz = mk48txx_models[i].nvramsz;
	sc->sc_clkoffset = mk48txx_models[i].clkoff;

	if (sc->sc_nvrd == NULL)
		sc->sc_nvrd = mk48txx_def_nvrd;
	if (sc->sc_nvwr == NULL)
		sc->sc_nvwr = mk48txx_def_nvwr;

	if (mk48txx_models[i].flags & MK48TXX_EXT_REGISTERS) {
		mtx_lock(&sc->sc_mtx);
		if ((*sc->sc_nvrd)(dev, sc->sc_clkoffset + MK48TXX_FLAGS) &
		    MK48TXX_FLAGS_BL) {
			mtx_unlock(&sc->sc_mtx);
			device_printf(dev, "%s: battery low\n", __func__);
			return (ENXIO);
		}
		mtx_unlock(&sc->sc_mtx);
	}

	if (sc->sc_flag & MK48TXX_NO_CENT_ADJUST) {
		/*
		 * Use MK48TXX_WDAY_CB instead of manually adjusting the
		 * century.
		 */
		if (!(mk48txx_models[i].flags & MK48TXX_EXT_REGISTERS)) {
			device_printf(dev, "%s: no century bit\n", __func__);
			return (ENXIO);
		} else {
			mtx_lock(&sc->sc_mtx);
			wday = (*sc->sc_nvrd)
			    (dev, sc->sc_clkoffset + MK48TXX_IWDAY);
			wday |= MK48TXX_WDAY_CEB;
			(*sc->sc_nvwr)
			    (dev, sc->sc_clkoffset + MK48TXX_IWDAY, wday);
			mtx_unlock(&sc->sc_mtx);
		}
	}

	clock_register(dev, 1000000);	/* 1 second resolution */

	if ((sc->sc_flag & MK48TXX_WDOG_REGISTER) &&
	    (mk48txx_models[i].flags & MK48TXX_EXT_REGISTERS)) {
		sc->sc_wet = EVENTHANDLER_REGISTER(watchdog_list,
		    mk48txx_watchdog, dev, 0);
		device_printf(dev,
		    "watchdog registered, timeout interval max. 128 sec\n");
	}

	return (0);
}

/*
 * Get time-of-day and convert to a `struct timespec'
 * Return 0 on success; an error number otherwise.
 */
int
mk48txx_gettime(device_t dev, struct timespec *ts)
{
	struct mk48txx_softc *sc;
	bus_size_t clkoff;
	struct clocktime ct;
	int year;
	uint8_t csr;

	sc = device_get_softc(dev);
	clkoff = sc->sc_clkoffset;

	mtx_lock(&sc->sc_mtx);
	/* enable read (stop time) */
	csr = (*sc->sc_nvrd)(dev, clkoff + MK48TXX_ICSR);
	csr |= MK48TXX_CSR_READ;
	(*sc->sc_nvwr)(dev, clkoff + MK48TXX_ICSR, csr);

#define	FROMREG(reg, mask)	((*sc->sc_nvrd)(dev, clkoff + (reg)) & (mask))

	ct.nsec = 0;
	ct.sec = FROMBCD(FROMREG(MK48TXX_ISEC, MK48TXX_SEC_MASK));
	ct.min = FROMBCD(FROMREG(MK48TXX_IMIN, MK48TXX_MIN_MASK));
	ct.hour = FROMBCD(FROMREG(MK48TXX_IHOUR, MK48TXX_HOUR_MASK));
	ct.day = FROMBCD(FROMREG(MK48TXX_IDAY, MK48TXX_DAY_MASK));
#if 0
	/* Map dow from 1 - 7 to 0 - 6; FROMBCD() isn't necessary here. */
	ct.dow = FROMREG(MK48TXX_IWDAY, MK48TXX_WDAY_MASK) - 1;
#else
	/*
	 * Set dow = -1 because some drivers (for example the NetBSD and
	 * OpenBSD mk48txx(4)) don't set it correctly.
	 */
	ct.dow = -1;
#endif
	ct.mon = FROMBCD(FROMREG(MK48TXX_IMON, MK48TXX_MON_MASK));
	year = FROMBCD(FROMREG(MK48TXX_IYEAR, MK48TXX_YEAR_MASK));
	year += sc->sc_year0;
	if (sc->sc_flag & MK48TXX_NO_CENT_ADJUST)
		year += (FROMREG(MK48TXX_IWDAY, MK48TXX_WDAY_CB) >>
		    MK48TXX_WDAY_CB_SHIFT) * 100;
	else if (year < POSIX_BASE_YEAR)
		year += 100;

#undef FROMREG

	ct.year = year;

	/* time wears on */
	csr = (*sc->sc_nvrd)(dev, clkoff + MK48TXX_ICSR);
	csr &= ~MK48TXX_CSR_READ;
	(*sc->sc_nvwr)(dev, clkoff + MK48TXX_ICSR, csr);
	mtx_unlock(&sc->sc_mtx);

	return (clock_ct_to_ts(&ct, ts));
}

/*
 * Set the time-of-day clock based on the value of the `struct timespec' arg.
 * Return 0 on success; an error number otherwise.
 */
int
mk48txx_settime(device_t dev, struct timespec *ts)
{
	struct mk48txx_softc *sc;
	bus_size_t clkoff;
	struct clocktime ct;
	uint8_t csr;
	int cent, year;

	sc = device_get_softc(dev);
	clkoff = sc->sc_clkoffset;

	/* Accuracy is only one second. */
	if (ts->tv_nsec >= 500000000)
		ts->tv_sec++;
	ts->tv_nsec = 0;
	clock_ts_to_ct(ts, &ct);

	mtx_lock(&sc->sc_mtx);
	/* enable write */
	csr = (*sc->sc_nvrd)(dev, clkoff + MK48TXX_ICSR);
	csr |= MK48TXX_CSR_WRITE;
	(*sc->sc_nvwr)(dev, clkoff + MK48TXX_ICSR, csr);

#define	TOREG(reg, mask, val)						\
	((*sc->sc_nvwr)(dev, clkoff + (reg),				\
	((*sc->sc_nvrd)(dev, clkoff + (reg)) & ~(mask)) |		\
	((val) & (mask))))

	TOREG(MK48TXX_ISEC, MK48TXX_SEC_MASK, TOBCD(ct.sec));
	TOREG(MK48TXX_IMIN, MK48TXX_MIN_MASK, TOBCD(ct.min));
	TOREG(MK48TXX_IHOUR, MK48TXX_HOUR_MASK, TOBCD(ct.hour));
	/* Map dow from 0 - 6 to 1 - 7; TOBCD() isn't necessary here. */
	TOREG(MK48TXX_IWDAY, MK48TXX_WDAY_MASK, ct.dow + 1);
	TOREG(MK48TXX_IDAY, MK48TXX_DAY_MASK, TOBCD(ct.day));
	TOREG(MK48TXX_IMON, MK48TXX_MON_MASK, TOBCD(ct.mon));

	year = ct.year - sc->sc_year0;
	if (sc->sc_flag & MK48TXX_NO_CENT_ADJUST) {
		cent = year / 100;
		TOREG(MK48TXX_IWDAY, MK48TXX_WDAY_CB,
		    cent << MK48TXX_WDAY_CB_SHIFT);
		year -= cent * 100;
	} else if (year > 99)
		year -= 100;
	TOREG(MK48TXX_IYEAR, MK48TXX_YEAR_MASK, TOBCD(year));

#undef TOREG

	/* load them up */
	csr = (*sc->sc_nvrd)(dev, clkoff + MK48TXX_ICSR);
	csr &= ~MK48TXX_CSR_WRITE;
	(*sc->sc_nvwr)(dev, clkoff + MK48TXX_ICSR, csr);
	mtx_unlock(&sc->sc_mtx);
	return (0);
}

static uint8_t
mk48txx_def_nvrd(device_t dev, int off)
{
	struct mk48txx_softc *sc;

	sc = device_get_softc(dev);
	return (bus_read_1(sc->sc_res, off));
}

static void
mk48txx_def_nvwr(device_t dev, int off, uint8_t v)
{
	struct mk48txx_softc *sc;

	sc = device_get_softc(dev);
	bus_write_1(sc->sc_res, off, v);
}

static void
mk48txx_watchdog(void *arg, u_int cmd, int *error)
{
	device_t dev;
	struct mk48txx_softc *sc;
	uint8_t t, wdog;

	dev = arg;
	sc = device_get_softc(dev);

	t = cmd & WD_INTERVAL;
	if (t >= 26 && t <= 37) {
		wdog = 0;
		if (t <= WD_TO_2SEC) {
			wdog |= MK48TXX_WDOG_RB_1_16;
			t -= 26;
		} else if (t <= WD_TO_8SEC) {
			wdog |= MK48TXX_WDOG_RB_1_4;
			t -= WD_TO_250MS;
		} else if (t <= WD_TO_32SEC) {
			wdog |= MK48TXX_WDOG_RB_1;
			t -= WD_TO_1SEC;
		} else {
			wdog |= MK48TXX_WDOG_RB_4;
			t -= WD_TO_4SEC;
		}
		wdog |= (min(1 << t,
		    MK48TXX_WDOG_BMB_MASK >> MK48TXX_WDOG_BMB_SHIFT)) <<
		    MK48TXX_WDOG_BMB_SHIFT;
		if (sc->sc_flag & MK48TXX_WDOG_ENABLE_WDS)
			wdog |= MK48TXX_WDOG_WDS;
		*error = 0;
	} else {
		wdog = 0;
	}
	mtx_lock(&sc->sc_mtx);
	(*sc->sc_nvwr)(dev, sc->sc_clkoffset + MK48TXX_WDOG, wdog);
	mtx_unlock(&sc->sc_mtx);
}
