/*	$OpenBSD: lis331dl.c,v 1.2 2022/04/06 18:59:28 naddy Exp $	*/

/*
 * Copyright (c) 2009 Constantine A. Murenin <cnst+openbsd@bugmail.mojo.ru>
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

/*
 * STMicroelectronics LIS331DL
 *	MEMS motion sensor
 * http://www.stm.com/stonline/products/literature/ds/13951.pdf
 * April 2008
 */

/* 3-axis accelerometer */
#define LISA_NUM_AXIS	3
static const struct {
	const char	*name;
	const uint8_t	reg;
} lisa_axis[LISA_NUM_AXIS] = {
	{ "OUT_X", 0x29 },
	{ "OUT_Y", 0x2b },
	{ "OUT_Z", 0x2d }
};

struct lisa_softc {
	struct device	sc_dev;
	i2c_tag_t	sc_tag;
	i2c_addr_t	sc_addr;

	struct ksensor	sc_sensors[LISA_NUM_AXIS];
	struct ksensordev sc_sensordev;
};


int	lisa_match(struct device *, void *, void *);
void	lisa_attach(struct device *, struct device *, void *);
void	lisa_refresh(void *);

uint8_t	lisa_readreg(struct lisa_softc *, uint8_t);
void	lisa_writereg(struct lisa_softc *, uint8_t, uint8_t);


const struct cfattach lisa_ca = {
	sizeof(struct lisa_softc), lisa_match, lisa_attach
};

struct cfdriver lisa_cd = {
	NULL, "lisa", DV_DULL
};


int
lisa_match(struct device *parent, void *match, void *aux)
{
	struct i2c_attach_args *ia = aux;

	if (strcmp(ia->ia_name, "lis331dl") == 0)
		return 1;
	return 0;
}

void
lisa_attach(struct device *parent, struct device *self, void *aux)
{
	struct lisa_softc *sc = (struct lisa_softc *)self;
	struct i2c_attach_args *ia = aux;
	int i;

	sc->sc_tag = ia->ia_tag;
	sc->sc_addr = ia->ia_addr;

	printf(": %s", ia->ia_name);

	strlcpy(sc->sc_sensordev.xname, sc->sc_dev.dv_xname,
	    sizeof(sc->sc_sensordev.xname));

	for (i = 0; i < LISA_NUM_AXIS; i++) {
		strlcpy(sc->sc_sensors[i].desc, lisa_axis[i].name,
		    sizeof(sc->sc_sensors[i].desc));
		sc->sc_sensors[i].type = SENSOR_INTEGER;
		sensor_attach(&sc->sc_sensordev, &sc->sc_sensors[i]);
	}

	if (sensor_task_register(sc, lisa_refresh, 1) == NULL) {
		printf(", unable to register update task\n");
		return;
	}

	sensordev_install(&sc->sc_sensordev);
	printf("\n");
}

void
lisa_refresh(void *arg)
{
	struct lisa_softc *sc = arg;
	struct ksensor *s = sc->sc_sensors;
	int i;

	iic_acquire_bus(sc->sc_tag, 0);
	for (i = 0; i < LISA_NUM_AXIS; i++)
		s[i].value = (int8_t)lisa_readreg(sc, lisa_axis[i].reg);
	iic_release_bus(sc->sc_tag, 0);
}

uint8_t
lisa_readreg(struct lisa_softc *sc, uint8_t reg)
{
	uint8_t data;

	iic_exec(sc->sc_tag, I2C_OP_READ_WITH_STOP,
	    sc->sc_addr, &reg, sizeof reg, &data, sizeof data, 0);

	return data;
}

void
lisa_writereg(struct lisa_softc *sc, uint8_t reg, uint8_t data)
{
	iic_exec(sc->sc_tag, I2C_OP_WRITE_WITH_STOP,
	    sc->sc_addr, &reg, sizeof reg, &data, sizeof data, 0);
}
