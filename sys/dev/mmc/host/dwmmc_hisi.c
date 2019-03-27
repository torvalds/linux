/*
 * Copyright 2015 Andrew Turner.
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

#include <dev/mmc/host/dwmmc_var.h>

#include "opt_mmccam.h"

static device_probe_t hisi_dwmmc_probe;
static device_attach_t hisi_dwmmc_attach;

static int
hisi_dwmmc_probe(device_t dev)
{

	if (!ofw_bus_status_okay(dev))
		return (ENXIO);

	if (!ofw_bus_is_compatible(dev, "hisilicon,hi6220-dw-mshc"))
		return (ENXIO);

	device_set_desc(dev, "Synopsys DesignWare Mobile "
	    "Storage Host Controller (HiSilicon)");

	return (BUS_PROBE_VENDOR);
}

static int
hisi_dwmmc_attach(device_t dev)
{
	struct dwmmc_softc *sc;

	sc = device_get_softc(dev);
	sc->hwtype = HWTYPE_HISILICON;
	/* TODO: Calculate this from a clock driver */
	sc->bus_hz = 24000000; /* 24MHz */

	/*
	 * ARM64TODO: This is likely because we lack support for
	 * DMA when the controller is not cache-coherent on arm64.
	 */
	sc->use_pio = 1;
	sc->desc_count = 1;

	return (dwmmc_attach(dev));
}

static device_method_t hisi_dwmmc_methods[] = {
	/* bus interface */
	DEVMETHOD(device_probe, hisi_dwmmc_probe),
	DEVMETHOD(device_attach, hisi_dwmmc_attach),

	DEVMETHOD_END
};

static devclass_t hisi_dwmmc_devclass;

DEFINE_CLASS_1(hisi_dwmmc, hisi_dwmmc_driver, hisi_dwmmc_methods,
    sizeof(struct dwmmc_softc), dwmmc_driver);

DRIVER_MODULE(hisi_dwmmc, simplebus, hisi_dwmmc_driver,
    hisi_dwmmc_devclass, 0, 0);
DRIVER_MODULE(hisi_dwmmc, ofwbus, hisi_dwmmc_driver, hisi_dwmmc_devclass
    , NULL, NULL);
#ifndef MMCCAM
MMC_DECLARE_BRIDGE(hisi_dwmmc);
#endif
