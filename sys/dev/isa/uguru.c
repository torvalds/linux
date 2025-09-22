/*	$OpenBSD: uguru.c,v 1.7 2022/04/08 15:02:28 naddy Exp $	*/

/*
 * Copyright (c) 2010 Mikko Tolmunen <oskari@sefirosu.org>
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

#include <machine/bus.h>

#include <dev/isa/isavar.h>

#ifdef UGURU_DEBUG
int	uguru_dbg = 0;
#define DPRINTF(lvl, fmt...)	\
	if (uguru_dbg >= lvl)	\
		printf(fmt);
#else
#define DPRINTF(lvl, fmt...)
#endif

#define UGURU_READ(iot, ioh, reg)	\
    bus_space_read_1((iot), (ioh), (reg))
#define UGURU_WRITE(iot, ioh, reg, val) \
    bus_space_write_1((iot), (ioh), (reg), (val))

#define UGURU_DATA		0x00	/* configuration data register */
#define UGURU_INDEX		0x04	/* configuration index register */
#define UGURU_IOSIZE		0x08

#define UGURU_DUMMY		0x00	/* dummy zero bit */
#define UGURU_ITM_DATA		0x21	/* temp/volt readings */
#define UGURU_ITM_CTRL		0x22	/* temp/volt settings */
#define UGURU_FAN_DATA		0x26	/* fan readings */
#define UGURU_FAN_CTRL		0x27	/* fan settings */
#define UGURU_PRODID		0x44	/* product ID */

#define UGURU_VENDID_ABIT	0x147b	/* ABIT */
#define UGURU_DEVID1		0x2003	/* AC2003 */
#define UGURU_DEVID2		0x2005	/* AC2005 */

#define ABIT_SYSID_KV01		0x0301
#define ABIT_SYSID_AI01		0x0302
#define ABIT_SYSID_AN01		0x0303
#define ABIT_SYSID_AA01		0x0304
#define ABIT_SYSID_AG01		0x0305
#define ABIT_SYSID_AV01		0x0306
#define ABIT_SYSID_KVP1		0x0307
#define ABIT_SYSID_AS01		0x0308
#define ABIT_SYSID_AX01		0x0309
#define ABIT_SYSID_M401		0x030a
#define ABIT_SYSID_AN02		0x030b
#define ABIT_SYSID_AU01		0x050c
#define ABIT_SYSID_AW01		0x050d
#define ABIT_SYSID_AL01		0x050e
#define ABIT_SYSID_BL01		0x050f
#define ABIT_SYSID_NI01		0x0510
#define ABIT_SYSID_AT01		0x0511
#define ABIT_SYSID_AN03		0x0512
#define ABIT_SYSID_AW02		0x0513
#define ABIT_SYSID_AB01		0x0514
#define ABIT_SYSID_AN04		0x0515
#define ABIT_SYSID_AW03		0x0516
#define ABIT_SYSID_AT02		0x0517
#define ABIT_SYSID_AB02		0x0518
#define ABIT_SYSID_IN01		0x0519
#define ABIT_SYSID_IP01		0x051a
#define ABIT_SYSID_IX01		0x051b
#define ABIT_SYSID_IX02		0x051c

#define UGURU_INTERVAL		5
#define UGURU_MAX_SENSORS	27

#define RFACT_NONE		13700
#define RFACT_NONE2		10000
#define RFACT(x, y)		(RFACT_NONE * ((x) + (y)) / (y))
#define RFACT2(x, y)		(RFACT_NONE2 * ((x) + (y)) / (y))

struct uguru_softc {
	struct device		 sc_dev;

	bus_space_tag_t		 sc_iot;
	bus_space_handle_t	 sc_ioh;

	struct ksensor		 sc_sensors[UGURU_MAX_SENSORS];
	struct ksensordev	 sc_sensordev;
	int			 sc_numsensors;
	const struct uguru_sensor *uguru_sensors;
	struct {
		uint8_t		 reading;
/*		uint8_t		 flags; */
		uint8_t		 lower;
		uint8_t		 upper;
	} cs;
	int			(*read)(struct uguru_softc *, int);
};

struct uguru_sensor {
	char			*desc;
	enum sensor_type	 type;
	void			(*refresh)(struct uguru_softc *, int);
	uint8_t			 reg;
	int			 rfact;
};

void	 uguru_refresh_temp(struct uguru_softc *, int);
void	 uguru_refresh_volt(struct uguru_softc *, int);
void	 uguru_refresh_fan(struct uguru_softc *, int);

#define UGURU_R_TEMP	uguru_refresh_temp
#define UGURU_R_VOLT	uguru_refresh_volt
#define UGURU_R_FAN	uguru_refresh_fan

const struct uguru_sensor abitkv_sensors[] = {
	{ "CPU", SENSOR_TEMP, UGURU_R_TEMP, 0x00 },
	{ "SYS", SENSOR_TEMP, UGURU_R_TEMP, 0x01 },
	{ "PWM", SENSOR_TEMP, UGURU_R_TEMP, 0x0f },

	{ "VCORE", SENSOR_VOLTS_DC, UGURU_R_VOLT, 0x03, RFACT_NONE },
	{ "+3.3V", SENSOR_VOLTS_DC, UGURU_R_VOLT, 0x05, RFACT(100, 402) },
	{ "+5V", SENSOR_VOLTS_DC, UGURU_R_VOLT, 0x06, RFACT(442, 560) },
	{ "+12V", SENSOR_VOLTS_DC, UGURU_R_VOLT, 0x08, RFACT(2800, 887) },
	{ "+5VSB", SENSOR_VOLTS_DC, UGURU_R_VOLT, 0x0b, RFACT(442, 560) },
	{ "DDR", SENSOR_VOLTS_DC, UGURU_R_VOLT, 0x04, RFACT_NONE },
	{ "DDRVTT", SENSOR_VOLTS_DC, UGURU_R_VOLT, 0x0a, RFACT_NONE },

	{ "CPU", SENSOR_FANRPM, UGURU_R_FAN, 0x00 },
	{ "NB", SENSOR_FANRPM, UGURU_R_FAN, 0x01 },
	{ "SYS", SENSOR_FANRPM, UGURU_R_FAN, 0x02 },
	{ "AUX1", SENSOR_FANRPM, UGURU_R_FAN, 0x03 },
	{ "AUX2", SENSOR_FANRPM, UGURU_R_FAN, 0x04 },

	{ NULL }
};

const struct uguru_sensor abitaa_sensors[] = {
	{ "CPU", SENSOR_TEMP, UGURU_R_TEMP, 0x00 },
	{ "SYS", SENSOR_TEMP, UGURU_R_TEMP, 0x01 },
	{ "PWM1", SENSOR_TEMP, UGURU_R_TEMP, 0x0f },
	{ "PWM2", SENSOR_TEMP, UGURU_R_TEMP, 0x0c },

	{ "VCORE", SENSOR_VOLTS_DC, UGURU_R_VOLT, 0x03, RFACT_NONE },
	{ "+3.3V", SENSOR_VOLTS_DC, UGURU_R_VOLT, 0x05, RFACT(100, 402) },
	{ "+5V", SENSOR_VOLTS_DC, UGURU_R_VOLT, 0x06, RFACT(442, 560) },
	{ "+12V", SENSOR_VOLTS_DC, UGURU_R_VOLT, 0x08, RFACT(2800, 888) },
	{ "+5VSB", SENSOR_VOLTS_DC, UGURU_R_VOLT, 0x0b, RFACT(442, 560) },
	{ "DDR", SENSOR_VOLTS_DC, UGURU_R_VOLT, 0x04, RFACT_NONE },
	{ "DDRVTT", SENSOR_VOLTS_DC, UGURU_R_VOLT, 0x0a, RFACT_NONE },
	{ "FSBVTT", SENSOR_VOLTS_DC, UGURU_R_VOLT, 0x0e, RFACT_NONE },
	{ "NB", SENSOR_VOLTS_DC, UGURU_R_VOLT, 0x02, RFACT_NONE },
	{ "NB +2.5V", SENSOR_VOLTS_DC, UGURU_R_VOLT, 0x09, RFACT_NONE },

	{ "CPU", SENSOR_FANRPM, UGURU_R_FAN, 0x00 },
	{ "NB", SENSOR_FANRPM, UGURU_R_FAN, 0x01 },
	{ "SYS", SENSOR_FANRPM, UGURU_R_FAN, 0x02 },
	{ "AUX1", SENSOR_FANRPM, UGURU_R_FAN, 0x03 },
	{ "AUX2", SENSOR_FANRPM, UGURU_R_FAN, 0x04 },

	{ NULL }
};

