/*	$OpenBSD: adm1025.c,v 1.26 2022/04/06 18:59:28 naddy Exp $	*/

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

/* ADM 1025 registers */
#define ADM1025_V2_5		0x20
#define ADM1025_Vccp		0x21
#define ADM1025_V3_3		0x22
#define ADM1025_V5		0x23
#define ADM1025_V12		0x24
#define ADM1025_Vcc		0x25
#define ADM1025_EXT_TEMP	0x26
#define ADM1025_INT_TEMP	0x27
#define ADM1025_STATUS2		0x42
#define  ADM1025_STATUS2_EXT	0x40
#define ADM1025_COMPANY		0x3e	/* contains 0x41 */
#define ADM1025_STEPPING	0x3f	/* contains 0x2? */
#define ADM1025_CONFIG		0x40
#define  ADM1025_CONFIG_START	0x01
#define SMSC47M192_V1_5		0x50
#define SMSC47M192_V1_8		0x51
#define SMSC47M192_TEMP2	0x52

/* Sensors */
#define ADMTM_INT		0
#define ADMTM_EXT		1
#define ADMTM_V2_5		2
#define ADMTM_Vccp		3
#define ADMTM_V3_3		4
#define ADMTM_V5		5
#define ADMTM_V12		6
#define ADMTM_Vcc		7
#define ADMTM_NUM_SENSORS	8
#define SMSC_V1_5		8
#define SMSC_V1_8		9
#define SMSC_TEMP2		10
#define SMSC_NUM_SENSORS	3
struct admtm_softc {
	struct device	sc_dev;
	i2c_tag_t	sc_tag;
	i2c_addr_t	sc_addr;

	struct ksensor	sc_sensor[ADMTM_NUM_SENSORS + SMSC_NUM_SENSORS];
	struct ksensordev sc_sensordev;
	int		sc_nsensors;
	int		sc_model;
};

int	admtm_match(struct device *, void *, void *);
void	admtm_attach(struct device *, struct device *, void *);
void	admtm_refresh(void *);

const struct cfattach admtm_ca = {
	sizeof(struct admtm_softc), admtm_match, admtm_attach
};

struct cfdriver admtm_cd = {
	NULL, "admtm", DV_DULL
};

int
admtm_match(struct device *parent, void *match, void *aux)
{
	struct i2c_attach_args *ia = aux;

	if (strcmp(ia->ia_name, "adm1025") == 0 ||
	    strcmp(ia->ia_name, "47m192") == 0 ||
	    strcmp(ia->ia_name, "ne1619") == 0)
		return (1);
	return (0);
}

