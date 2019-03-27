/*-
 * Copyright (c) 2015-2016 Emmanuel Vadot <manu@freebsd.org>
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

/*
* X-Power AXP209/AXP211 PMU for Allwinner SoCs
*/

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/eventhandler.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/clock.h>
#include <sys/time.h>
#include <sys/bus.h>
#include <sys/proc.h>
#include <sys/gpio.h>
#include <sys/reboot.h>
#include <sys/resource.h>
#include <sys/rman.h>
#include <sys/sysctl.h>

#include <dev/iicbus/iiconf.h>

#include <dev/gpio/gpiobusvar.h>

#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>

#include <dev/extres/regulator/regulator.h>

#include <arm/allwinner/axp209reg.h>

#include "gpio_if.h"
#include "regdev_if.h"

MALLOC_DEFINE(M_AXP2XX_REG, "Axp2XX regulator", "Axp2XX power regulator");

struct axp2xx_regdef {
	intptr_t		id;
	char			*name;
	uint8_t			enable_reg;
	uint8_t			enable_mask;
	uint8_t			voltage_reg;
	uint8_t			voltage_mask;
	uint8_t			voltage_shift;
	int			voltage_min;
	int			voltage_max;
	int			voltage_step;
	int			voltage_nstep;
};

static struct axp2xx_regdef axp209_regdefs[] = {
	{
		.id = AXP209_REG_ID_DCDC2,
		.name = "dcdc2",
		.enable_reg = AXP209_POWERCTL,
		.enable_mask = AXP209_POWERCTL_DCDC2,
		.voltage_reg = AXP209_REG_DCDC2_VOLTAGE,
		.voltage_mask = 0x3f,
		.voltage_min = 700,
		.voltage_max = 2275,
		.voltage_step = 25,
		.voltage_nstep = 64,
	},
	{
		.id = AXP209_REG_ID_DCDC3,
		.name = "dcdc3",
		.enable_reg = AXP209_POWERCTL,
		.enable_mask = AXP209_POWERCTL_DCDC3,
		.voltage_reg = AXP209_REG_DCDC3_VOLTAGE,
		.voltage_mask = 0x7f,
		.voltage_min = 700,
		.voltage_max = 3500,
		.voltage_step = 25,
		.voltage_nstep = 128,
	},
	{
		.id = AXP209_REG_ID_LDO2,
		.name = "ldo2",
		.enable_reg = AXP209_POWERCTL,
		.enable_mask = AXP209_POWERCTL_LDO2,
		.voltage_reg = AXP209_REG_LDO24_VOLTAGE,
		.voltage_mask = 0xf0,
		.voltage_shift = 4,
		.voltage_min = 1800,
		.voltage_max = 3300,
		.voltage_step = 100,
		.voltage_nstep = 16,
	},
	{
		.id = AXP209_REG_ID_LDO3,
		.name = "ldo3",
		.enable_reg = AXP209_POWERCTL,
		.enable_mask = AXP209_POWERCTL_LDO3,
		.voltage_reg = AXP209_REG_LDO3_VOLTAGE,
		.voltage_mask = 0x7f,
		.voltage_min = 700,
		.voltage_max = 2275,
		.voltage_step = 25,
		.voltage_nstep = 128,
	},
};

