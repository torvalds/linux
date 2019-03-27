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
#include <sys/bus.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/mutex.h>
#include <sys/rman.h>
#include <machine/bus.h>

#include <dev/iicbus/iiconf.h>
#include <dev/iicbus/iicbus.h>

#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>

#include <dev/extres/regulator/regulator.h>

#include <arm64/rockchip/rk805reg.h>

#include "regdev_if.h"

MALLOC_DEFINE(M_RK805_REG, "RK805 regulator", "RK805 power regulator");

/* #define	dprintf(sc, format, arg...)	device_printf(sc->base_dev, "%s: " format, __func__, arg) */
#define	dprintf(sc, format, arg...)

enum rk_pmic_type {
	RK805 = 1,
	RK808,
};

static struct ofw_compat_data compat_data[] = {
	{"rockchip,rk805", RK805},
	{"rockchip,rk808", RK808},
	{NULL,             0}
};

struct rk805_regdef {
	intptr_t		id;
	char			*name;
	uint8_t			enable_reg;
	uint8_t			enable_mask;
	uint8_t			voltage_reg;
	uint8_t			voltage_mask;
	int			voltage_min;
	int			voltage_max;
	int			voltage_step;
	int			voltage_nstep;
};

struct rk805_reg_sc {
	struct regnode		*regnode;
	device_t		base_dev;
	struct rk805_regdef	*def;
	phandle_t		xref;
	struct regnode_std_param *param;
};

struct rk805_softc {
	device_t		dev;
	struct mtx		mtx;
	struct resource *	res[1];
	void *			intrcookie;
	struct intr_config_hook	intr_hook;
	enum rk_pmic_type	type;

	struct rk805_reg_sc	**regs;
	int			nregs;
};

static struct rk805_regdef rk805_regdefs[] = {
	{
		.id = RK805_DCDC1,
		.name = "DCDC_REG1",
		.enable_reg = RK805_DCDC_EN,
		.enable_mask = 0x11,
		.voltage_reg = RK805_DCDC1_ON_VSEL,
		.voltage_mask = 0x3F,
		.voltage_min = 712500,
		.voltage_max = 1450000,
		.voltage_step = 12500,
		.voltage_nstep = 64,
	},
	{
		.id = RK805_DCDC2,
		.name = "DCDC_REG2",
		.enable_reg = RK805_DCDC_EN,
		.enable_mask = 0x22,
		.voltage_reg = RK805_DCDC2_ON_VSEL,
		.voltage_mask = 0x3F,
		.voltage_min = 712500,
		.voltage_max = 1450000,
		.voltage_step = 12500,
		.voltage_nstep = 64,
	},
	{
		.id = RK805_DCDC3,
		.name = "DCDC_REG3",
		.enable_reg = RK805_DCDC_EN,
		.enable_mask = 0x44,
	},
	{
		.id = RK805_DCDC4,
		.name = "DCDC_REG4",
		.enable_reg = RK805_DCDC_EN,
		.enable_mask = 0x88,
		.voltage_reg = RK805_DCDC4_ON_VSEL,
		.voltage_mask = 0x3F,
		.voltage_min = 800000,
		.voltage_max = 3500000,
		.voltage_step = 100000,
		.voltage_nstep = 28,
	},
	{
		.id = RK805_LDO1,
		.name = "LDO_REG1",
		.enable_reg = RK805_LDO_EN,
		.enable_mask = 0x11,
		.voltage_reg = RK805_LDO1_ON_VSEL,
		.voltage_mask = 0x1F,
		.voltage_min = 800000,
		.voltage_max = 3400000,
		.voltage_step = 100000,
		.voltage_nstep = 27,
	},
	{
		.id = RK805_LDO2,
		.name = "LDO_REG2",
		.enable_reg = RK805_LDO_EN,
		.enable_mask = 0x22,
		.voltage_reg = RK805_LDO2_ON_VSEL,
		.voltage_mask = 0x1F,
		.voltage_min = 800000,
		.voltage_max = 3400000,
		.voltage_step = 100000,
		.voltage_nstep = 27,
	},
	{
		.id = RK805_LDO3,
		.name = "LDO_REG3",
		.enable_reg = RK805_LDO_EN,
		.enable_mask = 0x44,
		.voltage_reg = RK805_LDO3_ON_VSEL,
		.voltage_mask = 0x1F,
		.voltage_min = 800000,
		.voltage_max = 3400000,
		.voltage_step = 100000,
		.voltage_nstep = 27,
	},
};

