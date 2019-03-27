/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2011 Nathan Whitehorn
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
#include <sys/module.h>
#include <sys/bus.h>
#include <sys/conf.h>
#include <sys/clock.h>
#include <sys/cpu.h>
#include <sys/kernel.h>
#include <sys/reboot.h>
#include <sys/sysctl.h>

#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/openfirm.h>

#include <machine/rtas.h>

#include "clock_if.h"

static int	rtasdev_probe(device_t);
static int	rtasdev_attach(device_t);
/* clock interface */
static int	rtas_gettime(device_t dev, struct timespec *ts);
static int	rtas_settime(device_t dev, struct timespec *ts);

static void	rtas_shutdown(void *arg, int howto);

static device_method_t  rtasdev_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		rtasdev_probe),
	DEVMETHOD(device_attach,	rtasdev_attach),

	/* clock interface */
	DEVMETHOD(clock_gettime,	rtas_gettime),
	DEVMETHOD(clock_settime,	rtas_settime),
	
	{ 0, 0 },
};

static driver_t rtasdev_driver = {
	"rtas",
	rtasdev_methods,
	0
};

static devclass_t rtasdev_devclass;

DRIVER_MODULE(rtasdev, ofwbus, rtasdev_driver, rtasdev_devclass, 0, 0);

static int
rtasdev_probe(device_t dev)
{
	const char *name = ofw_bus_get_name(dev);

	if (strcmp(name, "rtas") != 0)
		return (ENXIO);
	if (!rtas_exists())
		return (ENXIO);

	device_set_desc(dev, "Run-Time Abstraction Services");
	return (0);
}

static int
rtasdev_attach(device_t dev)
{
	if (rtas_token_lookup("get-time-of-day") != -1)
		clock_register(dev, 2000);
	
	EVENTHANDLER_REGISTER(shutdown_final, rtas_shutdown, NULL,
	    SHUTDOWN_PRI_LAST);

	return (0);
}

static int
rtas_gettime(device_t dev, struct timespec *ts) {
	struct clocktime ct;
	cell_t tod[8];
	cell_t token;
	int error;
	
	token = rtas_token_lookup("get-time-of-day");
	if (token == -1)
		return (ENXIO);
	error = rtas_call_method(token, 0, 8, &tod[0], &tod[1], &tod[2],
	    &tod[3], &tod[4], &tod[5], &tod[6], &tod[7]);
	if (error < 0)
		return (ENXIO);
	if (tod[0] != 0)
		return ((tod[0] == -1) ? ENXIO : EAGAIN);

	ct.year = tod[1];
	ct.mon  = tod[2];
	ct.day  = tod[3];
	ct.hour = tod[4];
	ct.min  = tod[5];
	ct.sec  = tod[6];
	ct.nsec = tod[7];

	return (clock_ct_to_ts(&ct, ts));
}

static int
rtas_settime(device_t dev, struct timespec *ts)
{
	struct clocktime ct;
	cell_t token, status;
	int error;
	
	token = rtas_token_lookup("set-time-of-day");
	if (token == -1)
		return (ENXIO);

	clock_ts_to_ct(ts, &ct);
	error = rtas_call_method(token, 7, 1, ct.year, ct.mon, ct.day, ct.hour,
	    ct.min, ct.sec, ct.nsec, &status);
	if (error < 0)
		return (ENXIO);
	if (status != 0)
		return (((int)status < 0) ? ENXIO : EAGAIN);

	return (0);
}

static void
rtas_shutdown(void *arg, int howto)
{
	cell_t token, status;
	
	if (howto & RB_HALT) {
		token = rtas_token_lookup("power-off");
		if (token == -1)
			return;

		rtas_call_method(token, 2, 1, 0, 0, &status);
	} else {
		token = rtas_token_lookup("system-reboot");
		if (token == -1)
			return;

		rtas_call_method(token, 0, 1, &status);
	}
}

