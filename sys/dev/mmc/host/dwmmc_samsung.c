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

#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>

#include <dev/mmc/host/dwmmc_var.h>
#include <dev/mmc/host/dwmmc_reg.h>

#define	WRITE4(_sc, _reg, _val)		\
	bus_write_4((_sc)->res[0], _reg, _val)

static struct ofw_compat_data compat_data[] = {
	{"samsung,exynos5420-dw-mshc",	1},
	{NULL,				0},
};

static int
samsung_dwmmc_probe(device_t dev)
{

	if (!ofw_bus_status_okay(dev))
		return (ENXIO);

	if (ofw_bus_search_compatible(dev, compat_data)->ocd_data == 0)
		return (ENXIO);

	device_set_desc(dev, "Synopsys DesignWare Mobile "
	    "Storage Host Controller (Samsung)");

	return (BUS_PROBE_VENDOR);
}

static int
samsung_dwmmc_attach(device_t dev)
{
	struct dwmmc_softc *sc;
	pcell_t dts_value[3];
	phandle_t node;
	int len;

	sc = device_get_softc(dev);
	sc->hwtype = HWTYPE_EXYNOS;

	if ((node = ofw_bus_get_node(sc->dev)) == -1)
		return (ENXIO);

	if ((len = OF_getproplen(node, "samsung,dw-mshc-ciu-div")) <= 0)
		return (ENXIO);
	OF_getencprop(node, "samsung,dw-mshc-ciu-div", dts_value, len);
	sc->sdr_timing = (dts_value[0] << SDMMC_CLKSEL_DIVIDER_SHIFT);
	sc->ddr_timing = (dts_value[0] << SDMMC_CLKSEL_DIVIDER_SHIFT);

	if ((len = OF_getproplen(node, "samsung,dw-mshc-sdr-timing")) <= 0)
		return (ENXIO);
	OF_getencprop(node, "samsung,dw-mshc-sdr-timing", dts_value, len);
	sc->sdr_timing |= ((dts_value[0] << SDMMC_CLKSEL_SAMPLE_SHIFT) |
			  (dts_value[1] << SDMMC_CLKSEL_DRIVE_SHIFT));

	if ((len = OF_getproplen(node, "samsung,dw-mshc-ddr-timing")) <= 0)
		return (ENXIO);
	OF_getencprop(node, "samsung,dw-mshc-ddr-timing", dts_value, len);
	sc->ddr_timing |= ((dts_value[0] << SDMMC_CLKSEL_SAMPLE_SHIFT) |
			  (dts_value[1] << SDMMC_CLKSEL_DRIVE_SHIFT));

	WRITE4(sc, EMMCP_MPSBEGIN0, 0);
	WRITE4(sc, EMMCP_SEND0, 0);
	WRITE4(sc, EMMCP_CTRL0, (MPSCTRL_SECURE_READ_BIT |
	    MPSCTRL_SECURE_WRITE_BIT |
	    MPSCTRL_NON_SECURE_READ_BIT |
	    MPSCTRL_NON_SECURE_WRITE_BIT |
	    MPSCTRL_VALID));

	return (dwmmc_attach(dev));
}

static device_method_t samsung_dwmmc_methods[] = {
	/* bus interface */
	DEVMETHOD(device_probe, samsung_dwmmc_probe),
	DEVMETHOD(device_attach, samsung_dwmmc_attach),

	DEVMETHOD_END
};

static devclass_t samsung_dwmmc_devclass;

DEFINE_CLASS_1(samsung_dwmmc, samsung_dwmmc_driver, samsung_dwmmc_methods,
    sizeof(struct dwmmc_softc), dwmmc_driver);

DRIVER_MODULE(samsung_dwmmc, simplebus, samsung_dwmmc_driver,
    samsung_dwmmc_devclass, 0, 0);
DRIVER_MODULE(samsung_dwmmc, ofwbus, samsung_dwmmc_driver, samsung_dwmmc_devclass
    , NULL, NULL);
#ifndef MMCCAM
MMC_DECLARE_BRIDGE(samsung_dwmmc);
#endif