static struct rk805_regdef rk808_regdefs[] = {
	{
		.id = RK805_DCDC1,
		.name = "DCDC_REG1",
		.enable_reg = RK805_DCDC_EN,
		.enable_mask = 0x1,
		.voltage_reg = RK805_DCDC1_ON_VSEL,
		.voltage_mask = 0x3F,
		.voltage_min = 712500,
		.voltage_max = 1500000,
		.voltage_step = 12500,
		.voltage_nstep = 64,
	},
	{
		.id = RK805_DCDC2,
		.name = "DCDC_REG2",
		.enable_reg = RK805_DCDC_EN,
		.enable_mask = 0x2,
		.voltage_reg = RK805_DCDC2_ON_VSEL,
		.voltage_mask = 0x3F,
		.voltage_min = 712500,
		.voltage_max = 1500000,
		.voltage_step = 12500,
		.voltage_nstep = 64,
	},
	{
		.id = RK805_DCDC3,
		.name = "DCDC_REG3",
		.enable_reg = RK805_DCDC_EN,
		.enable_mask = 0x4,
	},
	{
		.id = RK805_DCDC4,
		.name = "DCDC_REG4",
		.enable_reg = RK805_DCDC_EN,
		.enable_mask = 0x8,
		.voltage_reg = RK805_DCDC4_ON_VSEL,
		.voltage_mask = 0xF,
		.voltage_min = 1800000,
		.voltage_max = 3300000,
		.voltage_step = 100000,
		.voltage_nstep = 16,
	},
};

static int
rk805_read(device_t dev, uint8_t reg, uint8_t *data, uint8_t size)
{

	return (iicdev_readfrom(dev, reg, data, size, IIC_INTRWAIT));
}

static int
rk805_write(device_t dev, uint8_t reg, uint8_t data)
{
	struct iic_msg msg;
	uint8_t buf[2];

	buf[0] = reg;
	buf[1] = data;
	msg.slave = iicbus_get_addr(dev);
	msg.flags = IIC_M_WR;
	msg.buf = buf;
	msg.len = sizeof(buf);

	return (iicbus_transfer_excl(dev, &msg, 1, IIC_INTRWAIT));
}

static int
rk805_regnode_init(struct regnode *regnode)
{
	return (0);
}

static int
rk805_regnode_enable(struct regnode *regnode, bool enable, int *udelay)
{
	struct rk805_reg_sc *sc;
	uint8_t val;

	sc = regnode_get_softc(regnode);

	dprintf(sc, "%sabling regulator %s\n",
	    enable ? "En" : "Dis",
	    sc->def->name);
	rk805_read(sc->base_dev, sc->def->enable_reg, &val, 1);
	if (enable)
		val |= sc->def->enable_mask;
	else
		val &= ~sc->def->enable_mask;
	rk805_write(sc->base_dev, sc->def->enable_reg, val);

	*udelay = 0;

	return (0);
}

static void
rk805_regnode_reg_to_voltage(struct rk805_reg_sc *sc, uint8_t val, int *uv)
{
	if (val < sc->def->voltage_nstep)
		*uv = sc->def->voltage_min + val * sc->def->voltage_step;
	else
		*uv = sc->def->voltage_min +
		       (sc->def->voltage_nstep * sc->def->voltage_step);
}

static int
rk805_regnode_voltage_to_reg(struct rk805_reg_sc *sc, int min_uvolt,
    int max_uvolt, uint8_t *val)
{
	uint8_t nval;
	int nstep, uvolt;

	nval = 0;
	uvolt = sc->def->voltage_min;

	for (nstep = 0; nstep < sc->def->voltage_nstep && uvolt < min_uvolt;
	     nstep++) {
		++nval;
		uvolt += sc->def->voltage_step;
	}
	if (uvolt > max_uvolt)
		return (EINVAL);

	*val = nval;
	return (0);
}

