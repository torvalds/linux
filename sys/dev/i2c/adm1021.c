/*	$OpenBSD: adm1021.c,v 1.29 2022/04/06 18:59:28 naddy Exp $	*/

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

/* ADM 1021 registers */
#define ADM1021_INT_TEMP	0x00
#define ADM1021_EXT_TEMP	0x01
#define ADM1021_STATUS		0x02
#define  ADM1021_STATUS_INVAL	0x7f
#define  ADM1021_STATUS_NOEXT	0x40
#define ADM1021_CONFIG_READ	0x03
#define ADM1021_CONFIG_WRITE	0x09
#define  ADM1021_CONFIG_RUN	0x40
#define ADM1021_COMPANY		0xfe	/* contains 0x41 */
#define ADM1021_STEPPING	0xff	/* contains 0x3? */

/* Sensors */
#define ADMTEMP_EXT		0
#define ADMTEMP_INT		1
#define ADMTEMP_NUM_SENSORS	2

struct admtemp_softc {
	struct device	sc_dev;
	i2c_tag_t	sc_tag;
	i2c_addr_t	sc_addr;

	struct ksensor	sc_sensor[ADMTEMP_NUM_SENSORS];
	struct ksensordev sc_sensordev;
	int		sc_noexternal;
};

int	admtemp_match(struct device *, void *, void *);
void	admtemp_attach(struct device *, struct device *, void *);
void	admtemp_refresh(void *);

const struct cfattach admtemp_ca = {
	sizeof(struct admtemp_softc), admtemp_match, admtemp_attach
};

struct cfdriver admtemp_cd = {
	NULL, "admtemp", DV_DULL
};

int
admtemp_match(struct device *parent, void *match, void *aux)
{
	struct i2c_attach_args *ia = aux;

	if (strcmp(ia->ia_name, "adm1021") == 0 ||
	    strcmp(ia->ia_name, "adm1023") == 0 ||
	    strcmp(ia->ia_name, "adm1032") == 0 ||
	    strcmp(ia->ia_name, "g781") == 0 ||
	    strcmp(ia->ia_name, "g781-1") == 0 ||
	    strcmp(ia->ia_name, "gl523sm") == 0 ||
	    strcmp(ia->ia_name, "max1617") == 0 ||
	    strcmp(ia->ia_name, "sa56004x") == 0 ||
	    strcmp(ia->ia_name, "xeontemp") == 0)
		return (1);
	return (0);
}

