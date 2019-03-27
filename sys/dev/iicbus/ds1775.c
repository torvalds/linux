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

/* Drivebay sensor: LM75/DS1775. */
#define DS1775_TEMP         0x0

/* Regular bus attachment functions */
static int  ds1775_probe(device_t);
static int  ds1775_attach(device_t);

struct ds1775_softc {
	struct pmac_therm	sc_sensor;
	device_t		sc_dev;
	struct intr_config_hook enum_hook;
	uint32_t                sc_addr;
};

/* Utility functions */
static int  ds1775_sensor_read(struct ds1775_softc *sc);
static int  ds1775_sensor_sysctl(SYSCTL_HANDLER_ARGS);
static void ds1775_start(void *xdev);
static int  ds1775_read_2(device_t dev, uint32_t addr, uint8_t reg,
			  uint16_t *data);

static device_method_t  ds1775_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		ds1775_probe),
	DEVMETHOD(device_attach,	ds1775_attach),
	{ 0, 0 },
};

static driver_t ds1775_driver = {
	"ds1775",
	ds1775_methods,
	sizeof(struct ds1775_softc)
};

static devclass_t ds1775_devclass;

DRIVER_MODULE(ds1775, iicbus, ds1775_driver, ds1775_devclass, 0, 0);

static int
ds1775_read_2(device_t dev, uint32_t addr, uint8_t reg, uint16_t *data)
{
	uint8_t buf[4];
	int err, try = 0;

	struct iic_msg msg[2] = {
	    { addr, IIC_M_WR | IIC_M_NOSTOP, 1, &reg },
	    { addr, IIC_M_RD, 2, buf },
	};

	for (;;)
	{
		err = iicbus_transfer(dev, msg, 2);
		if (err != 0)
			goto retry;

		*data = *((uint16_t*)buf);
		return (0);
	retry:
		if (++try > 5) {
			device_printf(dev, "iicbus read failed\n");
			return (-1);
		}
		pause("ds1775_read_2", hz);
	}
}

static int
ds1775_probe(device_t dev)
{
	const char  *name, *compatible;
	struct ds1775_softc *sc;

	name = ofw_bus_get_name(dev);
	compatible = ofw_bus_get_compat(dev);

	if (!name)
		return (ENXIO);

	if (strcmp(name, "temp-monitor") != 0 ||
	    (strcmp(compatible, "ds1775") != 0 &&
	     strcmp(compatible, "lm75") != 0))
		return (ENXIO);

	sc = device_get_softc(dev);
	sc->sc_dev = dev;
	sc->sc_addr = iicbus_get_addr(dev);

	device_set_desc(dev, "Temp-Monitor DS1775");

	return (0);
}

static int
ds1775_attach(device_t dev)
{
	struct ds1775_softc *sc;

	sc = device_get_softc(dev);

	sc->enum_hook.ich_func = ds1775_start;
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
ds1775_start(void *xdev)
{
	phandle_t child;
	struct ds1775_softc *sc;
	struct sysctl_oid *oid, *sensroot_oid;
	struct sysctl_ctx_list *ctx;
	ssize_t plen;
	int i;
	char sysctl_name[40], sysctl_desc[40];

	device_t dev = (device_t)xdev;

	sc = device_get_softc(dev);

	child = ofw_bus_get_node(dev);

	ctx = device_get_sysctl_ctx(dev);
	sensroot_oid = SYSCTL_ADD_NODE(ctx,
	    SYSCTL_CHILDREN(device_get_sysctl_tree(dev)), OID_AUTO, "sensor",
	    CTLFLAG_RD, 0, "DS1775 Sensor Information");

	if (OF_getprop(child, "hwsensor-zone", &sc->sc_sensor.zone,
		       sizeof(int)) < 0)
		sc->sc_sensor.zone = 0;

	plen = OF_getprop(child, "hwsensor-location", sc->sc_sensor.name,
			  sizeof(sc->sc_sensor.name));

	if (plen == -1) {
		strcpy(sysctl_name, "sensor");
	} else {
		for (i = 0; i < strlen(sc->sc_sensor.name); i++) {
			sysctl_name[i] = tolower(sc->sc_sensor.name[i]);
			if (isspace(sysctl_name[i]))
				sysctl_name[i] = '_';
		}
		sysctl_name[i] = 0;
	}

	/* Make up target temperatures. These are low, for the drive bay. */
	if (sc->sc_sensor.zone == 0) {
		sc->sc_sensor.target_temp = 500 + ZERO_C_TO_K;
		sc->sc_sensor.max_temp = 600 + ZERO_C_TO_K;
	}
	else {
		sc->sc_sensor.target_temp = 300 + ZERO_C_TO_K;
		sc->sc_sensor.max_temp = 600 + ZERO_C_TO_K;
	}

	sc->sc_sensor.read =
	    (int (*)(struct pmac_therm *sc))(ds1775_sensor_read);
	pmac_thermal_sensor_register(&sc->sc_sensor);

	sprintf(sysctl_desc,"%s %s", sc->sc_sensor.name, "(C)");
	oid = SYSCTL_ADD_NODE(ctx, SYSCTL_CHILDREN(sensroot_oid),
			      OID_AUTO, sysctl_name, CTLFLAG_RD, 0,
			      "Sensor Information");
	SYSCTL_ADD_PROC(ctx, SYSCTL_CHILDREN(oid), OID_AUTO, "temp",
			CTLTYPE_INT | CTLFLAG_RD | CTLFLAG_MPSAFE, dev,
			0, ds1775_sensor_sysctl, "IK", sysctl_desc);

	config_intrhook_disestablish(&sc->enum_hook);
}

static int
ds1775_sensor_read(struct ds1775_softc *sc)
{
	uint16_t buf[2];
	uint16_t read;
	int err;

	err = ds1775_read_2(sc->sc_dev, sc->sc_addr, DS1775_TEMP, buf);
	if (err < 0)
		return (-1);

	read = *((int16_t *)buf);

	/* The default mode of the ADC is 9 bit, the resolution is 0.5 C per
	   bit. The temperature is in tenth kelvin.
	*/
	return (((int16_t)(read) >> 7) * 5 + ZERO_C_TO_K);
}

static int
ds1775_sensor_sysctl(SYSCTL_HANDLER_ARGS)
{
	device_t dev;
	struct ds1775_softc *sc;
	int error;
	int temp;

	dev = arg1;
	sc = device_get_softc(dev);

	temp = ds1775_sensor_read(sc);
	if (temp < 0)
		return (EIO);

	error = sysctl_handle_int(oidp, &temp, 0, req);

	return (error);
}
