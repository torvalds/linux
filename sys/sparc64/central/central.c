/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2003 Jake Burkholder.
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
#include <sys/malloc.h>
#include <sys/module.h>

#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>
#include <dev/ofw/openfirm.h>

#include <machine/bus.h>
#include <machine/resource.h>

#include <sys/rman.h>

#include <sparc64/sbus/ofw_sbus.h>

struct central_devinfo {
	struct ofw_bus_devinfo	cdi_obdinfo;
	struct resource_list	cdi_rl;
};

struct central_softc {
	int			sc_nrange;
	struct sbus_ranges	*sc_ranges;
};

static device_probe_t central_probe;
static device_attach_t central_attach;
static bus_print_child_t central_print_child;
static bus_probe_nomatch_t central_probe_nomatch;
static bus_alloc_resource_t central_alloc_resource;
static bus_adjust_resource_t central_adjust_resource;
static bus_get_resource_list_t central_get_resource_list;
static ofw_bus_get_devinfo_t central_get_devinfo;

static int central_print_res(struct central_devinfo *);

static device_method_t central_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		central_probe),
	DEVMETHOD(device_attach,	central_attach),
	DEVMETHOD(device_shutdown,	bus_generic_shutdown),
	DEVMETHOD(device_suspend,	bus_generic_suspend),
	DEVMETHOD(device_resume,	bus_generic_resume),

	/* Bus interface */
	DEVMETHOD(bus_print_child,	central_print_child),
	DEVMETHOD(bus_probe_nomatch,	central_probe_nomatch),
	DEVMETHOD(bus_alloc_resource,	central_alloc_resource),
	DEVMETHOD(bus_activate_resource, bus_generic_activate_resource),
	DEVMETHOD(bus_deactivate_resource, bus_generic_deactivate_resource),
	DEVMETHOD(bus_adjust_resource,	central_adjust_resource),
	DEVMETHOD(bus_release_resource,	bus_generic_rl_release_resource),
	DEVMETHOD(bus_setup_intr,	bus_generic_setup_intr),
	DEVMETHOD(bus_teardown_intr,	bus_generic_teardown_intr),
	DEVMETHOD(bus_get_resource,	bus_generic_rl_get_resource),
	DEVMETHOD(bus_get_resource_list, central_get_resource_list),
	DEVMETHOD(bus_child_pnpinfo_str, ofw_bus_gen_child_pnpinfo_str),

	/* ofw_bus interface */
	DEVMETHOD(ofw_bus_get_devinfo,	central_get_devinfo),
	DEVMETHOD(ofw_bus_get_compat,	ofw_bus_gen_get_compat),
	DEVMETHOD(ofw_bus_get_model,	ofw_bus_gen_get_model),
	DEVMETHOD(ofw_bus_get_name,	ofw_bus_gen_get_name),
	DEVMETHOD(ofw_bus_get_node,	ofw_bus_gen_get_node),
	DEVMETHOD(ofw_bus_get_type,	ofw_bus_gen_get_type),

	DEVMETHOD_END
};

static driver_t central_driver = {
	"central",
	central_methods,
	sizeof(struct central_softc),
};

static devclass_t central_devclass;

EARLY_DRIVER_MODULE(central, nexus, central_driver, central_devclass, 0, 0,
    BUS_PASS_BUS);
MODULE_DEPEND(fhc, nexus, 1, 1, 1);
MODULE_VERSION(central, 1);

static int
central_probe(device_t dev)
{

	if (strcmp(ofw_bus_get_name(dev), "central") == 0) {
		device_set_desc(dev, "central");
		return (0);
	}
	return (ENXIO);
}

