/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2009 Marius Strobl <marius@FreeBSD.org>
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
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/module.h>
#include <sys/mutex.h>
#include <sys/resource.h>
#include <sys/rman.h>

#include <dev/led/led.h>
#include <dev/ofw/ofw_bus.h>

#include <machine/bus.h>
#include <machine/resource.h>

#define	EPIC_DELAY			10000

#define	EPIC_NREG			1
#define	EPIC_FW_LED			0

#define	EPIC_FW_LED_DATA		0x40
#define	EPIC_FW_LED_ADDR		0x41
#define	EPIC_FW_LED_WRITE_MASK		0x80

#define	EPIC_FW_VERSION			0x05
#define	EPIC_LED_STATE0			0x06

#define	EPIC_LED_ALERT_MASK		0x0c
#define	EPIC_LED_ALERT_OFF		0x00
#define	EPIC_LED_ALERT_ON		0x04

#define	EPIC_LED_POWER_MASK		0x30
#define	EPIC_LED_POWER_OFF		0x00
#define	EPIC_LED_POWER_ON		0x10
#define	EPIC_LED_POWER_SB_BLINK		0x20
#define	EPIC_LED_POWER_FAST_BLINK	0x30

static struct resource_spec epic_res_spec[] = {
	{ SYS_RES_MEMORY, EPIC_FW_LED, RF_ACTIVE },
	{ -1, 0 }
};

struct epic_softc {
	struct mtx		sc_mtx;
	struct resource		*sc_res[EPIC_NREG];
	struct cdev		*sc_led_dev_alert;
	struct cdev		*sc_led_dev_power;
};

#define	EPIC_FW_LED_READ(sc, off) ({					\
	uint8_t	__val;							\
	bus_write_1((sc)->sc_res[EPIC_FW_LED], EPIC_FW_LED_ADDR, (off));\
	bus_barrier((sc)->sc_res[EPIC_FW_LED], EPIC_FW_LED_ADDR, 1,	\
	    BUS_SPACE_BARRIER_READ | BUS_SPACE_BARRIER_WRITE);		\
	DELAY(EPIC_DELAY);						\
	__val = bus_read_1((sc)->sc_res[EPIC_FW_LED], EPIC_FW_LED_DATA);\
	bus_barrier((sc)->sc_res[EPIC_FW_LED], EPIC_FW_LED_DATA, 1,	\
	    BUS_SPACE_BARRIER_READ);					\
	DELAY(EPIC_DELAY);						\
	__val;								\
})

#define	EPIC_FW_LED_WRITE(sc, off, mask, val) do {			\
	bus_write_1((sc)->sc_res[EPIC_FW_LED], EPIC_FW_LED_ADDR, (off));\
	bus_barrier((sc)->sc_res[EPIC_FW_LED], EPIC_FW_LED_ADDR, 1,	\
	    BUS_SPACE_BARRIER_READ | BUS_SPACE_BARRIER_WRITE);		\
	DELAY(EPIC_DELAY);						\
	bus_write_1((sc)->sc_res[EPIC_FW_LED], EPIC_FW_LED_WRITE_MASK,	\
	    (mask));							\
	bus_barrier((sc)->sc_res[EPIC_FW_LED], EPIC_FW_LED_WRITE_MASK,	\
	    1, BUS_SPACE_BARRIER_READ | BUS_SPACE_BARRIER_WRITE);	\
	DELAY(EPIC_DELAY);						\
	bus_write_1((sc)->sc_res[EPIC_FW_LED], EPIC_FW_LED_DATA, (val));\
	bus_barrier((sc)->sc_res[EPIC_FW_LED], EPIC_FW_LED_DATA, 1,	\
	    BUS_SPACE_BARRIER_READ | BUS_SPACE_BARRIER_WRITE);		\
	DELAY(EPIC_DELAY);						\
} while (0)

#define	EPIC_LOCK_INIT(sc)						\
	mtx_init(&(sc)->sc_mtx, "epic mtx", NULL, MTX_DEF)
#define	EPIC_LOCK_DESTROY(sc)	mtx_destroy(&(sc)->sc_mtx)
#define	EPIC_LOCK(sc)		mtx_lock(&(sc)->sc_mtx)
#define	EPIC_UNLOCK(sc)		mtx_unlock(&(sc)->sc_mtx)

static device_probe_t epic_probe;
static device_attach_t epic_attach;
static device_detach_t epic_detach;

static void epic_led_alert(void *arg, int onoff);
static void epic_led_power(void *arg, int onoff);

static device_method_t epic_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		epic_probe),
	DEVMETHOD(device_attach,	epic_attach),
	DEVMETHOD(device_detach,	epic_detach),

	DEVMETHOD_END
};

static devclass_t epic_devclass;

DEFINE_CLASS_0(epic, epic_driver, epic_methods,
    sizeof(struct epic_softc));
DRIVER_MODULE(epic, ebus, epic_driver, epic_devclass, 0, 0);

static int
epic_probe(device_t dev)
{
	const char* compat;

	compat = ofw_bus_get_compat(dev);
	if (compat != NULL && strcmp(ofw_bus_get_name(dev),
	    "env-monitor") == 0 && strcmp(compat, "epic") == 0) {
		device_set_desc(dev, "Sun Fire V215/V245 LEDs");
		return (BUS_PROBE_DEFAULT);
	}
	return (ENXIO);
}

static int
epic_attach(device_t dev)
{
	struct epic_softc *sc;

	sc = device_get_softc(dev);
	if (bus_alloc_resources(dev, epic_res_spec, sc->sc_res)) {
		device_printf(dev, "failed to allocate resources\n");
		bus_release_resources(dev, epic_res_spec, sc->sc_res);
		return (ENXIO);
	}

	EPIC_LOCK_INIT(sc);

	if (bootverbose)
		device_printf(dev, "version 0x%x\n",
		    EPIC_FW_LED_READ(sc, EPIC_FW_VERSION));

	sc->sc_led_dev_alert = led_create(epic_led_alert, sc, "alert");
	sc->sc_led_dev_power = led_create(epic_led_power, sc, "power");

	return (0);
}

static int
epic_detach(device_t dev)
{
	struct epic_softc *sc;

	sc = device_get_softc(dev);

	led_destroy(sc->sc_led_dev_alert);
	led_destroy(sc->sc_led_dev_power);

	bus_release_resources(dev, epic_res_spec, sc->sc_res);

	EPIC_LOCK_DESTROY(sc);

	return (0);
}

static void
epic_led_alert(void *arg, int onoff)
{
	struct epic_softc *sc;

	sc = (struct epic_softc *)arg;

	EPIC_LOCK(sc);
	EPIC_FW_LED_WRITE(sc, EPIC_LED_STATE0, EPIC_LED_ALERT_MASK,
	    onoff ? EPIC_LED_ALERT_ON : EPIC_LED_ALERT_OFF);
	EPIC_UNLOCK(sc);
}

static void
epic_led_power(void *arg, int onoff)
{
	struct epic_softc *sc;

	sc = (struct epic_softc *)arg;

	EPIC_LOCK(sc);
	EPIC_FW_LED_WRITE(sc, EPIC_LED_STATE0, EPIC_LED_POWER_MASK,
	    onoff ? EPIC_LED_POWER_ON : EPIC_LED_POWER_OFF);
	EPIC_UNLOCK(sc);
}
