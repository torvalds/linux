/*-
 * Copyright (c) 2014 Ian Lepore <ian@freebsd.org>
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
 *
 * $FreeBSD$
 */

#include <sys/cdefs.h>
#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/queue.h>
#include <sys/systm.h>

#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>

#include "fdt_clock_if.h"
#include <dev/fdt/fdt_clock.h>

/*
 * Loop through all the tuples in the clocks= property for a device, enabling or
 * disabling each clock.
 *
 * Be liberal about errors for now: warn about a failure to enable but keep
 * trying with any other clocks in the list.  Return ENXIO if any errors were
 * found, and let the caller decide whether the problem is fatal.
 */
static int
enable_disable_all(device_t consumer, boolean_t enable)
{
	phandle_t cnode;
	device_t clockdev;
	int clocknum, err, i, ncells;
	uint32_t *clks;
	boolean_t anyerrors;

	cnode = ofw_bus_get_node(consumer);
	ncells = OF_getencprop_alloc_multi(cnode, "clocks", sizeof(*clks),
	    (void **)&clks);
	if (enable && ncells < 2) {
		device_printf(consumer, "Warning: No clocks specified in fdt "
		    "data; device may not function.");
		return (ENXIO);
	}
	anyerrors = false;
	for (i = 0; i < ncells; i += 2) {
		clockdev = OF_device_from_xref(clks[i]);
		clocknum = clks[i + 1];
		if (clockdev == NULL) {
			if (enable)
				device_printf(consumer, "Warning: can not find "
				    "driver for clock number %u; device may not "
				    "function\n", clocknum);
			anyerrors = true;
			continue;
		}
		if (enable)
			err = FDT_CLOCK_ENABLE(clockdev, clocknum);
		else
			err = FDT_CLOCK_DISABLE(clockdev, clocknum);
		if (err != 0) {
			if (enable)
				device_printf(consumer, "Warning: failed to "
				    "enable clock number %u; device may not "
				    "function\n", clocknum);
			anyerrors = true;
		}
	}
	OF_prop_free(clks);
	return (anyerrors ? ENXIO : 0);
}

int
fdt_clock_get_info(device_t consumer, int n, struct fdt_clock_info *info)
{
	phandle_t cnode;
	device_t clockdev;
	int clocknum, err, ncells;
	uint32_t *clks;

	cnode = ofw_bus_get_node(consumer);
	ncells = OF_getencprop_alloc_multi(cnode, "clocks", sizeof(*clks),
	    (void **)&clks);
	if (ncells <= 0)
		return (ENXIO);
	n *= 2;
	if (ncells <= n)
		err = ENXIO;
	else {
		clockdev = OF_device_from_xref(clks[n]);
		if (clockdev == NULL)
			err = ENXIO;
		else  {
			/*
			 * Make struct contents minimally valid, then call
			 * provider to fill in what it knows (provider can
			 * override anything it wants to).
			 */
			clocknum = clks[n + 1];
			bzero(info, sizeof(*info));
			info->provider = clockdev;
			info->index = clocknum;
			info->name = "";
			err = FDT_CLOCK_GET_INFO(clockdev, clocknum, info);
		}
	}
	OF_prop_free(clks);
	return (err);
}

int
fdt_clock_enable_all(device_t consumer)
{

	return (enable_disable_all(consumer, true));
}

int
fdt_clock_disable_all(device_t consumer)
{

	return (enable_disable_all(consumer, false));
}

void
fdt_clock_register_provider(device_t provider)
{

	OF_device_register_xref(
	    OF_xref_from_node(ofw_bus_get_node(provider)), provider);
}

void
fdt_clock_unregister_provider(device_t provider)
{

	OF_device_register_xref(OF_xref_from_device(provider), NULL);
}