const struct uguru_sensor abitav_sensors[] = {
	{ "CPU", SENSOR_TEMP, UGURU_R_TEMP, 0x00 },
	{ "SYS", SENSOR_TEMP, UGURU_R_TEMP, 0x01 },
	{ "PWM", SENSOR_TEMP, UGURU_R_TEMP, 0x0f },

	{ "VCORE", SENSOR_VOLTS_DC, UGURU_R_VOLT, 0x03, RFACT_NONE },
	{ "+3.3V", SENSOR_VOLTS_DC, UGURU_R_VOLT, 0x05, RFACT(100, 402) },
	{ "+5V", SENSOR_VOLTS_DC, UGURU_R_VOLT, 0x06, RFACT(442, 560) },
	{ "+5VSB", SENSOR_VOLTS_DC, UGURU_R_VOLT, 0x0b, RFACT(442, 560) },
	{ "+3.3VSB", SENSOR_VOLTS_DC, UGURU_R_VOLT, 0x0d, RFACT(100, 402) },
	{ "DDR", SENSOR_VOLTS_DC, UGURU_R_VOLT, 0x04, RFACT_NONE },
	{ "DDRVTT", SENSOR_VOLTS_DC, UGURU_R_VOLT, 0x0a, RFACT_NONE },
	{ "NB", SENSOR_VOLTS_DC, UGURU_R_VOLT, 0x08, RFACT_NONE },
	{ "SB", SENSOR_VOLTS_DC, UGURU_R_VOLT, 0x0e, RFACT_NONE },
	{ "HTV", SENSOR_VOLTS_DC, UGURU_R_VOLT, 0x02, RFACT_NONE },
	{ "AGP", SENSOR_VOLTS_DC, UGURU_R_VOLT, 0x09, RFACT_NONE },

	{ "CPU", SENSOR_FANRPM, UGURU_R_FAN, 0x00 },
	{ "NB", SENSOR_FANRPM, UGURU_R_FAN, 0x01 },
	{ "SYS", SENSOR_FANRPM, UGURU_R_FAN, 0x02 },
	{ "AUX1", SENSOR_FANRPM, UGURU_R_FAN, 0x03 },
	{ "AUX2", SENSOR_FANRPM, UGURU_R_FAN, 0x04 },

	{ NULL }
};

const struct uguru_sensor abitas_sensors[] = {
	{ "CPU", SENSOR_TEMP, UGURU_R_TEMP, 0x00 },
	{ "SYS", SENSOR_TEMP, UGURU_R_TEMP, 0x01 },
	{ "PWM", SENSOR_TEMP, UGURU_R_TEMP, 0x0f },

	{ "VCORE", SENSOR_VOLTS_DC, UGURU_R_VOLT, 0x03, RFACT_NONE },
	{ "+3.3V", SENSOR_VOLTS_DC, UGURU_R_VOLT, 0x05, RFACT(100, 402) },
	{ "+5V", SENSOR_VOLTS_DC, UGURU_R_VOLT, 0x06, RFACT(442, 560) },
	{ "+12V", SENSOR_VOLTS_DC, UGURU_R_VOLT, 0x08, RFACT(2800, 884) },
	{ "+5VSB", SENSOR_VOLTS_DC, UGURU_R_VOLT, 0x0b, RFACT(442, 560) },
	{ "+3.3VSB", SENSOR_VOLTS_DC, UGURU_R_VOLT, 0x0d, RFACT(100, 402) },
	{ "DDR", SENSOR_VOLTS_DC, UGURU_R_VOLT, 0x04, RFACT_NONE },
	{ "DDRVTT", SENSOR_VOLTS_DC, UGURU_R_VOLT, 0x0a, RFACT_NONE },
	{ "FSBVTT", SENSOR_VOLTS_DC, UGURU_R_VOLT, 0x0e, RFACT_NONE },
	{ "NB/AGP", SENSOR_VOLTS_DC, UGURU_R_VOLT, 0x02, RFACT_NONE },
	{ "GMCH", SENSOR_VOLTS_DC, UGURU_R_VOLT, 0x09, RFACT_NONE },

	{ "CPU", SENSOR_FANRPM, UGURU_R_FAN, 0x00 },
	{ "NB", SENSOR_FANRPM, UGURU_R_FAN, 0x01 },
	{ "SYS", SENSOR_FANRPM, UGURU_R_FAN, 0x02 },
	{ "AUX1", SENSOR_FANRPM, UGURU_R_FAN, 0x03 },
	{ "AUX2", SENSOR_FANRPM, UGURU_R_FAN, 0x04 },

	{ NULL }
};

const struct uguru_sensor abitax_sensors[] = {
	{ "CPU", SENSOR_TEMP, UGURU_R_TEMP, 0x00 },
	{ "SYS", SENSOR_TEMP, UGURU_R_TEMP, 0x01 },
	{ "PWM", SENSOR_TEMP, UGURU_R_TEMP, 0x0f },

	{ "VCORE", SENSOR_VOLTS_DC, UGURU_R_VOLT, 0x03, RFACT_NONE },
	{ "+3.3V", SENSOR_VOLTS_DC, UGURU_R_VOLT, 0x05, RFACT(100, 402) },
	{ "+5V", SENSOR_VOLTS_DC, UGURU_R_VOLT, 0x06, RFACT(442, 560) },
	{ "+12V", SENSOR_VOLTS_DC, UGURU_R_VOLT, 0x0d, RFACT(2800, 888) },
	{ "+5VSB", SENSOR_VOLTS_DC, UGURU_R_VOLT, 0x0b, RFACT(442, 560) },
	{ "DDR", SENSOR_VOLTS_DC, UGURU_R_VOLT, 0x04, RFACT_NONE },
	{ "DDRVTT", SENSOR_VOLTS_DC, UGURU_R_VOLT, 0x0a, RFACT_NONE },
	{ "NB", SENSOR_VOLTS_DC, UGURU_R_VOLT, 0x08, RFACT_NONE },
	{ "SB", SENSOR_VOLTS_DC, UGURU_R_VOLT, 0x0e, RFACT_NONE },
	{ "HTV", SENSOR_VOLTS_DC, UGURU_R_VOLT, 0x02, RFACT_NONE },

	{ "CPU", SENSOR_FANRPM, UGURU_R_FAN, 0x00 },
	{ "NB", SENSOR_FANRPM, UGURU_R_FAN, 0x01 },
	{ "SYS", SENSOR_FANRPM, UGURU_R_FAN, 0x02 },
	{ "AUX", SENSOR_FANRPM, UGURU_R_FAN, 0x03 },

	{ NULL }
};

