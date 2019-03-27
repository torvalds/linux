/*-
 * Copyright 2016 Michal Meloun <mmel@FreeBSD.org>
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
#include <sys/gpio.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/malloc.h>
#include <sys/rman.h>
#include <sys/sx.h>

#include <machine/bus.h>

#include <dev/extres/regulator/regulator.h>
#include <dev/gpio/gpiobusvar.h>

#include <gnu/dts/include/dt-bindings/mfd/as3722.h>

#include "as3722.h"

MALLOC_DEFINE(M_AS3722_REG, "AS3722 regulator", "AS3722 power regulator");

#define	DIV_ROUND_UP(n,d) howmany(n, d)

enum as3722_reg_id {
	AS3722_REG_ID_SD0,
	AS3722_REG_ID_SD1,
	AS3722_REG_ID_SD2,
	AS3722_REG_ID_SD3,
	AS3722_REG_ID_SD4,
	AS3722_REG_ID_SD5,
	AS3722_REG_ID_SD6,
	AS3722_REG_ID_LDO0,
	AS3722_REG_ID_LDO1,
	AS3722_REG_ID_LDO2,
	AS3722_REG_ID_LDO3,
	AS3722_REG_ID_LDO4,
	AS3722_REG_ID_LDO5,
	AS3722_REG_ID_LDO6,
	AS3722_REG_ID_LDO7,
	AS3722_REG_ID_LDO9,
	AS3722_REG_ID_LDO10,
	AS3722_REG_ID_LDO11,
};


/* Regulator HW definition. */
struct reg_def {
	intptr_t		id;		/* ID */
	char			*name;		/* Regulator name */
	char			*supply_name;	/* Source property name */
	uint8_t			volt_reg;
	uint8_t			volt_vsel_mask;
	uint8_t			enable_reg;
	uint8_t			enable_mask;
	uint8_t			ext_enable_reg;
	uint8_t			ext_enable_mask;
	struct regulator_range	*ranges;
	int			nranges;
};

struct as3722_reg_sc {
	struct regnode		*regnode;
	struct as3722_softc	*base_sc;
	struct reg_def		*def;
	phandle_t		xref;

	struct regnode_std_param *param;
	int 			ext_control;
	int	 		enable_tracking;

	int			enable_usec;
};

static struct regulator_range as3722_sd016_ranges[] = {
	REG_RANGE_INIT(0x00, 0x00,       0,     0),
	REG_RANGE_INIT(0x01, 0x5A,  610000, 10000),
};

static struct regulator_range as3722_sd0_lv_ranges[] = {
	REG_RANGE_INIT(0x00, 0x00,       0,     0),
	REG_RANGE_INIT(0x01, 0x6E,  410000, 10000),
};

static struct regulator_range as3722_sd_ranges[] = {
	REG_RANGE_INIT(0x00, 0x00,       0,     0),
	REG_RANGE_INIT(0x01, 0x40,  612500, 12500),
	REG_RANGE_INIT(0x41, 0x70, 1425000, 25000),
	REG_RANGE_INIT(0x71, 0x7F, 2650000, 50000),
};

static struct regulator_range as3722_ldo3_ranges[] = {
	REG_RANGE_INIT(0x00, 0x00,       0,     0),
	REG_RANGE_INIT(0x01, 0x2D,  620000, 20000),
};

static struct regulator_range as3722_ldo_ranges[] = {
	REG_RANGE_INIT(0x00, 0x00,       0,     0),
	REG_RANGE_INIT(0x01, 0x24,  825000, 25000),
	REG_RANGE_INIT(0x40, 0x7F, 1725000, 25000),
};

