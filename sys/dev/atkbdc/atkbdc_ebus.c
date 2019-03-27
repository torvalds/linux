/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 1999 Kazutaka YOKOTA <yokota@zodiac.mech.utsunomiya-u.ac.jp>
 * Copyright (c) 2005 Marius Strobl <marius@FreeBSD.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer as
 *    the first lines of this file unmodified.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHORS ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 *	from: FreeBSD: src/sys/isa/atkbdc_isa.c,v 1.31 2005/05/29 04:42:28 nyan
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "opt_kbd.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/bus.h>
#include <sys/kbio.h>
#include <sys/malloc.h>

#include <dev/ofw/ofw_bus.h>

#include <machine/resource.h>
#include <machine/ver.h>

#include <sys/rman.h>

#include <dev/kbd/kbdreg.h>
#include <dev/atkbdc/atkbdreg.h>
#include <dev/atkbdc/atkbdc_subr.h>
#include <dev/atkbdc/atkbdcreg.h>
#include <dev/atkbdc/psm.h>

static device_probe_t atkbdc_ebus_probe;
static device_attach_t atkbdc_ebus_attach;

static device_method_t atkbdc_ebus_methods[] = {
	DEVMETHOD(device_probe,		atkbdc_ebus_probe),
	DEVMETHOD(device_attach,	atkbdc_ebus_attach),
	DEVMETHOD(device_suspend,	bus_generic_suspend),
	DEVMETHOD(device_resume,	bus_generic_resume),

	DEVMETHOD(bus_print_child,	atkbdc_print_child),
	DEVMETHOD(bus_read_ivar,	atkbdc_read_ivar),
	DEVMETHOD(bus_write_ivar,	atkbdc_write_ivar),
	DEVMETHOD(bus_get_resource_list,atkbdc_get_resource_list),
	DEVMETHOD(bus_alloc_resource,	bus_generic_rl_alloc_resource),
	DEVMETHOD(bus_release_resource,	bus_generic_rl_release_resource),
	DEVMETHOD(bus_activate_resource, bus_generic_activate_resource),
	DEVMETHOD(bus_deactivate_resource, bus_generic_deactivate_resource),
	DEVMETHOD(bus_get_resource,	bus_generic_rl_get_resource),
	DEVMETHOD(bus_set_resource,	bus_generic_rl_set_resource),
	DEVMETHOD(bus_delete_resource,	bus_generic_rl_delete_resource),
	DEVMETHOD(bus_setup_intr,	bus_generic_setup_intr),
	DEVMETHOD(bus_teardown_intr,	bus_generic_teardown_intr),

	{ 0, 0 }
};

static driver_t atkbdc_ebus_driver = {
	ATKBDC_DRIVER_NAME,
	atkbdc_ebus_methods,
	sizeof(atkbdc_softc_t *),
};

DRIVER_MODULE(atkbdc, ebus, atkbdc_ebus_driver, atkbdc_devclass, 0, 0);

static int
atkbdc_ebus_probe(device_t dev)
{
	struct resource *port0, *port1;
	rman_res_t count, start;
	int error, rid;

	if (strcmp(ofw_bus_get_name(dev), "8042") != 0)
		return (ENXIO);

	/*
	 * On AXi and AXmp boards the NS16550 (used to connect keyboard/
	 * mouse) share their IRQ lines with the i8042. Any IRQ activity
	 * (typically during attach) of the NS16550 used to connect the
	 * keyboard when actually the PS/2 keyboard is selected in OFW
	 * causes interaction with the OBP i8042 driver resulting in a
	 * hang and vice versa. As RS232 keyboards and mice obviously
	 * aren't meant to be used in parallel with PS/2 ones on these
	 * boards don't attach to the i8042 in case the PS/2 keyboard
	 * isn't selected in order to prevent such hangs.
	 * Note that it's not sufficient here to rely on the '8042' node
	 * only showing up when a PS/2 keyboard is actually connected as
	 * the user still might have adjusted the 'keyboard' alias to
	 * point to the RS232 keyboard.
	 */
	if ((!strcmp(sparc64_model, "SUNW,UltraAX-MP") ||
	    !strcmp(sparc64_model, "SUNW,UltraSPARC-IIi-Engine")) &&
	    OF_finddevice("keyboard") != ofw_bus_get_node(dev)) {
		device_disable(dev);
		return (ENXIO);
	}

	device_set_desc(dev, "Keyboard controller (i8042)");

	/*
	 * The '8042' node has two identical 8 addresses wide resources
	 * which are apparently meant to be used one for the keyboard
	 * half and the other one for the mouse half. To simplify matters
	 * we use one for the command/data port resource and the other
	 * one for the status port resource as the atkbdc(4) back-end
	 * expects two struct resource rather than two bus space handles.
	 */
	rid = 0;
	if (bus_get_resource(dev, SYS_RES_MEMORY, rid, &start, &count) != 0) {
		device_printf(dev,
		    "cannot determine command/data port resource\n");
		return (ENXIO);
	}
	port0 = bus_alloc_resource(dev, SYS_RES_MEMORY, &rid, start, start, 1,
	    RF_ACTIVE);
	if (port0 == NULL) {
		device_printf(dev,
		    "cannot allocate command/data port resource\n");
		return (ENXIO);
	}

	rid = 1;
	if (bus_get_resource(dev, SYS_RES_MEMORY, rid, &start, &count) != 0) {
		device_printf(dev, "cannot determine status port resource\n");
		error = ENXIO;
		goto fail_port0;
	}
	start += KBD_STATUS_PORT;
	port1 = bus_alloc_resource(dev, SYS_RES_MEMORY, &rid, start, start, 1,
	    RF_ACTIVE);
	if (port1 == NULL) {
		device_printf(dev, "cannot allocate status port resource\n");
		error = ENXIO;
		goto fail_port0;
	}

	error = atkbdc_probe_unit(device_get_unit(dev), port0, port1);
	if (error != 0)
		device_printf(dev, "atkbdc_porbe_unit failed\n");

	bus_release_resource(dev, SYS_RES_MEMORY, 1, port1);
 fail_port0:
	bus_release_resource(dev, SYS_RES_MEMORY, 0, port0);

	return (error);
}

