/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2012 Andreas Tobler
 * Copyright (c) 2014 Justin Hibbits
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
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/bus.h>
#include <sys/systm.h>
#include <sys/module.h>
#include <sys/callout.h>
#include <sys/conf.h>
#include <sys/cpu.h>
#include <sys/ctype.h>
#include <sys/kernel.h>
#include <sys/reboot.h>
#include <sys/rman.h>
#include <sys/sysctl.h>
#include <sys/limits.h>

#include <machine/bus.h>
#include <machine/md_var.h>

#include <dev/iicbus/iicbus.h>
#include <dev/iicbus/iiconf.h>

#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_bus.h>
#include <powerpc/powermac/powermac_thermal.h>

/* ADT746X registers. */
#define ADT746X_TACH1LOW          0x28
#define ADT746X_TACH1HIGH         0x29
#define ADT746X_TACH2LOW          0x2a
#define ADT746X_TACH2HIGH         0x2b
#define ADT746X_PWM1              0x30
#define ADT746X_PWM2              0x31
#define ADT746X_DEVICE_ID         0x3d
#define ADT746X_COMPANY_ID        0x3e
#define ADT746X_REV_ID            0x3f
#define ADT746X_CONFIG            0x40
#define ADT746X_PWM1_CONF         0x5c
#define ADT746X_PWM2_CONF         0x5d
#define ADT746X_MANUAL_MASK       0xe0

#define ADT7460_DEV_ID            0x27
#define ADT7467_DEV_ID            0x68

struct adt746x_fan {
	struct pmac_fan fan;
	device_t        dev;
	int             id;
	int             setpoint;
	int		pwm_reg;
	int		conf_reg;
};

struct adt746x_sensor {
	struct pmac_therm therm;
	device_t          dev;
	int               id;
	cell_t	          reg;
	enum {
		ADT746X_SENSOR_TEMP,
		ADT746X_SENSOR_VOLT,
		ADT746X_SENSOR_SPEED
	} type;
};

struct adt746x_softc {
	device_t		sc_dev;
	struct intr_config_hook enum_hook;
	uint32_t                sc_addr;
	/* The 7467 supports up to 4 fans, 2 voltage and 3 temperature sensors. */
	struct adt746x_fan	sc_fans[4];
	int			sc_nfans;
	struct adt746x_sensor   sc_sensors[9];
	int			sc_nsensors;
	int                     device_id;
    
};


/* Regular bus attachment functions */

static int  adt746x_probe(device_t);
static int  adt746x_attach(device_t);


/* Utility functions */
static void adt746x_attach_fans(device_t dev);
static void adt746x_attach_sensors(device_t dev);
static int  adt746x_fill_fan_prop(device_t dev);
static int  adt746x_fill_sensor_prop(device_t dev);

static int  adt746x_fan_set_pwm(struct adt746x_fan *fan, int pwm);
static int  adt746x_fan_get_pwm(struct adt746x_fan *fan);
static int  adt746x_sensor_read(struct adt746x_sensor *sens);
static void adt746x_start(void *xdev);

/* i2c read/write functions. */
static int  adt746x_write(device_t dev, uint32_t addr, uint8_t reg,
			  uint8_t *buf);
static int  adt746x_read(device_t dev, uint32_t addr, uint8_t reg,
			 uint8_t *data);

static device_method_t  adt746x_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,	 adt746x_probe),
	DEVMETHOD(device_attach, adt746x_attach),
	{ 0, 0 },
};

static driver_t adt746x_driver = {
	"adt746x",
	adt746x_methods,
	sizeof(struct adt746x_softc)
};

static devclass_t adt746x_devclass;

DRIVER_MODULE(adt746x, iicbus, adt746x_driver, adt746x_devclass, 0, 0);
static MALLOC_DEFINE(M_ADT746X, "adt746x", "ADT Sensor Information");


/* i2c read/write functions. */

static int
adt746x_write(device_t dev, uint32_t addr, uint8_t reg, uint8_t *buff)
{
	uint8_t buf[4];
	int try = 0;

	struct iic_msg msg[] = {
		{addr, IIC_M_WR, 2, buf }
	};

	/* Prepare the write msg. */
	buf[0] = reg;
	memcpy(buf + 1, buff, 1);

	for (;;)
	{
		if (iicbus_transfer(dev, msg, 1) == 0)
			return (0);
		if (++try > 5) {
			device_printf(dev, "iicbus write failed\n");
			return (-1);
		}
		pause("adt746x_write", hz);
	}
	return (0);
}

