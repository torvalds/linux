/*	$OpenBSD: tsl2560.c,v 1.8 2022/04/06 18:59:28 naddy Exp $	*/

/*
 * Copyright (c) 2005 Mark Kettenis
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

/* TSL2560/61 registers */
#define TSL2560_REG_CONTROL	0x80
#define  TSL2560_CONTROL_POWER	0x03
#define TSL2560_REG_TIMING	0x81
#define  TSL2560_TIMING_GAIN	0x10 /* high gain (16x) */
#define  TSL2560_TIMING_INTEG0	0x00 /* 13.7ms */
#define  TSL2560_TIMING_INTEG1	0x01 /* 101ms */
#define  TSL2560_TIMING_INTEG2	0x02 /* 402ms */
#define TSL2560_REG_ID		0x8a
#define TSL2560_REG_DATA0	0xac
#define TSL2560_REG_DATA1	0xae

struct tsl_softc {
	struct device sc_dev;
	i2c_tag_t sc_tag;
	i2c_addr_t sc_addr;

	struct ksensor sc_sensor;
	struct ksensordev sc_sensordev;
};

int	tsl_match(struct device *, void *, void *);
void	tsl_attach(struct device *, struct device *, void *);

void	tsl_refresh(void *);
u_int64_t tsl_lux(u_int32_t, u_int32_t);

const struct cfattach tsl_ca = {
	sizeof(struct tsl_softc), tsl_match, tsl_attach
};

struct cfdriver tsl_cd = {
	NULL, "tsl", DV_DULL
};

int
tsl_match(struct device *parent, void *match, void *aux)
{
	struct i2c_attach_args *ia = aux;

	if (strcmp(ia->ia_name, "tsl2560") == 0)
		return (1);
	return (0);
}

void
tsl_attach(struct device *parent, struct device *self, void *aux)
{
	struct tsl_softc *sc = (struct tsl_softc *)self;
	struct i2c_attach_args *ia = aux;
	u_int8_t cmd, data;

	sc->sc_tag = ia->ia_tag;
	sc->sc_addr = ia->ia_addr;

	iic_acquire_bus(sc->sc_tag, 0);
	cmd = TSL2560_REG_CONTROL; data = TSL2560_CONTROL_POWER;
	if (iic_exec(sc->sc_tag, I2C_OP_WRITE_WITH_STOP,
	    sc->sc_addr, &cmd, sizeof cmd, &data, sizeof data, 0)) {
		iic_release_bus(sc->sc_tag, 0);
		printf(": power up failed\n");
		return;
	}
	cmd = TSL2560_REG_TIMING;
	data = TSL2560_TIMING_GAIN | TSL2560_TIMING_INTEG2;
	if (iic_exec(sc->sc_tag, I2C_OP_WRITE_WITH_STOP,
	    sc->sc_addr, &cmd, sizeof cmd, &data, sizeof data, 0)) {
		iic_release_bus(sc->sc_tag, 0);
		printf(": cannot write timing register\n");
		return;
	}
	cmd = TSL2560_REG_ID;
	if (iic_exec(sc->sc_tag, I2C_OP_READ_WITH_STOP,
	    sc->sc_addr, &cmd, sizeof cmd, &data, sizeof data, 0)) {
		iic_release_bus(sc->sc_tag, 0);
		printf(": cannot read ID register\n");
		return;
	}
	iic_release_bus(sc->sc_tag, 0);

	switch (data >> 4) {
	case 0:
		printf(": TSL2560 rev %x", data & 0x0f);
		break;
	case 1:
		printf(": TSL2561 rev %x", data & 0x0f);
		break;
	default:
		printf(": unknown part number %x", data >> 4);
		break;
	}

	/* Initialize sensor data. */
	strlcpy(sc->sc_sensordev.xname, sc->sc_dev.dv_xname,
	    sizeof(sc->sc_sensordev.xname));
	sc->sc_sensor.type = SENSOR_LUX;

	if (sensor_task_register(sc, tsl_refresh, 5) == NULL) {
		printf(": unable to register update task\n");
		return;
	}

	sensor_attach(&sc->sc_sensordev, &sc->sc_sensor);
	sensordev_install(&sc->sc_sensordev);

	printf("\n");
}

void
tsl_refresh(void *arg)
{
	struct tsl_softc *sc = arg;
	u_int8_t cmd, data[2];
	u_int16_t chan0, chan1;

	iic_acquire_bus(sc->sc_tag, 0);
	cmd = TSL2560_REG_DATA0;
	if (iic_exec(sc->sc_tag, I2C_OP_READ_WITH_STOP,
	    sc->sc_addr, &cmd, sizeof cmd, &data, sizeof data, 0)) {
		iic_release_bus(sc->sc_tag, 0);
		sc->sc_sensor.flags |= SENSOR_FINVALID;
		return;
	}
	chan0 = data[1] << 8 | data[0];
	cmd = TSL2560_REG_DATA1;
	if (iic_exec(sc->sc_tag, I2C_OP_READ_WITH_STOP,
	    sc->sc_addr, &cmd, sizeof cmd, &data, sizeof data, 0)) {
		iic_release_bus(sc->sc_tag, 0);
		sc->sc_sensor.flags |= SENSOR_FINVALID;
		return;
	}
	chan1 = data[1] << 8 | data[0];
	iic_release_bus(sc->sc_tag, 0);

	sc->sc_sensor.value = tsl_lux(chan0, chan1);
	sc->sc_sensor.flags &= ~SENSOR_FINVALID;
}

