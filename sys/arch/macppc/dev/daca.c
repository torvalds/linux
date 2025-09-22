/*	$OpenBSD: daca.c,v 1.15 2022/10/26 20:19:07 kn Exp $	*/

/*-
 * Copyright (c) 2002,2003 Tsubai Masanari.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * Datasheet is available from
 * http://www.indata.si/grega/pdfs/dac3550a.pdf
 */

#include <sys/param.h>
#include <sys/audioio.h>
#include <sys/device.h>
#include <sys/systm.h>

#include <dev/audio_if.h>
#include <dev/ofw/openfirm.h>
#include <macppc/dev/dbdma.h>

#include <machine/autoconf.h>

#include <macppc/dev/i2svar.h>

#ifdef DACA_DEBUG
# define DPRINTF printf
#else
# define DPRINTF while (0) printf
#endif

/* XXX */
#define daca_softc i2s_softc

/* XXX */
int kiic_write(struct device *, int, int, const void *, int);
int kiic_writereg(struct device *, int, u_int);

int daca_match(struct device *, void *, void *);
void daca_attach(struct device *, struct device *, void *);
void daca_defer(struct device *);
void daca_init(struct daca_softc *);
void daca_set_volume(struct daca_softc *, int, int);

const struct cfattach daca_ca = {
	sizeof(struct daca_softc), daca_match, daca_attach
};

struct cfdriver daca_cd = {
	NULL, "daca", DV_DULL
};

const struct audio_hw_if daca_hw_if = {
	.open = i2s_open,
	.close = i2s_close,
	.set_params = i2s_set_params,
	.round_blocksize = i2s_round_blocksize,
	.halt_output = i2s_halt_output,
	.halt_input = i2s_halt_input,
	.set_port = i2s_set_port,
	.get_port = i2s_get_port,
	.query_devinfo = i2s_query_devinfo,
	.allocm = i2s_allocm,
	.round_buffersize = i2s_round_buffersize,
	.trigger_output = i2s_trigger_output,
	.trigger_input = i2s_trigger_input,
};

/* DAC3550A registers */
#define DEQ_SR		0x01	/* Sample rate control (8) */
#define DEQ_AVOL	0x02	/* Analog volume (16) */
#define DEQ_GCFG	0x03	/* Global configuration (8) */

int
daca_match(struct device *parent, void *match, void *aux)
{
	struct confargs *ca = aux;
	int soundbus, soundchip;
	char compat[32];

	if (strcmp(ca->ca_name, "i2s") != 0)
		return (0);

	if ((soundbus = OF_child(ca->ca_node)) == 0 ||
	    (soundchip = OF_child(soundbus)) == 0)
		return (0);

	bzero(compat, sizeof compat);
	OF_getprop(soundchip, "compatible", compat, sizeof compat);

	if (strcmp(compat, "daca") != 0)
		return (0);

	return (1);
}

#define DEQaddr 0x9a

void
daca_attach(struct device *parent,struct device *self, void *aux)
{
	struct daca_softc *sc = (struct daca_softc *)self;

	sc->sc_setvolume = daca_set_volume;

	i2s_attach(parent, sc, aux);
	config_defer(self, daca_defer);
}

void
daca_defer(struct device *dev)
{
	struct daca_softc *sc = (struct daca_softc *)dev;
	struct device *dv;

	TAILQ_FOREACH(dv, &alldevs, dv_list)
		if (strcmp(dv->dv_cfdata->cf_driver->cd_name, "kiic") == 0 &&
		    strcmp(dv->dv_parent->dv_cfdata->cf_driver->cd_name, "macobio") == 0)
			sc->sc_i2c = dv;
	if (sc->sc_i2c == NULL) {
		printf("%s: unable to find i2c\n", sc->sc_dev.dv_xname);
		return;
	}

	/* XXX If i2c has failed to attach, what should we do? */

	audio_attach_mi(&daca_hw_if, sc, NULL, &sc->sc_dev);

	daca_init(sc);
}

void
daca_init(struct daca_softc *sc)
{
	i2s_set_rate(sc, 44100);
	kiic_writereg(sc->sc_i2c, 4, 0x01 | 0x02 | 0x04);
}

void
daca_set_volume(struct daca_softc *sc, int left, int right)
{
	u_int16_t data;

	sc->sc_vol_l = left;
	sc->sc_vol_r = right;

	left >>= 2;
	right >>= 2;
	data = left << 8 | right;
	kiic_write(sc->sc_i2c, DEQaddr, DEQ_AVOL, &data, 2);
}