void
admtemp_attach(struct device *parent, struct device *self, void *aux)
{
	struct admtemp_softc *sc = (struct admtemp_softc *)self;
	struct i2c_attach_args *ia = aux;
	u_int8_t cmd, data, stat;
	int xeon = 0, i;

	sc->sc_tag = ia->ia_tag;
	sc->sc_addr = ia->ia_addr;

	if (strcmp(ia->ia_name, "xeontemp") == 0) {
		printf(": Xeon");
		xeon = 1;
	} else
		printf(": %s", ia->ia_name);

	iic_acquire_bus(sc->sc_tag, 0);
	cmd = ADM1021_CONFIG_READ;
	if (iic_exec(sc->sc_tag, I2C_OP_READ_WITH_STOP,
	    sc->sc_addr, &cmd, sizeof cmd, &data, sizeof data, 0)) {
		iic_release_bus(sc->sc_tag, 0);
		printf(", cannot get control register\n");
		return;
	}
	if (data & ADM1021_CONFIG_RUN) {
		cmd = ADM1021_STATUS;
		if (iic_exec(sc->sc_tag, I2C_OP_READ_WITH_STOP,
		    sc->sc_addr, &cmd, sizeof cmd, &stat, sizeof stat, 0)) {
			iic_release_bus(sc->sc_tag, 0);
			printf(", cannot read status register\n");
			return;
		}
		if ((stat & ADM1021_STATUS_INVAL) == ADM1021_STATUS_INVAL) {
			if (iic_exec(sc->sc_tag, I2C_OP_READ_WITH_STOP,
			    sc->sc_addr, &cmd, sizeof cmd, &stat, sizeof stat, 0)) {
				iic_release_bus(sc->sc_tag, 0);
				printf(", cannot read status register\n");
				return;
			}
		}

		/* means external is dead */
		if ((stat & ADM1021_STATUS_INVAL) != ADM1021_STATUS_INVAL &&
		    (stat & ADM1021_STATUS_NOEXT))
			sc->sc_noexternal = 1;

		data &= ~ADM1021_CONFIG_RUN;
		cmd = ADM1021_CONFIG_WRITE;
		if (iic_exec(sc->sc_tag, I2C_OP_WRITE_WITH_STOP,
		    sc->sc_addr, &cmd, sizeof cmd, &data, sizeof data, 0)) {
			iic_release_bus(sc->sc_tag, 0);
			printf(", cannot set control register\n");
			return;
		}
	}
	iic_release_bus(sc->sc_tag, 0);

	/* Initialize sensor data. */
	strlcpy(sc->sc_sensordev.xname, sc->sc_dev.dv_xname,
	    sizeof(sc->sc_sensordev.xname));

	sc->sc_sensor[ADMTEMP_EXT].type = SENSOR_TEMP;
	strlcpy(sc->sc_sensor[ADMTEMP_EXT].desc,
	    xeon ? "Xeon" : "External",
	    sizeof(sc->sc_sensor[ADMTEMP_EXT].desc));

	sc->sc_sensor[ADMTEMP_INT].type = SENSOR_TEMP;
	strlcpy(sc->sc_sensor[ADMTEMP_INT].desc,
	    xeon ? "Xeon" : "Internal",
	    sizeof(sc->sc_sensor[ADMTEMP_INT].desc));

	if (sensor_task_register(sc, admtemp_refresh, 5) == NULL) {
		printf(", unable to register update task\n");
		return;
	}

	for (i = 0; i < (sc->sc_noexternal ? 1 : ADMTEMP_NUM_SENSORS); i++)
		sensor_attach(&sc->sc_sensordev, &sc->sc_sensor[i]);
	sensordev_install(&sc->sc_sensordev);

	printf("\n");
}

void
admtemp_refresh(void *arg)
{
	struct admtemp_softc *sc = arg;
	u_int8_t cmd;
	int8_t sdata;

	iic_acquire_bus(sc->sc_tag, 0);

	if (sc->sc_noexternal == 0) {
		cmd = ADM1021_EXT_TEMP;
		if (iic_exec(sc->sc_tag, I2C_OP_READ_WITH_STOP, sc->sc_addr,
		    &cmd, sizeof cmd, &sdata, sizeof sdata, 0) == 0) {
			if (sdata == 0x7f) {
				sc->sc_sensor[ADMTEMP_EXT].flags |= SENSOR_FINVALID;
			} else {
				sc->sc_sensor[ADMTEMP_EXT].value =
				    273150000 + 1000000 * sdata;
				sc->sc_sensor[ADMTEMP_EXT].flags &= ~SENSOR_FINVALID;
			}
		}
	} else
		sc->sc_sensor[ADMTEMP_EXT].flags |= SENSOR_FINVALID;


	cmd = ADM1021_INT_TEMP;
	if (iic_exec(sc->sc_tag, I2C_OP_READ_WITH_STOP, sc->sc_addr,
	    &cmd, sizeof cmd, &sdata,  sizeof sdata, 0) == 0) {
		if (sdata == 0x7f) {
			sc->sc_sensor[ADMTEMP_INT].flags |= SENSOR_FINVALID;
		} else {
			sc->sc_sensor[ADMTEMP_INT].value =
			    273150000 + 1000000 * sdata;
			sc->sc_sensor[ADMTEMP_INT].flags &= ~SENSOR_FINVALID;
		}
	}

	iic_release_bus(sc->sc_tag, 0);
}
