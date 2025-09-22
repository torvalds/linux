/*	$OpenBSD: thmc50.c,v 1.5 2022/04/06 18:59:28 naddy Exp $	*/

/*
 * Copyright (c) 2007 Theo de Raadt
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

/* THMC50 registers */
#define THMC50_TEMP0		0x27
#define THMC50_TEMP1		0x26
#define THMC50_TEMP2		0x20

/* Sensors */
#define THMC_TEMP0		0
#define THMC_TEMP1		1
#define THMC_TEMP2		2
#define THMC_NUM_SENSORS	3

struct thmc_softc {
	struct device	sc_dev;
	i2c_tag_t	sc_tag;
	i2c_addr_t	sc_addr;

	struct ksensor	sc_sensor[THMC_NUM_SENSORS];
	struct ksensordev sc_sensordev;
};

int	thmc_match(struct device *, void *, void *);
void	thmc_attach(struct device *, struct device *, void *);
void	thmc_refresh(void *);

const struct cfattach thmc_ca = {
	sizeof(struct thmc_softc), thmc_match, thmc_attach
};

struct cfdriver thmc_cd = {
	NULL, "thmc", DV_DULL
};

int
thmc_match(struct device *parent, void *match, void *aux)
{
	struct i2c_attach_args *ia = aux;

	if (strcmp(ia->ia_name, "thmc50") == 0 ||
	    strcmp(ia->ia_name, "adm1022") == 0 ||
	    strcmp(ia->ia_name, "adm1028") == 0)
		return (1);
	return (0);
}

void
thmc_attach(struct device *parent, struct device *self, void *aux)
{
	struct thmc_softc *sc = (struct thmc_softc *)self;
	struct i2c_attach_args *ia = aux;
	int numsensors = THMC_NUM_SENSORS;
	int i;

	sc->sc_tag = ia->ia_tag;
	sc->sc_addr = ia->ia_addr;

	printf(": %s", ia->ia_name);

	/* Initialize sensor data. */
	strlcpy(sc->sc_sensordev.xname, sc->sc_dev.dv_xname,
	    sizeof(sc->sc_sensordev.xname));

	sc->sc_sensor[THMC_TEMP0].type = SENSOR_TEMP;
	strlcpy(sc->sc_sensor[THMC_TEMP0].desc, "Internal",
	    sizeof(sc->sc_sensor[THMC_TEMP0].desc));

	sc->sc_sensor[THMC_TEMP1].type = SENSOR_TEMP;
	strlcpy(sc->sc_sensor[THMC_TEMP1].desc, "External",
	    sizeof(sc->sc_sensor[THMC_TEMP1].desc));

	if (strcmp(ia->ia_name, "adm1022") == 0) {
		/* Only the adm1022 has a THMC50_TEMP2 sensor */
		sc->sc_sensor[THMC_TEMP2].type = SENSOR_TEMP;
		strlcpy(sc->sc_sensor[THMC_TEMP2].desc, "External",
		    sizeof(sc->sc_sensor[THMC_TEMP2].desc));
	} else {
		sc->sc_sensor[THMC_TEMP2].type = -1;
		numsensors--;
	}

	if (sensor_task_register(sc, thmc_refresh, 5) == NULL) {
		printf(", unable to register update task\n");
		return;
	}

	for (i = 0; i < numsensors; i++)
		sensor_attach(&sc->sc_sensordev, &sc->sc_sensor[i]);
	sensordev_install(&sc->sc_sensordev);

	printf("\n");
}

void
thmc_refresh(void *arg)
{
	struct thmc_softc *sc = arg;
	u_int8_t cmd;
	int8_t sdata;

	iic_acquire_bus(sc->sc_tag, 0);

	cmd = THMC50_TEMP0;
	if (iic_exec(sc->sc_tag, I2C_OP_READ_WITH_STOP,
	    sc->sc_addr, &cmd, sizeof cmd, &sdata, sizeof sdata, 0) == 0) {
		sc->sc_sensor[THMC_TEMP0].value = 273150000 + 1000000 * sdata;
		sc->sc_sensor[THMC_TEMP0].flags &= ~SENSOR_FINVALID;
	} else
		sc->sc_sensor[THMC_TEMP0].flags |= SENSOR_FINVALID;

	cmd = THMC50_TEMP1;
	if (iic_exec(sc->sc_tag, I2C_OP_READ_WITH_STOP,
	    sc->sc_addr, &cmd, sizeof cmd, &sdata, sizeof sdata, 0) == 0) {
		sc->sc_sensor[THMC_TEMP1].value = 273150000 + 1000000 * sdata;
		sc->sc_sensor[THMC_TEMP1].flags &= ~SENSOR_FINVALID;
	} else
		sc->sc_sensor[THMC_TEMP1].flags |= SENSOR_FINVALID;

	if (sc->sc_sensor[THMC_TEMP2].type > 0) { 
		cmd = THMC50_TEMP2;
		if (iic_exec(sc->sc_tag, I2C_OP_READ_WITH_STOP,
		    sc->sc_addr, &cmd, sizeof cmd, &sdata, sizeof sdata, 0) == 0) {
			sc->sc_sensor[THMC_TEMP2].value = 273150000 + 1000000 * sdata;
			sc->sc_sensor[THMC_TEMP2].flags &= ~SENSOR_FINVALID;
		} else
			sc->sc_sensor[THMC_TEMP2].flags |= SENSOR_FINVALID;
	}

	iic_release_bus(sc->sc_tag, 0);
}
