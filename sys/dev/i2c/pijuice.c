/*	$OpenBSD: pijuice.c,v 1.3 2022/10/25 19:32:18 mglocker Exp $ */

/*
 * Copyright (c) 2022 Marcus Glocker <mglocker@openbsd.org>
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

#include <machine/apmvar.h>

#include <dev/i2c/i2cvar.h>

#include "apm.h"

#ifdef PIJUICE_DEBUG
#define DPRINTF(x)	printf x
#else
#define DPRINTF(x)
#endif

/* I2C Status commands. */
#define PIJUICE_CMD_STATUS				0x40
#define PIJUICE_CMD_FAULT_EVENT				0x44
#define PIJUICE_CMD_CHARGE_LEVEL			0x41
#define PIJUICE_CMD_BUTTON_EVENT			0x45
#define PIJUICE_CMD_BATTERY_TEMP			0x47
#define PIJUICE_CMD_BATTERY_VOLTAGE			0x49
#define PIJUICE_CMD_BATTERY_CURRENT			0x4b
#define PIJUICE_CMD_IO_VOLTAGE				0x4d
#define PIJUICE_CMD_IO_CURRENT				0x4f
#define PIJUICE_CMD_LED_STATE				0x66
#define PIJUICE_CMD_LED_BLINK				0x68
#define PIJUICE_CMD_IO_PIN_ACCESS			0x75

/* I2C Config commands. */
#define PIJUICE_CMD_CHARGING_CONFIG			0x51
#define PIJUICE_CMD_BATTERY_PROFILE_ID			0x52
#define PIJUICE_CMD_BATTERY_PROFILE			0x53
#define PIJUICE_CMD_BATTERY_EXT_PROFILE			0x54
#define PIJUICE_CMD_BATTERY_TEMP_SENSE_CONFIG		0x5d
#define PIJUICE_CMD_POWER_INPUTS_CONFIG			0x5e
#define PIJUICE_CMD_RUN_PIN_CONFIG			0x5f
#define PIJUICE_CMD_POWER_REGULATOR_CONFIG		0x60
#define PIJUICE_CMD_LED_CONFIG				0x6a
#define PIJUICE_CMD_BUTTON_CONFIG			0x6e
#define PIJUICE_CMD_IO_CONFIG				0x72
#define PIJUICE_CMD_I2C_ADDRESS				0x7c
#define PIJUICE_CMD_ID_EEPROM_WRITE_PROTECT_CTRL	0x7e
#define PIJUICE_CMD_ID_EEPROM_ADDRESS			0x7f
#define PIJUICE_CMD_RESET_TO_DEFAULT			0xf0
#define PIJUICE_CMD_FIRMWARE_VERSION			0xfd

/* Sensors. */
#define PIJUICE_NSENSORS	3
enum pijuice_sensors {
	PIJUICE_SENSOR_CHARGE,	/* 0 */
	PIJUICE_SENSOR_TEMP,	/* 1 */
	PIJUICE_SENSOR_VOLTAGE,	/* 2 */
};

struct pijuice_softc {
	struct device		sc_dev;
	i2c_tag_t		sc_tag;
	int			sc_addr;

	struct ksensor		sc_sensor[PIJUICE_NSENSORS];
	struct ksensordev	sc_sensordev;
};

struct pijuice_softc *pijuice_sc;

int	pijuice_match(struct device *, void *, void *);
void	pijuice_attach(struct device *, struct device *, void *);
int	pijuice_read(struct pijuice_softc *, uint8_t *, uint8_t,
	    uint8_t *, uint8_t);
int	pijuice_write(struct pijuice_softc *, uint8_t *, uint8_t);
int	pijuice_get_fw_version(struct pijuice_softc *, const int, char *);
int	pijuice_get_bcl(struct pijuice_softc *, uint8_t *);
int	pijuice_get_status(struct pijuice_softc *, uint8_t *);
int	pijuice_get_temp(struct pijuice_softc *sc, uint8_t *);
int	pijuice_get_voltage(struct pijuice_softc *sc, uint16_t *);
void	pijuice_refresh_sensors(void *);
int	pijuice_apminfo(struct apm_power_info *);

const struct cfattach pijuice_ca = {
	sizeof(struct pijuice_softc), pijuice_match, pijuice_attach
};

struct cfdriver pijuice_cd = {
	NULL, "pijuice", DV_DULL
};

int
pijuice_match(struct device *parent, void *v, void *arg)
{
	struct i2c_attach_args *ia = arg;

	if (strcmp(ia->ia_name, "pisupply,pijuice") == 0)
		return 1;

	return 0;
}