static struct axp2xx_regdef axp221_regdefs[] = {
	{
		.id = AXP221_REG_ID_DLDO1,
		.name = "dldo1",
		.enable_reg = AXP221_POWERCTL_2,
		.enable_mask = AXP221_POWERCTL2_DLDO1,
		.voltage_reg = AXP221_REG_DLDO1_VOLTAGE,
		.voltage_mask = 0x1f,
		.voltage_min = 700,
		.voltage_max = 3300,
		.voltage_step = 100,
		.voltage_nstep = 26,
	},
	{
		.id = AXP221_REG_ID_DLDO2,
		.name = "dldo2",
		.enable_reg = AXP221_POWERCTL_2,
		.enable_mask = AXP221_POWERCTL2_DLDO2,
		.voltage_reg = AXP221_REG_DLDO2_VOLTAGE,
		.voltage_mask = 0x1f,
		.voltage_min = 700,
		.voltage_max = 3300,
		.voltage_step = 100,
		.voltage_nstep = 26,
	},
	{
		.id = AXP221_REG_ID_DLDO3,
		.name = "dldo3",
		.enable_reg = AXP221_POWERCTL_2,
		.enable_mask = AXP221_POWERCTL2_DLDO3,
		.voltage_reg = AXP221_REG_DLDO3_VOLTAGE,
		.voltage_mask = 0x1f,
		.voltage_min = 700,
		.voltage_max = 3300,
		.voltage_step = 100,
		.voltage_nstep = 26,
	},
	{
		.id = AXP221_REG_ID_DLDO4,
		.name = "dldo4",
		.enable_reg = AXP221_POWERCTL_2,
		.enable_mask = AXP221_POWERCTL2_DLDO4,
		.voltage_reg = AXP221_REG_DLDO4_VOLTAGE,
		.voltage_mask = 0x1f,
		.voltage_min = 700,
		.voltage_max = 3300,
		.voltage_step = 100,
		.voltage_nstep = 26,
	},
	{
		.id = AXP221_REG_ID_ELDO1,
		.name = "eldo1",
		.enable_reg = AXP221_POWERCTL_2,
		.enable_mask = AXP221_POWERCTL2_ELDO1,
		.voltage_reg = AXP221_REG_ELDO1_VOLTAGE,
		.voltage_mask = 0x1f,
		.voltage_min = 700,
		.voltage_max = 3300,
		.voltage_step = 100,
		.voltage_nstep = 26,
	},
	{
		.id = AXP221_REG_ID_ELDO2,
		.name = "eldo2",
		.enable_reg = AXP221_POWERCTL_2,
		.enable_mask = AXP221_POWERCTL2_ELDO2,
		.voltage_reg = AXP221_REG_ELDO2_VOLTAGE,
		.voltage_mask = 0x1f,
		.voltage_min = 700,
		.voltage_max = 3300,
		.voltage_step = 100,
		.voltage_nstep = 26,
	},
	{
		.id = AXP221_REG_ID_ELDO3,
		.name = "eldo3",
		.enable_reg = AXP221_POWERCTL_2,
		.enable_mask = AXP221_POWERCTL2_ELDO3,
		.voltage_reg = AXP221_REG_ELDO3_VOLTAGE,
		.voltage_mask = 0x1f,
		.voltage_min = 700,
		.voltage_max = 3300,
		.voltage_step = 100,
		.voltage_nstep = 26,
	},
	{
		.id = AXP221_REG_ID_DC5LDO,
		.name = "dc5ldo",
		.enable_reg = AXP221_POWERCTL_1,
		.enable_mask = AXP221_POWERCTL1_DC5LDO,
		.voltage_reg = AXP221_REG_DC5LDO_VOLTAGE,
		.voltage_mask = 0x3,
		.voltage_min = 700,
		.voltage_max = 1400,
		.voltage_step = 100,
		.voltage_nstep = 7,
	},
	{
		.id = AXP221_REG_ID_DCDC1,
		.name = "dcdc1",
		.enable_reg = AXP221_POWERCTL_1,
		.enable_mask = AXP221_POWERCTL1_DCDC1,
		.voltage_reg = AXP221_REG_DCDC1_VOLTAGE,
		.voltage_mask = 0x1f,
		.voltage_min = 1600,
		.voltage_max = 3400,
		.voltage_step = 100,
		.voltage_nstep = 18,
	},
	{
		.id = AXP221_REG_ID_DCDC2,
		.name = "dcdc2",
		.enable_reg = AXP221_POWERCTL_1,
		.enable_mask = AXP221_POWERCTL1_DCDC2,
		.voltage_reg = AXP221_REG_DCDC2_VOLTAGE,
		.voltage_mask = 0x3f,
		.voltage_min = 600,
		.voltage_max = 1540,
		.voltage_step = 20,
		.voltage_nstep = 47,
	},
	{
		.id = AXP221_REG_ID_DCDC3,
		.name = "dcdc3",
		.enable_reg = AXP221_POWERCTL_1,
		.enable_mask = AXP221_POWERCTL1_DCDC3,
		.voltage_reg = AXP221_REG_DCDC3_VOLTAGE,
		.voltage_mask = 0x3f,
		.voltage_min = 600,
		.voltage_max = 1860,
		.voltage_step = 20,
		.voltage_nstep = 63,
	},
	{
		.id = AXP221_REG_ID_DCDC4,
		.name = "dcdc4",
		.enable_reg = AXP221_POWERCTL_1,
		.enable_mask = AXP221_POWERCTL1_DCDC4,
		.voltage_reg = AXP221_REG_DCDC4_VOLTAGE,
		.voltage_mask = 0x3f,
		.voltage_min = 600,
		.voltage_max = 1540,
		.voltage_step = 20,
		.voltage_nstep = 47,
	},
	{
		.id = AXP221_REG_ID_DCDC5,
		.name = "dcdc5",
		.enable_reg = AXP221_POWERCTL_1,
		.enable_mask = AXP221_POWERCTL1_DCDC5,
		.voltage_reg = AXP221_REG_DCDC5_VOLTAGE,
		.voltage_mask = 0x1f,
		.voltage_min = 1000,
		.voltage_max = 2550,
		.voltage_step = 50,
		.voltage_nstep = 31,
	},
	{
		.id = AXP221_REG_ID_ALDO1,
		.name = "aldo1",
		.enable_reg = AXP221_POWERCTL_1,
		.enable_mask = AXP221_POWERCTL1_ALDO1,
		.voltage_reg = AXP221_REG_ALDO1_VOLTAGE,
		.voltage_mask = 0x1f,
		.voltage_min = 700,
		.voltage_max = 3300,
		.voltage_step = 100,
		.voltage_nstep = 26,
	},
	{
		.id = AXP221_REG_ID_ALDO2,
		.name = "aldo2",
		.enable_reg = AXP221_POWERCTL_1,
		.enable_mask = AXP221_POWERCTL1_ALDO2,
		.voltage_reg = AXP221_REG_ALDO2_VOLTAGE,
		.voltage_mask = 0x1f,
		.voltage_min = 700,
		.voltage_max = 3300,
		.voltage_step = 100,
		.voltage_nstep = 26,
	},
	{
		.id = AXP221_REG_ID_ALDO3,
		.name = "aldo3",
		.enable_reg = AXP221_POWERCTL_3,
		.enable_mask = AXP221_POWERCTL3_ALDO3,
		.voltage_reg = AXP221_REG_ALDO3_VOLTAGE,
		.voltage_mask = 0x1f,
		.voltage_min = 700,
		.voltage_max = 3300,
		.voltage_step = 100,
		.voltage_nstep = 26,
	},
	{
		.id = AXP221_REG_ID_DC1SW,
		.name = "dc1sw",
		.enable_reg = AXP221_POWERCTL_2,
		.enable_mask = AXP221_POWERCTL2_DC1SW,
	},
};

struct axp2xx_reg_sc {
	struct regnode		*regnode;
	device_t		base_dev;
	struct axp2xx_regdef	*def;
	phandle_t		xref;
	struct regnode_std_param *param;
};

struct axp2xx_pins {
	const char	*name;
	uint8_t		ctrl_reg;
	uint8_t		status_reg;
	uint8_t		status_mask;
	uint8_t		status_shift;
};

/* GPIO3 is different, don't expose it for now */
static const struct axp2xx_pins axp209_pins[] = {
	{
		.name = "GPIO0",
		.ctrl_reg = AXP2XX_GPIO0_CTRL,
		.status_reg = AXP2XX_GPIO_STATUS,
		.status_mask = 0x10,
		.status_shift = 4,
	},
	{
		.name = "GPIO1",
		.ctrl_reg = AXP2XX_GPIO1_CTRL,
		.status_reg = AXP2XX_GPIO_STATUS,
		.status_mask = 0x20,
		.status_shift = 5,
	},
	{
		.name = "GPIO2",
		.ctrl_reg = AXP209_GPIO2_CTRL,
		.status_reg = AXP2XX_GPIO_STATUS,
		.status_mask = 0x40,
		.status_shift = 6,
	},
};

