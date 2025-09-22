/*	$OpenBSD: bmc150.c,v 1.2 2022/04/06 18:59:28 naddy Exp $	*/

/*
 * Copyright (c) 2017 Mark Kettenis
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

#define BGW_CHIPID		0x00
#define  BGW_CHIPID_BMA222E	0xf8
#define  BGW_CHIPID_BMA250E	0xf9
#define  BGW_CHIPID_BMC150	0xfa
#define  BGW_CHIPID_BMI055	0xfa
#define  BGW_CHIPID_BMA255	0xfa
#define  BGW_CHIPID_BMA280	0xfb
#define ACCD_X_LSB		0x02
#define ACCD_X_MSB		0x03
#define ACCD_Y_LSB		0x04
#define ACCD_Y_MSB		0x05
#define ACCD_Z_LSB		0x06
#define ACCD_Z_MSB		0x07
#define ACCD_TEMP		0x08

#define BGW_NUM_SENSORS		4

#define BGW_SENSOR_XACCEL	0
#define BGW_SENSOR_YACCEL	1
#define BGW_SENSOR_ZACCEL	2
#define BGW_SENSOR_TEMP		3

struct bgw_softc {
	struct device sc_dev;
	i2c_tag_t sc_tag;
	i2c_addr_t sc_addr;

	uint8_t sc_temp0;

	struct ksensor sc_sensor[BGW_NUM_SENSORS];
	struct ksensordev sc_sensordev;
};

int	bgw_match(struct device *, void *, void *);
void	bgw_attach(struct device *, struct device *, void *);

const struct cfattach bgw_ca = {
	sizeof(struct bgw_softc), bgw_match, bgw_attach
};

struct cfdriver bgw_cd = {
	NULL, "bgw", DV_DULL
};

void	bgw_refresh(void *);

int
bgw_match(struct device *parent, void *match, void *aux)
{
	struct i2c_attach_args *ia = aux;

	if (strcmp(ia->ia_name, "BSBA0150") == 0 ||
	    strcmp(ia->ia_name, "BMC150A") == 0 ||
	    strcmp(ia->ia_name, "BMI055A") == 0 ||
	    strcmp(ia->ia_name, "BMA0255") == 0 ||
	    strcmp(ia->ia_name, "BMA250E") == 0 ||
	    strcmp(ia->ia_name, "BMA222E") == 0 ||
	    strcmp(ia->ia_name, "BMA0280") == 0 ||
	    strcmp(ia->ia_name, "BOSC0200") == 0)
		return 1;
	return 0;
}

void
bgw_attach(struct device *parent, struct device *self, void *aux)
{
	struct bgw_softc *sc = (struct bgw_softc *)self;
	struct i2c_attach_args *ia = aux;
	uint8_t cmd, data;
	int i;

	sc->sc_tag = ia->ia_tag;
	sc->sc_addr = ia->ia_addr;

	iic_acquire_bus(sc->sc_tag, 0);
	cmd = BGW_CHIPID;
	if (iic_exec(sc->sc_tag, I2C_OP_READ_WITH_STOP,
	    sc->sc_addr, &cmd, sizeof cmd, &data, sizeof data, 0)) {
		iic_release_bus(sc->sc_tag, 0);
		printf(": can't read chip ID\n");
		return;
	}
	iic_release_bus(sc->sc_tag, 0);

	switch (data) {
	case BGW_CHIPID_BMA222E:
		sc->sc_temp0 = 23;
		printf(": BMA222E");
		break;
	case BGW_CHIPID_BMA250E:
		sc->sc_temp0 = 23;
		printf(": BMA250E");
		break;
	case BGW_CHIPID_BMC150:
		sc->sc_temp0 = 24;
		printf(": BMC150");
		break;
	case BGW_CHIPID_BMA280:
		sc->sc_temp0 = 23;
		printf(": BMA280");
		break;
	}

	sc->sc_sensor[BGW_SENSOR_XACCEL].type = SENSOR_INTEGER;
	strlcpy(sc->sc_sensor[BGW_SENSOR_XACCEL].desc, "X_ACCEL",
	    sizeof(sc->sc_sensor[BGW_SENSOR_XACCEL].desc));
	sc->sc_sensor[BGW_SENSOR_YACCEL].type = SENSOR_INTEGER;
	strlcpy(sc->sc_sensor[BGW_SENSOR_YACCEL].desc, "Y_ACCEL",
	    sizeof(sc->sc_sensor[BGW_SENSOR_YACCEL].desc));
	sc->sc_sensor[BGW_SENSOR_ZACCEL].type = SENSOR_INTEGER;
	strlcpy(sc->sc_sensor[BGW_SENSOR_ZACCEL].desc, "Z_ACCEL",
	    sizeof(sc->sc_sensor[BGW_SENSOR_ZACCEL].desc));
	sc->sc_sensor[BGW_SENSOR_TEMP].type = SENSOR_TEMP;

	strlcpy(sc->sc_sensordev.xname, sc->sc_dev.dv_xname,
	    sizeof(sc->sc_sensordev.xname));
	for (i = 0; i < BGW_NUM_SENSORS; i++)
		sensor_attach(&sc->sc_sensordev, &sc->sc_sensor[i]);
	sensordev_install(&sc->sc_sensordev);

	if (sensor_task_register(sc, bgw_refresh, 5) == NULL) {
		printf(", unable to register update task\n");
		return;
	}

	printf("\n");
}

void
bgw_refresh(void *arg)
{
	struct bgw_softc *sc = arg;
	uint8_t cmd, data[7];
	uint8_t lsb;
	int8_t msb;
	int8_t temp;
	int i;

	iic_acquire_bus(sc->sc_tag, 0);
	cmd = ACCD_X_LSB;
	if (iic_exec(sc->sc_tag, I2C_OP_READ_WITH_STOP,
	    sc->sc_addr, &cmd, sizeof cmd, &data, sizeof data, 0)) {
		iic_release_bus(sc->sc_tag, 0);
		for (i = 0; i < BGW_NUM_SENSORS; i++)
			sc->sc_sensor[i].flags |= SENSOR_FINVALID;
		return;
	}
	iic_release_bus(sc->sc_tag, 0);

	lsb = data[ACCD_X_LSB - ACCD_X_LSB];
	msb = data[ACCD_X_MSB - ACCD_X_LSB];
	sc->sc_sensor[BGW_SENSOR_XACCEL].value = (msb << 4) | (lsb >> 4);
	sc->sc_sensor[BGW_SENSOR_XACCEL].flags &= ~SENSOR_FINVALID;

	lsb = data[ACCD_Y_LSB - ACCD_X_LSB];
	msb = data[ACCD_Y_MSB - ACCD_X_LSB];
	sc->sc_sensor[BGW_SENSOR_YACCEL].value = (msb << 4) | (lsb >> 4);
	sc->sc_sensor[BGW_SENSOR_YACCEL].flags &= ~SENSOR_FINVALID;

	lsb = data[ACCD_Z_LSB - ACCD_X_LSB];
	msb = data[ACCD_Z_MSB - ACCD_X_LSB];
	sc->sc_sensor[BGW_SENSOR_ZACCEL].value = (msb << 4) | (lsb >> 4);
	sc->sc_sensor[BGW_SENSOR_ZACCEL].flags &= ~SENSOR_FINVALID;

	temp = data[ACCD_TEMP - ACCD_X_LSB];
	sc->sc_sensor[BGW_SENSOR_TEMP].value =
	    273150000 + (sc->sc_temp0 + temp) * 1000000;
	sc->sc_sensor[BGW_SENSOR_TEMP].flags &= ~SENSOR_FINVALID;
}