static int
rk805_regnode_set_voltage(struct regnode *regnode, int min_uvolt,
    int max_uvolt, int *udelay)
{
	struct rk805_reg_sc *sc;
	uint8_t val;
	int uvolt;

	sc = regnode_get_softc(regnode);

	if (!sc->def->voltage_step)
		return (ENXIO);

	dprintf(sc, "Setting %s to %d<->%d uvolts\n",
	    sc->def->name,
	    min_uvolt,
	    max_uvolt);
	rk805_read(sc->base_dev, sc->def->voltage_reg, &val, 1);
	if (rk805_regnode_voltage_to_reg(sc, min_uvolt, max_uvolt, &val) != 0)
		return (ERANGE);

	rk805_write(sc->base_dev, sc->def->voltage_reg, val);

	rk805_read(sc->base_dev, sc->def->voltage_reg, &val, 1);

	*udelay = 0;

	rk805_regnode_reg_to_voltage(sc, val, &uvolt);
	dprintf(sc, "Regulator %s set to %d uvolt\n",
	  sc->def->name,
	  uvolt);

	return (0);
}

static int
rk805_regnode_get_voltage(struct regnode *regnode, int *uvolt)
{
	struct rk805_reg_sc *sc;
	uint8_t val;

	sc = regnode_get_softc(regnode);

	if (!sc->def->voltage_step)
		return (ENXIO);

	rk805_read(sc->base_dev, sc->def->voltage_reg, &val, 1);
	rk805_regnode_reg_to_voltage(sc, val & sc->def->voltage_mask, uvolt);

	dprintf(sc, "Regulator %s is at %d uvolt\n",
	  sc->def->name,
	  *uvolt);

	return (0);
}

static regnode_method_t rk805_regnode_methods[] = {
	/* Regulator interface */
	REGNODEMETHOD(regnode_init,		rk805_regnode_init),
	REGNODEMETHOD(regnode_enable,		rk805_regnode_enable),
	REGNODEMETHOD(regnode_set_voltage,	rk805_regnode_set_voltage),
	REGNODEMETHOD(regnode_get_voltage,	rk805_regnode_get_voltage),
	REGNODEMETHOD_END
};
DEFINE_CLASS_1(rk805_regnode, rk805_regnode_class, rk805_regnode_methods,
    sizeof(struct rk805_reg_sc), regnode_class);

static struct rk805_reg_sc *
rk805_reg_attach(device_t dev, phandle_t node,
    struct rk805_regdef *def)
{
	struct rk805_reg_sc *reg_sc;
	struct regnode_init_def initdef;
	struct regnode *regnode;

	memset(&initdef, 0, sizeof(initdef));
	if (regulator_parse_ofw_stdparam(dev, node, &initdef) != 0) {
		device_printf(dev, "cannot create regulator\n");
		return (NULL);
	}
	if (initdef.std_param.min_uvolt == 0)
		initdef.std_param.min_uvolt = def->voltage_min;
	if (initdef.std_param.max_uvolt == 0)
		initdef.std_param.max_uvolt = def->voltage_max;
	initdef.id = def->id;
	initdef.ofw_node = node;

	regnode = regnode_create(dev, &rk805_regnode_class, &initdef);
	if (regnode == NULL) {
		device_printf(dev, "cannot create regulator\n");
		return (NULL);
	}

	reg_sc = regnode_get_softc(regnode);
	reg_sc->regnode = regnode;
	reg_sc->base_dev = dev;
	reg_sc->def = def;
	reg_sc->xref = OF_xref_from_node(node);
	reg_sc->param = regnode_get_stdparam(regnode);

	regnode_register(regnode);

	return (reg_sc);
}

static int
rk805_probe(device_t dev)
{
	if (!ofw_bus_status_okay(dev))
		return (ENXIO);

	if (ofw_bus_search_compatible(dev, compat_data)->ocd_data == 0)
		return (ENXIO);

	device_set_desc(dev, "RockChip RK805 PMIC");
	return (BUS_PROBE_DEFAULT);
}