void
pijuice_attach(struct device *parent, struct device *self, void *arg)
{
	struct pijuice_softc *sc = (struct pijuice_softc *)self;
	struct i2c_attach_args *ia = arg;
	char fw_version[8];
	int i;

	pijuice_sc = sc;

	sc->sc_tag = ia->ia_tag;
	sc->sc_addr = ia->ia_addr;

	/* Setup sensor framework. */
	strlcpy(sc->sc_sensor[PIJUICE_SENSOR_CHARGE].desc, "battery charge",
	    sizeof(sc->sc_sensor[PIJUICE_SENSOR_CHARGE].desc));
	sc->sc_sensor[PIJUICE_SENSOR_CHARGE].type = SENSOR_PERCENT;

	strlcpy(sc->sc_sensor[PIJUICE_SENSOR_TEMP].desc, "battery temperature",
	    sizeof(sc->sc_sensor[PIJUICE_SENSOR_TEMP].desc));
	sc->sc_sensor[PIJUICE_SENSOR_TEMP].type = SENSOR_TEMP;

	strlcpy(sc->sc_sensor[PIJUICE_SENSOR_VOLTAGE].desc, "battery voltage",
	    sizeof(sc->sc_sensor[PIJUICE_SENSOR_VOLTAGE].desc));
	sc->sc_sensor[PIJUICE_SENSOR_VOLTAGE].type = SENSOR_VOLTS_DC;

	strlcpy(sc->sc_sensordev.xname, sc->sc_dev.dv_xname,
	    sizeof(sc->sc_sensordev.xname));
	for (i = 0; i < PIJUICE_NSENSORS; i++)
		sensor_attach(&sc->sc_sensordev, &sc->sc_sensor[i]);
	sensordev_install(&sc->sc_sensordev);

	if (sensor_task_register(sc, pijuice_refresh_sensors, 5) == NULL) {
		printf(": unable to register update task\n");
		return;
	}

	/* Print device firmware version. */
	if (pijuice_get_fw_version(sc, sizeof(fw_version), fw_version) == -1) {
		printf(": can't get firmware version\n");
		return;
	}
	printf(": firmware version %s\n", fw_version);

#if NAPM > 0
	apm_setinfohook(pijuice_apminfo);
#endif
}

int
pijuice_read(struct pijuice_softc *sc, uint8_t *cmd, uint8_t cmd_len,
    uint8_t *data, uint8_t data_len)
{
	int error;

	iic_acquire_bus(sc->sc_tag, I2C_F_POLL);
	error = iic_exec(sc->sc_tag, I2C_OP_READ_WITH_STOP, sc->sc_addr,
	    cmd, cmd_len, data, data_len, I2C_F_POLL);
	iic_release_bus(sc->sc_tag, I2C_F_POLL);

	return error;
}

int
pijuice_write(struct pijuice_softc *sc, uint8_t *data, uint8_t data_len)
{
	int error;

	iic_acquire_bus(sc->sc_tag, I2C_F_POLL);
	error = iic_exec(sc->sc_tag, I2C_OP_WRITE_WITH_STOP, sc->sc_addr,
	    NULL, 0, data, data_len, I2C_F_POLL);
	iic_release_bus(sc->sc_tag, I2C_F_POLL);

	return error;
}

/*
 * Get firmware version.
 */
int
pijuice_get_fw_version(struct pijuice_softc *sc, const int fw_version_size,
    char *fw_version)
{
	uint8_t cmd;
	uint8_t data[2];
	uint8_t fw_version_minor, fw_version_major;

	cmd = PIJUICE_CMD_FIRMWARE_VERSION;
	memset(data, 0, sizeof(data));
	if (pijuice_read(sc, &cmd, sizeof(cmd), data, sizeof(data)))
		return -1;

	fw_version_major = data[0] >> 4;
	fw_version_minor = (data[0] << 4 & 0xf0) >> 4;
	snprintf(fw_version, fw_version_size, "%d.%d",
	    fw_version_major, fw_version_minor);

	return 0;
}

/*
 * Get battery charge level.
 */
int
pijuice_get_bcl(struct pijuice_softc *sc, uint8_t *bcl)
{
	uint8_t cmd;
	uint8_t data;

	cmd = PIJUICE_CMD_CHARGE_LEVEL;
	data = 0;
	if (pijuice_read(sc, &cmd, sizeof(cmd), &data, sizeof(data)))
		return -1;

	*bcl = data;

	return 0;
}

/*
 * Get AC and Battery status.
 *
 */
#define PIJUICE_STATUS_FAULT_MASK(status)	((status >> 0) & 0x01)
#define PIJUICE_STATUS_BUTTON_MASK(status)	((status >> 0) & 0x02)
#define PIJUICE_STATUS_BATT_MASK(status)	((status >> 2) & 0x03)
#define   PIJUICE_STATUS_BATT_NORMAL		0
#define   PIJUICE_STATUS_BATT_CHARGE_AC		1
#define   PIJUICE_STATUS_BATT_CHARGE_5V		2
#define   PIJUICE_STATUS_BATT_ABSENT		3
#define PIJUICE_STATUS_AC_MASK(status)		((status >> 4) & 0x03)
#define   PIJUICE_STATUS_AC_ABSENT		0
#define   PIJUICE_STATUS_AC_BAD			1
#define   PIJUICE_STATUS_AC_WEAK		2
#define   PIJUICE_STATUS_AC_PRESENT		3
#define PIJUICE_STATUS_AC_IN_MASK(status)	((status >> 6) & 0x03)
int
pijuice_get_status(struct pijuice_softc *sc, uint8_t *status)
{
	uint8_t cmd;
	uint8_t data;

	cmd = PIJUICE_CMD_STATUS;
	data = 0;
	if (pijuice_read(sc, &cmd, sizeof(cmd), &data, sizeof(data)))
		return -1;

	*status = data;

	return 0;
}