static int
atkbdc_ebus_attach(device_t dev)
{
	atkbdc_softc_t *sc;
	atkbdc_device_t *adi;
	device_t cdev;
	phandle_t child;
	rman_res_t count, intr, start;
	int children, error, rid, unit;
	char *cname, *dname;

	unit = device_get_unit(dev);
	sc = *(atkbdc_softc_t **)device_get_softc(dev);
	if (sc == NULL) {
		/*
		 * We have to maintain two copies of the kbdc_softc struct,
		 * as the low-level console needs to have access to the
		 * keyboard controller before kbdc is probed and attached.
		 * kbdc_soft[] contains the default entry for that purpose.
		 * See atkbdc.c. XXX
		 */
		sc = atkbdc_get_softc(unit);
		if (sc == NULL)
			return (ENOMEM);
		device_set_softc(dev, sc);
	}

	rid = 0;
	if (bus_get_resource(dev, SYS_RES_MEMORY, rid, &start, &count) != 0) {
		device_printf(dev,
		    "cannot determine command/data port resource\n");
		return (ENXIO);
	}
	sc->retry = 5000;
	sc->port0 = bus_alloc_resource(dev, SYS_RES_MEMORY, &rid, start, start,
	    1, RF_ACTIVE);
	if (sc->port0 == NULL) {
		device_printf(dev,
		    "cannot allocate command/data port resource\n");
		return (ENXIO);
	}

	rid = 1;
	if (bus_get_resource(dev, SYS_RES_MEMORY, rid, &start, &count) != 0) {
		device_printf(dev, "cannot determine status port resource\n");
		error = ENXIO;
		goto fail_port0;
	}
	start += KBD_STATUS_PORT;
	sc->port1 = bus_alloc_resource(dev, SYS_RES_MEMORY, &rid, start, start,
	    1, RF_ACTIVE);
	if (sc->port1 == NULL) {
		device_printf(dev, "cannot allocate status port resource\n");
		error = ENXIO;
		goto fail_port0;
	}

	error = atkbdc_attach_unit(unit, sc, sc->port0, sc->port1);
	if (error != 0) {
		device_printf(dev, "atkbdc_attach_unit failed\n");
		goto fail_port1;
	}

	/* Attach children. */
	children = 0;
	for (child = OF_child(ofw_bus_get_node(dev)); child != 0;
	    child = OF_peer(child)) {
		if ((OF_getprop_alloc(child, "name", (void **)&cname)) == -1)
			continue;
		if (children >= 2) {
			device_printf(dev,
			    "<%s>: only two children per 8042 supported\n",
			    cname);
			OF_prop_free(cname);
			continue;
		}
		adi = malloc(sizeof(struct atkbdc_device), M_ATKBDDEV,
		    M_NOWAIT | M_ZERO);
		if (adi == NULL) {
			device_printf(dev, "<%s>: malloc failed\n", cname);
			OF_prop_free(cname);
			continue;
		}
		if (strcmp(cname, "kb_ps2") == 0) {
			adi->rid = KBDC_RID_KBD;
			dname = ATKBD_DRIVER_NAME;
		} else if (strcmp(cname, "kdmouse") == 0) {
			adi->rid = KBDC_RID_AUX;
			dname = PSM_DRIVER_NAME;
		} else {
			device_printf(dev, "<%s>: unknown device\n", cname);
			free(adi, M_ATKBDDEV);
			OF_prop_free(cname);
			continue;
		}
		intr = bus_get_resource_start(dev, SYS_RES_IRQ, adi->rid);
		if (intr == 0) {
			device_printf(dev,
			    "<%s>: cannot determine interrupt resource\n",
			    cname);
			free(adi, M_ATKBDDEV);
			OF_prop_free(cname);
			continue;
		}
		resource_list_init(&adi->resources);
		resource_list_add(&adi->resources, SYS_RES_IRQ, adi->rid,
		    intr, intr, 1);
		if ((cdev = device_add_child(dev, dname, -1)) == NULL) {
			device_printf(dev, "<%s>: device_add_child failed\n",
			    cname);
			resource_list_free(&adi->resources);
			free(adi, M_ATKBDDEV);
			OF_prop_free(cname);
			continue;
		}
		device_set_ivars(cdev, adi);
		children++;
	}

	error = bus_generic_attach(dev);
	if (error != 0) {
		device_printf(dev, "bus_generic_attach failed\n");
		goto fail_port1;
	}

	return (0);

 fail_port1:
	bus_release_resource(dev, SYS_RES_MEMORY, 1, sc->port1);
 fail_port0:
	bus_release_resource(dev, SYS_RES_MEMORY, 0, sc->port0);

	return (error);
}