static void
rk805_start(void *pdev)
{
	struct rk805_softc *sc;
	device_t dev;
	uint8_t data[2];
	int err;

	dev = pdev;
	sc = device_get_softc(dev);
	sc->dev = dev;

	/* No version register in RK808 */
	if (bootverbose && sc->type == RK805) {
		err = rk805_read(dev, RK805_CHIP_NAME, data, 1);
		if (err != 0) {
			device_printf(dev, "Cannot read chip name reg\n");
			return;
		}
		err = rk805_read(dev, RK805_CHIP_VER, data + 1, 1);
		if (err != 0) {
			device_printf(dev, "Cannot read chip version reg\n");
			return;
		}
		device_printf(dev, "Chip Name: %x\n",
		    data[0] << 4 | ((data[1] >> 4) & 0xf));
		device_printf(dev, "Chip Version: %x\n", data[1] & 0xf);
	}

	config_intrhook_disestablish(&sc->intr_hook);
}

static int
rk805_attach(device_t dev)
{
	struct rk805_softc *sc;
	struct rk805_reg_sc *reg;
	struct rk805_regdef *regdefs;
	phandle_t rnode, child;
	int i;

	sc = device_get_softc(dev);

	sc->intr_hook.ich_func = rk805_start;
	sc->intr_hook.ich_arg = dev;

	if (config_intrhook_establish(&sc->intr_hook) != 0)
		return (ENOMEM);

	sc->regs = malloc(sizeof(struct rk805_reg_sc *) * sc->nregs,
	    M_RK805_REG, M_WAITOK | M_ZERO);

	sc->type = ofw_bus_search_compatible(dev, compat_data)->ocd_data;
	switch (sc->type) {
	case RK805:
		regdefs = rk805_regdefs;
		sc->nregs = nitems(rk805_regdefs);
		break;
	case RK808:
		regdefs = rk808_regdefs;
		sc->nregs = nitems(rk808_regdefs);
		break;
	}

	rnode = ofw_bus_find_child(ofw_bus_get_node(dev), "regulators");
	if (rnode > 0) {
		for (i = 0; i < sc->nregs; i++) {
			child = ofw_bus_find_child(rnode,
			    regdefs[i].name);
			if (child == 0)
				continue;
			reg = rk805_reg_attach(dev, child, &regdefs[i]);
			if (reg == NULL) {
				device_printf(dev,
				    "cannot attach regulator %s\n",
				    regdefs[i].name);
				continue;
			}
			sc->regs[i] = reg;
			if (bootverbose)
				device_printf(dev, "Regulator %s attached\n",
				    regdefs[i].name);
		}
	}

	return (0);
}

static int
rk805_detach(device_t dev)
{

	/* We cannot detach regulators */
	return (EBUSY);
}

static int
rk805_map(device_t dev, phandle_t xref, int ncells,
    pcell_t *cells, intptr_t *id)
{
	struct rk805_softc *sc;
	int i;

	sc = device_get_softc(dev);

	for (i = 0; i < sc->nregs; i++) {
		if (sc->regs[i]->xref == xref) {
			*id = sc->regs[i]->def->id;
			return (0);
		}
	}

	return (ERANGE);
}

static device_method_t rk805_methods[] = {
	DEVMETHOD(device_probe,		rk805_probe),
	DEVMETHOD(device_attach,	rk805_attach),
	DEVMETHOD(device_detach,	rk805_detach),

	/* regdev interface */
	DEVMETHOD(regdev_map,		rk805_map),
	DEVMETHOD_END
};

static driver_t rk805_driver = {
	"rk805_pmu",
	rk805_methods,
	sizeof(struct rk805_softc),
};

static devclass_t rk805_devclass;

EARLY_DRIVER_MODULE(rk805, iicbus, rk805_driver, rk805_devclass, 0, 0,
    BUS_PASS_INTERRUPT + BUS_PASS_ORDER_LAST);
MODULE_DEPEND(rk805, iicbus, IICBUS_MINVER, IICBUS_PREFVER, IICBUS_MAXVER);
MODULE_VERSION(rk805, 1);
