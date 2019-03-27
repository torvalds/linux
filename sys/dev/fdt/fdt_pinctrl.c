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

#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>

#include "fdt_pinctrl_if.h"

#include <dev/fdt/fdt_common.h>
#include <dev/fdt/fdt_pinctrl.h>

int
fdt_pinctrl_configure(device_t client, u_int index)
{
	device_t pinctrl;
	phandle_t *configs;
	int i, nconfigs;
	char name[16];

	snprintf(name, sizeof(name), "pinctrl-%u", index);
	nconfigs = OF_getencprop_alloc_multi(ofw_bus_get_node(client), name,
	    sizeof(*configs), (void **)&configs);
	if (nconfigs < 0)
		return (ENOENT);
	if (nconfigs == 0)
		return (0); /* Empty property is documented as valid. */
	for (i = 0; i < nconfigs; i++) {
		if ((pinctrl = OF_device_from_xref(configs[i])) != NULL)
			FDT_PINCTRL_CONFIGURE(pinctrl, configs[i]);
	}
	OF_prop_free(configs);
	return (0);
}

int
fdt_pinctrl_configure_by_name(device_t client, const char * name)
{
	char * names;
	int i, offset, nameslen;

	nameslen = OF_getprop_alloc(ofw_bus_get_node(client), "pinctrl-names",
	    (void **)&names);
	if (nameslen <= 0)
		return (ENOENT);
	for (i = 0, offset = 0; offset < nameslen; i++) {
		if (strcmp(name, &names[offset]) == 0)
			break;
		offset += strlen(&names[offset]) + 1;
	}
	OF_prop_free(names);
	if (offset < nameslen)
		return (fdt_pinctrl_configure(client, i));
	else
		return (ENOENT);
}

static int
pinctrl_register_children(device_t pinctrl, phandle_t parent,
    const char *pinprop)
{
	phandle_t node;

	/*
	 * Recursively descend from parent, looking for nodes that have the
	 * given property, and associate the pinctrl device_t with each one.
	 */
	for (node = OF_child(parent); node != 0; node = OF_peer(node)) {
		pinctrl_register_children(pinctrl, node, pinprop);
		if (pinprop == NULL || OF_hasprop(node, pinprop)) {
			OF_device_register_xref(OF_xref_from_node(node),
			    pinctrl);
		}
	}
	return (0);
}

int
fdt_pinctrl_register(device_t pinctrl, const char *pinprop)
{
	phandle_t node;
	int ret;

	TSENTER();
	node = ofw_bus_get_node(pinctrl);
	OF_device_register_xref(OF_xref_from_node(node), pinctrl);
	ret = pinctrl_register_children(pinctrl, node, pinprop);
	TSEXIT();

	return (ret);
}

static int
pinctrl_configure_children(device_t pinctrl, phandle_t parent)
{
	phandle_t node, *configs;
	int i, nconfigs;

	TSENTER();

	for (node = OF_child(parent); node != 0; node = OF_peer(node)) {
		if (!ofw_bus_node_status_okay(node))
			continue;
		pinctrl_configure_children(pinctrl, node);
		nconfigs = OF_getencprop_alloc_multi(node, "pinctrl-0",
		    sizeof(*configs), (void **)&configs);
		if (nconfigs <= 0)
			continue;
		if (bootverbose) {
			char name[32];
			OF_getprop(node, "name", &name, sizeof(name));
			printf("Processing %d pin-config node(s) in pinctrl-0 for %s\n",
			    nconfigs, name);
		}
		for (i = 0; i < nconfigs; i++) {
			if (OF_device_from_xref(configs[i]) == pinctrl)
				FDT_PINCTRL_CONFIGURE(pinctrl, configs[i]);
		}
		OF_prop_free(configs);
	}
	TSEXIT();
	return (0);
}

int
fdt_pinctrl_configure_tree(device_t pinctrl)
{

	return (pinctrl_configure_children(pinctrl, OF_peer(0)));
}

