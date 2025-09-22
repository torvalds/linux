/*	$OpenBSD: adt7462.c,v 1.7 2022/04/06 18:59:28 naddy Exp $	*/

/*
 * Copyright (c) 2008 Theo de Raadt
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

#define ADT7462_TEMPL		0x88
#define ADT7462_TEMPH		0x89
#define ADT7462_TEMP1L		0x8a
#define ADT7462_TEMP1H		0x8b
#define ADT7462_TEMP2L		0x8c
#define ADT7462_TEMP2H		0x8d
#define ADT7462_TEMP3L		0x8e
#define ADT7462_TEMP3H		0x8f
#define ADT7262_TACH1L		0x98
#define ADT7262_TACH1H		0x99
#define ADT7262_TACH2L		0x9a
#define ADT7262_TACH2H		0x9b
#define ADT7262_TACH3L		0x9c
#define ADT7262_TACH3H		0x9d
#define ADT7262_TACH4L		0x9e
#define ADT7262_TACH4H		0x9f
#define ADT7262_TACH5L		0xa2
#define ADT7262_TACH5H		0xa3
#define ADT7262_TACH6L		0xa4
#define ADT7262_TACH6H		0xa5
#define ADT7262_TACH7L		0xa6
#define ADT7262_TACH7H		0xa7
#define ADT7262_TACH8L		0xa8
#define ADT7262_TACH8H		0xa9

/* Sensors */
#define ADTFSM_TEMP0		0
#define ADTFSM_TEMP1		1
#define ADTFSM_TEMP2		2
#define ADTFSM_TEMP3		3
#define ADTFSM_TACH1		4
#define ADTFSM_TACH2		5
#define ADTFSM_TACH3		6
#define ADTFSM_TACH4		7
#define ADTFSM_TACH5		8
#define ADTFSM_TACH6		9
#define ADTFSM_TACH7		10
#define ADTFSM_TACH8		11
#define ADTFSM_NUM_SENSORS	12

struct adtfsm_softc {
	struct device	sc_dev;
	i2c_tag_t	sc_tag;
	i2c_addr_t	sc_addr;
	int		sc_fanmul;

	struct ksensor	sc_sensor[ADTFSM_NUM_SENSORS];
	struct ksensordev sc_sensordev;
};

int	adtfsm_match(struct device *, void *, void *);
void	adtfsm_attach(struct device *, struct device *, void *);
void	adtfsm_refresh(void *);

const struct cfattach adtfsm_ca = {
	sizeof(struct adtfsm_softc), adtfsm_match, adtfsm_attach
};

struct cfdriver adtfsm_cd = {
	NULL, "adtfsm", DV_DULL
};

int
adtfsm_match(struct device *parent, void *match, void *aux)
{
	struct i2c_attach_args *ia = aux;

	if (strcmp(ia->ia_name, "adt7462") == 0)
		return (1);
	return (0);
}

void
adtfsm_attach(struct device *parent, struct device *self, void *aux)
{
	struct adtfsm_softc *sc = (struct adtfsm_softc *)self;
	struct i2c_attach_args *ia = aux;
	int i;

	sc->sc_tag = ia->ia_tag;
	sc->sc_addr = ia->ia_addr;

	/* Initialize sensor data. */
	strlcpy(sc->sc_sensordev.xname, sc->sc_dev.dv_xname,
	    sizeof(sc->sc_sensordev.xname));

	for (i = ADTFSM_TEMP0; i <= ADTFSM_TEMP3; i++) {
		sc->sc_sensor[i].type = SENSOR_TEMP;
		sc->sc_sensor[i].flags |= SENSOR_FINVALID;
	}

	strlcpy(sc->sc_sensor[0].desc, "Internal",
	    sizeof(sc->sc_sensor[0].desc));
	for (i = 1; i < 4; i++)
		strlcpy(sc->sc_sensor[i].desc, "External",
		    sizeof(sc->sc_sensor[i].desc));

	for (i = ADTFSM_TACH1; i <= ADTFSM_TACH8; i++) {
		sc->sc_sensor[i].type = SENSOR_FANRPM;
		sc->sc_sensor[i].flags |= SENSOR_FINVALID;
	}

	if (sensor_task_register(sc, adtfsm_refresh, 5) == NULL) {
		printf(", unable to register update task\n");
		return;
	}

	for (i = 0; i < ADTFSM_NUM_SENSORS; i++)
		sensor_attach(&sc->sc_sensordev, &sc->sc_sensor[i]);
	sensordev_install(&sc->sc_sensordev);

	printf("\n");
}

void
adtfsm_refresh(void *arg)
{
	struct adtfsm_softc *sc = arg;
	u_int8_t cmdh, cmdl, datah = 0x01, datal = 0x02;
	struct ksensor *ks;
	u_short ut;
	short t;
	int i;

	iic_acquire_bus(sc->sc_tag, 0);

	for (i = 0; i <= ADTFSM_TEMP3 - ADTFSM_TEMP0; i++) {
		cmdl = ADT7462_TEMPL + i * 2;
		cmdh = ADT7462_TEMPH + i * 2;
		ks = &sc->sc_sensor[ADTFSM_TEMP0 + i];
		if (iic_exec(sc->sc_tag, I2C_OP_READ_WITH_STOP,
		    sc->sc_addr, &cmdl, sizeof cmdl, &datal,
		    sizeof datal, 0) == 0 &&
		    iic_exec(sc->sc_tag, I2C_OP_READ_WITH_STOP,
		    sc->sc_addr, &cmdh, sizeof cmdh, &datah,
		    sizeof datah, 0) == 0) {
			t = (((datah << 8) | datal) >> 6) - (64 << 2);
			ks->value = 273150000 + t * 250000;
			ks->flags &= ~SENSOR_FINVALID;
		} else
			ks->flags |= SENSOR_FINVALID;
	}

	for (i = 0; i <= ADTFSM_TACH8 - ADTFSM_TACH1; i++) {
		cmdl = ADT7262_TACH1L + i * 2;
		cmdh = ADT7262_TACH1H + i * 2;
		ks = &sc->sc_sensor[ADTFSM_TACH1 + i];
		if (iic_exec(sc->sc_tag, I2C_OP_READ_WITH_STOP,
		    sc->sc_addr, &cmdl, sizeof cmdl, &datal,
		    sizeof datal, 0) == 0 &&
		    iic_exec(sc->sc_tag, I2C_OP_READ_WITH_STOP,
		    sc->sc_addr, &cmdh, sizeof cmdh, &datah,
		    sizeof datah, 0) == 0) {
			ut = ((datah << 8) | datal);
			if (ut == 0x0000 || ut == 0xffff)
				ks->flags |= SENSOR_FINVALID;
			else {
				ks->value = 90000 * 60 / ut;
				ks->flags &= ~SENSOR_FINVALID;
			}
		} else
			ks->flags |= SENSOR_FINVALID;
	}

	iic_release_bus(sc->sc_tag, 0);
}