const struct uguru_sensor abitm4_sensors[] = {
	{ "CPU", SENSOR_TEMP, UGURU_R_TEMP, 0x00 },
	{ "SYS", SENSOR_TEMP, UGURU_R_TEMP, 0x01 },
	{ "PWM1", SENSOR_TEMP, UGURU_R_TEMP, 0x02 },
	{ "PWM2", SENSOR_TEMP, UGURU_R_TEMP, 0x03 },
	{ "PWM3", SENSOR_TEMP, UGURU_R_TEMP, 0x04 },
	{ "PWM4", SENSOR_TEMP, UGURU_R_TEMP, 0x05 },

	{ "VCORE", SENSOR_VOLTS_DC, UGURU_R_VOLT, 0x08, RFACT_NONE },
	{ "+3.3V", SENSOR_VOLTS_DC, UGURU_R_VOLT, 0x13, RFACT(100, 402) },
	{ "+5V", SENSOR_VOLTS_DC, UGURU_R_VOLT, 0x12, RFACT(442, 560) },
	{ "+12V", SENSOR_VOLTS_DC, UGURU_R_VOLT, 0x11, RFACT(2800, 884) },
	{ "+5VSB", SENSOR_VOLTS_DC, UGURU_R_VOLT, 0x10, RFACT(442, 560) },
	{ "DDR", SENSOR_VOLTS_DC, UGURU_R_VOLT, 0x09, RFACT_NONE },
	{ "DDRVTT", SENSOR_VOLTS_DC, UGURU_R_VOLT, 0x0a, RFACT_NONE },
	{ "FSBVTT", SENSOR_VOLTS_DC, UGURU_R_VOLT, 0x0d, RFACT_NONE },
	{ "NB", SENSOR_VOLTS_DC, UGURU_R_VOLT, 0x0b, RFACT_NONE },
	{ "NB +2.5V", SENSOR_VOLTS_DC, UGURU_R_VOLT, 0x0c, RFACT_NONE },

	{ "CPU", SENSOR_FANRPM, UGURU_R_FAN, 0x00 },
	{ "NB", SENSOR_FANRPM, UGURU_R_FAN, 0x01 },
	{ "SYS", SENSOR_FANRPM, UGURU_R_FAN, 0x02 },
	{ "OTES1", SENSOR_FANRPM, UGURU_R_FAN, 0x03 },
	{ "OTES2", SENSOR_FANRPM, UGURU_R_FAN, 0x04 },

	{ NULL }
};

const struct uguru_sensor abitan_sensors[] = {
	{ "CPU", SENSOR_TEMP, UGURU_R_TEMP, 0x00 },
	{ "SYS", SENSOR_TEMP, UGURU_R_TEMP, 0x01 },
	{ "PWM", SENSOR_TEMP, UGURU_R_TEMP, 0x0f },

	{ "VCORE", SENSOR_VOLTS_DC, UGURU_R_VOLT, 0x03, RFACT_NONE },
	{ "+3.3V", SENSOR_VOLTS_DC, UGURU_R_VOLT, 0x05, RFACT(100, 402) },
	{ "+5V", SENSOR_VOLTS_DC, UGURU_R_VOLT, 0x06, RFACT(442, 560) },
	{ "+12V", SENSOR_VOLTS_DC, UGURU_R_VOLT, 0x0d, RFACT(2800, 844) },
	{ "+5VSB", SENSOR_VOLTS_DC, UGURU_R_VOLT, 0x0b, RFACT(442, 560) },
	{ "DDR", SENSOR_VOLTS_DC, UGURU_R_VOLT, 0x04, RFACT_NONE },
	{ "DDRVTT", SENSOR_VOLTS_DC, UGURU_R_VOLT, 0x0a, RFACT_NONE },
	{ "CPUVDDA", SENSOR_VOLTS_DC, UGURU_R_VOLT, 0x0e, RFACT_NONE },
	{ "HTV", SENSOR_VOLTS_DC, UGURU_R_VOLT, 0x02, RFACT_NONE },
	{ "MCP", SENSOR_VOLTS_DC, UGURU_R_VOLT, 0x09, RFACT_NONE },
	{ "MCP SB", SENSOR_VOLTS_DC, UGURU_R_VOLT, 0x08, RFACT_NONE },

	{ "CPU", SENSOR_FANRPM, UGURU_R_FAN, 0x00 },
	{ "NB", SENSOR_FANRPM, UGURU_R_FAN, 0x01 },
	{ "SYS", SENSOR_FANRPM, UGURU_R_FAN, 0x02 },
	{ "AUX", SENSOR_FANRPM, UGURU_R_FAN, 0x05 },
	{ "OTES1", SENSOR_FANRPM, UGURU_R_FAN, 0x04 },
	{ "OTES2", SENSOR_FANRPM, UGURU_R_FAN, 0x03 },

	{ NULL }
};

const struct uguru_sensor abital_sensors[] = {
	{ "CPU", SENSOR_TEMP, UGURU_R_TEMP, 0x18 },
	{ "SYS", SENSOR_TEMP, UGURU_R_TEMP, 0x19 },
	{ "PWM", SENSOR_TEMP, UGURU_R_TEMP, 0x1a },

	{ "VCORE", SENSOR_VOLTS_DC, UGURU_R_VOLT, 0x00, RFACT_NONE2 },
	{ "+3.3V", SENSOR_VOLTS_DC, UGURU_R_VOLT, 0x0a, RFACT2(34, 34) },
	{ "+5V", SENSOR_VOLTS_DC, UGURU_R_VOLT, 0x09, RFACT2(120, 60) },
	{ "+12V", SENSOR_VOLTS_DC, UGURU_R_VOLT, 0x07, RFACT2(50, 10) },
	{ "+12V", SENSOR_VOLTS_DC, UGURU_R_VOLT, 0x08, RFACT2(50, 10) },
	{ "+5VSB", SENSOR_VOLTS_DC, UGURU_R_VOLT, 0x0b, RFACT2(120, 60) },
	{ "DDR", SENSOR_VOLTS_DC, UGURU_R_VOLT, 0x01, RFACT_NONE2 },
	{ "DDRVTT", SENSOR_VOLTS_DC, UGURU_R_VOLT, 0x02, RFACT_NONE2 },
	{ "CPUVTT", SENSOR_VOLTS_DC, UGURU_R_VOLT, 0x03, RFACT_NONE2 },
	{ "MCH/PCIE", SENSOR_VOLTS_DC, UGURU_R_VOLT, 0x04, RFACT_NONE2 },
	{ "MCH", SENSOR_VOLTS_DC, UGURU_R_VOLT, 0x05, RFACT2(34, 34) },
	{ "ICH", SENSOR_VOLTS_DC, UGURU_R_VOLT, 0x06, RFACT_NONE2 },

	{ "CPU", SENSOR_FANRPM, UGURU_R_FAN, 0x20 },
	{ "NB", SENSOR_FANRPM, UGURU_R_FAN, 0x21 },
	{ "SYS", SENSOR_FANRPM, UGURU_R_FAN, 0x22 },
	{ "AUX", SENSOR_FANRPM, UGURU_R_FAN, 0x23 },

	{ NULL }
};

const struct uguru_sensor abitaw_sensors[] = {
	{ "CPU", SENSOR_TEMP, UGURU_R_TEMP, 0x18 },
	{ "SYS", SENSOR_TEMP, UGURU_R_TEMP, 0x19 },
	{ "PWM1", SENSOR_TEMP, UGURU_R_TEMP, 0x1a },
	{ "PWM2", SENSOR_TEMP, UGURU_R_TEMP, 0x1b },
	{ "PWM3", SENSOR_TEMP, UGURU_R_TEMP, 0x1c },
	{ "PWM4", SENSOR_TEMP, UGURU_R_TEMP, 0x1d },

	{ "VCORE", SENSOR_VOLTS_DC, UGURU_R_VOLT, 0x00, RFACT_NONE2 },
	{ "+3.3V", SENSOR_VOLTS_DC, UGURU_R_VOLT, 0x0a, RFACT2(34, 34) },
	{ "+5V", SENSOR_VOLTS_DC, UGURU_R_VOLT, 0x09, RFACT2(120, 60) },
	{ "+12V", SENSOR_VOLTS_DC, UGURU_R_VOLT, 0x07, RFACT2(50, 10) },
	{ "+12V", SENSOR_VOLTS_DC, UGURU_R_VOLT, 0x08, RFACT2(50, 10) },
	{ "+5VSB", SENSOR_VOLTS_DC, UGURU_R_VOLT, 0x0b, RFACT2(120, 60) },
	{ "DDR", SENSOR_VOLTS_DC, UGURU_R_VOLT, 0x01, RFACT_NONE2 },
	{ "DDRVTT", SENSOR_VOLTS_DC, UGURU_R_VOLT, 0x02, RFACT_NONE2 },
	{ "CPUVTT", SENSOR_VOLTS_DC, UGURU_R_VOLT, 0x03, RFACT_NONE2 },
	{ "MCH/PCIE", SENSOR_VOLTS_DC, UGURU_R_VOLT, 0x04, RFACT_NONE2 },
	{ "MCH", SENSOR_VOLTS_DC, UGURU_R_VOLT, 0x05, RFACT2(34, 34) },
	{ "ICH", SENSOR_VOLTS_DC, UGURU_R_VOLT, 0x06, RFACT_NONE2 },

	{ "CPU", SENSOR_FANRPM, UGURU_R_FAN, 0x20 },
	{ "NB", SENSOR_FANRPM, UGURU_R_FAN, 0x21 },
	{ "SYS", SENSOR_FANRPM, UGURU_R_FAN, 0x22 },
	{ "AUX1", SENSOR_FANRPM, UGURU_R_FAN, 0x23 },
	{ "AUX2", SENSOR_FANRPM, UGURU_R_FAN, 0x24 },
	{ "AUX3", SENSOR_FANRPM, UGURU_R_FAN, 0x25 },
	{ "AUX4", SENSOR_FANRPM, UGURU_R_FAN, 0x26 },
	{ "AUX5", SENSOR_FANRPM, UGURU_R_FAN, 0x27 },

	{ NULL }
};

