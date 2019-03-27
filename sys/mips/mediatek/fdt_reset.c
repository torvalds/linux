/*-
 * Copyright (c) 2016 Stanislav Galabov
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

#include "fdt_reset_if.h"
#include <mips/mediatek/fdt_reset.h>

/*
 * Loop through all the tuples in the resets= property for a device, asserting
 * or deasserting each reset.
 *
 * Be liberal about errors for now: warn about a failure to (de)assert but keep
 * trying with any other resets in the list.  Return ENXIO if any errors were
 * found, and let the caller decide whether the problem is fatal.
 */
static int
assert_deassert_all(device_t consumer, boolean_t assert)
{
	phandle_t rnode;
	device_t resetdev;
	int resetnum, err, i, ncells;
	uint32_t *resets;
	boolean_t anyerrors;

	rnode = ofw_bus_get_node(consumer);
	ncells = OF_getencprop_alloc_multi(rnode, "resets", sizeof(*resets),
	    (void **)&resets);
	if (!assert && ncells < 2) {
		device_printf(consumer, "Warning: No resets specified in fdt "
		    "data; device may not function.");
		return (ENXIO);
	}
	anyerrors = false;
	for (i = 0; i < ncells; i += 2) {
		resetdev = OF_device_from_xref(resets[i]);
		resetnum = resets[i + 1];
		if (resetdev == NULL) {
			if (!assert)
				device_printf(consumer, "Warning: can not find "
				    "driver for reset number %u; device may "
				    "not function\n", resetnum);
			anyerrors = true;
			continue;
		}
		if (assert)
			err = FDT_RESET_ASSERT(resetdev, resetnum);
		else
			err = FDT_RESET_DEASSERT(resetdev, resetnum);
		if (err != 0) {
			if (!assert)
				device_printf(consumer, "Warning: failed to "
				    "deassert reset number %u; device may not "
				    "function\n", resetnum);
			anyerrors = true;
		}
	}
	OF_prop_free(resets);
	return (anyerrors ? ENXIO : 0);
}

int
fdt_reset_assert_all(device_t consumer)
{

	return (assert_deassert_all(consumer, true));
}

int
fdt_reset_deassert_all(device_t consumer)
{

	return (assert_deassert_all(consumer, false));
}

void
fdt_reset_register_provider(device_t provider)
{

	OF_device_register_xref(
	    OF_xref_from_node(ofw_bus_get_node(provider)), provider);
}

void
fdt_reset_unregister_provider(device_t provider)
{

	OF_device_register_xref(OF_xref_from_device(provider), NULL);
}

