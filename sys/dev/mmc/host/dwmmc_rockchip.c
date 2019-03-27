/*
 * Copyright 2017 Emmanuel Vadot <manu@freebsd.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *  1. Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *  2. Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/bus.h>
#include <sys/module.h>

#include <machine/bus.h>

#include <dev/mmc/bridge.h>

#include <dev/ofw/ofw_bus_subr.h>

#ifdef EXT_RESOURCES
#include <dev/extres/clk/clk.h>
#endif

#include <dev/mmc/host/dwmmc_var.h>

#include "opt_mmccam.h"

enum RKTYPE {
	RK2928 = 1,
	RK3288,
};

static struct ofw_compat_data compat_data[] = {
	{"rockchip,rk2928-dw-mshc",	RK2928},
	{"rockchip,rk3288-dw-mshc",	RK3288},
	{NULL,				0},
};

static int dwmmc_rockchip_update_ios(struct dwmmc_softc *sc, struct mmc_ios *ios);

static int
rockchip_dwmmc_probe(device_t dev)
{

	if (!ofw_bus_status_okay(dev))
		return (ENXIO);

	if (ofw_bus_search_compatible(dev, compat_data)->ocd_data == 0)
		return (ENXIO);

	device_set_desc(dev, "Synopsys DesignWare Mobile "
	    "Storage Host Controller (RockChip)");

	return (BUS_PROBE_VENDOR);
}

static int
rockchip_dwmmc_attach(device_t dev)
{
	struct dwmmc_softc *sc;
	int type;

	sc = device_get_softc(dev);
	sc->hwtype = HWTYPE_ROCKCHIP;
	type = ofw_bus_search_compatible(dev, compat_data)->ocd_data;

	switch (type) {
	case RK2928:
		sc->use_pio = 1;
		break;
	}

	sc->pwren_inverted = 1;

#ifdef EXT_RESOURCES
	sc->update_ios = &dwmmc_rockchip_update_ios;
#endif

	return (dwmmc_attach(dev));
}

#ifdef EXT_RESOURCES
static int
dwmmc_rockchip_update_ios(struct dwmmc_softc *sc, struct mmc_ios *ios)
{
	unsigned int clock;
	int error;

	if (ios->clock && ios->clock != sc->bus_hz) {
		sc->bus_hz = clock = ios->clock;
		/* Set the MMC clock. */
		if (sc->ciu) {
			/* 
			 * Apparently you need to set the ciu clock to
			 * the double of bus_hz
			 */
			error = clk_set_freq(sc->ciu, clock * 2,
			    CLK_SET_ROUND_DOWN);
			if (error != 0) {
				device_printf(sc->dev,
				    "failed to set frequency to %u Hz: %d\n",
				    clock, error);
				return (error);
			}
		}
	}
	return (0);
}
#endif

static device_method_t rockchip_dwmmc_methods[] = {
	/* bus interface */
	DEVMETHOD(device_probe, rockchip_dwmmc_probe),
	DEVMETHOD(device_attach, rockchip_dwmmc_attach),

	DEVMETHOD_END
};

static devclass_t rockchip_dwmmc_devclass;

DEFINE_CLASS_1(rockchip_dwmmc, rockchip_dwmmc_driver, rockchip_dwmmc_methods,
    sizeof(struct dwmmc_softc), dwmmc_driver);

DRIVER_MODULE(rockchip_dwmmc, simplebus, rockchip_dwmmc_driver,
    rockchip_dwmmc_devclass, 0, 0);
DRIVER_MODULE(rockchip_dwmmc, ofwbus, rockchip_dwmmc_driver,
    rockchip_dwmmc_devclass, NULL, NULL);
#ifndef MMCCAM
MMC_DECLARE_BRIDGE(rockchip_dwmmc);
#endif