static struct reg_def as3722s_def[] = {
	{
		.id = AS3722_REG_ID_SD0,
		.name = "sd0",
		.volt_reg = AS3722_SD0_VOLTAGE,
		.volt_vsel_mask = AS3722_SD_VSEL_MASK,
		.enable_reg = AS3722_SD_CONTROL,
		.enable_mask = AS3722_SDN_CTRL(0),
		.ext_enable_reg = AS3722_ENABLE_CTRL1,
		.ext_enable_mask = AS3722_SD0_EXT_ENABLE_MASK,
		.ranges = as3722_sd016_ranges,
		.nranges = nitems(as3722_sd016_ranges),
	},
	{
		.id = AS3722_REG_ID_SD1,
		.name = "sd1",
		.volt_reg = AS3722_SD1_VOLTAGE,
		.volt_vsel_mask = AS3722_SD_VSEL_MASK,
		.enable_reg = AS3722_SD_CONTROL,
		.enable_mask = AS3722_SDN_CTRL(1),
		.ext_enable_reg = AS3722_ENABLE_CTRL1,
		.ext_enable_mask = AS3722_SD1_EXT_ENABLE_MASK,
		.ranges = as3722_sd_ranges,
		.nranges = nitems(as3722_sd_ranges),
	},
	{
		.id = AS3722_REG_ID_SD2,
		.name = "sd2",
		.supply_name = "vsup-sd2",
		.volt_reg = AS3722_SD2_VOLTAGE,
		.volt_vsel_mask = AS3722_SD_VSEL_MASK,
		.enable_reg = AS3722_SD_CONTROL,
		.enable_mask = AS3722_SDN_CTRL(2),
		.ext_enable_reg = AS3722_ENABLE_CTRL1,
		.ext_enable_mask = AS3722_SD2_EXT_ENABLE_MASK,
		.ranges = as3722_sd_ranges,
		.nranges = nitems(as3722_sd_ranges),
	},
	{
		.id = AS3722_REG_ID_SD3,
		.name = "sd3",
		.supply_name = "vsup-sd3",
		.volt_reg = AS3722_SD3_VOLTAGE,
		.volt_vsel_mask = AS3722_SD_VSEL_MASK,
		.enable_reg = AS3722_SD_CONTROL,
		.enable_mask = AS3722_SDN_CTRL(3),
		.ext_enable_reg = AS3722_ENABLE_CTRL1,
		.ext_enable_mask = AS3722_SD3_EXT_ENABLE_MASK,
		.ranges = as3722_sd_ranges,
		.nranges = nitems(as3722_sd_ranges),
	},
	{
		.id = AS3722_REG_ID_SD4,
		.name = "sd4",
		.supply_name = "vsup-sd4",
		.volt_reg = AS3722_SD4_VOLTAGE,
		.volt_vsel_mask = AS3722_SD_VSEL_MASK,
		.enable_reg = AS3722_SD_CONTROL,
		.enable_mask = AS3722_SDN_CTRL(4),
		.ext_enable_reg = AS3722_ENABLE_CTRL2,
		.ext_enable_mask = AS3722_SD4_EXT_ENABLE_MASK,
		.ranges = as3722_sd_ranges,
		.nranges = nitems(as3722_sd_ranges),
	},
	{
		.id = AS3722_REG_ID_SD5,
		.name = "sd5",
		.supply_name = "vsup-sd5",
		.volt_reg = AS3722_SD5_VOLTAGE,
		.volt_vsel_mask = AS3722_SD_VSEL_MASK,
		.enable_reg = AS3722_SD_CONTROL,
		.enable_mask = AS3722_SDN_CTRL(5),
		.ext_enable_reg = AS3722_ENABLE_CTRL2,
		.ext_enable_mask = AS3722_SD5_EXT_ENABLE_MASK,
		.ranges = as3722_sd_ranges,
		.nranges = nitems(as3722_sd_ranges),
	},
	{
		.id = AS3722_REG_ID_SD6,
		.name = "sd6",
		.volt_reg = AS3722_SD6_VOLTAGE,
		.volt_vsel_mask = AS3722_SD_VSEL_MASK,
		.enable_reg = AS3722_SD_CONTROL,
		.enable_mask = AS3722_SDN_CTRL(6),
		.ext_enable_reg = AS3722_ENABLE_CTRL2,
		.ext_enable_mask = AS3722_SD6_EXT_ENABLE_MASK,
		.ranges = as3722_sd016_ranges,
		.nranges = nitems(as3722_sd016_ranges),
	},
	{
		.id = AS3722_REG_ID_LDO0,
		.name = "ldo0",
		.supply_name = "vin-ldo0",
		.volt_reg = AS3722_LDO0_VOLTAGE,
		.volt_vsel_mask = AS3722_LDO0_VSEL_MASK,
		.enable_reg = AS3722_LDO_CONTROL0,
		.enable_mask = AS3722_LDO0_CTRL,
		.ext_enable_reg = AS3722_ENABLE_CTRL3,
		.ext_enable_mask = AS3722_LDO0_EXT_ENABLE_MASK,
		.ranges = as3722_ldo_ranges,
		.nranges = nitems(as3722_ldo_ranges),
	},
	{
		.id = AS3722_REG_ID_LDO1,
		.name = "ldo1",
		.supply_name = "vin-ldo1-6",
		.volt_reg = AS3722_LDO1_VOLTAGE,
		.volt_vsel_mask = AS3722_LDO_VSEL_MASK,
		.enable_reg = AS3722_LDO_CONTROL0,
		.enable_mask = AS3722_LDO1_CTRL,
		.ext_enable_reg = AS3722_ENABLE_CTRL3,
		.ext_enable_mask = AS3722_LDO1_EXT_ENABLE_MASK,
		.ranges = as3722_ldo_ranges,
		.nranges = nitems(as3722_ldo_ranges),
	},
	{
		.id = AS3722_REG_ID_LDO2,
		.name = "ldo2",
		.supply_name = "vin-ldo2-5-7",
		.volt_reg = AS3722_LDO2_VOLTAGE,
		.volt_vsel_mask = AS3722_LDO_VSEL_MASK,
		.enable_reg = AS3722_LDO_CONTROL0,
		.enable_mask = AS3722_LDO2_CTRL,
		.ext_enable_reg = AS3722_ENABLE_CTRL3,
		.ext_enable_mask = AS3722_LDO2_EXT_ENABLE_MASK,
		.ranges = as3722_ldo_ranges,
		.nranges = nitems(as3722_ldo_ranges),
	},
	{
		.id = AS3722_REG_ID_LDO3,
		.name = "ldo3",
		.supply_name = "vin-ldo3-4",
		.volt_reg = AS3722_LDO3_VOLTAGE,
		.volt_vsel_mask = AS3722_LDO3_VSEL_MASK,
		.enable_reg = AS3722_LDO_CONTROL0,
		.enable_mask = AS3722_LDO3_CTRL,
		.ext_enable_reg = AS3722_ENABLE_CTRL3,
		.ext_enable_mask = AS3722_LDO3_EXT_ENABLE_MASK,
		.ranges = as3722_ldo3_ranges,
		.nranges = nitems(as3722_ldo3_ranges),
	},
	{
		.id = AS3722_REG_ID_LDO4,
		.name = "ldo4",
		.supply_name = "vin-ldo3-4",
		.volt_reg = AS3722_LDO4_VOLTAGE,
		.volt_vsel_mask = AS3722_LDO_VSEL_MASK,
		.enable_reg = AS3722_LDO_CONTROL0,
		.enable_mask = AS3722_LDO4_CTRL,
		.ext_enable_reg = AS3722_ENABLE_CTRL4,
		.ext_enable_mask = AS3722_LDO4_EXT_ENABLE_MASK,
		.ranges = as3722_ldo_ranges,
		.nranges = nitems(as3722_ldo_ranges),
	},
	{
		.id = AS3722_REG_ID_LDO5,
		.name = "ldo5",
		.supply_name = "vin-ldo2-5-7",
		.volt_reg = AS3722_LDO5_VOLTAGE,
		.volt_vsel_mask = AS3722_LDO_VSEL_MASK,
		.enable_reg = AS3722_LDO_CONTROL0,
		.enable_mask = AS3722_LDO5_CTRL,
		.ext_enable_reg = AS3722_ENABLE_CTRL4,
		.ext_enable_mask = AS3722_LDO5_EXT_ENABLE_MASK,
		.ranges = as3722_ldo_ranges,
		.nranges = nitems(as3722_ldo_ranges),
	},
	{
		.id = AS3722_REG_ID_LDO6,
		.name = "ldo6",
		.supply_name = "vin-ldo1-6",
		.volt_reg = AS3722_LDO6_VOLTAGE,
		.volt_vsel_mask = AS3722_LDO_VSEL_MASK,
		.enable_reg = AS3722_LDO_CONTROL0,
		.enable_mask = AS3722_LDO6_CTRL,
		.ext_enable_reg = AS3722_ENABLE_CTRL4,
		.ext_enable_mask = AS3722_LDO6_EXT_ENABLE_MASK,
		.ranges = as3722_ldo_ranges,
		.nranges = nitems(as3722_ldo_ranges),
	},
	{
		.id = AS3722_REG_ID_LDO7,
		.name = "ldo7",
		.supply_name = "vin-ldo2-5-7",
		.volt_reg = AS3722_LDO7_VOLTAGE,
		.volt_vsel_mask = AS3722_LDO_VSEL_MASK,
		.enable_reg = AS3722_LDO_CONTROL0,
		.enable_mask = AS3722_LDO7_CTRL,
		.ext_enable_reg = AS3722_ENABLE_CTRL4,
		.ext_enable_mask = AS3722_LDO7_EXT_ENABLE_MASK,
		.ranges = as3722_ldo_ranges,
		.nranges = nitems(as3722_ldo_ranges),
	},
	{
		.id = AS3722_REG_ID_LDO9,
		.name = "ldo9",
		.supply_name = "vin-ldo9-10",
		.volt_reg = AS3722_LDO9_VOLTAGE,
		.volt_vsel_mask = AS3722_LDO_VSEL_MASK,
		.enable_reg = AS3722_LDO_CONTROL1,
		.enable_mask = AS3722_LDO9_CTRL,
		.ext_enable_reg = AS3722_ENABLE_CTRL5,
		.ext_enable_mask = AS3722_LDO9_EXT_ENABLE_MASK,
		.ranges = as3722_ldo_ranges,
		.nranges = nitems(as3722_ldo_ranges),
	},
	{
		.id = AS3722_REG_ID_LDO10,
		.name = "ldo10",
		.supply_name = "vin-ldo9-10",
		.volt_reg = AS3722_LDO10_VOLTAGE,
		.volt_vsel_mask = AS3722_LDO_VSEL_MASK,
		.enable_reg = AS3722_LDO_CONTROL1,
		.enable_mask = AS3722_LDO10_CTRL,
		.ext_enable_reg = AS3722_ENABLE_CTRL5,
		.ext_enable_mask = AS3722_LDO10_EXT_ENABLE_MASK,
		.ranges = as3722_ldo_ranges,
		.nranges = nitems(as3722_ldo_ranges),
	},
	{
		.id = AS3722_REG_ID_LDO11,
		.name = "ldo11",
		.supply_name = "vin-ldo11",
		.volt_reg = AS3722_LDO11_VOLTAGE,
		.volt_vsel_mask = AS3722_LDO_VSEL_MASK,
		.enable_reg = AS3722_LDO_CONTROL1,
		.enable_mask = AS3722_LDO11_CTRL,
		.ext_enable_reg = AS3722_ENABLE_CTRL5,
		.ext_enable_mask = AS3722_LDO11_EXT_ENABLE_MASK,
		.ranges = as3722_ldo_ranges,
		.nranges = nitems(as3722_ldo_ranges),
	},
};


