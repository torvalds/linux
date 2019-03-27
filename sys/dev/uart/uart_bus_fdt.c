/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2009-2010 The FreeBSD Foundation
 * All rights reserved.
 *
 * This software was developed by Semihalf under sponsorship from
 * the FreeBSD Foundation.
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
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
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

#include "opt_platform.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/kernel.h>
#include <sys/module.h>

#include <machine/bus.h>

#include <dev/fdt/fdt_common.h>
#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>
#include <dev/uart/uart.h>
#include <dev/uart/uart_bus.h>
#include <dev/uart/uart_cpu.h>
#include <dev/uart/uart_cpu_fdt.h>

static int uart_fdt_probe(device_t);

static device_method_t uart_fdt_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		uart_fdt_probe),
	DEVMETHOD(device_attach,	uart_bus_attach),
	DEVMETHOD(device_detach,	uart_bus_detach),
	{ 0, 0 }
};

static driver_t uart_fdt_driver = {
	uart_driver_name,
	uart_fdt_methods,
	sizeof(struct uart_softc),
};

int
uart_fdt_get_clock(phandle_t node, pcell_t *cell)
{

	/* clock-frequency is a FreeBSD-only extension. */
	if ((OF_getencprop(node, "clock-frequency", cell,
	    sizeof(*cell))) <= 0) {
		/* Try to retrieve parent 'bus-frequency' */
		/* XXX this should go to simple-bus fixup or so */
		if ((OF_getencprop(OF_parent(node), "bus-frequency", cell,
		    sizeof(*cell))) <= 0)
			*cell = 0;
	}

	return (0);
}

int
uart_fdt_get_shift(phandle_t node, pcell_t *cell)
{

	if ((OF_getencprop(node, "reg-shift", cell, sizeof(*cell))) <= 0)
		return (-1);
	return (0);
}

int
uart_fdt_get_io_width(phandle_t node, pcell_t *cell)
{

	if ((OF_getencprop(node, "reg-io-width", cell, sizeof(*cell))) <= 0)
		return (-1);
	return (0);
}

static uintptr_t
uart_fdt_find_device(device_t dev)
{
	struct ofw_compat_data **cd;
	const struct ofw_compat_data *ocd;

	SET_FOREACH(cd, uart_fdt_class_and_device_set) {
		ocd = ofw_bus_search_compatible(dev, *cd);
		if (ocd->ocd_data != 0)
			return (ocd->ocd_data);
	}
	return (0);
}

static int
phandle_chosen_propdev(phandle_t chosen, const char *name, phandle_t *node)
{
	char buf[64];
	char *sep;

	if (OF_getprop(chosen, name, buf, sizeof(buf)) <= 0)
		return (ENXIO);
	/*
	 * stdout-path may have a ':' to separate the device from the
	 * connection settings. Split the string so we just pass the former
	 * to OF_finddevice.
	 */
	sep = strchr(buf, ':');
	if (sep != NULL)
		*sep = '\0';
	if ((*node = OF_finddevice(buf)) == -1)
		return (ENXIO);

	return (0);
}

static const struct ofw_compat_data *
uart_fdt_find_compatible(phandle_t node, const struct ofw_compat_data *cd)
{
	const struct ofw_compat_data *ocd;

	for (ocd = cd; ocd->ocd_str != NULL; ocd++) {
		if (ofw_bus_node_is_compatible(node, ocd->ocd_str))
			return (ocd);
	}
	return (NULL);
}

static uintptr_t
uart_fdt_find_by_node(phandle_t node, int class_list)
{
	struct ofw_compat_data **cd;
	const struct ofw_compat_data *ocd;

	if (class_list) {
		SET_FOREACH(cd, uart_fdt_class_set) {
			ocd = uart_fdt_find_compatible(node, *cd);
			if ((ocd != NULL) && (ocd->ocd_data != 0))
				return (ocd->ocd_data);
		}
	} else {
		SET_FOREACH(cd, uart_fdt_class_and_device_set) {
			ocd = uart_fdt_find_compatible(node, *cd);
			if ((ocd != NULL) && (ocd->ocd_data != 0))
				return (ocd->ocd_data);
		}
	}

	return (0);
}

