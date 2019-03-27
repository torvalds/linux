/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2016 Jared McNeill <jmcneill@invisible.ca>
 * Copyright (c) 2018 Emmanuel Vadot <manu@FreeBSD.org>
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
#include <sys/reboot.h>
#include <sys/module.h>

#include <dev/iicbus/iicbus.h>
#include <dev/iicbus/iiconf.h>

#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>

#include <dev/extres/regulator/regulator.h>

#include "iicbus_if.h"
#include "regdev_if.h"

#define	VSEL0	0x00
#define	VSEL1	0x01

#define	VSEL_BUCK_EN	(1 << 7)
#define	VSEL_NSEL_MASK	0x3F
#define	VSEL_VOLTAGE_BASE	712500 /* uV */
#define	VSEL_VOLTAGE_STEP	12500  /* uV */

#define	ID1	0x03
#define	 ID1_VENDOR_MASK	0xE0
#define	 ID1_VENDOR_SHIFT	5
#define	 ID1_DIE_MASK		0xF

#define	ID2	0x4
#define	 ID2_DIE_REV_MASK	0xF

static struct ofw_compat_data compat_data[] = {
	{ "silergy,syr827",			1 },
	{ NULL,					0 }
};

struct syr827_reg_sc {
	struct regnode		*regnode;
	device_t		base_dev;
	phandle_t		xref;
	struct regnode_std_param *param;

	int			volt_reg;
	int			suspend_reg;
};

struct syr827_softc {
	uint16_t		addr;
	struct intr_config_hook	intr_hook;

	/* Regulator */
	struct syr827_reg_sc	*reg;
};