/* Precision for fixed-point math. */
#define TSL2560_RATIO_SCALE	9
#define TSL2560_LUX_SCALE	14

/*
 * The TSL2560/2561 comes in a ChipScale or TMB-6 package and the
 * calibration is slightly different for each package.  The constants
 * below are for the TMB-6 package.
 */

#define TSL2560_K1T	0x0040	/* 0.125 * (1 << TSL2560_RATIO_SCALE) */
#define TSL2560_B1T	0x01f2	/* 0.0304 * (1 << TSL2560_LUX_SCALE) */
#define TSL2560_M1T	0x01be	/* 0.0272 * (1 << TSL2560_LUX_SCALE) */

#define TSL2560_K2T	0x0080	/* 0.250 * (1 << TSL2560_RATIO_SCALE) */
#define TSL2560_B2T	0x0214	/* 0.0324 * (1 << TSL2560_LUX_SCALE) */
#define TSL2560_M2T	0x02d1	/* 0.0440 * (1 << TSL2560_LUX_SCALE) */

#define TSL2560_K3T	0x00c0	/* 0.375 * (1 << TSL2560_RATIO_SCALE) */
#define TSL2560_B3T	0x023f	/* 0.0351 * (1 << TSL2560_LUX_SCALE) */
#define TSL2560_M3T	0x037b	/* 0.0544 * (1 << TSL2560_LUX_SCALE) */

#define TSL2560_K4T	0x0080	/* 0.50 * (1 << TSL2560_RATIO_SCALE) */
#define TSL2560_B4T	0x0214	/* 0.0381 * (1 << TSL2560_LUX_SCALE) */
#define TSL2560_M4T	0x02d1	/* 0.0624 * (1 << TSL2560_LUX_SCALE) */

#define TSL2560_K5T	0x0138	/* 0.61 * (1 << TSL2560_RATIO_SCALE) */
#define TSL2560_B5T	0x016f	/* 0.0224 * (1 << TSL2560_LUX_SCALE) */
#define TSL2560_M5T	0x01fc	/* 0.0310 * (1 << TSL2560_LUX_SCALE) */

#define TSL2560_K6T	0x0100	/* 0.80 * (1 << TSL2560_RATIO_SCALE) */
#define TSL2560_B6T	0x0270	/* 0.0128 * (1 << TSL2560_LUX_SCALE) */
#define TSL2560_M6T	0x03fe	/* 0.0153 * (1 << TSL2560_LUX_SCALE) */

#define TSL2560_K7T	0x019a	/* 1.3 * (1 << TSL2560_RATIO_SCALE) */
#define TSL2560_B7T	0x0018	/* 0.00146 * (1 << TSL2560_LUX_SCALE) */
#define TSL2560_M7T	0x0012	/* 0.00112 * (1 << TSL2560_LUX_SCALE) */

u_int64_t
tsl_lux(u_int32_t chan0, u_int32_t chan1)
{
	u_int32_t ratio, ratio1;
	u_int32_t b, m;
	int64_t lux;

	ratio1 = 0;
	if (chan0 != 0)
		ratio1 = (chan1 << (TSL2560_RATIO_SCALE + 1)) / chan0;
	ratio = (ratio1 + 1) >> 1;

	b = 0, m = 0;
	if (ratio <= TSL2560_K1T)
		b = TSL2560_B1T, m = TSL2560_M1T;
	else if (ratio <= TSL2560_K2T)
		b = TSL2560_B2T, m = TSL2560_M2T;
	else if (ratio <= TSL2560_K3T)
		b = TSL2560_B3T, m = TSL2560_M3T;
	else if (ratio <= TSL2560_K4T)
		b = TSL2560_B4T, m = TSL2560_M4T;
	else if (ratio <= TSL2560_K5T)
		b = TSL2560_B5T, m = TSL2560_M5T;
	else if (ratio <= TSL2560_K6T)
		b = TSL2560_B6T, m = TSL2560_M6T;
	else if (ratio <= TSL2560_K7T)
		b = TSL2560_B7T, m = TSL2560_M7T;

	lux = b * chan0 - m * chan1;
	if (lux < 0)
		lux = 0;
	return ((lux * 1000000) >> TSL2560_LUX_SCALE);
}