static int
adt746x_read(device_t dev, uint32_t addr, uint8_t reg, uint8_t *data)
{
	uint8_t buf[4];
	int err, try = 0;

	struct iic_msg msg[2] = {
		{addr, IIC_M_WR | IIC_M_NOSTOP, 1, &reg},
		{addr, IIC_M_RD, 1, buf},
	};

	for (;;)
	{
		err = iicbus_transfer(dev, msg, 2);
		if (err != 0)
			goto retry;

		*data = *((uint8_t*)buf);
		return (0);
	retry:
		if (++try > 5) {
			device_printf(dev, "iicbus read failed\n");
			return (-1);
		}
		pause("adt746x_read", hz);
	}
}

static int
adt746x_probe(device_t dev)
{
	const char  *name, *compatible;
	struct adt746x_softc *sc;

	name = ofw_bus_get_name(dev);
	compatible = ofw_bus_get_compat(dev);

	if (!name)
		return (ENXIO);

	if (strcmp(name, "fan") != 0 ||
	    (strcmp(compatible, "adt7460") != 0 &&
	     strcmp(compatible, "adt7467") != 0))
		return (ENXIO);

	sc = device_get_softc(dev);
	sc->sc_dev = dev;
	sc->sc_addr = iicbus_get_addr(dev);

	device_set_desc(dev, "Apple Thermostat Unit ADT746X");

	return (0);
}

static int
adt746x_attach(device_t dev)
{
	struct adt746x_softc *sc;

	sc = device_get_softc(dev);

	sc->enum_hook.ich_func = adt746x_start;
	sc->enum_hook.ich_arg = dev;

	/* We have to wait until interrupts are enabled. I2C read and write
	 * only works if the interrupts are available.
	 * The unin/i2c is controlled by the htpic on unin. But this is not
	 * the master. The openpic on mac-io is controlling the htpic.
	 * This one gets attached after the mac-io probing and then the
	 * interrupts will be available.
	 */

	if (config_intrhook_establish(&sc->enum_hook) != 0)
		return (ENOMEM);

	return (0);
}

static void
adt746x_start(void *xdev)
{
	uint8_t did, cid, rev, conf;

	struct adt746x_softc *sc;

	device_t dev = (device_t)xdev;

	sc = device_get_softc(dev);

	adt746x_read(sc->sc_dev, sc->sc_addr, ADT746X_DEVICE_ID, &did);
	adt746x_read(sc->sc_dev, sc->sc_addr, ADT746X_COMPANY_ID, &cid);
	adt746x_read(sc->sc_dev, sc->sc_addr, ADT746X_REV_ID, &rev);
	adt746x_read(sc->sc_dev, sc->sc_addr, ADT746X_CONFIG, &conf);

	device_printf(dev, "Dev ID %#x, Company ID %#x, Rev ID %#x CNF: %#x\n",
		      did, cid, rev, conf);

	/* We can get the device id either from 'of' properties or from the chip
	   itself. This method makes sure we can read the chip, otherwise
	   we return.  */

	sc->device_id = did;

	conf = 1;
	/* Start the ADT7460.  */
	if (sc->device_id == ADT7460_DEV_ID)
		adt746x_write(sc->sc_dev, sc->sc_addr, ADT746X_CONFIG, &conf);

	/* Detect and attach child devices.  */
	adt746x_attach_fans(dev);
	adt746x_attach_sensors(dev);
	config_intrhook_disestablish(&sc->enum_hook);
}

/*
 * Sensor and fan management
 */
static int
adt746x_fan_set_pwm(struct adt746x_fan *fan, int pwm)
{
	uint8_t reg = 0, manual, mode = 0;
	struct adt746x_softc *sc;
	uint8_t buf;

	sc = device_get_softc(fan->dev);

	/* Clamp to allowed range */
	pwm = max(fan->fan.min_rpm, pwm);
	pwm = min(fan->fan.max_rpm, pwm);

	reg = fan->pwm_reg;
	mode = fan->conf_reg;

	/* From the 7460 datasheet:
	   PWM dutycycle can be programmed from 0% (0x00) to 100% (0xFF)
	   in steps of 0.39% (256 steps).
	 */
	buf = (pwm * 100 / 39) - (pwm ? 1 : 0);
	fan->setpoint = buf;

	/* Manual mode.  */
	adt746x_read(sc->sc_dev, sc->sc_addr, mode, &manual);
	manual |= ADT746X_MANUAL_MASK;
	adt746x_write(sc->sc_dev, sc->sc_addr, mode, &manual);

	/* Write speed.  */
	adt746x_write(sc->sc_dev, sc->sc_addr, reg, &buf);

	return (0);
}

