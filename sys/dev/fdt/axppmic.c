/*	$OpenBSD: axppmic.c,v 1.21 2024/10/06 03:46:48 jsg Exp $	*/
/*
 * Copyright (c) 2017 Mark Kettenis <kettenis@openbsd.org>
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
#include <sys/malloc.h>
#include <sys/sensors.h>

#include <dev/i2c/i2cvar.h>
#include <dev/fdt/rsbvar.h>

#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_regulator.h>
#include <dev/ofw/fdt.h>

extern void (*powerdownfn)(void);

#define AXP209_SDR		0x32
#define  AXP209_SDR_SHUTDOWN	(1 << 7)
#define AXP209_ADC_EN1		0x82
#define  AXP209_ADC_EN1_ACIN	(3 << 4)
#define  AXP209_ADC_EN1_VBUS	(3 << 2)

#define AXP803_IRQ1_EN		0x40
#define AXP803_IRQ2_EN		0x41
#define AXP803_IRQ3_EN		0x42
#define AXP803_IRQ4_EN		0x43
#define AXP803_IRQ5_EN		0x44
#define  AXP803_IRQ5_EN_PEK_SHORT	(1 << 4)
#define AXP803_IRQ6_EN		0x45
#define AXP803_IRQ1_STAT	0x48
#define AXP803_IRQ2_STAT	0x49
#define AXP803_IRQ3_STAT	0x4a
#define AXP803_IRQ4_STAT	0x4b
#define AXP803_IRQ5_STAT	0x4c
#define  AXP803_IRQ5_STAT_PEK_SHORT	(1 << 4)
#define AXP803_IRQ6_STAT	0x4d
#define AXP803_BAT_CAP_WARN		0xe6
#define  AXP803_BAT_CAP_WARN_LV1	0xf0
#define  AXP803_BAT_CAP_WARN_LV1BASE	5
#define  AXP803_BAT_CAP_WARN_LV2	0x0f

#define AXP806_REG_ADDR_EXT			0xff
#define  AXP806_REG_ADDR_EXT_MASTER_MODE	(0 << 4)
#define  AXP806_REG_ADDR_EXT_SLAVE_MODE		(1 << 4)

/* Regulators for AXP209, AXP221, AXP806 and AXP809. */

struct axppmic_regdata {
	const char *name;
	uint8_t ereg, emask, eval, dval;
	uint8_t vreg, vmask;
	uint32_t base, delta, nsteps;
	uint32_t base2, delta2, nsteps2;
};

const struct axppmic_regdata axp209_regdata[] = {
	{ "dcdc2", 0x12, (1 << 4), (1 << 4), (0 << 4),
	  0x23, 0x3f, 700000, 25000, 64 },
	{ "dcdc3", 0x12, (1 << 1), (1 << 1), (0 << 1),
	  0x27, 0x7f, 700000, 25000, 113 },
	/* LDO1 can't be controlled */
	{ "ldo2", 0x12, (1 << 2), (1 << 2), (0 << 2),
	  0x28, 0xf0, 1800000, (100000 >> 4), (16 << 4) },
	{ "ldo3", 0x12, (1 << 6), (1 << 6), (0 << 6),
	  0x29, 0x7f, 700000, 25000, 113 },
	/* LDO4 voltage levels are complicated */
	{ "ldo5", 0x90, 0x07, 0x03, 0x07,
	  0x91, 0xf0, 1800000, (100000 >> 4), (16 << 4) },
	{ NULL }
};

const struct axppmic_regdata axp221_regdata[] = {
	{ "dcdc1", 0x10, (1 << 1), (1 << 1), (0 << 1),
	  0x21, 0x1f, 1600000, 100000, 19 },
	{ "dcdc2", 0x10, (1 << 2), (1 << 2), (0 << 2),
	  0x22, 0x3f, 600000, 20000, 48 },
	{ "dcdc3", 0x10, (1 << 3), (1 << 3), (0 << 3),
	  0x23, 0x3f, 600000, 20000, 64 },
	{ "dcdc4", 0x10, (1 << 4), (1 << 4), (0 << 4),
	  0x24, 0x3f, 600000, 20000, 48 },
	{ "dcdc5", 0x10, (1 << 5), (1 << 5), (0 << 5),
	  0x25, 0x1f, 1000000, 50000, 32 },
	{ "dc1sw", 0x12, (1 << 7), (1 << 7), (0 << 7) },
	{ "dc5ldo", 0x10, (1 << 0), (1 << 0), (0 << 0),
	  0x1c, 0x07, 700000, 100000, 8 },
	{ "aldo1", 0x10, (1 << 6), (1 << 6), (0 << 6),
	  0x28, 0x1f, 700000, 100000, 27 },
	{ "aldo2", 0x10, (1 << 7), (1 << 7), (0 << 7),
	  0x29, 0x1f, 700000, 100000, 27 },
	{ "aldo3", 0x13, (1 << 7), (1 << 7), (0 << 7),
	  0x2a, 0x1f, 700000, 100000, 27 },
	{ "dldo1", 0x12, (1 << 3), (1 << 3), (0 << 3),
	  0x15, 0x1f, 700000, 100000, 27 },
	{ "dldo2", 0x12, (1 << 4), (1 << 4), (0 << 4),
	  0x16, 0x1f, 700000, 100000, 27 },
	{ "dldo3", 0x12, (1 << 5), (1 << 5), (0 << 5),
	  0x17, 0x1f, 700000, 100000, 27 },
	{ "dldo4", 0x12, (1 << 6), (1 << 6), (0 << 6),
	  0x18, 0x1f, 700000, 100000, 27 },
	{ "eldo1", 0x12, (1 << 0), (1 << 0), (0 << 0),
	  0x19, 0x1f, 700000, 100000, 27 },
	{ "eldo2", 0x12, (1 << 1), (1 << 1), (0 << 1),
	  0x1a, 0x1f, 700000, 100000, 27 },
	{ "eldo3", 0x12, (1 << 2), (1 << 2), (0 << 2),
	  0x1b, 0x1f, 700000, 100000, 27 },
	{ "ldo_io0", 0x90, 0x07, 0x03, 0x04,
	  0x91, 0x1f, 700000, 100000, 27 },
	{ "ldo_io1", 0x92, 0x07, 0x03, 0x04,
	  0x93, 0x1f, 700000, 100000, 27 },
	{ NULL }
};

