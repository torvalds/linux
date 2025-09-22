/*	$OpenBSD: asb100.c,v 1.12 2022/04/06 18:59:28 naddy Exp $	*/

/*
 * Copyright (c) 2005 Damien Miller <djm@openbsd.org>
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

/* Apparently the ASB100 always lives here */
#define ASB100_ADDR			0x2d

/* ASB100 registers */
#define ASB100_TEMP3			0x17
#define ASB100_TEMP3_MAX		0x18
#define ASB100_TEMP3_HYST		0x19
#define ASB100_VIN0			0x20
#define ASB100_VIN1			0x21
#define ASB100_VIN2			0x22
#define ASB100_VIN3			0x23
#define ASB100_VIN4			0x24
#define ASB100_VIN5			0x25
#define ASB100_VIN6			0x26
#define ASB100_TEMP0			0x27
#define ASB100_FAN0			0x28
#define ASB100_FAN1			0x29
#define ASB100_FAN2			0x30
#define ASB100_VIN0_MIN			0x2b
#define ASB100_VIN0_MAX			0x2c
#define ASB100_VIN1_MIN			0x2d
#define ASB100_VIN1_MAX			0x2e
#define ASB100_VIN2_MIN			0x2f
#define ASB100_VIN2_MAX			0x30
#define ASB100_VIN3_MIN			0x31
#define ASB100_VIN3_MAX			0x32
#define ASB100_VIN4_MIN			0x33
#define ASB100_VIN4_MAX			0x34
#define ASB100_VIN5_MIN			0x35
#define ASB100_VIN5_MAX			0x36
#define ASB100_VIN6_MIN			0x37
#define ASB100_VIN6_MAX			0x38
#define ASB100_TEMP0_MAX		0x39
#define ASB100_TEMP0_HYST		0x3a
#define ASB100_FAN0_MIN			0x3b
#define ASB100_FAN1_MIN			0x3c
#define ASB100_FAN2_MIN			0x3d
#define	ASB100_CONFIG			0x40
#define	ASB100_ALARM1			0x41
#define	ASB100_ALARM2			0x42
#define	ASB100_SMIM1			0x43
#define	ASB100_SMIM2			0x44
#define	ASB100_VID_FANDIV01		0x47 /* 0-3 vid, 4-5 fan0, 6-7 fan1 */
#define	ASB100_I2C_ADDR			0x48
#define	ASB100_CHIPID			0x49
#define	ASB100_I2C_SUBADDR		0x4a
#define	ASB100_PIN_FANDIV2		0x4b /* 6-7 fan2 */
#define	ASB100_IRQ			0x4c
#define	ASB100_BANK			0x4e
#define	ASB100_CHIPMAN			0x4f
#define ASB100_VID_CHIPID		0x58 /* 0 vid highbit, 1-7 chipid */
#define ASB100_PWM			0x59 /* 0-3 duty cycle, 7 enable */

/* TEMP1/2 sensors live on other chips, pointed to by the I2C_SUBADDR reg */
#define	ASB100_SUB1_TEMP1		0x50 /* LM75 format */
#define	ASB100_SUB1_TEMP1_HYST		0x53
#define	ASB100_SUB1_TEMP1_MAX		0x55
#define	ASB100_SUB2_TEMP2		0x50 /* LM75 format */
#define	ASB100_SUB2_TEMP2_HYST		0x53
#define	ASB100_SUB2_TEMP2_MAX		0x55

/* Sensors */
#define ASB100_SENSOR_VIN0	0
#define ASB100_SENSOR_VIN1	1
#define ASB100_SENSOR_VIN2	2
#define ASB100_SENSOR_VIN3	3
#define ASB100_SENSOR_VIN4	4
#define ASB100_SENSOR_VIN5	5
#define ASB100_SENSOR_VIN6	6
#define ASB100_SENSOR_FAN0	7
#define ASB100_SENSOR_FAN1	8
#define ASB100_SENSOR_FAN2	9
#define ASB100_SENSOR_TEMP0	10
#define ASB100_SENSOR_TEMP1	11
#define ASB100_SENSOR_TEMP2	12
#define ASB100_SENSOR_TEMP3	13
#define ASB100_NUM_SENSORS	14

struct asbtm_softc {
	struct device	sc_dev;
	i2c_tag_t	sc_tag;
	i2c_addr_t	sc_addr;

	struct ksensor	sc_sensor[ASB100_NUM_SENSORS];
	struct ksensordev sc_sensordev;
	int		sc_fanmul[3];
	int		sc_satellite[2];
};

