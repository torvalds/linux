/*-
 * Copyright (c) 2016 Stormshield
 * Copyright (c) 2016 Semihalf
 * All rights reserved.
 *
 * Developed by Semihalf.
 *
 * Portions of this software were developed by Semihalf
 * under sponsorship from the FreeBSD Foundation.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of MARVELL nor the names of contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 * Marvell integrated PCI/PCI-Express Bus Controller Driver.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/module.h>
#include <sys/bus.h>
#include <sys/rman.h>

#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>

static int mv_pcib_ctrl_probe(device_t);
static int mv_pcib_ctrl_attach(device_t);
static device_t mv_pcib_ctrl_add_child(device_t, u_int, const char *, int);
static const struct ofw_bus_devinfo * mv_pcib_ctrl_get_devinfo(device_t, device_t);
static struct resource * mv_pcib_ctrl_alloc_resource(device_t, device_t, int,
    int *, rman_res_t, rman_res_t, rman_res_t, u_int);
void mv_pcib_ctrl_init(device_t, phandle_t);
static int mv_pcib_ofw_bus_attach(device_t);

struct mv_pcib_ctrl_range {
	uint64_t bus;
	uint64_t host;
	uint64_t size;
};

typedef int (*get_rl_t)(device_t dev, phandle_t node, pcell_t acells,
    pcell_t scells, struct resource_list *rl);

struct mv_pcib_ctrl_softc {
	pcell_t				addr_cells;
	pcell_t				size_cells;
	int				nranges;
	struct mv_pcib_ctrl_range	*ranges;
};

struct mv_pcib_ctrl_devinfo {
	struct ofw_bus_devinfo	di_dinfo;
	struct resource_list	di_rl;
};

static int mv_pcib_ctrl_fill_ranges(phandle_t, struct mv_pcib_ctrl_softc *);

/*
 * Bus interface definitions
 */
static device_method_t mv_pcib_ctrl_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,			mv_pcib_ctrl_probe),
	DEVMETHOD(device_attach,		mv_pcib_ctrl_attach),

	/* Bus interface */
	DEVMETHOD(bus_add_child,		mv_pcib_ctrl_add_child),
	DEVMETHOD(bus_alloc_resource,		mv_pcib_ctrl_alloc_resource),
	DEVMETHOD(bus_release_resource,		bus_generic_release_resource),
	DEVMETHOD(bus_activate_resource,	bus_generic_activate_resource),
	DEVMETHOD(bus_deactivate_resource,	bus_generic_deactivate_resource),
	DEVMETHOD(bus_setup_intr,		bus_generic_setup_intr),

	/* ofw_bus interface */
	DEVMETHOD(ofw_bus_get_devinfo,		mv_pcib_ctrl_get_devinfo),
	DEVMETHOD(ofw_bus_get_compat,		ofw_bus_gen_get_compat),
	DEVMETHOD(ofw_bus_get_model,		ofw_bus_gen_get_model),
	DEVMETHOD(ofw_bus_get_name,		ofw_bus_gen_get_name),
	DEVMETHOD(ofw_bus_get_node,		ofw_bus_gen_get_node),
	DEVMETHOD(ofw_bus_get_type,		ofw_bus_gen_get_type),

	DEVMETHOD_END
};

static struct ofw_compat_data mv_pcib_ctrl_compat[] = {
	{"mrvl,pcie-ctrl",		(uintptr_t)&ofw_bus_reg_to_rl},
	{"marvell,armada-370-pcie",
	    (uintptr_t)&ofw_bus_assigned_addresses_to_rl},
	{NULL,				(uintptr_t)NULL},
};

static driver_t mv_pcib_ctrl_driver = {
	"pcib_ctrl",
	mv_pcib_ctrl_methods,
	sizeof(struct mv_pcib_ctrl_softc),
};

devclass_t pcib_ctrl_devclass;

DRIVER_MODULE(pcib_ctrl, simplebus, mv_pcib_ctrl_driver, pcib_ctrl_devclass, 0, 0);

MALLOC_DEFINE(M_PCIB_CTRL, "PCIe Bus Controller",
    "Marvell Integrated PCIe Bus Controller");

static int
mv_pcib_ctrl_probe(device_t dev)
{

	if (!ofw_bus_status_okay(dev))
		return (ENXIO);

	if (!ofw_bus_search_compatible(dev, mv_pcib_ctrl_compat)->ocd_data)
		return (ENXIO);

	device_set_desc(dev, "Marvell Integrated PCIe Bus Controller");
	return (BUS_PROBE_DEFAULT);
}

static int
mv_pcib_ctrl_attach(device_t dev)
{
	int err;

	err = mv_pcib_ofw_bus_attach(dev);
	if (err != 0)
		return (err);

	return (bus_generic_attach(dev));
}