const struct axppmic_regdata axp313a_regdata[] = {
	/* dcdc1: 1.6-3.4V (100mV step) not supported */
	{ "dcdc1", 0x10, (1 << 0), (1 << 0), (0 << 0),
	  0x13, 0x7f, 500000, 10000, 71, 122000, 20000, 17 },
	{ "dcdc2", 0x10, (1 << 1), (1 << 1), (0 << 1),
	  0x14, 0x7f, 500000, 10000, 71, 122000, 20000, 17 },
	{ "dcdc3", 0x10, (1 << 2), (1 << 2), (0 << 2),
	  0x15, 0x7f, 500000, 10000, 71, 122000, 20000, 32 },
	{ "aldo1", 0x10, (1 << 3), (1 << 3), (0 << 3),
	  0x16, 0x1f, 500000, 100000, 31 },
	{ "dldo1", 0x10, (1 << 4), (1 << 4), (0 << 4),
	  0x17, 0x1f, 500000, 100000, 31 },
	{ NULL }
};

const struct axppmic_regdata axp803_regdata[] = {
	{ "dcdc1", 0x10, (1 << 0), (1 << 0), (0 << 0),
	  0x20, 0x1f, 1600000, 100000, 19 },
	{ "dcdc2", 0x10, (1 << 1), (1 << 1), (0 << 1),
	  0x21, 0x7f, 500000, 10000, 71, 1220000, 20000, 5 },
	{ "dcdc3", 0x10, (1 << 2), (1 << 2), (0 << 2),
	  0x22, 0x7f, 500000, 10000, 71, 1220000, 20000, 5 },
	{ "dcdc4", 0x10, (1 << 3), (1 << 3), (0 << 3),
	  0x23, 0x7f, 500000, 10000, 71, 1220000, 20000, 5 },
	{ "dcdc5", 0x10, (1 << 4), (1 << 4), (0 << 4),
	  0x24, 0x7f, 800000, 10000, 33, 1140000, 20000, 36 },
	{ "dcdc6", 0x10, (1 << 5), (1 << 5), (0 << 5),
	  0x25, 0x7f, 600000, 10000, 51, 1120000, 20000, 21 },
	{ "dc1sw", 0x12, (1 << 7), (1 << 7), (0 << 7) },
	{ "aldo1", 0x13, (1 << 5), (1 << 5), (0 << 5),
	  0x28, 0x1f, 700000, 100000, 27 },
	{ "aldo2", 0x13, (1 << 6), (1 << 6), (0 << 6),
	  0x29, 0x1f, 700000, 100000, 27 },
	{ "aldo3", 0x13, (1 << 7), (1 << 7), (0 << 7),
	  0x2a, 0x1f, 700000, 100000, 27 },
	{ "dldo1", 0x12, (1 << 3), (1 << 3), (0 << 3),
	  0x15, 0x1f, 700000, 100000, 27 },
	{ "dldo2", 0x12, (1 << 4), (1 << 4), (0 << 4),
	  0x16, 0x1f, 700000, 100000, 27, 3400000, 200000, 5 },
	{ "dldo3", 0x12, (1 << 5), (1 << 5), (0 << 5),
	  0x17, 0x1f, 700000, 100000, 27 },
	{ "dldo4", 0x12, (1 << 6), (1 << 6), (0 << 6),
	  0x18, 0x1f, 700000, 100000, 27 },
	{ "eldo1", 0x12, (1 << 0), (1 << 0), (0 << 0),
	  0x19, 0x1f, 700000, 50000, 25 },
	{ "eldo2", 0x12, (1 << 1), (1 << 1), (0 << 1),
	  0x1a, 0x1f, 700000, 50000, 25 },
	{ "eldo3", 0x12, (1 << 2), (1 << 2), (0 << 2),
	  0x1b, 0x1f, 700000, 50000, 25 },
	{ "fldo1", 0x13, (1 << 2), (1 << 2), (0 << 2),
	  0x1c, 0x0f, 700000, 50000, 16 },
	{ "fldo2", 0x13, (1 << 3), (1 << 3), (0 << 3),
	  0x1d, 0x0f, 700000, 50000, 16 },
	{ "ldo-io0", 0x90, 0x07, 0x03, 0x04,
	  0x91, 0x1f, 700000, 100000, 27 },
	{ "ldo-io1", 0x92, 0x07, 0x03, 0x04,
	  0x93, 0x1f, 700000, 100000, 27 },
	{ NULL }
};