static int
central_attach(device_t dev)
{
	struct central_devinfo *cdi;
	struct sbus_regs *reg;
	struct central_softc *sc;
	phandle_t child;
	phandle_t node;
	device_t cdev;
	int nreg;
	int i;

	sc = device_get_softc(dev);
	node = ofw_bus_get_node(dev);

	sc->sc_nrange = OF_getprop_alloc_multi(node, "ranges",
	    sizeof(*sc->sc_ranges), (void **)&sc->sc_ranges);
	if (sc->sc_nrange == -1) {
		device_printf(dev, "can't get ranges\n");
		return (ENXIO);
	}

	for (child = OF_child(node); child != 0; child = OF_peer(child)) {
		cdi = malloc(sizeof(*cdi), M_DEVBUF, M_WAITOK | M_ZERO);
		if (ofw_bus_gen_setup_devinfo(&cdi->cdi_obdinfo, child) != 0) {
			free(cdi, M_DEVBUF);
			continue;
		}
		nreg = OF_getprop_alloc_multi(child, "reg", sizeof(*reg),
		    (void **)&reg);
		if (nreg == -1) {
			device_printf(dev, "<%s>: incomplete\n",
			    cdi->cdi_obdinfo.obd_name);
			ofw_bus_gen_destroy_devinfo(&cdi->cdi_obdinfo);
			free(cdi, M_DEVBUF);
			continue;
		}
		resource_list_init(&cdi->cdi_rl);
		for (i = 0; i < nreg; i++)
			resource_list_add(&cdi->cdi_rl, SYS_RES_MEMORY, i,
			    reg[i].sbr_offset, reg[i].sbr_offset +
			    reg[i].sbr_size, reg[i].sbr_size);
		OF_prop_free(reg);
		cdev = device_add_child(dev, NULL, -1);
		if (cdev == NULL) {
			device_printf(dev, "<%s>: device_add_child failed\n",
			    cdi->cdi_obdinfo.obd_name);
			resource_list_free(&cdi->cdi_rl);
			ofw_bus_gen_destroy_devinfo(&cdi->cdi_obdinfo);
			free(cdi, M_DEVBUF);
			continue;
		}
		device_set_ivars(cdev, cdi);
	}

	return (bus_generic_attach(dev));
}

static int
central_adjust_resource(device_t bus __unused, device_t child __unused,
    int type __unused, struct resource *r __unused, rman_res_t start __unused,
    rman_res_t end __unused)
{

	return (ENXIO);
}

static int
central_print_child(device_t dev, device_t child)
{
	int rv;

	rv = bus_print_child_header(dev, child);
	rv += central_print_res(device_get_ivars(child));
	rv += bus_print_child_footer(dev, child);
	return (rv);
}

static void
central_probe_nomatch(device_t dev, device_t child)
{
	const char *type;

	device_printf(dev, "<%s>", ofw_bus_get_name(child));
	central_print_res(device_get_ivars(child));
	type = ofw_bus_get_type(child);
	printf(" type %s (no driver attached)\n",
	    type != NULL ? type : "unknown");
}

static struct resource *
central_alloc_resource(device_t bus, device_t child, int type, int *rid,
    rman_res_t start, rman_res_t end, rman_res_t count, u_int flags)
{
	struct resource_list *rl;
	struct resource_list_entry *rle;
	struct central_softc *sc;
	struct resource *res;
	bus_addr_t coffset;
	bus_addr_t cend;
	bus_addr_t phys;
	int isdefault;
	int passthrough;
	int i;

	isdefault = RMAN_IS_DEFAULT_RANGE(start, end);
	passthrough = (device_get_parent(child) != bus);
	res = NULL;
	rle = NULL;
	rl = BUS_GET_RESOURCE_LIST(bus, child);
	sc = device_get_softc(bus);
	switch (type) {
	case SYS_RES_IRQ:
		return (resource_list_alloc(rl, bus, child, type, rid, start,
		    end, count, flags));
	case SYS_RES_MEMORY:
		if (!passthrough) {
			rle = resource_list_find(rl, type, *rid);
			if (rle == NULL)
				return (NULL);
			if (rle->res != NULL)
				panic("%s: resource entry is busy", __func__);
			if (isdefault) {
				start = rle->start;
				count = ulmax(count, rle->count);
				end = ulmax(rle->end, start + count - 1);
			}
		}
		for (i = 0; i < sc->sc_nrange; i++) {
			coffset = sc->sc_ranges[i].coffset;
			cend = coffset + sc->sc_ranges[i].size - 1;
			if (start >= coffset && end <= cend) {
				start -= coffset;
				end -= coffset;
				phys = sc->sc_ranges[i].poffset |
				    ((bus_addr_t)sc->sc_ranges[i].pspace << 32);
				res = bus_generic_alloc_resource(bus, child,
				    type, rid, phys + start, phys + end,
				    count, flags);
				if (!passthrough)
					rle->res = res;
				break;
			}
		}
		break;
	}
	return (res);
}

static struct resource_list *
central_get_resource_list(device_t bus, device_t child)
{
	struct central_devinfo *cdi;

	cdi = device_get_ivars(child);
	return (&cdi->cdi_rl);
}

static const struct ofw_bus_devinfo *
central_get_devinfo(device_t bus, device_t child)
{
	struct central_devinfo *cdi;

	cdi = device_get_ivars(child);
	return (&cdi->cdi_obdinfo);
}

static int
central_print_res(struct central_devinfo *cdi)
{

	return (resource_list_print_type(&cdi->cdi_rl, "mem", SYS_RES_MEMORY,
	    "%#jx"));
}
