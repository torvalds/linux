/*	$OpenBSD: adm1030.c,v 1.10 2022/04/06 18:59:28 naddy Exp $	*/

/*
 * Copyright (c) 2005 Theo de Raadt
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

/* adm 1030 registers */
#define ADM1030_INT_TEMP	0x0a
#define ADM1030_EXT_TEMP	0x0b
#define ADM1030_FAN		0x08
#define ADM1030_FANC		0x20
#define  ADM1024_FANC_VAL(x)	(x >> 6)

/* Sensors */
#define ADMTMP_INT		0
#define ADMTMP_EXT		1
#define ADMTMP_FAN		2
#define ADMTMP_NUM_SENSORS	3

struct admtmp_softc {
	struct device	sc_dev;
	i2c_tag_t	sc_tag;
	i2c_addr_t	sc_addr;
	int		sc_fanmul;

	struct ksensor	sc_sensor[ADMTMP_NUM_SENSORS];
	struct ksensordev sc_sensordev;
};

int	admtmp_match(struct device *, void *, void *);
void	admtmp_attach(struct device *, struct device *, void *);
void	admtmp_refresh(void *);

const struct cfattach admtmp_ca = {
	sizeof(struct admtmp_softc), admtmp_match, admtmp_attach
};

struct cfdriver admtmp_cd = {
	NULL, "admtmp", DV_DULL
};

int
admtmp_match(struct device *parent, void *match, void *aux)
{
	struct i2c_attach_args *ia = aux;

	if (strcmp(ia->ia_name, "adm1030") == 0)
		return (1);
	return (0);
}

void
admtmp_attach(struct device *parent, struct device *self, void *aux)
{
	struct admtmp_softc *sc = (struct admtmp_softc *)self;
	struct i2c_attach_args *ia = aux;
	u_int8_t cmd, data;
	int i;

	sc->sc_tag = ia->ia_tag;
	sc->sc_addr = ia->ia_addr;

	cmd = ADM1030_FANC;
	if (iic_exec(sc->sc_tag, I2C_OP_READ_WITH_STOP,
	    sc->sc_addr, &cmd, sizeof cmd, &data, sizeof data, 0)) {
		printf(": unable to read fan setting\n");
		return;
	}

	sc->sc_fanmul = 11250/8 * (1 << ADM1024_FANC_VAL(data)) * 60;

	/* Initialize sensor data. */
	strlcpy(sc->sc_sensordev.xname, sc->sc_dev.dv_xname,
	    sizeof(sc->sc_sensordev.xname));

	sc->sc_sensor[ADMTMP_INT].type = SENSOR_TEMP;
	strlcpy(sc->sc_sensor[ADMTMP_INT].desc, "Internal",
	    sizeof(sc->sc_sensor[ADMTMP_INT].desc));

	sc->sc_sensor[ADMTMP_EXT].type = SENSOR_TEMP;
	strlcpy(sc->sc_sensor[ADMTMP_EXT].desc, "External",
	    sizeof(sc->sc_sensor[ADMTMP_EXT].desc));

	sc->sc_sensor[ADMTMP_FAN].type = SENSOR_FANRPM;

	if (sensor_task_register(sc, admtmp_refresh, 5) == NULL) {
		printf(": unable to register update task\n");
		return;
	}

	for (i = 0; i < ADMTMP_NUM_SENSORS; i++)
		sensor_attach(&sc->sc_sensordev, &sc->sc_sensor[i]);
	sensordev_install(&sc->sc_sensordev);

	printf("\n");
}

void
admtmp_refresh(void *arg)
{
	struct admtmp_softc *sc = arg;
	u_int8_t cmd, data;

	iic_acquire_bus(sc->sc_tag, 0);

	cmd = ADM1030_INT_TEMP;
	if (iic_exec(sc->sc_tag, I2C_OP_READ_WITH_STOP,
	    sc->sc_addr, &cmd, sizeof cmd, &data, sizeof data, 0) == 0)
		sc->sc_sensor[ADMTMP_INT].value = 273150000 + 1000000 * data;

	cmd = ADM1030_EXT_TEMP;
	if (iic_exec(sc->sc_tag, I2C_OP_READ_WITH_STOP,
	    sc->sc_addr, &cmd, sizeof cmd, &data, sizeof data, 0) == 0)
		sc->sc_sensor[ADMTMP_EXT].value = 273150000 + 1000000 * data;

	cmd = ADM1030_FAN;
	if (iic_exec(sc->sc_tag, I2C_OP_READ_WITH_STOP,
	    sc->sc_addr, &cmd, sizeof cmd, &data, sizeof data, 0) == 0) {
		if (data == 0)
			sc->sc_sensor[ADMTMP_FAN].flags |= SENSOR_FINVALID;
		else {
			sc->sc_sensor[ADMTMP_FAN].value =
			    sc->sc_fanmul / (2 * (int)data);
			sc->sc_sensor[ADMTMP_FAN].flags &= ~SENSOR_FINVALID;
		}
	}

	iic_release_bus(sc->sc_tag, 0);
}