struct as3722_regnode_init_def {
	struct regnode_init_def	reg_init_def;
	int 			ext_control;
	int	 		enable_tracking;
};

static int as3722_regnode_init(struct regnode *regnode);
static int as3722_regnode_enable(struct regnode *regnode, bool enable,
    int *udelay);
static int as3722_regnode_set_volt(struct regnode *regnode, int min_uvolt,
    int max_uvolt, int *udelay);
static int as3722_regnode_get_volt(struct regnode *regnode, int *uvolt);
static regnode_method_t as3722_regnode_methods[] = {
	/* Regulator interface */
	REGNODEMETHOD(regnode_init,		as3722_regnode_init),
	REGNODEMETHOD(regnode_enable,		as3722_regnode_enable),
	REGNODEMETHOD(regnode_set_voltage,	as3722_regnode_set_volt),
	REGNODEMETHOD(regnode_get_voltage,	as3722_regnode_get_volt),
	REGNODEMETHOD_END
};
DEFINE_CLASS_1(as3722_regnode, as3722_regnode_class, as3722_regnode_methods,
   sizeof(struct as3722_reg_sc), regnode_class);

static int
as3722_read_sel(struct as3722_reg_sc *sc, uint8_t *sel)
{
	int rv;

	rv = RD1(sc->base_sc, sc->def->volt_reg, sel);
	if (rv != 0)
		return (rv);
	*sel &= sc->def->volt_vsel_mask;
	*sel >>= ffs(sc->def->volt_vsel_mask) - 1;
	return (0);
}