static const struct axp2xx_pins axp221_pins[] = {
	{
		.name = "GPIO0",
		.ctrl_reg = AXP2XX_GPIO0_CTRL,
		.status_reg = AXP2XX_GPIO_STATUS,
		.status_mask = 0x1,
		.status_shift = 0x0,
	},
	{
		.name = "GPIO1",
		.ctrl_reg = AXP2XX_GPIO0_CTRL,
		.status_reg = AXP2XX_GPIO_STATUS,
		.status_mask = 0x2,
		.status_shift = 0x1,
	},
};

struct axp2xx_sensors {
	int		id;
	const char	*name;
	const char	*desc;
	const char	*format;
	uint8_t		enable_reg;
	uint8_t		enable_mask;
	uint8_t		value_reg;
	uint8_t		value_size;
	uint8_t		h_value_mask;
	uint8_t		h_value_shift;
	uint8_t		l_value_mask;
	uint8_t		l_value_shift;
	int		value_step;
	int		value_convert;
};

static const struct axp2xx_sensors axp209_sensors[] = {
	{
		.id = AXP209_ACVOLT,
		.name = "acvolt",
		.desc = "AC Voltage (microvolt)",
		.format = "I",
		.enable_reg = AXP2XX_ADC_ENABLE1,
		.enable_mask = AXP209_ADC1_ACVOLT,
		.value_reg = AXP209_ACIN_VOLTAGE,
		.value_size = 2,
		.h_value_mask = 0xff,
		.h_value_shift = 4,
		.l_value_mask = 0xf,
		.l_value_shift = 0,
		.value_step = AXP209_VOLT_STEP,
	},
	{
		.id = AXP209_ACCURRENT,
		.name = "accurrent",
		.desc = "AC Current (microAmpere)",
		.format = "I",
		.enable_reg = AXP2XX_ADC_ENABLE1,
		.enable_mask = AXP209_ADC1_ACCURRENT,
		.value_reg = AXP209_ACIN_CURRENT,
		.value_size = 2,
		.h_value_mask = 0xff,
		.h_value_shift = 4,
		.l_value_mask = 0xf,
		.l_value_shift = 0,
		.value_step = AXP209_ACCURRENT_STEP,
	},
	{
		.id = AXP209_VBUSVOLT,
		.name = "vbusvolt",
		.desc = "VBUS Voltage (microVolt)",
		.format = "I",
		.enable_reg = AXP2XX_ADC_ENABLE1,
		.enable_mask = AXP209_ADC1_VBUSVOLT,
		.value_reg = AXP209_VBUS_VOLTAGE,
		.value_size = 2,
		.h_value_mask = 0xff,
		.h_value_shift = 4,
		.l_value_mask = 0xf,
		.l_value_shift = 0,
		.value_step = AXP209_VOLT_STEP,
	},
	{
		.id = AXP209_VBUSCURRENT,
		.name = "vbuscurrent",
		.desc = "VBUS Current (microAmpere)",
		.format = "I",
		.enable_reg = AXP2XX_ADC_ENABLE1,
		.enable_mask = AXP209_ADC1_VBUSCURRENT,
		.value_reg = AXP209_VBUS_CURRENT,
		.value_size = 2,
		.h_value_mask = 0xff,
		.h_value_shift = 4,
		.l_value_mask = 0xf,
		.l_value_shift = 0,
		.value_step = AXP209_VBUSCURRENT_STEP,
	},
	{
		.id = AXP2XX_BATVOLT,
		.name = "batvolt",
		.desc = "Battery Voltage (microVolt)",
		.format = "I",
		.enable_reg = AXP2XX_ADC_ENABLE1,
		.enable_mask = AXP2XX_ADC1_BATVOLT,
		.value_reg = AXP2XX_BAT_VOLTAGE,
		.value_size = 2,
		.h_value_mask = 0xff,
		.h_value_shift = 4,
		.l_value_mask = 0xf,
		.l_value_shift = 0,
		.value_step = AXP2XX_BATVOLT_STEP,
	},
	{
		.id = AXP2XX_BATCHARGECURRENT,
		.name = "batchargecurrent",
		.desc = "Battery Charging Current (microAmpere)",
		.format = "I",
		.enable_reg = AXP2XX_ADC_ENABLE1,
		.enable_mask = AXP2XX_ADC1_BATCURRENT,
		.value_reg = AXP2XX_BAT_CHARGE_CURRENT,
		.value_size = 2,
		.h_value_mask = 0xff,
		.h_value_shift = 5,
		.l_value_mask = 0x1f,
		.l_value_shift = 0,
		.value_step = AXP2XX_BATCURRENT_STEP,
	},
	{
		.id = AXP2XX_BATDISCHARGECURRENT,
		.name = "batdischargecurrent",
		.desc = "Battery Discharging Current (microAmpere)",
		.format = "I",
		.enable_reg = AXP2XX_ADC_ENABLE1,
		.enable_mask = AXP2XX_ADC1_BATCURRENT,
		.value_reg = AXP2XX_BAT_DISCHARGE_CURRENT,
		.value_size = 2,
		.h_value_mask = 0xff,
		.h_value_shift = 5,
		.l_value_mask = 0x1f,
		.l_value_shift = 0,
		.value_step = AXP2XX_BATCURRENT_STEP,
	},
	{
		.id = AXP2XX_TEMP,
		.name = "temp",
		.desc = "Internal Temperature",
		.format = "IK",
		.enable_reg = AXP209_ADC_ENABLE2,
		.enable_mask = AXP209_ADC2_TEMP,
		.value_reg = AXP209_TEMPMON,
		.value_size = 2,
		.h_value_mask = 0xff,
		.h_value_shift = 4,
		.l_value_mask = 0xf,
		.l_value_shift = 0,
		.value_step = 1,
		.value_convert = -(AXP209_TEMPMON_MIN - AXP209_0C_TO_K),
	},
};

