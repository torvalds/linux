/*-
 * Copyright (c) 2016 Jared McNeill <jmcneill@invisible.ca>
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

/*
 * Silergy Corp. SY8106A buck regulator
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/rman.h>
#include <sys/kernel.h>
#include <sys/reboot.h>
#include <sys/module.h>

#include <dev/iicbus/iicbus.h>
#include <dev/iicbus/iiconf.h>

#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>

#include <dev/extres/regulator/regulator.h>

#include "iicbus_if.h"
#include "regdev_if.h"

#define	VOUT1_SEL		0x01
#define	 SEL_GO			(1 << 7)
#define	 SEL_VOLTAGE_MASK	0x7f
#define	 SEL_VOLTAGE_BASE	680000	/* uV */
#define	 SEL_VOLTAGE_STEP	10000	/* uV */
#define	VOUT_COM		0x02
#define	 COM_DISABLE		(1 << 0)
#define	SYS_STATUS		0x06

static struct ofw_compat_data compat_data[] = {
	{ "silergy,sy8106a",			1 },
	{ NULL,					0 }
};

struct sy8106a_reg_sc {
	struct regnode		*regnode;
	device_t		base_dev;
	phandle_t		xref;
	struct regnode_std_param *param;
};

struct sy8106a_softc {
	uint16_t		addr;

	/* Regulator */
	struct sy8106a_reg_sc	*reg;
};

static int
sy8106a_read(device_t dev, uint8_t reg, uint8_t *data, uint8_t size)
{
	struct sy8106a_softc *sc;
	struct iic_msg msg[2];

	sc = device_get_softc(dev);

	msg[0].slave = sc->addr;
	msg[0].flags = IIC_M_WR;
	msg[0].len = 1;
	msg[0].buf = &reg;

	msg[1].slave = sc->addr;
	msg[1].flags = IIC_M_RD;
	msg[1].len = size;
	msg[1].buf = data;

	return (iicbus_transfer(dev, msg, 2));
}

static int
sy8106a_write(device_t dev, uint8_t reg, uint8_t val)
{
	struct sy8106a_softc *sc;
	struct iic_msg msg;
	uint8_t buffer[2];

	sc = device_get_softc(dev);

	buffer[0] = reg;
	buffer[1] = val;

	msg.slave = sc->addr;
	msg.flags = IIC_M_WR;
	msg.len = 2;
	msg.buf = buffer;

	return (iicbus_transfer(dev, &msg, 1));
}

static int
sy8106a_regnode_init(struct regnode *regnode)
{
	return (0);
}

static int
sy8106a_regnode_enable(struct regnode *regnode, bool enable, int *udelay)
{
	struct sy8106a_reg_sc *sc;
	uint8_t val;

	sc = regnode_get_softc(regnode);

	sy8106a_read(sc->base_dev, VOUT_COM, &val, 1);
	if (enable)
		val &= ~COM_DISABLE;
	else
		val |= COM_DISABLE;
	sy8106a_write(sc->base_dev, VOUT_COM, val);

	*udelay = sc->param->ramp_delay;

	return (0);
}

static int
sy8106a_regnode_set_voltage(struct regnode *regnode, int min_uvolt,
    int max_uvolt, int *udelay)
{
	struct sy8106a_reg_sc *sc;
	int cur_uvolt;
	uint8_t val, oval;

	sc = regnode_get_softc(regnode);

	/* Get current voltage */
	sy8106a_read(sc->base_dev, VOUT1_SEL, &oval, 1);
	cur_uvolt = (oval & SEL_VOLTAGE_MASK) * SEL_VOLTAGE_STEP +
	    SEL_VOLTAGE_BASE;

	/* Set new voltage */
	val = SEL_GO | ((min_uvolt - SEL_VOLTAGE_BASE) / SEL_VOLTAGE_STEP);
	sy8106a_write(sc->base_dev, VOUT1_SEL, val);

	/* Time to delay is based on the number of voltage steps */
	*udelay = sc->param->ramp_delay *
	    (abs(cur_uvolt - min_uvolt) / SEL_VOLTAGE_STEP);

	return (0);
}