static int
as3722_write_sel(struct as3722_reg_sc *sc, uint8_t sel)
{
	int rv;

	sel <<= ffs(sc->def->volt_vsel_mask) - 1;
	sel &= sc->def->volt_vsel_mask;

	rv = RM1(sc->base_sc, sc->def->volt_reg,
	    sc->def->volt_vsel_mask, sel);
	if (rv != 0)
		return (rv);
	return (rv);
}

static bool
as3722_sd0_is_low_voltage(struct as3722_reg_sc *sc)
{
	uint8_t val;
	int rv;

	rv = RD1(sc->base_sc, AS3722_FUSE7, &val);
	if (rv != 0)
		return (rv);
	return (val & AS3722_FUSE7_SD0_LOW_VOLTAGE ? true : false);
}

static int
as3722_reg_extreg_setup(struct as3722_reg_sc *sc, int ext_pwr_ctrl)
{
	uint8_t val;
	int rv;

	val =  ext_pwr_ctrl << (ffs(sc->def->ext_enable_mask) - 1);
	rv = RM1(sc->base_sc, sc->def->ext_enable_reg,
	    sc->def->ext_enable_mask, val);
	return (rv);
}

static int
as3722_reg_enable(struct as3722_reg_sc *sc)
{
	int rv;

	rv = RM1(sc->base_sc, sc->def->enable_reg,
	    sc->def->enable_mask, sc->def->enable_mask);
	return (rv);
}

