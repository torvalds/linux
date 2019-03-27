/*-
 * Copyright (c) 2016 Michal Meloun <mmel@FreeBSD.org>
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
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/clock.h>
#include <sys/kernel.h>

#include <dev/ofw/ofw_bus.h>

#include "clock_if.h"
#include "as3722.h"

#define	AS3722_RTC_START_YEAR	2000

int
as3722_rtc_gettime(device_t dev, struct timespec *ts)
{
	struct as3722_softc *sc;
	struct clocktime ct;
	uint8_t buf[6];
	int rv;

	sc = device_get_softc(dev);

	rv = as3722_read_buf(sc, AS3722_RTC_SECOND, buf, 6);
	if (rv != 0) {
		device_printf(sc->dev, "Failed to read RTC data\n");
		return (rv);
	}
	ct.nsec = 0;
	ct.sec = bcd2bin(buf[0] & 0x7F);
	ct.min = bcd2bin(buf[1] & 0x7F);
	ct.hour = bcd2bin(buf[2] & 0x3F);
	ct.day = bcd2bin(buf[3] & 0x3F);
	ct.mon = bcd2bin(buf[4] & 0x1F);
	ct.year = bcd2bin(buf[5] & 0x7F) + AS3722_RTC_START_YEAR;
	ct.dow = -1;

	return clock_ct_to_ts(&ct, ts);
}

int
as3722_rtc_settime(device_t dev, struct timespec *ts)
{
	struct as3722_softc *sc;
	struct clocktime ct;
	uint8_t buf[6];
	int rv;

	sc = device_get_softc(dev);
	clock_ts_to_ct(ts, &ct);

	if (ct.year < AS3722_RTC_START_YEAR)
		return (EINVAL);

	buf[0] = bin2bcd(ct.sec);
	buf[1] = bin2bcd(ct.min);
	buf[2] = bin2bcd(ct.hour);
	buf[3] = bin2bcd(ct.day);
	buf[4] = bin2bcd(ct.mon);
	buf[5] = bin2bcd(ct.year - AS3722_RTC_START_YEAR);

	rv = as3722_write_buf(sc, AS3722_RTC_SECOND, buf, 6);
	if (rv != 0) {
		device_printf(sc->dev, "Failed to write RTC data\n");
		return (rv);
	}
	return (0);
}

int
as3722_rtc_attach(struct as3722_softc *sc, phandle_t node)
{
	int rv;

	/* Enable RTC, set 24 hours mode  and alarms */
	rv = RM1(sc, AS3722_RTC_CONTROL,
	    AS3722_RTC_ON | AS3722_RTC_ALARM_WAKEUP_EN | AS3722_RTC_AM_PM_MODE,
	    AS3722_RTC_ON | AS3722_RTC_ALARM_WAKEUP_EN);
	if (rv < 0) {
		device_printf(sc->dev, "Failed to initialize RTC controller\n");
		return (ENXIO);
	}
	clock_register(sc->dev, 1000000);

	return (0);
}
