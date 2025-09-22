/*	$OpenBSD: w83795g.c,v 1.2 2022/04/06 18:59:28 naddy Exp $	*/

/*
 * Copyright (c) 2011 Mark Kettenis
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

/* Nuvoton W83795G Hardware Monitor */

#define NVT_BANKSELECT		0x00
#define NVT_CONFIG		0x01
#define  NVT_CONFIG_48		0x04
#define NVT_VOLT_CTRL1		0x02
#define NVT_VOLT_CTRL2		0x03
#define NVT_TEMP_CTRL1		0x04
#define NVT_TEMP_CTRL2		0x05
#define NVT_FANIN_CTRL1		0x06
#define NVT_FANIN_CTRL2		0x07
#define NVT_VSEN1		0x10
#define NVT_3VDD		0x1c
#define NVT_3VSB		0x1d
#define NVT_VBAT		0x1e
#define NVT_TR5			0x1f
#define NVT_TR6			0x20
#define NVT_TD1			0x21
#define NVT_TD2			0x22
#define NVT_TD3			0x23
#define NVT_TD4			0x24
#define NVT_FANIN1_COUNT	0x2e
#define NVT_VRLSB		0x3c

/* Voltage */
#define NVT_NUM_VOLTS	15

static const char *nvt_volt_desc[NVT_NUM_VOLTS] = {
	"", "", "", "", "", "", "", "", "", "", "",
	"VTT", "3VDD", "3VSB", "VBat"
};

/* Temperature */
#define NVT_NUM_TEMPS	6
#define NVT_NUM_TR	2
#define NVT_NUM_TD	4

/* Fan */
#define NVT_NUM_FANS	14

#define NVT_NUM_SENSORS (NVT_NUM_VOLTS + NVT_NUM_TEMPS + NVT_NUM_FANS)

struct nvt_softc {
	struct device	sc_dev;
	i2c_tag_t	sc_tag;
	i2c_addr_t	sc_addr;

	uint16_t	sc_vctrl;
	uint16_t	sc_tctrl1, sc_tctrl2;
	uint16_t	sc_fctrl;

	struct ksensor	sc_sensors[NVT_NUM_SENSORS];
	struct ksensordev sc_sensordev;
};


int	nvt_match(struct device *, void *, void *);
void	nvt_attach(struct device *, struct device *, void *);
void	nvt_refresh(void *);

void	nvt_refresh_volts(struct nvt_softc *);
void	nvt_refresh_temps(struct nvt_softc *);
void	nvt_refresh_fans(struct nvt_softc *);

uint8_t	nvt_readreg(struct nvt_softc *, uint8_t);
void	nvt_writereg(struct nvt_softc *, uint8_t, uint8_t);


const struct cfattach nvt_ca = {
	sizeof(struct nvt_softc), nvt_match, nvt_attach
};

struct cfdriver nvt_cd = {
	NULL, "nvt", DV_DULL
};


int
nvt_match(struct device *parent, void *match, void *aux)
{
	struct i2c_attach_args *ia = aux;

	if (strcmp(ia->ia_name, "w83795g") == 0)
		return (1);
	return (0);
}

void
nvt_attach(struct device *parent, struct device *self, void *aux)
{
	struct nvt_softc *sc = (struct nvt_softc *)self;
	struct i2c_attach_args *ia = aux;
	uint8_t cfg, vctrl1, vctrl2;
	uint8_t tctrl1, tctrl2, fctrl1, fctrl2;
	int i, j;

	sc->sc_tag = ia->ia_tag;
	sc->sc_addr = ia->ia_addr;

	cfg = nvt_readreg(sc, NVT_CONFIG);
	if (cfg & NVT_CONFIG_48)
		printf(": W83795ADG");
	else
		printf(": W83795G");

	vctrl1 = nvt_readreg(sc, NVT_VOLT_CTRL1);
	vctrl2 = nvt_readreg(sc, NVT_VOLT_CTRL2);
	tctrl1 = nvt_readreg(sc, NVT_TEMP_CTRL1);
	tctrl2 = nvt_readreg(sc, NVT_TEMP_CTRL2);
	fctrl1 = nvt_readreg(sc, NVT_FANIN_CTRL1);
	fctrl2 = nvt_readreg(sc, NVT_FANIN_CTRL2);

	sc->sc_vctrl = vctrl2 << 8 | vctrl1;
	sc->sc_tctrl1 = tctrl1;
	sc->sc_tctrl2 = tctrl2;
	sc->sc_fctrl = fctrl2 << 8 | fctrl1;

	strlcpy(sc->sc_sensordev.xname, sc->sc_dev.dv_xname,
	    sizeof(sc->sc_sensordev.xname));

	for (i = 0; i < NVT_NUM_VOLTS; i++) {
		strlcpy(sc->sc_sensors[i].desc, nvt_volt_desc[i],
		    sizeof(sc->sc_sensors[i].desc));
		sc->sc_sensors[i].type = SENSOR_VOLTS_DC;
	}

	for (j = i + NVT_NUM_TEMPS; i < j; i++)
		sc->sc_sensors[i].type = SENSOR_TEMP;

	for (j = i + NVT_NUM_FANS; i < j; i++)
		sc->sc_sensors[i].type = SENSOR_FANRPM;

	for (i = 0; i < NVT_NUM_VOLTS + NVT_NUM_TEMPS + NVT_NUM_FANS; i++)
		sensor_attach(&sc->sc_sensordev, &sc->sc_sensors[i]);

	if (sensor_task_register(sc, nvt_refresh, 5) == NULL) {
		printf(", unable to register update task\n");
		return;
	}

	sensordev_install(&sc->sc_sensordev);
	printf("\n");
}