static int
as3722_reg_disable(struct as3722_reg_sc *sc)
{
	int rv;

	rv = RM1(sc->base_sc, sc->def->enable_reg,
	    sc->def->enable_mask, 0);
	return (rv);
}

static int
as3722_regnode_init(struct regnode *regnode)
{
	struct as3722_reg_sc *sc;
	int rv;

	sc = regnode_get_softc(regnode);

	sc->enable_usec = 500;
	if (sc->def->id == AS3722_REG_ID_SD0) {
		if (as3722_sd0_is_low_voltage(sc)) {
			sc->def->ranges = as3722_sd0_lv_ranges;
			sc->def->nranges = nitems(as3722_sd0_lv_ranges);
		}
		sc->enable_usec = 600;
	} else if (sc->def->id == AS3722_REG_ID_LDO3) {
		if (sc->enable_tracking) {
			rv = RM1(sc->base_sc, sc->def->volt_reg,
			    AS3722_LDO3_MODE_MASK,
			    AS3722_LDO3_MODE_PMOS_TRACKING);
			if (rv < 0) {
				device_printf(sc->base_sc->dev,
					"LDO3 tracking failed: %d\n", rv);
				return (rv);
			}
		}
	}

	if (sc->ext_control) {

		rv = as3722_reg_enable(sc);
		if (rv < 0) {
			device_printf(sc->base_sc->dev,
				"Failed to enable %s regulator: %d\n",
				sc->def->name, rv);
			return (rv);
		}
		rv = as3722_reg_extreg_setup(sc, sc->ext_control);
		if (rv < 0) {
			device_printf(sc->base_sc->dev,
				"%s ext control failed: %d", sc->def->name, rv);
			return (rv);
		}
	}
	return (0);
}

