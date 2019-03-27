/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2010 Andreas Tobler
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

/* Inlet, Backside, U3 Heatsink sensor: MAX6690. */

#define MAX6690_INT_TEMP    0x0
#define MAX6690_EXT_TEMP    0x1
#define MAX6690_RSL_STATUS  0x2
#define MAX6690_EEXT_TEMP   0x10
#define MAX6690_IEXT_TEMP   0x11
#define MAX6690_TEMP_MASK   0xe0

struct max6690_sensor {
	struct pmac_therm therm;
	device_t dev;

	int     id;
};

/* Regular bus attachment functions */
static int  max6690_probe(device_t);
static int  max6690_attach(device_t);

/* Utility functions */
static int  max6690_sensor_read(struct max6690_sensor *sens);
static int  max6690_sensor_sysctl(SYSCTL_HANDLER_ARGS);
static void max6690_start(void *xdev);
static int  max6690_read(device_t dev, uint32_t addr, uint8_t reg,
			 uint8_t *data);

struct max6690_softc {
	device_t		sc_dev;
	struct intr_config_hook enum_hook;
	uint32_t                sc_addr;
	struct max6690_sensor   *sc_sensors;
	int                     sc_nsensors;
};
static device_method_t  max6690_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		max6690_probe),
	DEVMETHOD(device_attach,	max6690_attach),
	{ 0, 0 },
};

static driver_t max6690_driver = {
	"max6690",
	max6690_methods,
	sizeof(struct max6690_softc)
};

static devclass_t max6690_devclass;

DRIVER_MODULE(max6690, iicbus, max6690_driver, max6690_devclass, 0, 0);
static MALLOC_DEFINE(M_MAX6690, "max6690", "Temp-Monitor MAX6690");

static int
max6690_read(device_t dev, uint32_t addr, uint8_t reg, uint8_t *data)
{
	uint8_t buf[4];
	uint8_t busy[1], rsl;
	int err, try = 0;

	/* Busy register RSL. */
	rsl = MAX6690_RSL_STATUS;
	/* first read the status register, 0x2. If busy, retry. */
	struct iic_msg msg[4] = {
	    { addr, IIC_M_WR | IIC_M_NOSTOP, 1, &rsl },
	    { addr, IIC_M_RD, 1, busy },
	    { addr, IIC_M_WR | IIC_M_NOSTOP, 1, &reg },
	    { addr, IIC_M_RD, 1, buf },
	};

	for (;;)
	{
		err = iicbus_transfer(dev, msg, 4);
		if (err != 0)
			goto retry;
		if (busy[0] & 0x80)
			goto retry;
		/* Check for invalid value and retry. */
		if (buf[0] == 0xff)
			goto retry;

		*data = *((uint8_t*)buf);
		return (0);

	retry:
		if (++try > 5) {
			device_printf(dev, "iicbus read failed\n");
			return (-1);
		}
		pause("max6690_read", hz);
	}
}

static int
max6690_probe(device_t dev)
{
	const char  *name, *compatible;
	struct max6690_softc *sc;

	name = ofw_bus_get_name(dev);
	compatible = ofw_bus_get_compat(dev);

	if (!name)
		return (ENXIO);

	if (strcmp(name, "temp-monitor") != 0 ||
	    strcmp(compatible, "max6690") != 0)
		return (ENXIO);

	sc = device_get_softc(dev);
	sc->sc_dev = dev;
	sc->sc_addr = iicbus_get_addr(dev);

	device_set_desc(dev, "Temp-Monitor MAX6690");

	return (0);
}

/*
 * This function returns the number of sensors. If we call it the second time
 * and we have allocated memory for sc->sc_sensors, we fill in the properties.
 */