const struct axppmic_regdata axp806_regdata[] = {
	{ "dcdca", 0x10, (1 << 0), (1 << 0), (0 << 0),
	  0x12, 0x7f, 600000, 10000, 51, 1120000, 20000, 21 },
	{ "dcdcb", 0x10, (1 << 1), (1 << 1), (0 << 1),
	  0x13, 0x1f, 1000000, 50000, 32 },
	{ "dcdcc", 0x10, (1 << 2), (1 << 2), (0 << 2),
	  0x14, 0x7f, 600000, 10000, 51, 1120000, 20000, 21 },
	{ "dcdcd", 0x10, (1 << 3), (1 << 3), (0 << 3),
	  0x15, 0x3f, 600000, 20000, 46, 1600000, 100000, 18 },
	{ "dcdce", 0x10, (1 << 4), (1 << 4), (0 << 4),
	  0x16, 0x1f, 1100000, 100000, 24 },
	{ "aldo1", 0x10, (1 << 5), (1 << 5), (0 << 5),
	  0x17, 0x1f, 700000, 100000, 27 },
	{ "aldo2", 0x10, (1 << 6), (1 << 6), (0 << 6),
	  0x18, 0x1f, 700000, 100000, 27 },
	{ "aldo3", 0x10, (1 << 7), (1 << 7), (0 << 7),
	  0x19, 0x1f, 700000, 100000, 27 },
	{ "bldo1", 0x11, (1 << 0), (1 << 0), (0 << 0),
	  0x20, 0x0f, 700000, 100000, 13 },
	{ "bldo2", 0x11, (1 << 1), (1 << 1), (0 << 1),
	  0x21, 0x0f, 700000, 100000, 13 },
	{ "bldo3", 0x11, (1 << 2), (1 << 2), (0 << 2),
	  0x22, 0x0f, 700000, 100000, 13 },
	{ "bldo4", 0x11, (1 << 3), (1 << 3), (0 << 3),
	  0x23, 0x0f, 700000, 100000, 13 },
	{ "cldo1", 0x11, (1 << 4), (1 << 4), (0 << 4),
	  0x24, 0x1f, 700000, 100000 , 27},
	{ "cldo2", 0x11, (1 << 5), (1 << 5), (0 << 5),
	  0x25, 0x1f, 700000, 100000, 28, 3600000, 200000, 4 },
	{ "cldo3", 0x11, (1 << 6), (1 << 6), (0 << 6),
	  0x26, 0x1f, 700000, 100000, 27 },
	{ "sw", 0x11, (1 << 7), (1 << 7), (0 << 7) },
	{ NULL }
};

const struct axppmic_regdata axp809_regdata[] = {
	{ "dcdc1", 0x10, (1 << 1), (1 << 1), (0 << 1),
	  0x21, 0x1f, 1600000, 100000, 19 },
	{ "dcdc2", 0x10, (1 << 2), (1 << 2), (0 << 2),
	  0x22, 0x3f, 600000, 20000, 48 },
	{ "dcdc3", 0x10, (1 << 3), (1 << 3), (0 << 3),
	  0x23, 0x3f, 600000, 20000, 64 },
	{ "dcdc4", 0x10, (1 << 4), (1 << 4), (0 << 4),
	  0x24, 0x3f, 600000, 20000, 48, 1800000, 100000, 9 },
	{ "dcdc5", 0x10, (1 << 5), (1 << 5), (0 << 5),
	  0x25, 0x1f, 1000000, 50000, 32 },
	{ "dc5ldo", 0x10, (1 << 0), (1 << 0), (0 << 0),
	  0x1c, 0x07, 700000, 100000, 8 },
	{ "aldo1", 0x10, (1 << 6), (1 << 6), (0 << 6),
	  0x28, 0x1f, 700000, 100000, 27 },
	{ "aldo2", 0x10, (1 << 7), (1 << 7), (0 << 7),
	  0x29, 0x1f, 700000, 100000, 27 },
	{ "aldo3", 0x12, (1 << 5), (1 << 5), (0 << 5),
	  0x2a, 0x1f, 700000, 100000, 27 },
	{ "dldo1", 0x12, (1 << 3), (1 << 3), (0 << 3),
	  0x15, 0x1f, 700000, 100000, 27, 3400000, 200000, 5 },
	{ "dldo2", 0x12, (1 << 4), (1 << 4), (0 << 4),
	  0x16, 0x1f, 700000, 100000, 27 },
	{ "eldo1", 0x12, (1 << 0), (1 << 0), (0 << 0),
	  0x19, 0x1f, 700000, 100000, 27 },
	{ "eldo2", 0x12, (1 << 1), (1 << 1), (0 << 1),
	  0x1a, 0x1f, 700000, 100000, 27 },
	{ "eldo3", 0x12, (1 << 2), (1 << 2), (0 << 2),
	  0x1b, 0x1f, 700000, 100000, 27 },
	{ "ldo_io0", 0x90, 0x07, 0x03, 0x04,
	  0x91, 0x1f, 700000, 100000, 27 },
	{ "ldo_io1", 0x92, 0x07, 0x03, 0x04,
	  0x93, 0x1f, 700000, 100000, 27 },
	{ NULL }
};

