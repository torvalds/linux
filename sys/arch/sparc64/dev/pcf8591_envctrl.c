/*	$OpenBSD: pcf8591_envctrl.c,v 1.7 2021/10/24 17:05:03 mpi Exp $ */

/*
 * Copyright (c) 2006 Damien Miller <djm@openbsd.org>
 * Copyright (c) 2007 Mark Kettenis <kettenis@openbsd.org>
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

#include <dev/ofw/openfirm.h>
#include <dev/i2c/i2cvar.h>

#define PCF8591_CHANNELS	4

#define PCF8591_CTRL_CH0	0x00
#define PCF8591_CTRL_CH1	0x01
#define PCF8591_CTRL_CH2	0x02
#define PCF8591_CTRL_CH3	0x03
#define PCF8591_CTRL_AUTOINC	0x04
#define PCF8591_CTRL_OSCILLATOR	0x40

struct ecadc_channel {
	u_int		chan_num;
	struct ksensor	chan_sensor;
	u_char		*chan_xlate;
	int64_t		chan_factor;
	int64_t		chan_min;
	int64_t		chan_warn;
	int64_t		chan_crit;
};

struct ecadc_softc {
	struct device		sc_dev;
	i2c_tag_t		sc_tag;
	i2c_addr_t		sc_addr;
	u_char			sc_ps_xlate[256];
	u_char			sc_cpu_xlate[256];
	u_int			sc_nchan;
	struct ecadc_channel	sc_channels[PCF8591_CHANNELS];
	struct ksensordev	sc_sensordev;
};

int	ecadc_match(struct device *, void *, void *);
void	ecadc_attach(struct device *, struct device *, void *);
void	ecadc_refresh(void *);

const struct cfattach ecadc_ca = {
	sizeof(struct ecadc_softc), ecadc_match, ecadc_attach
};

struct cfdriver ecadc_cd = {
	NULL, "ecadc", DV_DULL
};

int
ecadc_match(struct device *parent, void *match, void *aux)
{
	struct i2c_attach_args *ia = aux;

	if (strcmp(ia->ia_name, "ecadc") != 0)
		return (0);

	return (1);
}

void
ecadc_attach(struct device *parent, struct device *self, void *aux)
{
	struct ecadc_softc *sc = (struct ecadc_softc *)self;
	u_char term[256];
	u_char *cp, *desc;
	int64_t min, warn, crit, num, den;
	u_int8_t junk[PCF8591_CHANNELS + 1];
	struct i2c_attach_args *ia = aux;
	struct ksensor *sensor;
	int len, addr, chan, node = *(int *)ia->ia_cookie;
	u_int i;

	if ((len = OF_getprop(node, "thermisters", term,
	    sizeof(term))) < 0) {
		printf(": couldn't find \"thermisters\" property\n");
		return;
	}

	if (OF_getprop(node, "cpu-temp-factors", &sc->sc_cpu_xlate[2],
	    sizeof(sc->sc_cpu_xlate) - 2) < 0) {
		printf(": couldn't find \"cpu-temp-factors\" property\n");
		return;
	}
	sc->sc_cpu_xlate[0] = sc->sc_cpu_xlate[1] = sc->sc_cpu_xlate[2];

	/* Only the Sun Enterprise 450 has these. */
	OF_getprop(node, "ps-temp-factors", &sc->sc_ps_xlate[2],
	    sizeof(sc->sc_ps_xlate) - 2);
	sc->sc_ps_xlate[0] = sc->sc_ps_xlate[1] = sc->sc_ps_xlate[2];

	cp = term;
	while (cp < term + len) {
		addr = cp[0] << 24 | cp[1] << 16 | cp[2] << 8 | cp[3]; cp += 4;
		chan = cp[0] << 24 | cp[1] << 16 | cp[2] << 8 | cp[3]; cp += 4;
		min = cp[0] << 24 | cp[1] << 16 | cp[2] << 8 | cp[3]; cp += 4;
		warn = cp[0] << 24 | cp[1] << 16 | cp[2] << 8 | cp[3]; cp += 4;
		crit = cp[0] << 24 | cp[1] << 16 | cp[2] << 8 | cp[3]; cp += 4;
		num = cp[0] << 24 | cp[1] << 16 | cp[2] << 8 | cp[3]; cp += 4;
		den = cp[0] << 24 | cp[1] << 16 | cp[2] << 8 | cp[3]; cp += 4;
		desc = cp;
		while (cp < term + len && *cp++);

		if (addr != (ia->ia_addr << 1))
			continue;

		if (num == 0 || den == 0)
			num = den = 1;

		sc->sc_channels[sc->sc_nchan].chan_num = chan;

		sensor = &sc->sc_channels[sc->sc_nchan].chan_sensor;
		sensor->type = SENSOR_TEMP;
		strlcpy(sensor->desc, desc, sizeof(sensor->desc));

		if (strncmp(desc, "CPU", 3) == 0)
			sc->sc_channels[sc->sc_nchan].chan_xlate =
			    sc->sc_cpu_xlate;
		else if (strncmp(desc, "PS", 2) == 0)
			sc->sc_channels[sc->sc_nchan].chan_xlate =
			    sc->sc_ps_xlate;
		else
			sc->sc_channels[sc->sc_nchan].chan_factor =
			    (1000000 * num) / den;
		sc->sc_channels[sc->sc_nchan].chan_min =
		    273150000 + 1000000 * min;
		sc->sc_channels[sc->sc_nchan].chan_warn =
		    273150000 + 1000000 * warn;
		sc->sc_channels[sc->sc_nchan].chan_crit =
		    273150000 + 1000000 * crit;
		sc->sc_nchan++;
	}

	sc->sc_tag = ia->ia_tag;
	sc->sc_addr = ia->ia_addr;

	iic_acquire_bus(sc->sc_tag, 0);

	/* Try a read now, so we can fail if it doesn't work */
	if (iic_exec(sc->sc_tag, I2C_OP_READ_WITH_STOP, sc->sc_addr,
	    NULL, 0, junk, sc->sc_nchan + 1, 0)) {
		printf(": read failed\n");
		iic_release_bus(sc->sc_tag, 0);
		return;
	}

	iic_release_bus(sc->sc_tag, 0);

	/* Initialize sensor data. */
	strlcpy(sc->sc_sensordev.xname, sc->sc_dev.dv_xname,
	    sizeof(sc->sc_sensordev.xname));

	for (i = 0; i < sc->sc_nchan; i++)
		sensor_attach(&sc->sc_sensordev, 
		    &sc->sc_channels[i].chan_sensor);

	if (sensor_task_register(sc, ecadc_refresh, 5) == NULL) {
		printf(": unable to register update task\n");
		return;
	}

	sensordev_install(&sc->sc_sensordev);

	printf("\n");
}

