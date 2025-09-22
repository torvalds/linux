/*	$OpenBSD: asc7611.c,v 1.3 2022/04/06 18:59:28 naddy Exp $	*/

/*
 * Copyright (c) 2008 Constantine A. Murenin <cnst+openbsd@bugmail.mojo.ru>
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

/*
 * Andigilog aSC7611
 *	Hardware Monitor with Integrated Fan Control
 * http://www.andigilog.com/downloads/aSC7611_70A05007.pdf
 * October 2006
 */

/* Temperature */
#define ANDL_NUM_TEMPS	3
static const struct {
	const char	*name;
	const uint8_t	mreg;
	const uint8_t	lreg;
} andl_temp[ANDL_NUM_TEMPS] = {
	{ "External", 0x25, 0x10 },
	{ "Internal", 0x26, 0x15 },
	{ "External", 0x27, 0x0e }
};

/* Voltage */
#define ANDL_NUM_VOLTS	5
static const struct {
	const char	*name;
	const short	nominal;
	const uint8_t	mreg;
} andl_volt[ANDL_NUM_VOLTS] = {
	{ "+2.5V", 2500, 0x20 },
	{ "Vccp", 2250, 0x21 },
	{ "+3.3V", 3300, 0x22 },
	{ "+5V", 5000, 0x23 },
	{ "+12V", 12000, 0x24 }
};

/* Fan */
#define ANDL_NUM_TACHS	4
#define ANDL_TACH_START	0x28

#define ANDL_NUM_TOTAL	(ANDL_NUM_TEMPS + ANDL_NUM_VOLTS + ANDL_NUM_TACHS)

struct andl_softc {
	struct device	sc_dev;
	i2c_tag_t	sc_tag;
	i2c_addr_t	sc_addr;

	struct ksensor	sc_sensors[ANDL_NUM_TOTAL];
	struct ksensordev sc_sensordev;
};


int	andl_match(struct device *, void *, void *);
void	andl_attach(struct device *, struct device *, void *);
void	andl_refresh(void *);

int	andl_refresh_temps(struct andl_softc *, struct ksensor *);
int	andl_refresh_volts(struct andl_softc *, struct ksensor *);
int	andl_refresh_tachs(struct andl_softc *, struct ksensor *);

uint8_t	andl_readreg(struct andl_softc *, uint8_t);
void	andl_writereg(struct andl_softc *, uint8_t, uint8_t);


const struct cfattach andl_ca = {
	sizeof(struct andl_softc), andl_match, andl_attach
};

struct cfdriver andl_cd = {
	NULL, "andl", DV_DULL
};


int
andl_match(struct device *parent, void *match, void *aux)
{
	struct i2c_attach_args *ia = aux;

	if (strcmp(ia->ia_name, "asc7611") == 0)
		return 1;
	return 0;
}

void
andl_attach(struct device *parent, struct device *self, void *aux)
{
	struct andl_softc *sc = (struct andl_softc *)self;
	struct i2c_attach_args *ia = aux;
	int i, j;

	sc->sc_tag = ia->ia_tag;
	sc->sc_addr = ia->ia_addr;

	printf(": %s", ia->ia_name);

	strlcpy(sc->sc_sensordev.xname, sc->sc_dev.dv_xname,
	    sizeof(sc->sc_sensordev.xname));

	for (i = 0; i < ANDL_NUM_TEMPS; i++) {
		strlcpy(sc->sc_sensors[i].desc, andl_temp[i].name,
		    sizeof(sc->sc_sensors[i].desc));
		sc->sc_sensors[i].type = SENSOR_TEMP;
	}

	for (j = i; i < j + ANDL_NUM_VOLTS; i++) {
		strlcpy(sc->sc_sensors[i].desc, andl_volt[i - j].name,
		    sizeof(sc->sc_sensors[i].desc));
		sc->sc_sensors[i].type = SENSOR_VOLTS_DC;
	}

	for (j = i + ANDL_NUM_TACHS; i < j; i++)
		sc->sc_sensors[i].type = SENSOR_FANRPM;

	for (i = 0; i < j; i++)
		sensor_attach(&sc->sc_sensordev, &sc->sc_sensors[i]);

	if (sensor_task_register(sc, andl_refresh, 5) == NULL) {
		printf(", unable to register update task\n");
		return;
	}

	sensordev_install(&sc->sc_sensordev);
	printf("\n");
}

void
andl_refresh(void *arg)
{
	struct andl_softc *sc = arg;
	struct ksensor *s = sc->sc_sensors;

	iic_acquire_bus(sc->sc_tag, 0);

	s += andl_refresh_temps(sc, s);
	s += andl_refresh_volts(sc, s);
	s += andl_refresh_tachs(sc, s);

	iic_release_bus(sc->sc_tag, 0);
}

int
andl_refresh_temps(struct andl_softc *sc, struct ksensor *s)
{
	int i;

	for (i = 0; i < ANDL_NUM_TEMPS; i++) {
		uint8_t m = andl_readreg(sc, andl_temp[i].mreg);
		uint8_t l = andl_readreg(sc, andl_temp[i].lreg);
		int32_t t = (m << 8 | l) >> (16 - 10);

		if (t & 0x200)
			t -= 0x400;
		t *= 250;
		if (t < -55000 || t > 125000) {
			s[i].flags |= SENSOR_FINVALID;
			s[i].value = 0;
		} else {
			s[i].value = t * 1000 + 273150000;
			s[i].flags &= ~SENSOR_FINVALID;
		}
	}
	return i;
}

int
andl_refresh_volts(struct andl_softc *sc, struct ksensor *s)
{
	int i;

	for (i = 0; i < ANDL_NUM_VOLTS; i++)
		s[i].value = 1000ll * andl_readreg(sc, andl_volt[i].mreg) *
		    andl_volt[i].nominal / 0xc0;
	return i;
}

int
andl_refresh_tachs(struct andl_softc *sc, struct ksensor *s)
{
	int i;

	for (i = 0; i < ANDL_NUM_TACHS; i++) {
		uint8_t l = andl_readreg(sc, ANDL_TACH_START + i * 2);
		uint8_t m = andl_readreg(sc, ANDL_TACH_START + i * 2 + 1);
		uint16_t b = m << 8 | l;

		if (b >= 0xfffc || b == 0) {
			s[i].flags |= SENSOR_FINVALID;
			s[i].value = 0;
		} else {
			s[i].value = (90000 * 60) / b;
			s[i].flags &= ~SENSOR_FINVALID;
		}
	}
	return i;
}

uint8_t
andl_readreg(struct andl_softc *sc, uint8_t reg)
{
	uint8_t data;

	iic_exec(sc->sc_tag, I2C_OP_READ_WITH_STOP,
	    sc->sc_addr, &reg, sizeof reg, &data, sizeof data, 0);

	return data;
}

void
andl_writereg(struct andl_softc *sc, uint8_t reg, uint8_t data)
{
	iic_exec(sc->sc_tag, I2C_OP_WRITE_WITH_STOP,
	    sc->sc_addr, &reg, sizeof reg, &data, sizeof data, 0);
}
