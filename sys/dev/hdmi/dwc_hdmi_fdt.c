/*-
 * Copyright (c) 2015 Oleksandr Tymoshenko <gonzo@freebsd.org>
 * Copyright (c) 2016 Jared McNeill <jmcneill@invisible.ca>
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

/*
 * HDMI core module
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/bus.h>
#include <sys/rman.h>

#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>

#include <machine/bus.h>

#include <dev/extres/clk/clk.h>

#include <dev/videomode/videomode.h>
#include <dev/videomode/edidvar.h>

#include <dev/hdmi/dwc_hdmi.h>

#include "hdmi_if.h"

struct dwc_hdmi_fdt_softc {
	struct dwc_hdmi_softc	base;
	clk_t			clk_hdmi;
	clk_t			clk_ahb;
	phandle_t		i2c_xref;
};

static struct ofw_compat_data compat_data[] = {
	{ "synopsys,dwc-hdmi",	1 },
	{ NULL,	            	0 }
};

static device_t
dwc_hdmi_fdt_get_i2c_dev(device_t dev)
{
	struct dwc_hdmi_fdt_softc *sc;

	sc = device_get_softc(dev);

	if (sc->i2c_xref == 0)
		return (NULL);

	return (OF_device_from_xref(sc->i2c_xref));
}

static int
dwc_hdmi_fdt_detach(device_t dev)
{
	struct dwc_hdmi_fdt_softc *sc;

	sc = device_get_softc(dev);

	if (sc->clk_ahb != NULL)
		clk_release(sc->clk_ahb);
	if (sc->clk_hdmi != NULL)
		clk_release(sc->clk_hdmi);

	if (sc->base.sc_mem_res != NULL)
		bus_release_resource(dev, SYS_RES_MEMORY,
		    sc->base.sc_mem_rid, sc->base.sc_mem_res);

	return (0);
}

static int
dwc_hdmi_fdt_attach(device_t dev)
{
	struct dwc_hdmi_fdt_softc *sc;
	phandle_t node, i2c_xref;
	uint32_t freq;
	int err;

	sc = device_get_softc(dev);
	sc->base.sc_dev = dev;
	sc->base.sc_get_i2c_dev = dwc_hdmi_fdt_get_i2c_dev;
	err = 0;

	/* Allocate memory resources. */
	sc->base.sc_mem_rid = 0;
	sc->base.sc_mem_res = bus_alloc_resource_any(dev, SYS_RES_MEMORY,
	    &sc->base.sc_mem_rid, RF_ACTIVE);
	if (sc->base.sc_mem_res == NULL) {
		device_printf(dev, "Cannot allocate memory resources\n");
		err = ENXIO;
		goto out;
	}

	node = ofw_bus_get_node(dev);
	if (OF_getencprop(node, "ddc", &i2c_xref, sizeof(i2c_xref)) == -1)
		sc->i2c_xref = 0;
	else
		sc->i2c_xref = i2c_xref;

	if (OF_getencprop(node, "reg-shift", &sc->base.sc_reg_shift,
	    sizeof(sc->base.sc_reg_shift)) <= 0)
		sc->base.sc_reg_shift = 0;

	if (clk_get_by_ofw_name(dev, 0, "hdmi", &sc->clk_hdmi) != 0 ||
	    clk_get_by_ofw_name(dev, 0, "ahb", &sc->clk_ahb) != 0) {
		device_printf(dev, "Cannot get clocks\n");
		err = ENXIO;
		goto out;
	}
	if (OF_getencprop(node, "clock-frequency", &freq, sizeof(freq)) > 0) {
		err = clk_set_freq(sc->clk_hdmi, freq, CLK_SET_ROUND_DOWN);
		if (err != 0) {
			device_printf(dev,
			    "Cannot set HDMI clock frequency to %u Hz\n", freq);
			goto out;
		}
	} else
		device_printf(dev, "HDMI clock frequency not specified\n");
	if (clk_enable(sc->clk_hdmi) != 0) {
		device_printf(dev, "Cannot enable HDMI clock\n");
		err = ENXIO;
		goto out;
	}
	if (clk_enable(sc->clk_ahb) != 0) {
		device_printf(dev, "Cannot enable AHB clock\n");
		err = ENXIO;
		goto out;
	}

	return (dwc_hdmi_init(dev));

out:

	dwc_hdmi_fdt_detach(dev);

	return (err);
}

static int
dwc_hdmi_fdt_probe(device_t dev)
{

	if (ofw_bus_search_compatible(dev, compat_data)->ocd_data == 0)
		return (ENXIO);

	device_set_desc(dev, "Synopsys DesignWare HDMI Controller");

	return (BUS_PROBE_DEFAULT);
}

static device_method_t dwc_hdmi_fdt_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,  dwc_hdmi_fdt_probe),
	DEVMETHOD(device_attach, dwc_hdmi_fdt_attach),
	DEVMETHOD(device_detach, dwc_hdmi_fdt_detach),

	/* HDMI methods */
	DEVMETHOD(hdmi_get_edid,	dwc_hdmi_get_edid),
	DEVMETHOD(hdmi_set_videomode,	dwc_hdmi_set_videomode),

	DEVMETHOD_END
};

static driver_t dwc_hdmi_fdt_driver = {
	"dwc_hdmi",
	dwc_hdmi_fdt_methods,
	sizeof(struct dwc_hdmi_fdt_softc)
};

static devclass_t dwc_hdmi_fdt_devclass;

DRIVER_MODULE(dwc_hdmi_fdt, simplebus, dwc_hdmi_fdt_driver,
    dwc_hdmi_fdt_devclass, 0, 0);