const struct axppmic_regdata axp15060_regdata[] = {
	{ "dcdc1", 0x10, (1 << 0), (1 << 0), (0 << 0),
	  0x13, 0x1f, 15000000, 100000, 20 },
	{ "dcdc2", 0x10, (1 << 1), (1 << 1), (0 << 1),
	  0x14, 0x7f, 500000, 10000, 71, 1220000, 20000, 17 },
	{ "dcdc3", 0x10, (1 << 2), (1 << 2), (0 << 2),
	  0x15, 0x7f, 500000, 10000, 71, 1220000, 20000, 17 },
	{ "dcdc4", 0x10, (1 << 3), (1 << 3), (0 << 3),
	  0x16, 0x7f, 500000, 10000, 71, 1220000, 20000, 17 },
	{ "dcdc5", 0x10, (1 << 4), (1 << 4), (0 << 4),
	  0x17, 0x7f, 800000, 10000, 33, 1140000, 20000, 36 },
	{ "dcdc6", 0x10, (1 << 5), (1 << 5), (0 << 5),
	  0x18, 0x1f, 500000, 100000, 30 },
	{ "aldo1", 0x11, (1 << 0), (1 << 0), (0 << 0),
	  0x19, 0x1f, 700000, 100000, 27 },
	{ "aldo2", 0x11, (1 << 1), (1 << 1), (0 << 1),
	  0x20, 0x1f, 700000, 100000, 27 },
	{ "aldo3", 0x11, (1 << 2), (1 << 2), (0 << 2),
	  0x21, 0x1f, 700000, 100000, 27 },
	{ "aldo4", 0x11, (1 << 3), (1 << 3), (0 << 3),
	  0x22, 0x1f, 700000, 100000, 27 },
	{ "aldo5", 0x11, (1 << 4), (1 << 4), (0 << 4),
	  0x23, 0x1f, 700000, 100000, 27 },
	{ "bldo1", 0x11, (1 << 5), (1 << 5), (0 << 5),
	  0x24, 0x1f, 700000, 100000, 27 },
	{ "bldo2", 0x11, (1 << 6), (1 << 6), (0 << 6),
	  0x25, 0x1f, 700000, 100000, 27 },
	{ "bldo3", 0x11, (1 << 7), (1 << 7), (0 << 7),
	  0x26, 0x1f, 700000, 100000, 27 },
	{ "bldo4", 0x12, (1 << 0), (1 << 0), (0 << 0),
	  0x27, 0x1f, 700000, 100000, 27 },
	{ "bldo5", 0x12, (1 << 1), (1 << 1), (0 << 1),
	  0x28, 0x1f, 700000, 100000, 27 },
	{ "cldo1", 0x12, (1 << 2), (1 << 2), (0 << 2),
	  0x29, 0x1f, 700000, 100000, 27 },
	{ "cldo2", 0x12, (1 << 3), (1 << 3), (0 << 3),
	  0x2a, 0x1f, 700000, 100000, 27 },
	{ "cldo3", 0x12, (1 << 4), (1 << 4), (0 << 4),
	  0x2b, 0x1f, 700000, 100000, 27 },
	{ "cldo4", 0x12, (1 << 5), (1 << 5), (0 << 5),
	  0x2d, 0x3f, 700000, 100000, 36 },
	{ "cpusldo", 0x12, (1 << 6), (1 << 6), (0 << 6),
	  0x2e, 0x0f, 700000, 50000, 15 },
	{ "sw", 0x12, (1 << 7), (1 << 7), (0 << 7) },
	{ NULL }
};

/* Sensors for AXP209 and AXP221/AXP809. */

#define AXPPMIC_NSENSORS 12

struct axppmic_sensdata {
	const char *name;
	enum sensor_type type;
	uint8_t reg;
	uint64_t base, delta;
};

const struct axppmic_sensdata axp209_sensdata[] = {
	{ "ACIN", SENSOR_INDICATOR, 0x00, (1 << 7), (1 << 6) },
	{ "VBUS", SENSOR_INDICATOR, 0x00, (1 << 5), (1 << 4) },
	{ "ACIN", SENSOR_VOLTS_DC, 0x56, 0, 1700 },
	{ "ACIN", SENSOR_AMPS, 0x58, 0, 625 },
	{ "VBUS", SENSOR_VOLTS_DC, 0x5a, 0, 1700 },
	{ "VBUS", SENSOR_AMPS, 0x5c, 0, 375 },
	{ "", SENSOR_TEMP, 0x5e, 128450000, 100000 },
	{ "APS", SENSOR_VOLTS_DC, 0x7e, 0, 1400 },
	{ NULL }
};

const struct axppmic_sensdata axp221_sensdata[] = {
	{ "ACIN", SENSOR_INDICATOR, 0x00, (1 << 7), (1 << 6) },
	{ "VBUS", SENSOR_INDICATOR, 0x00, (1 << 5), (1 << 4) },
	{ "", SENSOR_TEMP, 0x56, 5450000, 105861 },
	{ NULL }
};

const struct axppmic_sensdata axp803_sensdata[] = {
	{ "ACIN", SENSOR_INDICATOR, 0x00, (1 << 7), (1 << 6) },
	{ "VBUS", SENSOR_INDICATOR, 0x00, (1 << 5), (1 << 4) },
	{ "", SENSOR_TEMP, 0x56, 5450000, 106250 },
	{ NULL }
};
	
