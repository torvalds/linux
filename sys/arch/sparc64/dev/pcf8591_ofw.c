/*	$OpenBSD: pcf8591_ofw.c,v 1.6 2021/10/24 17:05:03 mpi Exp $ */

/*
 * Copyright (c) 2006 Damien Miller <djm@openbsd.org>
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

struct pcfadc_channel {
	u_int		chan_num;
	struct ksensor	chan_sensor;
};

struct pcfadc_softc {
	struct device		sc_dev;
	i2c_tag_t		sc_tag;
	i2c_addr_t		sc_addr;
	u_char			sc_xlate[256];
	u_int			sc_nchan;
	struct pcfadc_channel	sc_channels[PCF8591_CHANNELS];
	struct ksensordev	sc_sensordev;
};

int	pcfadc_match(struct device *, void *, void *);
void	pcfadc_attach(struct device *, struct device *, void *);
void	pcfadc_refresh(void *);

const struct cfattach pcfadc_ca = {
	sizeof(struct pcfadc_softc), pcfadc_match, pcfadc_attach
};

struct cfdriver pcfadc_cd = {
	NULL, "pcfadc", DV_DULL
};

int
pcfadc_match(struct device *parent, void *match, void *aux)
{
	struct i2c_attach_args *ia = aux;

	if (strcmp(ia->ia_name, "i2cpcf,8591") != 0)
		return (0);

	return (1);
}

void
pcfadc_attach(struct device *parent, struct device *self, void *aux)
{
	struct pcfadc_softc *sc = (struct pcfadc_softc *)self;
	u_char chanuse[PCF8591_CHANNELS * 4], desc[PCF8591_CHANNELS * 32];
	u_char *cp;
	u_int8_t junk[PCF8591_CHANNELS + 1];
	u_int32_t transinfo[PCF8591_CHANNELS * 4];
	struct i2c_attach_args *ia = aux;
	int dlen, clen, tlen, node = *(int *)ia->ia_cookie;
	u_int i;

	if ((dlen = OF_getprop(node, "channels-description", desc,
	    sizeof(desc))) < 0) {
		printf(": couldn't find \"channels-description\" property\n");
		return;
	}
	if (dlen > sizeof(desc) || desc[dlen - 1] != '\0') {
		printf(": bad \"channels-description\" property\n");
		return;
	}
	if ((clen = OF_getprop(node, "channels-in-use", chanuse,
	    sizeof(chanuse))) < 0) {
		printf(": couldn't find \"channels-in-use\" property\n");
		return;
	}
	if ((clen % 4) != 0) {
		printf(": invalid \"channels-in-use\" length %d\n", clen);
		return;
	}
	sc->sc_nchan = clen / 4;
	if (sc->sc_nchan > PCF8591_CHANNELS) {
		printf(": invalid number of channels (%d)\n", sc->sc_nchan);
		return;
	}

	if ((tlen = OF_getprop(node, "tables", sc->sc_xlate,
	    sizeof(sc->sc_xlate))) < 0) {
		printf(": couldn't find \"tables\" property\n");
		return;
	}
	/* We only support complete, single width tables */
	if (tlen != 256) {
		printf(": invalid \"tables\" length %d\n", tlen);
		return;
	}

	if ((tlen = OF_getprop(node, "translation", transinfo,
	    sizeof(transinfo))) < 0) {
		printf(": couldn't find \"translation\" property\n");
		return;
	}
	if (tlen != (sc->sc_nchan * 4 * 4)) {
		printf(": invalid \"translation\" length %d\n", tlen);
		return;
	}

	cp = desc;
	for (i = 0; i < sc->sc_nchan; i++) {
		struct pcfadc_channel *chp = &sc->sc_channels[i];

		chp->chan_sensor.type = SENSOR_TEMP;

		if (cp >= desc + dlen) {
			printf(": invalid \"channels-description\"\n");
			return;
		}
		strlcpy(chp->chan_sensor.desc, cp,
		    sizeof(chp->chan_sensor.desc));
		cp += strlen(cp) + 1;

		/*
		 * We only support input temperature channels, with
		 * valid channel numbers, and basic (unscaled) translation
		 * 
		 * XXX TODO: support voltage (type 2) channels and type 4
		 * (scaled) translation tables
		 */
		if (chanuse[(i * 4)] > PCF8591_CHANNELS || /* channel # */
		    chanuse[(i * 4) + 1] != 0 ||	/* dir == input */
		    chanuse[(i * 4) + 2] != 1 ||	/* type == temp */
		    transinfo[(i * 4)] != 3 ||		/* xlate == table */
		    transinfo[(i * 4) + 2] != 0 ||	/* no xlate offset */
		    transinfo[(i * 4) + 3] != 0x100) {	/* xlate tbl length */
			printf(": unsupported sensor %d\n", i);
			return;
		}
		chp->chan_num = chanuse[(i * 4)];
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

	if (sensor_task_register(sc, pcfadc_refresh, 5) == NULL) {
		printf(": unable to register update task\n");
		return;
	}

	sensordev_install(&sc->sc_sensordev);

	printf("\n");
}

void
pcfadc_refresh(void *arg)
{
	struct pcfadc_softc *sc = arg;
	u_int i;
	u_int8_t data[PCF8591_CHANNELS + 1];

	iic_acquire_bus(sc->sc_tag, 0);
	/* NB: first byte out is stale, so read num_channels + 1 */
	if (iic_exec(sc->sc_tag, I2C_OP_READ_WITH_STOP, sc->sc_addr,
	    NULL, 0, data, PCF8591_CHANNELS + 1, 0)) {
		iic_release_bus(sc->sc_tag, 0);
		return;
	}
	iic_release_bus(sc->sc_tag, 0);

	/* XXX: so far this only supports temperature channels */
	for (i = 0; i < sc->sc_nchan; i++) {
		struct pcfadc_channel *chp = &sc->sc_channels[i];

		chp->chan_sensor.value = 273150000 + 1000000 * 
		    sc->sc_xlate[data[1 + chp->chan_num]];
	}
}

