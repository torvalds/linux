/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2012 Alexander Rybalko <ray@freebsd.org>
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
#include <sys/bus.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/rman.h>

#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>

#include <machine/bus.h>
#include <machine/machdep.h>

#include <arm/broadcom/bcm2835/bcm2835_wdog.h>

#define	BCM2835_PASSWORD	0x5a

#define BCM2835_WDOG_RESET	0
#define BCM2835_PASSWORD_MASK	0xff000000
#define BCM2835_PASSWORD_SHIFT	24
#define BCM2835_WDOG_TIME_MASK	0x000fffff
#define BCM2835_WDOG_TIME_SHIFT	0

#define	READ(_sc, _r) bus_space_read_4((_sc)->bst, (_sc)->bsh, (_r) + (_sc)->regs_offset)
#define	WRITE(_sc, _r, _v) bus_space_write_4((_sc)->bst, (_sc)->bsh, (_r) + (_sc)->regs_offset, (_v))

#define BCM2835_RSTC_WRCFG_CLR		0xffffffcf
#define BCM2835_RSTC_WRCFG_SET		0x00000030
#define BCM2835_RSTC_WRCFG_FULL_RESET	0x00000020
#define BCM2835_RSTC_RESET		0x00000102

#define	BCM2835_RSTC_REG	0x00
#define	BCM2835_RSTS_REG	0x04
#define	BCM2835_WDOG_REG	0x08

static struct bcmwd_softc *bcmwd_lsc = NULL;

struct bcmwd_softc {
	device_t		dev;
	struct resource *	res;
	bus_space_tag_t		bst;
	bus_space_handle_t	bsh;
	int			wdog_armed;
	int			wdog_period;
	char			wdog_passwd;
	struct mtx		mtx;
	int			regs_offset;
};

#define	BSD_DTB		1
#define	UPSTREAM_DTB	2
#define	UPSTREAM_DTB_REGS_OFFSET	0x1c

static struct ofw_compat_data compat_data[] = {
	{"broadcom,bcm2835-wdt",	BSD_DTB},
	{"brcm,bcm2835-pm-wdt",		UPSTREAM_DTB},
	{NULL,				0}
};

static void bcmwd_watchdog_fn(void *private, u_int cmd, int *error);

static int
bcmwd_probe(device_t dev)
{

	if (!ofw_bus_status_okay(dev))
		return (ENXIO);

	if (ofw_bus_search_compatible(dev, compat_data)->ocd_data == 0)
		return (ENXIO);

	device_set_desc(dev, "BCM2708/2835 Watchdog");

	return (BUS_PROBE_DEFAULT);
}

static int
bcmwd_attach(device_t dev)
{
	struct bcmwd_softc *sc;
	int rid;

	if (bcmwd_lsc != NULL)
		return (ENXIO);

	sc = device_get_softc(dev);
	sc->wdog_period = 7;
	sc->wdog_passwd = BCM2835_PASSWORD;
	sc->wdog_armed = 0;
	sc->dev = dev;

	rid = 0;
	sc->res = bus_alloc_resource_any(dev, SYS_RES_MEMORY, &rid, RF_ACTIVE);
	if (sc->res == NULL) {
		device_printf(dev, "could not allocate memory resource\n");
		return (ENXIO);
	}

	sc->bst = rman_get_bustag(sc->res);
	sc->bsh = rman_get_bushandle(sc->res);

	/* compensate base address difference */
	if (ofw_bus_search_compatible(dev, compat_data)->ocd_data
	   == UPSTREAM_DTB)
		sc->regs_offset = UPSTREAM_DTB_REGS_OFFSET;

	bcmwd_lsc = sc;
	mtx_init(&sc->mtx, "BCM2835 Watchdog", "bcmwd", MTX_DEF);
	EVENTHANDLER_REGISTER(watchdog_list, bcmwd_watchdog_fn, sc, 0);

	return (0);
}

static void
bcmwd_watchdog_fn(void *private, u_int cmd, int *error)
{
	struct bcmwd_softc *sc;
	uint64_t sec;
	uint32_t ticks, reg;

	sc = private;
	mtx_lock(&sc->mtx);

	cmd &= WD_INTERVAL;

	if (cmd > 0) {
		sec = ((uint64_t)1 << (cmd & WD_INTERVAL)) / 1000000000;
		if (sec == 0 || sec > 15) {
			/* 
			 * Can't arm
			 * disable watchdog as watchdog(9) requires
			 */
			device_printf(sc->dev,
			    "Can't arm, timeout must be between 1-15 seconds\n");
			WRITE(sc, BCM2835_RSTC_REG, 
			    (BCM2835_PASSWORD << BCM2835_PASSWORD_SHIFT) |
			    BCM2835_RSTC_RESET);
			mtx_unlock(&sc->mtx);
			return;
		}

		ticks = (sec << 16) & BCM2835_WDOG_TIME_MASK;
		reg = (BCM2835_PASSWORD << BCM2835_PASSWORD_SHIFT) | ticks;
		WRITE(sc, BCM2835_WDOG_REG, reg);

		reg = READ(sc, BCM2835_RSTC_REG);
		reg &= BCM2835_RSTC_WRCFG_CLR;
		reg |= BCM2835_RSTC_WRCFG_FULL_RESET;
		reg |= (BCM2835_PASSWORD << BCM2835_PASSWORD_SHIFT);
		WRITE(sc, BCM2835_RSTC_REG, reg);

		*error = 0;
	}
	else
		WRITE(sc, BCM2835_RSTC_REG, 
		    (BCM2835_PASSWORD << BCM2835_PASSWORD_SHIFT) |
		    BCM2835_RSTC_RESET);

	mtx_unlock(&sc->mtx);
}

void
bcmwd_watchdog_reset(void)
{

	if (bcmwd_lsc == NULL)
		return;

	WRITE(bcmwd_lsc, BCM2835_WDOG_REG,
	    (BCM2835_PASSWORD << BCM2835_PASSWORD_SHIFT) | 10);

	WRITE(bcmwd_lsc, BCM2835_RSTC_REG,
	    (READ(bcmwd_lsc, BCM2835_RSTC_REG) & BCM2835_RSTC_WRCFG_CLR) |
		(BCM2835_PASSWORD << BCM2835_PASSWORD_SHIFT) |
		BCM2835_RSTC_WRCFG_FULL_RESET);
}

static device_method_t bcmwd_methods[] = {
	DEVMETHOD(device_probe, bcmwd_probe),
	DEVMETHOD(device_attach, bcmwd_attach),

	DEVMETHOD_END
};

static driver_t bcmwd_driver = {
	"bcmwd",
	bcmwd_methods,
	sizeof(struct bcmwd_softc),
};
static devclass_t bcmwd_devclass;

DRIVER_MODULE(bcmwd, simplebus, bcmwd_driver, bcmwd_devclass, 0, 0);