const struct axppmic_sensdata axp803_battery_sensdata[] = {
	{ "ACIN", SENSOR_INDICATOR, 0x00, (1 << 7), (1 << 6) },
	{ "VBUS", SENSOR_INDICATOR, 0x00, (1 << 5), (1 << 4) },
	{ "", SENSOR_TEMP, 0x56, 5450000, 106250 },
	{ "battery present", SENSOR_INDICATOR, 0x01, (1 << 5), (1 << 4) },
	{ "battery charging", SENSOR_INDICATOR, 0x01, (1 << 6), (1 << 6) },
	{ "battery percent", SENSOR_PERCENT, 0xb9, 0x7f, (1 << 7) },
	{ "battery voltage", SENSOR_VOLTS_DC, 0x78, 0x00, 1100 },
	{ "battery charging current", SENSOR_AMPS, 0x7a, 0x00, 1000 },
	{ "battery discharging current", SENSOR_AMPS, 0x7c, 0x00, 1000 },
	{ "battery maximum capacity", SENSOR_AMPHOUR, 0xe0, 0x00, 1456 },
	{ "battery current capacity", SENSOR_AMPHOUR, 0xe2, 0x00, 1456 },
	{ NULL }
};

struct axppmic_device {
	const char *name;
	const char *chip;
	const struct axppmic_regdata *regdata;
	const struct axppmic_sensdata *sensdata;
};

const struct axppmic_device axppmic_devices[] = {
	{ "x-powers,axp152", "AXP152" },
	{ "x-powers,axp209", "AXP209", axp209_regdata, axp209_sensdata },
	{ "x-powers,axp221", "AXP221", axp221_regdata, axp221_sensdata },
	{ "x-powers,axp223", "AXP223", axp221_regdata, axp221_sensdata },
	{ "x-powers,axp305", "AXP305", axp806_regdata },
	{ "x-powers,axp313a", "AXP313A", axp313a_regdata },
	{ "x-powers,axp803", "AXP803", axp803_regdata, axp803_sensdata },
	{ "x-powers,axp805", "AXP805", axp806_regdata },
	{ "x-powers,axp806", "AXP806", axp806_regdata },
	{ "x-powers,axp809", "AXP809", axp809_regdata, axp221_sensdata },
	{ "x-powers,axp15060", "AXP15060", axp15060_regdata },
};

const struct axppmic_device *
axppmic_lookup(const char *name)
{
	int i;

	for (i = 0; i < nitems(axppmic_devices); i++) {
		if (strcmp(name, axppmic_devices[i].name) == 0)
			return &axppmic_devices[i];
	}

	return NULL;
}

struct axppmic_softc {
	struct device	sc_dev;
	void		*sc_cookie;
	uint16_t 	sc_addr;
	const char	*sc_name;

	uint8_t		(*sc_read)(struct axppmic_softc *, uint8_t);
	void		(*sc_write)(struct axppmic_softc *, uint8_t, uint8_t);
	const struct axppmic_regdata *sc_regdata;
	const struct axppmic_sensdata *sc_sensdata;

	struct ksensor	sc_sensor[AXPPMIC_NSENSORS];
	struct ksensordev sc_sensordev;

	uint8_t 	sc_warn;
	uint8_t		sc_crit;
};

static inline uint8_t
axppmic_read_reg(struct axppmic_softc *sc, uint8_t reg)
{
	return sc->sc_read(sc, reg);
}

static inline void
axppmic_write_reg(struct axppmic_softc *sc, uint8_t reg, uint8_t value)
{
	sc->sc_write(sc, reg, value);
}

void	axppmic_attach_common(struct axppmic_softc *, const char *, int);
int	axppmic_activate(struct device *, int);

/* I2C interface */

int	axppmic_i2c_match(struct device *, void *, void *);
void	axppmic_i2c_attach(struct device *, struct device *, void *);

const struct cfattach axppmic_ca = {
	sizeof(struct axppmic_softc), axppmic_i2c_match, axppmic_i2c_attach,
	NULL, axppmic_activate
};

struct cfdriver axppmic_cd = {
	NULL, "axppmic", DV_DULL
};

uint8_t	axppmic_i2c_read(struct axppmic_softc *, uint8_t);
void	axppmic_i2c_write(struct axppmic_softc *, uint8_t, uint8_t);

int
axppmic_i2c_match(struct device *parent, void *match, void *aux)
{
	struct i2c_attach_args *ia = aux;

	if (axppmic_lookup(ia->ia_name))
		return 1;
	return 0;
}

void
axppmic_i2c_attach(struct device *parent, struct device *self, void *aux)
{
	struct axppmic_softc *sc = (struct axppmic_softc *)self;
	struct i2c_attach_args *ia = aux;
	int node = *(int *)ia->ia_cookie;

	sc->sc_cookie = ia->ia_tag;
	sc->sc_addr = ia->ia_addr;
	sc->sc_read = axppmic_i2c_read;
	sc->sc_write = axppmic_i2c_write;

	axppmic_attach_common(sc, ia->ia_name, node);
}

uint8_t
axppmic_i2c_read(struct axppmic_softc *sc, uint8_t reg)
{
	i2c_tag_t tag = sc->sc_cookie;
	int flags = cold ? I2C_F_POLL : 0;
	int error;
	uint8_t value;

	iic_acquire_bus(tag, flags);
	error = iic_smbus_read_byte(tag, sc->sc_addr, reg, &value, flags);
	iic_release_bus(tag, flags);
	if (error) {
		printf("%s: SMBus read byte from 0x%02x failed\n",
		    sc->sc_dev.dv_xname, reg);
		return 0xff;
	}

	return value;
}

void
axppmic_i2c_write(struct axppmic_softc *sc, uint8_t reg, uint8_t value)
{
	i2c_tag_t tag = sc->sc_cookie;
	int flags = cold ? I2C_F_POLL : 0;
	int error;

	iic_acquire_bus(tag, flags);
	error = iic_smbus_write_byte(tag, sc->sc_addr, reg, value, flags);
	iic_release_bus(tag, flags);
	if (error)
		printf("%s: SMBus write byte to 0x%02x failed\n",
		    sc->sc_dev.dv_xname, reg);
}

