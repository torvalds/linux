/*	$OpenBSD: adm1024.c,v 1.15 2022/04/06 18:59:28 naddy Exp $	*/

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

/* ADM 1024 registers */
#define ADM1024_V2_5		0x20
#define ADM1024_Vccp		0x21
#define ADM1024_Vcc		0x22
#define ADM1024_V5		0x23
#define ADM1024_V12		0x24
#define ADM1024_Vccp2		0x25
#define ADM1024_EXT_TEMP	0x26
#define ADM1024_INT_TEMP	0x27
#define ADM1024_FAN1		0x28
#define ADM1024_FAN2		0x29
#define ADM1024_STATUS2		0x42
#define ADM1024_FANC		0x47
#define  ADM1024_STATUS2_EXT	0x40
#define ADM1024_COMPANY		0x3e	/* contains 0x41 */
#define ADM1024_STEPPING	0x3f	/* contains 0x2? */
#define ADM1024_CONFIG1		0x40
#define  ADM1024_CONFIG1_START	0x01
#define  ADM1024_CONFIG1_INTCLR	0x08

/* Sensors */
#define ADMLC_INT		0
#define ADMLC_EXT		1
#define ADMLC_V2_5		2
#define ADMLC_Vccp		3
#define ADMLC_Vcc		4
#define ADMLC_V5		5
#define ADMLC_V12		6
#define ADMLC_Vccp2		7
#define ADMLC_FAN1		8
#define ADMLC_FAN2		9
#define ADMLC_NUM_SENSORS	10

struct admlc_softc {
	struct device	sc_dev;
	i2c_tag_t	sc_tag;
	i2c_addr_t	sc_addr;

	struct ksensor	sc_sensor[ADMLC_NUM_SENSORS];
	struct ksensordev sc_sensordev;
	int		sc_fan1mul;
	int		sc_fan2mul;
};

int	admlc_match(struct device *, void *, void *);
void	admlc_attach(struct device *, struct device *, void *);
void	admlc_refresh(void *);

const struct cfattach admlc_ca = {
	sizeof(struct admlc_softc), admlc_match, admlc_attach
};

struct cfdriver admlc_cd = {
	NULL, "admlc", DV_DULL
};

int
admlc_match(struct device *parent, void *match, void *aux)
{
	struct i2c_attach_args *ia = aux;

	if (strcmp(ia->ia_name, "adm1024") == 0)
		return (1);
	return (0);
}

void
admlc_attach(struct device *parent, struct device *self, void *aux)
{
	struct admlc_softc *sc = (struct admlc_softc *)self;
	struct i2c_attach_args *ia = aux;
	u_int8_t cmd, data, data2;
	int i;

	sc->sc_tag = ia->ia_tag;
	sc->sc_addr = ia->ia_addr;

	iic_acquire_bus(sc->sc_tag, 0);
	cmd = ADM1024_CONFIG1;
	if (iic_exec(sc->sc_tag, I2C_OP_READ_WITH_STOP,
	    sc->sc_addr, &cmd, sizeof cmd, &data, sizeof data, 0)) {
		iic_release_bus(sc->sc_tag, 0);
		printf(": cannot get control register\n");
		return;
	}
	data2 = data | ADM1024_CONFIG1_START;
	data2 = data2 & ~ADM1024_CONFIG1_INTCLR;
	if (data != data2) {
		if (iic_exec(sc->sc_tag, I2C_OP_WRITE_WITH_STOP,
		    sc->sc_addr, &cmd, sizeof cmd, &data2, sizeof data2, 0)) {
			iic_release_bus(sc->sc_tag, 0);
			printf(": cannot set control register\n");
			return;
		}
	}

	cmd = ADM1024_FANC;
	if (iic_exec(sc->sc_tag, I2C_OP_READ_WITH_STOP,
	    sc->sc_addr, &cmd, sizeof cmd, &data, sizeof data, 0)) {
		printf(", unable to read fan setting\n");
		return;
	}
	sc->sc_fan1mul = (1 << (data >> 4) & 0x3);
	sc->sc_fan2mul = (1 << (data >> 6) & 0x3);

	iic_release_bus(sc->sc_tag, 0);

	/* Initialize sensor data. */
	strlcpy(sc->sc_sensordev.xname, sc->sc_dev.dv_xname,
	    sizeof(sc->sc_sensordev.xname));

	sc->sc_sensor[ADMLC_INT].type = SENSOR_TEMP;
	strlcpy(sc->sc_sensor[ADMLC_INT].desc, "Internal",
	    sizeof(sc->sc_sensor[ADMLC_INT].desc));

	sc->sc_sensor[ADMLC_EXT].type = SENSOR_TEMP;
	strlcpy(sc->sc_sensor[ADMLC_EXT].desc, "External",
	    sizeof(sc->sc_sensor[ADMLC_EXT].desc));

	sc->sc_sensor[ADMLC_V2_5].type = SENSOR_VOLTS_DC;
	strlcpy(sc->sc_sensor[ADMLC_V2_5].desc, "2.5 V",
	    sizeof(sc->sc_sensor[ADMLC_V2_5].desc));

	sc->sc_sensor[ADMLC_Vccp].type = SENSOR_VOLTS_DC;
	strlcpy(sc->sc_sensor[ADMLC_Vccp].desc, "Vccp",
	    sizeof(sc->sc_sensor[ADMLC_Vccp].desc));

	sc->sc_sensor[ADMLC_Vcc].type = SENSOR_VOLTS_DC;
	strlcpy(sc->sc_sensor[ADMLC_Vcc].desc, "Vcc",
	    sizeof(sc->sc_sensor[ADMLC_Vcc].desc));

	sc->sc_sensor[ADMLC_V5].type = SENSOR_VOLTS_DC;
	strlcpy(sc->sc_sensor[ADMLC_V5].desc, "5 V",
	    sizeof(sc->sc_sensor[ADMLC_V5].desc));

	sc->sc_sensor[ADMLC_V12].type = SENSOR_VOLTS_DC;
	strlcpy(sc->sc_sensor[ADMLC_V12].desc, "12 V",
	    sizeof(sc->sc_sensor[ADMLC_V12].desc));

	sc->sc_sensor[ADMLC_Vccp2].type = SENSOR_VOLTS_DC;
	strlcpy(sc->sc_sensor[ADMLC_Vccp2].desc, "Vccp2",
	    sizeof(sc->sc_sensor[ADMLC_Vccp2].desc));

	sc->sc_sensor[ADMLC_FAN1].type = SENSOR_FANRPM;

	sc->sc_sensor[ADMLC_FAN2].type = SENSOR_FANRPM;


	if (sensor_task_register(sc, admlc_refresh, 5) == NULL) {
		printf(", unable to register update task\n");
		return;
	}

	for (i = 0; i < ADMLC_NUM_SENSORS; i++)
		sensor_attach(&sc->sc_sensordev, &sc->sc_sensor[i]);
	sensordev_install(&sc->sc_sensordev);

	printf("\n");
}

