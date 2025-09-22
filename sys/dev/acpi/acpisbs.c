/* $OpenBSD: acpisbs.c,v 1.11 2022/10/26 16:06:42 kn Exp $ */
/*
 * Smart Battery subsystem device driver
 * ACPI 5.0 spec section 10
 *
 * Copyright (c) 2016-2017 joshua stein <jcs@openbsd.org>
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
 * TODO: support multiple batteries based on _SBS, make sc_battery an array and
 * poll each battery independently
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/device.h>
#include <sys/malloc.h>

#include <machine/apmvar.h>

#include <dev/acpi/acpireg.h>
#include <dev/acpi/acpivar.h>
#include <dev/acpi/acpidev.h>
#include <dev/acpi/amltypes.h>
#include <dev/acpi/dsdt.h>

#include <sys/sensors.h>

/* #define ACPISBS_DEBUG */

#ifdef ACPISBS_DEBUG
#define DPRINTF(x) printf x
#else
#define DPRINTF(x)
#endif

/* how often (in seconds) to re-poll data */
#define ACPISBS_POLL_FREQ	30

/* number of polls for reading data */
#define SMBUS_TIMEOUT		50

#define CHECK(kind, cmd, val, senst, sens) { \
	SMBUS_READ_##kind, SMBATT_CMD_##cmd, \
	offsetof(struct acpisbs_battery, val), \
	(SMBUS_READ_##kind == SMBUS_READ_BLOCK ? SMBUS_DATA_SIZE : 2), \
	#val, senst, sens }

const struct acpisbs_battery_check {
	uint8_t	mode;
	uint8_t command;
	size_t	offset;
	int	len;
	char	*name;
	int	sensor_type;
	char	*sensor_desc;
} acpisbs_battery_checks[] = {
	/* mode must be checked first */
	CHECK(WORD, BATTERY_MODE, mode, -1,
	    "mode flags"),
	CHECK(WORD, TEMPERATURE, temperature, SENSOR_TEMP,
	    "internal temperature"),
	CHECK(WORD, VOLTAGE, voltage, SENSOR_VOLTS_DC,
	    "voltage"),
	CHECK(WORD, CURRENT, current, SENSOR_AMPS,
	    "current being supplied"),
	CHECK(WORD, AVERAGE_CURRENT, avg_current, SENSOR_AMPS,
	    "average current supplied"),
	CHECK(WORD, RELATIVE_STATE_OF_CHARGE, rel_charge, SENSOR_PERCENT,
	    "remaining capacity"),
	CHECK(WORD, ABSOLUTE_STATE_OF_CHARGE, abs_charge, SENSOR_PERCENT,
	    "remaining of design capacity"),
	CHECK(WORD, REMAINING_CAPACITY, capacity, SENSOR_AMPHOUR,
	    "remaining capacity"),
	CHECK(WORD, FULL_CHARGE_CAPACITY, full_capacity, SENSOR_AMPHOUR,
	    "capacity when fully charged"),
	CHECK(WORD, RUN_TIME_TO_EMPTY, run_time, SENSOR_INTEGER,
	    "remaining run time minutes"),
	CHECK(WORD, AVERAGE_TIME_TO_EMPTY, avg_empty_time, SENSOR_INTEGER,
	    "avg remaining minutes"),
	CHECK(WORD, AVERAGE_TIME_TO_FULL, avg_full_time, SENSOR_INTEGER,
	    "avg minutes until full charge"),
	CHECK(WORD, CHARGING_CURRENT, charge_current, SENSOR_AMPS,
	    "desired charging rate"),
	CHECK(WORD, CHARGING_VOLTAGE, charge_voltage, SENSOR_VOLTS_DC,
	    "desired charging voltage"),
	CHECK(WORD, BATTERY_STATUS, status, -1,
	    "status"),
	CHECK(WORD, CYCLE_COUNT, cycle_count, SENSOR_INTEGER,
	    "charge and discharge cycles"),
	CHECK(WORD, DESIGN_CAPACITY, design_capacity, SENSOR_AMPHOUR,
	    "capacity of new battery"),
	CHECK(WORD, DESIGN_VOLTAGE, design_voltage, SENSOR_VOLTS_DC,
	    "voltage of new battery"),
	CHECK(WORD, SERIAL_NUMBER, serial, -1,
	    "serial number"),

	CHECK(BLOCK, MANUFACTURER_NAME, manufacturer, -1,
	    "manufacturer name"),
	CHECK(BLOCK, DEVICE_NAME, device_name, -1,
	    "battery model number"),
	CHECK(BLOCK, DEVICE_CHEMISTRY, device_chemistry, -1,
	    "battery chemistry"),
#if 0
	CHECK(WORD, SPECIFICATION_INFO, spec, -1,
	    NULL),
	CHECK(WORD, MANUFACTURE_DATE, manufacture_date, -1,
	    "date battery was manufactured"),
	CHECK(BLOCK, MANUFACTURER_DATA, oem_data, -1,
	    "manufacturer-specific data"),
#endif
};

extern void acpiec_read(struct acpiec_softc *, uint8_t, int, uint8_t *);
extern void acpiec_write(struct acpiec_softc *, uint8_t, int, uint8_t *);

int	acpisbs_match(struct device *, void *, void *);
void	acpisbs_attach(struct device *, struct device *, void *);
int	acpisbs_activate(struct device *, int);
void	acpisbs_setup_sensors(struct acpisbs_softc *);
void	acpisbs_refresh_sensors(struct acpisbs_softc *);
void	acpisbs_read(struct acpisbs_softc *);
int	acpisbs_notify(struct aml_node *, int, void *);

int	acpi_smbus_read(struct acpisbs_softc *, uint8_t, uint8_t, int, void *);

const struct cfattach acpisbs_ca = {
	sizeof(struct acpisbs_softc),
	acpisbs_match,
	acpisbs_attach,
	NULL,
	acpisbs_activate,
};

struct cfdriver acpisbs_cd = {
	NULL, "acpisbs", DV_DULL
};

const char *acpisbs_hids[] = {
	ACPI_DEV_SBS,
	NULL
};

int
acpisbs_match(struct device *parent, void *match, void *aux)
{
	struct acpi_attach_args *aa = aux;
	struct cfdata *cf = match;

	return (acpi_matchhids(aa, acpisbs_hids, cf->cf_driver->cd_name));
}

void
acpisbs_attach(struct device *parent, struct device *self, void *aux)
{
	struct acpisbs_softc *sc = (struct acpisbs_softc *)self;
	struct acpi_attach_args *aa = aux;
	int64_t sbs, val;

	sc->sc_acpi = (struct acpi_softc *)parent;
	sc->sc_devnode = aa->aaa_node;
	sc->sc_batteries_present = 0;

	memset(&sc->sc_battery, 0, sizeof(sc->sc_battery));

	getmicrouptime(&sc->sc_lastpoll);

	if (aml_evalinteger(sc->sc_acpi, sc->sc_devnode, "_SBS", 0, NULL, &sbs))
		return;

	/*
	 * The parent node of the device block containing the _HID must also
	 * have an _EC node, which contains the base address and query value.
	 */
	if (aml_evalinteger(sc->sc_acpi, sc->sc_devnode->parent, "_EC", 0,
	    NULL, &val))
		return;
	sc->sc_ec_base = (val >> 8) & 0xff;

	if (!sc->sc_acpi->sc_ec)
		return;
	sc->sc_ec = sc->sc_acpi->sc_ec;

	printf(": %s", sc->sc_devnode->name);

	if (sbs > 0)
		acpisbs_read(sc);

	if (sc->sc_batteries_present) {
		if (sc->sc_battery.device_name[0])
			printf(" model \"%s\"", sc->sc_battery.device_name);
		if (sc->sc_battery.serial)
			printf(" serial %d", sc->sc_battery.serial);
		if (sc->sc_battery.device_chemistry[0])
			printf(" type %s", sc->sc_battery.device_chemistry);
		if (sc->sc_battery.manufacturer[0])
			printf(" oem \"%s\"", sc->sc_battery.manufacturer);
	}

	printf("\n");

	acpisbs_setup_sensors(sc);
	acpisbs_refresh_sensors(sc);

	/*
	 * Request notification of SCI events on the subsystem itself, but also
	 * periodically poll as a fallback in case those events never arrive.
	 */
	aml_register_notify(sc->sc_devnode->parent, aa->aaa_dev,
	    acpisbs_notify, sc, ACPIDEV_POLL);

	sc->sc_acpi->sc_havesbs = 1;
}

void
acpisbs_read(struct acpisbs_softc *sc)
{
	int i;

	for (i = 0; i < nitems(acpisbs_battery_checks); i++) {
		const struct acpisbs_battery_check check =
		    acpisbs_battery_checks[i];
		void *p = (void *)&sc->sc_battery + check.offset;

		acpi_smbus_read(sc, check.mode, check.command, check.len, p);

		if (check.mode == SMBUS_READ_BLOCK)
			DPRINTF(("%s: %s: %s\n", sc->sc_dev.dv_xname,
			    check.name, (char *)p));
		else
			DPRINTF(("%s: %s: %u\n", sc->sc_dev.dv_xname,
			    check.name, *(uint16_t *)p));

		if (check.command == SMBATT_CMD_BATTERY_MODE) {
			uint16_t *ival = (uint16_t *)p;
			if (*ival == 0) {
				/* battery not present, skip further checks */
				sc->sc_batteries_present = 0;
				break;
			}

			sc->sc_batteries_present = 1;

			if (*ival & SMBATT_BM_CAPACITY_MODE)
				sc->sc_battery.units = ACPISBS_UNITS_MW;
			else
				sc->sc_battery.units = ACPISBS_UNITS_MA;
		}
	}
}

void
acpisbs_setup_sensors(struct acpisbs_softc *sc)
{
	int i;

	memset(&sc->sc_sensordev, 0, sizeof(sc->sc_sensordev));
	strlcpy(sc->sc_sensordev.xname, DEVNAME(sc),
	    sizeof(sc->sc_sensordev.xname));

	sc->sc_sensors = mallocarray(sizeof(struct ksensor),
	    nitems(acpisbs_battery_checks), M_DEVBUF, M_WAITOK | M_ZERO);

	for (i = 0; i < nitems(acpisbs_battery_checks); i++) {
		const struct acpisbs_battery_check check =
		    acpisbs_battery_checks[i];

		if (check.sensor_type < 0)
			continue;

		strlcpy(sc->sc_sensors[i].desc, check.sensor_desc,
		    sizeof(sc->sc_sensors[i].desc));

		if (check.sensor_type == SENSOR_AMPHOUR &&
		    sc->sc_battery.units == ACPISBS_UNITS_MW)
			/* translate to watt-hours */
			sc->sc_sensors[i].type = SENSOR_WATTHOUR;
		else
			sc->sc_sensors[i].type = check.sensor_type;

		sc->sc_sensors[i].value = 0;
		sensor_attach(&sc->sc_sensordev, &sc->sc_sensors[i]);
	}

	sensordev_install(&sc->sc_sensordev);
}

void
acpisbs_refresh_sensors(struct acpisbs_softc *sc)
{
	int i;

	for (i = 0; i < nitems(acpisbs_battery_checks); i++) {
		const struct acpisbs_battery_check check =
		    acpisbs_battery_checks[i];
		void *p = (void *)&sc->sc_battery + check.offset;
		uint16_t *ival = (uint16_t *)p;

		if (check.sensor_type < 0)
			continue;

		if (sc->sc_batteries_present) {
			sc->sc_sensors[i].flags = 0;
			sc->sc_sensors[i].status = SENSOR_S_OK;

			switch (check.sensor_type) {
			case SENSOR_AMPS:
				sc->sc_sensors[i].value = *ival * 100;
				break;

			case SENSOR_AMPHOUR:
			case SENSOR_WATTHOUR:
				sc->sc_sensors[i].value = *ival * 10000;
				break;

			case SENSOR_PERCENT:
				sc->sc_sensors[i].value = *ival * 1000;
				break;

#if 0
			case SENSOR_STRING:
				strlcpy(sc->sc_sensors[i].string, (char *)p,
				    sizeof(sc->sc_sensors[i].string));
				break;
#endif
			case SENSOR_TEMP:
				/* .1 degK */
				sc->sc_sensors[i].value = (*ival * 10000) +
				    273150000;
				break;

			case SENSOR_VOLTS_DC:
				sc->sc_sensors[i].value = *ival * 1000;
				break;

			default:
				if (*ival == ACPISBS_VALUE_UNKNOWN) {
					sc->sc_sensors[i].value = 0;
					sc->sc_sensors[i].status =
					    SENSOR_S_UNKNOWN;
					sc->sc_sensors[i].flags =
					    SENSOR_FUNKNOWN;
				} else
					sc->sc_sensors[i].value = *ival;
			}
		} else {
			sc->sc_sensors[i].value = 0;
			sc->sc_sensors[i].status = SENSOR_S_UNKNOWN;
			sc->sc_sensors[i].flags = SENSOR_FUNKNOWN;
		}
	}
}

int
acpisbs_activate(struct device *self, int act)
{
	struct acpisbs_softc *sc = (struct acpisbs_softc *)self;

	switch (act) {
	case DVACT_WAKEUP:
		acpisbs_read(sc);
		acpisbs_refresh_sensors(sc);
		break;
	}

	return 0;
}

int
acpisbs_notify(struct aml_node *node, int notify_type, void *arg)
{
	struct acpisbs_softc *sc = arg;
	struct timeval diff, now;

	DPRINTF(("%s: %s: %d\n", sc->sc_dev.dv_xname, __func__, notify_type));

	getmicrouptime(&now);

	switch (notify_type) {
	case 0x00:
		/* fallback poll */
	case 0x80:
		/*
		 * EC SCI will come for every data point, so only run once in a
		 * while
		 */
		timersub(&now, &sc->sc_lastpoll, &diff);
		if (diff.tv_sec > ACPISBS_POLL_FREQ) {
			acpisbs_read(sc);
			acpisbs_refresh_sensors(sc);
			acpi_record_event(sc->sc_acpi, APM_POWER_CHANGE);
			getmicrouptime(&sc->sc_lastpoll);
		}
		break;
	default:
		break;
	}

	return 0;
}

int
acpi_smbus_read(struct acpisbs_softc *sc, uint8_t type, uint8_t cmd, int len,
    void *buf)
{
	int j;
	uint8_t addr = SMBATT_ADDRESS;
	uint8_t val;

	acpiec_write(sc->sc_ec, sc->sc_ec_base + SMBUS_ADDR, 1, &addr);
	acpiec_write(sc->sc_ec, sc->sc_ec_base + SMBUS_CMD, 1, &cmd);
	acpiec_write(sc->sc_ec, sc->sc_ec_base + SMBUS_PRTCL, 1, &type);

	for (j = SMBUS_TIMEOUT; j > 0; j--) {
		acpiec_read(sc->sc_ec, sc->sc_ec_base + SMBUS_PRTCL, 1, &val);
		if (val == 0)
			break;
	}
	if (j == 0) {
		printf("%s: %s: timeout reading 0x%x\n", sc->sc_dev.dv_xname,
		    __func__, addr);
		return 1;
	}

	acpiec_read(sc->sc_ec, sc->sc_ec_base + SMBUS_STS, 1, &val);
	if (val & SMBUS_STS_MASK) {
		printf("%s: %s: error reading status: 0x%x\n",
		    sc->sc_dev.dv_xname, __func__, addr);
		return 1;
	}

	switch (type) {
        case SMBUS_READ_WORD: {
		uint8_t word[2];
		acpiec_read(sc->sc_ec, sc->sc_ec_base + SMBUS_DATA, 2,
		    (uint8_t *)&word);

		*(uint16_t *)buf = (word[1] << 8) | word[0];

		break;
	}
	case SMBUS_READ_BLOCK:
		bzero(buf, len);

		/* find number of bytes to read */
		acpiec_read(sc->sc_ec, sc->sc_ec_base + SMBUS_BCNT, 1, &val);
		val &= 0x1f;
		if (len > val)
			len = val;

		for (j = 0; j < len; j++) {
			acpiec_read(sc->sc_ec, sc->sc_ec_base + SMBUS_DATA + j,
			    1, &val);
			((char *)buf)[j] = val;
		}
		break;
	default:
		printf("%s: %s: unknown mode 0x%x\n", sc->sc_dev.dv_xname,
		    __func__, type);
		return 1;
	}

	return 0;
}
