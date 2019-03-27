/*-
 * Copyright (c) 2015 Semihalf.
 * Copyright (c) 2015 Stormshield.
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
#include <sys/lock.h>
#include <sys/time.h>
#include <sys/proc.h>
#include <sys/conf.h>
#include <sys/rman.h>
#include <sys/clock.h>
#include <sys/systm.h>
#include <sys/mutex.h>
#include <sys/types.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/resource.h>

#include <machine/bus.h>
#include <machine/resource.h>

#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>

#include "clock_if.h"

#define	RTC_RES_US		1000000
#define	HALF_OF_SEC_NS		500000000

#define	RTC_STATUS		0x0
#define	RTC_TIME		0xC
#define	RTC_TEST_CONFIG		0x1C
#define	RTC_IRQ_1_CONFIG	0x4
#define	RTC_IRQ_2_CONFIG	0x8
#define	RTC_ALARM_1		0x10
#define	RTC_ALARM_2		0x14
#define	RTC_CLOCK_CORR		0x18

#define	RTC_NOMINAL_TIMING	0x2000
#define	RTC_NOMINAL_TIMING_MASK	0x7fff

#define	RTC_STATUS_ALARM1_MASK	0x1
#define	RTC_STATUS_ALARM2_MASK	0x2

#define	MV_RTC_LOCK(sc)		mtx_lock_spin(&(sc)->mutex)
#define	MV_RTC_UNLOCK(sc)	mtx_unlock_spin(&(sc)->mutex)

#define	RTC_BRIDGE_TIMING_CTRL		0x0
#define	RTC_WRCLK_PERIOD_SHIFT			0
#define	RTC_WRCLK_PERIOD_MASK			0x00000003FF
#define	RTC_WRCLK_PERIOD_MAX			0x3FF
#define	RTC_READ_OUTPUT_DELAY_SHIFT		26
#define	RTC_READ_OUTPUT_DELAY_MASK		0x007C000000
#define	RTC_READ_OUTPUT_DELAY_MAX		0x1F

#define	RTC_RES		0
#define	RTC_SOC_RES	1


static struct resource_spec res_spec[] = {
	{ SYS_RES_MEMORY,	0,	RF_ACTIVE },
	{ SYS_RES_MEMORY,	1,	RF_ACTIVE },
	{ -1, 0 }
};

struct mv_rtc_softc {
	device_t	dev;
	struct resource	*res[2];
	struct mtx	mutex;
};

static int mv_rtc_probe(device_t dev);
static int mv_rtc_attach(device_t dev);
static int mv_rtc_detach(device_t dev);

static int mv_rtc_gettime(device_t dev, struct timespec *ts);
static int mv_rtc_settime(device_t dev, struct timespec *ts);

static inline uint32_t mv_rtc_reg_read(struct mv_rtc_softc *sc,
    bus_size_t off);
static inline int mv_rtc_reg_write(struct mv_rtc_softc *sc, bus_size_t off,
    uint32_t val);
static inline void mv_rtc_configure_bus(struct mv_rtc_softc *sc);

static device_method_t mv_rtc_methods[] = {
	DEVMETHOD(device_probe,		mv_rtc_probe),
	DEVMETHOD(device_attach,	mv_rtc_attach),
	DEVMETHOD(device_detach,	mv_rtc_detach),

	DEVMETHOD(clock_gettime,	mv_rtc_gettime),
	DEVMETHOD(clock_settime,	mv_rtc_settime),

	{ 0, 0 },
};

static driver_t mv_rtc_driver = {
	"rtc",
	mv_rtc_methods,
	sizeof(struct mv_rtc_softc),
};

static struct ofw_compat_data mv_rtc_compat[] = {
	{"marvell,armada-380-rtc",	true},
	{"marvell,armada-8k-rtc",	true},
	{NULL,				false},
};

static devclass_t mv_rtc_devclass;

DRIVER_MODULE(a38x_rtc, simplebus, mv_rtc_driver, mv_rtc_devclass, 0, 0);

static void
mv_rtc_reset(device_t dev)
{
	struct mv_rtc_softc *sc;

	sc = device_get_softc(dev);

	/* Reset Test register */
	mv_rtc_reg_write(sc, RTC_TEST_CONFIG, 0);
	DELAY(500000);

	/* Reset Time register */
	mv_rtc_reg_write(sc, RTC_TIME, 0);
	DELAY(62);

	/* Reset Status register */
	mv_rtc_reg_write(sc, RTC_STATUS, (RTC_STATUS_ALARM1_MASK | RTC_STATUS_ALARM2_MASK));
	DELAY(62);

	/* Turn off Int1 and Int2 sources & clear the Alarm count */
	mv_rtc_reg_write(sc, RTC_IRQ_1_CONFIG, 0);
	mv_rtc_reg_write(sc, RTC_IRQ_2_CONFIG, 0);
	mv_rtc_reg_write(sc, RTC_ALARM_1, 0);
	mv_rtc_reg_write(sc, RTC_ALARM_2, 0);

	/* Setup nominal register access timing */
	mv_rtc_reg_write(sc, RTC_CLOCK_CORR, RTC_NOMINAL_TIMING);

	/* Reset Time register */
	mv_rtc_reg_write(sc, RTC_TIME, 0);
	DELAY(10);

	/* Reset Status register */
	mv_rtc_reg_write(sc, RTC_STATUS, (RTC_STATUS_ALARM1_MASK | RTC_STATUS_ALARM2_MASK));
	DELAY(50);
}

