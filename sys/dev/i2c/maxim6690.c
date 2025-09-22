/*	$OpenBSD: maxim6690.c,v 1.17 2022/04/06 18:59:28 naddy Exp $	*/

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

/* Maxim MAX6642/90 registers */
#define MAX6690_INT_TEMP	0x00
#define MAX6690_EXT_TEMP	0x01
#define MAX6690_INT_TEMP2	0x11
#define MAX6690_EXT_TEMP2	0x10
#define MAX6690_STATUS		0x02
#define MAX6690_DEVID		0xfe
#define MAX6690_REVISION	0xff	/* absent on MAX6642 */

#define MAX6642_TEMP_INVALID	0xff	/* sensor disconnected */
#define MAX6690_TEMP_INVALID	0x80	/* sensor disconnected */
#define MAX6690_TEMP_INVALID2	0x7f	/* open-circuit without pull-up */
#define LM90_TEMP_INVALID	0x7f	/* sensor disconnected */

#define MAX6642_TEMP2_MASK	0xc0	/* significant bits */
#define MAX6690_TEMP2_MASK	0xe0	/* significant bits */
#define LM90_TEMP2_MASK		0xe0	/* significant bits */

/* Sensors */
#define MAXTMP_INT		0
#define MAXTMP_EXT		1
#define MAXTMP_NUM_SENSORS	2

struct maxtmp_softc {
	struct device sc_dev;
	i2c_tag_t sc_tag;
	i2c_addr_t sc_addr;

	u_int8_t sc_temp_invalid[2];
	u_int8_t sc_temp2_mask;

	struct ksensor sc_sensor[MAXTMP_NUM_SENSORS];
	struct ksensordev sc_sensordev;
};

int	maxtmp_match(struct device *, void *, void *);
void	maxtmp_attach(struct device *, struct device *, void *);
void	maxtmp_refresh(void *);

const struct cfattach maxtmp_ca = {
	sizeof(struct maxtmp_softc), maxtmp_match, maxtmp_attach
};

struct cfdriver maxtmp_cd = {
	NULL, "maxtmp", DV_DULL
};

int
maxtmp_match(struct device *parent, void *match, void *aux)
{
	struct i2c_attach_args *ia = aux;

	if (strcmp(ia->ia_name, "max6642") == 0 ||
	    strcmp(ia->ia_name, "max6690") == 0 ||
	    strcmp(ia->ia_name, "max6657") == 0 ||
	    strcmp(ia->ia_name, "max6658") == 0 ||
	    strcmp(ia->ia_name, "max6659") == 0 ||
	    strcmp(ia->ia_name, "lm63") == 0 ||
	    strcmp(ia->ia_name, "lm86") == 0 ||
	    strcmp(ia->ia_name, "lm89") == 0 ||
	    strcmp(ia->ia_name, "lm89-1") == 0 ||
	    strcmp(ia->ia_name, "lm90") == 0 ||
	    strcmp(ia->ia_name, "lm99") == 0 ||
	    strcmp(ia->ia_name, "lm99-1") == 0)
		return (1);
	return (0);
}

void
maxtmp_attach(struct device *parent, struct device *self, void *aux)
{
	struct maxtmp_softc *sc = (struct maxtmp_softc *)self;
	struct i2c_attach_args *ia = aux;
	int i;

	sc->sc_tag = ia->ia_tag;
	sc->sc_addr = ia->ia_addr;

	if (strcmp(ia->ia_name, "max6642") == 0) {
		sc->sc_temp_invalid[0] = MAX6642_TEMP_INVALID;
		sc->sc_temp_invalid[1] = MAX6642_TEMP_INVALID;
		sc->sc_temp2_mask = MAX6642_TEMP2_MASK;
	} else if (strcmp(ia->ia_name, "max6690") == 0 ||
	    strcmp(ia->ia_name, "max6657") == 0 ||
	    strcmp(ia->ia_name, "max6658") == 0 ||
	    strcmp(ia->ia_name, "max6659") == 0) {
		sc->sc_temp_invalid[0] = MAX6690_TEMP_INVALID;
		sc->sc_temp_invalid[1] = MAX6690_TEMP_INVALID2;
		sc->sc_temp2_mask = MAX6690_TEMP2_MASK;
	} else {
		sc->sc_temp_invalid[0] = LM90_TEMP_INVALID;
		sc->sc_temp_invalid[1] = LM90_TEMP_INVALID;
		sc->sc_temp2_mask = LM90_TEMP2_MASK;
	}
	printf(": %s", ia->ia_name);

	/* Initialize sensor data. */
	strlcpy(sc->sc_sensordev.xname, sc->sc_dev.dv_xname,
	    sizeof(sc->sc_sensordev.xname));

	sc->sc_sensor[MAXTMP_INT].type = SENSOR_TEMP;
	strlcpy(sc->sc_sensor[MAXTMP_INT].desc, "Internal",
	    sizeof(sc->sc_sensor[MAXTMP_INT].desc));

	sc->sc_sensor[MAXTMP_EXT].type = SENSOR_TEMP;
	strlcpy(sc->sc_sensor[MAXTMP_EXT].desc, "External",
	    sizeof(sc->sc_sensor[MAXTMP_EXT].desc));

	if (sensor_task_register(sc, maxtmp_refresh, 5) == NULL) {
		printf(", unable to register update task\n");
		return;
	}

	for (i = 0; i < MAXTMP_NUM_SENSORS; i++)
		sensor_attach(&sc->sc_sensordev, &sc->sc_sensor[i]);
	sensordev_install(&sc->sc_sensordev);

	printf("\n");
}

void	maxtmp_readport(struct maxtmp_softc *, u_int8_t, u_int8_t, int);

void
maxtmp_readport(struct maxtmp_softc *sc, u_int8_t cmd1, u_int8_t cmd2,
    int index)
{
	u_int8_t data, data2;

	if (iic_exec(sc->sc_tag, I2C_OP_READ_WITH_STOP,
	    sc->sc_addr, &cmd1, sizeof cmd1, &data, sizeof data, 0))
		goto invalid;
	if (data == sc->sc_temp_invalid[0] || data == sc->sc_temp_invalid[1])
		goto invalid;
	if (iic_exec(sc->sc_tag, I2C_OP_READ_WITH_STOP,
	    sc->sc_addr, &cmd2, sizeof cmd2, &data2, sizeof data2, 0))
		goto invalid;

	/* Set any meaningless bits to zero. */
	data2 &= sc->sc_temp2_mask;

	sc->sc_sensor[index].value = 273150000 +
	    1000000 * data + (data2 >> 5) * 1000000 / 8;
	return;

invalid:
	sc->sc_sensor[index].flags |= SENSOR_FINVALID;
}

void
maxtmp_refresh(void *arg)
{
	struct maxtmp_softc *sc = arg;

	iic_acquire_bus(sc->sc_tag, 0);

	maxtmp_readport(sc, MAX6690_INT_TEMP, MAX6690_INT_TEMP2, MAXTMP_INT);
	maxtmp_readport(sc, MAX6690_EXT_TEMP, MAX6690_EXT_TEMP2, MAXTMP_EXT);

	iic_release_bus(sc->sc_tag, 0);
}
