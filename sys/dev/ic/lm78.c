/*	$OpenBSD: lm78.c,v 1.26 2022/04/08 15:02:28 naddy Exp $	*/

/*
 * Copyright (c) 2005, 2006 Mark Kettenis
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
#include <sys/kernel.h>
#include <sys/queue.h>
#include <sys/sensors.h>

#include <dev/ic/lm78var.h>
#include <dev/isa/wbsioreg.h>

#if defined(LMDEBUG)
#define DPRINTF(x)		do { printf x; } while (0)
#else
#define DPRINTF(x)
#endif

/*
 * LM78-compatible chips can typically measure voltages up to 4.096 V.
 * To measure higher voltages the input is attenuated with (external)
 * resistors.  Negative voltages are measured using inverting op amps
 * and resistors.  So we have to convert the sensor values back to
 * real voltages by applying the appropriate resistor factor.
 */
#define RFACT_NONE	10000
#define RFACT(x, y)	(RFACT_NONE * ((x) + (y)) / (y))
#define NRFACT(x, y)	(-RFACT_NONE * (x) / (y))

struct cfdriver lm_cd = {
	NULL, "lm", DV_DULL
};

int  lm_match(struct lm_softc *);
int  wb_match(struct lm_softc *);
int  def_match(struct lm_softc *);

void lm_setup_sensors(struct lm_softc *, const struct lm_sensor *);
void lm_refresh(void *);

void lm_refresh_sensor_data(struct lm_softc *);
void lm_refresh_volt(struct lm_softc *, int);
void lm_refresh_temp(struct lm_softc *, int);
void lm_refresh_fanrpm(struct lm_softc *, int);

void wb_refresh_sensor_data(struct lm_softc *);
void wb_w83637hf_refresh_vcore(struct lm_softc *, int);
void wb_refresh_nvolt(struct lm_softc *, int);
void wb_w83627ehf_refresh_nvolt(struct lm_softc *, int);
void wb_refresh_temp(struct lm_softc *, int);
void wb_refresh_fanrpm(struct lm_softc *, int);
void wb_nct6776f_refresh_fanrpm(struct lm_softc *, int);
void wb_w83792d_refresh_fanrpm(struct lm_softc *, int);

void as_refresh_temp(struct lm_softc *, int);

struct lm_chip {
	int (*chip_match)(struct lm_softc *);
};

const struct lm_chip lm_chips[] = {
	{ wb_match },
	{ lm_match },
	{ def_match } /* Must be last */
};

const struct lm_sensor lm78_sensors[] = {
	/* Voltage */
	{ "VCore A", SENSOR_VOLTS_DC, 0, 0x20, lm_refresh_volt, RFACT_NONE },
	{ "VCore B", SENSOR_VOLTS_DC, 0, 0x21, lm_refresh_volt, RFACT_NONE },
	{ "+3.3V", SENSOR_VOLTS_DC, 0, 0x22, lm_refresh_volt, RFACT_NONE },
	{ "+5V", SENSOR_VOLTS_DC, 0, 0x23, lm_refresh_volt, RFACT(68, 100) },
	{ "+12V", SENSOR_VOLTS_DC, 0, 0x24, lm_refresh_volt, RFACT(30, 10) },
	{ "-12V", SENSOR_VOLTS_DC, 0, 0x25, lm_refresh_volt, NRFACT(240, 60) },
	{ "-5V", SENSOR_VOLTS_DC, 0, 0x26, lm_refresh_volt, NRFACT(100, 60) },

	/* Temperature */
	{ "", SENSOR_TEMP, 0, 0x27, lm_refresh_temp },

	/* Fans */
	{ "", SENSOR_FANRPM, 0, 0x28, lm_refresh_fanrpm },
	{ "", SENSOR_FANRPM, 0, 0x29, lm_refresh_fanrpm },
	{ "", SENSOR_FANRPM, 0, 0x2a, lm_refresh_fanrpm },

	{ NULL }
};

const struct lm_sensor w83627hf_sensors[] = {
	/* Voltage */
	{ "VCore A", SENSOR_VOLTS_DC, 0, 0x20, lm_refresh_volt, RFACT_NONE },
	{ "VCore B", SENSOR_VOLTS_DC, 0, 0x21, lm_refresh_volt, RFACT_NONE },
	{ "+3.3V", SENSOR_VOLTS_DC, 0, 0x22, lm_refresh_volt, RFACT_NONE },
	{ "+5V", SENSOR_VOLTS_DC, 0, 0x23, lm_refresh_volt, RFACT(34, 50) },
	{ "+12V", SENSOR_VOLTS_DC, 0, 0x24, lm_refresh_volt, RFACT(28, 10) },
	{ "-12V", SENSOR_VOLTS_DC, 0, 0x25, wb_refresh_nvolt, RFACT(232, 56) },
	{ "-5V", SENSOR_VOLTS_DC, 0, 0x26, wb_refresh_nvolt, RFACT(120, 56) },
	{ "5VSB", SENSOR_VOLTS_DC, 5, 0x50, lm_refresh_volt, RFACT(17, 33) },
	{ "VBAT", SENSOR_VOLTS_DC, 5, 0x51, lm_refresh_volt, RFACT_NONE },

	/* Temperature */
	{ "", SENSOR_TEMP, 0, 0x27, lm_refresh_temp },
	{ "", SENSOR_TEMP, 1, 0x50, wb_refresh_temp },
	{ "", SENSOR_TEMP, 2, 0x50, wb_refresh_temp },

	/* Fans */
	{ "", SENSOR_FANRPM, 0, 0x28, wb_refresh_fanrpm },
	{ "", SENSOR_FANRPM, 0, 0x29, wb_refresh_fanrpm },
	{ "", SENSOR_FANRPM, 0, 0x2a, wb_refresh_fanrpm },

	{ NULL }
};

/*
 * The W83627EHF can measure voltages up to 2.048 V instead of the
 * traditional 4.096 V.  For measuring positive voltages, this can be
 * accounted for by halving the resistor factor.  Negative voltages
 * need special treatment, also because the reference voltage is 2.048 V
 * instead of the traditional 3.6 V.
 */