int
uart_cpu_fdt_probe(struct uart_class **classp, bus_space_tag_t *bst,
    bus_space_handle_t *bsh, int *baud, u_int *rclk, u_int *shiftp,
    u_int *iowidthp)
{
	const char *propnames[] = {"stdout-path", "linux,stdout-path", "stdout",
	    "stdin-path", "stdin", NULL};
	const char **name;
	struct uart_class *class;
	phandle_t node, chosen;
	pcell_t br, clk, shift, iowidth;
	char *cp;
	int err;

	/* Has the user forced a specific device node? */
	cp = kern_getenv("hw.fdt.console");
	if (cp == NULL) {
		/*
		 * Retrieve /chosen/std{in,out}.
		 */
		node = -1;
		if ((chosen = OF_finddevice("/chosen")) != -1) {
			for (name = propnames; *name != NULL; name++) {
				if (phandle_chosen_propdev(chosen, *name,
				    &node) == 0)
					break;
			}
		}
		if (chosen == -1 || *name == NULL)
			node = OF_finddevice("serial0"); /* Last ditch */
	} else {
		node = OF_finddevice(cp);
	}

	if (node == -1)
		return (ENXIO);

	/*
	 * Check old style of UART definition first. Unfortunately, the common
	 * FDT processing is not possible if we have clock, power domains and
	 * pinmux stuff.
	 */
	class = (struct uart_class *)uart_fdt_find_by_node(node, 0);
	if (class != NULL) {
		if ((err = uart_fdt_get_clock(node, &clk)) != 0)
			return (err);
	} else {
		/* Check class only linker set */
		class =
		    (struct uart_class *)uart_fdt_find_by_node(node, 1);
		if (class == NULL)
			return (ENXIO);
		clk = 0;
	}

	/*
	 * Retrieve serial attributes.
	 */
	if (uart_fdt_get_shift(node, &shift) != 0)
		shift = uart_getregshift(class);

	if (uart_fdt_get_io_width(node, &iowidth) != 0)
		iowidth = uart_getregiowidth(class);

	if (OF_getencprop(node, "current-speed", &br, sizeof(br)) <= 0)
		br = 0;

	err = OF_decode_addr(node, 0, bst, bsh, NULL);
	if (err != 0)
		return (err);

	*classp = class;
	*baud = br;
	*rclk = clk;
	*shiftp = shift;
	*iowidthp = iowidth;

	return (0);
}

static int
uart_fdt_probe(device_t dev)
{
	struct uart_softc *sc;
	phandle_t node;
	pcell_t clock, shift, iowidth;
	int err;

	sc = device_get_softc(dev);

	if (!ofw_bus_status_okay(dev))
		return (ENXIO);

	sc->sc_class = (struct uart_class *)uart_fdt_find_device(dev);
	if (sc->sc_class == NULL)
		return (ENXIO);

	node = ofw_bus_get_node(dev);

	if ((err = uart_fdt_get_clock(node, &clock)) != 0)
		return (err);
	if (uart_fdt_get_shift(node, &shift) != 0)
		shift = uart_getregshift(sc->sc_class);
	if (uart_fdt_get_io_width(node, &iowidth) != 0)
		iowidth = uart_getregiowidth(sc->sc_class);

	return (uart_bus_probe(dev, (int)shift, (int)iowidth, (int)clock, 0, 0, 0));
}

DRIVER_MODULE(uart, simplebus, uart_fdt_driver, uart_devclass, 0, 0);
DRIVER_MODULE(uart, ofwbus, uart_fdt_driver, uart_devclass, 0, 0);
