/*-
 * Copyright (c) 2017 Ian Lepore <ian@freebsd.org>
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
 * Driver for imx6 Secure Non-Volatile Storage system, which really means "all
 * the stuff that's powered by a battery when main power is off".  This includes
 * realtime clock, tamper monitor, and power-management functions.  Currently
 * this driver provides only realtime clock support.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/clock.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <machine/bus.h>

#include <dev/ofw/ofw_bus_subr.h>

#include "clock_if.h"

#define	SNVS_LPCR		0x38		/* Control register */
#define	  LPCR_LPCALB_VAL_SHIFT	  10		/* Calibration shift */
#define	  LPCR_LPCALB_VAL_MASK	  0x1f		/* Calibration mask */
#define	  LPCR_LPCALB_EN	  (1u << 8)	/* Calibration enable */
#define	  LPCR_SRTC_ENV		  (1u << 0)	/* RTC enabled/valid */

#define	SNVS_LPSRTCMR		0x50		/* Counter MSB */
#define	SNVS_LPSRTCLR		0x54		/* Counter LSB */

#define	RTC_RESOLUTION_US	(1000000 / 32768) /* 32khz clock */

/*
 * The RTC is a 47-bit counter clocked at 32KHz and organized as a 32.15
 * fixed-point binary value.  Shifting by SBT_LSB bits translates between
 * counter and sbintime values.
 */
#define	RTC_BITS	47
#define	SBT_BITS	64
#define	SBT_LSB		(SBT_BITS - RTC_BITS)

struct snvs_softc {
	device_t 		dev;
	struct resource *	memres;
	uint32_t		lpcr;
};

static struct ofw_compat_data compat_data[] = {
	{"fsl,sec-v4.0-mon-rtc-lp", true},
	{"fsl,sec-v4.0-mon", true},
	{NULL,               false}
};

static inline uint32_t
RD4(struct snvs_softc *sc, bus_size_t offset)
{

	return (bus_read_4(sc->memres, offset));
}

static inline void
WR4(struct snvs_softc *sc, bus_size_t offset, uint32_t value)
{

	bus_write_4(sc->memres, offset, value);
}

static void
snvs_rtc_enable(struct snvs_softc *sc, bool enable)
{
	uint32_t enbit;

	if (enable)
		sc->lpcr |= LPCR_SRTC_ENV;
	else
		sc->lpcr &= ~LPCR_SRTC_ENV;
	WR4(sc, SNVS_LPCR, sc->lpcr);

	/* Wait for the hardware to achieve the requested state. */
	enbit = sc->lpcr & LPCR_SRTC_ENV;
	while ((RD4(sc, SNVS_LPCR) & LPCR_SRTC_ENV) != enbit)
		continue;
}

static int
snvs_gettime(device_t dev, struct timespec *ts)
{
	struct snvs_softc *sc;
	sbintime_t counter1, counter2;

	sc = device_get_softc(dev);

	/* If the clock is not enabled and valid, we can't help. */
	if (!(RD4(sc, SNVS_LPCR) & LPCR_SRTC_ENV)) {
		return (EINVAL);
	}

	/*
	 * The counter is clocked asynchronously to cpu accesses; read and
	 * assemble the pieces of the counter until we get the same value twice.
	 * The counter is 47 bits, organized as a 32.15 binary fixed-point
	 * value. If we shift it up to the high order part of a 64-bit word it
	 * turns into an sbintime.
	 */
	do {
		counter1  = (uint64_t)RD4(sc, SNVS_LPSRTCMR) << (SBT_LSB + 32);
		counter1 |= (uint64_t)RD4(sc, SNVS_LPSRTCLR) << (SBT_LSB);
		counter2  = (uint64_t)RD4(sc, SNVS_LPSRTCMR) << (SBT_LSB + 32);
		counter2 |= (uint64_t)RD4(sc, SNVS_LPSRTCLR) << (SBT_LSB);
	} while (counter1 != counter2);

	*ts = sbttots(counter1);

	clock_dbgprint_ts(sc->dev, CLOCK_DBG_READ, ts); 

	return (0);
}

static int
snvs_settime(device_t dev, struct timespec *ts)
{
	struct snvs_softc *sc;
	sbintime_t sbt;

	sc = device_get_softc(dev);

	/*
	 * The hardware format is the same as sbt (with fewer fractional bits),
	 * so first convert the time to sbt.  It takes two clock cycles for the
	 * counter to start after setting the enable bit, so add two SBT_LSBs to
	 * what we're about to set.
	 */
	sbt = tstosbt(*ts);
	sbt += 2 << SBT_LSB;
	snvs_rtc_enable(sc, false);
	WR4(sc, SNVS_LPSRTCMR, (uint32_t)(sbt >> (SBT_LSB + 32)));
	WR4(sc, SNVS_LPSRTCLR, (uint32_t)(sbt >> (SBT_LSB)));
	snvs_rtc_enable(sc, true);

	clock_dbgprint_ts(sc->dev, CLOCK_DBG_WRITE, ts); 

	return (0);
}

static int
snvs_probe(device_t dev)
{

	if (!ofw_bus_status_okay(dev))
		return (ENXIO);

	if (!ofw_bus_search_compatible(dev, compat_data)->ocd_data)
		return (ENXIO);

	device_set_desc(dev, "i.MX6 SNVS RTC");
	return (BUS_PROBE_DEFAULT);
}

static int
snvs_attach(device_t dev)
{
	struct snvs_softc *sc;
	int rid;

	sc = device_get_softc(dev);
	sc->dev = dev;

	rid = 0;
	sc->memres = bus_alloc_resource_any(sc->dev, SYS_RES_MEMORY, &rid,
	    RF_ACTIVE);
	if (sc->memres == NULL) {
		device_printf(sc->dev, "could not allocate registers\n");
		return (ENXIO);
	}

	clock_register(sc->dev, RTC_RESOLUTION_US);

	return (0);
}

static int
snvs_detach(device_t dev)
{
	struct snvs_softc *sc;

	sc = device_get_softc(dev);
	clock_unregister(sc->dev);
	bus_release_resource(sc->dev, SYS_RES_MEMORY, 0, sc->memres);
	return (0);
}

static device_method_t snvs_methods[] = {
	DEVMETHOD(device_probe,		snvs_probe),
	DEVMETHOD(device_attach,	snvs_attach),
	DEVMETHOD(device_detach,	snvs_detach),

	/* clock_if methods */
	DEVMETHOD(clock_gettime,	snvs_gettime),
	DEVMETHOD(clock_settime,	snvs_settime),

	DEVMETHOD_END
};

static driver_t snvs_driver = {
	"snvs",
	snvs_methods,
	sizeof(struct snvs_softc),
};

static devclass_t snvs_devclass;

DRIVER_MODULE(snvs, simplebus, snvs_driver, snvs_devclass, 0, 0);
SIMPLEBUS_PNP_INFO(compat_data);