static const struct axp2xx_sensors axp221_sensors[] = {
	{
		.id = AXP2XX_BATVOLT,
		.name = "batvolt",
		.desc = "Battery Voltage (microVolt)",
		.format = "I",
		.enable_reg = AXP2XX_ADC_ENABLE1,
		.enable_mask = AXP2XX_ADC1_BATVOLT,
		.value_reg = AXP2XX_BAT_VOLTAGE,
		.value_size = 2,
		.h_value_mask = 0xff,
		.h_value_shift = 4,
		.l_value_mask = 0xf,
		.l_value_shift = 0,
		.value_step = AXP2XX_BATVOLT_STEP,
	},
	{
		.id = AXP2XX_BATCHARGECURRENT,
		.name = "batchargecurrent",
		.desc = "Battery Charging Current (microAmpere)",
		.format = "I",
		.enable_reg = AXP2XX_ADC_ENABLE1,
		.enable_mask = AXP2XX_ADC1_BATCURRENT,
		.value_reg = AXP2XX_BAT_CHARGE_CURRENT,
		.value_size = 2,
		.h_value_mask = 0xff,
		.h_value_shift = 5,
		.l_value_mask = 0x1f,
		.l_value_shift = 0,
		.value_step = AXP2XX_BATCURRENT_STEP,
	},
	{
		.id = AXP2XX_BATDISCHARGECURRENT,
		.name = "batdischargecurrent",
		.desc = "Battery Discharging Current (microAmpere)",
		.format = "I",
		.enable_reg = AXP2XX_ADC_ENABLE1,
		.enable_mask = AXP2XX_ADC1_BATCURRENT,
		.value_reg = AXP2XX_BAT_DISCHARGE_CURRENT,
		.value_size = 2,
		.h_value_mask = 0xff,
		.h_value_shift = 5,
		.l_value_mask = 0x1f,
		.l_value_shift = 0,
		.value_step = AXP2XX_BATCURRENT_STEP,
	},
	{
		.id = AXP2XX_TEMP,
		.name = "temp",
		.desc = "Internal Temperature",
		.format = "IK",
		.enable_reg = AXP2XX_ADC_ENABLE1,
		.enable_mask = AXP221_ADC1_TEMP,
		.value_reg = AXP221_TEMPMON,
		.value_size = 2,
		.h_value_mask = 0xff,
		.h_value_shift = 4,
		.l_value_mask = 0xf,
		.l_value_shift = 0,
		.value_step = 1,
		.value_convert = -(AXP221_TEMPMON_MIN - AXP209_0C_TO_K),
	},
};

enum AXP2XX_TYPE {
	AXP209 = 1,
	AXP221,
};

struct axp2xx_softc {
	device_t		dev;
	struct resource *	res[1];
	void *			intrcookie;
	struct intr_config_hook	intr_hook;
	struct mtx		mtx;
	uint8_t			type;

	/* GPIO */
	device_t		gpiodev;
	int			npins;
	const struct axp2xx_pins	*pins;

	/* Sensors */
	const struct axp2xx_sensors	*sensors;
	int				nsensors;

	/* Regulators */
	struct axp2xx_reg_sc	**regs;
	int			nregs;
	struct axp2xx_regdef	*regdefs;
};

static struct ofw_compat_data compat_data[] = {
	{ "x-powers,axp209",		AXP209 },
	{ "x-powers,axp221",		AXP221 },
	{ NULL,				0 }
};

static struct resource_spec axp_res_spec[] = {
	{ SYS_RES_IRQ,		0,	RF_ACTIVE },
	{ -1,			0,	0 }
};

#define	AXP_LOCK(sc)	mtx_lock(&(sc)->mtx)
#define	AXP_UNLOCK(sc)	mtx_unlock(&(sc)->mtx)

static int
axp2xx_read(device_t dev, uint8_t reg, uint8_t *data, uint8_t size)
{

	return (iicdev_readfrom(dev, reg, data, size, IIC_INTRWAIT));
}

static int
axp2xx_write(device_t dev, uint8_t reg, uint8_t data)
{

	return (iicdev_writeto(dev, reg, &data, sizeof(data), IIC_INTRWAIT));
}

static int
axp2xx_regnode_init(struct regnode *regnode)
{
	return (0);
}

static int
axp2xx_regnode_enable(struct regnode *regnode, bool enable, int *udelay)
{
	struct axp2xx_reg_sc *sc;
	uint8_t val;

	sc = regnode_get_softc(regnode);

	axp2xx_read(sc->base_dev, sc->def->enable_reg, &val, 1);
	if (enable)
		val |= sc->def->enable_mask;
	else
		val &= ~sc->def->enable_mask;
	axp2xx_write(sc->base_dev, sc->def->enable_reg, val);

	*udelay = 0;

	return (0);
}

static void
axp2xx_regnode_reg_to_voltage(struct axp2xx_reg_sc *sc, uint8_t val, int *uv)
{
	if (val < sc->def->voltage_nstep)
		*uv = sc->def->voltage_min + val * sc->def->voltage_step;
	else
		*uv = sc->def->voltage_min +
		       (sc->def->voltage_nstep * sc->def->voltage_step);
	*uv *= 1000;
}

static int
axp2xx_regnode_voltage_to_reg(struct axp2xx_reg_sc *sc, int min_uvolt,
    int max_uvolt, uint8_t *val)
{
	uint8_t nval;
	int nstep, uvolt;

	nval = 0;
	uvolt = sc->def->voltage_min * 1000;

	for (nstep = 0; nstep < sc->def->voltage_nstep && uvolt < min_uvolt;
	     nstep++) {
		++nval;
		uvolt += (sc->def->voltage_step * 1000);
	}
	if (uvolt > max_uvolt)
		return (EINVAL);

	*val = nval;
	return (0);
}

