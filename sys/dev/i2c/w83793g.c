/*	$OpenBSD: w83793g.c,v 1.6 2022/04/06 18:59:28 naddy Exp $	*/

/*
 * Copyright (c) 2007 Constantine A. Murenin <cnst+openbsd@bugmail.mojo.ru>
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

/* Winbond W83793G Hardware Monitor */

#define WB_BANKSELECT	0x00

/* Voltage */
#define WB_NUM_VOLTS	10

static const char *wb_volt_desc[WB_NUM_VOLTS] = {
	"VCore", "VCore", "VTT",
	"", "", "3.3V", "12V", "5VDD", "5VSB", "VBat"
};

#define WB_VCOREA	0x10
#define WB_VCOREB	0x11
#define WB_VTT		0x12
#define WB_VLOW		0x1b

#define WB_VSENS1	0x14
#define WB_VSENS2	0x15
#define WB_3VSEN	0x16
#define WB_12VSEN	0x17
#define WB_5VDD		0x18
#define WB_5VSB		0x19
#define WB_VBAT		0x1a

/* Temperature */
#define WB_NUM_TEMPS	6

#define WB_TD_COUNT	4
#define WB_TD_START	0x1c
#define WB_TDLOW	0x22

#define WB_TR_COUNT	2
#define WB_TR_START	0x20

/* Fan */
#define WB_NUM_FANS	12
#define WB_FAN_START	0x23


struct wbng_softc {
	struct device	sc_dev;
	i2c_tag_t	sc_tag;
	i2c_addr_t	sc_addr;

	struct ksensor	sc_sensors[WB_NUM_VOLTS + WB_NUM_TEMPS + WB_NUM_FANS];
	struct ksensordev sc_sensordev;
};


int	wbng_match(struct device *, void *, void *);
void	wbng_attach(struct device *, struct device *, void *);
void	wbng_refresh(void *);

void	wbng_refresh_volts(struct wbng_softc *);
void	wbng_refresh_temps(struct wbng_softc *);
void	wbng_refresh_fans(struct wbng_softc *);

uint8_t	wbng_readreg(struct wbng_softc *, uint8_t);
void	wbng_writereg(struct wbng_softc *, uint8_t, uint8_t);


const struct cfattach wbng_ca = {
	sizeof(struct wbng_softc), wbng_match, wbng_attach
};

struct cfdriver wbng_cd = {
	NULL, "wbng", DV_DULL
};


int
wbng_match(struct device *parent, void *match, void *aux)
{
	struct i2c_attach_args *ia = aux;

	if (strcmp(ia->ia_name, "w83793g") == 0)
		return 1;
	return 0;
}

void
wbng_attach(struct device *parent, struct device *self, void *aux)
{
	struct wbng_softc *sc = (struct wbng_softc *)self;
	struct i2c_attach_args *ia = aux;
	int i, j;

	sc->sc_tag = ia->ia_tag;
	sc->sc_addr = ia->ia_addr;

	printf(": %s", ia->ia_name);

	strlcpy(sc->sc_sensordev.xname, sc->sc_dev.dv_xname,
	    sizeof(sc->sc_sensordev.xname));

	for (i = 0; i < WB_NUM_VOLTS; i++) {
		strlcpy(sc->sc_sensors[i].desc, wb_volt_desc[i],
		    sizeof(sc->sc_sensors[i].desc));
		sc->sc_sensors[i].type = SENSOR_VOLTS_DC;
	}

	for (j = i + WB_NUM_TEMPS; i < j; i++)
		sc->sc_sensors[i].type = SENSOR_TEMP;

	for (j = i + WB_NUM_FANS; i < j; i++)
		sc->sc_sensors[i].type = SENSOR_FANRPM;

	for (i = 0; i < WB_NUM_VOLTS + WB_NUM_TEMPS + WB_NUM_FANS; i++)
		sensor_attach(&sc->sc_sensordev, &sc->sc_sensors[i]);

	if (sensor_task_register(sc, wbng_refresh, 5) == NULL) {
		printf(", unable to register update task\n");
		return;
	}

	sensordev_install(&sc->sc_sensordev);
	printf("\n");
}

void
wbng_refresh(void *arg)
{
	struct wbng_softc *sc = arg;
	uint8_t bsr;

	iic_acquire_bus(sc->sc_tag, 0);

	bsr = wbng_readreg(sc, WB_BANKSELECT);
	if ((bsr & 0x07) != 0x0)
		wbng_writereg(sc, WB_BANKSELECT, bsr & 0xf8);

	wbng_refresh_volts(sc);
	wbng_refresh_temps(sc);
	wbng_refresh_fans(sc);

	if ((bsr & 0x07) != 0x0)
		wbng_writereg(sc, WB_BANKSELECT, bsr);

	iic_release_bus(sc->sc_tag, 0);
}