const struct uguru_sensor abitni_sensors[] = {
	{ "CPU", SENSOR_TEMP, UGURU_R_TEMP, 0x18 },
	{ "SYS", SENSOR_TEMP, UGURU_R_TEMP, 0x19 },
	{ "PWM", SENSOR_TEMP, UGURU_R_TEMP, 0x1a },

	{ "VCORE", SENSOR_VOLTS_DC, UGURU_R_VOLT, 0x00, RFACT_NONE2 },
	{ "+3.3V", SENSOR_VOLTS_DC, UGURU_R_VOLT, 0x0a, RFACT2(34, 34) },
	{ "+5V", SENSOR_VOLTS_DC, UGURU_R_VOLT, 0x09, RFACT2(120, 60) },
	{ "+12V", SENSOR_VOLTS_DC, UGURU_R_VOLT, 0x07, RFACT2(50, 10) },
	{ "+12V", SENSOR_VOLTS_DC, UGURU_R_VOLT, 0x08, RFACT2(50, 10) },
	{ "+5VSB", SENSOR_VOLTS_DC, UGURU_R_VOLT, 0x0b, RFACT2(120, 60) },
	{ "DDR", SENSOR_VOLTS_DC, UGURU_R_VOLT, 0x01, RFACT_NONE2 },
	{ "DDRVTT", SENSOR_VOLTS_DC, UGURU_R_VOLT, 0x02, RFACT_NONE2 },
	{ "CPUVTT", SENSOR_VOLTS_DC, UGURU_R_VOLT, 0x03, RFACT_NONE2 },
	{ "NB", SENSOR_VOLTS_DC, UGURU_R_VOLT, 0x04, RFACT_NONE2 },
	{ "SB", SENSOR_VOLTS_DC, UGURU_R_VOLT, 0x06, RFACT_NONE2 },

	{ "CPU", SENSOR_FANRPM, UGURU_R_FAN, 0x20 },
	{ "NB", SENSOR_FANRPM, UGURU_R_FAN, 0x21 },
	{ "SYS", SENSOR_FANRPM, UGURU_R_FAN, 0x22 },
	{ "AUX", SENSOR_FANRPM, UGURU_R_FAN, 0x23 },
	{ "OTES1", SENSOR_FANRPM, UGURU_R_FAN, 0x24 },
	{ "OTES2", SENSOR_FANRPM, UGURU_R_FAN, 0x25 },

	{ NULL }
};

const struct uguru_sensor abitat_sensors[] = {
	{ "CPU", SENSOR_TEMP, UGURU_R_TEMP, 0x18 },
	{ "NB", SENSOR_TEMP, UGURU_R_TEMP, 0x19 },
	{ "SYS", SENSOR_TEMP, UGURU_R_TEMP, 0x1a },
	{ "PWM", SENSOR_TEMP, UGURU_R_TEMP, 0x1b },

	{ "VCORE", SENSOR_VOLTS_DC, UGURU_R_VOLT, 0x00, RFACT_NONE2 },
	{ "+3.3V", SENSOR_VOLTS_DC, UGURU_R_VOLT, 0x0a, RFACT2(34, 34) },
	{ "+5V", SENSOR_VOLTS_DC, UGURU_R_VOLT, 0x09, RFACT2(120, 60) },
	{ "+12V", SENSOR_VOLTS_DC, UGURU_R_VOLT, 0x07, RFACT2(50, 10) },
	{ "+12V", SENSOR_VOLTS_DC, UGURU_R_VOLT, 0x08, RFACT2(50, 10) },
	{ "+5VSB", SENSOR_VOLTS_DC, UGURU_R_VOLT, 0x0b, RFACT2(120, 60) },
	{ "DDR", SENSOR_VOLTS_DC, UGURU_R_VOLT, 0x01, RFACT2(34, 34) },
	{ "DDRVTT", SENSOR_VOLTS_DC, UGURU_R_VOLT, 0x02, RFACT_NONE2 },
	{ "CPUVDDA", SENSOR_VOLTS_DC, UGURU_R_VOLT, 0x06, RFACT2(34, 34) },
	{ "PCIE", SENSOR_VOLTS_DC, UGURU_R_VOLT, 0x0c, RFACT_NONE2 },
	{ "HTV", SENSOR_VOLTS_DC, UGURU_R_VOLT, 0x03, RFACT_NONE2 },
	{ "NB", SENSOR_VOLTS_DC, UGURU_R_VOLT, 0x0d, RFACT_NONE2 },
	{ "NB +1.8V", SENSOR_VOLTS_DC, UGURU_R_VOLT, 0x04, RFACT_NONE2 },
	{ "NB +1.8V", SENSOR_VOLTS_DC, UGURU_R_VOLT, 0x05, RFACT_NONE2 },

	{ "CPU", SENSOR_FANRPM, UGURU_R_FAN, 0x20 },
	{ "NB", SENSOR_FANRPM, UGURU_R_FAN, 0x21 },
	{ "SYS", SENSOR_FANRPM, UGURU_R_FAN, 0x22 },
	{ "AUX1", SENSOR_FANRPM, UGURU_R_FAN, 0x23 },
	{ "AUX2", SENSOR_FANRPM, UGURU_R_FAN, 0x24 },
	{ "AUX3", SENSOR_FANRPM, UGURU_R_FAN, 0x25 },

	{ NULL }
};

const struct uguru_sensor abitan2_sensors[] = {
	{ "CPU", SENSOR_TEMP, UGURU_R_TEMP, 0x18 },
	{ "SYS", SENSOR_TEMP, UGURU_R_TEMP, 0x19 },
	{ "PWM", SENSOR_TEMP, UGURU_R_TEMP, 0x1a },

	{ "VCORE", SENSOR_VOLTS_DC, UGURU_R_VOLT, 0x00, RFACT_NONE2 },
	{ "+3.3V", SENSOR_VOLTS_DC, UGURU_R_VOLT, 0x0a, RFACT2(34, 34) },
	{ "+5V", SENSOR_VOLTS_DC, UGURU_R_VOLT, 0x09, RFACT2(120, 60) },
	{ "+12V", SENSOR_VOLTS_DC, UGURU_R_VOLT, 0x07, RFACT2(50, 10) },
	{ "+12V", SENSOR_VOLTS_DC, UGURU_R_VOLT, 0x08, RFACT2(50, 10) },
	{ "+5VSB", SENSOR_VOLTS_DC, UGURU_R_VOLT, 0x0b, RFACT2(120, 60) },
	{ "DDR", SENSOR_VOLTS_DC, UGURU_R_VOLT, 0x01, RFACT2(34, 34) },
	{ "DDRVTT", SENSOR_VOLTS_DC, UGURU_R_VOLT, 0x02, RFACT_NONE2 },
	{ "CPUVDDA", SENSOR_VOLTS_DC, UGURU_R_VOLT, 0x05, RFACT2(34, 34) },
	{ "HTV", SENSOR_VOLTS_DC, UGURU_R_VOLT, 0x03, RFACT_NONE2 },
	{ "NB", SENSOR_VOLTS_DC, UGURU_R_VOLT, 0x04, RFACT_NONE2 },
	{ "SB", SENSOR_VOLTS_DC, UGURU_R_VOLT, 0x06, RFACT_NONE2 },

	{ "CPU", SENSOR_FANRPM, UGURU_R_FAN, 0x20 },
	{ "NB", SENSOR_FANRPM, UGURU_R_FAN, 0x21 },
	{ "SYS", SENSOR_FANRPM, UGURU_R_FAN, 0x22 },
	{ "AUX1", SENSOR_FANRPM, UGURU_R_FAN, 0x24 },
	{ "AUX2", SENSOR_FANRPM, UGURU_R_FAN, 0x25 },

	{ NULL }
};

