/*-
 * Copyright (c) 2016 Michal Meloun <mmel@FreeBSD.org>
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
#include <sys/cpu.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/module.h>
#include <sys/sysctl.h>

#include <machine/bus.h>
#include <machine/cpu.h>

#include <dev/extres/clk/clk.h>
#include <dev/ofw/ofw_bus_subr.h>

#include "tegra_soctherm_if.h"


enum therm_info {
	CORETEMP_TEMP,
	CORETEMP_DELTA,
	CORETEMP_RESOLUTION,
	CORETEMP_TJMAX,
};

struct tegra124_coretemp_softc {
	device_t		dev;
	int			overheat_log;
	int			core_max_temp;
	int			cpu_id;
	device_t		tsens_dev;
	intptr_t		tsens_id;
};

static int
coretemp_get_val_sysctl(SYSCTL_HANDLER_ARGS)
{
	device_t dev;
	int val, temp, rv;
	struct tegra124_coretemp_softc *sc;
	enum therm_info type;
	char stemp[16];


	dev = (device_t) arg1;
	sc = device_get_softc(dev);
	type = arg2;


	rv = TEGRA_SOCTHERM_GET_TEMPERATURE(sc->tsens_dev, sc->dev,
	     sc->tsens_id, &temp);
	if (rv != 0) {
		device_printf(sc->dev,
		    "Cannot read temperature sensor %d:  %d\n",
		    sc->tsens_id, rv);
		return (rv);
	}

	switch (type) {
	case CORETEMP_TEMP:
		val = temp / 100;
		val +=  2731;
		break;
	case CORETEMP_DELTA:
		val = (sc->core_max_temp - temp) / 1000;
		break;
	case CORETEMP_RESOLUTION:
		val = 1;
		break;
	case CORETEMP_TJMAX:
		val = sc->core_max_temp / 100;
		val +=  2731;
		break;
	}


	if ((temp > sc->core_max_temp)  && !sc->overheat_log) {
		sc->overheat_log = 1;

		/*
		 * Check for Critical Temperature Status and Critical
		 * Temperature Log.  It doesn't really matter if the
		 * current temperature is invalid because the "Critical
		 * Temperature Log" bit will tell us if the Critical
		 * Temperature has * been reached in past. It's not
		 * directly related to the current temperature.
		 *
		 * If we reach a critical level, allow devctl(4)
		 * to catch this and shutdown the system.
		 */
		device_printf(dev, "critical temperature detected, "
		    "suggest system shutdown\n");
		snprintf(stemp, sizeof(stemp), "%d", val);
		devctl_notify("coretemp", "Thermal", stemp,
		    "notify=0xcc");
	} else {
		sc->overheat_log = 0;
	}

	return (sysctl_handle_int(oidp, 0, val, req));
}

static int
tegra124_coretemp_ofw_parse(struct tegra124_coretemp_softc *sc)
{
	int rv, ncells;
	phandle_t node, xnode;
	pcell_t *cells;

	node = OF_peer(0);
	node = ofw_bus_find_child(node, "thermal-zones");
	if (node <= 0) {
		device_printf(sc->dev, "Cannot find 'thermal-zones'.\n");
		return (ENXIO);
	}

	node = ofw_bus_find_child(node, "cpu");
	if (node <= 0) {
		device_printf(sc->dev, "Cannot find 'cpu'\n");
		return (ENXIO);
	}
	rv = ofw_bus_parse_xref_list_alloc(node, "thermal-sensors",
	    "#thermal-sensor-cells", 0, &xnode, &ncells, &cells);
	if (rv != 0) {
		device_printf(sc->dev,
		    "Cannot parse 'thermal-sensors' property.\n");
		return (ENXIO);
	}
	if (ncells != 1) {
		device_printf(sc->dev,
		    "Invalid format of 'thermal-sensors' property(%d).\n",
		    ncells);
		return (ENXIO);
	}

	sc->tsens_id = 0x100 + sc->cpu_id; //cells[0];
	OF_prop_free(cells);

	sc->tsens_dev = OF_device_from_xref(xnode);
	if (sc->tsens_dev == NULL) {
		device_printf(sc->dev,
		    "Cannot find thermal sensors device.");
		return (ENXIO);
	}
	return (0);
}