const struct lm_sensor w83627ehf_sensors[] = {
	/* Voltage */
	{ "VCore", SENSOR_VOLTS_DC, 0, 0x20, lm_refresh_volt, RFACT_NONE / 2},
	{ "+12V", SENSOR_VOLTS_DC, 0, 0x21, lm_refresh_volt, RFACT(56, 10) / 2 },
	{ "+3.3V", SENSOR_VOLTS_DC, 0, 0x22, lm_refresh_volt, RFACT(34, 34) / 2 },
	{ "+3.3V", SENSOR_VOLTS_DC, 0, 0x23, lm_refresh_volt, RFACT(34, 34) / 2 },
	{ "-12V", SENSOR_VOLTS_DC, 0, 0x24, wb_w83627ehf_refresh_nvolt },
	{ "", SENSOR_VOLTS_DC, 0, 0x25, lm_refresh_volt, RFACT_NONE / 2 },
	{ "", SENSOR_VOLTS_DC, 0, 0x26, lm_refresh_volt, RFACT_NONE / 2 },
	{ "3.3VSB", SENSOR_VOLTS_DC, 5, 0x50, lm_refresh_volt, RFACT(34, 34) / 2 },
	{ "VBAT", SENSOR_VOLTS_DC, 5, 0x51, lm_refresh_volt, RFACT_NONE / 2 },
	{ "", SENSOR_VOLTS_DC, 5, 0x52, lm_refresh_volt, RFACT_NONE / 2 },

	/* Temperature */
	{ "", SENSOR_TEMP, 0, 0x27, lm_refresh_temp },
	{ "", SENSOR_TEMP, 1, 0x50, wb_refresh_temp },
	{ "", SENSOR_TEMP, 2, 0x50, wb_refresh_temp },

	/* Fans */
	{ "", SENSOR_FANRPM, 0, 0x28, wb_refresh_fanrpm },
	{ "", SENSOR_FANRPM, 0, 0x29, wb_refresh_fanrpm },
	{ "", SENSOR_FANRPM, 0, 0x2a, wb_refresh_fanrpm },

	{ NULL }
};

/* 
 * w83627dhg is almost identical to w83627ehf, except that 
 * it has 9 instead of 10 voltage sensors
 */
const struct lm_sensor w83627dhg_sensors[] = {
	/* Voltage */
	{ "VCore", SENSOR_VOLTS_DC, 0, 0x20, lm_refresh_volt, RFACT_NONE / 2},
	{ "+12V", SENSOR_VOLTS_DC, 0, 0x21, lm_refresh_volt, RFACT(56, 10) / 2 },
	{ "+3.3V", SENSOR_VOLTS_DC, 0, 0x22, lm_refresh_volt, RFACT(34, 34) / 2 },
	{ "+3.3V", SENSOR_VOLTS_DC, 0, 0x23, lm_refresh_volt, RFACT(34, 34) / 2 },
	{ "-12V", SENSOR_VOLTS_DC, 0, 0x24, wb_w83627ehf_refresh_nvolt },
	{ "", SENSOR_VOLTS_DC, 0, 0x25, lm_refresh_volt, RFACT_NONE / 2 },
	{ "", SENSOR_VOLTS_DC, 0, 0x26, lm_refresh_volt, RFACT_NONE / 2 },
	{ "3.3VSB", SENSOR_VOLTS_DC, 5, 0x50, lm_refresh_volt, RFACT(34, 34) / 2 },
	{ "VBAT", SENSOR_VOLTS_DC, 5, 0x51, lm_refresh_volt, RFACT_NONE / 2 },

	/* Temperature */
	{ "", SENSOR_TEMP, 0, 0x27, lm_refresh_temp },
	{ "", SENSOR_TEMP, 1, 0x50, wb_refresh_temp },
	{ "", SENSOR_TEMP, 2, 0x50, wb_refresh_temp },

	/* Fans */
	{ "", SENSOR_FANRPM, 0, 0x28, wb_refresh_fanrpm },
	{ "", SENSOR_FANRPM, 0, 0x29, wb_refresh_fanrpm },
	{ "", SENSOR_FANRPM, 0, 0x2a, wb_refresh_fanrpm },

	{ NULL }
};

const struct lm_sensor nct6776f_sensors[] = {
	/* Voltage */
	{ "VCore", SENSOR_VOLTS_DC, 0, 0x20, lm_refresh_volt, RFACT_NONE / 2},
	{ "+12V", SENSOR_VOLTS_DC, 0, 0x21, lm_refresh_volt, RFACT(56, 10) / 2 },
	{ "+3.3V", SENSOR_VOLTS_DC, 0, 0x22, lm_refresh_volt, RFACT(34, 34) / 2 },
	{ "+3.3V", SENSOR_VOLTS_DC, 0, 0x23, lm_refresh_volt, RFACT(34, 34) / 2 },
	{ "-12V", SENSOR_VOLTS_DC, 0, 0x24, wb_w83627ehf_refresh_nvolt },
	{ "", SENSOR_VOLTS_DC, 0, 0x25, lm_refresh_volt, RFACT_NONE / 2 },
	{ "", SENSOR_VOLTS_DC, 0, 0x26, lm_refresh_volt, RFACT_NONE / 2 },
	{ "3.3VSB", SENSOR_VOLTS_DC, 5, 0x50, lm_refresh_volt, RFACT(34, 34) / 2 },
	{ "VBAT", SENSOR_VOLTS_DC, 5, 0x51, lm_refresh_volt, RFACT_NONE / 2 },

	/* Temperature */
	{ "", SENSOR_TEMP, 0, 0x27, lm_refresh_temp },
	{ "", SENSOR_TEMP, 1, 0x50, wb_refresh_temp },
	{ "", SENSOR_TEMP, 2, 0x50, wb_refresh_temp },

	/* Fans */
	{ "", SENSOR_FANRPM, 6, 0x56, wb_nct6776f_refresh_fanrpm },
	{ "", SENSOR_FANRPM, 6, 0x58, wb_nct6776f_refresh_fanrpm },
	{ "", SENSOR_FANRPM, 6, 0x5a, wb_nct6776f_refresh_fanrpm },
	{ "", SENSOR_FANRPM, 6, 0x5c, wb_nct6776f_refresh_fanrpm },
	{ "", SENSOR_FANRPM, 6, 0x5e, wb_nct6776f_refresh_fanrpm },

	{ NULL }
};

