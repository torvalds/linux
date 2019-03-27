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

/*
 * RTC driver for Tegra SoCs.
 */
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/clock.h>
#include <sys/kernel.h>
#include <sys/limits.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/module.h>
#include <sys/resource.h>

#include <machine/bus.h>
#include <machine/resource.h>
#include <sys/rman.h>

#include <dev/extres/clk/clk.h>
#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>

#include "clock_if.h"

#define	RTC_CONTROL				0x00
#define	RTC_BUSY				0x04
#define	 RTC_BUSY_STATUS				(1 << 0)
#define	RTC_SECONDS				0x08
#define	RTC_SHADOW_SECONDS			0x0c
#define	RTC_MILLI_SECONDS			0x10
#define	RTC_SECONDS_ALARM0			0x14
#define	RTC_SECONDS_ALARM1			0x18
#define	RTC_MILLI_SECONDS_ALARM			0x1c
#define	RTC_SECONDS_COUNTDOWN_ALARM		0x20
#define	RTC_MILLI_SECONDS_COUNTDOW_ALARM	0x24
#define	RTC_INTR_MASK				0x28
#define	 RTC_INTR_MSEC_CDN_ALARM			(1 << 4)
#define	 RTC_INTR_SEC_CDN_ALARM				(1 << 3)
#define	 RTC_INTR_MSEC_ALARM				(1 << 2)
#define	 RTC_INTR_SEC_ALARM1				(1 << 1)
#define	 RTC_INTR_SEC_ALARM0				(1 << 0)

#define	RTC_INTR_STATUS				0x2c
#define	RTC_INTR_SOURCE				0x30
#define	RTC_INTR_SET				0x34
#define	RTC_CORRECTION_FACTOR			0x38

#define	WR4(_sc, _r, _v)	bus_write_4((_sc)->mem_res, (_r), (_v))
#define	RD4(_sc, _r)		bus_read_4((_sc)->mem_res, (_r))

#define	LOCK(_sc)		mtx_lock(&(_sc)->mtx)
#define	UNLOCK(_sc)		mtx_unlock(&(_sc)->mtx)
#define	SLEEP(_sc, timeout)						\
	mtx_sleep(sc, &sc->mtx, 0, "rtcwait", timeout);
#define	LOCK_INIT(_sc)							\
	mtx_init(&_sc->mtx, device_get_nameunit(_sc->dev), "tegra_rtc", MTX_DEF)
#define	LOCK_DESTROY(_sc)	mtx_destroy(&_sc->mtx)
#define	ASSERT_LOCKED(_sc)	mtx_assert(&_sc->mtx, MA_OWNED)
#define	ASSERT_UNLOCKED(_sc)	mtx_assert(&_sc->mtx, MA_NOTOWNED)

static struct ofw_compat_data compat_data[] = {
	{"nvidia,tegra124-rtc",	1},
	{NULL,			0}
};

struct tegra_rtc_softc {
	device_t		dev;
	struct mtx		mtx;

	struct resource		*mem_res;
	struct resource		*irq_res;
	void			*irq_h;

	clk_t			clk;
	uint32_t		core_freq;
};

static void
tegra_rtc_wait(struct tegra_rtc_softc *sc)
{
	int timeout;

	for (timeout = 500; timeout >0; timeout--) {
		if ((RD4(sc, RTC_BUSY) & RTC_BUSY_STATUS) == 0)
			break;
		DELAY(1);
	}
	if (timeout <= 0)
		device_printf(sc->dev, "Device busy timeouted\n");

}

/*
 * Get the time of day clock and return it in ts.
 * Return 0 on success, an error number otherwise.
 */
static int
tegra_rtc_gettime(device_t dev, struct timespec *ts)
{
	struct tegra_rtc_softc *sc;
	struct timeval tv;
	uint32_t msec, sec;

	sc = device_get_softc(dev);

	LOCK(sc);
	msec = RD4(sc, RTC_MILLI_SECONDS);
	sec = RD4(sc, RTC_SHADOW_SECONDS);
	UNLOCK(sc);
	tv.tv_sec = sec;
	tv.tv_usec = msec * 1000;
	TIMEVAL_TO_TIMESPEC(&tv, ts);
	return (0);
}


static int
tegra_rtc_settime(device_t dev, struct timespec *ts)
{
	struct tegra_rtc_softc *sc;
	struct timeval tv;

	sc = device_get_softc(dev);

	LOCK(sc);
	TIMESPEC_TO_TIMEVAL(&tv, ts);
	tegra_rtc_wait(sc);
	WR4(sc, RTC_SECONDS, tv.tv_sec);
	UNLOCK(sc);

	return (0);
}