static int
sy8106a_regnode_get_voltage(struct regnode *regnode, int *uvolt)
{
	struct sy8106a_reg_sc *sc;
	uint8_t val;

	sc = regnode_get_softc(regnode);

	sy8106a_read(sc->base_dev, VOUT1_SEL, &val, 1);
	*uvolt = (val & SEL_VOLTAGE_MASK) * SEL_VOLTAGE_STEP +
	    SEL_VOLTAGE_BASE;

	return (0);
}

static regnode_method_t sy8106a_regnode_methods[] = {
	/* Regulator interface */
	REGNODEMETHOD(regnode_init,		sy8106a_regnode_init),
	REGNODEMETHOD(regnode_enable,		sy8106a_regnode_enable),
	REGNODEMETHOD(regnode_set_voltage,	sy8106a_regnode_set_voltage),
	REGNODEMETHOD(regnode_get_voltage,	sy8106a_regnode_get_voltage),
	REGNODEMETHOD_END
};
DEFINE_CLASS_1(sy8106a_regnode, sy8106a_regnode_class, sy8106a_regnode_methods,
    sizeof(struct sy8106a_reg_sc), regnode_class);

static struct sy8106a_reg_sc *
sy8106a_reg_attach(device_t dev, phandle_t node)
{
	struct sy8106a_reg_sc *reg_sc;
	struct regnode_init_def initdef;
	struct regnode *regnode;

	memset(&initdef, 0, sizeof(initdef));
	regulator_parse_ofw_stdparam(dev, node, &initdef);
	initdef.id = 0;
	initdef.ofw_node = node;
	regnode = regnode_create(dev, &sy8106a_regnode_class, &initdef);
	if (regnode == NULL) {
		device_printf(dev, "cannot create regulator\n");
		return (NULL);
	}

	reg_sc = regnode_get_softc(regnode);
	reg_sc->regnode = regnode;
	reg_sc->base_dev = dev;
	reg_sc->xref = OF_xref_from_node(node);
	reg_sc->param = regnode_get_stdparam(regnode);

	regnode_register(regnode);

	return (reg_sc);
}

static int
sy8106a_regdev_map(device_t dev, phandle_t xref, int ncells, pcell_t *cells,
    intptr_t *num)
{
	struct sy8106a_softc *sc;

	sc = device_get_softc(dev);

	if (sc->reg->xref != xref)
		return (ENXIO);

	*num = 0;

	return (0);
}

static int
sy8106a_probe(device_t dev)
{
	if (!ofw_bus_status_okay(dev))
		return (ENXIO);

	if (ofw_bus_search_compatible(dev, compat_data)->ocd_data == 0)
		return (ENXIO);

	device_set_desc(dev, "Silergy SY8106A regulator");

	return (BUS_PROBE_DEFAULT);
}

static int
sy8106a_attach(device_t dev)
{
	struct sy8106a_softc *sc;
	phandle_t node;

	sc = device_get_softc(dev);
	node = ofw_bus_get_node(dev);

	sc->addr = iicbus_get_addr(dev);

	sc->reg = sy8106a_reg_attach(dev, node);
	if (sc->reg == NULL) {
		device_printf(dev, "cannot attach regulator\n");
		return (ENXIO);
	}

	return (0);
}

static device_method_t sy8106a_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		sy8106a_probe),
	DEVMETHOD(device_attach,	sy8106a_attach),

	/* Regdev interface */
	DEVMETHOD(regdev_map,		sy8106a_regdev_map),

	DEVMETHOD_END
};

static driver_t sy8106a_driver = {
	"sy8106a",
	sy8106a_methods,
	sizeof(struct sy8106a_softc),
};

static devclass_t sy8106a_devclass;

EARLY_DRIVER_MODULE(sy8106a, iicbus, sy8106a_driver, sy8106a_devclass, 0, 0,
    BUS_PASS_RESOURCE);
MODULE_VERSION(sy8106a, 1);
MODULE_DEPEND(sy8106a, iicbus, 1, 1, 1);