static void
fanval(struct ksensor *sens, int mul, u_int8_t data)
{
	int tmp = data * mul;

	if (tmp == 0)
		sens->flags |= SENSOR_FINVALID;
	else
		sens->value = 1350000 / tmp;
}

void
admlc_refresh(void *arg)
{
	struct admlc_softc *sc = arg;
	u_int8_t cmd, data;
	int8_t sdata;

	iic_acquire_bus(sc->sc_tag, 0);

	cmd = ADM1024_INT_TEMP;
	if (iic_exec(sc->sc_tag, I2C_OP_READ_WITH_STOP,
	    sc->sc_addr, &cmd, sizeof cmd, &sdata, sizeof sdata, 0) == 0)
		sc->sc_sensor[ADMLC_INT].value = 273150000 + 1000000 * sdata;

	cmd = ADM1024_EXT_TEMP;
	if (iic_exec(sc->sc_tag, I2C_OP_READ_WITH_STOP,
	    sc->sc_addr, &cmd, sizeof cmd, &sdata, sizeof sdata, 0) == 0)
		sc->sc_sensor[ADMLC_EXT].value = 273150000 + 1000000 * sdata;

	cmd = ADM1024_STATUS2;
	if (iic_exec(sc->sc_tag, I2C_OP_READ_WITH_STOP,
	    sc->sc_addr, &cmd, sizeof cmd, &data, sizeof data, 0) == 0) {
		if (data & ADM1024_STATUS2_EXT)
			sc->sc_sensor[ADMLC_EXT].flags |= SENSOR_FINVALID;
		else
			sc->sc_sensor[ADMLC_EXT].flags &= ~SENSOR_FINVALID;
	}

	cmd = ADM1024_V2_5;
	if (iic_exec(sc->sc_tag, I2C_OP_READ_WITH_STOP,
	    sc->sc_addr, &cmd, sizeof cmd, &data, sizeof data, 0) == 0)
		sc->sc_sensor[ADMLC_V2_5].value = 2500000 * data / 192;

	cmd = ADM1024_Vccp;
	if (iic_exec(sc->sc_tag, I2C_OP_READ_WITH_STOP,
	    sc->sc_addr, &cmd, sizeof cmd, &data, sizeof data, 0) == 0)
		sc->sc_sensor[ADMLC_Vcc].value = 2249000 * data / 192;

	cmd = ADM1024_Vcc;
	if (iic_exec(sc->sc_tag, I2C_OP_READ_WITH_STOP,
	    sc->sc_addr, &cmd, sizeof cmd, &data, sizeof data, 0) == 0)
		sc->sc_sensor[ADMLC_Vcc].value = 3300000 * data / 192;

	cmd = ADM1024_V5;
	if (iic_exec(sc->sc_tag, I2C_OP_READ_WITH_STOP,
	    sc->sc_addr, &cmd, sizeof cmd, &data, sizeof data, 0) == 0)
		sc->sc_sensor[ADMLC_V5].value = 5000000 * data / 192;

	cmd = ADM1024_V12;
	if (iic_exec(sc->sc_tag, I2C_OP_READ_WITH_STOP,
	    sc->sc_addr, &cmd, sizeof cmd, &data, sizeof data, 0) == 0)
		sc->sc_sensor[ADMLC_V12].value = 12000000 * data / 192;

	cmd = ADM1024_Vccp2;
	if (iic_exec(sc->sc_tag, I2C_OP_READ_WITH_STOP,
	    sc->sc_addr, &cmd, sizeof cmd, &data, sizeof data, 0) == 0)
		sc->sc_sensor[ADMLC_Vccp2].value = 2700000 * data / 192;

	cmd = ADM1024_FAN1;
	if (iic_exec(sc->sc_tag, I2C_OP_READ_WITH_STOP,
	    sc->sc_addr, &cmd, sizeof cmd, &data, sizeof data, 0) == 0)
		fanval(&sc->sc_sensor[ADMLC_FAN1], sc->sc_fan1mul, data);

	cmd = ADM1024_FAN2;
	if (iic_exec(sc->sc_tag, I2C_OP_READ_WITH_STOP,
	    sc->sc_addr, &cmd, sizeof cmd, &data, sizeof data, 0) == 0)
		fanval(&sc->sc_sensor[ADMLC_FAN2], sc->sc_fan2mul, data);
	iic_release_bus(sc->sc_tag, 0);
}
