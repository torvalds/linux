/*	$OpenBSD: fins.c,v 1.6 2022/04/08 15:02:28 naddy Exp $	*/

/*
 * Copyright (c) 2005, 2006 Mark Kettenis
 * Copyright (c) 2007, 2008 Geoff Steckel
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
#include <machine/bus.h>

#include <dev/isa/isareg.h>
#include <dev/isa/isavar.h>

/* Derived from LM78 code.  Only handles chips attached to ISA bus */

/*
 * Fintek F71805 registers and constants
 * http://www.fintek.com.tw/files/productfiles/F71805F_V025.pdf
 * This chip is a multi-io chip with many functions.
 * Each function may be relocated in I/O space by the BIOS.
 * The base address (2E or 4E) accesses a configuration space which
 * has pointers to the individual functions. The config space must be
 * unlocked with a cookie and relocked afterwards. The chip ID is stored
 * in config space so it is not normally visible.
 *
 * We assume that the monitor is enabled. We don't try to start or stop it.
 * The voltage dividers specified are from reading the chips on one board.
 * There is no way to determine what they are in the general case.
 * This section of the chip controls the fans. We don't do anything to them.
 */

#define FINS_UNLOCK	0x87	/* magic constant - write 2x to select chip */
#define FINS_LOCK	0xaa	/* magic constant - write 1x to deselect reg */

/* ISA registers index to an internal register space on chip */
#define FINS_ADDR	0x00
#define FINS_DATA	0x01

#define FINS_FUNC_SEL	0x07	/* select which chip function to access */
#define FINS_CHIP	0x20	/* chip ID */
#define FINS_MANUF	0x23	/* manufacturer ID */
#define FINS_BASEADDR	0x60	/* I/O base of chip function */

#define FINS_71806	0x0341	/* same as F71872 */
#define FINS_71805	0x0406
#define FINS_71882	0x0541	/* same as F71883 */
#define FINS_71862	0x0601	/* same as F71863 */
#define FINTEK_ID	0x1934

#define FINS_FUNC_SENSORS	0x04
#define FINS_FUNC_WATCHDOG	0x07

/* sensors device registers */
#define FINS_SENS_TMODE(sc)	((sc)->fins_chipid <= FINS_71805 ? 0x01 : 0x6b)
#define FINS_SENS_VDIVS		0x0e

/* watchdog device registers (mapped straight to i/o port offsets) */
#define FINS_WDOG_CR0	0x00
#define FINS_WDOG_CR1	0x05
#define FINS_WDOG_TIMER	0x06

/* CR0 flags */
#define FINS_WDOG_OUTEN	0x80

/* CR1 flags */
#define FINS_WDOG_EN	0x20
#define FINS_WDOG_MINS	0x08

#define FINS_MAX_SENSORS 18
/*
 * Fintek chips typically measure voltages using 8mv steps.
 * To measure higher voltages the input is attenuated with (external)
 * resistors.  Negative voltages are measured using inverting op amps
 * and resistors.  So we have to convert the sensor values back to
 * real voltages by applying the appropriate resistor factor.
 */
#define FRFACT_NONE	8000
#define FRFACT(x, y)	(FRFACT_NONE * ((x) + (y)) / (y))
#define FNRFACT(x, y)	(-FRFACT_NONE * (x) / (y))

struct fins_softc;

struct fins_sensor {
	char *fs_desc;
	void (*fs_refresh)(struct fins_softc *, int);
	enum sensor_type fs_type;
	int fs_aux;
	u_int8_t fs_reg;
};

struct fins_softc {
	struct device sc_dev;

	struct ksensor fins_ksensors[FINS_MAX_SENSORS];
	struct ksensordev fins_sensordev;
	struct sensor_task *fins_sensortask;
	const struct fins_sensor *fins_sensors;

	bus_space_handle_t sc_ioh_sens;
	bus_space_handle_t sc_ioh_wdog;
	bus_space_tag_t sc_iot;

	u_int16_t fins_chipid;
	u_int8_t fins_tempsel;
	u_int8_t fins_wdog_cr;
};

int  fins_match(struct device *, void *, void *);
void fins_attach(struct device *, struct device *, void *);
int  fins_activate(struct device *, int);