void
wbng_refresh_volts(struct wbng_softc *sc)
{
	struct ksensor *s = &sc->sc_sensors[0];
	uint8_t	vlow, data;

	/* high precision voltage sensors */

	vlow = wbng_readreg(sc, WB_VLOW);

	data = wbng_readreg(sc, WB_VCOREA);
	s[0].value = ((data << 3) | (((vlow & 0x03)) << 1)) * 1000;

	data = wbng_readreg(sc, WB_VCOREB);
	s[1].value = ((data << 3) | (((vlow & 0x0c) >> 2) << 1)) * 1000;

	data = wbng_readreg(sc, WB_VTT);
	s[2].value = ((data << 3) | (((vlow & 0x30) >> 4) << 1)) * 1000;

	/* low precision voltage sensors */

	data = wbng_readreg(sc, WB_VSENS1);
	s[3].value = (data << 4) * 1000;

	data = wbng_readreg(sc, WB_VSENS2);
	s[4].value = (data << 4) * 1000;

	data = wbng_readreg(sc, WB_3VSEN);
	s[5].value = (data << 4) * 1000;

	data = wbng_readreg(sc, WB_12VSEN);
	s[6].value = (data << 4) * 6100;	/*XXX, the factor is a guess */

	data = wbng_readreg(sc, WB_5VDD);
	s[7].value = (data << 4) * 1500 + 150000;

	data = wbng_readreg(sc, WB_5VSB);
	s[8].value = (data << 4) * 1500 + 150000;

	data = wbng_readreg(sc, WB_VBAT);
	s[9].value = (data << 4) * 1000;
}

void
wbng_refresh_temps(struct wbng_softc *sc)
{
	struct ksensor *s = &sc->sc_sensors[WB_NUM_VOLTS];
	int data, i;
	uint8_t	tdlow, low;

	/* high precision temperature sensors */
	tdlow = wbng_readreg(sc, WB_TDLOW);
	for (i = 0; i < WB_TD_COUNT; i++) {
		data = wbng_readreg(sc, WB_TD_START + i);
		/*
		 * XXX: datasheet says nothing about acceptable values,
		 * let's consider only values between -55 degC and +125 degC.
		 */
		if (data > 0x7f && data < 0xc9) {
			s[i].flags |= SENSOR_FINVALID;
			s[i].value = 0;
			continue;
		}
		if (data & 0x80)
			data -= 0x100;
		low = (tdlow & (0x03 << (i * 2))) >> (i * 2);
		s[i].value = data * 1000000 + low * 250000 + 273150000;
		s[i].flags &= ~SENSOR_FINVALID;
	}
	s += i;

	/* low precision temperature sensors */
	for (i = 0; i < WB_TR_COUNT; i++) {
		data = wbng_readreg(sc, WB_TR_START + i);
		/*
		 * XXX: datasheet says nothing about acceptable values,
		 * let's consider only values between -55 degC and +125 degC.
		 */
		if (data > 0x7f && data < 0xc9) {
			s[i].flags |= SENSOR_FINVALID;
			s[i].value = 0;
			continue;
		}
		if (data & 0x80)
			data -= 0x100;
		s[i].value = data * 1000000 + 273150000;
		s[i].flags &= ~SENSOR_FINVALID;
	}
}

void
wbng_refresh_fans(struct wbng_softc *sc)
{
	struct ksensor *s = &sc->sc_sensors[WB_NUM_VOLTS + WB_NUM_TEMPS];
	int i;

	for (i = 0; i < WB_NUM_FANS; i++) {
		uint8_t h = wbng_readreg(sc, WB_FAN_START + i * 2);
		uint8_t l = wbng_readreg(sc, WB_FAN_START + i * 2 + 1);
		uint16_t b = h << 8 | l;

		if (b >= 0x0fff || b == 0x0f00 || b == 0x0000) {
			s[i].flags |= SENSOR_FINVALID;
			s[i].value = 0;
		} else {
			s[i].flags &= ~SENSOR_FINVALID;
			s[i].value = 1350000 / b;
		}
	}
}

uint8_t
wbng_readreg(struct wbng_softc *sc, uint8_t reg)
{
	uint8_t data;

	iic_exec(sc->sc_tag, I2C_OP_READ_WITH_STOP,
	    sc->sc_addr, &reg, sizeof reg, &data, sizeof data, 0);

	return data;
}

void
wbng_writereg(struct wbng_softc *sc, uint8_t reg, uint8_t data)
{
	iic_exec(sc->sc_tag, I2C_OP_WRITE_WITH_STOP,
	    sc->sc_addr, &reg, sizeof reg, &data, sizeof data, 0);
}
