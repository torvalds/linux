/*-
 * Copyright 2016 Michal Meloun <mmel@FreeBSD.org>
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
#include "opt_platform.h"
#include <sys/cdefs.h>
#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/kobj.h>
#include <sys/malloc.h>
#include <sys/systm.h>

#ifdef FDT
#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>
#endif

#include <dev/extres/hwreset/hwreset.h>

#include "hwreset_if.h"

struct hwreset {
	device_t consumer_dev;		/* consumer device*/
	device_t provider_dev;		/* provider device*/
	int 	rst_id;			/* reset id */
};

MALLOC_DEFINE(M_HWRESET, "hwreset", "Reset framework");

int
hwreset_assert(hwreset_t rst)
{

	return (HWRESET_ASSERT(rst->provider_dev, rst->rst_id, true));
}

int
hwreset_deassert(hwreset_t rst)
{

	return (HWRESET_ASSERT(rst->provider_dev, rst->rst_id, false));
}

int
hwreset_is_asserted(hwreset_t rst, bool *value)
{

	return (HWRESET_IS_ASSERTED(rst->provider_dev, rst->rst_id, value));
}

void
hwreset_release(hwreset_t rst)
{
	free(rst, M_HWRESET);
}

int
hwreset_get_by_id(device_t consumer_dev, device_t provider_dev, intptr_t id,
    hwreset_t *rst_out)
{
	hwreset_t rst;

	/* Create handle */
	rst = malloc(sizeof(struct hwreset), M_HWRESET,
	    M_WAITOK | M_ZERO);
	rst->consumer_dev = consumer_dev;
	rst->provider_dev = provider_dev;
	rst->rst_id = id;
	*rst_out = rst;
	return (0);
}

#ifdef FDT
int
hwreset_default_ofw_map(device_t provider_dev, phandle_t xref, int ncells,
    pcell_t *cells, intptr_t *id)
{
	if (ncells == 0)
		*id = 1;
	else if (ncells == 1)
		*id = cells[0];
	else
		return  (ERANGE);

	return (0);
}

int
hwreset_get_by_ofw_idx(device_t consumer_dev, phandle_t cnode, int idx,
    hwreset_t *rst)
{
	phandle_t xnode;
	pcell_t *cells;
	device_t rstdev;
	int ncells, rv;
	intptr_t id;

	if (cnode <= 0)
		cnode = ofw_bus_get_node(consumer_dev);
	if (cnode <= 0) {
		device_printf(consumer_dev,
		    "%s called on not ofw based device\n", __func__);
		return (ENXIO);
	}

	rv = ofw_bus_parse_xref_list_alloc(cnode, "resets", "#reset-cells",
	    idx, &xnode, &ncells, &cells);
	if (rv != 0)
		return (rv);

	/* Tranlate provider to device */
	rstdev = OF_device_from_xref(xnode);
	if (rstdev == NULL) {
		OF_prop_free(cells);
		return (ENODEV);
	}
	/* Map reset to number */
	rv = HWRESET_MAP(rstdev, xnode, ncells, cells, &id);
	OF_prop_free(cells);
	if (rv != 0)
		return (rv);

	return (hwreset_get_by_id(consumer_dev, rstdev, id, rst));
}

int
hwreset_get_by_ofw_name(device_t consumer_dev, phandle_t cnode, char *name,
    hwreset_t *rst)
{
	int rv, idx;

	if (cnode <= 0)
		cnode = ofw_bus_get_node(consumer_dev);
	if (cnode <= 0) {
		device_printf(consumer_dev,
		    "%s called on not ofw based device\n",  __func__);
		return (ENXIO);
	}
	rv = ofw_bus_find_string_index(cnode, "reset-names", name, &idx);
	if (rv != 0)
		return (rv);
	return (hwreset_get_by_ofw_idx(consumer_dev, cnode, idx, rst));
}

void
hwreset_register_ofw_provider(device_t provider_dev)
{
	phandle_t xref, node;

	node = ofw_bus_get_node(provider_dev);
	if (node <= 0)
		panic("%s called on not ofw based device.\n", __func__);

	xref = OF_xref_from_node(node);
	OF_device_register_xref(xref, provider_dev);
}

void
hwreset_unregister_ofw_provider(device_t provider_dev)
{
	phandle_t xref;

	xref = OF_xref_from_device(provider_dev);
	OF_device_register_xref(xref, NULL);
}
#endif
