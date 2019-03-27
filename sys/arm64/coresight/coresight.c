/*-
 * Copyright (c) 2018 Ruslan Bukin <br@bsdpad.com>
 * All rights reserved.
 *
 * This software was developed by SRI International and the University of
 * Cambridge Computer Laboratory under DARPA/AFRL contract FA8750-10-C-0237
 * ("CTSRD"), as part of the DARPA CRASH research programme.
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
#include <sys/rman.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <machine/bus.h>

#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>

#include <arm64/coresight/coresight.h>

MALLOC_DEFINE(M_CORESIGHT, "coresight", "ARM Coresight");
static struct mtx cs_mtx;

struct coresight_device_list cs_devs;

static int
coresight_get_ports(phandle_t dev_node,
    struct coresight_platform_data *pdata)
{
	phandle_t node, child;
	pcell_t port_reg;
	phandle_t xref;
	char *name;
	int ret;
	phandle_t endpoint_child;
	struct endpoint *endp;

	child = ofw_bus_find_child(dev_node, "ports");
	if (child)
		node = child;
	else
		node = dev_node;

	for (child = OF_child(node); child != 0; child = OF_peer(child)) {
		ret = OF_getprop_alloc(child, "name", (void **)&name);
		if (ret == -1)
			continue;

		if (strcasecmp(name, "port") ||
		    strncasecmp(name, "port@", 6)) {

			port_reg = -1;
			OF_getencprop(child, "reg", (void *)&port_reg, sizeof(port_reg));

			endpoint_child = ofw_bus_find_child(child, "endpoint");
			if (endpoint_child) {
				if (OF_getencprop(endpoint_child, "remote-endpoint", &xref,
				    sizeof(xref)) == -1) {
					printf("failed\n");
					continue;
				}
				endp = malloc(sizeof(struct endpoint), M_CORESIGHT,
				    M_WAITOK | M_ZERO);
				endp->my_node = endpoint_child;
				endp->their_node = OF_node_from_xref(xref);
				endp->dev_node = dev_node;
				endp->reg = port_reg;
				if (OF_getproplen(endpoint_child, "slave-mode") >= 0) {
					pdata->in_ports++;
					endp->slave = 1;
				} else {
					pdata->out_ports++;
				}

				mtx_lock(&pdata->mtx_lock);
				TAILQ_INSERT_TAIL(&pdata->endpoints, endp, link);
				mtx_unlock(&pdata->mtx_lock);
			}
		}
	}

	return (0);
}

int
coresight_register(struct coresight_desc *desc)
{
	struct coresight_device *cs_dev;

	cs_dev = malloc(sizeof(struct coresight_device),
	    M_CORESIGHT, M_WAITOK | M_ZERO);
	cs_dev->dev = desc->dev;
	cs_dev->node = ofw_bus_get_node(desc->dev);
	cs_dev->pdata = desc->pdata;
	cs_dev->dev_type = desc->dev_type;

	mtx_lock(&cs_mtx);
	TAILQ_INSERT_TAIL(&cs_devs, cs_dev, link);
	mtx_unlock(&cs_mtx);

	return (0);
}

struct endpoint *
coresight_get_output_endpoint(struct coresight_platform_data *pdata)
{
	struct endpoint *endp;

	if (pdata->out_ports != 1)
		return (NULL);

	TAILQ_FOREACH(endp, &pdata->endpoints, link) {
		if (endp->slave == 0)
			return (endp);
	}

	return (NULL);
}

struct coresight_device *
coresight_get_output_device(struct endpoint *endp, struct endpoint **out_endp)
{
	struct coresight_device *cs_dev;
	struct endpoint *endp2;

	TAILQ_FOREACH(cs_dev, &cs_devs, link) {
		TAILQ_FOREACH(endp2, &cs_dev->pdata->endpoints, link) {
			if (endp->their_node == endp2->my_node) {
				*out_endp = endp2;
				return (cs_dev);
			}
		}
	}

	return (NULL);
}

static int
coresight_get_cpu(phandle_t node,
    struct coresight_platform_data *pdata)
{
	phandle_t cpu_node;
	pcell_t xref;
	pcell_t cpu_reg;

	if (OF_getencprop(node, "cpu", &xref, sizeof(xref)) != -1) {
		cpu_node = OF_node_from_xref(xref);
		if (OF_getencprop(cpu_node, "reg", (void *)&cpu_reg,
			sizeof(cpu_reg)) > 0) {
			pdata->cpu = cpu_reg;
			return (0);
		}
	}

	return (-1);
}

struct coresight_platform_data *
coresight_get_platform_data(device_t dev)
{
	struct coresight_platform_data *pdata;
	phandle_t node;

	node = ofw_bus_get_node(dev);

	pdata = malloc(sizeof(struct coresight_platform_data),
	    M_CORESIGHT, M_WAITOK | M_ZERO);
	mtx_init(&pdata->mtx_lock, "Coresight Platform Data", NULL, MTX_DEF);
	TAILQ_INIT(&pdata->endpoints);

	coresight_get_cpu(node, pdata);
	coresight_get_ports(node, pdata);

	if (bootverbose)
		printf("Total ports: in %d out %d\n",
		    pdata->in_ports, pdata->out_ports);

	return (pdata);
}

static void
coresight_init(void)
{

	mtx_init(&cs_mtx, "ARM Coresight", NULL, MTX_DEF);
	TAILQ_INIT(&cs_devs);
}

SYSINIT(coresight, SI_SUB_DRIVERS, SI_ORDER_FIRST, coresight_init, NULL);
