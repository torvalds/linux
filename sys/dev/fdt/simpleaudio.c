/*	$OpenBSD: simpleaudio.c,v 1.7 2022/10/28 15:09:45 kn Exp $	*/
/*
 * Copyright (c) 2020 Patrick Wildt <patrick@blueri.se>
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

#include <machine/fdt.h>

#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_misc.h>
#include <dev/ofw/ofw_pinctrl.h>

#include <sys/audioio.h>
#include <dev/audio_if.h>
#include <dev/midi_if.h>

struct simpleaudio_softc {
	struct device		sc_dev;
	int			sc_node;

	uint32_t		sc_mclk_fs;

	struct dai_device	*sc_dai_cpu;
	struct dai_device	*sc_dai_codec;
	struct dai_device	**sc_dai_aux;
	int			sc_dai_naux;
};

int simpleaudio_match(struct device *, void *, void *);
void simpleaudio_attach(struct device *, struct device *, void *);
void simpleaudio_attach_deferred(struct device *);
void simpleaudio_set_format(struct simpleaudio_softc *, uint32_t,
    uint32_t, uint32_t);

int simpleaudio_open(void *, int);
void simpleaudio_close(void *);
int simpleaudio_set_params(void *, int, int,
    struct audio_params *, struct audio_params *);
void *simpleaudio_allocm(void *, int, size_t, int, int);
void simpleaudio_freem(void *, void *, int);
int simpleaudio_set_port(void *, mixer_ctrl_t *);
int simpleaudio_get_port(void *, mixer_ctrl_t *);
int simpleaudio_query_devinfo(void *, mixer_devinfo_t *);
int simpleaudio_round_blocksize(void *, int);
size_t simpleaudio_round_buffersize(void *, int, size_t);
int simpleaudio_trigger_output(void *, void *, void *, int,
    void (*)(void *), void *, struct audio_params *);
int simpleaudio_trigger_input(void *, void *, void *, int,
    void (*)(void *), void *, struct audio_params *);
int simpleaudio_halt_output(void *);
int simpleaudio_halt_input(void *);

const struct audio_hw_if simpleaudio_hw_if = {
	.open = simpleaudio_open,
	.close = simpleaudio_close,
	.set_params = simpleaudio_set_params,
	.allocm = simpleaudio_allocm,
	.freem = simpleaudio_freem,
	.set_port = simpleaudio_set_port,
	.get_port = simpleaudio_get_port,
	.query_devinfo = simpleaudio_query_devinfo,
	.round_blocksize = simpleaudio_round_blocksize,
	.round_buffersize = simpleaudio_round_buffersize,
	.trigger_output = simpleaudio_trigger_output,
	.trigger_input = simpleaudio_trigger_input,
	.halt_output = simpleaudio_halt_output,
	.halt_input = simpleaudio_halt_input,
};

const struct cfattach simpleaudio_ca = {
	sizeof(struct simpleaudio_softc), simpleaudio_match, simpleaudio_attach
};

struct cfdriver simpleaudio_cd = {
	NULL, "simpleaudio", DV_DULL
};

int
simpleaudio_match(struct device *parent, void *match, void *aux)
{
	struct fdt_attach_args *faa = aux;

	return OF_is_compatible(faa->fa_node, "simple-audio-card");
}

void
simpleaudio_attach(struct device *parent, struct device *self, void *aux)
{
	struct simpleaudio_softc *sc = (struct simpleaudio_softc *)self;
	struct fdt_attach_args *faa = aux;

	printf("\n");

	pinctrl_byname(faa->fa_node, "default");

	sc->sc_node = faa->fa_node;
	config_defer(self, simpleaudio_attach_deferred);
}

void
simpleaudio_attach_deferred(struct device *self)
{
	struct simpleaudio_softc *sc = (struct simpleaudio_softc *)self;
	char format[16] = { 0 };
	uint32_t fmt, pol, clk;
	uint32_t *auxdevs;
	ssize_t len;
	int i, node;

	/* TODO: implement simple-audio-card,dai-link */
	if (OF_getnodebyname(sc->sc_node, "simple-audio-card,cpu") == 0 ||
	    OF_getnodebyname(sc->sc_node, "simple-audio-card,codec") == 0)
		return;

	sc->sc_mclk_fs = OF_getpropint(sc->sc_node,
	    "simple-audio-card,mclk-fs", 0);

	node = OF_getnodebyname(sc->sc_node, "simple-audio-card,cpu");
	sc->sc_dai_cpu = dai_byphandle(OF_getpropint(node, "sound-dai", 0));

	node = OF_getnodebyname(sc->sc_node, "simple-audio-card,codec");
	sc->sc_dai_codec = dai_byphandle(OF_getpropint(node, "sound-dai", 0));

	if (sc->sc_dai_cpu == NULL || sc->sc_dai_codec == NULL)
		return;

	OF_getprop(sc->sc_node, "simple-audio-card,format",
	    format, sizeof(format));
	if (!strcmp(format, "i2s"))
		fmt = DAI_FORMAT_I2S;
	else if (!strcmp(format, "right_j"))
		fmt = DAI_FORMAT_RJ;
	else if (!strcmp(format, "left_j"))
		fmt = DAI_FORMAT_LJ;
	else if (!strcmp(format, "dsp_a"))
		fmt = DAI_FORMAT_DSPA;
	else if (!strcmp(format, "dsp_b"))
		fmt = DAI_FORMAT_DSPB;
	else if (!strcmp(format, "ac97"))
		fmt = DAI_FORMAT_AC97;
	else if (!strcmp(format, "pdm"))
		fmt = DAI_FORMAT_PDM;
	else if (!strcmp(format, "msb"))
		fmt = DAI_FORMAT_MSB;
	else if (!strcmp(format, "lsb"))
		fmt = DAI_FORMAT_LSB;
	else
		return;

	pol = 0;
	if (OF_getproplen(sc->sc_node, "simple-audio-card,frame-inversion") == 0)
		pol |= DAI_POLARITY_IF;
	else
		pol |= DAI_POLARITY_NF;
	if (OF_getproplen(sc->sc_node, "simple-audio-card,bitclock-inversion") == 0)
		pol |= DAI_POLARITY_IB;
	else
		pol |= DAI_POLARITY_NB;

	clk = 0;
	if (OF_getproplen(sc->sc_node, "simple-audio-card,frame-master") == 0)
		clk |= DAI_CLOCK_CFM;
	else
		clk |= DAI_CLOCK_CFS;
	if (OF_getproplen(sc->sc_node, "simple-audio-card,bitclock-master") == 0)
		clk |= DAI_CLOCK_CBM;
	else
		clk |= DAI_CLOCK_CBS;

	len = OF_getproplen(sc->sc_node, "simple-audio-card,aux-devs");
	if (len > 0) {
		if (len % sizeof(uint32_t) != 0)
			return;

		auxdevs = malloc(len, M_TEMP, M_WAITOK);
		OF_getpropintarray(sc->sc_node, "simple-audio-card,aux-devs",
		    auxdevs, len);

		sc->sc_dai_naux = len / sizeof(uint32_t);
		sc->sc_dai_aux = mallocarray(sc->sc_dai_naux,
		    sizeof(struct dai_device *), M_DEVBUF, M_WAITOK | M_ZERO);

		for (i = 0; i < sc->sc_dai_naux; i++)
			sc->sc_dai_aux[i] = dai_byphandle(auxdevs[i]);

		free(auxdevs, M_TEMP, len);
	}

	simpleaudio_set_format(sc, fmt, pol, clk);

	audio_attach_mi(&simpleaudio_hw_if, sc, NULL, &sc->sc_dev);
}

