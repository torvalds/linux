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

/* FCU registers
 * /u3@0,f8000000/i2c@f8001000/fan@15e
 */
#define FCU_RPM_FAIL      0x0b      /* fans states in bits 0<1-6>7 */
#define FCU_RPM_AVAILABLE 0x0c
#define FCU_RPM_ACTIVE    0x0d
#define FCU_RPM_READ(x)   0x11 + (x) * 2
#define FCU_RPM_SET(x)    0x10 + (x) * 2

#define FCU_PWM_FAIL      0x2b
#define FCU_PWM_AVAILABLE 0x2c
#define FCU_PWM_ACTIVE    0x2d
#define FCU_PWM_RPM(x)    0x31 + (x) * 2 /* Get RPM. */
#define FCU_PWM_SGET(x)   0x30 + (x) * 2 /* Set or get PWM. */

struct fcu_fan {
	struct	pmac_fan fan;
	device_t dev;

	int     id;
	enum {
		FCU_FAN_RPM,
		FCU_FAN_PWM
	} type;
	int     setpoint;
	int     rpm;
};

struct fcu_softc {
	device_t		sc_dev;
	struct intr_config_hook enum_hook;
	uint32_t                sc_addr;
	struct fcu_fan		*sc_fans;
	int			sc_nfans;
};

/* We can read the PWM and the RPM from a PWM controlled fan.
 * Offer both values via sysctl.
 */
enum {
	FCU_PWM_SYSCTL_PWM   = 1 << 8,
	FCU_PWM_SYSCTL_RPM   = 2 << 8
};

static int fcu_rpm_shift;

/* Regular bus attachment functions */
static int  fcu_probe(device_t);
static int  fcu_attach(device_t);

/* Utility functions */
static void fcu_attach_fans(device_t dev);
static int  fcu_fill_fan_prop(device_t dev);
static int  fcu_fan_set_rpm(struct fcu_fan *fan, int rpm);
static int  fcu_fan_get_rpm(struct fcu_fan *fan);
static int  fcu_fan_set_pwm(struct fcu_fan *fan, int pwm);
static int  fcu_fan_get_pwm(device_t dev, struct fcu_fan *fan, int *pwm,
			    int *rpm);
static int  fcu_fanrpm_sysctl(SYSCTL_HANDLER_ARGS);
static void fcu_start(void *xdev);
static int  fcu_write(device_t dev, uint32_t addr, uint8_t reg, uint8_t *buf,
		      int len);
static int  fcu_read_1(device_t dev, uint32_t addr, uint8_t reg, uint8_t *data);

static device_method_t  fcu_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		fcu_probe),
	DEVMETHOD(device_attach,	fcu_attach),
	{ 0, 0 },
};

static driver_t fcu_driver = {
	"fcu",
	fcu_methods,
	sizeof(struct fcu_softc)
};

static devclass_t fcu_devclass;

DRIVER_MODULE(fcu, iicbus, fcu_driver, fcu_devclass, 0, 0);
static MALLOC_DEFINE(M_FCU, "fcu", "FCU Sensor Information");

static int
fcu_write(device_t dev, uint32_t addr, uint8_t reg, uint8_t *buff,
	  int len)
{
	unsigned char buf[4];
	int try = 0;

	struct iic_msg msg[] = {
		{ addr, IIC_M_WR, 0, buf }
	};

	msg[0].len = len + 1;
	buf[0] = reg;
	memcpy(buf + 1, buff, len);

	for (;;)
	{
		if (iicbus_transfer(dev, msg, 1) == 0)
			return (0);

		if (++try > 5) {
			device_printf(dev, "iicbus write failed\n");
			return (-1);
		}
		pause("fcu_write", hz);
	}
}

static int
fcu_read_1(device_t dev, uint32_t addr, uint8_t reg, uint8_t *data)
{
	uint8_t buf[4];
	int err, try = 0;

	struct iic_msg msg[2] = {
	    { addr, IIC_M_WR | IIC_M_NOSTOP, 1, &reg },
	    { addr, IIC_M_RD, 1, buf },
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
		  pause("fcu_read_1", hz);
	}
}