void fins_unlock(bus_space_tag_t, bus_space_handle_t);
void fins_lock(bus_space_tag_t, bus_space_handle_t);

u_int8_t fins_read(bus_space_tag_t, bus_space_handle_t, int);
u_int16_t fins_read_2(bus_space_tag_t, bus_space_handle_t, int);
void fins_write(bus_space_tag_t, bus_space_handle_t, int, u_int8_t);

static __inline u_int8_t fins_read_sens(struct fins_softc *, int);
static __inline u_int16_t fins_read_sens_2(struct fins_softc *, int);

static __inline u_int8_t fins_read_wdog(struct fins_softc *, int);
static __inline void fins_write_wdog(struct fins_softc *, int, u_int8_t);

void fins_setup_sensors(struct fins_softc *, const struct fins_sensor *);
void fins_refresh(void *);

void fins_get_rpm(struct fins_softc *, int);
void fins_get_temp(struct fins_softc *, int);
void fins_get_volt(struct fins_softc *, int);

int fins_wdog_cb(void *, int);

const struct cfattach fins_ca = {
	sizeof(struct fins_softc),
	fins_match,
	fins_attach,
	NULL,
	fins_activate
};

struct cfdriver fins_cd = {
	NULL, "fins", DV_DULL
};

const struct fins_sensor fins_71805_sensors[] = {
	{ "+3.3V",  fins_get_volt, SENSOR_VOLTS_DC, FRFACT(100, 100),	0x10 },
	{ "Vtt",    fins_get_volt, SENSOR_VOLTS_DC, FRFACT_NONE,	0x11 },
	{ "Vram",   fins_get_volt, SENSOR_VOLTS_DC, FRFACT(100, 100),	0x12 },
	{ "Vchips", fins_get_volt, SENSOR_VOLTS_DC, FRFACT(47, 100),	0x13 },
	{ "+5V",    fins_get_volt, SENSOR_VOLTS_DC, FRFACT(200, 47),	0x14 },
	{ "+12V",   fins_get_volt, SENSOR_VOLTS_DC, FRFACT(200, 20),	0x15 },
	{ "+1.5V",  fins_get_volt, SENSOR_VOLTS_DC, FRFACT_NONE,	0x16 },
	{ "Vcore",  fins_get_volt, SENSOR_VOLTS_DC, FRFACT_NONE,	0x17 },
	{ "Vsb",    fins_get_volt, SENSOR_VOLTS_DC, FRFACT(200, 47),	0x18 },
	{ "Vsbint", fins_get_volt, SENSOR_VOLTS_DC, FRFACT(200, 47),	0x19 },
	{ "Vbat",   fins_get_volt, SENSOR_VOLTS_DC, FRFACT(200, 47),	0x1a },

	{ NULL, fins_get_temp, SENSOR_TEMP, 0x01, 0x1b },
	{ NULL, fins_get_temp, SENSOR_TEMP, 0x02, 0x1c },
	{ NULL, fins_get_temp, SENSOR_TEMP, 0x04, 0x1d },

	{ NULL, fins_get_rpm, SENSOR_FANRPM, 0, 0x20 },
	{ NULL, fins_get_rpm, SENSOR_FANRPM, 0, 0x22 },
	{ NULL, fins_get_rpm, SENSOR_FANRPM, 0, 0x24 },

	{ NULL }
};

const struct fins_sensor fins_71882_sensors[] = {
	{ "+3.3V",  fins_get_volt, SENSOR_VOLTS_DC, FRFACT(100, 100),	0x20 },
	{ "Vcore",  fins_get_volt, SENSOR_VOLTS_DC, FRFACT_NONE,	0x21 },
	{ "Vram",   fins_get_volt, SENSOR_VOLTS_DC, FRFACT(100, 100),	0x22 },
	{ "Vchips", fins_get_volt, SENSOR_VOLTS_DC, FRFACT(47, 100),	0x23 },
	{ "+5V",    fins_get_volt, SENSOR_VOLTS_DC, FRFACT(200, 47),	0x24 },
	{ "+12V",   fins_get_volt, SENSOR_VOLTS_DC, FRFACT(200, 20),	0x25 },
	{ "+1.5V",  fins_get_volt, SENSOR_VOLTS_DC, FRFACT_NONE,	0x26 },
	{ "Vsb",    fins_get_volt, SENSOR_VOLTS_DC, FRFACT(100, 100),	0x27 },
	{ "Vbat",   fins_get_volt, SENSOR_VOLTS_DC, FRFACT(100, 100),	0x28 },

	{ NULL, fins_get_temp, SENSOR_TEMP, 0x02, 0x72 },
	{ NULL, fins_get_temp, SENSOR_TEMP, 0x04, 0x74 },
	{ NULL, fins_get_temp, SENSOR_TEMP, 0x08, 0x76 },

	{ NULL, fins_get_rpm, SENSOR_FANRPM, 0, 0xa0 },
	{ NULL, fins_get_rpm, SENSOR_FANRPM, 0, 0xb0 },
	{ NULL, fins_get_rpm, SENSOR_FANRPM, 0, 0xc0 },
	{ NULL, fins_get_rpm, SENSOR_FANRPM, 0, 0xd0 },

	{ NULL }
};