static int
axp2xx_regnode_set_voltage(struct regnode *regnode, int min_uvolt,
    int max_uvolt, int *udelay)
{
	struct axp2xx_reg_sc *sc;
	uint8_t val;

	sc = regnode_get_softc(regnode);

	if (!sc->def->voltage_step)
		return (ENXIO);

	if (axp2xx_regnode_voltage_to_reg(sc, min_uvolt, max_uvolt, &val) != 0)
		return (ERANGE);

	axp2xx_write(sc->base_dev, sc->def->voltage_reg, val);

	*udelay = 0;

	return (0);
}

static int
axp2xx_regnode_get_voltage(struct regnode *regnode, int *uvolt)
{
	struct axp2xx_reg_sc *sc;
	uint8_t val;

	sc = regnode_get_softc(regnode);

	if (!sc->def->voltage_step)
		return (ENXIO);

	axp2xx_read(sc->base_dev, sc->def->voltage_reg, &val, 1);
	axp2xx_regnode_reg_to_voltage(sc, val & sc->def->voltage_mask, uvolt);

	return (0);
}

static regnode_method_t axp2xx_regnode_methods[] = {
	/* Regulator interface */
	REGNODEMETHOD(regnode_init,		axp2xx_regnode_init),
	REGNODEMETHOD(regnode_enable,		axp2xx_regnode_enable),
	REGNODEMETHOD(regnode_set_voltage,	axp2xx_regnode_set_voltage),
	REGNODEMETHOD(regnode_get_voltage,	axp2xx_regnode_get_voltage),
	REGNODEMETHOD_END
};
DEFINE_CLASS_1(axp2xx_regnode, axp2xx_regnode_class, axp2xx_regnode_methods,
    sizeof(struct axp2xx_reg_sc), regnode_class);

static int
axp2xx_sysctl(SYSCTL_HANDLER_ARGS)
{
	struct axp2xx_softc *sc;
	device_t dev = arg1;
	enum axp2xx_sensor sensor = arg2;
	uint8_t data[2];
	int val, error, i, found;

	sc = device_get_softc(dev);

	for (found = 0, i = 0; i < sc->nsensors; i++) {
		if (sc->sensors[i].id == sensor) {
			found = 1;
			break;
		}
	}

	if (found == 0)
		return (ENOENT);

	error = axp2xx_read(dev, sc->sensors[i].value_reg, data, 2);
	if (error != 0)
		return (error);

	val = ((data[0] & sc->sensors[i].h_value_mask) <<
	    sc->sensors[i].h_value_shift);
	val |= ((data[1] & sc->sensors[i].l_value_mask) <<
	    sc->sensors[i].l_value_shift);
	val *= sc->sensors[i].value_step;
	val += sc->sensors[i].value_convert;

	return sysctl_handle_opaque(oidp, &val, sizeof(val), req);
}

static void
axp2xx_shutdown(void *devp, int howto)
{
	device_t dev;

	if (!(howto & RB_POWEROFF))
		return;
	dev = (device_t)devp;

	if (bootverbose)
		device_printf(dev, "Shutdown AXP2xx\n");

	axp2xx_write(dev, AXP2XX_SHUTBAT, AXP2XX_SHUTBAT_SHUTDOWN);
}

static void
axp2xx_intr(void *arg)
{
	struct axp2xx_softc *sc;
	uint8_t reg;

	sc = arg;

	axp2xx_read(sc->dev, AXP2XX_IRQ1_STATUS, &reg, 1);
	if (reg) {
		if (reg & AXP2XX_IRQ1_AC_OVERVOLT)
			devctl_notify("PMU", "AC", "overvoltage", NULL);
		if (reg & AXP2XX_IRQ1_VBUS_OVERVOLT)
			devctl_notify("PMU", "USB", "overvoltage", NULL);
		if (reg & AXP2XX_IRQ1_VBUS_LOW)
			devctl_notify("PMU", "USB", "undervoltage", NULL);
		if (reg & AXP2XX_IRQ1_AC_CONN)
			devctl_notify("PMU", "AC", "plugged", NULL);
		if (reg & AXP2XX_IRQ1_AC_DISCONN)
			devctl_notify("PMU", "AC", "unplugged", NULL);
		if (reg & AXP2XX_IRQ1_VBUS_CONN)
			devctl_notify("PMU", "USB", "plugged", NULL);
		if (reg & AXP2XX_IRQ1_VBUS_DISCONN)
			devctl_notify("PMU", "USB", "unplugged", NULL);
		axp2xx_write(sc->dev, AXP2XX_IRQ1_STATUS, AXP2XX_IRQ_ACK);
	}

	axp2xx_read(sc->dev, AXP2XX_IRQ2_STATUS, &reg, 1);
	if (reg) {
		if (reg & AXP2XX_IRQ2_BATT_CHARGED)
			devctl_notify("PMU", "Battery", "charged", NULL);
		if (reg & AXP2XX_IRQ2_BATT_CHARGING)
			devctl_notify("PMU", "Battery", "charging", NULL);
		if (reg & AXP2XX_IRQ2_BATT_CONN)
			devctl_notify("PMU", "Battery", "connected", NULL);
		if (reg & AXP2XX_IRQ2_BATT_DISCONN)
			devctl_notify("PMU", "Battery", "disconnected", NULL);
		if (reg & AXP2XX_IRQ2_BATT_TEMP_LOW)
			devctl_notify("PMU", "Battery", "low temp", NULL);
		if (reg & AXP2XX_IRQ2_BATT_TEMP_OVER)
			devctl_notify("PMU", "Battery", "high temp", NULL);
		axp2xx_write(sc->dev, AXP2XX_IRQ2_STATUS, AXP2XX_IRQ_ACK);
	}

	axp2xx_read(sc->dev, AXP2XX_IRQ3_STATUS, &reg, 1);
	if (reg) {
		if (reg & AXP2XX_IRQ3_PEK_SHORT)
			shutdown_nice(RB_POWEROFF);
		axp2xx_write(sc->dev, AXP2XX_IRQ3_STATUS, AXP2XX_IRQ_ACK);
	}

	axp2xx_read(sc->dev, AXP2XX_IRQ4_STATUS, &reg, 1);
	if (reg) {
		axp2xx_write(sc->dev, AXP2XX_IRQ4_STATUS, AXP2XX_IRQ_ACK);
	}

	axp2xx_read(sc->dev, AXP2XX_IRQ5_STATUS, &reg, 1);
	if (reg) {
		axp2xx_write(sc->dev, AXP2XX_IRQ5_STATUS, AXP2XX_IRQ_ACK);
	}
}

