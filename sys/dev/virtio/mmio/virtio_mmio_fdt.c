/*-
 * Copyright (c) 2014 Ruslan Bukin <br@bsdpad.com>
 * Copyright (c) 2014 The FreeBSD Foundation
 * All rights reserved.
 *
 * This software was developed by SRI International and the University of
 * Cambridge Computer Laboratory under DARPA/AFRL contract (FA8750-10-C-0237)
 * ("CTSRD"), as part of the DARPA CRASH research programme.
 *
 * Portions of this software were developed by Andrew Turner
 * under sponsorship from the FreeBSD Foundation.
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
 * VirtIO MMIO interface.
 * This driver is heavily based on VirtIO PCI interface driver.
 */

/*
 * FDT example:
 *		virtio_block@1000 {
 *			compatible = "virtio,mmio";
 *			reg = <0x1000 0x100>;
 *			interrupts = <63>;
 *			interrupt-parent = <&GIC>;
 *		};
 */

#include "opt_platform.h"

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/kernel.h>
#include <sys/module.h>

#include <dev/fdt/fdt_common.h>
#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>

#include <dev/virtio/mmio/virtio_mmio.h>

static int	vtmmio_fdt_probe(device_t);
static int	vtmmio_fdt_attach(device_t);

static device_method_t vtmmio_fdt_methods[] = {
	/* Device interface. */
	DEVMETHOD(device_probe,		vtmmio_fdt_probe),
	DEVMETHOD(device_attach,	vtmmio_fdt_attach),

	DEVMETHOD_END
};

DEFINE_CLASS_1(virtio_mmio, vtmmio_fdt_driver, vtmmio_fdt_methods,
    sizeof(struct vtmmio_softc), vtmmio_driver);

static devclass_t vtmmio_fdt_devclass;

DRIVER_MODULE(virtio_mmio, simplebus, vtmmio_fdt_driver, vtmmio_fdt_devclass,
    0, 0);
DRIVER_MODULE(virtio_mmio, ofwbus, vtmmio_fdt_driver, vtmmio_fdt_devclass, 0,0);
MODULE_DEPEND(virtio_mmio, simplebus, 1, 1, 1);
MODULE_DEPEND(virtio_mmio, virtio, 1, 1, 1);

static int
vtmmio_fdt_probe(device_t dev)
{

	if (!ofw_bus_status_okay(dev))
		return (ENXIO);

	if (!ofw_bus_is_compatible(dev, "virtio,mmio"))
		return (ENXIO);

	device_set_desc(dev, "VirtIO MMIO adapter");
	return (BUS_PROBE_DEFAULT);
}

static int
vtmmio_setup_platform(device_t dev, struct vtmmio_softc *sc)
{
	phandle_t platform_node;
	struct fdt_ic *ic;
	phandle_t xref;
	phandle_t node;

	sc->platform = NULL;

	if ((node = ofw_bus_get_node(dev)) == -1)
		return (ENXIO);

	if (OF_searchencprop(node, "platform", &xref,
		sizeof(xref)) == -1) {
		return (ENXIO);
	}

	platform_node = OF_node_from_xref(xref);

	SLIST_FOREACH(ic, &fdt_ic_list_head, fdt_ics) {
		if (ic->iph == platform_node) {
			sc->platform = ic->dev;
			break;
		}
	}

	if (sc->platform == NULL) {
		/* No platform-specific device. Ignore it. */
	}

	return (0);
}

static int
vtmmio_fdt_attach(device_t dev)
{
	struct vtmmio_softc *sc;

	sc = device_get_softc(dev);
	vtmmio_setup_platform(dev, sc);

	return (vtmmio_attach(dev));
}
