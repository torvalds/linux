/*-
 * Copyright (C) 2009 Nathan Whitehorn
 * Copyright (C) 2015 The FreeBSD Foundation
 * All rights reserved.
 *
 * Portions of this software were developed by Andrew Turner
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
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/malloc.h>
#include <sys/bus.h>
#include <sys/cpu.h>
#include <machine/bus.h>

#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>
#include <dev/ofw/ofw_cpu.h>

#ifdef EXT_RESOURCES
#include <dev/extres/clk/clk.h>
#endif

static int	ofw_cpulist_probe(device_t);
static int	ofw_cpulist_attach(device_t);
static const struct ofw_bus_devinfo *ofw_cpulist_get_devinfo(device_t dev,
    device_t child);

static MALLOC_DEFINE(M_OFWCPU, "ofwcpu", "OFW CPU device information");

struct ofw_cpulist_softc {
	pcell_t	 sc_addr_cells;
};

static device_method_t ofw_cpulist_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		ofw_cpulist_probe),
	DEVMETHOD(device_attach,	ofw_cpulist_attach),

	/* Bus interface */
	DEVMETHOD(bus_add_child,	bus_generic_add_child),
	DEVMETHOD(bus_child_pnpinfo_str, ofw_bus_gen_child_pnpinfo_str),

	/* ofw_bus interface */
	DEVMETHOD(ofw_bus_get_devinfo,	ofw_cpulist_get_devinfo),
	DEVMETHOD(ofw_bus_get_compat,	ofw_bus_gen_get_compat),
	DEVMETHOD(ofw_bus_get_model,	ofw_bus_gen_get_model),
	DEVMETHOD(ofw_bus_get_name,	ofw_bus_gen_get_name),
	DEVMETHOD(ofw_bus_get_node,	ofw_bus_gen_get_node),
	DEVMETHOD(ofw_bus_get_type,	ofw_bus_gen_get_type),

	DEVMETHOD_END
};

static driver_t ofw_cpulist_driver = {
	"cpulist",
	ofw_cpulist_methods,
	sizeof(struct ofw_cpulist_softc)
};

static devclass_t ofw_cpulist_devclass;

DRIVER_MODULE(ofw_cpulist, ofwbus, ofw_cpulist_driver, ofw_cpulist_devclass,
    0, 0);

static int 
ofw_cpulist_probe(device_t dev) 
{
	const char	*name;

	name = ofw_bus_get_name(dev);

	if (name == NULL || strcmp(name, "cpus") != 0)
		return (ENXIO);

	device_set_desc(dev, "Open Firmware CPU Group");

	return (0);
}

static int 
ofw_cpulist_attach(device_t dev) 
{
	struct ofw_cpulist_softc *sc;
	phandle_t root, child;
	device_t cdev;
	struct ofw_bus_devinfo *dinfo;

	sc = device_get_softc(dev);
	root = ofw_bus_get_node(dev);

	sc->sc_addr_cells = 1;
	OF_getencprop(root, "#address-cells", &sc->sc_addr_cells,
	    sizeof(sc->sc_addr_cells));

	for (child = OF_child(root); child != 0; child = OF_peer(child)) {
		dinfo = malloc(sizeof(*dinfo), M_OFWCPU, M_WAITOK | M_ZERO);

                if (ofw_bus_gen_setup_devinfo(dinfo, child) != 0) {
                        free(dinfo, M_OFWCPU);
                        continue;
                }
                cdev = device_add_child(dev, NULL, -1);
                if (cdev == NULL) {
                        device_printf(dev, "<%s>: device_add_child failed\n",
                            dinfo->obd_name);
                        ofw_bus_gen_destroy_devinfo(dinfo);
                        free(dinfo, M_OFWCPU);
                        continue;
                }
		device_set_ivars(cdev, dinfo);
	}

	return (bus_generic_attach(dev));
}

static const struct ofw_bus_devinfo *
ofw_cpulist_get_devinfo(device_t dev, device_t child) 
{
	return (device_get_ivars(child));	
}

static int	ofw_cpu_probe(device_t);
static int	ofw_cpu_attach(device_t);
static int	ofw_cpu_read_ivar(device_t dev, device_t child, int index,
    uintptr_t *result);