/* NCT6779D */
const struct lm_sensor nct6779d_sensors[] = {
	/* Voltage */
	{ "VCore", SENSOR_VOLTS_DC, 4, 0x80, lm_refresh_volt, RFACT_NONE },
	{ "VIN1", SENSOR_VOLTS_DC, 4, 0x81, lm_refresh_volt, RFACT(56, 10) / 2 },
	{ "AVCC", SENSOR_VOLTS_DC, 4, 0x82, lm_refresh_volt, RFACT(34, 34) / 2 },
	{ "+3.3V", SENSOR_VOLTS_DC, 4, 0x83, lm_refresh_volt, RFACT(34, 34) / 2 },
	{ "VIN0", SENSOR_VOLTS_DC, 4, 0x84, lm_refresh_volt, RFACT(48600, 10000) },
	{ "VIN8", SENSOR_VOLTS_DC, 4, 0x85, lm_refresh_volt, RFACT_NONE / 2 },
	{ "VIN4", SENSOR_VOLTS_DC, 4, 0x86, lm_refresh_volt, RFACT_NONE / 2 },
	{ "+3.3VSB", SENSOR_VOLTS_DC, 4, 0x87, lm_refresh_volt, RFACT(34, 34) / 2 },
	{ "VBAT", SENSOR_VOLTS_DC, 4, 0x88, lm_refresh_volt, RFACT_NONE },
	{ "VTT", SENSOR_VOLTS_DC, 4, 0x89, lm_refresh_volt, RFACT_NONE },
	{ "VIN5", SENSOR_VOLTS_DC, 4, 0x8a, lm_refresh_volt, RFACT_NONE / 2 },
	{ "VIN6", SENSOR_VOLTS_DC, 4, 0x8b, lm_refresh_volt, RFACT_NONE / 2 },
	{ "VIN2", SENSOR_VOLTS_DC, 4, 0x8c, lm_refresh_volt, RFACT_NONE },
	{ "VIN3", SENSOR_VOLTS_DC, 4, 0x8d, lm_refresh_volt, RFACT(14414, 10000) },
	{ "VIN7", SENSOR_VOLTS_DC, 4, 0x8e, lm_refresh_volt, RFACT_NONE / 2 },

	/* Temperature */
	{ "MB Temperature", SENSOR_TEMP, 4, 0x90, lm_refresh_temp, 0 },
	{ "CPU Temperature", SENSOR_TEMP, 4, 0x91, wb_refresh_temp, 0 },
	{ "Aux Temp0", SENSOR_TEMP, 4, 0x92, wb_refresh_temp, 0 },
	{ "Aux Temp1", SENSOR_TEMP, 4, 0x93, wb_refresh_temp, 0 },
	{ "Aux Temp2", SENSOR_TEMP, 4, 0x94, wb_refresh_temp, 0 },
	{ "Aux Temp3", SENSOR_TEMP, 4, 0x95, wb_refresh_temp, 0 },

	/* Fans */
	{ "System Fan", SENSOR_FANRPM, 4, 0xc0, wb_nct6776f_refresh_fanrpm, 0 },
	{ "CPU Fan", SENSOR_FANRPM, 4, 0xc2, wb_nct6776f_refresh_fanrpm, 0 },
	{ "Aux Fan0", SENSOR_FANRPM, 4, 0xc4, wb_nct6776f_refresh_fanrpm, 0 },
	{ "Aux Fan1", SENSOR_FANRPM, 4, 0xc6, wb_nct6776f_refresh_fanrpm, 0 },
	{ "Aux Fan2", SENSOR_FANRPM, 4, 0xc8, wb_nct6776f_refresh_fanrpm, 0 },

	{  NULL }
};

const struct lm_sensor w83637hf_sensors[] = {
	/* Voltage */
	{ "VCore", SENSOR_VOLTS_DC, 0, 0x20, wb_w83637hf_refresh_vcore },
	{ "+12V", SENSOR_VOLTS_DC, 0, 0x21, lm_refresh_volt, RFACT(28, 10) },
	{ "+3.3V", SENSOR_VOLTS_DC, 0, 0x22, lm_refresh_volt, RFACT_NONE },
	{ "+5V", SENSOR_VOLTS_DC, 0, 0x23, lm_refresh_volt, RFACT(34, 51) },
	{ "-12V", SENSOR_VOLTS_DC, 0, 0x24, wb_refresh_nvolt, RFACT(232, 56) },
	{ "5VSB", SENSOR_VOLTS_DC, 5, 0x50, lm_refresh_volt, RFACT(34, 51) },
	{ "VBAT", SENSOR_VOLTS_DC, 5, 0x51, lm_refresh_volt, RFACT_NONE },

	/* Temperature */
	{ "", SENSOR_TEMP, 0, 0x27, lm_refresh_temp },
	{ "", SENSOR_TEMP, 1, 0x50, wb_refresh_temp },
	{ "", SENSOR_TEMP, 2, 0x50, wb_refresh_temp },

	/* Fans */
	{ "", SENSOR_FANRPM, 0, 0x28, wb_refresh_fanrpm },
	{ "", SENSOR_FANRPM, 0, 0x29, wb_refresh_fanrpm },
	{ "", SENSOR_FANRPM, 0, 0x2a, wb_refresh_fanrpm },

	{ NULL }
};