int
fins_match(struct device *parent, void *match, void *aux)
{
	struct isa_attach_args *ia = aux;
	bus_space_handle_t ioh;
	bus_space_tag_t iot;
	int ret = 0;
	u_int16_t id;

	iot = ia->ia_iot;
	if (bus_space_map(iot, ia->ipa_io[0].base, 2, 0, &ioh))
		return (0);

	/* Fintek uses magic cookie locks to distinguish their chips */
	fins_unlock(iot, ioh);

	fins_write(iot, ioh, FINS_FUNC_SEL, 0);	/* IDs appear only in space 0 */
	if (fins_read_2(iot, ioh, FINS_MANUF) != FINTEK_ID)
		goto match_done;
	id = fins_read_2(iot, ioh, FINS_CHIP);
	switch(id) {
	case FINS_71882:
	case FINS_71862:
		ia->ipa_nio = 3;
		fins_write(iot, ioh, FINS_FUNC_SEL, FINS_FUNC_WATCHDOG);
		ia->ipa_io[2].base = fins_read_2(iot, ioh, FINS_BASEADDR);
		ia->ipa_io[2].length = 8;
		fins_write(iot, ioh, FINS_FUNC_SEL, FINS_FUNC_SENSORS);
		ia->ipa_io[1].base = fins_read_2(iot, ioh, FINS_BASEADDR);
		ia->ipa_io[1].base += 5;
		break;
	case FINS_71806:
	case FINS_71805:
		ia->ipa_nio = 2;
		fins_write(iot, ioh, FINS_FUNC_SEL, FINS_FUNC_SENSORS);
		ia->ipa_io[1].base = fins_read_2(iot, ioh, FINS_BASEADDR);
		break;
	default:
		goto match_done;
	}
	ia->ipa_io[0].length = ia->ipa_io[1].length = 2;
	ia->ipa_nmem = ia->ipa_nirq = ia->ipa_ndrq = 0;
	ia->ia_aux = (void *)(u_long)id;
	ret = 1;
match_done:
	fins_lock(iot, ioh);
	return (ret);
}

void
fins_attach(struct device *parent, struct device *self, void *aux)
{
	struct fins_softc *sc = (struct fins_softc *)self;
	struct isa_attach_args *ia = aux;
	bus_addr_t iobase;
	u_int32_t iosize;
	u_int i;

	sc->sc_iot = ia->ia_iot;
	sc->fins_chipid = (u_int16_t)(u_long)ia->ia_aux;
	iobase = ia->ipa_io[1].base;
	iosize = ia->ipa_io[1].length;
	if (bus_space_map(sc->sc_iot, iobase, iosize, 0, &sc->sc_ioh_sens)) {
		printf(": can't map sensor i/o space\n");
		return;
	}
	switch(sc->fins_chipid) {
	case FINS_71882:
	case FINS_71862:
		fins_setup_sensors(sc, fins_71882_sensors);
		break;
	case FINS_71806:
	case FINS_71805:
		fins_setup_sensors(sc, fins_71805_sensors);
		break;
	}
	sc->fins_sensortask = sensor_task_register(sc, fins_refresh, 5);
	if (sc->fins_sensortask == NULL) {
		printf(": can't register update task\n");
		return;
	}
	for (i = 0; sc->fins_sensors[i].fs_refresh != NULL; ++i)
		sensor_attach(&sc->fins_sensordev, &sc->fins_ksensors[i]);
	sensordev_install(&sc->fins_sensordev);

	if (sc->fins_chipid <= FINS_71805)
		goto attach_done;
	iobase = ia->ipa_io[2].base;
	iosize = ia->ipa_io[2].length;
	if (bus_space_map(sc->sc_iot, iobase, iosize, 0, &sc->sc_ioh_wdog)) {
		printf(": can't map watchdog i/o space\n");
		return;
	}
	sc->fins_wdog_cr = fins_read_wdog(sc, FINS_WDOG_CR1);
	sc->fins_wdog_cr &= ~(FINS_WDOG_MINS | FINS_WDOG_EN);
	fins_write_wdog(sc, FINS_WDOG_CR1, sc->fins_wdog_cr);
	wdog_register(fins_wdog_cb, sc);
attach_done:
	printf("\n");
}

