/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2013 Ian Lepore <ian@freebsd.org>
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
 * USBPHY "no-op" driver for Freescale family of SoCs.  This driver is used on
 * SoCs which have usbphy hardware whose clocks need to be enabled, but no other
 * action has to be taken to make the hardware work.
 */

#include "opt_bus.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/bus.h>
#include <sys/rman.h>

#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>

#include <machine/bus.h>

#include <arm/freescale/imx/imx_ccmvar.h>

/*
 * Table of supported FDT compat strings.
 */
static struct ofw_compat_data compat_data[] = {
	{"nop-usbphy",		true},
	{"usb-nop-xceiv",	true},
	{NULL,		 	false},
};

struct usbphy_softc {
	device_t	dev;
	u_int		phy_num;
};

static int
usbphy_detach(device_t dev)
{

	return (0);
}

static int
usbphy_attach(device_t dev)
{
	struct usbphy_softc *sc;

	sc = device_get_softc(dev);

	/*
         * Turn on the phy clocks.
         */
	imx_ccm_usbphy_enable(dev);

	return (0);
}

static int
usbphy_probe(device_t dev)
{

	if (!ofw_bus_status_okay(dev))
		return (ENXIO);

	if (ofw_bus_search_compatible(dev, compat_data)->ocd_data != 0) {
		device_set_desc(dev, "Freescale USB PHY");
		return (BUS_PROBE_DEFAULT);
	}

	return (ENXIO);
}

static device_method_t usbphy_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,  usbphy_probe),
	DEVMETHOD(device_attach, usbphy_attach),
	DEVMETHOD(device_detach, usbphy_detach),

	DEVMETHOD_END
};

static driver_t usbphy_driver = {
	"usbphy",
	usbphy_methods,
	sizeof(struct usbphy_softc)
};

static devclass_t usbphy_devclass;

DRIVER_MODULE(usbphy, simplebus, usbphy_driver, usbphy_devclass, 0, 0);