const struct lm_sensor w83697hf_sensors[] = {
	/* Voltage */
	{ "VCore", SENSOR_VOLTS_DC, 0, 0x20, lm_refresh_volt, RFACT_NONE },
	{ "+3.3V", SENSOR_VOLTS_DC, 0, 0x22, lm_refresh_volt, RFACT_NONE },
	{ "+5V", SENSOR_VOLTS_DC, 0, 0x23, lm_refresh_volt, RFACT(34, 50) },
	{ "+12V", SENSOR_VOLTS_DC, 0, 0x24, lm_refresh_volt, RFACT(28, 10) },
	{ "-12V", SENSOR_VOLTS_DC, 0, 0x25, wb_refresh_nvolt, RFACT(232, 56) },
	{ "-5V", SENSOR_VOLTS_DC, 0, 0x26, wb_refresh_nvolt, RFACT(120, 56) },
	{ "5VSB", SENSOR_VOLTS_DC, 5, 0x50, lm_refresh_volt, RFACT(17, 33) },
	{ "VBAT", SENSOR_VOLTS_DC, 5, 0x51, lm_refresh_volt, RFACT_NONE },

	/* Temperature */
	{ "", SENSOR_TEMP, 0, 0x27, lm_refresh_temp },
	{ "", SENSOR_TEMP, 1, 0x50, wb_refresh_temp },

	/* Fans */
	{ "", SENSOR_FANRPM, 0, 0x28, wb_refresh_fanrpm },
	{ "", SENSOR_FANRPM, 0, 0x29, wb_refresh_fanrpm },

	{ NULL }
};

/*
 * The datasheet doesn't mention the (internal) resistors used for the
 * +5V, but using the values from the W83782D datasheets seems to
 * provide sensible results.
 */
const struct lm_sensor w83781d_sensors[] = {
	/* Voltage */
	{ "VCore A", SENSOR_VOLTS_DC, 0, 0x20, lm_refresh_volt, RFACT_NONE },
	{ "VCore B", SENSOR_VOLTS_DC, 0, 0x21, lm_refresh_volt, RFACT_NONE },
	{ "+3.3V", SENSOR_VOLTS_DC, 0, 0x22, lm_refresh_volt, RFACT_NONE },
	{ "+5V", SENSOR_VOLTS_DC, 0, 0x23, lm_refresh_volt, RFACT(34, 50) },
	{ "+12V", SENSOR_VOLTS_DC, 0, 0x24, lm_refresh_volt, RFACT(28, 10) },
	{ "-12V", SENSOR_VOLTS_DC, 0, 0x25, lm_refresh_volt, NRFACT(2100, 604) },
	{ "-5V", SENSOR_VOLTS_DC, 0, 0x26, lm_refresh_volt, NRFACT(909, 604) },

	/* Temperature */
	{ "", SENSOR_TEMP, 0, 0x27, lm_refresh_temp },
	{ "", SENSOR_TEMP, 1, 0x50, wb_refresh_temp },
	{ "", SENSOR_TEMP, 2, 0x50, wb_refresh_temp },

	/* Fans */
	{ "", SENSOR_FANRPM, 0, 0x28, lm_refresh_fanrpm },
	{ "", SENSOR_FANRPM, 0, 0x29, lm_refresh_fanrpm },
	{ "", SENSOR_FANRPM, 0, 0x2a, lm_refresh_fanrpm },

	{ NULL }
};

const struct lm_sensor w83782d_sensors[] = {
	/* Voltage */
	{ "VCore", SENSOR_VOLTS_DC, 0, 0x20, lm_refresh_volt, RFACT_NONE },
	{ "VINR0", SENSOR_VOLTS_DC, 0, 0x21, lm_refresh_volt, RFACT_NONE },
	{ "+3.3V", SENSOR_VOLTS_DC, 0, 0x22, lm_refresh_volt, RFACT_NONE },
	{ "+5V", SENSOR_VOLTS_DC, 0, 0x23, lm_refresh_volt, RFACT(34, 50) },
	{ "+12V", SENSOR_VOLTS_DC, 0, 0x24, lm_refresh_volt, RFACT(28, 10) },
	{ "-12V", SENSOR_VOLTS_DC, 0, 0x25, wb_refresh_nvolt, RFACT(232, 56) },
	{ "-5V", SENSOR_VOLTS_DC, 0, 0x26, wb_refresh_nvolt, RFACT(120, 56) },
	{ "5VSB", SENSOR_VOLTS_DC, 5, 0x50, lm_refresh_volt, RFACT(17, 33) },
	{ "VBAT", SENSOR_VOLTS_DC, 5, 0x51, lm_refresh_volt, RFACT_NONE },

	/* Temperature */
	{ "", SENSOR_TEMP, 0, 0x27, lm_refresh_temp },
	{ "", SENSOR_TEMP, 1, 0x50, wb_refresh_temp },
	{ "", SENSOR_TEMP, 2, 0x50, wb_refresh_temp },

	/* Fans */
	{ "", SENSOR_FANRPM, 0, 0x28, wb_refresh_fanrpm },
	{ "", SENSOR_FANRPM, 0, 0x29, wb_refresh_fanrpm },
	{ "", SENSOR_FANRPM, 0, 0x2a, wb_refresh_fanrpm },

	{ NULL }
};

const struct lm_sensor w83783s_sensors[] = {
	/* Voltage */
	{ "VCore", SENSOR_VOLTS_DC, 0, 0x20, lm_refresh_volt, RFACT_NONE },
	{ "+3.3V", SENSOR_VOLTS_DC, 0, 0x22, lm_refresh_volt, RFACT_NONE },
	{ "+5V", SENSOR_VOLTS_DC, 0, 0x23, lm_refresh_volt, RFACT(34, 50) },
	{ "+12V", SENSOR_VOLTS_DC, 0, 0x24, lm_refresh_volt, RFACT(28, 10) },
	{ "-12V", SENSOR_VOLTS_DC, 0, 0x25, wb_refresh_nvolt, RFACT(232, 56) },
	{ "-5V", SENSOR_VOLTS_DC, 0, 0x26, wb_refresh_nvolt, RFACT(120, 56) },

	/* Temperature */
	{ "", SENSOR_TEMP, 0, 0x27, lm_refresh_temp },
	{ "", SENSOR_TEMP, 1, 0x50, wb_refresh_temp },

	/* Fans */
	{ "", SENSOR_FANRPM, 0, 0x28, wb_refresh_fanrpm },
	{ "", SENSOR_FANRPM, 0, 0x29, wb_refresh_fanrpm },
	{ "", SENSOR_FANRPM, 0, 0x2a, wb_refresh_fanrpm },

	{ NULL }
};

