/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2010 Juli Mallett <jmallett@FreeBSD.org>
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
 *
 * $FreeBSD$
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/clock.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/module.h>

#include <contrib/octeon-sdk/cvmx.h>
#include <contrib/octeon-sdk/cvmx-rtc.h>

#include "clock_if.h"

static int	octeon_rtc_attach(device_t dev);
static int	octeon_rtc_probe(device_t dev);

static int	octeon_rtc_settime(device_t dev, struct timespec *ts);
static int	octeon_rtc_gettime(device_t dev, struct timespec *ts);

static device_method_t octeon_rtc_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		octeon_rtc_probe),
	DEVMETHOD(device_attach,	octeon_rtc_attach),

	/* clock interface */
	DEVMETHOD(clock_gettime,	octeon_rtc_gettime),
	DEVMETHOD(clock_settime,	octeon_rtc_settime),

	{ 0, 0 }
};

static driver_t octeon_rtc_driver = {
	"rtc",
	octeon_rtc_methods,
	0
};
static devclass_t octeon_rtc_devclass;
DRIVER_MODULE(rtc, nexus, octeon_rtc_driver, octeon_rtc_devclass, 0, 0);

static int
octeon_rtc_probe(device_t dev)
{
	cvmx_rtc_options_t supported;

	if (device_get_unit(dev) != 0)
		return (ENXIO);

	supported = cvmx_rtc_supported();
	if (supported == 0)
		return (ENXIO);

	device_set_desc(dev, "Cavium Octeon Realtime Clock");
	return (BUS_PROBE_NOWILDCARD);
}

static int
octeon_rtc_attach(device_t dev)
{
	cvmx_rtc_options_t supported;

	supported = cvmx_rtc_supported();
	if ((supported & CVMX_RTC_READ) == 0)
		return (ENXIO);

	clock_register(dev, 1000000);
	return (0);
}

static int
octeon_rtc_settime(device_t dev, struct timespec *ts)
{
	cvmx_rtc_options_t supported;
	uint32_t status;

	supported = cvmx_rtc_supported();
	if ((supported & CVMX_RTC_WRITE) == 0)
		return (ENOTSUP);

	status = cvmx_rtc_write(ts->tv_sec);
	if (status != 0)
		return (EINVAL);

	return (0);
}

static int
octeon_rtc_gettime(device_t dev, struct timespec *ts)
{
	uint32_t secs;

	secs = cvmx_rtc_read();
	if (secs == 0)
		return (ENOTSUP);

	ts->tv_sec = secs;
	ts->tv_nsec = 0;

	return (0);
}