int	asbtm_banksel(struct asbtm_softc *, u_int8_t, u_int8_t *);
int	asbtm_match(struct device *, void *, void *);
void	asbtm_attach(struct device *, struct device *, void *);
void	asbtm_refresh(void *);

const struct cfattach asbtm_ca = {
	sizeof(struct asbtm_softc), asbtm_match, asbtm_attach
};

struct cfdriver asbtm_cd = {
	NULL, "asbtm", DV_DULL
};

int
asbtm_match(struct device *parent, void *match, void *aux)
{
	struct i2c_attach_args *ia = aux;

	if (strcmp(ia->ia_name, "asb100") == 0)
		return (1);

	return (0);
}

int
asbtm_banksel(struct asbtm_softc *sc, u_int8_t new_bank, u_int8_t *orig_bank)
{
	u_int8_t cmd, data;

	new_bank &= 0xf;

	cmd = ASB100_BANK;
	if (iic_exec(sc->sc_tag, I2C_OP_READ_WITH_STOP, sc->sc_addr,
	    &cmd, sizeof cmd, &data, sizeof data, 0))
		return (-1);

	if (orig_bank != NULL)
		*orig_bank = data & 0x0f;

	if ((data & 0xf) != new_bank) {
		cmd = ASB100_BANK;
		data = new_bank | (data & 0xf0);
		if (iic_exec(sc->sc_tag, I2C_OP_WRITE_WITH_STOP, sc->sc_addr,
		    &cmd, sizeof cmd, &data, sizeof data, 0))
			return (-1);
	}

	return (0);
}

void
asbtm_attach(struct device *parent, struct device *self, void *aux)
{
	struct asbtm_softc *sc = (struct asbtm_softc *)self;
	struct i2c_attach_args *ia = aux;
	u_int8_t orig_bank, cmd, data;
	int i;

	sc->sc_tag = ia->ia_tag;
	sc->sc_addr = ia->ia_addr;

	iic_acquire_bus(sc->sc_tag, 0);

	if (asbtm_banksel(sc, 0, &orig_bank) == -1) {
		printf(": cannot get/set register bank\n");
		iic_release_bus(sc->sc_tag, 0);
		return;
	}

	cmd = ASB100_VID_FANDIV01;
	if (iic_exec(sc->sc_tag, I2C_OP_READ_WITH_STOP, sc->sc_addr,
	    &cmd, sizeof cmd, &data, sizeof data, 0)) {
		printf(": cannot get fan01 register\n");
		iic_release_bus(sc->sc_tag, 0);
		return;
	}
	sc->sc_fanmul[0] = (1 << (data >> 4) & 0x3);
	sc->sc_fanmul[1] = (1 << (data >> 6) & 0x3);

	cmd = ASB100_PIN_FANDIV2;
	if (iic_exec(sc->sc_tag, I2C_OP_READ_WITH_STOP, sc->sc_addr,
	    &cmd, sizeof cmd, &data, sizeof data, 0)) {
		printf(": cannot get fan2 register\n");
		iic_release_bus(sc->sc_tag, 0);
		return;
	}
	sc->sc_fanmul[0] = (1 << (data >> 6) & 0x3);

	cmd = ASB100_I2C_SUBADDR;
	if (iic_exec(sc->sc_tag, I2C_OP_READ_WITH_STOP, sc->sc_addr,
	    &cmd, sizeof cmd, &data, sizeof data, 0)) {
		printf(": cannot get satellite chip address register\n");
		iic_release_bus(sc->sc_tag, 0);
		return;
	}
	/* Maybe a relative address of zero means "not present" here... */
	sc->sc_satellite[0] = 0x48 + (data & 0xf);
	sc->sc_satellite[1] = 0x48 + ((data >> 4) & 0xf);

	iic_ignore_addr(sc->sc_satellite[0]);
	iic_ignore_addr(sc->sc_satellite[1]);
	if (sc->sc_satellite[0] == sc->sc_satellite[1])
		sc->sc_satellite[1] = -1;

	if (asbtm_banksel(sc, orig_bank, NULL) == -1) {
		printf(": cannot restore saved bank %d\n", orig_bank);
		iic_release_bus(sc->sc_tag, 0);
		return;
	}

	iic_release_bus(sc->sc_tag, 0);

	/* Initialize sensor data. */
	strlcpy(sc->sc_sensordev.xname, sc->sc_dev.dv_xname,
	    sizeof(sc->sc_sensordev.xname));

	sc->sc_sensor[ASB100_SENSOR_VIN0].type = SENSOR_VOLTS_DC;
	sc->sc_sensor[ASB100_SENSOR_VIN1].type = SENSOR_VOLTS_DC;
	sc->sc_sensor[ASB100_SENSOR_VIN2].type = SENSOR_VOLTS_DC;
	sc->sc_sensor[ASB100_SENSOR_VIN3].type = SENSOR_VOLTS_DC;
	sc->sc_sensor[ASB100_SENSOR_VIN4].type = SENSOR_VOLTS_DC;
	sc->sc_sensor[ASB100_SENSOR_VIN5].type = SENSOR_VOLTS_DC;
	sc->sc_sensor[ASB100_SENSOR_VIN6].type = SENSOR_VOLTS_DC;

	sc->sc_sensor[ASB100_SENSOR_FAN0].type = SENSOR_FANRPM;
	sc->sc_sensor[ASB100_SENSOR_FAN1].type = SENSOR_FANRPM;
	sc->sc_sensor[ASB100_SENSOR_FAN2].type = SENSOR_FANRPM;

	sc->sc_sensor[ASB100_SENSOR_TEMP0].type = SENSOR_TEMP;
	strlcpy(sc->sc_sensor[ASB100_SENSOR_TEMP0].desc, "External",
	    sizeof(sc->sc_sensor[ASB100_SENSOR_TEMP0].desc));

	sc->sc_sensor[ASB100_SENSOR_TEMP1].type = SENSOR_TEMP;
	strlcpy(sc->sc_sensor[ASB100_SENSOR_TEMP1].desc, "Internal",
	    sizeof(sc->sc_sensor[ASB100_SENSOR_TEMP1].desc));

	sc->sc_sensor[ASB100_SENSOR_TEMP2].type = SENSOR_TEMP;
	strlcpy(sc->sc_sensor[ASB100_SENSOR_TEMP2].desc, "Internal",
	    sizeof(sc->sc_sensor[ASB100_SENSOR_TEMP2].desc));
	if (sc->sc_satellite[1] == -1)
		sc->sc_sensor[ASB100_SENSOR_TEMP2].flags |= SENSOR_FINVALID;

	sc->sc_sensor[ASB100_SENSOR_TEMP3].type = SENSOR_TEMP;
	strlcpy(sc->sc_sensor[ASB100_SENSOR_TEMP3].desc, "External",
	    sizeof(sc->sc_sensor[ASB100_SENSOR_TEMP3].desc));

	if (sensor_task_register(sc, asbtm_refresh, 5) == NULL) {
		printf(", unable to register update task\n");
		return;
	}

	for (i = 0; i < ASB100_NUM_SENSORS; i++)
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
	else {
		sens->value = 1350000 / tmp;
		sens->flags &= ~SENSOR_FINVALID;
	}
}

