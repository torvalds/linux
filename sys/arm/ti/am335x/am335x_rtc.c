/*-
 * Copyright (c) 2015 Luiz Otavio O Souza <loos@FreeBSD.org>
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
#include <sys/lock.h>
#include <sys/module.h>
#include <sys/mutex.h>
#include <sys/rman.h>

#include <machine/bus.h>

#include <dev/ofw/ofw_bus_subr.h>
#include <arm/ti/ti_prcm.h>
#include <arm/ti/am335x/am335x_rtcvar.h>
#include <arm/ti/am335x/am335x_rtcreg.h>

#define	RTC_LOCK(_sc)		mtx_lock(&(_sc)->sc_mtx)
#define	RTC_UNLOCK(_sc)		mtx_unlock(&(_sc)->sc_mtx)
#define	RTC_LOCK_INIT(_sc)	mtx_init(&(_sc)->sc_mtx, \
    device_get_nameunit(_sc->sc_dev), "am335x_rtc", MTX_DEF)
#define	RTC_LOCK_DESTROY(_sc)	mtx_destroy(&(_sc)->sc_mtx)

#define	RTC_READ4(_sc, reg)		\
	bus_read_4((_sc)->sc_mem_res, reg)
#define	RTC_WRITE4(_sc, reg, value)	\
	bus_write_4((_sc)->sc_mem_res, reg, value)

#define	RTC_MAXIRQS		2

struct am335x_rtc_softc {
	device_t		sc_dev;
	struct mtx		sc_mtx;
	struct resource		*sc_irq_res[RTC_MAXIRQS];
	struct resource		*sc_mem_res;
};

static struct am335x_rtc_softc *rtc_sc = NULL;
static struct resource_spec am335x_rtc_irq_spec[] = {
	{ SYS_RES_IRQ, 0,  RF_ACTIVE },
	{ SYS_RES_IRQ, 1,  RF_ACTIVE },
	{ -1, 0,  0 }
};

static int
am335x_rtc_probe(device_t dev)
{

	if (!ofw_bus_status_okay(dev))
		return (ENXIO);
	if (!ofw_bus_is_compatible(dev, "ti,da830-rtc"))
		return (ENXIO);
	device_set_desc(dev, "AM335x RTC (power management mode)");

	return (BUS_PROBE_DEFAULT);
}

static int
am335x_rtc_attach(device_t dev)
{
	int rid;
	struct am335x_rtc_softc *sc;
	uint32_t rev;

	if (rtc_sc != NULL)
		return (ENXIO);
	rtc_sc = sc = device_get_softc(dev);
	sc->sc_dev = dev;
	rid = 0;
	sc->sc_mem_res = bus_alloc_resource_any(dev, SYS_RES_MEMORY, &rid,
	    RF_ACTIVE);
	if (!sc->sc_mem_res) {
		device_printf(dev, "cannot allocate memory resources\n");
		return (ENXIO);
	}
	if (bus_alloc_resources(dev, am335x_rtc_irq_spec, sc->sc_irq_res) != 0) {
		bus_release_resource(dev, SYS_RES_MEMORY, 0, sc->sc_mem_res);
		device_printf(dev, "cannot allocate irq resources\n");
		return (ENXIO);
	}
	RTC_LOCK_INIT(sc);

	/* Enable the RTC module. */
	ti_prcm_clk_enable(RTC_CLK);
	rev = RTC_READ4(sc, RTC_REVISION);
	device_printf(dev, "AM335X RTC v%d.%d.%d\n",
            (rev >> 8) & 0x7, (rev >> 6) & 0x3, rev & 0x3f);
	/* Unlock the RTC. */
	RTC_WRITE4(sc, RTC_KICK0R, RTC_KICK0R_PASS);
	RTC_WRITE4(sc, RTC_KICK1R, RTC_KICK1R_PASS);
	/* Stop the RTC, we don't need it right now. */
	RTC_WRITE4(sc, RTC_CTRL, 0);
	/* Disable interrupts. */
	RTC_WRITE4(sc, RTC_INTR, 0);
	/* Ack any pending interrupt. */
	RTC_WRITE4(sc, RTC_STATUS, RTC_STATUS_ALARM2 | RTC_STATUS_ALARM);
	/* Enable external clock (xtal) and 32 kHz clock. */
	RTC_WRITE4(sc, RTC_OSC, RTC_OSC_32KCLK_EN | RTC_OSC_32KCLK_SEL);
	/* Enable pmic_pwr_enable. */
	RTC_WRITE4(sc, RTC_PMIC, PMIC_PWR_ENABLE);

	return (0);
}