void
ecadc_refresh(void *arg)
{
	struct ecadc_softc *sc = arg;
	u_int i;
	u_int8_t data[PCF8591_CHANNELS + 1];
	u_int8_t ctrl = PCF8591_CTRL_CH0 | PCF8591_CTRL_AUTOINC |
	    PCF8591_CTRL_OSCILLATOR;

	iic_acquire_bus(sc->sc_tag, 0);
	if (iic_exec(sc->sc_tag, I2C_OP_WRITE_WITH_STOP, sc->sc_addr,
	    &ctrl, 1, NULL, 0, 0)) {
		iic_release_bus(sc->sc_tag, 0);
		return;
	}
	/* NB: first byte out is stale, so read num_channels + 1 */
	if (iic_exec(sc->sc_tag, I2C_OP_READ_WITH_STOP, sc->sc_addr,
	    NULL, 0, data, PCF8591_CHANNELS + 1, 0)) {
		iic_release_bus(sc->sc_tag, 0);
		return;
	}
	iic_release_bus(sc->sc_tag, 0);

	/* We only support temperature channels. */
	for (i = 0; i < sc->sc_nchan; i++) {
		struct ecadc_channel *chp = &sc->sc_channels[i];

		if (chp->chan_xlate)
			chp->chan_sensor.value = 273150000 + 1000000 *
			    chp->chan_xlate[data[1 + chp->chan_num]];
		else
			chp->chan_sensor.value = 273150000 +
			    chp->chan_factor * data[1 + chp->chan_num];

		chp->chan_sensor.status = SENSOR_S_OK;
		if (chp->chan_sensor.value < chp->chan_min)
			chp->chan_sensor.status = SENSOR_S_UNKNOWN;
		if (chp->chan_sensor.value > chp->chan_warn)
			chp->chan_sensor.status = SENSOR_S_WARN;
		if (chp->chan_sensor.value > chp->chan_crit)
			chp->chan_sensor.status = SENSOR_S_CRIT;
	}
}