void
asbtm_refresh(void *arg)
{
	struct asbtm_softc *sc = arg;
	u_int8_t orig_bank, cmd, data;
	int8_t sdata;
	u_int16_t sdata2;

	iic_acquire_bus(sc->sc_tag, 0);

	if (asbtm_banksel(sc, 0, &orig_bank) == -1) {
		printf("%s: cannot get/set register bank\n",
		    sc->sc_dev.dv_xname);
		iic_release_bus(sc->sc_tag, 0);
		return;
	}

	cmd = ASB100_VIN0;
	if (iic_exec(sc->sc_tag, I2C_OP_READ_WITH_STOP, sc->sc_addr,
	    &cmd, sizeof cmd, &data, sizeof data, 0) == 0)
		sc->sc_sensor[ASB100_SENSOR_VIN0].value = (data * 1000000) / 16;

	cmd = ASB100_VIN1;
	if (iic_exec(sc->sc_tag, I2C_OP_READ_WITH_STOP, sc->sc_addr,
	    &cmd, sizeof cmd, &data, sizeof data, 0) == 0)
		sc->sc_sensor[ASB100_SENSOR_VIN1].value = (data * 1000000) / 16;

	cmd = ASB100_VIN2;
	if (iic_exec(sc->sc_tag, I2C_OP_READ_WITH_STOP, sc->sc_addr,
	    &cmd, sizeof cmd, &data, sizeof data, 0) == 0)
		sc->sc_sensor[ASB100_SENSOR_VIN2].value = (data * 1000000) / 16;

	cmd = ASB100_VIN3;
	if (iic_exec(sc->sc_tag, I2C_OP_READ_WITH_STOP, sc->sc_addr,
	    &cmd, sizeof cmd, &data, sizeof data, 0) == 0)
		sc->sc_sensor[ASB100_SENSOR_VIN3].value = (data * 1000000) / 16;

	cmd = ASB100_VIN4;
	if (iic_exec(sc->sc_tag, I2C_OP_READ_WITH_STOP, sc->sc_addr,
	    &cmd, sizeof cmd, &data, sizeof data, 0) == 0)
		sc->sc_sensor[ASB100_SENSOR_VIN4].value = (data * 1000000) / 16;

	cmd = ASB100_VIN5;
	if (iic_exec(sc->sc_tag, I2C_OP_READ_WITH_STOP, sc->sc_addr,
	    &cmd, sizeof cmd, &data, sizeof data, 0) == 0)
		sc->sc_sensor[ASB100_SENSOR_VIN5].value = (data * 1000000) / 16;

	cmd = ASB100_VIN6;
	if (iic_exec(sc->sc_tag, I2C_OP_READ_WITH_STOP, sc->sc_addr,
	    &cmd, sizeof cmd, &data, sizeof data, 0) == 0)
		sc->sc_sensor[ASB100_SENSOR_VIN6].value = (data * 1000000) / 16;

	cmd = ASB100_FAN0;
	if (iic_exec(sc->sc_tag, I2C_OP_READ_WITH_STOP, sc->sc_addr,
	    &cmd, sizeof cmd, &data, sizeof data, 0) == 0)
		fanval(&sc->sc_sensor[ASB100_SENSOR_FAN0],
		    sc->sc_fanmul[0], data);

	cmd = ASB100_FAN1;
	if (iic_exec(sc->sc_tag, I2C_OP_READ_WITH_STOP, sc->sc_addr,
	    &cmd, sizeof cmd, &data, sizeof data, 0) == 0)
		fanval(&sc->sc_sensor[ASB100_SENSOR_FAN1],
		    sc->sc_fanmul[1], data);

	cmd = ASB100_FAN2;
	if (iic_exec(sc->sc_tag, I2C_OP_READ_WITH_STOP, sc->sc_addr,
	    &cmd, sizeof cmd, &data, sizeof data, 0) == 0)
		fanval(&sc->sc_sensor[ASB100_SENSOR_FAN2],
		    sc->sc_fanmul[2], data);

	cmd = ASB100_TEMP0;
	if (iic_exec(sc->sc_tag, I2C_OP_READ_WITH_STOP, sc->sc_addr,
	    &cmd, sizeof cmd, &sdata, sizeof sdata, 0) == 0)
		sc->sc_sensor[ASB100_SENSOR_TEMP0].value = 273150000 +
		    1000000 * sdata;

	cmd = ASB100_TEMP3;
	if (iic_exec(sc->sc_tag, I2C_OP_READ_WITH_STOP, sc->sc_addr,
	    &cmd, sizeof cmd, &data, sizeof sdata, 0) == 0)
		sc->sc_sensor[ASB100_SENSOR_TEMP3].value = 273150000 +
		    1000000 * sdata;

	/* Read satellite chips for TEMP1/TEMP2 */
	cmd = ASB100_SUB1_TEMP1;
	if (sc->sc_satellite[0] != -1) {
		if (iic_exec(sc->sc_tag, I2C_OP_READ_WITH_STOP,
		    sc->sc_satellite[0], &cmd, sizeof cmd, &sdata2,
		    sizeof sdata2, 0) == 0 && sdata2 != 0xffff) {
			sc->sc_sensor[ASB100_SENSOR_TEMP1].value = 273150000 +
			    500000 * (betoh16(sdata2) / 128);
			sc->sc_sensor[ASB100_SENSOR_TEMP2].flags &=
			    ~SENSOR_FINVALID;
		} else {
			sc->sc_sensor[ASB100_SENSOR_TEMP2].flags |=
			    SENSOR_FINVALID;
		}
	}

	cmd = ASB100_SUB2_TEMP2;
	if (sc->sc_satellite[1] != -1) {
		if (iic_exec(sc->sc_tag, I2C_OP_READ_WITH_STOP,
		    sc->sc_satellite[1], &cmd, sizeof cmd, &sdata2,
		    sizeof sdata2, 0) == 0 && sdata2 != 0xffff) {
			sc->sc_sensor[ASB100_SENSOR_TEMP2].value = 273150000 +
			    500000 * (betoh16(sdata2) / 128);
			sc->sc_sensor[ASB100_SENSOR_TEMP2].flags &=
			    ~SENSOR_FINVALID;
		} else {
			sc->sc_sensor[ASB100_SENSOR_TEMP2].flags |=
			    SENSOR_FINVALID;
		}
	}

	asbtm_banksel(sc, orig_bank, NULL);

	iic_release_bus(sc->sc_tag, 0);
}
