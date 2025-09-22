/*	$OpenBSD: tmp451.c,v 1.2 2022/04/06 18:59:28 naddy Exp $	*/
/*
 * Copyright (c) 2018 Mark Kettenis <kettenis@openbsd.org>
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

/* TI TMP451 registers */
#define TMP451_LT_HI		0x00
#define TMP451_RT_HI		0x01
#define TMP451_RT_LO		0x10
#define TMP451_RTOS_HI		0x11
#define TMP451_RTOS_LO		0x12
#define TMP451_LT_LO		0x15

/* Sensors */
#define TITMP_LT		0
#define TITMP_RT		1
#define TITMP_NUM_SENSORS	2

struct titmp_softc {
	struct device sc_dev;
	i2c_tag_t sc_tag;
	i2c_addr_t sc_addr;

	uint64_t sc_offset[TITMP_NUM_SENSORS];
	struct ksensor sc_sensor[TITMP_NUM_SENSORS];
	struct ksensordev sc_sensordev;
};

int	titmp_match(struct device *, void *, void *);
void	titmp_attach(struct device *, struct device *, void *);

const struct cfattach titmp_ca = {
	sizeof(struct titmp_softc), titmp_match, titmp_attach
};

struct cfdriver titmp_cd = {
	NULL, "titmp", DV_DULL
};

void	titmp_read_offsets(struct titmp_softc *);
void	titmp_refresh_sensors(void *);

int
titmp_match(struct device *parent, void *match, void *aux)
{
	struct i2c_attach_args *ia = aux;

	return (strcmp(ia->ia_name, "ti,tmp451") == 0);
}

void
titmp_attach(struct device *parent, struct device *self, void *aux)
{
	struct titmp_softc *sc = (struct titmp_softc *)self;
	struct i2c_attach_args *ia = aux;
	int i;

	sc->sc_tag = ia->ia_tag;
	sc->sc_addr = ia->ia_addr;

	printf("\n");

	titmp_read_offsets(sc);

	/* Register sensors. */
	strlcpy(sc->sc_sensordev.xname, sc->sc_dev.dv_xname,
	    sizeof(sc->sc_sensordev.xname));
	sc->sc_sensor[TITMP_LT].type = SENSOR_TEMP;
	strlcpy(sc->sc_sensor[TITMP_LT].desc, "Local",
	    sizeof(sc->sc_sensor[TITMP_LT].desc));
	sc->sc_sensor[TITMP_RT].type = SENSOR_TEMP;
	strlcpy(sc->sc_sensor[TITMP_RT].desc, "Remote",
	    sizeof(sc->sc_sensor[TITMP_RT].desc));
	for (i = 0; i < TITMP_NUM_SENSORS; i++)
		sensor_attach(&sc->sc_sensordev, &sc->sc_sensor[i]);
	sensordev_install(&sc->sc_sensordev);
	sensor_task_register(sc, titmp_refresh_sensors, 5);
}

void
titmp_read_offsets(struct titmp_softc *sc)
{
	uint8_t cmd, hi, lo;
	int error;

	iic_acquire_bus(sc->sc_tag, I2C_F_POLL);

	cmd = TMP451_RTOS_HI;
	error = iic_exec(sc->sc_tag, I2C_OP_READ_WITH_STOP, sc->sc_addr,
	    &cmd, sizeof(cmd), &hi, sizeof(hi), I2C_F_POLL);
	if (error)
		goto fail;
	cmd = TMP451_RTOS_LO;
	error = iic_exec(sc->sc_tag, I2C_OP_READ_WITH_STOP, sc->sc_addr,
	    &cmd, sizeof(cmd), &lo, sizeof(lo), I2C_F_POLL);
	if (error)
		goto fail;

	sc->sc_offset[TITMP_RT] = 1000000 * hi + (lo >> 4) * 1000000 / 16;

fail:
	iic_release_bus(sc->sc_tag, I2C_F_POLL);
}

void
titmp_read_sensor(struct titmp_softc *sc, uint8_t cmdhi, uint8_t cmdlo,
    int index)
{
	uint8_t hi, lo;

	if (iic_exec(sc->sc_tag, I2C_OP_READ_WITH_STOP,
	    sc->sc_addr, &cmdhi, sizeof(cmdhi), &hi, sizeof(hi), 0))
		goto invalid;
	if (iic_exec(sc->sc_tag, I2C_OP_READ_WITH_STOP,
	    sc->sc_addr, &cmdlo, sizeof(cmdlo), &lo, sizeof(lo), 0))
		goto invalid;

	sc->sc_sensor[index].value = 273150000 +
	    1000000 * hi + (lo >> 4) * 1000000 / 16;
	sc->sc_sensor[index].value += sc->sc_offset[index];
	return;

invalid:
	sc->sc_sensor[index].flags |= SENSOR_FINVALID;
}

void
titmp_refresh_sensors(void *arg)
{
	struct titmp_softc *sc = arg;

	iic_acquire_bus(sc->sc_tag, 0);

	titmp_read_sensor(sc, TMP451_LT_HI, TMP451_LT_LO, TITMP_LT);
	titmp_read_sensor(sc, TMP451_RT_HI, TMP451_RT_LO, TITMP_RT);

	iic_release_bus(sc->sc_tag, 0);
}