void
simpleaudio_set_format(struct simpleaudio_softc *sc, uint32_t fmt, uint32_t pol,
    uint32_t clk)
{
	if (sc->sc_dai_cpu->dd_set_format)
		sc->sc_dai_cpu->dd_set_format(sc->sc_dai_cpu->dd_cookie,
		    fmt, pol, clk);
	if (sc->sc_dai_codec->dd_set_format)
		sc->sc_dai_codec->dd_set_format(sc->sc_dai_codec->dd_cookie,
		    fmt, pol, clk);
}

int
simpleaudio_open(void *cookie, int flags)
{
	struct simpleaudio_softc *sc = cookie;
	struct dai_device *dai;
	const struct audio_hw_if *hwif;
	int error, i;

	dai = sc->sc_dai_cpu;
	hwif = dai->dd_hw_if;
	if (hwif->open) {
		error = hwif->open(dai->dd_cookie, flags);
		if (error) {
			simpleaudio_close(cookie);
			return error;
		}
	}

	dai = sc->sc_dai_codec;
	hwif = dai->dd_hw_if;
	if (hwif->open) {
		error = hwif->open(dai->dd_cookie, flags);
		if (error) {
			simpleaudio_close(cookie);
			return error;
		}
	}

	for (i = 0; i < sc->sc_dai_naux; i++) {
		dai = sc->sc_dai_aux[i];
		hwif = dai->dd_hw_if;
		if (hwif->open) {
			error = hwif->open(dai->dd_cookie, flags);
			if (error) {
				simpleaudio_close(cookie);
				return error;
			}
		}
	}

	return 0;
}

