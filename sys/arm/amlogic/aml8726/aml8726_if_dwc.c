/*-
 * Copyright (c) 2015 Ganbold Tsagaankhuu <ganbold@FreeBSD.org>
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
#include <sys/module.h>

#include <machine/bus.h>

#include <dev/dwc/if_dwc.h>
#include <dev/dwc/if_dwcvar.h>
#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>

#include "if_dwc_if.h"

static int
aml8726_if_dwc_probe(device_t dev)
{

	if (!ofw_bus_status_okay(dev))
		return (ENXIO);
	if (!ofw_bus_is_compatible(dev, "amlogic,meson6-dwmac"))
		return (ENXIO);
	device_set_desc(dev, "Amlogic Meson Gigabit Ethernet Controller");

	return (BUS_PROBE_DEFAULT);
}

static int
aml8726_if_dwc_init(device_t dev)
{

	return (0);
}

static int
aml8726_if_dwc_mac_type(device_t dev)
{

	return (DWC_GMAC_ALT_DESC);
}

static int
aml8726_if_dwc_mii_clk(device_t dev)
{

	return (GMAC_MII_CLK_100_150M_DIV62);
}

static device_method_t aml8726_dwc_methods[] = {
	DEVMETHOD(device_probe,		aml8726_if_dwc_probe),

	DEVMETHOD(if_dwc_init,		aml8726_if_dwc_init),
	DEVMETHOD(if_dwc_mac_type,	aml8726_if_dwc_mac_type),
	DEVMETHOD(if_dwc_mii_clk,	aml8726_if_dwc_mii_clk),

	DEVMETHOD_END
};

static devclass_t aml8726_dwc_devclass;

extern driver_t dwc_driver;

DEFINE_CLASS_1(dwc, aml8726_dwc_driver, aml8726_dwc_methods,
    sizeof(struct dwc_softc), dwc_driver);
DRIVER_MODULE(aml8726_dwc, simplebus, aml8726_dwc_driver,
    aml8726_dwc_devclass, 0, 0);

MODULE_DEPEND(aml8726_dwc, dwc, 1, 1, 1);