struct ofw_cpu_softc {
	struct pcpu	*sc_cpu_pcpu;
	uint32_t	 sc_nominal_mhz;
	boolean_t	 sc_reg_valid;
	pcell_t		 sc_reg[2];
};

static device_method_t ofw_cpu_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		ofw_cpu_probe),
	DEVMETHOD(device_attach,	ofw_cpu_attach),

	/* Bus interface */
	DEVMETHOD(bus_add_child,	bus_generic_add_child),
	DEVMETHOD(bus_read_ivar,	ofw_cpu_read_ivar),
	DEVMETHOD(bus_setup_intr,	bus_generic_setup_intr),
	DEVMETHOD(bus_teardown_intr,	bus_generic_teardown_intr),
	DEVMETHOD(bus_alloc_resource,	bus_generic_alloc_resource),
	DEVMETHOD(bus_release_resource,	bus_generic_release_resource),
	DEVMETHOD(bus_activate_resource,bus_generic_activate_resource),

	DEVMETHOD_END
};

static driver_t ofw_cpu_driver = {
	"cpu",
	ofw_cpu_methods,
	sizeof(struct ofw_cpu_softc)
};

static devclass_t ofw_cpu_devclass;

DRIVER_MODULE(ofw_cpu, cpulist, ofw_cpu_driver, ofw_cpu_devclass, 0, 0);

static int
ofw_cpu_probe(device_t dev)
{
	const char *type = ofw_bus_get_type(dev);

	if (type == NULL || strcmp(type, "cpu") != 0)
		return (ENXIO);

	device_set_desc(dev, "Open Firmware CPU");
	return (0);
}

static int
ofw_cpu_attach(device_t dev)
{
	struct ofw_cpulist_softc *psc;
	struct ofw_cpu_softc *sc;
	phandle_t node;
	pcell_t cell;
	int rv;
#ifdef EXT_RESOURCES
	clk_t cpuclk;
	uint64_t freq;
#endif

	sc = device_get_softc(dev);
	psc = device_get_softc(device_get_parent(dev));

	if (nitems(sc->sc_reg) < psc->sc_addr_cells) {
		if (bootverbose)
			device_printf(dev, "Too many address cells\n");
		return (EINVAL);
	}

	node = ofw_bus_get_node(dev);

	/* Read and validate the reg property for use later */
	sc->sc_reg_valid = false;
	rv = OF_getencprop(node, "reg", sc->sc_reg, sizeof(sc->sc_reg));
	if (rv < 0)
		device_printf(dev, "missing 'reg' property\n");
	else if ((rv % 4) != 0) {
		if (bootverbose)
			device_printf(dev, "Malformed reg property\n");
	} else if ((rv / 4) != psc->sc_addr_cells) {
		if (bootverbose)
			device_printf(dev, "Invalid reg size %u\n", rv);
	} else
		sc->sc_reg_valid = true;

#ifdef __powerpc__
	/*
	 * On powerpc, "interrupt-servers" denotes a SMT CPU.  Look for any
	 * thread on this CPU, and assign that.
	 */
	if (OF_hasprop(node, "ibm,ppc-interrupt-server#s")) {
		struct cpuref cpuref;
		cell_t *servers;
		int i, nservers, rv;
		
		if ((nservers = OF_getencprop_alloc(node, 
		    "ibm,ppc-interrupt-server#s", (void **)&servers)) < 0)
			return (ENXIO);
		nservers /= sizeof(cell_t);
		for (i = 0; i < nservers; i++) {
			for (rv = platform_smp_first_cpu(&cpuref); rv == 0;
			    rv = platform_smp_next_cpu(&cpuref)) {
				if (cpuref.cr_hwref == servers[i]) {
					sc->sc_cpu_pcpu =
					    pcpu_find(cpuref.cr_cpuid);
					if (sc->sc_cpu_pcpu == NULL) {
						OF_prop_free(servers);
						return (ENXIO);
					}
					break;
				}
			}
			if (rv != ENOENT)
				break;
		}
		OF_prop_free(servers);
		if (sc->sc_cpu_pcpu == NULL) {
			device_printf(dev, "No CPU found for this device.\n");
			return (ENXIO);
		}
	} else
#endif
	sc->sc_cpu_pcpu = pcpu_find(device_get_unit(dev));

	if (OF_getencprop(node, "clock-frequency", &cell, sizeof(cell)) < 0) {
#ifdef EXT_RESOURCES
		rv = clk_get_by_ofw_index(dev, 0, 0, &cpuclk);
		if (rv == 0) {
			rv = clk_get_freq(cpuclk, &freq);
			if (rv != 0 && bootverbose)
				device_printf(dev,
				    "Cannot get freq of property clocks\n");
			else
				sc->sc_nominal_mhz = freq / 1000000;
		} else
#endif
		{
			if (bootverbose)
				device_printf(dev,
				    "missing 'clock-frequency' property\n");
		}
	} else
		sc->sc_nominal_mhz = cell / 1000000; /* convert to MHz */

	if (sc->sc_nominal_mhz != 0 && bootverbose)
		device_printf(dev, "Nominal frequency %dMhz\n",
		    sc->sc_nominal_mhz);
	bus_generic_probe(dev);
	return (bus_generic_attach(dev));
}