void
admtm_attach(struct device *parent, struct device *self, void *aux)
{
	struct admtm_softc *sc = (struct admtm_softc *)self;
	struct i2c_attach_args *ia = aux;
	u_int8_t cmd, data, data2;
	int i;

	sc->sc_tag = ia->ia_tag;
	sc->sc_addr = ia->ia_addr;

	printf(": %s", ia->ia_name);

	sc->sc_nsensors = ADMTM_NUM_SENSORS;
	sc->sc_model = 1025;
	if (strcmp(ia->ia_name, "47m192") == 0) {
		sc->sc_nsensors += SMSC_NUM_SENSORS;
		sc->sc_model = 192;
	}

	iic_acquire_bus(sc->sc_tag, 0);
	cmd = ADM1025_CONFIG;
	if (iic_exec(sc->sc_tag, I2C_OP_READ_WITH_STOP,
	    sc->sc_addr, &cmd, sizeof cmd, &data, sizeof data, 0)) {
		iic_release_bus(sc->sc_tag, 0);
		printf(", cannot get control register\n");
		return;
	}

	data2 = data | ADM1025_CONFIG_START;
	if (data != data2) {
		if (iic_exec(sc->sc_tag, I2C_OP_WRITE_WITH_STOP,
		    sc->sc_addr, &cmd, sizeof cmd, &data2, sizeof data2, 0)) {
			iic_release_bus(sc->sc_tag, 0);
			printf(", cannot set control register\n");
			return;
		}
	}
	iic_release_bus(sc->sc_tag, 0);

	/* Initialize sensor data. */
	strlcpy(sc->sc_sensordev.xname, sc->sc_dev.dv_xname,
	    sizeof(sc->sc_sensordev.xname));

	sc->sc_sensor[ADMTM_INT].type = SENSOR_TEMP;
	strlcpy(sc->sc_sensor[ADMTM_INT].desc, "Internal",
	    sizeof(sc->sc_sensor[ADMTM_INT].desc));

	sc->sc_sensor[ADMTM_EXT].type = SENSOR_TEMP;
	strlcpy(sc->sc_sensor[ADMTM_EXT].desc, "External",
	    sizeof(sc->sc_sensor[ADMTM_EXT].desc));

	sc->sc_sensor[ADMTM_V2_5].type = SENSOR_VOLTS_DC;
	strlcpy(sc->sc_sensor[ADMTM_V2_5].desc, "2.5 V",
	    sizeof(sc->sc_sensor[ADMTM_V2_5].desc));

	sc->sc_sensor[ADMTM_Vccp].type = SENSOR_VOLTS_DC;
	strlcpy(sc->sc_sensor[ADMTM_Vccp].desc, "Vccp",
	    sizeof(sc->sc_sensor[ADMTM_Vccp].desc));

	sc->sc_sensor[ADMTM_V3_3].type = SENSOR_VOLTS_DC;
	strlcpy(sc->sc_sensor[ADMTM_V3_3].desc, "3.3 V",
	    sizeof(sc->sc_sensor[ADMTM_V3_3].desc));

	sc->sc_sensor[ADMTM_V5].type = SENSOR_VOLTS_DC;
	strlcpy(sc->sc_sensor[ADMTM_V5].desc, "5 V",
	    sizeof(sc->sc_sensor[ADMTM_V5].desc));

	sc->sc_sensor[ADMTM_V12].type = SENSOR_VOLTS_DC;
	strlcpy(sc->sc_sensor[ADMTM_V12].desc, "12 V",
	    sizeof(sc->sc_sensor[ADMTM_V12].desc));

	sc->sc_sensor[ADMTM_Vcc].type = SENSOR_VOLTS_DC;
	strlcpy(sc->sc_sensor[ADMTM_Vcc].desc, "Vcc",
	    sizeof(sc->sc_sensor[ADMTM_Vcc].desc));

	sc->sc_sensor[SMSC_V1_5].type = SENSOR_VOLTS_DC;
	strlcpy(sc->sc_sensor[SMSC_V1_5].desc, "1.5 V",
	    sizeof(sc->sc_sensor[SMSC_V1_5].desc));

	sc->sc_sensor[SMSC_V1_8].type = SENSOR_VOLTS_DC;
	strlcpy(sc->sc_sensor[SMSC_V1_8].desc, "1.8 V",
	    sizeof(sc->sc_sensor[SMSC_V1_8].desc));

	sc->sc_sensor[SMSC_TEMP2].type = SENSOR_TEMP;
	strlcpy(sc->sc_sensor[SMSC_TEMP2].desc, "External",
	    sizeof(sc->sc_sensor[SMSC_TEMP2].desc));

	if (sensor_task_register(sc, admtm_refresh, 5) == NULL) {
		printf(", unable to register update task\n");
		return;
	}

	for (i = 0; i < sc->sc_nsensors; i++)
		sensor_attach(&sc->sc_sensordev, &sc->sc_sensor[i]);
	sensordev_install(&sc->sc_sensordev);

	printf("\n");
}