static device_t
axp2xx_gpio_get_bus(device_t dev)
{
	struct axp2xx_softc *sc;

	sc = device_get_softc(dev);

	return (sc->gpiodev);
}

static int
axp2xx_gpio_pin_max(device_t dev, int *maxpin)
{
	struct axp2xx_softc *sc;

	sc = device_get_softc(dev);

	*maxpin = sc->npins - 1;

	return (0);
}

static int
axp2xx_gpio_pin_getname(device_t dev, uint32_t pin, char *name)
{
	struct axp2xx_softc *sc;

	sc = device_get_softc(dev);

	if (pin >= sc->npins)
		return (EINVAL);

	snprintf(name, GPIOMAXNAME, "%s", axp209_pins[pin].name);

	return (0);
}

static int
axp2xx_gpio_pin_getcaps(device_t dev, uint32_t pin, uint32_t *caps)
{
	struct axp2xx_softc *sc;

	sc = device_get_softc(dev);

	if (pin >= sc->npins)
		return (EINVAL);

	*caps = GPIO_PIN_INPUT | GPIO_PIN_OUTPUT;

	return (0);
}

static int
axp2xx_gpio_pin_getflags(device_t dev, uint32_t pin, uint32_t *flags)
{
	struct axp2xx_softc *sc;
	uint8_t data, func;
	int error;

	sc = device_get_softc(dev);

	if (pin >= sc->npins)
		return (EINVAL);

	AXP_LOCK(sc);
	error = axp2xx_read(dev, sc->pins[pin].ctrl_reg, &data, 1);
	if (error == 0) {
		func = data & AXP2XX_GPIO_FUNC_MASK;
		if (func == AXP2XX_GPIO_FUNC_INPUT)
			*flags = GPIO_PIN_INPUT;
		else if (func == AXP2XX_GPIO_FUNC_DRVLO ||
		    func == AXP2XX_GPIO_FUNC_DRVHI)
			*flags = GPIO_PIN_OUTPUT;
		else
			*flags = 0;
	}
	AXP_UNLOCK(sc);

	return (error);
}

static int
axp2xx_gpio_pin_setflags(device_t dev, uint32_t pin, uint32_t flags)
{
	struct axp2xx_softc *sc;
	uint8_t data;
	int error;

	sc = device_get_softc(dev);

	if (pin >= sc->npins)
		return (EINVAL);

	AXP_LOCK(sc);
	error = axp2xx_read(dev, sc->pins[pin].ctrl_reg, &data, 1);
	if (error == 0) {
		data &= ~AXP2XX_GPIO_FUNC_MASK;
		if ((flags & (GPIO_PIN_INPUT|GPIO_PIN_OUTPUT)) != 0) {
			if ((flags & GPIO_PIN_OUTPUT) == 0)
				data |= AXP2XX_GPIO_FUNC_INPUT;
		}
		error = axp2xx_write(dev, sc->pins[pin].ctrl_reg, data);
	}
	AXP_UNLOCK(sc);

	return (error);
}

static int
axp2xx_gpio_pin_get(device_t dev, uint32_t pin, unsigned int *val)
{
	struct axp2xx_softc *sc;
	uint8_t data, func;
	int error;

	sc = device_get_softc(dev);

	if (pin >= sc->npins)
		return (EINVAL);

	AXP_LOCK(sc);
	error = axp2xx_read(dev, sc->pins[pin].ctrl_reg, &data, 1);
	if (error == 0) {
		func = data & AXP2XX_GPIO_FUNC_MASK;
		switch (func) {
		case AXP2XX_GPIO_FUNC_DRVLO:
			*val = 0;
			break;
		case AXP2XX_GPIO_FUNC_DRVHI:
			*val = 1;
			break;
		case AXP2XX_GPIO_FUNC_INPUT:
			error = axp2xx_read(dev, sc->pins[pin].status_reg,
			    &data, 1);
			if (error == 0) {
				*val = (data & sc->pins[pin].status_mask);
				*val >>= sc->pins[pin].status_shift;
			}
			break;
		default:
			error = EIO;
			break;
		}
	}
	AXP_UNLOCK(sc);

	return (error);
}

static int
axp2xx_gpio_pin_set(device_t dev, uint32_t pin, unsigned int val)
{
	struct axp2xx_softc *sc;
	uint8_t data, func;
	int error;

	sc = device_get_softc(dev);

	if (pin >= sc->npins)
		return (EINVAL);

	AXP_LOCK(sc);
	error = axp2xx_read(dev, sc->pins[pin].ctrl_reg, &data, 1);
	if (error == 0) {
		func = data & AXP2XX_GPIO_FUNC_MASK;
		switch (func) {
		case AXP2XX_GPIO_FUNC_DRVLO:
		case AXP2XX_GPIO_FUNC_DRVHI:
			/* GPIO2 can't be set to 1 */
			if (pin == 2 && val == 1) {
				error = EINVAL;
				break;
			}
			data &= ~AXP2XX_GPIO_FUNC_MASK;
			data |= val;
			break;
		default:
			error = EIO;
			break;
		}
	}
	if (error == 0)
		error = axp2xx_write(dev, sc->pins[pin].ctrl_reg, data);
	AXP_UNLOCK(sc);

	return (error);
}