const struct uguru_sensor abitab_sensors[] = {
	{ "CPU", SENSOR_TEMP, UGURU_R_TEMP, 0x18 },
	{ "SYS", SENSOR_TEMP, UGURU_R_TEMP, 0x19 },
	{ "PWM", SENSOR_TEMP, UGURU_R_TEMP, 0x1a },

	{ "VCORE", SENSOR_VOLTS_DC, UGURU_R_VOLT, 0x00, RFACT_NONE2 },
	{ "+3.3V", SENSOR_VOLTS_DC, UGURU_R_VOLT, 0x0a, RFACT2(34, 34) },
	{ "+5V", SENSOR_VOLTS_DC, UGURU_R_VOLT, 0x09, RFACT2(120, 60) },
	{ "+12V", SENSOR_VOLTS_DC, UGURU_R_VOLT, 0x07, RFACT2(50, 10) },
	{ "+12V", SENSOR_VOLTS_DC, UGURU_R_VOLT, 0x08, RFACT2(50, 10) },
	{ "+5VSB", SENSOR_VOLTS_DC, UGURU_R_VOLT, 0x0b, RFACT2(120, 60) },
	{ "DDR", SENSOR_VOLTS_DC, UGURU_R_VOLT, 0x01, RFACT_NONE2 },
	{ "DDRVTT", SENSOR_VOLTS_DC, UGURU_R_VOLT, 0x02, RFACT_NONE2 },
	{ "CPUVTT", SENSOR_VOLTS_DC, UGURU_R_VOLT, 0x03, RFACT_NONE2 },
	{ "ICHIO", SENSOR_VOLTS_DC, UGURU_R_VOLT, 0x05, RFACT_NONE2 },
	{ "ICH", SENSOR_VOLTS_DC, UGURU_R_VOLT, 0x06, RFACT_NONE2 },
	{ "MCH", SENSOR_VOLTS_DC, UGURU_R_VOLT, 0x04, RFACT_NONE2 },

	{ "CPU", SENSOR_FANRPM, UGURU_R_FAN, 0x20 },
	{ "NB", SENSOR_FANRPM, UGURU_R_FAN, 0x21 },
	{ "SYS", SENSOR_FANRPM, UGURU_R_FAN, 0x22 },
	{ "AUX1", SENSOR_FANRPM, UGURU_R_FAN, 0x23 },
	{ "AUX2", SENSOR_FANRPM, UGURU_R_FAN, 0x24 },
	{ "AUX3", SENSOR_FANRPM, UGURU_R_FAN, 0x25 },

	{ NULL }
};

const struct uguru_sensor abitan3_sensors[] = {
	{ "CPU", SENSOR_TEMP, UGURU_R_TEMP, 0x18 },
	{ "SYS", SENSOR_TEMP, UGURU_R_TEMP, 0x19 },
	{ "PWM", SENSOR_TEMP, UGURU_R_TEMP, 0x1a },

	{ "VCORE", SENSOR_VOLTS_DC, UGURU_R_VOLT, 0x00, RFACT_NONE2 },
	{ "+3.3V", SENSOR_VOLTS_DC, UGURU_R_VOLT, 0x0a, RFACT2(34, 34) },
	{ "+5V", SENSOR_VOLTS_DC, UGURU_R_VOLT, 0x09, RFACT2(120, 60) },
	{ "+12V", SENSOR_VOLTS_DC, UGURU_R_VOLT, 0x07, RFACT2(50, 10) },
	{ "+12V", SENSOR_VOLTS_DC, UGURU_R_VOLT, 0x08, RFACT2(50, 10) },
	{ "+5VSB", SENSOR_VOLTS_DC, UGURU_R_VOLT, 0x0b, RFACT2(120, 60) },
	{ "DDR", SENSOR_VOLTS_DC, UGURU_R_VOLT, 0x01, RFACT2(34, 34) },
	{ "DDRVTT", SENSOR_VOLTS_DC, UGURU_R_VOLT, 0x02, RFACT_NONE2 },
	{ "CPUVDDA", SENSOR_VOLTS_DC, UGURU_R_VOLT, 0x05, RFACT2(34, 34) },
	{ "HTV", SENSOR_VOLTS_DC, UGURU_R_VOLT, 0x03, RFACT_NONE2 },
	{ "NB", SENSOR_VOLTS_DC, UGURU_R_VOLT, 0x04, RFACT_NONE2 },
	{ "NB/PCIE", SENSOR_VOLTS_DC, UGURU_R_VOLT, 0x0e, RFACT_NONE2 },
	{ "SB", SENSOR_VOLTS_DC, UGURU_R_VOLT, 0x06, RFACT_NONE2 },

	{ "CPU", SENSOR_FANRPM, UGURU_R_FAN, 0x20 },
	{ "SYS", SENSOR_FANRPM, UGURU_R_FAN, 0x22 },
	{ "AUX1", SENSOR_FANRPM, UGURU_R_FAN, 0x21 },
	{ "AUX2", SENSOR_FANRPM, UGURU_R_FAN, 0x23 },
	{ "AUX3", SENSOR_FANRPM, UGURU_R_FAN, 0x24 },
	{ "AUX4", SENSOR_FANRPM, UGURU_R_FAN, 0x25 },

	{ NULL }
};

const struct uguru_sensor abitaw2_sensors[] = {
	{ "CPU", SENSOR_TEMP, UGURU_R_TEMP, 0x18 },
	{ "SYS", SENSOR_TEMP, UGURU_R_TEMP, 0x19 },
	{ "PWM1", SENSOR_TEMP, UGURU_R_TEMP, 0x1a },
	{ "PWM2", SENSOR_TEMP, UGURU_R_TEMP, 0x1b },
	{ "PWM3", SENSOR_TEMP, UGURU_R_TEMP, 0x1c },
	{ "PWM4", SENSOR_TEMP, UGURU_R_TEMP, 0x1d },

	{ "VCORE", SENSOR_VOLTS_DC, UGURU_R_VOLT, 0x00, RFACT_NONE2 },
	{ "+3.3V", SENSOR_VOLTS_DC, UGURU_R_VOLT, 0x0a, RFACT2(34, 34) },
	{ "+5V", SENSOR_VOLTS_DC, UGURU_R_VOLT, 0x09, RFACT2(120, 60) },
	{ "+12V", SENSOR_VOLTS_DC, UGURU_R_VOLT, 0x07, RFACT2(50, 10) },
	{ "+12V", SENSOR_VOLTS_DC, UGURU_R_VOLT, 0x08, RFACT2(50, 10) },
	{ "+5VSB", SENSOR_VOLTS_DC, UGURU_R_VOLT, 0x0b, RFACT2(120, 60) },
	{ "DDR", SENSOR_VOLTS_DC, UGURU_R_VOLT, 0x01, RFACT2(34, 34) },
	{ "DDRVTT", SENSOR_VOLTS_DC, UGURU_R_VOLT, 0x02, RFACT_NONE2 },
	{ "CPUVTT", SENSOR_VOLTS_DC, UGURU_R_VOLT, 0x03, RFACT_NONE2 },
	{ "MCH/PCIE", SENSOR_VOLTS_DC, UGURU_R_VOLT, 0x04, RFACT_NONE2 },
	{ "MCH", SENSOR_VOLTS_DC, UGURU_R_VOLT, 0x05, RFACT2(34, 34) },
	{ "ICH", SENSOR_VOLTS_DC, UGURU_R_VOLT, 0x06, RFACT_NONE2 },

	{ "CPU", SENSOR_FANRPM, UGURU_R_FAN, 0x20 },
	{ "NB", SENSOR_FANRPM, UGURU_R_FAN, 0x21 },
	{ "SYS", SENSOR_FANRPM, UGURU_R_FAN, 0x22 },
	{ "AUX1", SENSOR_FANRPM, UGURU_R_FAN, 0x23 },
	{ "AUX2", SENSOR_FANRPM, UGURU_R_FAN, 0x24 },
	{ "AUX3", SENSOR_FANRPM, UGURU_R_FAN, 0x25 },
	{ "OTES1", SENSOR_FANRPM, UGURU_R_FAN, 0x26 },
	{ "OTES2", SENSOR_FANRPM, UGURU_R_FAN, 0x27 },

	{ NULL }
};