static void
as3722_fdt_parse(struct as3722_softc *sc, phandle_t node, struct reg_def *def,
struct as3722_regnode_init_def *init_def)
{
	int rv;
	phandle_t parent, supply_node;
	char prop_name[64]; /* Maximum OFW property name length. */

	rv = regulator_parse_ofw_stdparam(sc->dev, node,
	    &init_def->reg_init_def);

	rv = OF_getencprop(node, "ams,ext-control", &init_def->ext_control,
	    sizeof(init_def->ext_control));
	if (rv <= 0)
		init_def->ext_control = 0;
	if (init_def->ext_control > 3) {
		device_printf(sc->dev,
		    "Invalid value for ams,ext-control property: %d\n",
		    init_def->ext_control);
		init_def->ext_control = 0;
	}
	if (OF_hasprop(node, "ams,enable-tracking"))
		init_def->enable_tracking = 1;


	/* Get parent supply. */
	if (def->supply_name == NULL)
		 return;

	parent = OF_parent(node);
	snprintf(prop_name, sizeof(prop_name), "%s-supply",
	    def->supply_name);
	rv = OF_getencprop(parent, prop_name, &supply_node,
	    sizeof(supply_node));
	if (rv <= 0)
		return;
	supply_node = OF_node_from_xref(supply_node);
	rv = OF_getprop_alloc(supply_node, "regulator-name",
	    (void **)&init_def->reg_init_def.parent_name);
	if (rv <= 0)
		init_def->reg_init_def.parent_name = NULL;
}

static struct as3722_reg_sc *
as3722_attach(struct as3722_softc *sc, phandle_t node, struct reg_def *def)
{
	struct as3722_reg_sc *reg_sc;
	struct as3722_regnode_init_def init_def;
	struct regnode *regnode;

	bzero(&init_def, sizeof(init_def));

	as3722_fdt_parse(sc, node, def, &init_def);
	init_def.reg_init_def.id = def->id;
	init_def.reg_init_def.ofw_node = node;
	regnode = regnode_create(sc->dev, &as3722_regnode_class,
	    &init_def.reg_init_def);
	if (regnode == NULL) {
		device_printf(sc->dev, "Cannot create regulator.\n");
		return (NULL);
	}
	reg_sc = regnode_get_softc(regnode);

	/* Init regulator softc. */
	reg_sc->regnode = regnode;
	reg_sc->base_sc = sc;
	reg_sc->def = def;
	reg_sc->xref = OF_xref_from_node(node);

	reg_sc->param = regnode_get_stdparam(regnode);
	reg_sc->ext_control = init_def.ext_control;
	reg_sc->enable_tracking = init_def.enable_tracking;

	regnode_register(regnode);
	if (bootverbose) {
		int volt, rv;
		regnode_topo_slock();
		rv = regnode_get_voltage(regnode, &volt);
		if (rv == ENODEV) {
			device_printf(sc->dev,
			   " Regulator %s: parent doesn't exist yet.\n",
			   regnode_get_name(regnode));
		} else if (rv != 0) {
			device_printf(sc->dev,
			   " Regulator %s: voltage: INVALID!!!\n",
			   regnode_get_name(regnode));
		} else {
			device_printf(sc->dev,
			    " Regulator %s: voltage: %d uV\n",
			    regnode_get_name(regnode), volt);
		}
		regnode_topo_unlock();
	}

	return (reg_sc);
}

