/*	$OpenBSD: asc7621.c,v 1.5 2022/04/06 18:59:28 naddy Exp $	*/

/*
 * Copyright (c) 2007 Mike Belopuhov
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

/* ASC7621 registers */

#define ASC7621_PECI		0x40	/* Check for PECI monitoring */
#define ASC7621_PECI_MASK	0x10	/* 00010000 */

#define ASC7621_LEGACY		0x36	/* Check for legacy mode */
#define ASC7621_LEGACY_MASK	0x10	/* 00010000 */

#define ASC7621_TEMP1H		0x25	/* Zone 1 Temperature (MS Byte) */
#define ASC7621_TEMP1L		0x10	/* Zone 1 Temperature (LS Byte) */
#define ASC7621_TEMP2H		0x26	/* Zone 2 Temperature (MS Byte) */
#define ASC7621_TEMP2L		0x15	/* Zone 2 Temperature (LS Byte) */
#define ASC7621_TEMP3H		0x27	/* Zone 3 Temperature (MS Byte) */
#define ASC7621_TEMP3L		0x16	/* Zone 3 Temperature (LS Byte) */
#define ASC7621_TEMP4H		0x33	/* Zone 4 Temperature (MS Byte) */
#define ASC7621_TEMP4L		0x17	/* Zone 4 Temperature (LS Byte) */
#define ASC7621_TEMP_NA		0x80	/* Not plugged */

#define ASC7621_IN1_VH		0x20	/* 2.5V (MS Byte) */
#define ASC7621_IN1_VL		0x13	/* 2.5V (LS Byte) */
#define ASC7621_IN2_VH		0x21	/* VCCP (MS Byte) */
#define ASC7621_IN2_VL		0x18	/* VCCP (LS Byte) */
#define ASC7621_IN3_VH		0x22	/* 3.3V (MS Byte) */
#define ASC7621_IN3_VL		0x11	/* 2.3V (LS Byte) */
#define ASC7621_IN4_VH		0x23	/* 5V   (MS Byte) */
#define ASC7621_IN4_VL		0x12	/* 5V   (LS Byte) */
#define ASC7621_IN5_VH		0x24	/* 12V  (MS Byte) */
#define ASC7621_IN5_VL		0x14	/* 12V  (LS Byte) */

#define ASC7621_TACH1H		0x29	/* Tachometer 1 (MS Byte) */
#define ASC7621_TACH1L		0x28	/* Tachometer 1 (LS Byte) */
#define ASC7621_TACH2H		0x2b	/* Tachometer 2 (MS Byte) */
#define ASC7621_TACH2L		0x2a	/* Tachometer 2 (LS Byte) */
#define ASC7621_TACH3H		0x2d	/* Tachometer 3 (MS Byte) */
#define ASC7621_TACH3L		0x2c	/* Tachometer 3 (LS Byte) */
#define ASC7621_TACH4H		0x2f	/* Tachometer 4 (MS Byte) */
#define ASC7621_TACH4L		0x2e	/* Tachometer 4 (LS Byte) */

/* Sensors */
#define ADL_TEMP1		0
#define ADL_TEMP2		1
#define ADL_TEMP3		2
#define ADL_TEMP4		3
#define ADL_IN1_V		4
#define ADL_IN2_V		5
#define ADL_IN3_V		6
#define ADL_IN4_V		7
#define ADL_IN5_V		8
#define ADL_TACH1		9
#define ADL_TACH2		10
#define ADL_TACH3		11
#define ADL_TACH4		12
#define ADL_NUM_SENSORS		13

struct {
	char		sensor;
	u_int8_t	hreg;			/* MS-byte register */
	u_int8_t	lreg;			/* LS-byte register */
	char		*name;
	u_short		mVscale;
	u_short		tempscale;		/* else a fan */
} adl_worklist[] = {
	{ ADL_TEMP1, ASC7621_TEMP1H, ASC7621_TEMP1L, "CPU", 0, 1 },
	{ ADL_TEMP2, ASC7621_TEMP2H, ASC7621_TEMP2L, "CPU", 0, 1 },
	{ ADL_TEMP3, ASC7621_TEMP3H, ASC7621_TEMP3L, "Internal", 0, 1 },
	{ ADL_TEMP4, ASC7621_TEMP4H, ASC7621_TEMP4L, "External", 0, 1 },

	{ ADL_IN1_V, ASC7621_IN1_VH, ASC7621_IN1_VL, "+1.5V", 2500, 0 },
	{ ADL_IN2_V, ASC7621_IN2_VH, ASC7621_IN2_VL, "Vccp",  2250, 0 },
	{ ADL_IN3_V, ASC7621_IN3_VH, ASC7621_IN3_VL, "+3.3V", 3300, 0 },
	{ ADL_IN4_V, ASC7621_IN4_VH, ASC7621_IN4_VL, "+5V",   5000, 0 },
	{ ADL_IN5_V, ASC7621_IN5_VH, ASC7621_IN5_VL, "+12V", 12000, 0 },

	{ ADL_TACH1, ASC7621_TACH1L, ASC7621_TACH1H, "", 0, 0 },
	{ ADL_TACH2, ASC7621_TACH2L, ASC7621_TACH2H, "", 0, 0 },
	{ ADL_TACH3, ASC7621_TACH3L, ASC7621_TACH3H, "", 0, 0 },
	{ ADL_TACH4, ASC7621_TACH4L, ASC7621_TACH4H, "", 0, 0 }
};