static int
syr827_read(device_t dev, uint8_t reg, uint8_t *data, uint8_t size)
{
	struct syr827_softc *sc;
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
syr827_write(device_t dev, uint8_t reg, uint8_t val)
{
	struct syr827_softc *sc;
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
syr827_regnode_init(struct regnode *regnode)
{
	return (0);
}

static int
syr827_regnode_enable(struct regnode *regnode, bool enable, int *udelay)
{
	struct syr827_reg_sc *sc;
	uint8_t val;

	sc = regnode_get_softc(regnode);

	syr827_read(sc->base_dev, sc->volt_reg, &val, 1);
	if (enable)
		val &= ~VSEL_BUCK_EN;
	else
		val |= VSEL_BUCK_EN;
	syr827_write(sc->base_dev, sc->volt_reg, val);

	*udelay = sc->param->ramp_delay;

	return (0);
}

static int
syr827_regnode_set_voltage(struct regnode *regnode, int min_uvolt,
    int max_uvolt, int *udelay)
{
	struct syr827_reg_sc *sc;
	int cur_uvolt;
	uint8_t val;

	sc = regnode_get_softc(regnode);

	/* Get current voltage */
	syr827_read(sc->base_dev, sc->volt_reg, &val, 1);
	cur_uvolt = (val & VSEL_NSEL_MASK) * VSEL_VOLTAGE_STEP +
	    VSEL_VOLTAGE_BASE;

	/* Set new voltage */
	val &= ~VSEL_NSEL_MASK;
	val |= ((min_uvolt - VSEL_VOLTAGE_BASE) / VSEL_VOLTAGE_STEP);
	syr827_write(sc->base_dev, sc->volt_reg, val);

	/* Time to delay is based on the number of voltage steps */
	*udelay = sc->param->ramp_delay *
	    (abs(cur_uvolt - min_uvolt) / VSEL_VOLTAGE_STEP);

	return (0);
}

static int
syr827_regnode_get_voltage(struct regnode *regnode, int *uvolt)
{
	struct syr827_reg_sc *sc;
	uint8_t val;

	sc = regnode_get_softc(regnode);

	syr827_read(sc->base_dev, sc->volt_reg, &val, 1);
	*uvolt = (val & VSEL_NSEL_MASK) * VSEL_VOLTAGE_STEP +
	    VSEL_VOLTAGE_BASE;

	return (0);
}

static regnode_method_t syr827_regnode_methods[] = {
	/* Regulator interface */
	REGNODEMETHOD(regnode_init,		syr827_regnode_init),
	REGNODEMETHOD(regnode_enable,		syr827_regnode_enable),
	REGNODEMETHOD(regnode_set_voltage,	syr827_regnode_set_voltage),
	REGNODEMETHOD(regnode_get_voltage,	syr827_regnode_get_voltage),
	REGNODEMETHOD_END
};
DEFINE_CLASS_1(syr827_regnode, syr827_regnode_class, syr827_regnode_methods,
    sizeof(struct syr827_reg_sc), regnode_class);

static struct syr827_reg_sc *
syr827_reg_attach(device_t dev, phandle_t node)
{
	struct syr827_reg_sc *reg_sc;
	struct regnode_init_def initdef;
	struct regnode *regnode;
	int suspend_reg;

	memset(&initdef, 0, sizeof(initdef));
	regulator_parse_ofw_stdparam(dev, node, &initdef);
	initdef.id = 0;
	initdef.ofw_node = node;
	regnode = regnode_create(dev, &syr827_regnode_class, &initdef);
	if (regnode == NULL) {
		device_printf(dev, "cannot create regulator\n");
		return (NULL);
	}

	reg_sc = regnode_get_softc(regnode);
	reg_sc->regnode = regnode;
	reg_sc->base_dev = dev;
	reg_sc->xref = OF_xref_from_node(node);
	reg_sc->param = regnode_get_stdparam(regnode);

	if (OF_getencprop(node, "fcs,suspend-voltage-selector", &suspend_reg,
	    sizeof(uint32_t)) <= 0)
		suspend_reg = 0;

	switch (suspend_reg) {
	case 0:
		reg_sc->suspend_reg = VSEL0;
		reg_sc->volt_reg = VSEL1;
		break;
	case 1:
		reg_sc->suspend_reg = VSEL1;
		reg_sc->volt_reg = VSEL0;
		break;
	}

	regnode_register(regnode);

	return (reg_sc);
}

static int
syr827_regdev_map(device_t dev, phandle_t xref, int ncells, pcell_t *cells,
    intptr_t *num)
{
	struct syr827_softc *sc;

	sc = device_get_softc(dev);

	if (sc->reg->xref != xref)
		return (ENXIO);

	*num = 0;

	return (0);
}

static int
syr827_probe(device_t dev)
{
	if (!ofw_bus_status_okay(dev))
		return (ENXIO);

	if (ofw_bus_search_compatible(dev, compat_data)->ocd_data == 0)
		return (ENXIO);

	device_set_desc(dev, "Silergy SYR827 regulator");

	return (BUS_PROBE_DEFAULT);
}

static void
syr827_start(void *pdev)
{
	struct syr827_softc *sc;
	device_t dev;
	uint8_t val;

	dev = pdev;
	sc = device_get_softc(dev);

	if (bootverbose) {
		syr827_read(dev, ID1, &val, 1);
		device_printf(dev, "Vendor ID: %x, DIE ID: %x\n",
		    (val & ID1_VENDOR_MASK) >> ID1_VENDOR_SHIFT,
		    val & ID1_DIE_MASK);
		syr827_read(dev, ID2, &val, 1);
		device_printf(dev, "DIE Rev: %x\n", val & ID2_DIE_REV_MASK);
	}

	config_intrhook_disestablish(&sc->intr_hook);
}

static int
syr827_attach(device_t dev)
{
	struct syr827_softc *sc;
	phandle_t node;

	sc = device_get_softc(dev);
	node = ofw_bus_get_node(dev);

	sc->addr = iicbus_get_addr(dev);

	sc->intr_hook.ich_func = syr827_start;
	sc->intr_hook.ich_arg = dev;

	if (config_intrhook_establish(&sc->intr_hook) != 0)
		return (ENOMEM);

	sc->reg = syr827_reg_attach(dev, node);
	if (sc->reg == NULL) {
		device_printf(dev, "cannot attach regulator\n");
		return (ENXIO);
	}

	return (0);
}

static device_method_t syr827_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		syr827_probe),
	DEVMETHOD(device_attach,	syr827_attach),

	/* Regdev interface */
	DEVMETHOD(regdev_map,		syr827_regdev_map),

	DEVMETHOD_END
};

static driver_t syr827_driver = {
	"syr827",
	syr827_methods,
	sizeof(struct syr827_softc),
};

static devclass_t syr827_devclass;

EARLY_DRIVER_MODULE(syr827, iicbus, syr827_driver, syr827_devclass, 0, 0,
    BUS_PASS_RESOURCE);
MODULE_VERSION(syr827, 1);
MODULE_DEPEND(syr827, iicbus, 1, 1, 1);
