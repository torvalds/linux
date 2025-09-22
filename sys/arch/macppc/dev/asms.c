/*	$OpenBSD: asms.c,v 1.9 2023/12/26 14:04:50 miod Exp $	*/
/*
 * Copyright (c) 2005 Xavier Santolaria <xsa@openbsd.org>
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

/*
 * A driver for the Apple Sudden Motion Sensor based on notes from
 * https://johannes.sipsolutions.net/PowerBook/apple-motion-sensor-specification
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/device.h>
#include <sys/sensors.h>

#include <dev/i2c/i2cvar.h>

/* ASMS Registers */
#define ASMS_REG_COMMAND	0x00
#define ASMS_REG_STATUS		0x01
#define ASMS_REG_RCONTROL1	0x02
#define ASMS_REG_RCONTROL2	0x03
#define ASMS_REG_RCONTROL3	0x04
#define ASMS_REG_RDATA1		0x05
#define ASMS_REG_RDATA2		0x06
#define ASMS_REG_DATA_X		0x20
#define ASMS_REG_DATA_Y		0x21
#define ASMS_REG_DATA_Z		0x22
#define ASMS_REG_SENS_LOW	0x26	/* init with 0x15 */
#define ASMS_REG_SENS_HIGH	0x27	/* init with 0x60 */
#define ASMS_REG_CONTROL_X	0x28	/* init with 0x08 */
#define ASMS_REG_CONTROL_Y	0x29	/* init with 0x0f */
#define ASMS_REG_CONTROL_Z	0x2a	/* init with 0x4f */
#define ASMS_REG_UNKNOWN1	0x2b	/* init with 0x14 */
#define ASMS_REG_VENDOR		0x2e
#define ASMS_CMD_READ_VER	0x01
#define ASMS_CMD_READ_MEM	0x02
#define ASMS_CMD_RESET		0x07
#define ASMS_CMD_START		0x08

/* Sensors */
#define ASMS_DATA_X		0
#define ASMS_DATA_Y		1
#define ASMS_DATA_Z		2
#define ASMS_NUM_SENSORS	3

struct asms_softc {
	struct device	sc_dev;
	i2c_tag_t	sc_tag;
	i2c_addr_t	sc_addr;

	struct ksensor	sc_sensor[ASMS_NUM_SENSORS];
	struct ksensordev sc_sensordev;
};

int	asms_match(struct device *, void *, void *);
void	asms_attach(struct device *, struct device *, void *);
void	asms_refresh(void *);

const struct cfattach asms_ca = {
	sizeof(struct asms_softc), asms_match, asms_attach
};

struct cfdriver asms_cd = {
	NULL, "asms", DV_DULL
};

int
asms_match(struct device *parent, void *match, void *aux)
{
	struct i2c_attach_args *ia = aux;

	if (strcmp(ia->ia_name, "AAPL,accelerometer_1") == 0)
		return (1);
	return (0);
}