const struct lm_sensor w83791d_sensors[] = {
	/* Voltage */
	{ "VCore", SENSOR_VOLTS_DC, 0, 0x20, lm_refresh_volt, 10000 },
	{ "VINR0", SENSOR_VOLTS_DC, 0, 0x21, lm_refresh_volt, 10000 },
	{ "+3.3V", SENSOR_VOLTS_DC, 0, 0x22, lm_refresh_volt, 10000 },
	{ "+5V", SENSOR_VOLTS_DC, 0, 0x23, lm_refresh_volt, RFACT(34, 50) },
	{ "+12V", SENSOR_VOLTS_DC, 0, 0x24, lm_refresh_volt, RFACT(28, 10) },
	{ "-12V", SENSOR_VOLTS_DC, 0, 0x25, wb_refresh_nvolt, RFACT(232, 56) },
	{ "-5V", SENSOR_VOLTS_DC, 0, 0x26, wb_refresh_nvolt, RFACT(120, 56) },
	{ "5VSB", SENSOR_VOLTS_DC, 0, 0xb0, lm_refresh_volt, RFACT(17, 33) },
	{ "VBAT", SENSOR_VOLTS_DC, 0, 0xb1, lm_refresh_volt, RFACT_NONE },
	{ "VINR1", SENSOR_VOLTS_DC, 0, 0xb2, lm_refresh_volt, RFACT_NONE },

	/* Temperature */
	{ "", SENSOR_TEMP, 0, 0x27, lm_refresh_temp },
	{ "", SENSOR_TEMP, 0, 0xc0, wb_refresh_temp },
	{ "", SENSOR_TEMP, 0, 0xc8, wb_refresh_temp },

	/* Fans */
	{ "", SENSOR_FANRPM, 0, 0x28, wb_refresh_fanrpm },
	{ "", SENSOR_FANRPM, 0, 0x29, wb_refresh_fanrpm },
	{ "", SENSOR_FANRPM, 0, 0x2a, wb_refresh_fanrpm },
	{ "", SENSOR_FANRPM, 0, 0xba, wb_refresh_fanrpm },
	{ "", SENSOR_FANRPM, 0, 0xbb, wb_refresh_fanrpm },

	{ NULL }
};

const struct lm_sensor w83792d_sensors[] = {
	/* Voltage */
	{ "VCore A", SENSOR_VOLTS_DC, 0, 0x20, lm_refresh_volt, RFACT_NONE },
	{ "VCore B", SENSOR_VOLTS_DC, 0, 0x21, lm_refresh_volt, RFACT_NONE },
	{ "+3.3V", SENSOR_VOLTS_DC, 0, 0x22, lm_refresh_volt, RFACT_NONE },
	{ "-5V", SENSOR_VOLTS_DC, 0, 0x23, wb_refresh_nvolt, RFACT(120, 56) },
	{ "+12V", SENSOR_VOLTS_DC, 0, 0x24, lm_refresh_volt, RFACT(28, 10) },
	{ "-12V", SENSOR_VOLTS_DC, 0, 0x25, wb_refresh_nvolt, RFACT(232, 56) },
	{ "+5V", SENSOR_VOLTS_DC, 0, 0x26, lm_refresh_volt, RFACT(34, 50) },
	{ "5VSB", SENSOR_VOLTS_DC, 0, 0xb0, lm_refresh_volt, RFACT(17, 33) },
	{ "VBAT", SENSOR_VOLTS_DC, 0, 0xb1, lm_refresh_volt, RFACT_NONE },

	/* Temperature */
	{ "", SENSOR_TEMP, 0, 0x27, lm_refresh_temp },
	{ "", SENSOR_TEMP, 0, 0xc0, wb_refresh_temp },
	{ "", SENSOR_TEMP, 0, 0xc8, wb_refresh_temp },

	/* Fans */
	{ "", SENSOR_FANRPM, 0, 0x28, wb_w83792d_refresh_fanrpm },
	{ "", SENSOR_FANRPM, 0, 0x29, wb_w83792d_refresh_fanrpm },
	{ "", SENSOR_FANRPM, 0, 0x2a, wb_w83792d_refresh_fanrpm },
	{ "", SENSOR_FANRPM, 0, 0xb8, wb_w83792d_refresh_fanrpm },
	{ "", SENSOR_FANRPM, 0, 0xb9, wb_w83792d_refresh_fanrpm },
	{ "", SENSOR_FANRPM, 0, 0xba, wb_w83792d_refresh_fanrpm },
	{ "", SENSOR_FANRPM, 0, 0xbe, wb_w83792d_refresh_fanrpm },

	{ NULL }
};

const struct lm_sensor as99127f_sensors[] = {
	/* Voltage */
	{ "VCore A", SENSOR_VOLTS_DC, 0, 0x20, lm_refresh_volt, RFACT_NONE },
	{ "VCore B", SENSOR_VOLTS_DC, 0, 0x21, lm_refresh_volt, RFACT_NONE },
	{ "+3.3V", SENSOR_VOLTS_DC, 0, 0x22, lm_refresh_volt, RFACT_NONE },
	{ "+5V", SENSOR_VOLTS_DC, 0, 0x23, lm_refresh_volt, RFACT(34, 50) },
	{ "+12V", SENSOR_VOLTS_DC, 0, 0x24, lm_refresh_volt, RFACT(28, 10) },
	{ "-12V", SENSOR_VOLTS_DC, 0, 0x25, wb_refresh_nvolt, RFACT(232, 56) },
	{ "-5V", SENSOR_VOLTS_DC, 0, 0x26, wb_refresh_nvolt, RFACT(120, 56) },

	/* Temperature */
	{ "", SENSOR_TEMP, 0, 0x27, lm_refresh_temp },
	{ "", SENSOR_TEMP, 1, 0x50, as_refresh_temp },
	{ "", SENSOR_TEMP, 2, 0x50, as_refresh_temp },

	/* Fans */
	{ "", SENSOR_FANRPM, 0, 0x28, lm_refresh_fanrpm },
	{ "", SENSOR_FANRPM, 0, 0x29, lm_refresh_fanrpm },
	{ "", SENSOR_FANRPM, 0, 0x2a, lm_refresh_fanrpm },

	{ NULL }
};