/* RSB interface */

#include "sxirsb.h"

#if NSXIRSB > 0

int	axppmic_rsb_match(struct device *, void *, void *);
void	axppmic_rsb_attach(struct device *, struct device *, void *);

const struct cfattach axppmic_rsb_ca = {
	sizeof(struct axppmic_softc), axppmic_rsb_match, axppmic_rsb_attach,
	NULL, axppmic_activate
};

uint8_t	axppmic_rsb_read(struct axppmic_softc *, uint8_t);
void	axppmic_rsb_write(struct axppmic_softc *, uint8_t, uint8_t);

int
axppmic_rsb_match(struct device *parent, void *match, void *aux)
{
	struct rsb_attach_args *ra = aux;

	if (axppmic_lookup(ra->ra_name))
		return 1;
	return 0;
}

void
axppmic_rsb_attach(struct device *parent, struct device *self, void *aux)
{
	struct axppmic_softc *sc = (struct axppmic_softc *)self;
	struct rsb_attach_args *ra = aux;

	sc->sc_cookie = ra->ra_cookie;
	sc->sc_addr = ra->ra_rta;
	sc->sc_read = axppmic_rsb_read;
	sc->sc_write = axppmic_rsb_write;

	axppmic_attach_common(sc, ra->ra_name, ra->ra_node);
}

uint8_t
axppmic_rsb_read(struct axppmic_softc *sc, uint8_t reg)
{
	return rsb_read_1(sc->sc_cookie, sc->sc_addr, reg);
}

void
axppmic_rsb_write(struct axppmic_softc *sc, uint8_t reg, uint8_t value)
{
	rsb_write_1(sc->sc_cookie, sc->sc_addr, reg, value);
}

#endif

/* Common code */

void	axppmic_attach_node(struct axppmic_softc *, int);
void	axppmic_attach_regulators(struct axppmic_softc *, int);
void	axppmic_attach_sensors(struct axppmic_softc *);

struct axppmic_softc *axppmic_sc;
void	axp209_powerdown(void);

void
axppmic_attach_common(struct axppmic_softc *sc, const char *name, int node)
{
	const struct axppmic_device *device;
	int child;

	device = axppmic_lookup(name);
	printf(": %s\n", device->chip);

	sc->sc_name = device->name;
	sc->sc_regdata = device->regdata;
	sc->sc_sensdata = device->sensdata;

	/* Switch AXP806 into master or slave mode. */
	if (strcmp(name, "x-powers,axp305") == 0 ||
	    strcmp(name, "x-powers,axp805") == 0 ||
	    strcmp(name, "x-powers,axp806") == 0) {
	    if (OF_getproplen(node, "x-powers,master-mode") == 0 ||
	        OF_getproplen(node, "x-powers,self-working-mode") == 0) {
			axppmic_write_reg(sc, AXP806_REG_ADDR_EXT,
			    AXP806_REG_ADDR_EXT_MASTER_MODE);
		} else {
			axppmic_write_reg(sc, AXP806_REG_ADDR_EXT,
			    AXP806_REG_ADDR_EXT_SLAVE_MODE);
		}
	}

	/* Enable data collection on AXP209. */
	if (strcmp(name, "x-powers,axp209") == 0) {
		uint8_t reg;

		/* Turn on sampling of ACIN and VBUS voltage and current. */
		reg = axppmic_read_reg(sc, AXP209_ADC_EN1);
		reg |= AXP209_ADC_EN1_ACIN;
		reg |= AXP209_ADC_EN1_VBUS;
		axppmic_write_reg(sc, AXP209_ADC_EN1, reg);
	}

	/* Read battery warning levels on AXP803. */
	if (strcmp(name, "x-powers,axp803") == 0) {
		uint8_t value;

		value = axppmic_read_reg(sc, AXP803_BAT_CAP_WARN);
		sc->sc_warn = ((value & AXP803_BAT_CAP_WARN_LV1) >> 4);
		sc->sc_warn += AXP803_BAT_CAP_WARN_LV1BASE;
		sc->sc_crit = (value & AXP803_BAT_CAP_WARN_LV2);
	}

	for (child = OF_child(node); child; child = OF_peer(child))
		axppmic_attach_node(sc, child);

	if (sc->sc_regdata)
		axppmic_attach_regulators(sc, node);

	if (sc->sc_sensdata)
		axppmic_attach_sensors(sc);

	/* Disable all interrupts on AXP803. */
	if (strcmp(name, "x-powers,axp803") == 0) {
		axppmic_write_reg(sc, AXP803_IRQ1_EN, 0);
		axppmic_write_reg(sc, AXP803_IRQ2_EN, 0);
		axppmic_write_reg(sc, AXP803_IRQ3_EN, 0);
		axppmic_write_reg(sc, AXP803_IRQ4_EN, 0);
		axppmic_write_reg(sc, AXP803_IRQ5_EN, 0);
		axppmic_write_reg(sc, AXP803_IRQ6_EN, 0);
	}

#ifdef __armv7__
	if (strcmp(name, "x-powers,axp152") == 0 ||
	    strcmp(name, "x-powers,axp209") == 0) {
		axppmic_sc = sc;
		powerdownfn = axp209_powerdown;
	}
#endif

#ifdef SUSPEND
	/* AXP803 can wake us up. */
	if (strcmp(name, "x-powers,axp803") == 0)
		device_register_wakeup(&sc->sc_dev);
#endif
}