const struct uguru_sensor abitat2_sensors[] = {
	{ "CPU", SENSOR_TEMP, UGURU_R_TEMP, 0x18 },
	{ "SYS", SENSOR_TEMP, UGURU_R_TEMP, 0x1a },
	{ "PWM", SENSOR_TEMP, UGURU_R_TEMP, 0x1b },

	{ "VCORE", SENSOR_VOLTS_DC, UGURU_R_VOLT, 0x00, RFACT_NONE2 },
	{ "+3.3V", SENSOR_VOLTS_DC, UGURU_R_VOLT, 0x0a, RFACT2(34, 34) },
	{ "+5V", SENSOR_VOLTS_DC, UGURU_R_VOLT, 0x09, RFACT2(120, 60) },
	{ "+12V", SENSOR_VOLTS_DC, UGURU_R_VOLT, 0x07, RFACT2(50, 10) },
	{ "+12V", SENSOR_VOLTS_DC, UGURU_R_VOLT, 0x08, RFACT2(50, 10) },
	{ "+5VSB", SENSOR_VOLTS_DC, UGURU_R_VOLT, 0x0b, RFACT2(120, 60) },
	{ "DDR", SENSOR_VOLTS_DC, UGURU_R_VOLT, 0x01, RFACT2(34, 34) },
	{ "DDRVTT", SENSOR_VOLTS_DC, UGURU_R_VOLT, 0x02, RFACT_NONE2 },
	{ "CPUVDDA", SENSOR_VOLTS_DC, UGURU_R_VOLT, 0x06, RFACT2(34, 34) },
	{ "PCIE", SENSOR_VOLTS_DC, UGURU_R_VOLT, 0x0c, RFACT_NONE2 },
	{ "HTV", SENSOR_VOLTS_DC, UGURU_R_VOLT, 0x03, RFACT_NONE2 },
	{ "NB", SENSOR_VOLTS_DC, UGURU_R_VOLT, 0x0d, RFACT_NONE2 },
	{ "NB +1.8V", SENSOR_VOLTS_DC, UGURU_R_VOLT, 0x04, RFACT_NONE2 },
	{ "SB", SENSOR_VOLTS_DC, UGURU_R_VOLT, 0x05, RFACT_NONE2 },

	{ "CPU", SENSOR_FANRPM, UGURU_R_FAN, 0x20 },
	{ "SYS", SENSOR_FANRPM, UGURU_R_FAN, 0x22 },
	{ "AUX1", SENSOR_FANRPM, UGURU_R_FAN, 0x23 },
	{ "AUX2", SENSOR_FANRPM, UGURU_R_FAN, 0x24 },
	{ "AUX3", SENSOR_FANRPM, UGURU_R_FAN, 0x25 },
	{ "AUX4", SENSOR_FANRPM, UGURU_R_FAN, 0x21 },

	{ NULL }
};

const struct uguru_sensor abitab2_sensors[] = {
	{ "CPU", SENSOR_TEMP, UGURU_R_TEMP, 0x18 },
	{ "SYS", SENSOR_TEMP, UGURU_R_TEMP, 0x19 },
	{ "PWM1", SENSOR_TEMP, UGURU_R_TEMP, 0x1a },
	{ "PWM2", SENSOR_TEMP, UGURU_R_TEMP, 0x1b },
	{ "PWM3", SENSOR_TEMP, UGURU_R_TEMP, 0x1c },
	{ "PWM4", SENSOR_TEMP, UGURU_R_TEMP, 0x1d },
	{ "PWM5", SENSOR_TEMP, UGURU_R_TEMP, 0x1e },

	{ "VCORE", SENSOR_VOLTS_DC, UGURU_R_VOLT, 0x00, RFACT_NONE2 },
	{ "+3.3V", SENSOR_VOLTS_DC, UGURU_R_VOLT, 0x0a, RFACT2(34, 34) },
	{ "+5V", SENSOR_VOLTS_DC, UGURU_R_VOLT, 0x09, RFACT2(120, 60) },
	{ "+12V", SENSOR_VOLTS_DC, UGURU_R_VOLT, 0x07, RFACT2(50, 10) },
	{ "+12V", SENSOR_VOLTS_DC, UGURU_R_VOLT, 0x08, RFACT2(50, 10) },
	{ "+5VSB", SENSOR_VOLTS_DC, UGURU_R_VOLT, 0x0b, RFACT2(120, 60) },
	{ "DDR", SENSOR_VOLTS_DC, UGURU_R_VOLT, 0x01, RFACT2(34, 34) },
	{ "DDRVTT", SENSOR_VOLTS_DC, UGURU_R_VOLT, 0x02, RFACT_NONE2 },
	{ "CPUVTT", SENSOR_VOLTS_DC, UGURU_R_VOLT, 0x03, RFACT_NONE2 },
	{ "ICHIO", SENSOR_VOLTS_DC, UGURU_R_VOLT, 0x05, RFACT_NONE2 },
	{ "ICH", SENSOR_VOLTS_DC, UGURU_R_VOLT, 0x06, RFACT_NONE2 },
	{ "MCH", SENSOR_VOLTS_DC, UGURU_R_VOLT, 0x04, RFACT_NONE2 },

	{ "CPU", SENSOR_FANRPM, UGURU_R_FAN, 0x20 },
	{ "SYS", SENSOR_FANRPM, UGURU_R_FAN, 0x22 },
	{ "AUX1", SENSOR_FANRPM, UGURU_R_FAN, 0x21 },
	{ "AUX2", SENSOR_FANRPM, UGURU_R_FAN, 0x23 },
	{ "AUX3", SENSOR_FANRPM, UGURU_R_FAN, 0x24 },
	{ "AUX4", SENSOR_FANRPM, UGURU_R_FAN, 0x25 },

	{ NULL }
};

