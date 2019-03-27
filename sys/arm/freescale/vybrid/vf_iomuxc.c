/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2013-2014 Ruslan Bukin <br@bsdpad.com>
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
 * Vybrid Family Input/Output Multiplexer Controller (IOMUXC)
 * Chapter 5, Vybrid Reference Manual, Rev. 5, 07/2013
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

#include <dev/fdt/fdt_common.h>
#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>

#include <machine/bus.h>
#include <machine/cpu.h>
#include <machine/intr.h>

#include <arm/freescale/vybrid/vf_iomuxc.h>
#include <arm/freescale/vybrid/vf_common.h>

#define	MUX_MODE_MASK		7
#define	MUX_MODE_SHIFT		20
#define	MUX_MODE_GPIO		0
#define	MUX_MODE_VBUS_EN_OTG	2

#define	IBE		(1 << 0)	/* Input Buffer Enable Field */
#define	OBE		(1 << 1)	/* Output Buffer Enable Field. */
#define	PUE		(1 << 2)	/* Pull / Keep Select Field. */
#define	PKE		(1 << 3)	/* Pull / Keep Enable Field. */
#define	HYS		(1 << 9)	/* Hysteresis Enable Field */
#define	ODE		(1 << 10)	/* Open Drain Enable Field. */
#define	SRE		(1 << 11)	/* Slew Rate Field. */

#define	SPEED_SHIFT		12
#define	SPEED_MASK		0x3
#define	SPEED_LOW		0	/* 50 MHz */
#define	SPEED_MEDIUM		0x1	/* 100 MHz */
#define	SPEED_HIGH		0x3	/* 200 MHz */

#define	PUS_SHIFT		4	/* Pull Up / Down Config Field Shift */
#define	PUS_MASK		0x3
#define	PUS_100_KOHM_PULL_DOWN	0
#define	PUS_47_KOHM_PULL_UP	0x1
#define	PUS_100_KOHM_PULL_UP	0x2
#define	PUS_22_KOHM_PULL_UP	0x3

#define	DSE_SHIFT		6	/* Drive Strength Field Shift */
#define	DSE_MASK		0x7
#define	DSE_DISABLED		0	/* Output driver disabled */
#define	DSE_150_OHM		0x1
#define	DSE_75_OHM		0x2
#define	DSE_50_OHM		0x3
#define	DSE_37_OHM		0x4
#define	DSE_30_OHM		0x5
#define	DSE_25_OHM		0x6
#define	DSE_20_OHM		0x7

#define	MAX_MUX_LEN		1024

struct iomuxc_softc {
	struct resource		*tmr_res[1];
	bus_space_tag_t		bst;
	bus_space_handle_t	bsh;
	device_t		dev;
};

static struct resource_spec iomuxc_spec[] = {
	{ SYS_RES_MEMORY,	0,	RF_ACTIVE },
	{ -1, 0 }
};

static int
iomuxc_probe(device_t dev)
{

	if (!ofw_bus_status_okay(dev))
		return (ENXIO);

	if (!ofw_bus_is_compatible(dev, "fsl,mvf600-iomuxc"))
		return (ENXIO);

	device_set_desc(dev, "Vybrid Family IOMUXC Unit");
	return (BUS_PROBE_DEFAULT);
}

static int
pinmux_set(struct iomuxc_softc *sc)
{
	phandle_t child, parent, root;
	pcell_t iomux_config[MAX_MUX_LEN];
	int len;
	int values;
	int pin;
	int pin_cfg;
	int i;

	root = OF_finddevice("/");
	len = 0;
	parent = root;

	/* Find 'iomux_config' prop in the nodes */
	for (child = OF_child(parent); child != 0; child = OF_peer(child)) {

		/* Find a 'leaf'. Start the search from this node. */
		while (OF_child(child)) {
			parent = child;
			child = OF_child(child);
		}

		if (!ofw_bus_node_status_okay(child))
			continue;

		if ((len = OF_getproplen(child, "iomux_config")) > 0) {
			OF_getencprop(child, "iomux_config", iomux_config, len);

			values = len / (sizeof(uint32_t));
			for (i = 0; i < values; i += 2) {
				pin = iomux_config[i];
				pin_cfg = iomux_config[i+1];
#if 0
				device_printf(sc->dev, "Set pin %d to 0x%08x\n",
				    pin, pin_cfg);
#endif
				WRITE4(sc, IOMUXC(pin), pin_cfg);
			}
		}

		if (OF_peer(child) == 0) {
			/* No more siblings. */
			child = parent;
			parent = OF_parent(child);
		}
	}

	return (0);
}

static int
iomuxc_attach(device_t dev)
{
	struct iomuxc_softc *sc;

	sc = device_get_softc(dev);
	sc->dev = dev;

	if (bus_alloc_resources(dev, iomuxc_spec, sc->tmr_res)) {
		device_printf(dev, "could not allocate resources\n");
		return (ENXIO);
	}

	/* Memory interface */
	sc->bst = rman_get_bustag(sc->tmr_res[0]);
	sc->bsh = rman_get_bushandle(sc->tmr_res[0]);

	pinmux_set(sc);

	return (0);
}

static device_method_t iomuxc_methods[] = {
	DEVMETHOD(device_probe,		iomuxc_probe),
	DEVMETHOD(device_attach,	iomuxc_attach),
	{ 0, 0 }
};

static driver_t iomuxc_driver = {
	"iomuxc",
	iomuxc_methods,
	sizeof(struct iomuxc_softc),
};

static devclass_t iomuxc_devclass;

DRIVER_MODULE(iomuxc, simplebus, iomuxc_driver, iomuxc_devclass, 0, 0);