static int
axp2xx_gpio_pin_toggle(device_t dev, uint32_t pin)
{
	struct axp2xx_softc *sc;
	uint8_t data, func;
	int error;

	sc = device_get_softc(dev);

	if (pin >= sc->npins)
		return (EINVAL);

	AXP_LOCK(sc);
	error = axp2xx_read(dev, sc->pins[pin].ctrl_reg, &data, 1);
	if (error == 0) {
		func = data & AXP2XX_GPIO_FUNC_MASK;
		switch (func) {
		case AXP2XX_GPIO_FUNC_DRVLO:
			/* Pin 2 can't be set to 1*/
			if (pin == 2) {
				error = EINVAL;
				break;
			}
			data &= ~AXP2XX_GPIO_FUNC_MASK;
			data |= AXP2XX_GPIO_FUNC_DRVHI;
			break;
		case AXP2XX_GPIO_FUNC_DRVHI:
			data &= ~AXP2XX_GPIO_FUNC_MASK;
			data |= AXP2XX_GPIO_FUNC_DRVLO;
			break;
		default:
			error = EIO;
			break;
		}
	}
	if (error == 0)
		error = axp2xx_write(dev, sc->pins[pin].ctrl_reg, data);
	AXP_UNLOCK(sc);

	return (error);
}

static int
axp2xx_gpio_map_gpios(device_t bus, phandle_t dev, phandle_t gparent,
    int gcells, pcell_t *gpios, uint32_t *pin, uint32_t *flags)
{
	struct axp2xx_softc *sc;

	sc = device_get_softc(bus);

	if (gpios[0] >= sc->npins)
		return (EINVAL);

	*pin = gpios[0];
	*flags = gpios[1];

	return (0);
}

static phandle_t
axp2xx_get_node(device_t dev, device_t bus)
{
	return (ofw_bus_get_node(dev));
}

static struct axp2xx_reg_sc *
axp2xx_reg_attach(device_t dev, phandle_t node,
    struct axp2xx_regdef *def)
{
	struct axp2xx_reg_sc *reg_sc;
	struct regnode_init_def initdef;
	struct regnode *regnode;