static int
am335x_rtc_detach(device_t dev)
{
	struct am335x_rtc_softc *sc;

	sc = device_get_softc(dev);
	if (sc->sc_irq_res[0] != NULL)
		bus_release_resources(dev, am335x_rtc_irq_spec, sc->sc_irq_res);
	if (sc->sc_mem_res)
		bus_release_resource(dev, SYS_RES_MEMORY, 0, sc->sc_mem_res);
	RTC_LOCK_DESTROY(sc);

	return (0);
}

void
am335x_rtc_pmic_pwr_toggle(void)
{
	int timeout;
	struct clocktime ct;
	struct timespec ts;

	/*
	 * We stop the RTC so we don't need to check the STATUS.BUSY bit
	 * before update ALARM2 registers.
	 */
	timeout = 10;
	RTC_WRITE4(rtc_sc, RTC_CTRL, 0);
	while (--timeout && RTC_READ4(rtc_sc, RTC_STATUS) & RTC_STATUS_RUN)
		DELAY(100);
	if (timeout == 0) {
		device_printf(rtc_sc->sc_dev, "RTC does not stop.\n");
		return;
	}
	/* Program the ALARM2 to fire in 2 seconds. */
	ct.dow = 0;
	ct.nsec = 0;
	ct.sec = FROMBCD(RTC_READ4(rtc_sc, RTC_SECONDS) & 0x7f);
	ct.min = FROMBCD(RTC_READ4(rtc_sc, RTC_MINUTES) & 0x7f);
	ct.hour = FROMBCD(RTC_READ4(rtc_sc, RTC_HOURS) & 0x3f);
	ct.day = FROMBCD(RTC_READ4(rtc_sc, RTC_DAYS) & 0x3f);
	ct.mon = FROMBCD(RTC_READ4(rtc_sc, RTC_MONTHS) & 0x1f);
	ct.year = FROMBCD(RTC_READ4(rtc_sc, RTC_YEARS) & 0xff);
	ct.year += POSIX_BASE_YEAR;
	clock_ct_to_ts(&ct, &ts);
	ts.tv_sec += 2;
	clock_ts_to_ct(&ts, &ct);
	RTC_WRITE4(rtc_sc, RTC_ALARM2_SECONDS, TOBCD(ct.sec));
	RTC_WRITE4(rtc_sc, RTC_ALARM2_MINUTES, TOBCD(ct.min));
	RTC_WRITE4(rtc_sc, RTC_ALARM2_HOURS, TOBCD(ct.hour));
	RTC_WRITE4(rtc_sc, RTC_ALARM2_DAYS, TOBCD(ct.day));
	RTC_WRITE4(rtc_sc, RTC_ALARM2_MONTHS, TOBCD(ct.mon));
	RTC_WRITE4(rtc_sc, RTC_ALARM2_YEARS, TOBCD(ct.year - POSIX_BASE_YEAR));
	/* Enable ALARM2 interrupt. */
	RTC_WRITE4(rtc_sc, RTC_INTR, RTC_INTR_ALARM2);
	/* Start count. */
	RTC_WRITE4(rtc_sc, RTC_CTRL, RTC_CTRL_RUN);
}

static device_method_t am335x_rtc_methods[] = {
	DEVMETHOD(device_probe,		am335x_rtc_probe),
	DEVMETHOD(device_attach,	am335x_rtc_attach),
	DEVMETHOD(device_detach,	am335x_rtc_detach),

	DEVMETHOD_END
};

static driver_t am335x_rtc_driver = {
	"am335x_rtc",
	am335x_rtc_methods,
	sizeof(struct am335x_rtc_softc),
};

static devclass_t am335x_rtc_devclass;

DRIVER_MODULE(am335x_rtc, simplebus, am335x_rtc_driver, am335x_rtc_devclass, 0, 0);
MODULE_VERSION(am335x_rtc, 1);
MODULE_DEPEND(am335x_rtc, simplebus, 1, 1, 1);
