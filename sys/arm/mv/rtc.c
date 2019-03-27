/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (C) 2008 MARVELL INTERNATIONAL LTD.
 * All rights reserved.
 *
 * Developed by Semihalf.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of MARVELL nor the names of contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL AUTHOR OR CONTRIBUTORS BE LIABLE
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
#include <sys/lock.h>
#include <sys/time.h>
#include <sys/clock.h>
#include <sys/resource.h>
#include <sys/systm.h>
#include <sys/rman.h>
#include <sys/kernel.h>
#include <sys/module.h>

#include <machine/bus.h>
#include <machine/resource.h>

#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>

#include "clock_if.h"

#define MV_RTC_TIME_REG		0x00
#define MV_RTC_DATE_REG		0x04
#define YEAR_BASE		2000

struct mv_rtc_softc {
	device_t dev;
	struct resource *res[1];
};

static struct resource_spec res_spec[] = {
	{ SYS_RES_MEMORY, 0, RF_ACTIVE },
	{ -1, 0 }
};

static int mv_rtc_probe(device_t dev);
static int mv_rtc_attach(device_t dev);

static int mv_rtc_gettime(device_t dev, struct timespec *ts);
static int mv_rtc_settime(device_t dev, struct timespec *ts);

static uint32_t mv_rtc_reg_read(struct mv_rtc_softc *sc, bus_size_t off);
static int mv_rtc_reg_write(struct mv_rtc_softc *sc, bus_size_t off,
    uint32_t val);

static device_method_t mv_rtc_methods[] = {
	DEVMETHOD(device_probe,		mv_rtc_probe),
	DEVMETHOD(device_attach,	mv_rtc_attach),

	DEVMETHOD(clock_gettime,	mv_rtc_gettime),
	DEVMETHOD(clock_settime,	mv_rtc_settime),

	{ 0, 0 },
};

static driver_t mv_rtc_driver = {
	"rtc",
	mv_rtc_methods,
	sizeof(struct mv_rtc_softc),
};
static devclass_t mv_rtc_devclass;

DRIVER_MODULE(mv_rtc, simplebus, mv_rtc_driver, mv_rtc_devclass, 0, 0);

static int
mv_rtc_probe(device_t dev)
{

	if (!ofw_bus_status_okay(dev))
		return (ENXIO);

	if (!ofw_bus_is_compatible(dev, "mrvl,rtc"))
		return (ENXIO);

	device_set_desc(dev, "Marvell Integrated RTC");
	return (BUS_PROBE_DEFAULT);
}

static int
mv_rtc_attach(device_t dev)
{
	struct mv_rtc_softc *sc;

	sc = device_get_softc(dev);
	sc->dev = dev;

	clock_register(dev, 1000000);

	if (bus_alloc_resources(dev, res_spec, sc->res)) {
		device_printf(dev, "could not allocate resources\n");
		return (ENXIO);
	}

	return (0);
}

static int
mv_rtc_gettime(device_t dev, struct timespec *ts)
{
	struct clocktime ct;
	struct mv_rtc_softc *sc;
	uint32_t val;

	sc = device_get_softc(dev);

	val = mv_rtc_reg_read(sc, MV_RTC_TIME_REG);

	ct.nsec = 0;
	ct.sec = FROMBCD(val & 0x7f);
	ct.min = FROMBCD((val & 0x7f00) >> 8);
	ct.hour = FROMBCD((val & 0x3f0000) >> 16);
	ct.dow = FROMBCD((val & 0x7000000) >> 24) - 1;

	val = mv_rtc_reg_read(sc, MV_RTC_DATE_REG);

	ct.day = FROMBCD(val & 0x7f);
	ct.mon = FROMBCD((val & 0x1f00) >> 8);
	ct.year = YEAR_BASE + FROMBCD((val & 0xff0000) >> 16);

	return (clock_ct_to_ts(&ct, ts));
}

static int
mv_rtc_settime(device_t dev, struct timespec *ts)
{
	struct clocktime ct;
	struct mv_rtc_softc *sc;
	uint32_t val;

	sc = device_get_softc(dev);

	/* Resolution: 1 sec */
	if (ts->tv_nsec >= 500000000)
		ts->tv_sec++;
	ts->tv_nsec = 0;
	clock_ts_to_ct(ts, &ct);

	val = TOBCD(ct.sec) | (TOBCD(ct.min) << 8) |
	    (TOBCD(ct.hour) << 16) | (TOBCD( ct.dow + 1) << 24);
	mv_rtc_reg_write(sc, MV_RTC_TIME_REG, val);

	val = TOBCD(ct.day) | (TOBCD(ct.mon) << 8) |
	    (TOBCD(ct.year - YEAR_BASE) << 16);
	mv_rtc_reg_write(sc, MV_RTC_DATE_REG, val);

	return (0);
}

static uint32_t
mv_rtc_reg_read(struct mv_rtc_softc *sc, bus_size_t off)
{

	return (bus_read_4(sc->res[0], off));
}

static int
mv_rtc_reg_write(struct mv_rtc_softc *sc, bus_size_t off, uint32_t val)
{

	bus_write_4(sc->res[0], off, val);
	return (0);
}
