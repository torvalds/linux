/*-
 * Copyright 2016 Alexander Kabaev <kan@FreeBSD.org>
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

/*
 * Ingenic JZ4780 RTC driver
 *
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/conf.h>
#include <sys/clock.h>
#include <sys/bus.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/resource.h>

#include <machine/bus.h>

#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>

#include "clock_if.h"

#define	JZ_RTC_TIMEOUT	5000

#define JZ_RTCCR	0x00
# define JZ_RTCCR_WRDY	(1u << 7)
#define JZ_RTSR		0x04
#define JZ_HSPR		0x34
#define JZ_WENR		0x3C
# define JZ_WENR_PAT	0xa55a
# define JZ_WENR_WEN	(1u <<31)

struct jz4780_rtc_softc {
	device_t		dev;
	struct resource		*res[2];
};

static struct resource_spec jz4780_rtc_spec[] = {
	{ SYS_RES_MEMORY, 0, RF_ACTIVE },
	{ SYS_RES_IRQ,    0, RF_ACTIVE },
	{ -1, 0 }
};

#define	CSR_READ(sc, reg)	bus_read_4((sc)->res[0], (reg))
#define	CSR_WRITE(sc, reg, val)	bus_write_4((sc)->res[0], (reg), (val))

static int jz4780_rtc_probe(device_t dev);
static int jz4780_rtc_attach(device_t dev);
static int jz4780_rtc_detach(device_t dev);

static int
jz4780_rtc_probe(device_t dev)
{

	if (!ofw_bus_status_okay(dev))
		return (ENXIO);

	if (!ofw_bus_is_compatible(dev, "ingenic,jz4780-rtc"))
		return (ENXIO);

	device_set_desc(dev, "JZ4780 RTC");

	return (BUS_PROBE_DEFAULT);
}

/* Poll control register until RTC is ready to accept register writes */
static int
jz4780_rtc_wait(struct jz4780_rtc_softc *sc)
{
	int timeout;

	timeout = JZ_RTC_TIMEOUT;
	while (timeout-- > 0) {
		if (CSR_READ(sc, JZ_RTCCR) & JZ_RTCCR_WRDY)
			return (0);
	}
	return (EIO);
}

/*
 * Write RTC register. It appears that RTC goes into read-only mode at random,
 * which suggests something is up with how it is powered up, so do the pattern
 * writing dance every time just in case.
 */
static int
jz4780_rtc_write(struct jz4780_rtc_softc *sc, uint32_t reg, uint32_t val)
{
	int ret, timeout;

	ret = jz4780_rtc_wait(sc);
	if (ret != 0)
		return (ret);

	CSR_WRITE(sc, JZ_WENR, JZ_WENR_PAT);

	ret = jz4780_rtc_wait(sc);
	if (ret)
		return ret;

	timeout = JZ_RTC_TIMEOUT;
	while (timeout-- > 0) {
		if (CSR_READ(sc, JZ_WENR) & JZ_WENR_WEN)
			break;
	}
	if (timeout < 0)
		return (EIO);

	CSR_WRITE(sc, reg, val);
	return 0;
}

static int
jz4780_rtc_attach(device_t dev)
{
	struct jz4780_rtc_softc *sc = device_get_softc(dev);
	uint32_t scratch;
	int ret;

	sc->dev = dev;

	if (bus_alloc_resources(dev, jz4780_rtc_spec, sc->res)) {
		device_printf(dev, "could not allocate resources for device\n");
		return (ENXIO);
	}

	scratch = CSR_READ(sc, JZ_HSPR);
	if (scratch != 0x12345678) {
		ret = jz4780_rtc_write(sc, JZ_HSPR, 0x12345678);
		if (ret == 0)
			ret = jz4780_rtc_write(sc, JZ_RTSR, 0);
		if (ret) {
			device_printf(dev, "Unable to write RTC registers\n");
			jz4780_rtc_detach(dev);
			return (ret);
		}
	}
	clock_register(dev, 1000000); /* Register 1 HZ clock */
	return (0);
}

static int
jz4780_rtc_detach(device_t dev)
{
	struct jz4780_rtc_softc *sc;

	sc = device_get_softc(dev);
	bus_release_resources(dev, jz4780_rtc_spec, sc->res);
	return (0);
}

static int
jz4780_rtc_gettime(device_t dev, struct timespec *ts)
{
	struct jz4780_rtc_softc *sc;
	uint32_t val1, val2;
	int timeout;

	sc = device_get_softc(dev);

	timeout = JZ_RTC_TIMEOUT;
	val2 = CSR_READ(sc, JZ_RTSR);
	do {
		val1 = val2;
		val2 = CSR_READ(sc, JZ_RTSR);
	} while (val1 != val2 && timeout-- >= 0);

	if (timeout < 0)
		return (EIO);

	/* Convert secs to timespec directly */
	ts->tv_sec = val1;
	ts->tv_nsec = 0;
	return 0;
}

static int
jz4780_rtc_settime(device_t dev, struct timespec *ts)
{
	struct jz4780_rtc_softc *sc;

	sc = device_get_softc(dev);
	return jz4780_rtc_write(sc, JZ_RTSR, ts->tv_sec);
}

static device_method_t jz4780_rtc_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		jz4780_rtc_probe),
	DEVMETHOD(device_attach,	jz4780_rtc_attach),
	DEVMETHOD(device_detach,	jz4780_rtc_detach),

	DEVMETHOD(clock_gettime,        jz4780_rtc_gettime),
	DEVMETHOD(clock_settime,        jz4780_rtc_settime),

	DEVMETHOD_END
};

static driver_t jz4780_rtc_driver = {
	"jz4780_rtc",
	jz4780_rtc_methods,
	sizeof(struct jz4780_rtc_softc),
};

static devclass_t jz4780_rtc_devclass;

EARLY_DRIVER_MODULE(jz4780_rtc, simplebus, jz4780_rtc_driver,
    jz4780_rtc_devclass, 0, 0, BUS_PASS_TIMER + BUS_PASS_ORDER_MIDDLE);