void
simpleaudio_close(void *cookie)
{
	struct simpleaudio_softc *sc = cookie;
	struct dai_device *dai;
	const struct audio_hw_if *hwif;
	int i;

	for (i = 0; i < sc->sc_dai_naux; i++) {
		dai = sc->sc_dai_aux[i];
		hwif = dai->dd_hw_if;
		if (hwif->close)
			hwif->close(dai->dd_cookie);
	}

	dai = sc->sc_dai_codec;
	hwif = dai->dd_hw_if;
	if (hwif->close)
		hwif->close(dai->dd_cookie);

	dai = sc->sc_dai_cpu;
	hwif = dai->dd_hw_if;
	if (hwif->close)
		hwif->close(dai->dd_cookie);
}

int
simpleaudio_set_params(void *cookie, int setmode, int usemode,
    struct audio_params *play, struct audio_params *rec)
{
	struct simpleaudio_softc *sc = cookie;
	struct dai_device *dai;
	const struct audio_hw_if *hwif;
	uint32_t rate;
	int error;

	if (sc->sc_mclk_fs) {
		if (setmode & AUMODE_PLAY)
			rate = play->sample_rate * sc->sc_mclk_fs;
		else
			rate = rec->sample_rate * sc->sc_mclk_fs;

		dai = sc->sc_dai_codec;
		if (dai->dd_set_sysclk) {
			error = dai->dd_set_sysclk(dai->dd_cookie, rate);
			if (error)
				return error;
		}

		dai = sc->sc_dai_cpu;
		if (dai->dd_set_sysclk) {
			error = dai->dd_set_sysclk(dai->dd_cookie, rate);
			if (error)
				return error;
		}
	}

	dai = sc->sc_dai_cpu;
	hwif = dai->dd_hw_if;
	if (hwif->set_params) {
		error = hwif->set_params(dai->dd_cookie,
		    setmode, usemode, play, rec);
		if (error)
			return error;
	}

	dai = sc->sc_dai_codec;
	hwif = dai->dd_hw_if;
	if (hwif->set_params) {
		error = hwif->set_params(dai->dd_cookie,
		    setmode, usemode, play, rec);
		if (error)
			return error;
	}

	return 0;
}

void *
simpleaudio_allocm(void *cookie, int direction, size_t size, int type,
    int flags)
{
	struct simpleaudio_softc *sc = cookie;
	struct dai_device *dai = sc->sc_dai_cpu;
	const struct audio_hw_if *hwif = dai->dd_hw_if;

	if (hwif->allocm)
		return hwif->allocm(dai->dd_cookie,
		    direction, size, type, flags);

	return NULL;
}

void
simpleaudio_freem(void *cookie, void *addr, int type)
{
	struct simpleaudio_softc *sc = cookie;
	struct dai_device *dai = sc->sc_dai_cpu;
	const struct audio_hw_if *hwif = dai->dd_hw_if;

	if (hwif->freem)
		hwif->freem(dai->dd_cookie, addr, type);
}

int
simpleaudio_set_port(void *cookie, mixer_ctrl_t *cp)
{
	struct simpleaudio_softc *sc = cookie;
	struct dai_device *dai = sc->sc_dai_codec;
	const struct audio_hw_if *hwif = dai->dd_hw_if;

	if (hwif->set_port)
		return hwif->set_port(dai->dd_cookie, cp);

	return ENXIO;
}

int
simpleaudio_get_port(void *cookie, mixer_ctrl_t *cp)
{
	struct simpleaudio_softc *sc = cookie;
	struct dai_device *dai = sc->sc_dai_codec;
	const struct audio_hw_if *hwif = dai->dd_hw_if;

	if (hwif->get_port)
		return hwif->get_port(dai->dd_cookie, cp);

	return ENXIO;
}

int
simpleaudio_query_devinfo(void *cookie, mixer_devinfo_t *dip)
{
	struct simpleaudio_softc *sc = cookie;
	struct dai_device *dai = sc->sc_dai_codec;
	const struct audio_hw_if *hwif = dai->dd_hw_if;

	if (hwif->query_devinfo)
		return hwif->query_devinfo(dai->dd_cookie, dip);

	return ENXIO;
}

int
simpleaudio_round_blocksize(void *cookie, int block)
{
	struct simpleaudio_softc *sc = cookie;
	struct dai_device *dai = sc->sc_dai_cpu;
	const struct audio_hw_if *hwif = dai->dd_hw_if;

	if (hwif->round_blocksize)
		return hwif->round_blocksize(dai->dd_cookie, block);

	return block;
}