void
asms_attach(struct device *parent, struct device *self, void *aux)
{
	struct asms_softc *sc = (struct asms_softc *)self;
	struct i2c_attach_args *ia = aux;
	u_int8_t cmd, data, rev1, rev2, ver1, ver2;
	int i, vflag = 0;

	sc->sc_tag = ia->ia_tag;
	sc->sc_addr = ia ->ia_addr;

	iic_acquire_bus(sc->sc_tag, 0);

	cmd = ASMS_REG_COMMAND; data = ASMS_CMD_START;
	if (iic_exec(sc->sc_tag, I2C_OP_WRITE_WITH_STOP,
	    sc->sc_addr, &cmd, sizeof cmd, &data, sizeof data, 0)) {
		iic_release_bus(sc->sc_tag, 0);
		printf(": cannot write command register\n");
		return;
	}
	delay(10000);

	cmd = ASMS_REG_RCONTROL1; data = 0x02;
	if (iic_exec(sc->sc_tag, I2C_OP_WRITE_WITH_STOP,
	    sc->sc_addr, &cmd, sizeof cmd, &data, sizeof data, 0)) {
		iic_release_bus(sc->sc_tag, 0);
		printf(": cannot write read control register\n");
		return;
	}

	cmd = ASMS_REG_RCONTROL2; data = 0x85;
	if (iic_exec(sc->sc_tag, I2C_OP_WRITE_WITH_STOP,
	    sc->sc_addr, &cmd, sizeof cmd, &data, sizeof data, 0)) {
		iic_release_bus(sc->sc_tag, 0);
		printf(": cannot write read control register\n");
		return;
	}

	cmd = ASMS_REG_RCONTROL3; data = 0x01;
	if (iic_exec(sc->sc_tag, I2C_OP_WRITE_WITH_STOP,
	    sc->sc_addr, &cmd, sizeof cmd, &data, sizeof data, 0)) {
		iic_release_bus(sc->sc_tag, 0);
		printf(": cannot write read control register\n");
		return;
	}

	cmd = ASMS_REG_COMMAND; data = ASMS_CMD_READ_MEM;
	if (iic_exec(sc->sc_tag, I2C_OP_WRITE_WITH_STOP,
	    sc->sc_addr, &cmd, sizeof cmd, &data, sizeof data, 0)) {
		iic_release_bus(sc->sc_tag, 0);
		printf(": cannot write command register\n");
		return;
	}
	delay(10000);

	cmd = ASMS_REG_RDATA1;
	if (iic_exec(sc->sc_tag, I2C_OP_READ_WITH_STOP,
	    sc->sc_addr, &cmd, sizeof cmd, &rev1, sizeof rev1, 0)) {
		iic_release_bus(sc->sc_tag, 0);
		printf(": cannot read data register\n");
		return;
	}

	cmd = ASMS_REG_RDATA2;
	if (iic_exec(sc->sc_tag, I2C_OP_READ_WITH_STOP,
	    sc->sc_addr, &cmd, sizeof cmd, &rev2, sizeof rev2, 0)) {
		iic_release_bus(sc->sc_tag, 0);
		printf(": cannot read data register\n");
		return;
	}

	cmd = ASMS_REG_COMMAND; data = ASMS_CMD_READ_VER;
	if (iic_exec(sc->sc_tag, I2C_OP_WRITE_WITH_STOP,
	    sc->sc_addr, &cmd, sizeof cmd, &data, sizeof data, 0)) {
		iic_release_bus(sc->sc_tag, 0);
		printf(": cannot write command register\n");
		return;
	}
	delay(10000);

	cmd = ASMS_REG_RDATA1;
	if (iic_exec(sc->sc_tag, I2C_OP_READ_WITH_STOP,
	    sc->sc_addr, &cmd, sizeof cmd, &ver1, sizeof ver1, 0)) {
		iic_release_bus(sc->sc_tag, 0);
		printf(": cannot read data register\n");
		return;
	}

	cmd = ASMS_REG_RDATA2;
	if (iic_exec(sc->sc_tag, I2C_OP_READ_WITH_STOP,
	    sc->sc_addr, &cmd, sizeof cmd, &ver2, sizeof ver2, 0)) {
		iic_release_bus(sc->sc_tag, 0);
		printf(": cannot read data register\n");
		return;
	}

	cmd = ASMS_REG_VENDOR;
	if (iic_exec(sc->sc_tag, I2C_OP_READ_WITH_STOP,
	    sc->sc_addr, &cmd, sizeof cmd, &data, sizeof data, 0)) {
		iic_release_bus(sc->sc_tag, 0);
		printf(": cannot read vendor register\n");
		return;
	}
	if (data & 0x10)
		vflag = 1;

	cmd = ASMS_REG_SENS_LOW; data = 0x15;
	if (iic_exec(sc->sc_tag, I2C_OP_WRITE_WITH_STOP,
	    sc->sc_addr, &cmd, sizeof cmd, &data, sizeof data, 0)) {
		iic_release_bus(sc->sc_tag, 0);
		printf(": cannot write sensibility low register\n");
		return;
	}

	cmd = ASMS_REG_SENS_HIGH; data = 0x60;
	if (iic_exec(sc->sc_tag, I2C_OP_WRITE_WITH_STOP,
	    sc->sc_addr, &cmd, sizeof cmd, &data, sizeof data, 0)) {
		iic_release_bus(sc->sc_tag, 0);
		printf(": cannot write sensibility high register\n");
		return;
	}

	cmd = ASMS_REG_CONTROL_X; data = 0x08;
	if (iic_exec(sc->sc_tag, I2C_OP_WRITE_WITH_STOP,
	    sc->sc_addr, &cmd, sizeof cmd, &data, sizeof data, 0)) {
		iic_release_bus(sc->sc_tag, 0);
		printf(": cannot write control X register\n");
		return;
	}

	cmd = ASMS_REG_CONTROL_Y; data= 0x0f;
	if (iic_exec(sc->sc_tag, I2C_OP_WRITE_WITH_STOP,
	    sc->sc_addr, &cmd, sizeof cmd, &data, sizeof data, 0)) {
		iic_release_bus(sc->sc_tag, 0);
		printf(": cannot write control Y register\n");
		return;
	}

	cmd = ASMS_REG_CONTROL_Z; data = 0x4f;
	if (iic_exec(sc->sc_tag, I2C_OP_WRITE_WITH_STOP,
	    sc->sc_addr, &cmd, sizeof cmd, &data, sizeof data, 0)) {
		iic_release_bus(sc->sc_tag, 0);
		printf(": cannot write control Z register\n");
		return;
	}

	cmd = ASMS_REG_UNKNOWN1; data = 0x14;
	if (iic_exec(sc->sc_tag, I2C_OP_WRITE_WITH_STOP,
	    sc->sc_addr, &cmd, sizeof cmd, &data, sizeof data, 0)) {
		iic_release_bus(sc->sc_tag, 0);
		printf(": cannot write unknown 1 register\n");
		return;
	}

	iic_release_bus(sc->sc_tag, 0);

	printf(": rev %x.%x, version %x.%x", rev1, rev2, ver1, ver2);

	/* Initialize sensor data. */
	strlcpy(sc->sc_sensordev.xname, sc->sc_dev.dv_xname,
	    sizeof(sc->sc_sensordev.xname));

	sc->sc_sensor[ASMS_DATA_X].type = SENSOR_INTEGER;
	strlcpy(sc->sc_sensor[ASMS_DATA_X].desc, "X_ACCEL",
	    sizeof(sc->sc_sensor[ASMS_DATA_X].desc));

	sc->sc_sensor[ASMS_DATA_Y].type = SENSOR_INTEGER;
	strlcpy(sc->sc_sensor[ASMS_DATA_Y].desc, "Y_ACCEL",
	    sizeof(sc->sc_sensor[ASMS_DATA_Y].desc));

	sc->sc_sensor[ASMS_DATA_Z].type = SENSOR_INTEGER;
	strlcpy(sc->sc_sensor[ASMS_DATA_Z].desc, "Z_ACCEL",
	    sizeof(sc->sc_sensor[ASMS_DATA_Z].desc));

	if (sensor_task_register(sc, asms_refresh, 5) == NULL) {
		printf(": unable to register update task\n");
		return;
	}

	for (i = 0; i < ASMS_NUM_SENSORS; i++)
		sensor_attach(&sc->sc_sensordev, &sc->sc_sensor[i]);
	sensordev_install(&sc->sc_sensordev);

	printf("\n");
}

void
asms_refresh(void *arg)
{
	struct asms_softc *sc = arg;
	u_int8_t cmd;
	int8_t sdata;

	iic_acquire_bus(sc->sc_tag, 0);

	cmd = ASMS_REG_DATA_X;
	if (iic_exec(sc->sc_tag, I2C_OP_READ_WITH_STOP,
	    sc->sc_addr, &cmd, sizeof cmd, &sdata, sizeof sdata, 0) == 0)
		sc->sc_sensor[ASMS_DATA_X].value = sdata;

	cmd = ASMS_REG_DATA_Y;
	if (iic_exec(sc->sc_tag, I2C_OP_READ_WITH_STOP,
	    sc->sc_addr, &cmd, sizeof cmd, &sdata, sizeof sdata, 0) == 0)
		sc->sc_sensor[ASMS_DATA_Y].value = sdata;

	cmd = ASMS_REG_DATA_Z;
	if (iic_exec(sc->sc_tag, I2C_OP_READ_WITH_STOP,
	    sc->sc_addr, &cmd, sizeof cmd, &sdata, sizeof sdata, 0) == 0)
		sc->sc_sensor[ASMS_DATA_Z].value = sdata;

	iic_release_bus(sc->sc_tag, 0);
}