int
as3722_regulator_attach(struct as3722_softc *sc, phandle_t node)
{
	struct as3722_reg_sc *reg;
	phandle_t child, rnode;
	int i;

	rnode = ofw_bus_find_child(node, "regulators");
	if (rnode <= 0) {
		device_printf(sc->dev, " Cannot find regulators subnode\n");
		return (ENXIO);
	}

	sc->nregs = nitems(as3722s_def);
	sc->regs = malloc(sizeof(struct as3722_reg_sc *) * sc->nregs,
	    M_AS3722_REG, M_WAITOK | M_ZERO);


	/* Attach all known regulators if exist in DT. */
	for (i = 0; i < sc->nregs; i++) {
		child = ofw_bus_find_child(rnode, as3722s_def[i].name);
		if (child == 0) {
			if (bootverbose)
				device_printf(sc->dev,
				    "Regulator %s missing in DT\n",
				    as3722s_def[i].name);
			continue;
		}
		reg = as3722_attach(sc, child, as3722s_def + i);
		if (reg == NULL) {
			device_printf(sc->dev, "Cannot attach regulator: %s\n",
			    as3722s_def[i].name);
			return (ENXIO);
		}
		sc->regs[i] = reg;
	}
	return (0);
}

int
as3722_regulator_map(device_t dev, phandle_t xref, int ncells,
    pcell_t *cells, int *num)
{
	struct as3722_softc *sc;
	int i;

	sc = device_get_softc(dev);
	for (i = 0; i < sc->nregs; i++) {
		if (sc->regs[i] == NULL)
			continue;
		if (sc->regs[i]->xref == xref) {
			*num = sc->regs[i]->def->id;
			return (0);
		}
	}
	return (ENXIO);
}

static int
as3722_regnode_enable(struct regnode *regnode, bool val, int *udelay)
{
	struct as3722_reg_sc *sc;
	int rv;

	sc = regnode_get_softc(regnode);

	if (val)
		rv = as3722_reg_enable(sc);
	else
		rv = as3722_reg_disable(sc);
	*udelay = sc->enable_usec;
	return (rv);
}

static int
as3722_regnode_set_volt(struct regnode *regnode, int min_uvolt, int max_uvolt,
    int *udelay)
{
	struct as3722_reg_sc *sc;
	uint8_t sel;
	int rv;

	sc = regnode_get_softc(regnode);

	*udelay = 0;
	rv = regulator_range_volt_to_sel8(sc->def->ranges, sc->def->nranges,
	    min_uvolt, max_uvolt, &sel);
	if (rv != 0)
		return (rv);
	rv = as3722_write_sel(sc, sel);
	return (rv);

}

static int
as3722_regnode_get_volt(struct regnode *regnode, int *uvolt)
{
	struct as3722_reg_sc *sc;
	uint8_t sel;
	int rv;

	sc = regnode_get_softc(regnode);
	rv = as3722_read_sel(sc, &sel);
	if (rv != 0)
		return (rv);

	/* LDO6 have bypass. */
	if (sc->def->id == AS3722_REG_ID_LDO6 && sel == AS3722_LDO6_SEL_BYPASS)
		return (ENOENT);
	rv = regulator_range_sel8_to_volt(sc->def->ranges, sc->def->nranges,
	    sel, uvolt);
	return (rv);
}
