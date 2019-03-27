/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2004 Marius Strobl <marius@FreeBSD.org>
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

/*
 * The `rtc' device is found on the ISA bus and the EBus.  The ISA version
 * always is a MC146818 compatible clock while the EBus variant either is the
 * MC146818 compatible Real-Time Clock function of a National Semiconductor
 * PC87317/PC97317 which also provides Advanced Power Control functionality
 * or a Texas Instruments bq4802.
 */

#include "opt_isa.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/module.h>
#include <sys/mutex.h>
#include <sys/resource.h>

#include <dev/ofw/ofw_bus.h>

#include <machine/bus.h>
#include <machine/resource.h>

#include <sys/rman.h>

#include <isa/isavar.h>

#include <dev/mc146818/mc146818reg.h>
#include <dev/mc146818/mc146818var.h>

#include "clock_if.h"

#define	RTC_DESC	"Real-Time Clock"

#define	RTC_READ	mc146818_def_read
#define	RTC_WRITE	mc146818_def_write

#define	PC87317_COMMON		MC_REGA_DV0	/* bank 0 */
#define	PC87317_RTC		(MC_REGA_DV1 | MC_REGA_DV0) /* bank 1 */
#define	PC87317_RTC_CR		0x48		/* Century Register */
#define	PC87317_APC		MC_REGA_DV2	/* bank 2 */
#define	PC87317_APC_CADDR	0x51		/* Century Address Register */
#define	PC87317_APC_CADDR_BANK0	0x00		/* locate CR in bank 0 */
#define	PC87317_APC_CADDR_BANK1	0x80		/* locate CR in bank 1 */

static devclass_t rtc_devclass;

static device_attach_t rtc_attach;
static device_probe_t rtc_ebus_probe;
#ifdef DEV_ISA
static device_probe_t rtc_isa_probe;
#endif

static device_method_t rtc_ebus_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		rtc_ebus_probe),
	DEVMETHOD(device_attach,	rtc_attach),

	/* clock interface */
	DEVMETHOD(clock_gettime,	mc146818_gettime),
	DEVMETHOD(clock_settime,	mc146818_settime),

	DEVMETHOD_END
};

static driver_t rtc_ebus_driver = {
	"rtc",
	rtc_ebus_methods,
	sizeof(struct mc146818_softc),
};

DRIVER_MODULE(rtc, ebus, rtc_ebus_driver, rtc_devclass, 0, 0);

#ifdef DEV_ISA
static device_method_t rtc_isa_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		rtc_isa_probe),
	DEVMETHOD(device_attach,	rtc_attach),

	/* clock interface */
	DEVMETHOD(clock_gettime,	mc146818_gettime),
	DEVMETHOD(clock_settime,	mc146818_settime),

	DEVMETHOD_END
};

static driver_t rtc_isa_driver = {
	"rtc",
	rtc_isa_methods,
	sizeof(struct mc146818_softc),
};

static struct isa_pnp_id rtc_isa_ids[] = {
	{ 0x000bd041, RTC_DESC }, /* PNP0B00 */
	{ 0 }
};

DRIVER_MODULE(rtc, isa, rtc_isa_driver, rtc_devclass, 0, 0);
ISA_PNP_INFO(rtc_isa_ids);
#endif

static u_int pc87317_getcent(device_t dev);
static void pc87317_setcent(device_t dev, u_int cent);

static int
rtc_ebus_probe(device_t dev)
{

	if (strcmp(ofw_bus_get_name(dev), "rtc") == 0) {
		/* The bq4802 is not supported, yet. */
		if (ofw_bus_get_compat(dev) != NULL &&
		    strcmp(ofw_bus_get_compat(dev), "bq4802") == 0)
			return (ENXIO);
		device_set_desc(dev, RTC_DESC);
		return (0);
	}

	return (ENXIO);
}

#ifdef DEV_ISA
static int
rtc_isa_probe(device_t dev)
{

	if (ISA_PNP_PROBE(device_get_parent(dev), dev, rtc_isa_ids) == 0)
		return (0);

	return (ENXIO);
}
#endif

static int
rtc_attach(device_t dev)
{
	struct timespec ts;
	struct mc146818_softc *sc;
	struct resource *res;
	int ebus, error, rid;

	sc = device_get_softc(dev);

	mtx_init(&sc->sc_mtx, "rtc_mtx", NULL, MTX_SPIN);

	ebus = 0;
	if (strcmp(device_get_name(device_get_parent(dev)), "ebus") == 0)
		ebus = 1;

	rid = 0;
	res = bus_alloc_resource_any(dev, ebus ? SYS_RES_MEMORY :
	    SYS_RES_IOPORT, &rid, RF_ACTIVE);
	if (res == NULL) {
		device_printf(dev, "cannot allocate resources\n");
		error = ENXIO;
		goto fail_mtx;
	}
	sc->sc_bst = rman_get_bustag(res);
	sc->sc_bsh = rman_get_bushandle(res);

	sc->sc_mcread = RTC_READ;
	sc->sc_mcwrite = RTC_WRITE;
	/* The TOD clock year 0 is 0. */
	sc->sc_year0 = 0;
	/*
	 * For ISA use the default century get/set functions, for EBus we
	 * provide our own versions.
	 */
	sc->sc_flag = MC146818_NO_CENT_ADJUST;
	if (ebus) {
		/*
		 * Make sure the CR is at the default location (also used
		 * by Solaris).
		 */
		RTC_WRITE(dev, MC_REGA, PC87317_APC);
		RTC_WRITE(dev, PC87317_APC_CADDR, PC87317_APC_CADDR_BANK1 |
		    PC87317_RTC_CR);
		RTC_WRITE(dev, MC_REGA, PC87317_COMMON);
		sc->sc_getcent = pc87317_getcent;
		sc->sc_setcent = pc87317_setcent;
	}
	if ((error = mc146818_attach(dev)) != 0) {
		device_printf(dev, "cannot attach time of day clock\n");
		goto fail_res;
	}

	if (bootverbose) {
		if (mc146818_gettime(dev, &ts) != 0)
			device_printf(dev, "invalid time");
		else
			device_printf(dev, "current time: %ld.%09ld\n",
			    (long)ts.tv_sec, ts.tv_nsec);
	}

	return (0);

 fail_res:
	bus_release_resource(dev, ebus ? SYS_RES_MEMORY : SYS_RES_IOPORT, rid,
	    res);
 fail_mtx:
	mtx_destroy(&sc->sc_mtx);

	return (error);
}

static u_int
pc87317_getcent(device_t dev)
{
	u_int cent;

	RTC_WRITE(dev, MC_REGA, PC87317_RTC);
	cent = RTC_READ(dev, PC87317_RTC_CR);
	RTC_WRITE(dev, MC_REGA, PC87317_COMMON);
	return (cent);
}

static void
pc87317_setcent(device_t dev, u_int cent)
{

	RTC_WRITE(dev, MC_REGA, PC87317_RTC);
	RTC_WRITE(dev, PC87317_RTC_CR, cent);
	RTC_WRITE(dev, MC_REGA, PC87317_COMMON);
}
