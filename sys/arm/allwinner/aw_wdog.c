/*-
 * Copyright (c) 2013 Oleksandr Tymoshenko <gonzo@freebsd.org>
 * Copyright (c) 2016 Emmanuel Vadot <manu@freebsd.org>
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
#include <sys/watchdog.h>
#include <sys/reboot.h>
#include <sys/bus.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/module.h>
#include <sys/mutex.h>
#include <sys/rman.h>

#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>

#include <machine/bus.h>
#include <machine/machdep.h>

#include <arm/allwinner/aw_wdog.h>

#define	READ(_sc, _r) bus_read_4((_sc)->res, (_r))
#define	WRITE(_sc, _r, _v) bus_write_4((_sc)->res, (_r), (_v))

#define	A10_WDOG_CTRL		0x00
#define	A31_WDOG_CTRL		0x10
#define	 WDOG_CTRL_RESTART	(1 << 0)
#define	 A31_WDOG_CTRL_KEY	(0xa57 << 1)
#define	A10_WDOG_MODE		0x04
#define	A31_WDOG_MODE		0x18
#define	 A10_WDOG_MODE_INTVL_SHIFT	3
#define	 A31_WDOG_MODE_INTVL_SHIFT	4
#define	 A10_WDOG_MODE_RST_EN	(1 << 1)
#define	 WDOG_MODE_EN		(1 << 0)
#define	A31_WDOG_CONFIG		0x14
#define	 A31_WDOG_CONFIG_RST_EN_SYSTEM	(1 << 0)
#define	 A31_WDOG_CONFIG_RST_EN_INT	(2 << 0)

struct aw_wdog_interval {
	uint64_t	milliseconds;
	unsigned int	value;
};

struct aw_wdog_interval wd_intervals[] = {
	{   500,	 0 },
	{  1000,	 1 },
	{  2000,	 2 },
	{  3000,	 3 },
	{  4000,	 4 },
	{  5000,	 5 },
	{  6000,	 6 },
	{  8000,	 7 },
	{ 10000,	 8 },
	{ 12000,	 9 },
	{ 14000,	10 },
	{ 16000,	11 },
	{ 0,		 0 } /* sentinel */
};

static struct aw_wdog_softc *aw_wdog_sc = NULL;

struct aw_wdog_softc {
	device_t		dev;
	struct resource *	res;
	struct mtx		mtx;
	uint8_t			wdog_ctrl;
	uint32_t		wdog_ctrl_key;
	uint8_t			wdog_mode;
	uint8_t			wdog_mode_intvl_shift;
	uint8_t			wdog_mode_en;
	uint8_t			wdog_config;
	uint8_t			wdog_config_value;
};

#define	A10_WATCHDOG	1
#define	A31_WATCHDOG	2

static struct ofw_compat_data compat_data[] = {
	{"allwinner,sun4i-a10-wdt", A10_WATCHDOG},
	{"allwinner,sun6i-a31-wdt", A31_WATCHDOG},
	{NULL,             0}
};

static void aw_wdog_watchdog_fn(void *, u_int, int *);
static void aw_wdog_shutdown_fn(void *, int);

static int
aw_wdog_probe(device_t dev)
{

	if (!ofw_bus_status_okay(dev))
		return (ENXIO);
	switch (ofw_bus_search_compatible(dev, compat_data)->ocd_data) {
	case A10_WATCHDOG:
		device_set_desc(dev, "Allwinner A10 Watchdog");
		return (BUS_PROBE_DEFAULT);
	case A31_WATCHDOG:
		device_set_desc(dev, "Allwinner A31 Watchdog");
		return (BUS_PROBE_DEFAULT);
	}
	return (ENXIO);
}

