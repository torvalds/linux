/*-
 * Copyright (c) 2011-2012 Semihalf.
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

#include "opt_platform.h"
#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/bus.h>
#include <sys/module.h>
#include <sys/smp.h>

#include <machine/bus.h>

#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>
#include <dev/ofw/ofw_subr.h>

#include "qman.h"
#include "portals.h"

#define	FQMAN_DEVSTR	"Freescale Queue Manager"

static int qman_fdt_probe(device_t);

static device_method_t qman_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		qman_fdt_probe),
	DEVMETHOD(device_attach,	qman_attach),
	DEVMETHOD(device_detach,	qman_detach),

	DEVMETHOD(device_suspend,	qman_suspend),
	DEVMETHOD(device_resume,	qman_resume),
	DEVMETHOD(device_shutdown,	qman_shutdown),

	{ 0, 0 }
};

static driver_t qman_driver = {
	"qman",
	qman_methods,
	sizeof(struct qman_softc),
};

static devclass_t qman_devclass;
EARLY_DRIVER_MODULE(qman, simplebus, qman_driver, qman_devclass, 0, 0,
    BUS_PASS_SUPPORTDEV);

static int
qman_fdt_probe(device_t dev)
{

	if (!ofw_bus_is_compatible(dev, "fsl,qman"))
		return (ENXIO);

	device_set_desc(dev, FQMAN_DEVSTR);

	return (BUS_PROBE_DEFAULT);
}

/*
 * QMAN Portals
 */
#define	QMAN_PORT_DEVSTR	"Freescale Queue Manager - Portals"

static device_probe_t qman_portals_fdt_probe;
static device_attach_t qman_portals_fdt_attach;

static device_method_t qm_portals_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		qman_portals_fdt_probe),
	DEVMETHOD(device_attach,	qman_portals_fdt_attach),
	DEVMETHOD(device_detach,	qman_portals_detach),

	{ 0, 0 }
};

static driver_t qm_portals_driver = {
	"qman-portals",
	qm_portals_methods,
	sizeof(struct dpaa_portals_softc),
};

static devclass_t qm_portals_devclass;
EARLY_DRIVER_MODULE(qman_portals, ofwbus, qm_portals_driver,
    qm_portals_devclass, 0, 0, BUS_PASS_BUS);

static void
get_addr_props(phandle_t node, uint32_t *addrp, uint32_t *sizep)
{

	*addrp = 2;
	*sizep = 1;
	OF_getencprop(node, "#address-cells", addrp, sizeof(*addrp));
	OF_getencprop(node, "#size-cells", sizep, sizeof(*sizep));
}

static int
qman_portals_fdt_probe(device_t dev)
{
	phandle_t node;

	if (ofw_bus_is_compatible(dev, "simple-bus")) {
		node = ofw_bus_get_node(dev);
		for (node = OF_child(node); node > 0; node = OF_peer(node)) {
			if (ofw_bus_node_is_compatible(node, "fsl,qman-portal"))
				break;
		}
		if (node <= 0)
			return (ENXIO);
	} else if (!ofw_bus_is_compatible(dev, "fsl,qman-portals"))
		return (ENXIO);

	device_set_desc(dev, QMAN_PORT_DEVSTR);

	return (BUS_PROBE_DEFAULT);
}

static phandle_t
qman_portal_find_cpu(int cpu)
{
	phandle_t node;
	pcell_t reg;

	node = OF_finddevice("/cpus");
	if (node == -1)
		return (-1);

	for (node = OF_child(node); node != 0; node = OF_peer(node)) {
		if (OF_getprop(node, "reg", &reg, sizeof(reg)) <= 0)
			continue;
		if (reg == cpu)
			return (node);
	}
	return (-1);
}

static int
qman_portals_fdt_attach(device_t dev)
{
	struct dpaa_portals_softc *sc;
	phandle_t node, child, cpu_node;
	vm_paddr_t portal_pa, portal_par_pa;
	vm_size_t portal_size;
	uint32_t addr, paddr, size;
	ihandle_t cpu;
	int cpu_num, cpus, intr_rid;
	struct dpaa_portals_devinfo di;
	struct ofw_bus_devinfo ofw_di = {};
	cell_t *range;
	int nrange;
	int i;

	cpus = 0;
	sc = device_get_softc(dev);
	sc->sc_dev = dev;

	node = ofw_bus_get_node(dev);

	/* Get this node's range */
	get_addr_props(ofw_bus_get_node(device_get_parent(dev)), &paddr, &size);
	get_addr_props(node, &addr, &size);

	nrange = OF_getencprop_alloc_multi(node, "ranges",
	    sizeof(*range), (void **)&range);
	if (nrange < addr + paddr + size)
		return (ENXIO);
	portal_pa = portal_par_pa = 0;
	portal_size = 0;
	for (i = 0; i < addr; i++) {
		portal_pa <<= 32;
		portal_pa |= range[i];
	}
	for (; i < paddr + addr; i++) {
		portal_par_pa <<= 32;
		portal_par_pa |= range[i];
	}
	portal_pa += portal_par_pa;
	for (; i < size + paddr + addr; i++) {
		portal_size = (uintmax_t)portal_size << 32;
		portal_size |= range[i];
	}
	OF_prop_free(range);
	sc->sc_dp_size = portal_size;
	sc->sc_dp_pa = portal_pa;

	/* Find portals tied to CPUs */
	for (child = OF_child(node); child != 0; child = OF_peer(child)) {
		if (cpus >= mp_ncpus)
			break;
		if (!ofw_bus_node_is_compatible(child, "fsl,qman-portal")) {
			continue;
		}
		/* Checkout related cpu */
		if (OF_getprop(child, "cpu-handle", (void *)&cpu,
		    sizeof(cpu)) <= 0) {
			cpu = qman_portal_find_cpu(cpus);
			if (cpu <= 0)
				continue;
		}
		/* Acquire cpu number */
		cpu_node = OF_instance_to_package(cpu);
		if (OF_getencprop(cpu_node, "reg", &cpu_num, sizeof(cpu_num)) <= 0) {
			device_printf(dev, "Could not retrieve CPU number.\n");
			return (ENXIO);
		}

		cpus++;

		if (ofw_bus_gen_setup_devinfo(&ofw_di, child) != 0) {
			device_printf(dev, "could not set up devinfo\n");
			continue;
		}

		resource_list_init(&di.di_res);
		if (ofw_bus_reg_to_rl(dev, child, addr, size, &di.di_res)) {
			device_printf(dev, "%s: could not process 'reg' "
			    "property\n", ofw_di.obd_name);
			ofw_bus_gen_destroy_devinfo(&ofw_di);
			continue;
		}
		if (ofw_bus_intr_to_rl(dev, child, &di.di_res, &intr_rid)) {
			device_printf(dev, "%s: could not process "
			    "'interrupts' property\n", ofw_di.obd_name);
			resource_list_free(&di.di_res);
			ofw_bus_gen_destroy_devinfo(&ofw_di);
			continue;
		}
		di.di_intr_rid = intr_rid;

		if (dpaa_portal_alloc_res(dev, &di, cpu_num))
			goto err;
	}

	ofw_bus_gen_destroy_devinfo(&ofw_di);

	return (qman_portals_attach(dev));
err:
	resource_list_free(&di.di_res);
	ofw_bus_gen_destroy_devinfo(&ofw_di);
	qman_portals_detach(dev);
	return (ENXIO);
}