static void
tegra_rtc_intr(void *arg)
{
	struct tegra_rtc_softc *sc;
	uint32_t status;

	sc = (struct tegra_rtc_softc *)arg;
	LOCK(sc);
	status = RD4(sc, RTC_INTR_STATUS);
	WR4(sc, RTC_INTR_STATUS, status);
	UNLOCK(sc);
}

static int
tegra_rtc_probe(device_t dev)
{
	if (!ofw_bus_status_okay(dev))
		return (ENXIO);

	if (ofw_bus_search_compatible(dev, compat_data)->ocd_data == 0)
		return (ENXIO);

	return (BUS_PROBE_DEFAULT);
}

static int
tegra_rtc_attach(device_t dev)
{
	int rv, rid;
	struct tegra_rtc_softc *sc;

	sc = device_get_softc(dev);
	sc->dev = dev;

	LOCK_INIT(sc);

	/* Get the memory resource for the register mapping. */
	rid = 0;
	sc->mem_res = bus_alloc_resource_any(dev, SYS_RES_MEMORY, &rid,
	    RF_ACTIVE);
	if (sc->mem_res == NULL) {
		device_printf(dev, "Cannot map registers.\n");
		rv = ENXIO;
		goto fail;
	}

	/* Allocate our IRQ resource. */
	rid = 0;
	sc->irq_res = bus_alloc_resource_any(dev, SYS_RES_IRQ, &rid,
	    RF_ACTIVE);
	if (sc->irq_res == NULL) {
		device_printf(dev, "Cannot allocate interrupt.\n");
		rv = ENXIO;
		goto fail;
	}

	/* OFW resources. */
	rv = clk_get_by_ofw_index(dev, 0, 0, &sc->clk);
	if (rv != 0) {
		device_printf(dev, "Cannot get i2c clock: %d\n", rv);
		goto fail;
	}
	rv = clk_enable(sc->clk);
	if (rv != 0) {
		device_printf(dev, "Cannot enable clock: %d\n", rv);
		goto fail;
	}

	/* Init hardware. */
	WR4(sc, RTC_SECONDS_ALARM0, 0);
	WR4(sc, RTC_SECONDS_ALARM1, 0);
	WR4(sc, RTC_INTR_STATUS, 0xFFFFFFFF);
	WR4(sc, RTC_INTR_MASK, 0);

	/* Setup  interrupt */
	rv = bus_setup_intr(dev, sc->irq_res, INTR_TYPE_MISC | INTR_MPSAFE,
	    NULL, tegra_rtc_intr, sc, &sc->irq_h);
	if (rv) {
		device_printf(dev, "Cannot setup interrupt.\n");
		goto fail;
	}

	/*
	 * Register as a time of day clock with 1-second resolution.
	 *
	 * XXXX Not yet, we don't have support for multiple RTCs
	 */
	/* clock_register(dev, 1000000); */

	return (bus_generic_attach(dev));

fail:
	if (sc->clk != NULL)
		clk_release(sc->clk);
	if (sc->irq_h != NULL)
		bus_teardown_intr(dev, sc->irq_res, sc->irq_h);
	if (sc->irq_res != NULL)
		bus_release_resource(dev, SYS_RES_IRQ, 0, sc->irq_res);
	if (sc->mem_res != NULL)
		bus_release_resource(dev, SYS_RES_MEMORY, 0, sc->mem_res);
	LOCK_DESTROY(sc);

	return (rv);
}

static int
tegra_rtc_detach(device_t dev)
{
	struct tegra_rtc_softc *sc;

	sc = device_get_softc(dev);
	if (sc->irq_h != NULL)
		bus_teardown_intr(dev, sc->irq_res, sc->irq_h);
	if (sc->irq_res != NULL)
		bus_release_resource(dev, SYS_RES_IRQ, 0, sc->irq_res);
	if (sc->mem_res != NULL)
		bus_release_resource(dev, SYS_RES_MEMORY, 0, sc->mem_res);

	LOCK_DESTROY(sc);
	return (bus_generic_detach(dev));
}

static device_method_t tegra_rtc_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		tegra_rtc_probe),
	DEVMETHOD(device_attach,	tegra_rtc_attach),
	DEVMETHOD(device_detach,	tegra_rtc_detach),

	/* clock interface */
	DEVMETHOD(clock_gettime,	tegra_rtc_gettime),
	DEVMETHOD(clock_settime,	tegra_rtc_settime),

	DEVMETHOD_END
};

static devclass_t tegra_rtc_devclass;
static DEFINE_CLASS_0(rtc, tegra_rtc_driver, tegra_rtc_methods,
    sizeof(struct tegra_rtc_softc));
DRIVER_MODULE(tegra_rtc, simplebus, tegra_rtc_driver, tegra_rtc_devclass,
    NULL, NULL);