	memset(&initdef, 0, sizeof(initdef));
	if (regulator_parse_ofw_stdparam(dev, node, &initdef) != 0) {
		device_printf(dev, "cannot create regulator\n");
		return (NULL);
	}
	if (initdef.std_param.min_uvolt == 0)
		initdef.std_param.min_uvolt = def->voltage_min * 1000;
	if (initdef.std_param.max_uvolt == 0)
		initdef.std_param.max_uvolt = def->voltage_max * 1000;
	initdef.id = def->id;
	initdef.ofw_node = node;
	regnode = regnode_create(dev, &axp2xx_regnode_class, &initdef);
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
axp2xx_regdev_map(device_t dev, phandle_t xref, int ncells, pcell_t *cells,
    intptr_t *num)
{
	struct axp2xx_softc *sc;
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

static void
axp2xx_start(void *pdev)
{
	device_t dev;
	struct axp2xx_softc *sc;
	const char *pwr_name[] = {"Battery", "AC", "USB", "AC and USB"};
	int i;
	uint8_t reg, data;
	uint8_t pwr_src;

	dev = pdev;

	sc = device_get_softc(dev);
	sc->dev = dev;

	if (bootverbose) {
		/*
		 * Read the Power State register.
		 * Shift the AC presence into bit 0.
		 * Shift the Battery presence into bit 1.
		 */
		axp2xx_read(dev, AXP2XX_PSR, &data, 1);
		pwr_src = ((data & AXP2XX_PSR_ACIN) >> AXP2XX_PSR_ACIN_SHIFT) |
		    ((data & AXP2XX_PSR_VBUS) >> (AXP2XX_PSR_VBUS_SHIFT - 1));

		device_printf(dev, "Powered by %s\n",
		    pwr_name[pwr_src]);
	}

	/* Only enable interrupts that we are interested in */
	axp2xx_write(dev, AXP2XX_IRQ1_ENABLE,
	    AXP2XX_IRQ1_AC_OVERVOLT |
	    AXP2XX_IRQ1_AC_DISCONN |
	    AXP2XX_IRQ1_AC_CONN |
	    AXP2XX_IRQ1_VBUS_OVERVOLT |
	    AXP2XX_IRQ1_VBUS_DISCONN |
	    AXP2XX_IRQ1_VBUS_CONN);
	axp2xx_write(dev, AXP2XX_IRQ2_ENABLE,
	    AXP2XX_IRQ2_BATT_CONN |
	    AXP2XX_IRQ2_BATT_DISCONN |
	    AXP2XX_IRQ2_BATT_CHARGE_ACCT_ON |
	    AXP2XX_IRQ2_BATT_CHARGE_ACCT_OFF |
	    AXP2XX_IRQ2_BATT_CHARGING |
	    AXP2XX_IRQ2_BATT_CHARGED |
	    AXP2XX_IRQ2_BATT_TEMP_OVER |
	    AXP2XX_IRQ2_BATT_TEMP_LOW);
	axp2xx_write(dev, AXP2XX_IRQ3_ENABLE,
	    AXP2XX_IRQ3_PEK_SHORT | AXP2XX_IRQ3_PEK_LONG);
	axp2xx_write(dev, AXP2XX_IRQ4_ENABLE, AXP2XX_IRQ4_APS_LOW_2);
	axp2xx_write(dev, AXP2XX_IRQ5_ENABLE, 0x0);

	EVENTHANDLER_REGISTER(shutdown_final, axp2xx_shutdown, dev,
	    SHUTDOWN_PRI_LAST);

	/* Enable ADC sensors */
	for (i = 0; i < sc->nsensors; i++) {
		if (axp2xx_read(dev, sc->sensors[i].enable_reg, &reg, 1) == -1) {
			device_printf(dev, "Cannot enable sensor '%s'\n",
			    sc->sensors[i].name);
			continue;
		}
		reg |= sc->sensors[i].enable_mask;
		if (axp2xx_write(dev, sc->sensors[i].enable_reg, reg) == -1) {
			device_printf(dev, "Cannot enable sensor '%s'\n",
			    sc->sensors[i].name);
			continue;
		}
		SYSCTL_ADD_PROC(device_get_sysctl_ctx(dev),
		    SYSCTL_CHILDREN(device_get_sysctl_tree(dev)),
		    OID_AUTO, sc->sensors[i].name,
		    CTLTYPE_INT | CTLFLAG_RD,
		    dev, sc->sensors[i].id, axp2xx_sysctl,
		    sc->sensors[i].format,
		    sc->sensors[i].desc);
	}

	if ((bus_setup_intr(dev, sc->res[0], INTR_TYPE_MISC | INTR_MPSAFE,
	      NULL, axp2xx_intr, sc, &sc->intrcookie)))
		device_printf(dev, "unable to register interrupt handler\n");

	config_intrhook_disestablish(&sc->intr_hook);
}

static int
axp2xx_probe(device_t dev)
{

	if (!ofw_bus_status_okay(dev))
		return (ENXIO);

	switch (ofw_bus_search_compatible(dev, compat_data)->ocd_data)
	{
	case AXP209:
		device_set_desc(dev, "X-Powers AXP209 Power Management Unit");
		break;
	case AXP221:
		device_set_desc(dev, "X-Powers AXP221 Power Management Unit");
		break;
	default:
		return (ENXIO);
	}

	return (BUS_PROBE_DEFAULT);
}

static int
axp2xx_attach(device_t dev)
{
	struct axp2xx_softc *sc;
	struct axp2xx_reg_sc *reg;
	struct axp2xx_regdef *regdefs;
	phandle_t rnode, child;
	int i;

	sc = device_get_softc(dev);
	mtx_init(&sc->mtx, device_get_nameunit(dev), NULL, MTX_DEF);

	if (bus_alloc_resources(dev, axp_res_spec, sc->res) != 0) {
		device_printf(dev, "can't allocate device resources\n");
		return (ENXIO);
	}

	sc->type = ofw_bus_search_compatible(dev, compat_data)->ocd_data;
	switch (sc->type) {
	case AXP209:
		sc->pins = axp209_pins;
		sc->npins = nitems(axp209_pins);
		sc->gpiodev = gpiobus_attach_bus(dev);

		sc->sensors = axp209_sensors;
		sc->nsensors = nitems(axp209_sensors);

		regdefs = axp209_regdefs;
		sc->nregs = nitems(axp209_regdefs);
		break;
	case AXP221:
		sc->pins = axp221_pins;
		sc->npins = nitems(axp221_pins);
		sc->gpiodev = gpiobus_attach_bus(dev);

		sc->sensors = axp221_sensors;
		sc->nsensors = nitems(axp221_sensors);

		regdefs = axp221_regdefs;
		sc->nregs = nitems(axp221_regdefs);
		break;
	}

	sc->regs = malloc(sizeof(struct axp2xx_reg_sc *) * sc->nregs,
	    M_AXP2XX_REG, M_WAITOK | M_ZERO);

	sc->intr_hook.ich_func = axp2xx_start;
	sc->intr_hook.ich_arg = dev;

	if (config_intrhook_establish(&sc->intr_hook) != 0)
		return (ENOMEM);

	/* Attach known regulators that exist in the DT */
	rnode = ofw_bus_find_child(ofw_bus_get_node(dev), "regulators");
	if (rnode > 0) {
		for (i = 0; i < sc->nregs; i++) {
			child = ofw_bus_find_child(rnode,
			    regdefs[i].name);
			if (child == 0)
				continue;
			reg = axp2xx_reg_attach(dev, child, &regdefs[i]);
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

static device_method_t axp2xx_methods[] = {
	DEVMETHOD(device_probe,		axp2xx_probe),
	DEVMETHOD(device_attach,	axp2xx_attach),

	/* GPIO interface */
	DEVMETHOD(gpio_get_bus,		axp2xx_gpio_get_bus),
	DEVMETHOD(gpio_pin_max,		axp2xx_gpio_pin_max),
	DEVMETHOD(gpio_pin_getname,	axp2xx_gpio_pin_getname),
	DEVMETHOD(gpio_pin_getcaps,	axp2xx_gpio_pin_getcaps),
	DEVMETHOD(gpio_pin_getflags,	axp2xx_gpio_pin_getflags),
	DEVMETHOD(gpio_pin_setflags,	axp2xx_gpio_pin_setflags),
	DEVMETHOD(gpio_pin_get,		axp2xx_gpio_pin_get),
	DEVMETHOD(gpio_pin_set,		axp2xx_gpio_pin_set),
	DEVMETHOD(gpio_pin_toggle,	axp2xx_gpio_pin_toggle),
	DEVMETHOD(gpio_map_gpios,	axp2xx_gpio_map_gpios),

	/* Regdev interface */
	DEVMETHOD(regdev_map,		axp2xx_regdev_map),

	/* OFW bus interface */
	DEVMETHOD(ofw_bus_get_node,	axp2xx_get_node),

	DEVMETHOD_END
};

static driver_t axp2xx_driver = {
	"axp2xx_pmu",
	axp2xx_methods,
	sizeof(struct axp2xx_softc),
};

static devclass_t axp2xx_devclass;
extern devclass_t ofwgpiobus_devclass, gpioc_devclass;
extern driver_t ofw_gpiobus_driver, gpioc_driver;

EARLY_DRIVER_MODULE(axp2xx, iicbus, axp2xx_driver, axp2xx_devclass,
  0, 0, BUS_PASS_INTERRUPT + BUS_PASS_ORDER_LATE);
EARLY_DRIVER_MODULE(ofw_gpiobus, axp2xx_pmu, ofw_gpiobus_driver,
    ofwgpiobus_devclass, 0, 0, BUS_PASS_INTERRUPT + BUS_PASS_ORDER_LATE);
DRIVER_MODULE(gpioc, axp2xx_pmu, gpioc_driver, gpioc_devclass,
    0, 0);
MODULE_VERSION(axp2xx, 1);
MODULE_DEPEND(axp2xx, iicbus, IICBUS_MINVER, IICBUS_PREFVER, IICBUS_MAXVER);