static int
max6690_fill_sensor_prop(device_t dev)
{
	phandle_t child;
	struct max6690_softc *sc;
	u_int id[8];
	char location[96];
	int i = 0, j, len = 0, prop_len, prev_len = 0;

	sc = device_get_softc(dev);

	child = ofw_bus_get_node(dev);

	/* Fill the sensor location property. */
	prop_len = OF_getprop(child, "hwsensor-location", location,
			      sizeof(location));
	while (len < prop_len) {
		if (sc->sc_sensors != NULL)
			strcpy(sc->sc_sensors[i].therm.name, location + len);
		prev_len = strlen(location + len) + 1;
		len += prev_len;
		i++;
	}
	if (sc->sc_sensors == NULL)
		return (i);

	/* Fill the sensor id property. */
	prop_len = OF_getprop(child, "hwsensor-id", id, sizeof(id));
	for (j = 0; j < i; j++)
		sc->sc_sensors[j].id = (id[j] & 0xf);

	/* Fill the sensor zone property. */
	prop_len = OF_getprop(child, "hwsensor-zone", id, sizeof(id));
	for (j = 0; j < i; j++)
		sc->sc_sensors[j].therm.zone = id[j];

	/* Set up remaining sensor properties */
	for (j = 0; j < i; j++) {
		sc->sc_sensors[j].dev = dev;

		sc->sc_sensors[j].therm.target_temp = 400 + ZERO_C_TO_K;
		sc->sc_sensors[j].therm.max_temp = 800 + ZERO_C_TO_K;

		sc->sc_sensors[j].therm.read =
		    (int (*)(struct pmac_therm *))(max6690_sensor_read);
	}

	return (i);
}
static int
max6690_attach(device_t dev)
{
	struct max6690_softc *sc;

	sc = device_get_softc(dev);

	sc->enum_hook.ich_func = max6690_start;
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
max6690_start(void *xdev)
{
	struct max6690_softc *sc;
	struct sysctl_oid *oid, *sensroot_oid;
	struct sysctl_ctx_list *ctx;
	char sysctl_desc[40], sysctl_name[32];
	int i, j;

	device_t dev = (device_t)xdev;

	sc = device_get_softc(dev);

	sc->sc_nsensors = 0;

	/* Count the actual number of sensors. */
	sc->sc_nsensors = max6690_fill_sensor_prop(dev);

	device_printf(dev, "%d sensors detected.\n", sc->sc_nsensors);

	if (sc->sc_nsensors == 0)
		device_printf(dev, "WARNING: No MAX6690 sensors detected!\n");

	sc->sc_sensors = malloc (sc->sc_nsensors * sizeof(struct max6690_sensor),
				 M_MAX6690, M_WAITOK | M_ZERO);

	ctx = device_get_sysctl_ctx(dev);
	sensroot_oid = SYSCTL_ADD_NODE(ctx,
	    SYSCTL_CHILDREN(device_get_sysctl_tree(dev)), OID_AUTO, "sensor",
	    CTLFLAG_RD, 0, "MAX6690 Sensor Information");

	/* Now we can fill the properties into the allocated struct. */
	sc->sc_nsensors = max6690_fill_sensor_prop(dev);

	/* Register with powermac_thermal */
	for (i = 0; i < sc->sc_nsensors; i++)
		pmac_thermal_sensor_register(&sc->sc_sensors[i].therm);

	/* Add sysctls for the sensors. */
	for (i = 0; i < sc->sc_nsensors; i++) {
		for (j = 0; j < strlen(sc->sc_sensors[i].therm.name); j++) {
			sysctl_name[j] =
			    tolower(sc->sc_sensors[i].therm.name[j]);
			if (isspace(sysctl_name[j]))
				sysctl_name[j] = '_';
		}
		sysctl_name[j] = 0;

		sprintf(sysctl_desc,"%s %s", sc->sc_sensors[i].therm.name,
			"(C)");
		oid = SYSCTL_ADD_NODE(ctx, SYSCTL_CHILDREN(sensroot_oid),
				      OID_AUTO,
				      sysctl_name, CTLFLAG_RD, 0,
				      "Sensor Information");
		/* I use i to pass the sensor id. */
		SYSCTL_ADD_PROC(ctx, SYSCTL_CHILDREN(oid), OID_AUTO, "temp",
				CTLTYPE_INT | CTLFLAG_RD, dev, i % 2,
				max6690_sensor_sysctl, "IK", sysctl_desc);

	}
	/* Dump sensor location & ID. */
	if (bootverbose) {
		device_printf(dev, "Sensors\n");
		for (i = 0; i < sc->sc_nsensors; i++) {
			device_printf(dev, "Location : %s ID: %d\n",
				      sc->sc_sensors[i].therm.name,
				      sc->sc_sensors[i].id);
		}
	}

	config_intrhook_disestablish(&sc->enum_hook);
}

static int
max6690_sensor_read(struct max6690_sensor *sens)
{
	uint8_t reg_int = 0, reg_ext = 0;
	uint8_t integer = 0;
	uint8_t fraction = 0;
	int err, temp;

	struct max6690_softc *sc;

	sc = device_get_softc(sens->dev);

	/* The internal sensor id's are even, the external are odd. */
	if ((sens->id % 2) == 0) {
		reg_int = MAX6690_INT_TEMP;
		reg_ext = MAX6690_IEXT_TEMP;
	} else {
		reg_int = MAX6690_EXT_TEMP;
		reg_ext = MAX6690_EEXT_TEMP;
	}

	err = max6690_read(sc->sc_dev, sc->sc_addr, reg_int, &integer);

	if (err < 0)
		return (-1);

	err = max6690_read(sc->sc_dev, sc->sc_addr, reg_ext, &fraction);

	if (err < 0)
		return (-1);

	fraction &= MAX6690_TEMP_MASK;

	/* The temperature is in tenth kelvin, the fractional part resolution
	   is 0.125.
	*/
	temp = (integer * 10) + (fraction >> 5) * 10 / 8;

	return (temp + ZERO_C_TO_K);
}

static int
max6690_sensor_sysctl(SYSCTL_HANDLER_ARGS)
{
	device_t dev;
	struct max6690_softc *sc;
	struct max6690_sensor *sens;
	int error;
	int temp;

	dev = arg1;
	sc = device_get_softc(dev);
	sens = &sc->sc_sensors[arg2];

	temp = max6690_sensor_read(sens);
	if (temp < 0)
		return (EIO);

	error = sysctl_handle_int(oidp, &temp, 0, req);

	return (error);
}