void
nvt_refresh(void *arg)
{
	struct nvt_softc *sc = arg;
	uint8_t bsr;

	iic_acquire_bus(sc->sc_tag, 0);

	bsr = nvt_readreg(sc, NVT_BANKSELECT);
	if ((bsr & 0x07) != 0x00)
		nvt_writereg(sc, NVT_BANKSELECT, bsr & 0xf8);

	nvt_refresh_volts(sc);
	nvt_refresh_temps(sc);
	nvt_refresh_fans(sc);

	if ((bsr & 0x07) != 0x00)
		nvt_writereg(sc, NVT_BANKSELECT, bsr);

	iic_release_bus(sc->sc_tag, 0);
}

void
nvt_refresh_volts(struct nvt_softc *sc)
{
	struct ksensor *s = &sc->sc_sensors[0];
	uint8_t	vrlsb, data;
	int i, reg;

	for (i = 0; i < NVT_NUM_VOLTS; i++) {
		if ((sc->sc_vctrl & (1 << i)) == 0) {
			s[i].flags |= SENSOR_FINVALID;
			s[i].value = 0;
			continue;
		}

		reg = NVT_VSEN1 + i;
		data = nvt_readreg(sc, reg);
		vrlsb = nvt_readreg(sc, NVT_VRLSB);
		if (reg != NVT_3VDD && reg != NVT_3VSB && reg != NVT_VBAT)
			s[i].value = 10000000 - ((data << 3) | (vrlsb >> 6)) * 2000;
		else
			s[i].value = 10000000 - ((data << 3) | (vrlsb >> 6)) * 6000;
		s[i].flags &= ~SENSOR_FINVALID;
	}
}

void
nvt_refresh_temps(struct nvt_softc *sc)
{
	struct ksensor *s = &sc->sc_sensors[NVT_NUM_VOLTS];
	uint8_t	vrlsb;
	int8_t data;
	int i;

	for (i = 0; i < NVT_NUM_TEMPS; i++) {
		if (i < NVT_NUM_TR
		    && (sc->sc_tctrl1 & (1 << (2 * i))) == 0) {
			s[i].flags |= SENSOR_FINVALID;
			s[i].value = 0;
			continue;
		}

		if (i >= NVT_NUM_TR
		    && (sc->sc_tctrl2 & (1 << (2 * (i - NVT_NUM_TR)))) == 0) {
			s[i].flags |= SENSOR_FINVALID;
			s[i].value = 0;
			continue;
		}

		data = nvt_readreg(sc, NVT_TR5 + i);
		vrlsb = nvt_readreg(sc, NVT_VRLSB);
		if (data == -128 && (vrlsb >> 6) == 0) {
			s[i].flags |= SENSOR_FINVALID;
			s[i].value = 0;
			continue;
		}
		s[i].value = data * 1000000 + (vrlsb >> 6) * 250000;
		s[i].value += 273150000;
		s[i].flags &= ~SENSOR_FINVALID;
	}
}

void
nvt_refresh_fans(struct nvt_softc *sc)
{
	struct ksensor *s = &sc->sc_sensors[NVT_NUM_VOLTS + NVT_NUM_TEMPS];
	uint8_t	data, vrlsb;
	uint16_t count;
	int i;

	for (i = 0; i < NVT_NUM_FANS; i++) {
		if ((sc->sc_fctrl & (1 << i)) == 0) {
			s[i].flags |= SENSOR_FINVALID;
			s[i].value = 0;
			continue;
		}

		data = nvt_readreg(sc, NVT_FANIN1_COUNT + i);
		vrlsb = nvt_readreg(sc, NVT_VRLSB);
		count = (data << 4) + (vrlsb >> 4);
		if (count == 0) {
			s[i].flags |= SENSOR_FINVALID;
			s[i].value = 0;
			continue;
		}
		s[i].value = 1350000 / (count * 2);
		s[i].flags &= ~SENSOR_FINVALID;
	}
}

uint8_t
nvt_readreg(struct nvt_softc *sc, uint8_t reg)
{
	uint8_t data;

	iic_exec(sc->sc_tag, I2C_OP_READ_WITH_STOP,
	    sc->sc_addr, &reg, sizeof reg, &data, sizeof data, 0);

	return data;
}

void
nvt_writereg(struct nvt_softc *sc, uint8_t reg, uint8_t data)
{
	iic_exec(sc->sc_tag, I2C_OP_WRITE_WITH_STOP,
	    sc->sc_addr, &reg, sizeof reg, &data, sizeof data, 0);
}