static int
ofw_cpu_read_ivar(device_t dev, device_t child, int index, uintptr_t *result)
{
	struct ofw_cpulist_softc *psc;
	struct ofw_cpu_softc *sc;

	sc = device_get_softc(dev);

	switch (index) {
	case CPU_IVAR_PCPU:
		*result = (uintptr_t)sc->sc_cpu_pcpu;
		return (0);
	case CPU_IVAR_NOMINAL_MHZ:
		if (sc->sc_nominal_mhz > 0) {
			*result = (uintptr_t)sc->sc_nominal_mhz;
			return (0);
		}
		break;
	case CPU_IVAR_CPUID_SIZE:
		psc = device_get_softc(device_get_parent(dev));
		*result = psc->sc_addr_cells;
		return (0);
	case CPU_IVAR_CPUID:
		if (sc->sc_reg_valid) {
			*result = (uintptr_t)sc->sc_reg;
			return (0);
		}
		break;
	}

	return (ENOENT);
}

int
ofw_cpu_early_foreach(ofw_cpu_foreach_cb callback, boolean_t only_runnable)
{
	phandle_t node, child;
	pcell_t addr_cells, reg[2];
	char status[16];
	char device_type[16];
	u_int id, next_id;
	int count, rv;

	count = 0;
	id = 0;
	next_id = 0;

	node = OF_finddevice("/cpus");
	if (node == -1)
		return (-1);

	/* Find the number of cells in the cpu register */
	if (OF_getencprop(node, "#address-cells", &addr_cells,
	    sizeof(addr_cells)) < 0)
		return (-1);

	for (child = OF_child(node); child != 0; child = OF_peer(child),
	    id = next_id) {

		/* Check if child is a CPU */
		memset(device_type, 0, sizeof(device_type));
		rv = OF_getprop(child, "device_type", device_type,
		    sizeof(device_type) - 1);
		if (rv < 0)
			continue;
		if (strcmp(device_type, "cpu") != 0)
			continue;

		/* We're processing CPU, update next_id used in the next iteration */
		next_id++;

		/*
		 * If we are filtering by runnable then limit to only
		 * those that have been enabled, or do provide a method
		 * to enable them.
		 */
		if (only_runnable) {
			status[0] = '\0';
			OF_getprop(child, "status", status, sizeof(status));
			if (status[0] != '\0' && strcmp(status, "okay") != 0 &&
				strcmp(status, "ok") != 0 &&
				!OF_hasprop(child, "enable-method"))
					continue;
		}

		/*
		 * Check we have a register to identify the cpu
		 */
		rv = OF_getencprop(child, "reg", reg,
		    addr_cells * sizeof(cell_t));
		if (rv != addr_cells * sizeof(cell_t))
			continue;

		if (callback == NULL || callback(id, child, addr_cells, reg))
			count++;
	}

	return (only_runnable ? count : id);
}
