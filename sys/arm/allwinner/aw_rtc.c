/*-
 * Copyright (c) 2016 Vladimir Belian <fate10@gmail.com>
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

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/bus.h>
#include <sys/time.h>
#include <sys/rman.h>
#include <sys/clock.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/resource.h>

#include <machine/bus.h>
#include <machine/resource.h>

#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>

#include <arm/allwinner/aw_machdep.h>

#include "clock_if.h"

#define	LOSC_CTRL_REG			0x00
#define	A10_RTC_DATE_REG		0x04
#define	A10_RTC_TIME_REG		0x08
#define	A31_LOSC_AUTO_SWT_STA		0x04
#define	A31_RTC_DATE_REG		0x10
#define	A31_RTC_TIME_REG		0x14

#define	TIME_MASK			0x001f3f3f

#define	LOSC_OSC_SRC			(1 << 0)
#define	LOSC_GSM			(1 << 3)
#define	LOSC_AUTO_SW_EN			(1 << 14)
#define	LOSC_MAGIC			0x16aa0000
#define	LOSC_BUSY_MASK			0x00000380

#define	IS_SUN7I			(sc->type == A20_RTC)

#define	YEAR_MIN			(IS_SUN7I ? 1970 : 2010)
#define	YEAR_MAX			(IS_SUN7I ? 2100 : 2073)
#define	YEAR_OFFSET			(IS_SUN7I ? 1900 : 2010)
#define	YEAR_MASK			(IS_SUN7I ? 0xff : 0x3f)
#define	LEAP_BIT			(IS_SUN7I ? 24 : 22)

#define	GET_SEC_VALUE(x)		((x)  & 0x0000003f)
#define	GET_MIN_VALUE(x)		(((x) & 0x00003f00) >> 8)
#define	GET_HOUR_VALUE(x)		(((x) & 0x001f0000) >> 16)
#define	GET_DAY_VALUE(x)		((x)  & 0x0000001f)
#define	GET_MON_VALUE(x)		(((x) & 0x00000f00) >> 8)
#define	GET_YEAR_VALUE(x)		(((x) >> 16) & YEAR_MASK)

#define	SET_DAY_VALUE(x)		GET_DAY_VALUE(x)
#define	SET_MON_VALUE(x)		(((x) & 0x0000000f) << 8)
#define	SET_YEAR_VALUE(x)		(((x) & YEAR_MASK)  << 16)
#define	SET_LEAP_VALUE(x)		(((x) & 0x00000001) << LEAP_BIT)
#define	SET_SEC_VALUE(x)		GET_SEC_VALUE(x)
#define	SET_MIN_VALUE(x)		(((x) & 0x0000003f) << 8)
#define	SET_HOUR_VALUE(x)		(((x) & 0x0000001f) << 16)

#define	HALF_OF_SEC_NS			500000000
#define	RTC_RES_US			1000000
#define	RTC_TIMEOUT			70

#define	RTC_READ(sc, reg) 		bus_read_4((sc)->res, (reg))
#define	RTC_WRITE(sc, reg, val)		bus_write_4((sc)->res, (reg), (val))

#define	IS_LEAP_YEAR(y) \
	(((y) % 400) == 0 || (((y) % 100) != 0 && ((y) % 4) == 0))

#define	A10_RTC	1
#define	A20_RTC	2
#define	A31_RTC	3

static struct ofw_compat_data compat_data[] = {
	{ "allwinner,sun4i-a10-rtc", A10_RTC },
	{ "allwinner,sun7i-a20-rtc", A20_RTC },
	{ "allwinner,sun6i-a31-rtc", A31_RTC },
	{ NULL, 0 }
};

struct aw_rtc_softc {
	struct resource		*res;
	int			type;
	bus_size_t		rtc_date;
	bus_size_t		rtc_time;
};

static int aw_rtc_probe(device_t dev);
static int aw_rtc_attach(device_t dev);
static int aw_rtc_detach(device_t dev);

static int aw_rtc_gettime(device_t dev, struct timespec *ts);
static int aw_rtc_settime(device_t dev, struct timespec *ts);

static device_method_t aw_rtc_methods[] = {
	DEVMETHOD(device_probe,		aw_rtc_probe),
	DEVMETHOD(device_attach,	aw_rtc_attach),
	DEVMETHOD(device_detach,	aw_rtc_detach),

	DEVMETHOD(clock_gettime,	aw_rtc_gettime),
	DEVMETHOD(clock_settime,	aw_rtc_settime),

	DEVMETHOD_END
};

static driver_t aw_rtc_driver = {
	"rtc",
	aw_rtc_methods,
	sizeof(struct aw_rtc_softc),
};

static devclass_t aw_rtc_devclass;

EARLY_DRIVER_MODULE(aw_rtc, simplebus, aw_rtc_driver, aw_rtc_devclass, 0, 0,
    BUS_PASS_TIMER + BUS_PASS_ORDER_MIDDLE);


static int
aw_rtc_probe(device_t dev)
{
	if (!ofw_bus_status_okay(dev))
		return (ENXIO);

	if (!ofw_bus_search_compatible(dev, compat_data)->ocd_data)
		return (ENXIO);

	device_set_desc(dev, "Allwinner RTC");

	return (BUS_PROBE_DEFAULT);
}

static int
aw_rtc_attach(device_t dev)
{
	struct aw_rtc_softc *sc  = device_get_softc(dev);
	bus_size_t rtc_losc_sta;
	uint32_t val;
	int rid = 0;

	sc->res = bus_alloc_resource_any(dev, SYS_RES_MEMORY, &rid, RF_ACTIVE);
	if (!sc->res) {
		device_printf(dev, "could not allocate resources\n");
		return (ENXIO);
	}

	sc->type = ofw_bus_search_compatible(dev, compat_data)->ocd_data;
	switch (sc->type) {
	case A10_RTC:
	case A20_RTC:
		sc->rtc_date = A10_RTC_DATE_REG;
		sc->rtc_time = A10_RTC_TIME_REG;
		rtc_losc_sta = LOSC_CTRL_REG;
		break;
	case A31_RTC:
		sc->rtc_date = A31_RTC_DATE_REG;
		sc->rtc_time = A31_RTC_TIME_REG;
		rtc_losc_sta = A31_LOSC_AUTO_SWT_STA;
		break;
	}
	val = RTC_READ(sc, LOSC_CTRL_REG);
	val |= LOSC_AUTO_SW_EN;
	val |= LOSC_MAGIC | LOSC_GSM | LOSC_OSC_SRC;
	RTC_WRITE(sc, LOSC_CTRL_REG, val);

	DELAY(100);

	if (bootverbose) {
		val = RTC_READ(sc, rtc_losc_sta);
		if ((val & LOSC_OSC_SRC) == 0)
			device_printf(dev, "Using internal oscillator\n");
		else
			device_printf(dev, "Using external oscillator\n");
	}

	clock_register(dev, RTC_RES_US);
	
	return (0);
}

static int
aw_rtc_detach(device_t dev)
{
	/* can't support detach, since there's no clock_unregister function */
	return (EBUSY);
}