static int
adt746x_fan_get_pwm(struct adt746x_fan *fan)
{
	uint8_t buf, reg;
	uint16_t pwm;
	struct adt746x_softc *sc;

	sc = device_get_softc(fan->dev);

	reg = fan->pwm_reg;

	adt746x_read(sc->sc_dev, sc->sc_addr, reg, &buf);

	pwm = (buf * 39 / 100) + (buf ? 1 : 0);
	return (pwm);
}

static int
adt746x_fill_fan_prop(device_t dev)
{
	phandle_t child;
	struct adt746x_softc *sc;
	u_int *id;
	char *location;
	int i, id_len, len = 0, location_len, prev_len = 0;

	sc = device_get_softc(dev);

	child = ofw_bus_get_node(dev);

	/* Fill the fan location property. */
	location_len = OF_getprop_alloc(child, "hwctrl-location", (void **)&location);
	id_len = OF_getprop_alloc_multi(child, "hwctrl-id", sizeof(cell_t), (void **)&id);
	if (location_len == -1 || id_len == -1) {
		OF_prop_free(location);
		OF_prop_free(id);
		return 0;
	}

	/* Fill in all the properties for each fan. */
	for (i = 0; i < id_len; i++) {
		strlcpy(sc->sc_fans[i].fan.name, location + len, 32);
		prev_len = strlen(location + len) + 1;
		len += prev_len;
		sc->sc_fans[i].id = id[i];
		if (id[i] == 6) {
			sc->sc_fans[i].pwm_reg = ADT746X_PWM1;
			sc->sc_fans[i].conf_reg = ADT746X_PWM1_CONF;
		} else if (id[i] == 7) {
			sc->sc_fans[i].pwm_reg = ADT746X_PWM2;
			sc->sc_fans[i].conf_reg = ADT746X_PWM2_CONF;
		} else {
			sc->sc_fans[i].pwm_reg = ADT746X_PWM1 + i;
			sc->sc_fans[i].conf_reg = ADT746X_PWM1_CONF + i;
		}
		sc->sc_fans[i].dev = sc->sc_dev;
		sc->sc_fans[i].fan.min_rpm = 5;	/* Percent */
		sc->sc_fans[i].fan.max_rpm = 100;
		sc->sc_fans[i].fan.read = NULL;
		sc->sc_fans[i].fan.set =
			(int (*)(struct pmac_fan *, int))(adt746x_fan_set_pwm);
		sc->sc_fans[i].fan.default_rpm = sc->sc_fans[i].fan.max_rpm;
	}
	OF_prop_free(location);
	OF_prop_free(id);

	return (i);
}

static int
adt746x_fill_sensor_prop(device_t dev)
{
	phandle_t child, node;
	struct adt746x_softc *sc;
	char sens_type[32];
	int i = 0, reg, sensid;

	sc = device_get_softc(dev);

	child = ofw_bus_get_node(dev);

	/* Fill in the sensor properties for each child. */
	for (node = OF_child(child); node != 0; node = OF_peer(node)) {
		if (OF_getprop(node, "sensor-id", &sensid, sizeof(sensid)) == -1)
		    continue;
		OF_getprop(node, "location", sc->sc_sensors[i].therm.name, 32);
		OF_getprop(node, "device_type", sens_type, sizeof(sens_type));
		if (strcmp(sens_type, "temperature") == 0)
			sc->sc_sensors[i].type = ADT746X_SENSOR_TEMP;
		else if (strcmp(sens_type, "voltage") == 0)
			sc->sc_sensors[i].type = ADT746X_SENSOR_VOLT;
		else
			sc->sc_sensors[i].type = ADT746X_SENSOR_SPEED;
		OF_getprop(node, "reg", &reg, sizeof(reg));
		OF_getprop(node, "sensor-id", &sensid,
			sizeof(sensid));
		/* This is the i2c register of the sensor.  */
		sc->sc_sensors[i].reg = reg;
		sc->sc_sensors[i].id = sensid;
		OF_getprop(node, "zone", &sc->sc_sensors[i].therm.zone,
			sizeof(sc->sc_sensors[i].therm.zone));
		sc->sc_sensors[i].dev = dev;
		sc->sc_sensors[i].therm.read =
		    (int (*)(struct pmac_therm *))adt746x_sensor_read;
		if (sc->sc_sensors[i].type == ADT746X_SENSOR_TEMP) {
		    /* Make up some ranges */
		    sc->sc_sensors[i].therm.target_temp = 500 + ZERO_C_TO_K;
		    sc->sc_sensors[i].therm.max_temp = 800 + ZERO_C_TO_K;

		    pmac_thermal_sensor_register(&sc->sc_sensors[i].therm);
		}
		i++;
	}

	return (i);
}