int
fins_activate(struct device *self, int act)
{
	switch (act) {
	case DVACT_POWERDOWN:
		wdog_shutdown(self);
		break;
	}

	return (0);
}

u_int8_t
fins_read(bus_space_tag_t iot, bus_space_handle_t ioh, int reg)
{
	bus_space_write_1(iot, ioh, FINS_ADDR, reg);
	return (bus_space_read_1(iot, ioh, FINS_DATA));
}

u_int16_t
fins_read_2(bus_space_tag_t iot, bus_space_handle_t ioh, int reg)
{
	u_int16_t val;

	bus_space_write_1(iot, ioh, FINS_ADDR, reg);
	val = bus_space_read_1(iot, ioh, FINS_DATA) << 8;
	bus_space_write_1(iot, ioh, FINS_ADDR, reg + 1);
	return (val | bus_space_read_1(iot, ioh, FINS_DATA));
}

void
fins_write(bus_space_tag_t iot, bus_space_handle_t ioh, int reg, u_int8_t val)
{
	bus_space_write_1(iot, ioh, FINS_ADDR, reg);
	bus_space_write_1(iot, ioh, FINS_DATA, val);
}

static __inline u_int8_t
fins_read_sens(struct fins_softc *sc, int reg)
{
	return (fins_read(sc->sc_iot, sc->sc_ioh_sens, reg));
}

static __inline u_int16_t
fins_read_sens_2(struct fins_softc *sc, int reg)
{
	return (fins_read_2(sc->sc_iot, sc->sc_ioh_sens, reg));
}

static __inline u_int8_t
fins_read_wdog(struct fins_softc *sc, int reg)
{
	return (bus_space_read_1(sc->sc_iot, sc->sc_ioh_wdog, reg));
}

static __inline void
fins_write_wdog(struct fins_softc *sc, int reg, u_int8_t val)
{
	bus_space_write_1(sc->sc_iot, sc->sc_ioh_wdog, reg, val);
}

void
fins_unlock(bus_space_tag_t iot, bus_space_handle_t ioh)
{
	bus_space_write_1(iot, ioh, 0, FINS_UNLOCK);
	bus_space_write_1(iot, ioh, 0, FINS_UNLOCK);
}

void
fins_lock(bus_space_tag_t iot, bus_space_handle_t ioh)
{
	bus_space_write_1(iot, ioh, 0, FINS_LOCK);
	bus_space_unmap(iot, ioh, 2);
}

void
fins_setup_sensors(struct fins_softc *sc, const struct fins_sensor *sensors)
{
	int i;

	for (i = 0; sensors[i].fs_refresh != NULL; ++i) {
		sc->fins_ksensors[i].type = sensors[i].fs_type;
		if (sensors[i].fs_desc != NULL)
			strlcpy(sc->fins_ksensors[i].desc, sensors[i].fs_desc,
				sizeof(sc->fins_ksensors[i].desc));
	}
	strlcpy(sc->fins_sensordev.xname, sc->sc_dev.dv_xname,
		sizeof(sc->fins_sensordev.xname));
	sc->fins_sensors = sensors;
	sc->fins_tempsel = fins_read_sens(sc, FINS_SENS_TMODE(sc));
}