size_t
simpleaudio_round_buffersize(void *cookie, int direction, size_t bufsize)
{
	struct simpleaudio_softc *sc = cookie;
	struct dai_device *dai = sc->sc_dai_cpu;
	const struct audio_hw_if *hwif = dai->dd_hw_if;

	if (hwif->round_buffersize)
		return hwif->round_buffersize(dai->dd_cookie,
		    direction, bufsize);

	return bufsize;
}

int
simpleaudio_trigger_output(void *cookie, void *start, void *end, int blksize,
    void (*intr)(void *), void *arg, struct audio_params *param)
{
	struct simpleaudio_softc *sc = cookie;
	struct dai_device *dai;
	const struct audio_hw_if *hwif;
	int error, i;

	for (i = 0; i < sc->sc_dai_naux; i++) {
		dai = sc->sc_dai_aux[i];
		hwif = dai->dd_hw_if;
		if (hwif->trigger_output) {
			error = hwif->trigger_output(dai->dd_cookie,
			    start, end, blksize, intr, arg, param);
			if (error) {
				simpleaudio_halt_output(cookie);
				return error;
			}
		}
	}

	dai = sc->sc_dai_codec;
	hwif = dai->dd_hw_if;
	if (hwif->trigger_output) {
		error = hwif->trigger_output(dai->dd_cookie,
		    start, end, blksize, intr, arg, param);
		if (error) {
			simpleaudio_halt_output(cookie);
			return error;
		}
	}

	dai = sc->sc_dai_cpu;
	hwif = dai->dd_hw_if;
	if (hwif->trigger_output) {
		error = hwif->trigger_output(dai->dd_cookie,
		    start, end, blksize, intr, arg, param);
		if (error) {
			simpleaudio_halt_output(cookie);
			return error;
		}
	}

	return 0;
}

int
simpleaudio_trigger_input(void *cookie, void *start, void *end, int blksize,
    void (*intr)(void *), void *arg, struct audio_params *param)
{
	struct simpleaudio_softc *sc = cookie;
	struct dai_device *dai;
	const struct audio_hw_if *hwif;
	int error, i;

	for (i = 0; i < sc->sc_dai_naux; i++) {
		dai = sc->sc_dai_aux[i];
		hwif = dai->dd_hw_if;
		if (hwif->trigger_input) {
			error = hwif->trigger_input(dai->dd_cookie,
			    start, end, blksize, intr, arg, param);
			if (error) {
				simpleaudio_halt_input(cookie);
				return error;
			}
		}
	}

	dai = sc->sc_dai_codec;
	hwif = dai->dd_hw_if;
	if (hwif->trigger_input) {
		error = hwif->trigger_input(dai->dd_cookie,
		    start, end, blksize, intr, arg, param);
		if (error) {
			simpleaudio_halt_input(cookie);
			return error;
		}
	}

	dai = sc->sc_dai_cpu;
	hwif = dai->dd_hw_if;
	if (hwif->trigger_input) {
		error = hwif->trigger_input(dai->dd_cookie,
		    start, end, blksize, intr, arg, param);
		if (error) {
			simpleaudio_halt_input(cookie);
			return error;
		}
	}

	return 0;
}

int
simpleaudio_halt_output(void *cookie)
{
	struct simpleaudio_softc *sc = cookie;
	struct dai_device *dai;
	const struct audio_hw_if *hwif;
	int i;

	for (i = 0; i < sc->sc_dai_naux; i++) {
		dai = sc->sc_dai_aux[i];
		hwif = dai->dd_hw_if;
		if (hwif->halt_output)
			hwif->halt_output(dai->dd_cookie);
	}

	dai = sc->sc_dai_codec;
	hwif = dai->dd_hw_if;
	if (hwif->halt_output)
		hwif->halt_output(dai->dd_cookie);

	dai = sc->sc_dai_cpu;
	hwif = dai->dd_hw_if;
	if (hwif->halt_output)
		hwif->halt_output(dai->dd_cookie);

	return 0;
}

int
simpleaudio_halt_input(void *cookie)
{
	struct simpleaudio_softc *sc = cookie;
	struct dai_device *dai;
	const struct audio_hw_if *hwif;
	int i;

	for (i = 0; i < sc->sc_dai_naux; i++) {
		dai = sc->sc_dai_aux[i];
		hwif = dai->dd_hw_if;
		if (hwif->halt_input)
			hwif->halt_input(dai->dd_cookie);
	}

	dai = sc->sc_dai_codec;
	hwif = dai->dd_hw_if;
	if (hwif->halt_input)
		hwif->halt_input(dai->dd_cookie);

	dai = sc->sc_dai_cpu;
	hwif = dai->dd_hw_if;
	if (hwif->halt_input)
		hwif->halt_input(dai->dd_cookie);

	return 0;
}