void
axppmic_attach_node(struct axppmic_softc *sc, int node)
{
	char status[32];

	if (OF_getprop(node, "status", status, sizeof(status)) > 0 &&
	    strcmp(status, "disabled") == 0)
		return;

	if (OF_is_compatible(node, "x-powers,axp803-battery-power-supply"))
		sc->sc_sensdata = axp803_battery_sensdata;
}

int
axppmic_activate(struct device *self, int act)
{
	struct axppmic_softc *sc = (struct axppmic_softc *)self;

	switch (act) {
	case DVACT_SUSPEND:
		if (strcmp(sc->sc_name, "x-powers,axp803") == 0) {
			/* Enable interrupt for short power button press. */
			axppmic_write_reg(sc, AXP803_IRQ5_STAT,
			    AXP803_IRQ5_STAT_PEK_SHORT);
			axppmic_write_reg(sc, AXP803_IRQ5_EN,
			    AXP803_IRQ5_EN_PEK_SHORT);
		}
		break;
	case DVACT_RESUME:
		if (strcmp(sc->sc_name, "x-powers,axp803") == 0) {
			/* Disable interrupt for short power button press. */
			axppmic_write_reg(sc, AXP803_IRQ5_EN, 0);
		}
		break;
	}

	return 0;
}

/* Regulators */

struct axppmic_regulator {
	struct axppmic_softc *ar_sc;

	uint8_t ar_ereg, ar_emask;
	uint8_t ar_eval, ar_dval;

	uint8_t ar_vreg, ar_vmask;
	uint32_t ar_base, ar_delta, ar_nsteps;
	uint32_t ar_base2, ar_delta2, ar_nsteps2;

	struct regulator_device ar_rd;
};

void	axppmic_attach_regulator(struct axppmic_softc *, int);
uint32_t axppmic_get_voltage(void *);
int	axppmic_set_voltage(void *, uint32_t);
int	axppmic_enable(void *, int);

void
axppmic_attach_regulators(struct axppmic_softc *sc, int node)
{
	node = OF_getnodebyname(node, "regulators");
	if (node == 0)
		return;

	for (node = OF_child(node); node; node = OF_peer(node))
		axppmic_attach_regulator(sc, node);
}

void
axppmic_attach_regulator(struct axppmic_softc *sc, int node)
{
	struct axppmic_regulator *ar;
	char name[32];
	int i;

	name[0] = 0;
	OF_getprop(node, "name", name, sizeof(name));
	name[sizeof(name) - 1] = 0;
	for (i = 0; sc->sc_regdata[i].name; i++) {
		if (strcmp(sc->sc_regdata[i].name, name) == 0)
			break;
	}
	if (sc->sc_regdata[i].name == NULL)
		return;

	ar = malloc(sizeof(*ar), M_DEVBUF, M_WAITOK | M_ZERO);
	ar->ar_sc = sc;

	ar->ar_ereg = sc->sc_regdata[i].ereg;
	ar->ar_emask = sc->sc_regdata[i].emask;
	ar->ar_eval = sc->sc_regdata[i].eval;
	ar->ar_dval = sc->sc_regdata[i].dval;
	ar->ar_vreg = sc->sc_regdata[i].vreg;
	ar->ar_vmask = sc->sc_regdata[i].vmask;
	ar->ar_base = sc->sc_regdata[i].base;
	ar->ar_delta = sc->sc_regdata[i].delta;
	ar->ar_nsteps = sc->sc_regdata[i].nsteps;
	ar->ar_base2 = sc->sc_regdata[i].base2;
	ar->ar_delta2 = sc->sc_regdata[i].delta2;
	ar->ar_nsteps2 = sc->sc_regdata[i].nsteps2;

	ar->ar_rd.rd_node = node;
	ar->ar_rd.rd_cookie = ar;
	ar->ar_rd.rd_get_voltage = axppmic_get_voltage;
	ar->ar_rd.rd_set_voltage = axppmic_set_voltage;
	ar->ar_rd.rd_enable = axppmic_enable;
	regulator_register(&ar->ar_rd);
}

uint32_t
axppmic_get_voltage(void *cookie)
{
	struct axppmic_regulator *ar = cookie;
	uint32_t voltage;
	uint8_t value;

	value = axppmic_read_reg(ar->ar_sc, ar->ar_vreg);
	value &= ar->ar_vmask;
	if (ar->ar_base2 > 0 && value >= ar->ar_nsteps) {
		voltage =
		     ar->ar_base2 + (value - ar->ar_nsteps) * ar->ar_delta2;
	} else {
		voltage = ar->ar_base + value * ar->ar_delta;
	}
	return voltage;
}

int
axppmic_set_voltage(void *cookie, uint32_t voltage)
{
	struct axppmic_regulator *ar = cookie;
	uint32_t value, reg;

	if (voltage < ar->ar_base)
		return EINVAL;
	if (ar->ar_base2 > 0 && voltage >= ar->ar_base2) {
		value = (voltage - ar->ar_base2) / ar->ar_delta2;
		if (value >= ar->ar_nsteps2)
			return EINVAL;
		value += ar->ar_nsteps;
	} else {
		value = (voltage - ar->ar_base) / ar->ar_delta;
		if (value >= ar->ar_nsteps)
			return EINVAL;
	}
	if (value > ar->ar_vmask)
		return EINVAL;

	reg = axppmic_read_reg(ar->ar_sc, ar->ar_vreg);
	axppmic_write_reg(ar->ar_sc, ar->ar_vreg,
	    (reg & ~ar->ar_vmask) | (value & ar->ar_vmask));
	return 0;
}

