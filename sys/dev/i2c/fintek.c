/*	$OpenBSD: fintek.c,v 1.9 2022/04/06 18:59:28 naddy Exp $ */
/*
 * Copyright (c) 2006 Dale Rahn <drahn@openbsd.org>
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

/* Sensors */
#define F_VCC	0
#define F_V1	1
#define F_V2	2
#define F_V3	3
#define F_TEMP1	4
#define F_TEMP2	5
#define F_FAN1	6
#define F_FAN2	7
#define F_NUM_SENSORS	8

struct fintek_softc {
	struct device sc_dev;
	i2c_tag_t sc_tag;
	i2c_addr_t sc_addr;

#ifndef SMALL_KERNEL
	struct ksensor sc_sensor[F_NUM_SENSORS];
	struct ksensordev sc_sensordev;
#endif
};

int	fintek_match(struct device *, void *, void *);
void	fintek_attach(struct device *, struct device *, void *);

void	fintek_refresh(void *);
int	fintek_read_reg(struct fintek_softc *sc, u_int8_t cmd, u_int8_t *data,
	    size_t size);
int	fintek_write_reg(struct fintek_softc *sc, u_int8_t cmd, u_int8_t *data,
	    size_t size);
void	fintek_fullspeed(struct fintek_softc *sc);

const struct cfattach fintek_ca = {
	sizeof(struct fintek_softc), fintek_match, fintek_attach
};

struct cfdriver fintek_cd = {
	NULL, "fintek", DV_DULL
};

#define FINTEK_CONFIG1		0x01
#define  FINTEK_FAN1_LINEAR_MODE	0x10
#define  FINTEK_FAN2_LINEAR_MODE	0x20
#define FINTEK_VOLT0		0x10
#define FINTEK_VOLT1		0x11
#define FINTEK_VOLT2		0x12
#define FINTEK_VOLT3		0x13
#define FINTEK_TEMP1		0x14
#define FINTEK_TEMP2		0x15
#define FINTEK_FAN1		0x16
#define FINTEK_FAN2		0x18
#define FINTEK_VERSION		0x5c
#define FINTEK_RSTCR		0x60
#define  FINTEK_FAN1_MODE_MANUAL	0x30
#define  FINTEK_FAN2_MODE_MANUAL	0xc0
#define FINTEK_PWM_DUTY1	0x76
#define FINTEK_PWM_DUTY2	0x86

/* Options passed via the 'flags' config keyword. */
#define FINTEK_OPTION_FULLSPEED	0x0001

int
fintek_match(struct device *parent, void *match, void *aux)
{
	struct i2c_attach_args *ia = aux;

	if (strcmp(ia->ia_name, "f75375") == 0)
		return (1);
	return (0);
}

int
fintek_read_reg(struct fintek_softc *sc, u_int8_t cmd, u_int8_t *data,
    size_t size)
{
	return iic_exec(sc->sc_tag, I2C_OP_READ_WITH_STOP,
	    sc->sc_addr, &cmd, sizeof cmd, data, size, 0);
}

int
fintek_write_reg(struct fintek_softc *sc, u_int8_t cmd, u_int8_t *data,
    size_t size)
{
	return iic_exec(sc->sc_tag, I2C_OP_WRITE_WITH_STOP,
	    sc->sc_addr, &cmd, sizeof cmd, data, size, 0);
}

void
fintek_attach(struct device *parent, struct device *self, void *aux)
{
	struct fintek_softc *sc = (struct fintek_softc *)self;
	struct i2c_attach_args *ia = aux;
	u_int8_t cmd, data;
#ifndef SMALL_KERNEL
	int i;
#endif

	sc->sc_tag = ia->ia_tag;
	sc->sc_addr = ia->ia_addr;

	iic_acquire_bus(sc->sc_tag, 0);

	cmd = FINTEK_VERSION;
	if (fintek_read_reg(sc, cmd, &data, sizeof data))
		goto failread;

	printf(": F75375 rev %d.%d", data>> 4, data & 0xf);

	/*
	 * It seems the fan in the Thecus n2100 doesn't provide a
	 * reliable fan count.  As a result the automatic fan
	 * controlling mode that the chip comes up in after reset
	 * doesn't work reliably.  So we have a flag to drive the fan
	 * at maximum voltage such that the box doesn't overheat.
	 */
	if (sc->sc_dev.dv_cfdata->cf_flags & FINTEK_OPTION_FULLSPEED)
		fintek_fullspeed(sc);

	iic_release_bus(sc->sc_tag, 0);

#ifndef SMALL_KERNEL
	strlcpy(sc->sc_sensordev.xname, sc->sc_dev.dv_xname,
	    sizeof(sc->sc_sensordev.xname));

	sc->sc_sensor[F_VCC].type = SENSOR_VOLTS_DC;
	strlcpy(sc->sc_sensor[F_VCC].desc, "Vcc",
	    sizeof(sc->sc_sensor[F_VCC].desc));

	sc->sc_sensor[F_V1].type = SENSOR_VOLTS_DC;
	sc->sc_sensor[F_V2].type = SENSOR_VOLTS_DC;
	sc->sc_sensor[F_V3].type = SENSOR_VOLTS_DC;

	sc->sc_sensor[F_TEMP1].type = SENSOR_TEMP;
	sc->sc_sensor[F_TEMP2].type = SENSOR_TEMP;

	sc->sc_sensor[F_FAN1].type = SENSOR_FANRPM;
	sc->sc_sensor[F_FAN2].type = SENSOR_FANRPM;

	if (sensor_task_register(sc, fintek_refresh, 5) == NULL) {
		printf(", unable to register update task\n");
		return;
	}

	for (i = 0; i < F_NUM_SENSORS; i++)
		sensor_attach(&sc->sc_sensordev, &sc->sc_sensor[i]);
	sensordev_install(&sc->sc_sensordev);
#endif

	printf("\n");
	return;

failread:
	printf("unable to read reg %d\n", cmd);
	iic_release_bus(sc->sc_tag, 0);
	return;
}