#if 0
void
fins_get_dividers(struct fins_softc *sc)
{
	int i, p, m;
	u_int16_t r = fins_read_sens_2(sc, FINS_SENS_VDIVS);

	for (i = 0; i < 6; ++i) {
		p = (i < 4) ? i : i + 2;
		m = (r & (0x03 << p)) >> p;
		if (m == 3)
			m = 4;
		fins_71882_sensors[i + 1].fs_aux = FRFACT_NONE << m;
	}
}
#endif

void
fins_refresh(void *arg)
{
	struct fins_softc *sc = arg;
	int i;

	for (i = 0; sc->fins_sensors[i].fs_refresh != NULL; ++i)
		sc->fins_sensors[i].fs_refresh(sc, i);
}

void
fins_get_volt(struct fins_softc *sc, int n)
{
	struct ksensor *sensor = &sc->fins_ksensors[n];
	const struct fins_sensor *fs = &sc->fins_sensors[n];
	int data;

	data = fins_read_sens(sc, fs->fs_reg);
	if (data == 0xff || data == 0) {
		sensor->flags |= SENSOR_FINVALID;
		sensor->value = 0;
	} else {
		sensor->flags &= ~SENSOR_FINVALID;
		sensor->value = data * fs->fs_aux;
	}
}

/* The BIOS seems to add a fudge factor to the CPU temp of +5C */
void
fins_get_temp(struct fins_softc *sc, int n)
{
	struct ksensor *sensor = &sc->fins_ksensors[n];
	const struct fins_sensor *fs = &sc->fins_sensors[n];
	u_int data;
	u_int max;

	/*
	 * The data sheet says that the range of the temperature
	 * sensor is between 0 and 127 or 140 degrees C depending on
	 * what kind of sensor is used.
	 * A disconnected sensor seems to read over 110 or so.
	 */
	data = fins_read_sens(sc, fs->fs_reg);
	max = (sc->fins_tempsel & fs->fs_aux) ? 111 : 128;
	if (data == 0 || data >= max) {	/* disconnected? */
		sensor->flags |= SENSOR_FINVALID;
		sensor->value = 0;
	} else {
		sensor->flags &= ~SENSOR_FINVALID;
		sensor->value = data * 1000000 + 273150000;
	}
}

/* The chip holds a fudge factor for BJT sensors */
/* this is currently unused but might be reenabled */
#if 0
void
fins_refresh_offset(struct fins_softc *sc, int n)
{
	struct ksensor *sensor = &sc->fins_ksensors[n];
	const struct fins_sensor *fs = &sc->fins_sensors[n];
	u_int data;

	sensor->flags &= ~SENSOR_FINVALID;
	data = fins_read_sens(sc, fs->fs_reg);
	data |= ~0 * (data & 0x40);	/* sign extend 7-bit value */
	sensor->value = data * 1000000 + 273150000;
}
#endif

/* fan speed appears to be a 12-bit number */
void
fins_get_rpm(struct fins_softc *sc, int n)
{
	struct ksensor *sensor = &sc->fins_ksensors[n];
	const struct fins_sensor *fs = &sc->fins_sensors[n];
	int data;

	data = fins_read_sens_2(sc, fs->fs_reg);
	if (data >= 0xfff) {
		sensor->value = 0;
		sensor->flags |= SENSOR_FINVALID;
	} else {
		sensor->value = 1500000 / data;
		sensor->flags &= ~SENSOR_FINVALID;
	}
}

int
fins_wdog_cb(void *arg, int period)
{
	struct fins_softc *sc = arg;
	u_int8_t cr0, cr1, t;

	cr0 = fins_read_wdog(sc, FINS_WDOG_CR0) & ~FINS_WDOG_OUTEN;
	fins_write_wdog(sc, FINS_WDOG_CR0, cr0);

	cr1 = sc->fins_wdog_cr;
	if (period > 0xff) {
		cr1 |= FINS_WDOG_MINS;
		t = (period + 59) / 60;
		period = (int)t * 60;
	} else if (period > 0)
		t = period;
	else
		return (0);

	fins_write_wdog(sc, FINS_WDOG_TIMER, t);
	fins_write_wdog(sc, FINS_WDOG_CR0, cr0 | FINS_WDOG_OUTEN);
	fins_write_wdog(sc, FINS_WDOG_CR1, cr1 | FINS_WDOG_EN);
	return (period);
}