static int
aw_wdog_attach(device_t dev)
{
	struct aw_wdog_softc *sc;
	int rid;

	if (aw_wdog_sc != NULL)
		return (ENXIO);

	sc = device_get_softc(dev);
	sc->dev = dev;

	rid = 0;
	sc->res = bus_alloc_resource_any(dev, SYS_RES_MEMORY, &rid, RF_ACTIVE);
	if (sc->res == NULL) {
		device_printf(dev, "could not allocate memory resource\n");
		return (ENXIO);
	}

	aw_wdog_sc = sc;

	switch (ofw_bus_search_compatible(dev, compat_data)->ocd_data) {
	case A10_WATCHDOG:
		sc->wdog_ctrl = A10_WDOG_CTRL;
		sc->wdog_mode = A10_WDOG_MODE;
		sc->wdog_mode_intvl_shift = A10_WDOG_MODE_INTVL_SHIFT;
		sc->wdog_mode_en = A10_WDOG_MODE_RST_EN | WDOG_MODE_EN;
		break;
	case A31_WATCHDOG:
		sc->wdog_ctrl = A31_WDOG_CTRL;
		sc->wdog_ctrl_key = A31_WDOG_CTRL_KEY;
		sc->wdog_mode = A31_WDOG_MODE;
		sc->wdog_mode_intvl_shift = A31_WDOG_MODE_INTVL_SHIFT;
		sc->wdog_mode_en = WDOG_MODE_EN;
		sc->wdog_config = A31_WDOG_CONFIG;
		sc->wdog_config_value = A31_WDOG_CONFIG_RST_EN_SYSTEM;
		break;
	default:
		bus_release_resource(dev, SYS_RES_MEMORY, rid, sc->res);
		return (ENXIO);
	}

	mtx_init(&sc->mtx, "AW Watchdog", "aw_wdog", MTX_DEF);
	EVENTHANDLER_REGISTER(watchdog_list, aw_wdog_watchdog_fn, sc, 0);
	EVENTHANDLER_REGISTER(shutdown_final, aw_wdog_shutdown_fn, sc,
	    SHUTDOWN_PRI_LAST - 1);
	
	return (0);
}

static void
aw_wdog_watchdog_fn(void *private, u_int cmd, int *error)
{
	struct aw_wdog_softc *sc;
	uint64_t ms;
	int i;

	sc = private;
	mtx_lock(&sc->mtx);

	cmd &= WD_INTERVAL;

	if (cmd > 0) {
		ms = ((uint64_t)1 << (cmd & WD_INTERVAL)) / 1000000;
		i = 0;
		while (wd_intervals[i].milliseconds &&
		    (ms > wd_intervals[i].milliseconds))
			i++;
		if (wd_intervals[i].milliseconds) {
			WRITE(sc, sc->wdog_mode,
			  (wd_intervals[i].value << sc->wdog_mode_intvl_shift) |
			    sc->wdog_mode_en);
			WRITE(sc, sc->wdog_ctrl,
			    WDOG_CTRL_RESTART | sc->wdog_ctrl_key);
			if (sc->wdog_config)
				WRITE(sc, sc->wdog_config,
				    sc->wdog_config_value);
			*error = 0;
		}
		else {
			/*
			 * Can't arm
			 * disable watchdog as watchdog(9) requires
			 */
			device_printf(sc->dev,
			    "Can't arm, timeout is more than 16 sec\n");
			mtx_unlock(&sc->mtx);
			WRITE(sc, sc->wdog_mode, 0);
			return;
		}
	}
	else
		WRITE(sc, sc->wdog_mode, 0);

	mtx_unlock(&sc->mtx);
}

static void
aw_wdog_shutdown_fn(void *private, int howto)
{
	if ((howto & (RB_POWEROFF|RB_HALT)) == 0)
		aw_wdog_watchdog_reset();
}

void
aw_wdog_watchdog_reset(void)
{

	if (aw_wdog_sc == NULL) {
		printf("Reset: watchdog device has not been initialized\n");
		return;
	}

	WRITE(aw_wdog_sc, aw_wdog_sc->wdog_mode,
	    (wd_intervals[0].value << aw_wdog_sc->wdog_mode_intvl_shift) |
	    aw_wdog_sc->wdog_mode_en);
	if (aw_wdog_sc->wdog_config)
		WRITE(aw_wdog_sc, aw_wdog_sc->wdog_config,
		      aw_wdog_sc->wdog_config_value);
	WRITE(aw_wdog_sc, aw_wdog_sc->wdog_ctrl,
	    WDOG_CTRL_RESTART | aw_wdog_sc->wdog_ctrl_key);
	while(1)
		;

}

static device_method_t aw_wdog_methods[] = {
	DEVMETHOD(device_probe, aw_wdog_probe),
	DEVMETHOD(device_attach, aw_wdog_attach),

	DEVMETHOD_END
};

static driver_t aw_wdog_driver = {
	"aw_wdog",
	aw_wdog_methods,
	sizeof(struct aw_wdog_softc),
};
static devclass_t aw_wdog_devclass;

DRIVER_MODULE(aw_wdog, simplebus, aw_wdog_driver, aw_wdog_devclass, 0, 0);
