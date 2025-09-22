/*	$OpenBSD: gl518sm.c,v 1.7 2022/04/06 18:59:28 naddy Exp $	*/

/*
 * Copyright (c) 2006 Mark Kettenis
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/device.h>
#include <sys/sensors.h>

#include <dev/i2c/i2cvar.h>

/* GL518SM registers */
#define GL518SM_CHIPID		0x00
#define GL518SM_REVISION	0x01
#define GL518SM_VENDORID	0x02
#define GL518SM_CONFIG		0x03
#define  GL518SM_CONFIG_START		0x40
#define  GL518SM_CONFIG_CLEARST		0x20
#define  GL518SM_CONFIG_NOFAN2		0x10
#define GL518SM_TEMP		0x04
#define GL518SM_TEMP_OVER	0x05
#define GL518SM_TEMP_HYST	0x06
#define GL518SM_FAN_COUNT	0x07
#define GL518SM_FAN_LIMIT	0x08
#define GL518SM_VIN1_LIMIT	0x09
#define GL518SM_VIN2_LIMIT	0x0a
#define GL518SM_VIN3_LIMIT	0x0b
#define GL518SM_VDD_LIMIT	0x0c
#define GL518SM_VOLTMETER	0x0d
#define GL518SM_MISC		0x0f
#define GL518SM_ALARM		0x10
#define GL518SM_MASK		0x11
#define GL518SM_INTSTAT		0x12

/* Sensors */
#define GLENV_VIN3		0
#define GLENV_TEMP		1
#define GLENV_FAN1		2
#define GLENV_FAN2		3
#define GLENV_NUM_SENSORS	4

struct glenv_softc {
	struct device sc_dev;

	i2c_tag_t sc_tag;
	i2c_addr_t sc_addr;

	struct ksensor sc_sensor[GLENV_NUM_SENSORS];
	struct ksensordev sc_sensordev;
	int	sc_fan1_div, sc_fan2_div;
};

int	glenv_match(struct device *, void *, void *);
void	glenv_attach(struct device *, struct device *, void *);

void	glenv_refresh(void *);

const struct cfattach glenv_ca = {
	sizeof(struct glenv_softc), glenv_match, glenv_attach
};

struct cfdriver glenv_cd = {
	NULL, "glenv", DV_DULL
};

int
glenv_match(struct device *parent, void *match, void *aux)
{
	struct i2c_attach_args *ia = aux;

	if (strcmp(ia->ia_name, "gl518sm") == 0)
		return (1);
	return (0);
}

void
glenv_attach(struct device *parent, struct device *self, void *aux)
{
	struct glenv_softc *sc = (struct glenv_softc *)self;
	struct i2c_attach_args *ia = aux;
	u_int8_t cmd, data;
	int i;

	sc->sc_tag = ia->ia_tag;
	sc->sc_addr = ia->ia_addr;

	iic_acquire_bus(sc->sc_tag, 0);

	cmd = GL518SM_REVISION;
	if (iic_exec(sc->sc_tag, I2C_OP_READ_WITH_STOP,
	    sc->sc_addr, &cmd, sizeof cmd, &data, sizeof data, 0)) {
		iic_release_bus(sc->sc_tag, 0);
		printf(": cannot read revision register\n");
		return;
	}
	
	printf(": GL518SM rev 0x%02x", data);

	cmd = GL518SM_MISC;
	if (iic_exec(sc->sc_tag, I2C_OP_READ_WITH_STOP,
	    sc->sc_addr, &cmd, sizeof cmd, &data, sizeof data, 0)) {
		iic_release_bus(sc->sc_tag, 0);
		printf(", cannot read misc register\n");
		return;
	}
	sc->sc_fan1_div = 1 << ((data >> 6) & 0x03);
	sc->sc_fan2_div = 1 << ((data >> 4) & 0x03);

	cmd = GL518SM_CONFIG;
	if (iic_exec(sc->sc_tag, I2C_OP_READ_WITH_STOP,
	    sc->sc_addr, &cmd, sizeof cmd, &data, sizeof data, 0)) {
		iic_release_bus(sc->sc_tag, 0);
		printf(", cannot read configuration register\n");
		return;
	}
	if (data & GL518SM_CONFIG_NOFAN2)
		sc->sc_fan2_div = 0;

	/* Start monitoring and clear interrupt status. */
	data = (data | GL518SM_CONFIG_START | GL518SM_CONFIG_CLEARST);
	if (iic_exec(sc->sc_tag, I2C_OP_WRITE_WITH_STOP,
	    sc->sc_addr, &cmd, sizeof cmd, &data, sizeof data, 0)) {
			iic_release_bus(sc->sc_tag, 0);
			printf(", cannot write configuration register\n");
			return;
	}

	iic_release_bus(sc->sc_tag, 0);

	/* Initialize sensor data. */
	strlcpy(sc->sc_sensordev.xname, sc->sc_dev.dv_xname,
	    sizeof(sc->sc_sensordev.xname));

	sc->sc_sensor[GLENV_VIN3].type = SENSOR_VOLTS_DC;

	sc->sc_sensor[GLENV_TEMP].type = SENSOR_TEMP;

	sc->sc_sensor[GLENV_FAN1].type = SENSOR_FANRPM;

	sc->sc_sensor[GLENV_FAN2].type = SENSOR_FANRPM;
	if (sc->sc_fan2_div == -1)
		sc->sc_sensor[GLENV_FAN2].flags |= SENSOR_FINVALID;

	if (sensor_task_register(sc, glenv_refresh, 5) == NULL) {
		printf(", unable to register update task\n");
		return;
	}

	for (i = 0; i < GLENV_NUM_SENSORS; i++)
		sensor_attach(&sc->sc_sensordev, &sc->sc_sensor[i]);
	sensordev_install(&sc->sc_sensordev);

	printf("\n");
}