static int
fcu_probe(device_t dev)
{
	const char  *name, *compatible;
	struct fcu_softc *sc;

	name = ofw_bus_get_name(dev);
	compatible = ofw_bus_get_compat(dev);

	if (!name)
		return (ENXIO);

	if (strcmp(name, "fan") != 0 || strcmp(compatible, "fcu") != 0)
		return (ENXIO);

	sc = device_get_softc(dev);
	sc->sc_dev = dev;
	sc->sc_addr = iicbus_get_addr(dev);

	device_set_desc(dev, "Apple Fan Control Unit");

	return (0);
}

static int
fcu_attach(device_t dev)
{
	struct fcu_softc *sc;

	sc = device_get_softc(dev);

	sc->enum_hook.ich_func = fcu_start;
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
fcu_start(void *xdev)
{
	unsigned char buf[1] = { 0xff };
	struct fcu_softc *sc;

	device_t dev = (device_t)xdev;

	sc = device_get_softc(dev);

	/* Start the fcu device. */
	fcu_write(sc->sc_dev, sc->sc_addr, 0xe, buf, 1);
	fcu_write(sc->sc_dev, sc->sc_addr, 0x2e, buf, 1);
	fcu_read_1(sc->sc_dev, sc->sc_addr, 0, buf);
	fcu_rpm_shift = (buf[0] == 1) ? 2 : 3;

	device_printf(dev, "FCU initialized, RPM shift: %d\n",
		      fcu_rpm_shift);

	/* Detect and attach child devices. */

	fcu_attach_fans(dev);

	config_intrhook_disestablish(&sc->enum_hook);

}

static int
fcu_fan_set_rpm(struct fcu_fan *fan, int rpm)
{
	uint8_t reg;
	struct fcu_softc *sc;
	unsigned char buf[2];

	sc = device_get_softc(fan->dev);

	/* Clamp to allowed range */
	rpm = max(fan->fan.min_rpm, rpm);
	rpm = min(fan->fan.max_rpm, rpm);

	if (fan->type == FCU_FAN_RPM) {
		reg = FCU_RPM_SET(fan->id);
		fan->setpoint = rpm;
	} else {
		device_printf(fan->dev, "Unknown fan type: %d\n", fan->type);
		return (ENXIO);
	}

	buf[0] = rpm >> (8 - fcu_rpm_shift);
	buf[1] = rpm << fcu_rpm_shift;

	if (fcu_write(sc->sc_dev, sc->sc_addr, reg, buf, 2) < 0)
		return (EIO);

	return (0);
}

static int
fcu_fan_get_rpm(struct fcu_fan *fan)
{
	uint8_t reg;
	struct fcu_softc *sc;
	uint8_t buff[2] = { 0, 0 };
	uint8_t active = 0, avail = 0, fail = 0;
	int rpm;

	sc = device_get_softc(fan->dev);

	if (fan->type == FCU_FAN_RPM) {
		/* Check if the fan is available. */
		reg = FCU_RPM_AVAILABLE;
		if (fcu_read_1(sc->sc_dev, sc->sc_addr, reg, &avail) < 0)
			return (-1);
		if ((avail & (1 << fan->id)) == 0) {
			device_printf(fan->dev,
			    "RPM Fan not available ID: %d\n", fan->id);
			return (-1);
		}
		/* Check if we have a failed fan. */
		reg = FCU_RPM_FAIL;
		if (fcu_read_1(sc->sc_dev, sc->sc_addr, reg, &fail) < 0)
			return (-1);
		if ((fail & (1 << fan->id)) != 0) {
			device_printf(fan->dev,
			    "RPM Fan failed ID: %d\n", fan->id);
			return (-1);
		}
		/* Check if fan is active. */
		reg = FCU_RPM_ACTIVE;
		if (fcu_read_1(sc->sc_dev, sc->sc_addr, reg, &active) < 0)
			return (-1);
		if ((active & (1 << fan->id)) == 0) {
			device_printf(fan->dev, "RPM Fan not active ID: %d\n",
				      fan->id);
			return (-1);
		}
		reg = FCU_RPM_READ(fan->id);

	} else {
		device_printf(fan->dev, "Unknown fan type: %d\n", fan->type);
		return (-1);
	}

	/* It seems that we can read the fans rpm. */
	if (fcu_read_1(sc->sc_dev, sc->sc_addr, reg, buff) < 0)
		return (-1);

	rpm = (buff[0] << (8 - fcu_rpm_shift)) | buff[1] >> fcu_rpm_shift;

	return (rpm);
}

static int
fcu_fan_set_pwm(struct fcu_fan *fan, int pwm)
{
	uint8_t reg;
	struct fcu_softc *sc;
	uint8_t buf[2];

	sc = device_get_softc(fan->dev);

	/* Clamp to allowed range */
	pwm = max(fan->fan.min_rpm, pwm);
	pwm = min(fan->fan.max_rpm, pwm);

	if (fan->type == FCU_FAN_PWM) {
		reg = FCU_PWM_SGET(fan->id);
		if (pwm > 100)
			pwm = 100;
		if (pwm < 30)
			pwm = 30;
		fan->setpoint = pwm;
	} else {
		device_printf(fan->dev, "Unknown fan type: %d\n", fan->type);
		return (EIO);
	}

	buf[0] = (pwm * 2550) / 1000;

	if (fcu_write(sc->sc_dev, sc->sc_addr, reg, buf, 1) < 0)
		return (EIO);
	return (0);
}

static int
fcu_fan_get_pwm(device_t dev, struct fcu_fan *fan, int *pwm, int *rpm)
{
	uint8_t reg;
	struct fcu_softc *sc;
	uint8_t buf[2];
	uint8_t active = 0, avail = 0, fail = 0;

	sc = device_get_softc(dev);

	if (fan->type == FCU_FAN_PWM) {
		/* Check if the fan is available. */
		reg = FCU_PWM_AVAILABLE;
		if (fcu_read_1(sc->sc_dev, sc->sc_addr, reg, &avail) < 0)
			return (-1);
		if ((avail & (1 << fan->id)) == 0) {
			device_printf(dev, "PWM Fan not available ID: %d\n",
				      fan->id);
			return (-1);
		}
		/* Check if we have a failed fan. */
		reg = FCU_PWM_FAIL;
		if (fcu_read_1(sc->sc_dev, sc->sc_addr, reg, &fail) < 0)
			return (-1);
		if ((fail & (1 << fan->id)) != 0) {
			device_printf(dev, "PWM Fan failed ID: %d\n", fan->id);
			return (-1);
		}
		/* Check if fan is active. */
		reg = FCU_PWM_ACTIVE;
		if (fcu_read_1(sc->sc_dev, sc->sc_addr, reg, &active) < 0)
			return (-1);
		if ((active & (1 << fan->id)) == 0) {
			device_printf(dev, "PWM Fan not active ID: %d\n",
				      fan->id);
			return (-1);
		}
		reg = FCU_PWM_SGET(fan->id);
	} else {
		device_printf(dev, "Unknown fan type: %d\n", fan->type);
		return (EIO);
	}

	/* It seems that we can read the fans pwm. */
	if (fcu_read_1(sc->sc_dev, sc->sc_addr, reg, buf) < 0)
		return (-1);

	*pwm = (buf[0] * 1000) / 2550;

	/* Now read the rpm. */
	reg = FCU_PWM_RPM(fan->id);
	if (fcu_read_1(sc->sc_dev, sc->sc_addr, reg, buf) < 0)
		return (-1);

	*rpm = (buf[0] << (8 - fcu_rpm_shift)) | buf[1] >> fcu_rpm_shift;

	return (0);
}

/*
 * This function returns the number of fans. If we call it the second time
 * and we have allocated memory for sc->sc_fans, we fill in the properties.
 */
static int
fcu_fill_fan_prop(device_t dev)
{
	phandle_t child;
	struct fcu_softc *sc;
	u_int id[12];
	char location[144];
	char type[96];
	int i = 0, j, len = 0, prop_len, prev_len = 0;

	sc = device_get_softc(dev);

	child = ofw_bus_get_node(dev);

	/* Fill the fan location property. */
	prop_len = OF_getprop(child, "hwctrl-location", location,
			      sizeof(location));
	while (len < prop_len) {
		if (sc->sc_fans != NULL) {
			strcpy(sc->sc_fans[i].fan.name, location + len);
		}
		prev_len = strlen(location + len) + 1;
		len += prev_len;
		i++;
	}
	if (sc->sc_fans == NULL)
		return (i);

	/* Fill the fan type property. */
	len = 0;
	i = 0;
	prev_len = 0;
	prop_len = OF_getprop(child, "hwctrl-type", type, sizeof(type));
	while (len < prop_len) {
		if (strcmp(type + len, "fan-rpm") == 0)
			sc->sc_fans[i].type = FCU_FAN_RPM;
		else
			sc->sc_fans[i].type = FCU_FAN_PWM;
		prev_len = strlen(type + len) + 1;
		len += prev_len;
		i++;
	}

	/* Fill the fan ID property. */
	prop_len = OF_getprop(child, "hwctrl-id", id, sizeof(id));
	for (j = 0; j < i; j++)
		sc->sc_fans[j].id = ((id[j] >> 8) & 0x0f) % 8;

	/* Fill the fan zone property. */
	prop_len = OF_getprop(child, "hwctrl-zone", id, sizeof(id));
	for (j = 0; j < i; j++)
		sc->sc_fans[j].fan.zone = id[j];

	/* Finish setting up fan properties */
	for (j = 0; j < i; j++) {
		sc->sc_fans[j].dev = sc->sc_dev;
		if (sc->sc_fans[j].type == FCU_FAN_RPM) {
			sc->sc_fans[j].fan.min_rpm = 4800 >> fcu_rpm_shift;
			sc->sc_fans[j].fan.max_rpm = 56000 >> fcu_rpm_shift;
			sc->sc_fans[j].setpoint =
			    fcu_fan_get_rpm(&sc->sc_fans[j]);
			sc->sc_fans[j].fan.read = 
			    (int (*)(struct pmac_fan *))(fcu_fan_get_rpm);
			sc->sc_fans[j].fan.set =
			    (int (*)(struct pmac_fan *, int))(fcu_fan_set_rpm);
		} else {
			sc->sc_fans[j].fan.min_rpm = 30;	/* Percent */
			sc->sc_fans[j].fan.max_rpm = 100;
			sc->sc_fans[j].fan.read = NULL;
			sc->sc_fans[j].fan.set =
			    (int (*)(struct pmac_fan *, int))(fcu_fan_set_pwm);
		}
		sc->sc_fans[j].fan.default_rpm = sc->sc_fans[j].fan.max_rpm;
	}

	return (i);
}

static int
fcu_fanrpm_sysctl(SYSCTL_HANDLER_ARGS)
{
	device_t fcu;
	struct fcu_softc *sc;
	struct fcu_fan *fan;
	int rpm = 0, pwm = 0, error = 0;

	fcu = arg1;
	sc = device_get_softc(fcu);
	fan = &sc->sc_fans[arg2 & 0x00ff];
	if (fan->type == FCU_FAN_RPM) {
		rpm = fcu_fan_get_rpm(fan);
		if (rpm < 0)
			return (EIO);
		error = sysctl_handle_int(oidp, &rpm, 0, req);
	} else {
		error = fcu_fan_get_pwm(fcu, fan, &pwm, &rpm);
		if (error < 0)
			return (EIO);

		switch (arg2 & 0xff00) {
		case FCU_PWM_SYSCTL_PWM:
			error = sysctl_handle_int(oidp, &pwm, 0, req);
			break;
		case FCU_PWM_SYSCTL_RPM:
			error = sysctl_handle_int(oidp, &rpm, 0, req);
			break;
		default:
			/* This should never happen */
			return (EINVAL);
		}
	}

	/* We can only read the RPM from a PWM controlled fan, so return. */
	if ((arg2 & 0xff00) == FCU_PWM_SYSCTL_RPM)
		return (0);

	if (error || !req->newptr)
		return (error);

	if (fan->type == FCU_FAN_RPM)
		return (fcu_fan_set_rpm(fan, rpm));
	else
		return (fcu_fan_set_pwm(fan, pwm));
}

static void
fcu_attach_fans(device_t dev)
{
	struct fcu_softc *sc;
	struct sysctl_oid *oid, *fanroot_oid;
	struct sysctl_ctx_list *ctx;
	char sysctl_name[32];
	int i, j;

	sc = device_get_softc(dev);

	sc->sc_nfans = 0;

	/* Count the actual number of fans. */
	sc->sc_nfans = fcu_fill_fan_prop(dev);

	device_printf(dev, "%d fans detected!\n", sc->sc_nfans);

	if (sc->sc_nfans == 0) {
		device_printf(dev, "WARNING: No fans detected!\n");
		return;
	}

	sc->sc_fans = malloc(sc->sc_nfans * sizeof(struct fcu_fan), M_FCU,
			     M_WAITOK | M_ZERO);

	ctx = device_get_sysctl_ctx(dev);
	fanroot_oid = SYSCTL_ADD_NODE(ctx,
	    SYSCTL_CHILDREN(device_get_sysctl_tree(dev)), OID_AUTO, "fans",
	    CTLFLAG_RD, 0, "FCU Fan Information");

	/* Now we can fill the properties into the allocated struct. */
	sc->sc_nfans = fcu_fill_fan_prop(dev);

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

		if (sc->sc_fans[i].type == FCU_FAN_RPM) {
			oid = SYSCTL_ADD_NODE(ctx, SYSCTL_CHILDREN(fanroot_oid),
					      OID_AUTO, sysctl_name,
					      CTLFLAG_RD, 0, "Fan Information");
			SYSCTL_ADD_INT(ctx, SYSCTL_CHILDREN(oid), OID_AUTO,
				       "minrpm", CTLFLAG_RD,
				       &(sc->sc_fans[i].fan.min_rpm), 0,
				       "Minimum allowed RPM");
			SYSCTL_ADD_INT(ctx, SYSCTL_CHILDREN(oid), OID_AUTO,
				       "maxrpm", CTLFLAG_RD,
				       &(sc->sc_fans[i].fan.max_rpm), 0,
				       "Maximum allowed RPM");
			/* I use i to pass the fan id. */
			SYSCTL_ADD_PROC(ctx, SYSCTL_CHILDREN(oid), OID_AUTO,
					"rpm", CTLTYPE_INT | CTLFLAG_RW, dev, i,
					fcu_fanrpm_sysctl, "I", "Fan RPM");
		} else {
			fcu_fan_get_pwm(dev, &sc->sc_fans[i],
					&sc->sc_fans[i].setpoint,
					&sc->sc_fans[i].rpm);

			oid = SYSCTL_ADD_NODE(ctx, SYSCTL_CHILDREN(fanroot_oid),
					      OID_AUTO, sysctl_name,
					      CTLFLAG_RD, 0, "Fan Information");
			SYSCTL_ADD_INT(ctx, SYSCTL_CHILDREN(oid), OID_AUTO,
				       "minpwm", CTLFLAG_RD,
				       &(sc->sc_fans[i].fan.min_rpm), 0,
				       "Minimum allowed PWM in %");
			SYSCTL_ADD_INT(ctx, SYSCTL_CHILDREN(oid), OID_AUTO,
				       "maxpwm", CTLFLAG_RD,
				       &(sc->sc_fans[i].fan.max_rpm), 0,
				       "Maximum allowed PWM in %");
			/* I use i to pass the fan id or'ed with the type
			 * of info I want to display/modify.
			 */
			SYSCTL_ADD_PROC(ctx, SYSCTL_CHILDREN(oid), OID_AUTO,
					"pwm", CTLTYPE_INT | CTLFLAG_RW, dev,
					FCU_PWM_SYSCTL_PWM | i,
					fcu_fanrpm_sysctl, "I", "Fan PWM in %");
			SYSCTL_ADD_PROC(ctx, SYSCTL_CHILDREN(oid), OID_AUTO,
					"rpm", CTLTYPE_INT | CTLFLAG_RD, dev,
					FCU_PWM_SYSCTL_RPM | i,
					fcu_fanrpm_sysctl, "I", "Fan RPM");
		}
	}

	/* Dump fan location, type & RPM. */
	if (bootverbose) {
		device_printf(dev, "Fans\n");
		for (i = 0; i < sc->sc_nfans; i++) {
			device_printf(dev, "Location: %s type: %d ID: %d "
				      "RPM: %d\n", sc->sc_fans[i].fan.name,
				      sc->sc_fans[i].type, sc->sc_fans[i].id,
				      (sc->sc_fans[i].type == FCU_FAN_RPM) ?
				      sc->sc_fans[i].setpoint :
				      sc->sc_fans[i].rpm );
	    }
	}
}