static int
mv_rtc_probe(device_t dev)
{

	if (!ofw_bus_status_okay(dev))
		return (ENXIO);

	if (!ofw_bus_search_compatible(dev, mv_rtc_compat)->ocd_data)
		return (ENXIO);

	device_set_desc(dev, "Marvell Integrated RTC");

	return (BUS_PROBE_DEFAULT);
}

static int
mv_rtc_attach(device_t dev)
{
	struct mv_rtc_softc *sc;
	int unit, ret;

	unit = device_get_unit(dev);

	sc = device_get_softc(dev);
	sc->dev = dev;

	clock_register(dev, RTC_RES_US);

	mtx_init(&sc->mutex, device_get_nameunit(dev), NULL, MTX_SPIN);

	ret = bus_alloc_resources(dev, res_spec, sc->res);

	if (ret != 0) {
		device_printf(dev, "could not allocate resources\n");
		mtx_destroy(&sc->mutex);
		return (ENXIO);
	}
	mv_rtc_configure_bus(sc);

	return (0);
}

static int
mv_rtc_detach(device_t dev)
{
	struct mv_rtc_softc *sc;

	sc = device_get_softc(dev);

	mtx_destroy(&sc->mutex);

	bus_release_resources(dev, res_spec, sc->res);

	return (0);
}

static int
mv_rtc_gettime(device_t dev, struct timespec *ts)
{
	struct mv_rtc_softc *sc;
	uint32_t val, val_check;

	sc = device_get_softc(dev);

	MV_RTC_LOCK(sc);
	/*
	 * According to HW Errata if more than one second between
	 * two time reads is detected, then read once again
	 */
	val = mv_rtc_reg_read(sc, RTC_TIME);
	val_check = mv_rtc_reg_read(sc, RTC_TIME);
	if (val_check - val > 1)
		val_check = mv_rtc_reg_read(sc, RTC_TIME);

	MV_RTC_UNLOCK(sc);

	ts->tv_sec = val_check;
	/* RTC resolution is 1 sec */
	ts->tv_nsec = 0;

	return (0);
}

static int
mv_rtc_settime(device_t dev, struct timespec *ts)
{
	struct mv_rtc_softc *sc;

	sc = device_get_softc(dev);

	/* RTC resolution is 1 sec */
	if (ts->tv_nsec >= HALF_OF_SEC_NS)
		ts->tv_sec++;
	ts->tv_nsec = 0;

	MV_RTC_LOCK(sc);

	if ((mv_rtc_reg_read(sc, RTC_CLOCK_CORR) & RTC_NOMINAL_TIMING_MASK) !=
	    RTC_NOMINAL_TIMING) {
		/* RTC was not resetted yet */
		mv_rtc_reset(dev);
	}

	/*
	 * According to errata FE-3124064, Write to RTC TIME register
	 * may fail. As a workaround, before writing to RTC TIME register,
	 * issue a dummy write of 0x0 twice to RTC Status register.
	 */
	mv_rtc_reg_write(sc, RTC_STATUS, 0x0);
	mv_rtc_reg_write(sc, RTC_STATUS, 0x0);
	mv_rtc_reg_write(sc, RTC_TIME, ts->tv_sec);

	MV_RTC_UNLOCK(sc);

	return (0);
}

static inline uint32_t
mv_rtc_reg_read(struct mv_rtc_softc *sc, bus_size_t off)
{

	return (bus_read_4(sc->res[RTC_RES], off));
}

/*
 * According to the datasheet, the OS should wait 5us after every
 * register write to the RTC hard macro so that the required update
 * can occur without holding off the system bus
 */
static inline int
mv_rtc_reg_write(struct mv_rtc_softc *sc, bus_size_t off, uint32_t val)
{

	bus_write_4(sc->res[RTC_RES], off, val);
	DELAY(5);

	return (0);
}

static inline void
mv_rtc_configure_bus(struct mv_rtc_softc *sc)
{
	int val;

	val = bus_read_4(sc->res[RTC_SOC_RES], RTC_BRIDGE_TIMING_CTRL);
	val &= ~(RTC_WRCLK_PERIOD_MASK | RTC_READ_OUTPUT_DELAY_MASK);
	val |= RTC_WRCLK_PERIOD_MAX << RTC_WRCLK_PERIOD_SHIFT;
	val |= RTC_READ_OUTPUT_DELAY_MAX << RTC_READ_OUTPUT_DELAY_SHIFT;
	bus_write_4(sc->res[RTC_SOC_RES], RTC_BRIDGE_TIMING_CTRL, val);
}
