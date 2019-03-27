/*-
 * Copyright (c) 2017 Andrew Turner
 * All rights reserved.
 *
 * This software was developed by SRI International and the University of
 * Cambridge Computer Laboratory under DARPA/AFRL contract FA8750-10-C-0237
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
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/clock.h>
#include <sys/efi.h>
#include <sys/kernel.h>
#include <sys/module.h>

#include "clock_if.h"

static bool efirtc_zeroes_subseconds;
static struct timespec efirtc_resadj;

static const u_int us_per_s  = 1000000;
static const u_int ns_per_s  = 1000000000;
static const u_int ns_per_us = 1000;

static void
efirtc_identify(driver_t *driver, device_t parent)
{

	/* Don't add the driver unless we have working runtime services. */
	if (efi_rt_ok() != 0)
		return;
	if (device_find_child(parent, "efirtc", -1) != NULL)
		return;
	if (BUS_ADD_CHILD(parent, 0, "efirtc", -1) == NULL)
		device_printf(parent, "add child failed\n");
}

static int
efirtc_probe(device_t dev)
{
	struct efi_tm tm;
	int error;

	/*
	 * Check whether we can read the time.  This will stop us from attaching
	 * when there is EFI Runtime support but the gettime function is
	 * unimplemented, e.g. on some builds of U-Boot.
	 */
	if ((error = efi_get_time(&tm)) != 0) {
		if (bootverbose)
			device_printf(dev, "cannot read EFI realtime clock, "
			    "error %d\n", error);
		return (error);
	}
	device_set_desc(dev, "EFI Realtime Clock");
	return (BUS_PROBE_DEFAULT);
}

static int
efirtc_attach(device_t dev)
{
	struct efi_tmcap tmcap;
	long res;
	int error;

	bzero(&tmcap, sizeof(tmcap));
	if ((error = efi_get_time_capabilities(&tmcap)) != 0) {
		device_printf(dev, "cannot get EFI time capabilities");
		return (error);
	}

	/* Translate resolution in Hz to tick length in usec. */
	if (tmcap.tc_res == 0)
		res = us_per_s; /* 0 is insane, assume 1 Hz. */
	else if (tmcap.tc_res > us_per_s)
		res = 1; /* 1us is the best we can represent */
	else
		res = us_per_s / tmcap.tc_res;

	/* Clock rounding adjustment is 1/2 of resolution, in nsec. */
	efirtc_resadj.tv_nsec = (res * ns_per_us) / 2;

	/* Does the clock zero the subseconds when time is set? */
	efirtc_zeroes_subseconds = tmcap.tc_stz;

	/*
	 * Register.  If the clock zeroes out the subseconds when it's set,
	 * schedule the SetTime calls to happen just before top-of-second.
	 */
	clock_register_flags(dev, res, CLOCKF_SETTIME_NO_ADJ);
	if (efirtc_zeroes_subseconds)
		clock_schedule(dev, ns_per_s - ns_per_us);

	return (0);
}

static int
efirtc_detach(device_t dev)
{

	clock_unregister(dev);
	return (0);
}

static int
efirtc_gettime(device_t dev, struct timespec *ts)
{
	struct clocktime ct;
	struct efi_tm tm;
	int error;

	error = efi_get_time(&tm);
	if (error != 0)
		return (error);

	ct.sec = tm.tm_sec;
	ct.min = tm.tm_min;
	ct.hour = tm.tm_hour;
	ct.day = tm.tm_mday;
	ct.mon = tm.tm_mon;
	ct.year = tm.tm_year;
	ct.nsec = tm.tm_nsec;

	clock_dbgprint_ct(dev, CLOCK_DBG_READ, &ct);
	return (clock_ct_to_ts(&ct, ts));
}

static int
efirtc_settime(device_t dev, struct timespec *ts)
{
	struct clocktime ct;
	struct efi_tm tm;

	/*
	 * We request a timespec with no resolution-adjustment so that we can
	 * apply it ourselves based on whether or not the clock zeroes the
	 * sub-second part of the time when setting the time.
	 */
	ts->tv_sec -= utc_offset();
	if (!efirtc_zeroes_subseconds)
		timespecadd(ts, &efirtc_resadj, ts);
	
	clock_ts_to_ct(ts, &ct);
	clock_dbgprint_ct(dev, CLOCK_DBG_WRITE, &ct);

	bzero(&tm, sizeof(tm));
	tm.tm_sec = ct.sec;
	tm.tm_min = ct.min;
	tm.tm_hour = ct.hour;
	tm.tm_mday = ct.day;
	tm.tm_mon = ct.mon;
	tm.tm_year = ct.year;
	tm.tm_nsec = ct.nsec;

	return (efi_set_time(&tm));
}

static device_method_t efirtc_methods[] = {
	/* Device interface */
	DEVMETHOD(device_identify,	efirtc_identify),
	DEVMETHOD(device_probe,		efirtc_probe),
	DEVMETHOD(device_attach,	efirtc_attach),
	DEVMETHOD(device_detach,	efirtc_detach),

	/* Clock interface */
	DEVMETHOD(clock_gettime,	efirtc_gettime),
	DEVMETHOD(clock_settime,	efirtc_settime),

	DEVMETHOD_END
};

static devclass_t efirtc_devclass;
static driver_t efirtc_driver = {
	"efirtc",
	efirtc_methods,
	0
};

DRIVER_MODULE(efirtc, nexus, efirtc_driver, efirtc_devclass, 0, 0);
MODULE_VERSION(efirtc, 1);
MODULE_DEPEND(efirtc, efirt, 1, 1, 1);