const struct uguru_sensor abitin_sensors[] = {
	{ "CPU", SENSOR_TEMP, UGURU_R_TEMP, 0x18 },
	{ "SYS", SENSOR_TEMP, UGURU_R_TEMP, 0x19 },
	{ "PWM1", SENSOR_TEMP, UGURU_R_TEMP, 0x1a },
	{ "PWM2", SENSOR_TEMP, UGURU_R_TEMP, 0x1b },
	{ "PWM3", SENSOR_TEMP, UGURU_R_TEMP, 0x1c },
	{ "PWM4", SENSOR_TEMP, UGURU_R_TEMP, 0x1d },
	{ "PWM5", SENSOR_TEMP, UGURU_R_TEMP, 0x1e },

	{ "VCORE", SENSOR_VOLTS_DC, UGURU_R_VOLT, 0x07, RFACT_NONE2 },
	{ "+3.3V", SENSOR_VOLTS_DC, UGURU_R_VOLT, 0x0a, RFACT2(34, 34) },
	{ "+5V", SENSOR_VOLTS_DC, UGURU_R_VOLT, 0x09, RFACT2(120, 60) },
	{ "+12V", SENSOR_VOLTS_DC, UGURU_R_VOLT, 0x0c, RFACT2(50, 10) },
	{ "+12V", SENSOR_VOLTS_DC, UGURU_R_VOLT, 0x08, RFACT2(50, 10) },
	{ "+5VSB", SENSOR_VOLTS_DC, UGURU_R_VOLT, 0x0b, RFACT2(120, 60) },
	{ "DDR", SENSOR_VOLTS_DC, UGURU_R_VOLT, 0x0d, RFACT2(34, 34) },
	{ "DDRVTT", SENSOR_VOLTS_DC, UGURU_R_VOLT, 0x0e, RFACT_NONE2 },
	{ "CPUVTT", SENSOR_VOLTS_DC, UGURU_R_VOLT, 0x03, RFACT_NONE2 },
	{ "HTV", SENSOR_VOLTS_DC, UGURU_R_VOLT, 0x05, RFACT_NONE2 },
	{ "NB", SENSOR_VOLTS_DC, UGURU_R_VOLT, 0x04, RFACT_NONE2 },
	{ "SB", SENSOR_VOLTS_DC, UGURU_R_VOLT, 0x06, RFACT_NONE2 },

	{ "CPU", SENSOR_FANRPM, UGURU_R_FAN, 0x20 },
	{ "SYS", SENSOR_FANRPM, UGURU_R_FAN, 0x22 },
	{ "AUX1", SENSOR_FANRPM, UGURU_R_FAN, 0x21 },
	{ "AUX2", SENSOR_FANRPM, UGURU_R_FAN, 0x23 },
	{ "AUX3", SENSOR_FANRPM, UGURU_R_FAN, 0x24 },
	{ "AUX4", SENSOR_FANRPM, UGURU_R_FAN, 0x25 },

	{ NULL }
};

int	 uguru_match(struct device *, void *, void *);
void	 uguru_attach(struct device *, struct device *, void *);
void	 uguru_refresh(void *);
int	 uguru_read_sensor(struct uguru_softc *, int);
int	 uguru_ac5_read_sensor(struct uguru_softc *, int);
int	 uguru_ac5_read(bus_space_tag_t, bus_space_handle_t,
	    uint16_t, void *, int);
int	 uguru_write_multi(bus_space_tag_t, bus_space_handle_t,
	    uint8_t, void *, int);
int	 uguru_read_multi(bus_space_tag_t, bus_space_handle_t, void *, int);

struct cfdriver uguru_cd = {
	NULL, "uguru", DV_DULL
};

const struct cfattach uguru_ca = {
	sizeof(struct uguru_softc), uguru_match, uguru_attach
};

int
uguru_match(struct device *parent, void *match, void *aux)
{
	struct isa_attach_args *ia = aux;
	bus_space_tag_t iot;
	bus_space_handle_t ioh;
	uint8_t data[9];
	uint16_t vendid, devid;
	int ret = 0;

	iot = ia->ia_iot;
	if (bus_space_map(iot, ia->ipa_io[0].base, UGURU_IOSIZE, 0, &ioh)) {
		DPRINTF(0, ": can't map i/o space\n");
		return 0;
	}

	UGURU_WRITE(iot, ioh, UGURU_INDEX, UGURU_PRODID);
	if (!uguru_read_multi(iot, ioh, &data, sizeof(data)) ||
	    !uguru_ac5_read(iot, ioh, 0x0904, &data, sizeof(data))) {
		vendid = data[0] << 8 | data[1];
		devid = data[2] << 8 | data[3];

		if (vendid == UGURU_VENDID_ABIT &&
		    (devid == UGURU_DEVID1 ||
		     devid == UGURU_DEVID2)) {
			ia->ipa_nio = 1;
			ia->ipa_io[0].length = UGURU_IOSIZE;
			ia->ipa_nmem = 0;
			ia->ipa_nirq = 0;
			ia->ipa_ndrq = 0;
			ret = 1;
		}
	}
	bus_space_unmap(iot, ioh, UGURU_IOSIZE);
	return (ret);
}

void
uguru_attach(struct device *parent, struct device *self, void *aux)
{
	struct uguru_softc *sc = (void *)self;
	struct isa_attach_args *ia = aux;
	const struct uguru_sensor *sensors;
	uint8_t data[9];
	uint16_t vendid, devid, sysid;
	int i;

	sc->sc_iot = ia->ia_iot;
	if (bus_space_map(sc->sc_iot, ia->ipa_io[0].base,
	    UGURU_IOSIZE, 0, &sc->sc_ioh)) {
		printf(": can't map i/o space\n");
		return;
	}

	UGURU_WRITE(sc->sc_iot, sc->sc_ioh, UGURU_INDEX, UGURU_PRODID);
	if (!uguru_read_multi(sc->sc_iot, sc->sc_ioh, &data, sizeof(data))) {
		sc->read = uguru_read_sensor;
		goto done;
	}

	/* AC2005 product ID */
	if (!uguru_ac5_read(sc->sc_iot, sc->sc_ioh,
	    0x0904, &data, sizeof(data))) {
		sc->read = uguru_ac5_read_sensor;
		goto done;
	}

	printf("\n");
	return;

done:
	DPRINTF(5, ": %.2x %.2x %.2x %.2x %.2x %.2x %.2x %.2x %.2x",
	    data[0], data[1], data[2], data[3], data[4],
	    data[5], data[6], data[7], data[8]);

	vendid = data[0] << 8 | data[1];
	devid = data[2] << 8 | data[3];
	sysid = data[3] << 8 | data[7];

	if (vendid != UGURU_VENDID_ABIT ||
	    (devid != UGURU_DEVID1 &&
	     devid != UGURU_DEVID2)) {
		printf(": attach failed\n");
		return;
	}
	printf(": AC%x", devid);

	switch(sysid) {
	case ABIT_SYSID_KV01:
	case ABIT_SYSID_AI01:
	case ABIT_SYSID_AN01:
		printf(" KV1");
		sensors = abitkv_sensors;
		break;
	case ABIT_SYSID_AA01:
	case ABIT_SYSID_AG01:
		printf(" AA1");
		sensors = abitaa_sensors;
		break;
	case ABIT_SYSID_AV01:
	case ABIT_SYSID_KVP1:
		printf(" AV1");
		sensors = abitav_sensors;
		break;
	case ABIT_SYSID_AS01:
		printf(" AS1");
		sensors = abitas_sensors;
		break;
	case ABIT_SYSID_AX01:
		printf(" AX1");
		sensors = abitax_sensors;
		break;
	case ABIT_SYSID_M401:
		printf(" M41");
		sensors = abitm4_sensors;
		break;
	case ABIT_SYSID_AN02:
		printf(" AN1");
		sensors = abitan_sensors;
		break;
	case ABIT_SYSID_AU01:
	case ABIT_SYSID_AL01:
	case ABIT_SYSID_BL01:
		printf(" AL1");
		sensors = abital_sensors;
		break;
	case ABIT_SYSID_AW01:
	case ABIT_SYSID_AW02:
		printf(" AW1");
		sensors = abitaw_sensors;
		break;
	case ABIT_SYSID_NI01:
		printf(" NI1");
		sensors = abitni_sensors;
		break;
	case ABIT_SYSID_AT01:
		printf(" AT1");
		sensors = abitat_sensors;
		break;
	case ABIT_SYSID_AN03:
		printf(" AN2");
		sensors = abitan2_sensors;
		break;
	case ABIT_SYSID_AB01:
		printf(" AB1");
		sensors = abitab_sensors;
		break;
	case ABIT_SYSID_AN04:
		printf(" AN3");
		sensors = abitan3_sensors;
		break;
	case ABIT_SYSID_AW03:
		printf(" AW2");
		sensors = abitaw2_sensors;
		break;
	case ABIT_SYSID_AT02:
		printf(" AT2");
		sensors = abitat2_sensors;
		break;
	case ABIT_SYSID_AB02:
	case ABIT_SYSID_IP01:
	case ABIT_SYSID_IX01:
	case ABIT_SYSID_IX02:
		printf(" AB2");
		sensors = abitab2_sensors;
		break;
	case ABIT_SYSID_IN01:
		printf(" IN1");
		sensors = abitin_sensors;
		break;
	default:
		printf(" unknown system (ID 0x%.4x)\n", sysid);
		return;
	}
	printf("\n");

	strlcpy(sc->sc_sensordev.xname,
	    sc->sc_dev.dv_xname,
	    sizeof(sc->sc_sensordev.xname));

	for (i = 0; sensors[i].desc != NULL; i++) {
		strlcpy(sc->sc_sensors[i].desc,
		    sensors[i].desc, sizeof(sc->sc_sensors[i].desc));
		sc->sc_sensors[i].type = sensors[i].type;
		sensor_attach(&sc->sc_sensordev, &sc->sc_sensors[i]);
		sc->sc_numsensors++;
	}
	sc->uguru_sensors = sensors;

	if (sensor_task_register(sc, uguru_refresh, UGURU_INTERVAL) == NULL) {
		printf("%s: unable to register update task\n",
		    sc->sc_sensordev.xname);
		return;
	}
	sensordev_install(&sc->sc_sensordev);
}