void
admtm_refresh(void *arg)
{
	struct admtm_softc *sc = arg;
	u_int8_t cmd, data;
	int8_t sdata;

	iic_acquire_bus(sc->sc_tag, 0);

	cmd = ADM1025_INT_TEMP;
	if (iic_exec(sc->sc_tag, I2C_OP_READ_WITH_STOP,
	    sc->sc_addr, &cmd, sizeof cmd, &sdata, sizeof sdata, 0) == 0)
		sc->sc_sensor[ADMTM_INT].value = 273150000 + 1000000 * sdata;

	cmd = ADM1025_EXT_TEMP;
	if (iic_exec(sc->sc_tag, I2C_OP_READ_WITH_STOP,
	    sc->sc_addr, &cmd, sizeof cmd, &sdata, sizeof sdata, 0) == 0)
		sc->sc_sensor[ADMTM_EXT].value = 273150000 + 1000000 * sdata;

	cmd = ADM1025_STATUS2;
	if (iic_exec(sc->sc_tag, I2C_OP_READ_WITH_STOP,
	    sc->sc_addr, &cmd, sizeof cmd, &data, sizeof data, 0) == 0) {
		if (data & ADM1025_STATUS2_EXT)
			sc->sc_sensor[ADMTM_EXT].flags |= SENSOR_FINVALID;
		else
			sc->sc_sensor[ADMTM_EXT].flags &= ~SENSOR_FINVALID;
	}

	cmd = ADM1025_V2_5;
	if (iic_exec(sc->sc_tag, I2C_OP_READ_WITH_STOP,
	    sc->sc_addr, &cmd, sizeof cmd, &data, sizeof data, 0) == 0)
		sc->sc_sensor[ADMTM_V2_5].value = 2500000 * data / 192;

	cmd = ADM1025_Vccp;
	if (iic_exec(sc->sc_tag, I2C_OP_READ_WITH_STOP,
	    sc->sc_addr, &cmd, sizeof cmd, &data, sizeof data, 0) == 0)
		sc->sc_sensor[ADMTM_Vcc].value = 2249000 * data / 192;

	cmd = ADM1025_V3_3;
	if (iic_exec(sc->sc_tag, I2C_OP_READ_WITH_STOP,
	    sc->sc_addr, &cmd, sizeof cmd, &data, sizeof data, 0) == 0)
		sc->sc_sensor[ADMTM_V3_3].value = 3300000 * data / 192;

	cmd = ADM1025_V5;
	if (iic_exec(sc->sc_tag, I2C_OP_READ_WITH_STOP,
	    sc->sc_addr, &cmd, sizeof cmd, &data, sizeof data, 0) == 0)
		sc->sc_sensor[ADMTM_V5].value = 5000000 * data / 192;

	cmd = ADM1025_V12;
	if (iic_exec(sc->sc_tag, I2C_OP_READ_WITH_STOP,
	    sc->sc_addr, &cmd, sizeof cmd, &data, sizeof data, 0) == 0)
		sc->sc_sensor[ADMTM_V12].value = 12000000 * data / 192;

	cmd = ADM1025_Vcc;
	if (iic_exec(sc->sc_tag, I2C_OP_READ_WITH_STOP,
	    sc->sc_addr, &cmd, sizeof cmd, &data, sizeof data, 0) == 0)
		sc->sc_sensor[ADMTM_Vcc].value = 3300000 * data / 192;

	if (sc->sc_model == 192) {
		cmd = SMSC47M192_V1_5;
		if (iic_exec(sc->sc_tag, I2C_OP_READ_WITH_STOP,
		    sc->sc_addr, &cmd, sizeof cmd, &data, sizeof data, 0) == 0)
			sc->sc_sensor[SMSC_V1_5].value = 1500000 * data / 192;

		cmd = SMSC47M192_V1_8;
		if (iic_exec(sc->sc_tag, I2C_OP_READ_WITH_STOP,
		    sc->sc_addr, &cmd, sizeof cmd, &data, sizeof data, 0) == 0)
			sc->sc_sensor[SMSC_V1_8].value = 1800000 * data / 192;

		cmd = SMSC47M192_TEMP2;
		if (iic_exec(sc->sc_tag, I2C_OP_READ_WITH_STOP,
		    sc->sc_addr, &cmd, sizeof cmd, &sdata, sizeof sdata,
		    0) == 0)
			sc->sc_sensor[SMSC_TEMP2].value = 273150000 + 1000000 * sdata;

	}

	iic_release_bus(sc->sc_tag, 0);
}