static void
tegra124_coretemp_identify(driver_t *driver, device_t parent)
{
	phandle_t root;

	root = OF_finddevice("/");
	if (!ofw_bus_node_is_compatible(root, "nvidia,tegra124"))
		return;
	if (device_find_child(parent, "tegra124_coretemp", -1) != NULL)
		return;
	if (BUS_ADD_CHILD(parent, 0, "tegra124_coretemp", -1) == NULL)
		device_printf(parent, "add child failed\n");
}

static int
tegra124_coretemp_probe(device_t dev)
{

	device_set_desc(dev, "CPU Thermal Sensor");
	return (0);
}

static int
tegra124_coretemp_attach(device_t dev)
{
	struct tegra124_coretemp_softc *sc;
	device_t pdev;
	struct sysctl_oid *oid;
	struct sysctl_ctx_list *ctx;
	int rv;

	sc = device_get_softc(dev);
	sc->dev = dev;
	sc->cpu_id = device_get_unit(dev);
	sc->core_max_temp = 102000;
	pdev = device_get_parent(dev);

	rv = tegra124_coretemp_ofw_parse(sc);
	if (rv != 0)
		return (rv);

	ctx = device_get_sysctl_ctx(dev);

	oid = SYSCTL_ADD_NODE(ctx,
	    SYSCTL_CHILDREN(device_get_sysctl_tree(pdev)), OID_AUTO,
	    "coretemp", CTLFLAG_RD, NULL, "Per-CPU thermal information");

	/*
	 * Add the MIBs to dev.cpu.N and dev.cpu.N.coretemp.
	 */
	SYSCTL_ADD_PROC(ctx, SYSCTL_CHILDREN(device_get_sysctl_tree(pdev)),
	    OID_AUTO, "temperature", CTLTYPE_INT | CTLFLAG_RD | CTLFLAG_MPSAFE,
	    dev, CORETEMP_TEMP, coretemp_get_val_sysctl, "IK",
	    "Current temperature");
	SYSCTL_ADD_PROC(ctx, SYSCTL_CHILDREN(oid), OID_AUTO, "delta",
	    CTLTYPE_INT | CTLFLAG_RD | CTLFLAG_MPSAFE, dev, CORETEMP_DELTA,
	    coretemp_get_val_sysctl, "I",
	    "Delta between TCC activation and current temperature");
	SYSCTL_ADD_PROC(ctx, SYSCTL_CHILDREN(oid), OID_AUTO, "resolution",
	    CTLTYPE_INT | CTLFLAG_RD | CTLFLAG_MPSAFE, dev, CORETEMP_RESOLUTION,
	    coretemp_get_val_sysctl, "I",
	    "Resolution of CPU thermal sensor");
	SYSCTL_ADD_PROC(ctx, SYSCTL_CHILDREN(oid), OID_AUTO, "tjmax",
	    CTLTYPE_INT | CTLFLAG_RD | CTLFLAG_MPSAFE, dev, CORETEMP_TJMAX,
	    coretemp_get_val_sysctl, "IK",
	    "TCC activation temperature");

	return (0);
}

static int
tegra124_coretemp_detach(device_t dev)
{
	struct tegra124_coretemp_softc *sc;

	sc = device_get_softc(dev);
	return (0);
}

static device_method_t tegra124_coretemp_methods[] = {
	/* Device interface */
	DEVMETHOD(device_identify,	tegra124_coretemp_identify),
	DEVMETHOD(device_probe,		tegra124_coretemp_probe),
	DEVMETHOD(device_attach,	tegra124_coretemp_attach),
	DEVMETHOD(device_detach,	tegra124_coretemp_detach),


	DEVMETHOD_END
};

static devclass_t tegra124_coretemp_devclass;
static DEFINE_CLASS_0(tegra124_coretemp, tegra124_coretemp_driver,
    tegra124_coretemp_methods, sizeof(struct tegra124_coretemp_softc));
DRIVER_MODULE(tegra124_coretemp, cpu, tegra124_coretemp_driver,
    tegra124_coretemp_devclass, NULL, NULL);