/*
 * Get battery temperature.
 */
int
pijuice_get_temp(struct pijuice_softc *sc, uint8_t *temp)
{
	uint8_t cmd;
	uint8_t data[2];

	cmd = PIJUICE_CMD_BATTERY_TEMP;
	memset(data, 0, sizeof(data));
	if (pijuice_read(sc, &cmd, sizeof(cmd), data, sizeof(data)))
		return -1;

	*temp = (uint8_t)data[0];
	if (data[0] & (1 << 7)) {
		/* Minus degree. */
		*temp = *temp - (1 << 8);
	}

	return 0;
}

/*
 * Get battery voltage.
 */
int
pijuice_get_voltage(struct pijuice_softc *sc, uint16_t *voltage)
{
	uint8_t cmd;
	uint8_t data[2];

	cmd = PIJUICE_CMD_BATTERY_VOLTAGE;
	memset(data, 0, sizeof(data));
	if (pijuice_read(sc, &cmd, sizeof(cmd), data, sizeof(data)))
		return -1;

	*voltage = (uint16_t)(data[1] << 8) | data[0];

	return 0;
}

void
pijuice_refresh_sensors(void *arg)
{
	struct pijuice_softc *sc = arg;
	uint8_t val8;
	uint16_t val16;
	int i;

	for (i = 0; i < PIJUICE_NSENSORS; i++)
		sc->sc_sensor[i].flags |= SENSOR_FINVALID;

	if (pijuice_get_bcl(sc, &val8) == 0) {
		DPRINTF(("%s: Battery Charge Level=%d\n", __func__, val8));

		sc->sc_sensor[0].value = val8 * 1000;
		sc->sc_sensor[0].flags &= ~SENSOR_FINVALID;
	}

	if (pijuice_get_temp(sc, &val8) == 0) {
		DPRINTF(("%s: Battery Temperature=%d\n", __func__, val8));

		sc->sc_sensor[PIJUICE_SENSOR_TEMP].value =
		    273150000 + 1000000 * val8;
		sc->sc_sensor[PIJUICE_SENSOR_TEMP].flags &= ~SENSOR_FINVALID;
	}

	if (pijuice_get_voltage(sc, &val16) == 0) {
		DPRINTF(("%s: Battery Voltage=%d\n", __func__, val16));

		sc->sc_sensor[PIJUICE_SENSOR_VOLTAGE].value = val16 * 1000;
		sc->sc_sensor[PIJUICE_SENSOR_VOLTAGE].flags &= ~SENSOR_FINVALID;
	}
}

#if NAPM > 0
int
pijuice_apminfo(struct apm_power_info *info)
{
	struct pijuice_softc *sc = pijuice_sc;
	uint8_t val8;

	info->battery_state = APM_BATT_UNKNOWN;
	info->ac_state = APM_AC_UNKNOWN;
	info->battery_life = 0;
	info->minutes_left = -1;

	if (pijuice_get_bcl(sc, &val8) == 0) {
		DPRINTF(("%s: Battery Charge Level=%d\n", __func__, val8));

		info->battery_life = val8;
		/* On "normal load" we suck 1% battery in 30 seconds. */
		info->minutes_left = (val8 * 30) / 60;
	}

	if (pijuice_get_status(sc, &val8) == 0) {
		DPRINTF(("%s: Battery Status=%d\n",
		    __func__, PIJUICE_STATUS_BATT_MASK(val8)));

		switch (PIJUICE_STATUS_BATT_MASK(val8)) {
		case PIJUICE_STATUS_BATT_NORMAL:
			if (info->battery_life > 50)
				info->battery_state = APM_BATT_HIGH;
			else if (info->battery_life > 25)
				info->battery_state = APM_BATT_LOW;
			else
				info->battery_state = APM_BATT_CRITICAL;
			break;
		case PIJUICE_STATUS_BATT_CHARGE_AC:
		case PIJUICE_STATUS_BATT_CHARGE_5V:
			info->battery_state = APM_BATT_CHARGING;
			info->minutes_left =
			    ((99 * 30) / 60) - info->minutes_left;
			break;
		case PIJUICE_STATUS_BATT_ABSENT:
			info->battery_state = APM_BATTERY_ABSENT;
			break;
		}

		DPRINTF(("%s: AC Status=%d\n",
		    __func__, PIJUICE_STATUS_AC_MASK(val8)));

		switch (PIJUICE_STATUS_AC_MASK(val8)) {
		case PIJUICE_STATUS_AC_ABSENT:
			info->ac_state = APM_AC_OFF;
			break;
		case PIJUICE_STATUS_AC_BAD:
		case PIJUICE_STATUS_AC_WEAK:
			info->ac_state = APM_AC_BACKUP;
			break;
		case PIJUICE_STATUS_AC_PRESENT:
			info->ac_state = APM_AC_ON;
			break;
                }
	}

        return 0;
}
#endif