int
axppmic_enable(void *cookie, int on)
{
	struct axppmic_regulator *ar = cookie;
	uint8_t reg;

	reg = axppmic_read_reg(ar->ar_sc, ar->ar_ereg);
	reg &= ~ar->ar_emask;
	if (on)
		reg |= ar->ar_eval;
	else
		reg |= ar->ar_dval;
	axppmic_write_reg(ar->ar_sc, ar->ar_ereg, reg);
	return 0;
}

/* Sensors */

void	axppmic_update_sensors(void *);
void	axppmic_update_indicator(struct axppmic_softc *, int);
void	axppmic_update_percent(struct axppmic_softc *, int);
void	axppmic_update_amphour(struct axppmic_softc *, int);
void	axppmic_update_sensor(struct axppmic_softc *, int);

void
axppmic_attach_sensors(struct axppmic_softc *sc)
{
	int i;

	for (i = 0; sc->sc_sensdata[i].name; i++) {
		KASSERT(i < AXPPMIC_NSENSORS);

		sc->sc_sensor[i].type = sc->sc_sensdata[i].type;
		strlcpy(sc->sc_sensor[i].desc, sc->sc_sensdata[i].name,
		    sizeof(sc->sc_sensor[i].desc));
		sensor_attach(&sc->sc_sensordev, &sc->sc_sensor[i]);
	}

	axppmic_update_sensors(sc);
	if (sensor_task_register(sc, axppmic_update_sensors, 5) == NULL) {
		printf(", unable to register update task\n");
		return;
	}

	strlcpy(sc->sc_sensordev.xname, sc->sc_dev.dv_xname,
	    sizeof(sc->sc_sensordev.xname));
	sensordev_install(&sc->sc_sensordev);
}

void
axppmic_update_sensors(void *arg)
{
	struct axppmic_softc *sc = arg;
	int i;

	for (i = 0; sc->sc_sensdata[i].name; i++) {
		switch (sc->sc_sensdata[i].type) {
		case SENSOR_INDICATOR:
			axppmic_update_indicator(sc, i);
			break;
		case SENSOR_PERCENT:
			axppmic_update_percent(sc, i);
			break;
		case SENSOR_AMPHOUR:
			axppmic_update_amphour(sc, i);
			break;
		default:
			axppmic_update_sensor(sc, i);
			break;
		}
	}
}

void
axppmic_update_indicator(struct axppmic_softc *sc, int i)
{
	uint8_t reg = sc->sc_sensdata[i].reg;
	uint8_t mask = sc->sc_sensdata[i].base;
	uint8_t mask_ok = sc->sc_sensdata[i].delta;
	uint8_t value;

	value = axppmic_read_reg(sc, reg);
	sc->sc_sensor[i].value = (value & mask) ? 1 : 0;
	if (value & mask) {
		sc->sc_sensor[i].status =
		    (value & mask_ok) ? SENSOR_S_OK : SENSOR_S_WARN;
	} else {
		sc->sc_sensor[i].status = SENSOR_S_UNSPEC;
	}
}

void
axppmic_update_percent(struct axppmic_softc *sc, int i)
{
	uint8_t reg = sc->sc_sensdata[i].reg;
	uint8_t mask = sc->sc_sensdata[i].base;
	uint8_t mask_ok = sc->sc_sensdata[i].delta;
	uint8_t value;

	value = axppmic_read_reg(sc, reg);
	sc->sc_sensor[i].value = (value & mask) * 1000;

	if (value & mask_ok) {
		if ((value & mask) <= sc->sc_crit)
			sc->sc_sensor[i].status = SENSOR_S_CRIT;
		else if ((value & mask) <= sc->sc_warn)
			sc->sc_sensor[i].status = SENSOR_S_WARN;
		else
			sc->sc_sensor[i].status = SENSOR_S_OK;
	} else {
		sc->sc_sensor[i].status = SENSOR_S_UNSPEC;
	}
}

void
axppmic_update_amphour(struct axppmic_softc *sc, int i)
{
	uint8_t reg = sc->sc_sensdata[i].reg;
	uint64_t base = sc->sc_sensdata[i].base;
	uint64_t delta = sc->sc_sensdata[i].delta;
	uint16_t value;

	value = axppmic_read_reg(sc, reg);
	sc->sc_sensor[i].status = (value & 0x80) ? SENSOR_S_OK : SENSOR_S_WARN;
	value = ((value & 0x7f) << 8) | axppmic_read_reg(sc, reg + 1);
	sc->sc_sensor[i].value = base + value * delta;
}

void
axppmic_update_sensor(struct axppmic_softc *sc, int i)
{
	uint8_t reg = sc->sc_sensdata[i].reg;
	uint64_t base = sc->sc_sensdata[i].base;
	uint64_t delta = sc->sc_sensdata[i].delta;
	uint16_t value;

	value = axppmic_read_reg(sc, reg);
	value = (value << 4) | axppmic_read_reg(sc, reg + 1);
	sc->sc_sensor[i].value = base + value * delta;
}

void
axp209_powerdown(void)
{
	axppmic_write_reg(axppmic_sc, AXP209_SDR, AXP209_SDR_SHUTDOWN);
}
