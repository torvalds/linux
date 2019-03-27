/*-
 * Copyright (c) 2014-2017 Ruslan Bukin <br@bsdpad.com>
 * All rights reserved.
 *
 * This software was developed by SRI International and the University of
 * Cambridge Computer Laboratory under DARPA/AFRL contract (FA8750-10-C-0237)
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

/*
 * SOCFPGA Reset Manager.
 * Chapter 3, Cyclone V Device Handbook (CV-5V2 2014.07.22)
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/malloc.h>
#include <sys/rman.h>
#include <sys/timeet.h>
#include <sys/timetc.h>
#include <sys/sysctl.h>

#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>

#include <machine/bus.h>
#include <machine/fdt.h>
#include <machine/cpu.h>
#include <machine/intr.h>

#include <arm/altera/socfpga/socfpga_common.h>
#include <arm/altera/socfpga/socfpga_rstmgr.h>
#include <arm/altera/socfpga/socfpga_l3regs.h>

struct rstmgr_softc {
	struct resource		*res[1];
	bus_space_tag_t		bst;
	bus_space_handle_t	bsh;
	device_t		dev;
};

struct rstmgr_softc *rstmgr_sc;

static struct resource_spec rstmgr_spec[] = {
	{ SYS_RES_MEMORY,	0,	RF_ACTIVE },
	{ -1, 0 }
};

enum {
	RSTMGR_SYSCTL_FPGA2HPS,
	RSTMGR_SYSCTL_LWHPS2FPGA,
	RSTMGR_SYSCTL_HPS2FPGA
};

static int
l3remap(struct rstmgr_softc *sc, int remap, int enable)
{
	uint32_t paddr;
	bus_addr_t vaddr;
	phandle_t node;
	int reg;

	/*
	 * Control whether bridge is visible to L3 masters or not.
	 * Register is write-only.
	 */

	reg = REMAP_MPUZERO;
	if (enable)
		reg |= (remap);
	else
		reg &= ~(remap);

	node = OF_finddevice("l3regs");
	if (node == -1) {
		device_printf(sc->dev, "Can't find l3regs node\n");
		return (1);
	}

	if ((OF_getencprop(node, "reg", &paddr, sizeof(paddr))) > 0) {
		if (bus_space_map(fdtbus_bs_tag, paddr, 0x4, 0, &vaddr) == 0) {
			bus_space_write_4(fdtbus_bs_tag, vaddr,
			    L3REGS_REMAP, reg);
			return (0);
		}
	}

	return (1);
}

static int
rstmgr_sysctl(SYSCTL_HANDLER_ARGS)
{
	struct rstmgr_softc *sc;
	int enable;
	int remap;
	int err;
	int reg;
	int bit;

	sc = arg1;

	switch (arg2) {
	case RSTMGR_SYSCTL_FPGA2HPS:
		bit = BRGMODRST_FPGA2HPS;
		remap = 0;
		break;
	case RSTMGR_SYSCTL_LWHPS2FPGA:
		bit = BRGMODRST_LWHPS2FPGA;
		remap = REMAP_LWHPS2FPGA;
		break;
	case RSTMGR_SYSCTL_HPS2FPGA:
		bit = BRGMODRST_HPS2FPGA;
		remap = REMAP_HPS2FPGA;
		break;
	default:
		return (1);
	}

	reg = READ4(sc, RSTMGR_BRGMODRST);
	enable = reg & bit ? 0 : 1;

	err = sysctl_handle_int(oidp, &enable, 0, req);
	if (err || !req->newptr)
		return (err);

	if (enable == 1)
		reg &= ~(bit);
	else if (enable == 0)
		reg |= (bit);
	else
		return (EINVAL);

	WRITE4(sc, RSTMGR_BRGMODRST, reg);
	l3remap(sc, remap, enable);

	return (0);
}

int
rstmgr_warmreset(uint32_t reg)
{
	struct rstmgr_softc *sc;

	sc = rstmgr_sc;
	if (sc == NULL)
		return (1);

	/* Request warm reset */
	WRITE4(sc, reg, CTRL_SWWARMRSTREQ);

	return (0);
}

static int
rstmgr_add_sysctl(struct rstmgr_softc *sc)
{
	struct sysctl_oid_list *children;
	struct sysctl_ctx_list *ctx;

	ctx = device_get_sysctl_ctx(sc->dev);
	children = SYSCTL_CHILDREN(device_get_sysctl_tree(sc->dev));

	SYSCTL_ADD_PROC(ctx, children, OID_AUTO, "fpga2hps",
	    CTLTYPE_UINT | CTLFLAG_RW, sc, RSTMGR_SYSCTL_FPGA2HPS,
	    rstmgr_sysctl, "I", "Enable fpga2hps bridge");
	SYSCTL_ADD_PROC(ctx, children, OID_AUTO, "lwhps2fpga",
	    CTLTYPE_UINT | CTLFLAG_RW, sc, RSTMGR_SYSCTL_LWHPS2FPGA,
	    rstmgr_sysctl, "I", "Enable lwhps2fpga bridge");
	SYSCTL_ADD_PROC(ctx, children, OID_AUTO, "hps2fpga",
	    CTLTYPE_UINT | CTLFLAG_RW, sc, RSTMGR_SYSCTL_HPS2FPGA,
	    rstmgr_sysctl, "I", "Enable hps2fpga bridge");

	return (0);
}

static int
rstmgr_probe(device_t dev)
{

	if (!ofw_bus_status_okay(dev))
		return (ENXIO);

	if (!ofw_bus_is_compatible(dev, "altr,rst-mgr"))
		return (ENXIO);

	device_set_desc(dev, "Reset Manager");

	return (BUS_PROBE_DEFAULT);
}

static int
rstmgr_attach(device_t dev)
{
	struct rstmgr_softc *sc;

	sc = device_get_softc(dev);
	sc->dev = dev;

	if (bus_alloc_resources(dev, rstmgr_spec, sc->res)) {
		device_printf(dev, "could not allocate resources\n");
		return (ENXIO);
	}

	/* Memory interface */
	sc->bst = rman_get_bustag(sc->res[0]);
	sc->bsh = rman_get_bushandle(sc->res[0]);

	rstmgr_sc = sc;
	rstmgr_add_sysctl(sc);

	return (0);
}

static device_method_t rstmgr_methods[] = {
	DEVMETHOD(device_probe,		rstmgr_probe),
	DEVMETHOD(device_attach,	rstmgr_attach),
	{ 0, 0 }
};

static driver_t rstmgr_driver = {
	"rstmgr",
	rstmgr_methods,
	sizeof(struct rstmgr_softc),
};

static devclass_t rstmgr_devclass;

DRIVER_MODULE(rstmgr, simplebus, rstmgr_driver, rstmgr_devclass, 0, 0);