void
lm_attach(struct lm_softc *sc)
{
	u_int i, config;

	for (i = 0; i < sizeof(lm_chips) / sizeof(lm_chips[0]); i++)
		if (lm_chips[i].chip_match(sc))
			break;

	/* No point in doing anything if we don't have any sensors. */
	if (sc->numsensors == 0)
		return;

	sc->sensortask = sensor_task_register(sc, lm_refresh, 5);
	if (sc->sensortask == NULL) {
		printf("%s: unable to register update task\n",
		    sc->sc_dev.dv_xname);
		return;
	}

	/* Start the monitoring loop */
	config = sc->lm_readreg(sc, LM_CONFIG);
	sc->lm_writereg(sc, LM_CONFIG, config | 0x01);

	/* Add sensors */
	for (i = 0; i < sc->numsensors; ++i)
		sensor_attach(&sc->sensordev, &sc->sensors[i]);
	sensordev_install(&sc->sensordev);
}

int
lm_match(struct lm_softc *sc)
{
	int chipid;

	/* See if we have an LM78 or LM79. */
	chipid = sc->lm_readreg(sc, LM_CHIPID) & LM_CHIPID_MASK;
	switch(chipid) {
	case LM_CHIPID_LM78:
		printf(": LM78\n");
		break;
	case LM_CHIPID_LM78J:
		printf(": LM78J\n");
		break;
	case LM_CHIPID_LM79:
		printf(": LM79\n");
		break;
	case LM_CHIPID_LM81:
		printf(": LM81\n");
		break;
	default:
		return 0;
	}

	lm_setup_sensors(sc, lm78_sensors);
	sc->refresh_sensor_data = lm_refresh_sensor_data;
	return 1;
}

int
def_match(struct lm_softc *sc)
{
	int chipid;

	chipid = sc->lm_readreg(sc, LM_CHIPID) & LM_CHIPID_MASK;
	printf(": unknown chip (ID %d)\n", chipid);

	lm_setup_sensors(sc, lm78_sensors);
	sc->refresh_sensor_data = lm_refresh_sensor_data;
	return 1;
}

int
wb_match(struct lm_softc *sc)
{
	int banksel, vendid, devid;

	/* Read vendor ID */
	banksel = sc->lm_readreg(sc, WB_BANKSEL);
	sc->lm_writereg(sc, WB_BANKSEL, WB_BANKSEL_HBAC);
	vendid = sc->lm_readreg(sc, WB_VENDID) << 8;
	sc->lm_writereg(sc, WB_BANKSEL, 0);
	vendid |= sc->lm_readreg(sc, WB_VENDID);
	sc->lm_writereg(sc, WB_BANKSEL, banksel);
	DPRINTF((" winbond vend id 0x%x\n", vendid));
	if (vendid != WB_VENDID_WINBOND && vendid != WB_VENDID_ASUS)
		return 0;

	/* Read device/chip ID */
	sc->lm_writereg(sc, WB_BANKSEL, WB_BANKSEL_B0);
	devid = sc->lm_readreg(sc, LM_CHIPID);
	sc->chipid = sc->lm_readreg(sc, WB_BANK0_CHIPID);
	sc->lm_writereg(sc, WB_BANKSEL, banksel);
	DPRINTF((" winbond chip id 0x%x\n", sc->chipid));
	switch(sc->chipid) {
	case WB_CHIPID_W83627HF:
		printf(": W83627HF\n");
		lm_setup_sensors(sc, w83627hf_sensors);
		break;
	case WB_CHIPID_W83627THF:
		printf(": W83627THF\n");
		lm_setup_sensors(sc, w83637hf_sensors);
		break;
	case WB_CHIPID_W83627EHF_A:
		printf(": W83627EHF-A\n");
		lm_setup_sensors(sc, w83627ehf_sensors);
		break;
	case WB_CHIPID_W83627EHF:
		printf(": W83627EHF\n");
		lm_setup_sensors(sc, w83627ehf_sensors);
		break;
	case WB_CHIPID_W83627DHG:
		switch (sc->sioid) {
		case WBSIO_ID_NCT6775F:
			printf(": NCT6775F\n");
			lm_setup_sensors(sc, nct6776f_sensors);
			break;
		case WBSIO_ID_NCT6776F:
			printf(": NCT6776F\n");
			lm_setup_sensors(sc, nct6776f_sensors);
			break;
		case WBSIO_ID_NCT5104D:
			printf(": NCT5104D\n");
			lm_setup_sensors(sc, nct6776f_sensors);
			break;
		case WBSIO_ID_NCT6779D:
			printf(": NCT6779D\n");
			lm_setup_sensors(sc, nct6779d_sensors);
			break;
		case WBSIO_ID_NCT6791D:
			printf(": NCT6791D\n");
			lm_setup_sensors(sc, nct6779d_sensors);
			break;
		case WBSIO_ID_NCT6792D:
			printf(": NCT6792D\n");
			lm_setup_sensors(sc, nct6779d_sensors);
			break;
		case WBSIO_ID_NCT6793D:
			printf(": NCT6793D\n");
			lm_setup_sensors(sc, nct6779d_sensors);
			break;
		case WBSIO_ID_NCT6795D:
			printf(": NCT6795D\n");
			lm_setup_sensors(sc, nct6779d_sensors);
			break;
		default:
			printf(": W83627DHG\n");
			lm_setup_sensors(sc, w83627dhg_sensors);
		}
		break;
	case WB_CHIPID_W83637HF:
		printf(": W83637HF\n");
		sc->lm_writereg(sc, WB_BANKSEL, WB_BANKSEL_B0);
		if (sc->lm_readreg(sc, WB_BANK0_CONFIG) & WB_CONFIG_VMR9)
			sc->vrm9 = 1;
		sc->lm_writereg(sc, WB_BANKSEL, banksel);
		lm_setup_sensors(sc, w83637hf_sensors);
		break;
	case WB_CHIPID_W83697HF:
		printf(": W83697HF\n");
		lm_setup_sensors(sc, w83697hf_sensors);
		break;
	case WB_CHIPID_W83781D:
	case WB_CHIPID_W83781D_2:
		printf(": W83781D\n");
		lm_setup_sensors(sc, w83781d_sensors);
		break;
	case WB_CHIPID_W83782D:
		printf(": W83782D\n");
		lm_setup_sensors(sc, w83782d_sensors);
		break;
	case WB_CHIPID_W83783S:
		printf(": W83783S\n");
		lm_setup_sensors(sc, w83783s_sensors);
		break;
	case WB_CHIPID_W83791D:
		printf(": W83791D\n");
		lm_setup_sensors(sc, w83791d_sensors);
		break;
	case WB_CHIPID_W83791SD:
		printf(": W83791SD\n");
		break;
	case WB_CHIPID_W83792D:
		if (devid >= 0x10 && devid <= 0x29)
			printf(": W83792D rev %c\n", 'A' + devid - 0x10);
		else
			printf(": W83792D rev 0x%x\n", devid);
		lm_setup_sensors(sc, w83792d_sensors);
		break;
	case WB_CHIPID_AS99127F:
		if (vendid == WB_VENDID_ASUS) {
			printf(": AS99127F\n");
			lm_setup_sensors(sc, w83781d_sensors);
		} else {
			printf(": AS99127F rev 2\n");
			lm_setup_sensors(sc, as99127f_sensors);
		}
		break;
	default:
		printf(": unknown Winbond chip (ID 0x%x)\n", sc->chipid);
		/* Handle as a standard LM78. */
		lm_setup_sensors(sc, lm78_sensors);
		sc->refresh_sensor_data = lm_refresh_sensor_data;
		return 1;
	}

	sc->refresh_sensor_data = wb_refresh_sensor_data;
	return 1;
}

