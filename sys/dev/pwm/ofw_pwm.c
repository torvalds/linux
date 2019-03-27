/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2018 Emmanuel Vadot <manu@FreeBSD.org>
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
#include <sys/resource.h>

#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>

#include <dev/pwm/pwmbus.h>

#include "pwm_if.h"

int
pwm_get_by_ofw_propidx(device_t consumer, phandle_t node,
    const char *prop_name, int idx, pwm_channel_t *out_channel)
{
	phandle_t xref;
	pcell_t *cells;
	struct pwm_channel channel;
	int ncells, rv;

	rv = ofw_bus_parse_xref_list_alloc(node, prop_name, "#pwm-cells",
	  idx, &xref, &ncells, &cells);
	if (rv != 0)
		return (rv);

	channel.dev = OF_device_from_xref(xref);
	if (channel.dev == NULL) {
		OF_prop_free(cells);
		return (ENODEV);
	}

	channel.busdev = PWM_GET_BUS(channel.dev);
	if (channel.busdev == NULL) {
		OF_prop_free(cells);
		return (ENODEV);
	}

	channel.channel = cells[0];
	channel.period = cells[1];

	if (ncells >= 3)
		channel.flags = cells[2];

	*out_channel = malloc(sizeof(struct pwm_channel), M_DEVBUF, M_WAITOK | M_ZERO);
	**out_channel = channel;
	return (0);
}

int
pwm_get_by_ofw_idx(device_t consumer, phandle_t node, int idx,
    pwm_channel_t *out_channel)
{

	return (pwm_get_by_ofw_propidx(consumer, node, "pwms", idx, out_channel));
}

int
pwm_get_by_ofw_property(device_t consumer, phandle_t node,
    const char *prop_name, pwm_channel_t *out_channel)
{

	return (pwm_get_by_ofw_propidx(consumer, node, prop_name, 0, out_channel));
}

int
pwm_get_by_ofw_name(device_t consumer, phandle_t node, const char *name,
    pwm_channel_t *out_channel)
{
	int rv, idx;

	rv = ofw_bus_find_string_index(node, "pwm-names", name, &idx);
	if (rv != 0)
		return (rv);

	return (pwm_get_by_ofw_idx(consumer, node, idx, out_channel));
}