static int
mv_pcib_ofw_bus_attach(device_t dev)
{
	struct mv_pcib_ctrl_devinfo *di;
	struct mv_pcib_ctrl_softc *sc;
	device_t child;
	phandle_t parent, node;
	get_rl_t get_rl;

	parent = ofw_bus_get_node(dev);
	sc = device_get_softc(dev);
	if (parent > 0) {
		sc->addr_cells = 1;
		if (OF_getencprop(parent, "#address-cells", &(sc->addr_cells),
		    sizeof(sc->addr_cells)) <= 0)
			return(ENXIO);

		sc->size_cells = 1;
		if (OF_getencprop(parent, "#size-cells", &(sc->size_cells),
		    sizeof(sc->size_cells)) <= 0)
			return(ENXIO);

		for (node = OF_child(parent); node > 0; node = OF_peer(node)) {
			di = malloc(sizeof(*di), M_PCIB_CTRL, M_WAITOK | M_ZERO);
			if (ofw_bus_gen_setup_devinfo(&di->di_dinfo, node)) {
				if (bootverbose) {
					device_printf(dev,
					    "Could not set up devinfo for PCI\n");
				}
				free(di, M_PCIB_CTRL);
				continue;
			}

			child = device_add_child(dev, NULL, -1);
			if (child == NULL) {
				if (bootverbose) {
					device_printf(dev,
					    "Could not add child: %s\n",
					    di->di_dinfo.obd_name);
				}
				ofw_bus_gen_destroy_devinfo(&di->di_dinfo);
				free(di, M_PCIB_CTRL);
				continue;
			}

			resource_list_init(&di->di_rl);
			get_rl = (get_rl_t) ofw_bus_search_compatible(dev,
			    mv_pcib_ctrl_compat)->ocd_data;
			if (get_rl != NULL)
				get_rl(child, node, sc->addr_cells,
				    sc->size_cells, &di->di_rl);

			device_set_ivars(child, di);
		}
	}

	if (mv_pcib_ctrl_fill_ranges(parent, sc) < 0) {
		device_printf(dev, "could not get ranges\n");
		return (ENXIO);
	}

	return (0);
}

static device_t
mv_pcib_ctrl_add_child(device_t dev, u_int order, const char *name, int unit)
{
	device_t cdev;
	struct mv_pcib_ctrl_devinfo *di;

	cdev = device_add_child_ordered(dev, order, name, unit);
	if (cdev == NULL)
		return (NULL);

	di = malloc(sizeof(*di), M_DEVBUF, M_WAITOK | M_ZERO);
	di->di_dinfo.obd_node = -1;
	resource_list_init(&di->di_rl);
	device_set_ivars(cdev, di);

	return (cdev);
}

static struct resource *
mv_pcib_ctrl_alloc_resource(device_t bus, device_t child, int type, int *rid,
    rman_res_t start, rman_res_t end, rman_res_t count, u_int flags)
{
	struct mv_pcib_ctrl_devinfo *di;
	struct resource_list_entry *rle;
	struct mv_pcib_ctrl_softc *sc;
	int i;

	if (RMAN_IS_DEFAULT_RANGE(start, end)) {

		if ((di = device_get_ivars(child)) == NULL)
			return (NULL);
		if (type != SYS_RES_MEMORY)
			return (NULL);

		/* Find defaults for this rid */
		rle = resource_list_find(&di->di_rl, type, *rid);

		if (rle == NULL)
			return (NULL);

		start = rle->start;
		end = rle->end;
		count = rle->count;
	}

	sc = device_get_softc(bus);
	if (type == SYS_RES_MEMORY) {
		/* Remap through ranges property */
		for (i = 0; i < sc->nranges; i++) {
			if (start >= sc->ranges[i].bus && end <
			    sc->ranges[i].bus + sc->ranges[i].size) {
				start -= sc->ranges[i].bus;
				start += sc->ranges[i].host;
				end -= sc->ranges[i].bus;
				end += sc->ranges[i].host;
				break;
			}
		}

		if (i == sc->nranges && sc->nranges != 0) {
			device_printf(bus, "Could not map resource "
			    "%#llx-%#llx\n", start, end);
			return (NULL);
		}
	}

	return (bus_generic_alloc_resource(bus, child, type, rid, start, end,
	    count, flags));
}

static int
mv_pcib_ctrl_fill_ranges(phandle_t node, struct mv_pcib_ctrl_softc *sc)
{
	int host_address_cells;
	cell_t *base_ranges;
	ssize_t nbase_ranges;
	int err;
	int i, j, k;

	err = OF_searchencprop(OF_parent(node), "#address-cells",
	    &host_address_cells, sizeof(host_address_cells));
	if (err <= 0)
		return (-1);

	nbase_ranges = OF_getproplen(node, "ranges");
	if (nbase_ranges < 0)
		return (-1);
	sc->nranges = nbase_ranges / sizeof(cell_t) /
	    (sc->addr_cells + host_address_cells + sc->size_cells);
	if (sc->nranges == 0)
		return (0);

	sc->ranges = malloc(sc->nranges * sizeof(sc->ranges[0]),
	    M_DEVBUF, M_WAITOK);
	base_ranges = malloc(nbase_ranges, M_DEVBUF, M_WAITOK);
	OF_getencprop(node, "ranges", base_ranges, nbase_ranges);

	for (i = 0, j = 0; i < sc->nranges; i++) {
		sc->ranges[i].bus = 0;
		for (k = 0; k < sc->addr_cells; k++) {
			sc->ranges[i].bus <<= 32;
			sc->ranges[i].bus |= base_ranges[j++];
		}
		sc->ranges[i].host = 0;
		for (k = 0; k < host_address_cells; k++) {
			sc->ranges[i].host <<= 32;
			sc->ranges[i].host |= base_ranges[j++];
		}
		sc->ranges[i].size = 0;
		for (k = 0; k < sc->size_cells; k++) {
			sc->ranges[i].size <<= 32;
			sc->ranges[i].size |= base_ranges[j++];
		}
	}

	free(base_ranges, M_DEVBUF);
	return (sc->nranges);
}

static const struct ofw_bus_devinfo *
mv_pcib_ctrl_get_devinfo(device_t bus __unused, device_t child)
{
	struct mv_pcib_ctrl_devinfo *di;

	di = device_get_ivars(child);
	return (&di->di_dinfo);
}