struct adl_softc {
	struct device sc_dev;
	i2c_tag_t sc_tag;
	i2c_addr_t sc_addr;
	u_int8_t sc_conf;

	struct ksensor sc_sensor[ADL_NUM_SENSORS];
	struct ksensordev sc_sensordev;
};

#if 0
static int peci_enabled;
static int legacy_mode;
#endif

int	adl_match(struct device *, void *, void *);
void	adl_attach(struct device *, struct device *, void *);

void	adl_refresh(void *);

const struct cfattach adl_ca = {
	sizeof(struct adl_softc), adl_match, adl_attach
};

struct cfdriver adl_cd = {
	NULL, "adl", DV_DULL
};

int
adl_match(struct device *parent, void *match, void *aux)
{
	struct i2c_attach_args *ia = aux;

	if (strcmp(ia->ia_name, "asc7621") == 0)
		return (1);
	return (0);
}

void
adl_attach(struct device *parent, struct device *self, void *aux)
{
	struct adl_softc *sc = (struct adl_softc *)self;
	struct i2c_attach_args *ia = aux;
	u_int8_t cmd, data;
	int i;

	sc->sc_tag = ia->ia_tag;
	sc->sc_addr = ia->ia_addr;

	printf(": %s", ia->ia_name);

	/* Initialize sensor data. */
	strlcpy(sc->sc_sensordev.xname, sc->sc_dev.dv_xname,
	    sizeof(sc->sc_sensordev.xname));

	/* Check for PECI mode */
	cmd = ASC7621_PECI;
	(void)iic_exec(sc->sc_tag, I2C_OP_READ_WITH_STOP, sc->sc_addr,
	    &cmd, sizeof(cmd), &data, sizeof(data), 0);
	if (data & ASC7621_PECI_MASK)
		printf(", PECI enabled\n");

#if 0
	/* Check for legacy mode */
	cmd = ASC7621_LEGACY;
	if (iic_exec(sc->sc_tag, I2C_OP_READ_WITH_STOP, sc->sc_addr,
	    &cmd, sizeof(cmd), &data, sizeof(data), 0)) {
		printf(", unable to read PECI configuration register");
	}
	if (data & ASC7621_LEGACY_MASK)
		legacy_mode = 1;
#endif

	if (sensor_task_register(sc, adl_refresh, 5) == NULL) {
		printf(", unable to register update task\n");
		return;
	}

	for (i = 0; i < ADL_NUM_SENSORS; i++) {
		if (adl_worklist[i].tempscale)
			sc->sc_sensor[i].type = SENSOR_TEMP;
		else if (adl_worklist[i].mVscale)
			sc->sc_sensor[i].type = SENSOR_VOLTS_DC;
		else
			sc->sc_sensor[i].type = SENSOR_FANRPM;
		strlcpy(sc->sc_sensor[i].desc, adl_worklist[i].name,
		    sizeof(sc->sc_sensor[i].desc));

		sensor_attach(&sc->sc_sensordev, &sc->sc_sensor[i]);
	}
	sensordev_install(&sc->sc_sensordev);

	printf("\n");
}

void
adl_refresh(void *arg)
{
	struct adl_softc *sc = arg;
	int64_t temp, volt;
	u_int8_t hdata, ldata, hreg, lreg;
	u_int16_t fan;
	int i;

	iic_acquire_bus(sc->sc_tag, 0);

	for (i = 0; i < sizeof adl_worklist / sizeof(adl_worklist[0]); i++) {
		hreg = adl_worklist[i].hreg;
		if (iic_exec(sc->sc_tag, I2C_OP_READ_WITH_STOP,
		    sc->sc_addr, &hreg, sizeof hreg, &hdata, sizeof hdata, 0)) {
			sc->sc_sensor[i].flags |= SENSOR_FINVALID;
			continue;
		}
		lreg = adl_worklist[i].lreg;
		if (iic_exec(sc->sc_tag, I2C_OP_READ_WITH_STOP,
		    sc->sc_addr, &lreg, sizeof lreg, &ldata, sizeof ldata, 0)) {
			sc->sc_sensor[i].flags |= SENSOR_FINVALID;
			continue;
		}

		sc->sc_sensor[i].flags &= ~SENSOR_FINVALID;
		if (adl_worklist[i].tempscale) {
			if (hdata == ASC7621_TEMP_NA)
				sc->sc_sensor[i].flags |= SENSOR_FINVALID;
			else {
				/*
				 * 10-bit two's complement integer in
				 * steps of 0.25
				 */
				temp = ((hdata << 8 | ldata)) >> (16 - 10);
				temp = temp * 250000 + 273150000;
				sc->sc_sensor[i].value = temp;
			}
		} else if (adl_worklist[i].mVscale) {
			volt = ((hdata << 8 | ldata)) >> (16 - 10);
			volt = volt * adl_worklist[i].mVscale / (192 << 2);
			sc->sc_sensor[i].value = volt * 1000;
		} else {
			/*
			 * Inversed to ensure that the LS byte will be read
			 * before MS byte.
			 */
			fan = hdata + (ldata << 8);
			if (fan == 0 || fan == 0xffff)
				sc->sc_sensor[i].flags |= SENSOR_FINVALID;
			else
				sc->sc_sensor[i].value = (90000 * 60) / fan;
		}
	}

	iic_release_bus(sc->sc_tag, 0);
}
