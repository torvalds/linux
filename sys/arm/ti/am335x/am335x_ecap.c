/*-
 * Copyright (c) 2013 Oleksandr Tymoshenko <gonzo@freebsd.org>
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
#include <sys/limits.h>
#include <sys/lock.h>
#include <sys/module.h>
#include <sys/mutex.h>
#include <sys/resource.h>
#include <sys/rman.h>
#include <sys/sysctl.h>

#include <machine/bus.h>

#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>

#include "am335x_pwm.h"

#define	ECAP_TSCTR		0x00
#define	ECAP_CAP1		0x08
#define	ECAP_CAP2		0x0C
#define	ECAP_CAP3		0x10
#define	ECAP_CAP4		0x14
#define	ECAP_ECCTL2		0x2A
#define		ECCTL2_MODE_APWM		(1 << 9)
#define		ECCTL2_SYNCO_SEL		(3 << 6)
#define		ECCTL2_TSCTRSTOP_FREERUN	(1 << 4)

#define	ECAP_READ2(_sc, reg)	bus_read_2((_sc)->sc_mem_res, reg);
#define	ECAP_WRITE2(_sc, reg, value)	\
    bus_write_2((_sc)->sc_mem_res, reg, value);
#define	ECAP_READ4(_sc, reg)	bus_read_4((_sc)->sc_mem_res, reg);
#define	ECAP_WRITE4(_sc, reg, value)	\
    bus_write_4((_sc)->sc_mem_res, reg, value);

#define	PWM_LOCK(_sc)		mtx_lock(&(_sc)->sc_mtx)
#define	PWM_UNLOCK(_sc)		mtx_unlock(&(_sc)->sc_mtx)
#define	PWM_LOCK_INIT(_sc)	mtx_init(&(_sc)->sc_mtx, \
    device_get_nameunit(_sc->sc_dev), "am335x_ecap softc", MTX_DEF)
#define	PWM_LOCK_DESTROY(_sc)	mtx_destroy(&(_sc)->sc_mtx)

static device_probe_t am335x_ecap_probe;
static device_attach_t am335x_ecap_attach;
static device_detach_t am335x_ecap_detach;

struct am335x_ecap_softc {
	device_t		sc_dev;
	struct mtx		sc_mtx;
	struct resource		*sc_mem_res;
	int			sc_mem_rid;
};

static device_method_t am335x_ecap_methods[] = {
	DEVMETHOD(device_probe,		am335x_ecap_probe),
	DEVMETHOD(device_attach,	am335x_ecap_attach),
	DEVMETHOD(device_detach,	am335x_ecap_detach),

	DEVMETHOD_END
};

static driver_t am335x_ecap_driver = {
	"am335x_ecap",
	am335x_ecap_methods,
	sizeof(struct am335x_ecap_softc),
};

static devclass_t am335x_ecap_devclass;

/*
 * API function to set period/duty cycles for ECAPx
 */
int
am335x_pwm_config_ecap(int unit, int period, int duty)
{
	device_t dev;
	struct am335x_ecap_softc *sc;
	uint16_t reg;

	dev = devclass_get_device(am335x_ecap_devclass, unit);
	if (dev == NULL)
		return (ENXIO);

	if (duty > period)
		return (EINVAL);

	if (period == 0)
		return (EINVAL);

	sc = device_get_softc(dev);
	PWM_LOCK(sc);

	reg = ECAP_READ2(sc, ECAP_ECCTL2);
	reg |= ECCTL2_MODE_APWM | ECCTL2_TSCTRSTOP_FREERUN | ECCTL2_SYNCO_SEL;
	ECAP_WRITE2(sc, ECAP_ECCTL2, reg);

	/* CAP3 in APWM mode is APRD shadow register */
	ECAP_WRITE4(sc, ECAP_CAP3, period - 1);

	/* CAP4 in APWM mode is ACMP shadow register */
	ECAP_WRITE4(sc, ECAP_CAP4, duty);
	/* Restart counter */
	ECAP_WRITE4(sc, ECAP_TSCTR, 0);

	PWM_UNLOCK(sc);

	return (0);
}

static int
am335x_ecap_probe(device_t dev)
{

	if (!ofw_bus_status_okay(dev))
		return (ENXIO);

	if (!ofw_bus_is_compatible(dev, "ti,am33xx-ecap"))
		return (ENXIO);

	device_set_desc(dev, "AM335x eCAP");

	return (BUS_PROBE_DEFAULT);
}

static int
am335x_ecap_attach(device_t dev)
{
	struct am335x_ecap_softc *sc;

	sc = device_get_softc(dev);
	sc->sc_dev = dev;

	PWM_LOCK_INIT(sc);

	sc->sc_mem_res = bus_alloc_resource_any(dev, SYS_RES_MEMORY,
	    &sc->sc_mem_rid, RF_ACTIVE);
	if (sc->sc_mem_res == NULL) {
		device_printf(dev, "cannot allocate memory resources\n");
		goto fail;
	}

	return (0);

fail:
	PWM_LOCK_DESTROY(sc);
	return (ENXIO);
}

static int
am335x_ecap_detach(device_t dev)
{
	struct am335x_ecap_softc *sc;

	sc = device_get_softc(dev);

	PWM_LOCK(sc);
	if (sc->sc_mem_res)
		bus_release_resource(dev, SYS_RES_MEMORY,
		    sc->sc_mem_rid, sc->sc_mem_res);
	PWM_UNLOCK(sc);

	PWM_LOCK_DESTROY(sc);


	return (0);
}

DRIVER_MODULE(am335x_ecap, am335x_pwmss, am335x_ecap_driver, am335x_ecap_devclass, 0, 0);
MODULE_VERSION(am335x_ecap, 1);
MODULE_DEPEND(am335x_ecap, am335x_pwmss, 1, 1, 1);