void
lm_setup_sensors(struct lm_softc *sc, const struct lm_sensor *sensors)
{
	int i;

	strlcpy(sc->sensordev.xname, sc->sc_dev.dv_xname,
	    sizeof(sc->sensordev.xname));

	for (i = 0; sensors[i].desc; i++) {
		sc->sensors[i].type = sensors[i].type;
		strlcpy(sc->sensors[i].desc, sensors[i].desc,
		    sizeof(sc->sensors[i].desc));
		sc->numsensors++;
	}
	sc->lm_sensors = sensors;
}

void
lm_refresh(void *arg)
{
	struct lm_softc *sc = arg;

	sc->refresh_sensor_data(sc);
}

void
lm_refresh_sensor_data(struct lm_softc *sc)
{
	int i;

	for (i = 0; i < sc->numsensors; i++)
		sc->lm_sensors[i].refresh(sc, i);
}

void
lm_refresh_volt(struct lm_softc *sc, int n)
{
	struct ksensor *sensor = &sc->sensors[n];
	int data;

	data = sc->lm_readreg(sc, sc->lm_sensors[n].reg);
	sensor->value = (data << 4);
	sensor->value *= sc->lm_sensors[n].rfact;
	sensor->value /= 10;
}

void
lm_refresh_temp(struct lm_softc *sc, int n)
{
	struct ksensor *sensor = &sc->sensors[n];
	int sdata;

	/*
	 * The data sheet suggests that the range of the temperature
	 * sensor is between -55 degC and +125 degC.
	 */
	sdata = sc->lm_readreg(sc, sc->lm_sensors[n].reg);
	if (sdata > 0x7d && sdata < 0xc9) {
		sensor->flags |= SENSOR_FINVALID;
		sensor->value = 0;
	} else {
		if (sdata & 0x80)
			sdata -= 0x100;
		sensor->flags &= ~SENSOR_FINVALID;
		sensor->value = sdata * 1000000 + 273150000;
	}
}

void
lm_refresh_fanrpm(struct lm_softc *sc, int n)
{
	struct ksensor *sensor = &sc->sensors[n];
	int data, divisor = 1;

	/*
	 * We might get more accurate fan readings by adjusting the
	 * divisor, but that might interfere with APM or other SMM
	 * BIOS code reading the fan speeds.
	 */

	/* FAN3 has a fixed fan divisor. */
	if (sc->lm_sensors[n].reg == LM_FAN1 ||
	    sc->lm_sensors[n].reg == LM_FAN2) {
		data = sc->lm_readreg(sc, LM_VIDFAN);
		if (sc->lm_sensors[n].reg == LM_FAN1)
			divisor = (data >> 4) & 0x03;
		else
			divisor = (data >> 6) & 0x03;
	}

	data = sc->lm_readreg(sc, sc->lm_sensors[n].reg);
	if (data == 0xff || data == 0x00) {
		sensor->flags |= SENSOR_FINVALID;
		sensor->value = 0;
	} else {
		sensor->flags &= ~SENSOR_FINVALID;
		sensor->value = 1350000 / (data << divisor);
	}
}

void
wb_refresh_sensor_data(struct lm_softc *sc)
{
	int banksel, bank, i;

	/*
	 * Properly save and restore bank selection register.
	 */

	banksel = bank = sc->lm_readreg(sc, WB_BANKSEL);
	for (i = 0; i < sc->numsensors; i++) {
		if (bank != sc->lm_sensors[i].bank) {
			bank = sc->lm_sensors[i].bank;
			sc->lm_writereg(sc, WB_BANKSEL, bank);
		}
		sc->lm_sensors[i].refresh(sc, i);
	}
	sc->lm_writereg(sc, WB_BANKSEL, banksel);
}

void
wb_w83637hf_refresh_vcore(struct lm_softc *sc, int n)
{
	struct ksensor *sensor = &sc->sensors[n];
	int data;

	data = sc->lm_readreg(sc, sc->lm_sensors[n].reg);

	/*
	 * Depending on the voltage detection method,
	 * one of the following formulas is used:
	 *	VRM8 method: value = raw * 0.016V
	 *	VRM9 method: value = raw * 0.00488V + 0.70V
	 */
	if (sc->vrm9)
		sensor->value = (data * 4880) + 700000;
	else
		sensor->value = (data * 16000);
}

void
wb_refresh_nvolt(struct lm_softc *sc, int n)
{
	struct ksensor *sensor = &sc->sensors[n];
	int data;

	data = sc->lm_readreg(sc, sc->lm_sensors[n].reg);
	sensor->value = ((data << 4) - WB_VREF);
	sensor->value *= sc->lm_sensors[n].rfact;
	sensor->value /= 10;
	sensor->value += WB_VREF * 1000;
}