void
uguru_refresh(void *arg)
{
	struct uguru_softc *sc = (struct uguru_softc *)arg;
	int i;

	for (i = 0; i < sc->sc_numsensors; i++)
		sc->uguru_sensors[i].refresh(sc, i);
}

void
uguru_refresh_temp(struct uguru_softc *sc, int n)
{
	struct ksensor *sensor = &sc->sc_sensors[n];
	int status = SENSOR_S_OK;
	int ret;

	ret = sc->read(sc, n);
	if (sc->cs.reading == 0x00) {
		sensor->flags |= SENSOR_FINVALID;
		sensor->value = 0;
		return;
	}
	sensor->flags &= ~SENSOR_FINVALID;
	sensor->value = sc->cs.reading * 1000000 + 273150000;

	if (ret)
		status = SENSOR_S_UNSPEC;
	else {
		if (sc->cs.reading >= sc->cs.lower)
			status = SENSOR_S_WARN;
		if (sc->cs.reading >= sc->cs.upper)
			status = SENSOR_S_CRIT;
	}
	sensor->status = status;
}

void
uguru_refresh_volt(struct uguru_softc *sc, int n)
{
	int status = SENSOR_S_OK;

	if (sc->read(sc, n))
		status = SENSOR_S_UNSPEC;
	else
		if (sc->cs.reading <= sc->cs.lower ||
		    sc->cs.reading >= sc->cs.upper)
			status = SENSOR_S_CRIT;

	sc->sc_sensors[n].value =
	    sc->cs.reading * sc->uguru_sensors[n].rfact;
	sc->sc_sensors[n].status = status;
}

void
uguru_refresh_fan(struct uguru_softc *sc, int n)
{
	struct ksensor *sensor = &sc->sc_sensors[n];
	int ret;

	ret = sc->read(sc, n);
	if (sc->cs.reading == 0x00) {
		sensor->flags |= SENSOR_FINVALID;
		sensor->value = 0;
		return;
	}
	sensor->flags &= ~SENSOR_FINVALID;
	sensor->value = sc->cs.reading * 60;

	if (ret)
		sensor->status = SENSOR_S_UNSPEC;
	else
		if (sc->cs.reading <= sc->cs.lower)
			sensor->status = SENSOR_S_CRIT;
		else
			sensor->status = SENSOR_S_OK;
}

int
uguru_read_sensor(struct uguru_softc *sc, int n)
{
	struct ksensor *sensor = &sc->sc_sensors[n];
	bus_space_tag_t iot = sc->sc_iot;
	bus_space_handle_t ioh = sc->sc_ioh;
	uint8_t reg = sc->uguru_sensors[n].reg;
	uint8_t idx, data[3];
	uint8_t val = 0x00;
	int count, ret = 0;

	if (sensor->type == SENSOR_FANRPM)
		idx = UGURU_FAN_DATA;
	else
		idx = UGURU_ITM_DATA;

	/* sensor value */
	if (uguru_write_multi(iot, ioh, idx, &reg, sizeof(reg)) ||
	    uguru_read_multi(iot, ioh, &val, sizeof(val)))
		++ret;

	/* sensor status */
	bzero(&data, sizeof(data));
	count = sensor->type == SENSOR_FANRPM ? 2 : 3;

	if (uguru_write_multi(iot, ioh, idx + 1, &reg, sizeof(reg)) ||
	    uguru_read_multi(iot, ioh, &data, count))
		++ret;

	/* fill in current sensor structure */
	sc->cs.reading = val;
/*	sc->cs.flags = data[0]; */
	sc->cs.lower = data[1];
	sc->cs.upper = data[2];

	DPRINTF(50, "0x%.2x: 0x%.2x 0x%.2x 0x%.2x 0x%.2x 0x%.2x\n",
	    idx, reg, val, data[0], data[1], data[2]);

	return (ret);
}

int
uguru_ac5_read_sensor(struct uguru_softc *sc, int n)
{
	uint16_t reg;
	uint8_t val = 0x00;
	int ret = 1;

	reg = sc->uguru_sensors[n].reg | 0x0880;
	if (uguru_ac5_read(sc->sc_iot, sc->sc_ioh, reg, &val, sizeof(val)))
		++ret;

	sc->cs.reading = val;
	return (ret);
}

int
uguru_ac5_read(bus_space_tag_t iot, bus_space_handle_t ioh,
    uint16_t reg, void *data, int count)
{
	uint8_t buf[3];

	buf[0] = reg >> 8;
	buf[1] = reg & 0xff;
	buf[2] = count;

	if (!uguru_write_multi(iot, ioh, 0x1a, &buf, sizeof(buf)) &&
	    !uguru_read_multi(iot, ioh, data, count))
		return 0;

	DPRINTF(0, "uguru_ac5_read: timeout 0x%.2x 0x%.2x 0x%.2x\n",
	    buf[0], buf[1], buf[2]);

	return 1;
}

int
uguru_write_multi(bus_space_tag_t iot, bus_space_handle_t ioh,
    uint8_t idx, void *data, int count)
{
	uint8_t *inbuf = data;
	int i, ntries;

	UGURU_WRITE(iot, ioh, UGURU_INDEX, idx);

	for (i = 0; i < count; ++i) {
		/*
		 * wait for non-busy status before write
		 * to the data port.
		 */
		ntries = 0;
		while (UGURU_READ(iot, ioh, UGURU_INDEX) >> 1 & 1) {
			if (++ntries > 65)
				goto timeout;
			DELAY(5);
		}
		/* dummy read to flush the internal buffer */
		if (i == 0)
			UGURU_READ(iot, ioh, UGURU_DATA);

		UGURU_WRITE(iot, ioh, UGURU_DATA, *inbuf++);
	}
	return 0;

timeout:
	DPRINTF(0, "uguru_write_multi: timeout 0x%.2x\n", idx);
	return 1;
}

int
uguru_read_multi(bus_space_tag_t iot, bus_space_handle_t ioh,
    void *data, int count)
{
	uint8_t *outbuf = data;
	int i, ntries;

	for (i = 0; i < count; ++i) {
		/*
		 * wait for valid status before read
		 * from the data port.
		 */
		ntries = 0;
		while (!(UGURU_READ(iot, ioh, UGURU_INDEX) & 1)) {
			if (++ntries > 40) {
				DPRINTF(0, "uguru_read_multi: timeout\n");
				return 1;
			}
			DELAY(35);
		}
		*outbuf++ = UGURU_READ(iot, ioh, UGURU_DATA);
	}
	return 0;
}