#ifndef SMALL_KERNEL
struct {
	char		sensor;
	u_int8_t	cmd;
} fintek_worklist[] = {
	{ F_VCC, FINTEK_VOLT0 },
	{ F_V1, FINTEK_VOLT1 },
	{ F_V2, FINTEK_VOLT2 },
	{ F_V3, FINTEK_VOLT3 },
	{ F_TEMP1, FINTEK_TEMP1 },
	{ F_TEMP2, FINTEK_TEMP2 },
	{ F_FAN1, FINTEK_FAN1 },
	{ F_FAN2, FINTEK_FAN2 }
};
#define FINTEK_WORKLIST_SZ (sizeof(fintek_worklist) / sizeof(fintek_worklist[0]))

void
fintek_refresh(void *arg)
{
	struct fintek_softc *sc =  arg;
	u_int8_t cmd, data, data2;
	int i;

	iic_acquire_bus(sc->sc_tag, 0);

	for (i = 0; i < FINTEK_WORKLIST_SZ; i++){
		cmd = fintek_worklist[i].cmd;
		if (fintek_read_reg(sc, cmd, &data, sizeof data)) {
			sc->sc_sensor[i].flags |= SENSOR_FINVALID;
			continue;
		}
		sc->sc_sensor[i].flags &= ~SENSOR_FINVALID;
		switch (fintek_worklist[i].sensor) {
		case  F_VCC:
			sc->sc_sensor[i].value = data * 16000;
			break;
		case  F_V1:
			/* FALLTHROUGH */
		case  F_V2:
			/* FALLTHROUGH */
		case  F_V3:
			sc->sc_sensor[i].value = data * 8000;
			break;
		case  F_TEMP1:
			/* FALLTHROUGH */
		case  F_TEMP2:
			sc->sc_sensor[i].value = 273150000 + data * 1000000;
			break;
		case  F_FAN1:
			/* FALLTHROUGH */
		case  F_FAN2:
			/* FANx LSB follows FANx MSB */
			cmd = fintek_worklist[i].cmd + 1;
			if (fintek_read_reg(sc, cmd, &data2, sizeof data2)) {
				sc->sc_sensor[i].flags |= SENSOR_FINVALID;
				continue;
			}
			if ((data == 0xff && data2 == 0xff) ||
			    (data == 0 && data2 == 0))
				sc->sc_sensor[i].value = 0;
			else
				sc->sc_sensor[i].value = 1500000 /
				    (data << 8 | data2);
			break;
		default:
			sc->sc_sensor[i].flags |= SENSOR_FINVALID;
			break;
		}
	}

	iic_release_bus(sc->sc_tag, 0);
}
#endif

void
fintek_fullspeed(struct fintek_softc *sc)
{
	u_int8_t data;

	data = FINTEK_FAN1_LINEAR_MODE | FINTEK_FAN2_LINEAR_MODE;
	fintek_write_reg(sc, FINTEK_CONFIG1, &data, sizeof data);

	data = FINTEK_FAN1_MODE_MANUAL | FINTEK_FAN2_MODE_MANUAL;
	fintek_write_reg(sc, FINTEK_RSTCR, &data, sizeof data);

	data = 0xff;		/* Maximum voltage */
	fintek_write_reg(sc, FINTEK_PWM_DUTY1, &data, sizeof data);
	fintek_write_reg(sc, FINTEK_PWM_DUTY2, &data, sizeof data);
}	