void
wb_w83627ehf_refresh_nvolt(struct lm_softc *sc, int n)
{
	struct ksensor *sensor = &sc->sensors[n];
	int data;

	data = sc->lm_readreg(sc, sc->lm_sensors[n].reg);
	sensor->value = ((data << 3) - WB_W83627EHF_VREF);
	sensor->value *= RFACT(232, 10);
	sensor->value /= 10;
	sensor->value += WB_W83627EHF_VREF * 1000;
}

void
wb_refresh_temp(struct lm_softc *sc, int n)
{
	struct ksensor *sensor = &sc->sensors[n];
	int sdata;

	/*
	 * The data sheet suggests that the range of the temperature
	 * sensor is between -55 degC and +125 degC.  However, values
	 * around -48 degC seem to be a very common bogus values.
	 * Since such values are unreasonably low, we use -45 degC for
	 * the lower limit instead.
	 */
	sdata = sc->lm_readreg(sc, sc->lm_sensors[n].reg) << 1;
	sdata += sc->lm_readreg(sc, sc->lm_sensors[n].reg + 1) >> 7;
	if (sdata > 0x0fa && sdata < 0x1a6) {
		sensor->flags |= SENSOR_FINVALID;
		sensor->value = 0;
	} else {
		if (sdata & 0x100)
			sdata -= 0x200;
		sensor->flags &= ~SENSOR_FINVALID;
		sensor->value = sdata * 500000 + 273150000;
	}
}

void
wb_refresh_fanrpm(struct lm_softc *sc, int n)
{
	struct ksensor *sensor = &sc->sensors[n];
	int fan, data, divisor = 0;

	/* 
	 * This is madness; the fan divisor bits are scattered all
	 * over the place.
	 */

	if (sc->lm_sensors[n].reg == LM_FAN1 ||
	    sc->lm_sensors[n].reg == LM_FAN2 ||
	    sc->lm_sensors[n].reg == LM_FAN3) {
		data = sc->lm_readreg(sc, WB_BANK0_VBAT);
		fan = (sc->lm_sensors[n].reg - LM_FAN1);
		if ((data >> 5) & (1 << fan))
			divisor |= 0x04;
	}

	if (sc->lm_sensors[n].reg == LM_FAN1 ||
	    sc->lm_sensors[n].reg == LM_FAN2) {
		data = sc->lm_readreg(sc, LM_VIDFAN);
		if (sc->lm_sensors[n].reg == LM_FAN1)
			divisor |= (data >> 4) & 0x03;
		else
			divisor |= (data >> 6) & 0x03;
	} else if (sc->lm_sensors[n].reg == LM_FAN3) {
		data = sc->lm_readreg(sc, WB_PIN);
		divisor |= (data >> 6) & 0x03;
	} else if (sc->lm_sensors[n].reg == WB_BANK0_FAN4 ||
		   sc->lm_sensors[n].reg == WB_BANK0_FAN5) {
		data = sc->lm_readreg(sc, WB_BANK0_FAN45);
		if (sc->lm_sensors[n].reg == WB_BANK0_FAN4)
			divisor |= (data >> 0) & 0x07;
		else
			divisor |= (data >> 4) & 0x07;
	}

	data = sc->lm_readreg(sc, sc->lm_sensors[n].reg);
	if (data == 0xff || data == 0x00) {
		sensor->flags |= SENSOR_FINVALID;
		sensor->value = 0;
	} else {
		sensor->flags &= ~SENSOR_FINVALID;
		sensor->value = 1350000 / (data << divisor);
	}
}

void
wb_nct6776f_refresh_fanrpm(struct lm_softc *sc, int n)
{
	struct ksensor *sensor = &sc->sensors[n];
	int datah, datal;

	datah = sc->lm_readreg(sc, sc->lm_sensors[n].reg);
	datal = sc->lm_readreg(sc, sc->lm_sensors[n].reg + 1);

	if (datah == 0xff) {
		sensor->flags |= SENSOR_FINVALID;
		sensor->value = 0;
	} else {
		sensor->flags &= ~SENSOR_FINVALID;
		sensor->value = (datah << 8) | datal;
	}
}

void
wb_w83792d_refresh_fanrpm(struct lm_softc *sc, int n)
{
	struct ksensor *sensor = &sc->sensors[n];
	int reg, shift, data, divisor = 1;

	switch (sc->lm_sensors[n].reg) {
	case 0x28:
		reg = 0x47; shift = 0;
		break;
	case 0x29:
		reg = 0x47; shift = 4;
		break;
	case 0x2a:
		reg = 0x5b; shift = 0;
		break;
	case 0xb8:
		reg = 0x5b; shift = 4;
		break;
	case 0xb9:
		reg = 0x5c; shift = 0;
		break;
	case 0xba:
		reg = 0x5c; shift = 4;
		break;
	case 0xbe:
		reg = 0x9e; shift = 0;
		break;
	default:
		reg = 0;
		break;
	}

	data = sc->lm_readreg(sc, sc->lm_sensors[n].reg);
	if (data == 0xff || data == 0x00) {
		sensor->flags |= SENSOR_FINVALID;
		sensor->value = 0;
	} else {
		if (reg != 0)
			divisor = (sc->lm_readreg(sc, reg) >> shift) & 0x7;
		sensor->flags &= ~SENSOR_FINVALID;
		sensor->value = 1350000 / (data << divisor);
	}
}

void
as_refresh_temp(struct lm_softc *sc, int n)
{
	struct ksensor *sensor = &sc->sensors[n];
	int sdata;

	/*
	 * It seems a shorted temperature diode produces an all-ones
	 * bit pattern.
	 */
	sdata = sc->lm_readreg(sc, sc->lm_sensors[n].reg) << 1;
	sdata += sc->lm_readreg(sc, sc->lm_sensors[n].reg + 1) >> 7;
	if (sdata == 0x1ff) {
		sensor->flags |= SENSOR_FINVALID;
		sensor->value = 0;
	} else {
		if (sdata & 0x100)
			sdata -= 0x200;
		sensor->flags &= ~SENSOR_FINVALID;
		sensor->value = sdata * 500000 + 273150000;
	}
}
