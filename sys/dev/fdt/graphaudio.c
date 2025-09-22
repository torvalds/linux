/*	$OpenBSD: graphaudio.c,v 1.5 2022/10/28 15:09:45 kn Exp $	*/
/*
 * Copyright (c) 2020 Patrick Wildt <patrick@blueri.se>
 * Copyright (c) 2021 Mark Kettenis <kettenis@openbsd.org>
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

#include <sys/audioio.h>
#include <dev/audio_if.h>
#include <dev/midi_if.h>

struct graphaudio_softc {
	struct device		sc_dev;
	int			sc_node;

	uint32_t		sc_mclk_fs;

	struct dai_device	*sc_dai_cpu;
	struct dai_device	*sc_dai_codec;
};

int	graphaudio_match(struct device *, void *, void *);
void	graphaudio_attach(struct device *, struct device *, void *);
void	graphaudio_attach_deferred(struct device *);
void	graphaudio_set_format(struct graphaudio_softc *, uint32_t,
	    uint32_t, uint32_t);

int	graphaudio_open(void *, int);
void	graphaudio_close(void *);
int	graphaudio_set_params(void *, int, int,
	    struct audio_params *, struct audio_params *);
void	*graphaudio_allocm(void *, int, size_t, int, int);
void	graphaudio_freem(void *, void *, int);
int	graphaudio_set_port(void *, mixer_ctrl_t *);
int	graphaudio_get_port(void *, mixer_ctrl_t *);
int	graphaudio_query_devinfo(void *, mixer_devinfo_t *);
int	graphaudio_round_blocksize(void *, int);
size_t	graphaudio_round_buffersize(void *, int, size_t);
int	graphaudio_trigger_output(void *, void *, void *, int,
	    void (*)(void *), void *, struct audio_params *);
int	graphaudio_trigger_input(void *, void *, void *, int,
	    void (*)(void *), void *, struct audio_params *);
int	graphaudio_halt_output(void *);
int	graphaudio_halt_input(void *);

const struct audio_hw_if graphaudio_hw_if = {
	.open = graphaudio_open,
	.close = graphaudio_close,
	.set_params = graphaudio_set_params,
	.allocm = graphaudio_allocm,
	.freem = graphaudio_freem,
	.set_port = graphaudio_set_port,
	.get_port = graphaudio_get_port,
	.query_devinfo = graphaudio_query_devinfo,
	.round_blocksize = graphaudio_round_blocksize,
	.round_buffersize = graphaudio_round_buffersize,
	.trigger_output = graphaudio_trigger_output,
	.trigger_input = graphaudio_trigger_input,
	.halt_output = graphaudio_halt_output,
	.halt_input = graphaudio_halt_input,
};

const struct cfattach graphaudio_ca = {
	sizeof(struct graphaudio_softc), graphaudio_match, graphaudio_attach
};

struct cfdriver graphaudio_cd = {
	NULL, "graphaudio", DV_DULL
};

int
graphaudio_match(struct device *parent, void *match, void *aux)
{
	struct fdt_attach_args *faa = aux;

	return OF_is_compatible(faa->fa_node, "audio-graph-card");
}

void
graphaudio_attach(struct device *parent, struct device *self, void *aux)
{
	struct graphaudio_softc *sc = (struct graphaudio_softc *)self;
	struct fdt_attach_args *faa = aux;

	printf("\n");

	sc->sc_node = faa->fa_node;
	config_defer(self, graphaudio_attach_deferred);
}

void
graphaudio_attach_deferred(struct device *self)
{
	struct graphaudio_softc *sc = (struct graphaudio_softc *)self;
	char format[16] = { 0 };
	uint32_t fmt, pol, clk;
	uint32_t dais;
	struct device_ports *dp;
	struct endpoint *ep, *rep;

	dais = OF_getpropint(sc->sc_node, "dais", 0);
	dp = device_ports_byphandle(dais);
	if (dp == NULL)
		return;

	ep = endpoint_byreg(dp, -1, -1);
	if (ep == NULL)
		return;

	rep = endpoint_remote(ep);
	if (rep == NULL)
		return;

	sc->sc_mclk_fs = OF_getpropint(ep->ep_node, "mclk-fs", 0);

	sc->sc_dai_cpu = endpoint_get_cookie(ep);
	sc->sc_dai_codec = endpoint_get_cookie(rep);

	if (sc->sc_dai_cpu == NULL || sc->sc_dai_codec == NULL)
		return;

	OF_getprop(ep->ep_node, "dai-format", format, sizeof(format));
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
	if (OF_getproplen(ep->ep_node, "frame-inversion") == 0)
		pol |= DAI_POLARITY_IF;
	else
		pol |= DAI_POLARITY_NF;
	if (OF_getproplen(ep->ep_node, "bitclock-inversion") == 0)
		pol |= DAI_POLARITY_IB;
	else
		pol |= DAI_POLARITY_NB;

	clk = 0;
	if (OF_getproplen(ep->ep_node, "frame-master") == 0)
		clk |= DAI_CLOCK_CFM;
	else
		clk |= DAI_CLOCK_CFS;
	if (OF_getproplen(ep->ep_node, "bitclock-master") == 0)
		clk |= DAI_CLOCK_CBM;
	else
		clk |= DAI_CLOCK_CBS;

	graphaudio_set_format(sc, fmt, pol, clk);

	audio_attach_mi(&graphaudio_hw_if, sc, NULL, &sc->sc_dev);
}

void
graphaudio_set_format(struct graphaudio_softc *sc, uint32_t fmt, uint32_t pol,
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
graphaudio_open(void *cookie, int flags)
{
	struct graphaudio_softc *sc = cookie;
	struct dai_device *dai;
	const struct audio_hw_if *hwif;
	int error;

	dai = sc->sc_dai_cpu;
	hwif = dai->dd_hw_if;
	if (hwif->open) {
		error = hwif->open(dai->dd_cookie, flags);
		if (error) {
			graphaudio_close(cookie);
			return error;
		}
	}

	dai = sc->sc_dai_codec;
	hwif = dai->dd_hw_if;
	if (hwif->open) {
		error = hwif->open(dai->dd_cookie, flags);
		if (error) {
			graphaudio_close(cookie);
			return error;
		}
	}

	return 0;
}

void
graphaudio_close(void *cookie)
{
	struct graphaudio_softc *sc = cookie;
	struct dai_device *dai;
	const struct audio_hw_if *hwif;

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
graphaudio_set_params(void *cookie, int setmode, int usemode,
    struct audio_params *play, struct audio_params *rec)
{
	struct graphaudio_softc *sc = cookie;
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
graphaudio_allocm(void *cookie, int direction, size_t size, int type,
    int flags)
{
	struct graphaudio_softc *sc = cookie;
	struct dai_device *dai = sc->sc_dai_cpu;
	const struct audio_hw_if *hwif = dai->dd_hw_if;

	if (hwif->allocm)
		return hwif->allocm(dai->dd_cookie,
		    direction, size, type, flags);

	return NULL;
}

void
graphaudio_freem(void *cookie, void *addr, int type)
{
	struct graphaudio_softc *sc = cookie;
	struct dai_device *dai = sc->sc_dai_cpu;
	const struct audio_hw_if *hwif = dai->dd_hw_if;

	if (hwif->freem)
		hwif->freem(dai->dd_cookie, addr, type);
}

int
graphaudio_set_port(void *cookie, mixer_ctrl_t *cp)
{
	struct graphaudio_softc *sc = cookie;
	struct dai_device *dai = sc->sc_dai_codec;
	const struct audio_hw_if *hwif = dai->dd_hw_if;

	if (hwif->set_port)
		return hwif->set_port(dai->dd_cookie, cp);

	return ENXIO;
}

int
graphaudio_get_port(void *cookie, mixer_ctrl_t *cp)
{
	struct graphaudio_softc *sc = cookie;
	struct dai_device *dai = sc->sc_dai_codec;
	const struct audio_hw_if *hwif = dai->dd_hw_if;

	if (hwif->get_port)
		return hwif->get_port(dai->dd_cookie, cp);

	return ENXIO;
}

int
graphaudio_query_devinfo(void *cookie, mixer_devinfo_t *dip)
{
	struct graphaudio_softc *sc = cookie;
	struct dai_device *dai = sc->sc_dai_codec;
	const struct audio_hw_if *hwif = dai->dd_hw_if;

	if (hwif->query_devinfo)
		return hwif->query_devinfo(dai->dd_cookie, dip);

	return ENXIO;
}

int
graphaudio_round_blocksize(void *cookie, int block)
{
	struct graphaudio_softc *sc = cookie;
	struct dai_device *dai = sc->sc_dai_cpu;
	const struct audio_hw_if *hwif = dai->dd_hw_if;

	if (hwif->round_blocksize)
		return hwif->round_blocksize(dai->dd_cookie, block);

	return block;
}

size_t
graphaudio_round_buffersize(void *cookie, int direction, size_t bufsize)
{
	struct graphaudio_softc *sc = cookie;
	struct dai_device *dai = sc->sc_dai_cpu;
	const struct audio_hw_if *hwif = dai->dd_hw_if;

	if (hwif->round_buffersize)
		return hwif->round_buffersize(dai->dd_cookie,
		    direction, bufsize);

	return bufsize;
}

int
graphaudio_trigger_output(void *cookie, void *start, void *end, int blksize,
    void (*intr)(void *), void *arg, struct audio_params *param)
{
	struct graphaudio_softc *sc = cookie;
	struct dai_device *dai;
	const struct audio_hw_if *hwif;
	int error;

	dai = sc->sc_dai_codec;
	hwif = dai->dd_hw_if;
	if (hwif->trigger_output) {
		error = hwif->trigger_output(dai->dd_cookie,
		    start, end, blksize, intr, arg, param);
		if (error) {
			graphaudio_halt_output(cookie);
			return error;
		}
	}

	dai = sc->sc_dai_cpu;
	hwif = dai->dd_hw_if;
	if (hwif->trigger_output) {
		error = hwif->trigger_output(dai->dd_cookie,
		    start, end, blksize, intr, arg, param);
		if (error) {
			graphaudio_halt_output(cookie);
			return error;
		}
	}

	return 0;
}

int
graphaudio_trigger_input(void *cookie, void *start, void *end, int blksize,
    void (*intr)(void *), void *arg, struct audio_params *param)
{
	struct graphaudio_softc *sc = cookie;
	struct dai_device *dai;
	const struct audio_hw_if *hwif;
	int error;

	dai = sc->sc_dai_codec;
	hwif = dai->dd_hw_if;
	if (hwif->trigger_input) {
		error = hwif->trigger_input(dai->dd_cookie,
		    start, end, blksize, intr, arg, param);
		if (error) {
			graphaudio_halt_input(cookie);
			return error;
		}
	}

	dai = sc->sc_dai_cpu;
	hwif = dai->dd_hw_if;
	if (hwif->trigger_input) {
		error = hwif->trigger_input(dai->dd_cookie,
		    start, end, blksize, intr, arg, param);
		if (error) {
			graphaudio_halt_input(cookie);
			return error;
		}
	}

	return 0;
}

int
graphaudio_halt_output(void *cookie)
{
	struct graphaudio_softc *sc = cookie;
	struct dai_device *dai;
	const struct audio_hw_if *hwif;

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
graphaudio_halt_input(void *cookie)
{
	struct graphaudio_softc *sc = cookie;
	struct dai_device *dai;
	const struct audio_hw_if *hwif;

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
