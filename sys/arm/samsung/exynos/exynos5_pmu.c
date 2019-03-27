/*-
 * Copyright (c) 2014 Ruslan Bukin <br@bsdpad.com>
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
 * Exynos 5 Power Management Unit (PMU)
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/malloc.h>
#include <sys/rman.h>
#include <sys/timeet.h>
#include <sys/timetc.h>
#include <sys/watchdog.h>

#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>

#include <machine/bus.h>
#include <machine/cpu.h>
#include <machine/intr.h>

#include <arm/samsung/exynos/exynos5_common.h>
#include <arm/samsung/exynos/exynos5_pmu.h>

#define	EXYNOS5250	1
#define	EXYNOS5420	2

/* PWR control */
#define	EXYNOS5_PWR_USBHOST_PHY		0x708
#define	EXYNOS5_USBDRD_PHY_CTRL		0x704
#define	EXYNOS5420_USBDRD1_PHY_CTRL	0x708

#define	PHY_POWER_ON			1
#define	PHY_POWER_OFF			0

struct pmu_softc {
	struct resource		*res[1];
	bus_space_tag_t		bst;
	bus_space_handle_t	bsh;
	device_t		dev;
	int			model;
};

struct pmu_softc *pmu_sc;

static struct ofw_compat_data compat_data[] = {
	{"samsung,exynos5420-pmu",	EXYNOS5420},
	{"samsung,exynos5250-pmu",	EXYNOS5250},
	{NULL, 0}
};

static struct resource_spec pmu_spec[] = {
	{ SYS_RES_MEMORY,	0,	RF_ACTIVE },
	{ -1, 0 }
};

static int
pmu_probe(device_t dev)
{

	if (!ofw_bus_status_okay(dev))
		return (ENXIO);

	if (ofw_bus_search_compatible(dev, compat_data)->ocd_data != 0) {
		device_set_desc(dev, "Samsung Exynos 5 Power Management Unit");
		return (BUS_PROBE_DEFAULT);
	}

	return (ENXIO);
}

int
usb2_phy_power_on(void)
{
	struct pmu_softc *sc;

	sc = pmu_sc;
	if (sc == NULL)
		return (-1);

	/* EHCI */
	WRITE4(sc, EXYNOS5_PWR_USBHOST_PHY, PHY_POWER_ON);

	return (0);
}

int
usbdrd_phy_power_on(void)
{
	struct pmu_softc *sc;

	sc = pmu_sc;
	if (sc == NULL)
		return (-1);

	/*
	 * First XHCI controller (left-side USB port on chromebook2)
	 */
	WRITE4(sc, EXYNOS5_USBDRD_PHY_CTRL, PHY_POWER_ON);

	/*
	 * Second XHCI controller (right-side USB port on chrombook2)
	 * Only available on 5420.
	 */
	if (sc->model == EXYNOS5420)
		WRITE4(sc, EXYNOS5420_USBDRD1_PHY_CTRL, PHY_POWER_ON);

	return (0);
}

static int
pmu_attach(device_t dev)
{
	struct pmu_softc *sc;

	sc = device_get_softc(dev);
	sc->dev = dev;
	sc->model = ofw_bus_search_compatible(dev, compat_data)->ocd_data;

	if (bus_alloc_resources(dev, pmu_spec, sc->res)) {
		device_printf(dev, "could not allocate resources\n");
		return (ENXIO);
	}

	/* Memory interface */
	sc->bst = rman_get_bustag(sc->res[0]);
	sc->bsh = rman_get_bushandle(sc->res[0]);

	pmu_sc = sc;

	return (0);
}

static device_method_t pmu_methods[] = {
	DEVMETHOD(device_probe,		pmu_probe),
	DEVMETHOD(device_attach,	pmu_attach),
	{ 0, 0 }
};

static driver_t pmu_driver = {
	"pmu",
	pmu_methods,
	sizeof(struct pmu_softc),
};

static devclass_t pmu_devclass;

DRIVER_MODULE(pmu, simplebus, pmu_driver, pmu_devclass, 0, 0);