void
glenv_refresh(void *arg)
{
	struct glenv_softc *sc = arg;
	u_int8_t cmd, data, data2[2];
	u_int tmp;

	iic_acquire_bus(sc->sc_tag, 0);

	cmd = GL518SM_VOLTMETER;
	if (iic_exec(sc->sc_tag, I2C_OP_READ_WITH_STOP,
	    sc->sc_addr, &cmd, sizeof cmd, &data, sizeof data, 0)) {
		sc->sc_sensor[GLENV_VIN3].flags |= SENSOR_FINVALID;
	} else {
		sc->sc_sensor[GLENV_VIN3].flags &= ~SENSOR_FINVALID;
		sc->sc_sensor[GLENV_VIN3].value = data * 19000;
	}
	
	cmd = GL518SM_TEMP;
	if (iic_exec(sc->sc_tag, I2C_OP_READ_WITH_STOP,
	    sc->sc_addr, &cmd, sizeof cmd, &data, sizeof data, 0)) {
		sc->sc_sensor[GLENV_TEMP].flags |= SENSOR_FINVALID;
	} else {
		sc->sc_sensor[GLENV_TEMP].flags &= ~SENSOR_FINVALID;
		sc->sc_sensor[GLENV_TEMP].value =
			(data - 119) * 1000000 + 273150000;
	}

	cmd = GL518SM_FAN_COUNT;
	if (iic_exec(sc->sc_tag, I2C_OP_READ_WITH_STOP,
	    sc->sc_addr, &cmd, sizeof cmd, &data2, sizeof data2, 0)) {
		sc->sc_sensor[GLENV_FAN1].flags |= SENSOR_FINVALID;
		sc->sc_sensor[GLENV_FAN2].flags |= SENSOR_FINVALID;
	} else {
		sc->sc_sensor[GLENV_FAN1].flags &= ~SENSOR_FINVALID;
		tmp = data2[0] * sc->sc_fan1_div * 2;
		if (tmp == 0)
			sc->sc_sensor[GLENV_FAN1].flags |= SENSOR_FINVALID;
		else
			sc->sc_sensor[GLENV_FAN1].value = 960000 / tmp;

		sc->sc_sensor[GLENV_FAN2].flags &= ~SENSOR_FINVALID;
		tmp = data2[1] * sc->sc_fan2_div * 2;
		if (tmp == 0)
			sc->sc_sensor[GLENV_FAN2].flags |= SENSOR_FINVALID;
		else
			sc->sc_sensor[GLENV_FAN2].value = 960000 / tmp;
	}

	iic_release_bus(sc->sc_tag, 0);
}