static int
adt746x_fanrpm_sysctl(SYSCTL_HANDLER_ARGS)
{
	device_t adt;
	struct adt746x_softc *sc;
	struct adt746x_fan *fan;
	int pwm = 0, error;

	adt = arg1;
	sc = device_get_softc(adt);
	fan = &sc->sc_fans[arg2];
	pwm = adt746x_fan_get_pwm(fan);
	error = sysctl_handle_int(oidp, &pwm, 0, req);

	if (error || !req->newptr)
		return (error);

	return (adt746x_fan_set_pwm(fan, pwm));
}

static void
adt746x_attach_fans(device_t dev)
{
	struct adt746x_softc *sc;
	struct sysctl_oid *oid, *fanroot_oid;
	struct sysctl_ctx_list *ctx;
	phandle_t child;
	char sysctl_name[32];
	int i, j;

	sc = device_get_softc(dev);

	sc->sc_nfans = 0;

	child = ofw_bus_get_node(dev);

	/* Count the actual number of fans. */
	sc->sc_nfans = adt746x_fill_fan_prop(dev);

	device_printf(dev, "%d fans detected!\n", sc->sc_nfans);

	if (sc->sc_nfans == 0) {
		device_printf(dev, "WARNING: No fans detected!\n");
		return;
	}

	ctx = device_get_sysctl_ctx(dev);
	fanroot_oid = SYSCTL_ADD_NODE(ctx,
	    SYSCTL_CHILDREN(device_get_sysctl_tree(dev)), OID_AUTO, "fans",
	    CTLFLAG_RD, 0, "ADT Fan Information");

	/* Now we can fill the properties into the allocated struct. */
	sc->sc_nfans = adt746x_fill_fan_prop(dev);

	/* Register fans with pmac_thermal */
	for (i = 0; i < sc->sc_nfans; i++)
		pmac_thermal_fan_register(&sc->sc_fans[i].fan);

	/* Add sysctls for the fans. */
	for (i = 0; i < sc->sc_nfans; i++) {
		for (j = 0; j < strlen(sc->sc_fans[i].fan.name); j++) {
			sysctl_name[j] = tolower(sc->sc_fans[i].fan.name[j]);
			if (isspace(sysctl_name[j]))
				sysctl_name[j] = '_';
		}
		sysctl_name[j] = 0;

		sc->sc_fans[i].setpoint =
			adt746x_fan_get_pwm(&sc->sc_fans[i]);

		oid = SYSCTL_ADD_NODE(ctx, SYSCTL_CHILDREN(fanroot_oid),
		    OID_AUTO, sysctl_name, CTLFLAG_RD, 0, "Fan Information");

		/* I use i to pass the fan id. */
		SYSCTL_ADD_PROC(ctx, SYSCTL_CHILDREN(oid), OID_AUTO,
				"pwm", CTLTYPE_INT | CTLFLAG_RW, dev, i,
				adt746x_fanrpm_sysctl, "I", "Fan PWM in %");
	}

	/* Dump fan location & type. */
	if (bootverbose) {
		for (i = 0; i < sc->sc_nfans; i++) {
			device_printf(dev, "Fan location: %s",
				      sc->sc_fans[i].fan.name);
			device_printf(dev, " id: %d RPM: %d\n",
				      sc->sc_fans[i].id,
				      sc->sc_fans[i].setpoint);
		}
	}
}