static int
aw_rtc_gettime(device_t dev, struct timespec *ts)
{
	struct aw_rtc_softc *sc  = device_get_softc(dev);
	struct clocktime ct;
	uint32_t rdate, rtime;

	rdate = RTC_READ(sc, sc->rtc_date);
	rtime = RTC_READ(sc, sc->rtc_time);
	
	if ((rtime & TIME_MASK) == 0)
		rdate = RTC_READ(sc, sc->rtc_date);

	ct.sec = GET_SEC_VALUE(rtime);
	ct.min = GET_MIN_VALUE(rtime);
	ct.hour = GET_HOUR_VALUE(rtime);
	ct.day = GET_DAY_VALUE(rdate);
	ct.mon = GET_MON_VALUE(rdate);
	ct.year = GET_YEAR_VALUE(rdate) + YEAR_OFFSET;
	ct.dow = -1;
	/* RTC resolution is 1 sec */
	ct.nsec = 0;
	
	return (clock_ct_to_ts(&ct, ts));
}

static int
aw_rtc_settime(device_t dev, struct timespec *ts)
{
	struct aw_rtc_softc *sc  = device_get_softc(dev);
	struct clocktime ct;
	uint32_t clk, rdate, rtime;

	/* RTC resolution is 1 sec */
	if (ts->tv_nsec >= HALF_OF_SEC_NS)
		ts->tv_sec++;
	ts->tv_nsec = 0;

	clock_ts_to_ct(ts, &ct);
	
	if ((ct.year < YEAR_MIN) || (ct.year > YEAR_MAX)) {
		device_printf(dev, "could not set time, year out of range\n");
		return (EINVAL);
	}

	for (clk = 0; RTC_READ(sc, LOSC_CTRL_REG) & LOSC_BUSY_MASK; clk++) {
		if (clk > RTC_TIMEOUT) {
			device_printf(dev, "could not set time, RTC busy\n");
			return (EINVAL);
		}
		DELAY(1);
	}
	/* reset time register to avoid unexpected date increment */
	RTC_WRITE(sc, sc->rtc_time, 0);

	rdate = SET_DAY_VALUE(ct.day) | SET_MON_VALUE(ct.mon) |
		SET_YEAR_VALUE(ct.year - YEAR_OFFSET) | 
		SET_LEAP_VALUE(IS_LEAP_YEAR(ct.year));
			
	rtime = SET_SEC_VALUE(ct.sec) | SET_MIN_VALUE(ct.min) |
		SET_HOUR_VALUE(ct.hour);

	for (clk = 0; RTC_READ(sc, LOSC_CTRL_REG) & LOSC_BUSY_MASK; clk++) {
		if (clk > RTC_TIMEOUT) {
			device_printf(dev, "could not set date, RTC busy\n");
			return (EINVAL);
		}
		DELAY(1);
	}
	RTC_WRITE(sc, sc->rtc_date, rdate);

	for (clk = 0; RTC_READ(sc, LOSC_CTRL_REG) & LOSC_BUSY_MASK; clk++) {
		if (clk > RTC_TIMEOUT) {
			device_printf(dev, "could not set time, RTC busy\n");
			return (EINVAL);
		}
		DELAY(1);
	}
	RTC_WRITE(sc, sc->rtc_time, rtime);

	DELAY(RTC_TIMEOUT);

	return (0);
}