static int
adt746x_sensor_read(struct adt746x_sensor *sens)
{
	struct adt746x_softc *sc;
	int tmp = 0;
	uint16_t val;
	uint8_t data[1], data1[1];
	int8_t temp;

	sc = device_get_softc(sens->dev);
	if (sens->type != ADT746X_SENSOR_SPEED) {
		if (adt746x_read(sc->sc_dev, sc->sc_addr, sens->reg,
				 &temp) < 0)
			return (-1);
		if (sens->type == ADT746X_SENSOR_TEMP)
			tmp = 10 * temp + ZERO_C_TO_K;
		else
			tmp = temp;
	} else {
		if (adt746x_read(sc->sc_dev, sc->sc_addr, sens->reg,
				 data) < 0)
			return (-1);
		if (adt746x_read(sc->sc_dev, sc->sc_addr, sens->reg + 1,
				 data1) < 0)
			return (-1);
		val = data[0] + (data1[0] << 8);
		/* A value of 0xffff means the fan is stopped.  */
		if (val == 0 || val == 0xffff)
			tmp = 0;
		else
			tmp = (90000 * 60) / val;
	}
	return (tmp);
}

static int
adt746x_sensor_sysctl(SYSCTL_HANDLER_ARGS)
{
	device_t dev;
	struct adt746x_softc *sc;
	struct adt746x_sensor *sens;
	int value, error;

	dev = arg1;
	sc = device_get_softc(dev);
	sens = &sc->sc_sensors[arg2];

	value = sens->therm.read(&sens->therm);
	if (value < 0)
		return (ENXIO);

	error = sysctl_handle_int(oidp, &value, 0, req);

	return (error);
}

static void
adt746x_attach_sensors(device_t dev)
{
	struct adt746x_softc *sc;
	struct sysctl_oid *oid, *sensroot_oid;
	struct sysctl_ctx_list *ctx;
	phandle_t child;
	char sysctl_name[40];
	const char *unit;
	const char *desc;
	int i, j;


	sc = device_get_softc(dev);
	sc->sc_nsensors = 0;
	child = ofw_bus_get_node(dev);

	/* Count the actual number of sensors. */
	sc->sc_nsensors = adt746x_fill_sensor_prop(dev);
	device_printf(dev, "%d sensors detected!\n", sc->sc_nsensors);
	if (sc->sc_nsensors == 0) {
		device_printf(dev, "WARNING: No sensors detected!\n");
		return;
	}

	ctx = device_get_sysctl_ctx(dev);
	sensroot_oid = SYSCTL_ADD_NODE(ctx,
	    SYSCTL_CHILDREN(device_get_sysctl_tree(dev)), OID_AUTO, "sensors",
	    CTLFLAG_RD, 0, "ADT Sensor Information");

	/* Add the sysctl for the sensors. */
	for (i = 0; i < sc->sc_nsensors; i++) {
		for (j = 0; j < strlen(sc->sc_sensors[i].therm.name); j++) {
			sysctl_name[j] = tolower(sc->sc_sensors[i].therm.name[j]);
			if (isspace(sysctl_name[j]))
				sysctl_name[j] = '_';
		}
		sysctl_name[j] = 0;
		oid = SYSCTL_ADD_NODE(ctx, SYSCTL_CHILDREN(sensroot_oid),
				      OID_AUTO,
				      sysctl_name, CTLFLAG_RD, 0,
				      "Sensor Information");
		if (sc->sc_sensors[i].type == ADT746X_SENSOR_TEMP) {
			unit = "temp";
			desc = "sensor unit (C)";
		} else if (sc->sc_sensors[i].type == ADT746X_SENSOR_VOLT) {
			unit = "volt";
			desc = "sensor unit (mV)";
		} else {
			unit = "rpm";
			desc = "sensor unit (RPM)";
		}
		/* I use i to pass the sensor id. */
		SYSCTL_ADD_PROC(ctx, SYSCTL_CHILDREN(oid), OID_AUTO,
				unit, CTLTYPE_INT | CTLFLAG_RD, dev, i,
				adt746x_sensor_sysctl,
				sc->sc_sensors[i].type == ADT746X_SENSOR_TEMP ?
				"IK" : "I", desc);
	}

	/* Dump sensor location & type. */
	if (bootverbose) {
		for (i = 0; i < sc->sc_nsensors; i++) {
			device_printf(dev, "Sensor location: %s",
				      sc->sc_sensors[i].therm.name);
			device_printf(dev, " type: %d id: %d reg: 0x%x\n",
				      sc->sc_sensors[i].type,
				      sc->sc_sensors[i].id,
				      sc->sc_sensors[i].reg);
		}
	}
}
