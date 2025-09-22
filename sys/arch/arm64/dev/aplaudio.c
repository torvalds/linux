/*	$OpenBSD: aplaudio.c,v 1.6 2023/01/14 23:35:09 kettenis Exp $	*/
/*
 * Copyright (c) 2022 Mark Kettenis <kettenis@openbsd.org>
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

#include <machine/bus.h>
#include <machine/fdt.h>

#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_misc.h>
#include <dev/ofw/fdt.h>

#include <sys/audioio.h>
#include <dev/audio_if.h>

#include <arm64/dev/aplmca.h>

struct aplaudio_softc {
	struct device		sc_dev;

	struct dai_device	*sc_dai_cpu;
	struct dai_device	*sc_dai_codec[6];
};

void	aplaudio_set_format(struct aplaudio_softc *, uint32_t,
	    uint32_t, uint32_t);
void	aplaudio_set_tdm_slots(struct aplaudio_softc *);

int	aplaudio_open(void *, int);
void	aplaudio_close(void *);
int	aplaudio_set_params(void *, int, int,
	    struct audio_params *, struct audio_params *);
void	*aplaudio_allocm(void *, int, size_t, int, int);
void	aplaudio_freem(void *, void *, int);
int	aplaudio_set_port(void *, mixer_ctrl_t *);
int	aplaudio_get_port(void *, mixer_ctrl_t *);
int	aplaudio_query_devinfo(void *, mixer_devinfo_t *);
int	aplaudio_round_blocksize(void *, int);
size_t	aplaudio_round_buffersize(void *, int, size_t);
int	aplaudio_trigger_output(void *, void *, void *, int,
	    void (*)(void *), void *, struct audio_params *);
int	aplaudio_trigger_input(void *, void *, void *, int,
	    void (*)(void *), void *, struct audio_params *);
int	aplaudio_halt_output(void *);
int	aplaudio_halt_input(void *);

const struct audio_hw_if aplaudio_hw_if = {
	.open = aplaudio_open,
	.close = aplaudio_close,
	.set_params = aplaudio_set_params,
	.allocm = aplaudio_allocm,
	.freem = aplaudio_freem,
	.set_port = aplaudio_set_port,
	.get_port = aplaudio_get_port,
	.query_devinfo = aplaudio_query_devinfo,
	.round_blocksize = aplaudio_round_blocksize,
	.round_buffersize = aplaudio_round_buffersize,
	.trigger_output = aplaudio_trigger_output,
	.trigger_input = aplaudio_trigger_input,
	.halt_output = aplaudio_halt_output,
	.halt_input = aplaudio_halt_input,
};

int	aplaudio_match(struct device *, void *, void *);
void	aplaudio_attach(struct device *, struct device *, void *);

const struct cfattach aplaudio_ca = {
	sizeof (struct aplaudio_softc), aplaudio_match, aplaudio_attach
};

struct cfdriver aplaudio_cd = {
	NULL, "aplaudio", DV_DULL
};

int
aplaudio_match(struct device *parent, void *match, void *aux)
{
	struct fdt_attach_args *faa = aux;

	return OF_is_compatible(faa->fa_node, "apple,macaudio");
}

void
aplaudio_attach(struct device *parent, struct device *self, void *aux)
{
	struct aplaudio_softc *sc = (struct aplaudio_softc *)self;
	struct fdt_attach_args *faa = aux;
	uint32_t fmt, pol, clk;
	uint32_t node, cpu, codec;
	uint32_t *dais;
	char status[32];
	int count = 0;
	int i, ncells;
	int len;

	printf("\n");

	for (node = OF_child(faa->fa_node); node; node = OF_peer(node)) {
		if (OF_getprop(node, "status", status, sizeof(status)) > 0 &&
		    strcmp(status, "disabled") == 0)
			continue;

		cpu = OF_getnodebyname(node, "cpu");
		if (cpu == 0)
			continue;

		sc->sc_dai_cpu = aplmca_alloc_cluster(cpu);
		if (sc->sc_dai_cpu == NULL)
			continue;

		codec = OF_getnodebyname(node, "codec");
		if (codec == 0)
			continue;

		len = OF_getproplen(codec, "sound-dai");
		if (len < 0)
			continue;

		dais = malloc(len, M_TEMP, M_WAITOK);
		OF_getpropintarray(codec, "sound-dai", dais, len);

		ncells = len / sizeof(uint32_t);
		ncells = MIN(ncells, nitems(sc->sc_dai_codec));
		for (i = 0; i < ncells; i++) {
			sc->sc_dai_codec[i] = dai_byphandle(dais[i]);
			if (sc->sc_dai_codec[i] == NULL)
				continue;
			count++;
		}

		free(dais, M_TEMP, len);

		if (count == 0)
			continue;
		if (count > 1)
			aplaudio_set_tdm_slots(sc);

		/* XXX Parameters are missing from the device tree? */
		fmt = DAI_FORMAT_LJ;
		pol = 0;
		clk = DAI_CLOCK_CFM | DAI_CLOCK_CBM;
		aplaudio_set_format(sc, fmt, pol, clk);

		audio_attach_mi(&aplaudio_hw_if, sc, NULL, self);

		/* XXX Only attach the first set of interfaces for now. */
		return;
	}
}

void
aplaudio_set_format(struct aplaudio_softc *sc, uint32_t fmt, uint32_t pol,
    uint32_t clk)
{
	struct dai_device *dai;
	int i;

	if (sc->sc_dai_cpu->dd_set_format)
		sc->sc_dai_cpu->dd_set_format(sc->sc_dai_cpu->dd_cookie,
		    fmt, pol, clk);
	for (i = 0; i < nitems(sc->sc_dai_codec); i++) {
		dai = sc->sc_dai_codec[i];
		if (dai == NULL)
			continue;
		if (dai->dd_set_format)
			dai->dd_set_format(dai->dd_cookie, fmt, pol, clk);
	}
}

void
aplaudio_set_tdm_slots(struct aplaudio_softc *sc)
{
	struct dai_device *dai;
	int i;

	for (i = 0; i < nitems(sc->sc_dai_codec); i++) {
		dai = sc->sc_dai_codec[i];
		if (dai == NULL)
			continue;
		if (dai->dd_set_tdm_slot) {
			char prefix[8];
			int slot = 0;

			if (OF_getprop(dai->dd_node, "sound-name-prefix",
			    prefix, sizeof(prefix)) > 0) {
				if (strncmp(prefix, "Right", 5) == 0)
					slot = 1;
			}

			dai->dd_set_tdm_slot(dai->dd_cookie, slot);
		}
	}
}

int
aplaudio_open(void *cookie, int flags)
{
	struct aplaudio_softc *sc = cookie;
	struct dai_device *dai;
	const struct audio_hw_if *hwif;
	int error;
	int i;

	dai = sc->sc_dai_cpu;
	hwif = dai->dd_hw_if;
	if (hwif->open) {
		error = hwif->open(dai->dd_cookie, flags);
		if (error) {
			aplaudio_close(cookie);
			return error;
		}
	}

	for (i = 0; i < nitems(sc->sc_dai_codec); i++) {
		dai = sc->sc_dai_codec[i];
		if (dai == NULL)
			continue;
		hwif = dai->dd_hw_if;
		if (hwif->open) {
			error = hwif->open(dai->dd_cookie, flags);
			if (error) {
				aplaudio_close(cookie);
				return error;
			}
		}
	}

	return 0;
}

void
aplaudio_close(void *cookie)
{
	struct aplaudio_softc *sc = cookie;
	struct dai_device *dai;
	const struct audio_hw_if *hwif;
	int i;

	for (i = 0; i < nitems(sc->sc_dai_codec); i++) {
		dai = sc->sc_dai_codec[i];
		if (dai == NULL)
			continue;
		hwif = dai->dd_hw_if;
		if (hwif->close)
			hwif->close(dai->dd_cookie);
	}

	dai = sc->sc_dai_cpu;
	hwif = dai->dd_hw_if;
	if (hwif->close)
		hwif->close(dai->dd_cookie);
}

int
aplaudio_set_params(void *cookie, int setmode, int usemode,
    struct audio_params *play, struct audio_params *rec)
{
	struct aplaudio_softc *sc = cookie;
	struct dai_device *dai;
	const struct audio_hw_if *hwif;
	uint32_t rate;
	int error;
	int i;

	dai = sc->sc_dai_cpu;
	hwif = dai->dd_hw_if;
	if (hwif->set_params) {
		error = hwif->set_params(dai->dd_cookie,
		    setmode, usemode, play, rec);
		if (error)
			return error;
	}

	if (setmode & AUMODE_PLAY)
		rate = play->sample_rate * play->channels * play->bps * 8;
	else
		rate = rec->sample_rate * rec->channels * rec->bps * 8;

	for (i = 0; i < nitems(sc->sc_dai_codec); i++) {
		dai = sc->sc_dai_codec[i];
		if (dai == NULL)
			continue;
		if (dai->dd_set_sysclk) {
			error = dai->dd_set_sysclk(dai->dd_cookie, rate);
			if (error)
				return error;
		}
	}

	dai = sc->sc_dai_cpu;
	if (dai->dd_set_sysclk) {
		error = dai->dd_set_sysclk(dai->dd_cookie, rate);
		if (error)
			return error;
	}

	return 0;
}

void *
aplaudio_allocm(void *cookie, int direction, size_t size, int type,
    int flags)
{
	struct aplaudio_softc *sc = cookie;
	struct dai_device *dai = sc->sc_dai_cpu;
	const struct audio_hw_if *hwif = dai->dd_hw_if;

	if (hwif->allocm)
		return hwif->allocm(dai->dd_cookie,
		    direction, size, type, flags);

	return NULL;
}

void
aplaudio_freem(void *cookie, void *addr, int type)
{
	struct aplaudio_softc *sc = cookie;
	struct dai_device *dai = sc->sc_dai_cpu;
	const struct audio_hw_if *hwif = dai->dd_hw_if;

	if (hwif->freem)
		hwif->freem(dai->dd_cookie, addr, type);
}

int
aplaudio_set_port(void *cookie, mixer_ctrl_t *cp)
{
	struct aplaudio_softc *sc = cookie;
	struct dai_device *dai;
	const struct audio_hw_if *hwif;
	int error = ENXIO;
	int i;

	for (i = 0; i < nitems(sc->sc_dai_codec); i++) {
		dai = sc->sc_dai_codec[i];
		if (dai == NULL)
			continue;
		hwif = dai->dd_hw_if;
		if (hwif->set_port)
			error = hwif->set_port(dai->dd_cookie, cp);
	}

	return error;
}

int
aplaudio_get_port(void *cookie, mixer_ctrl_t *cp)
{
	struct aplaudio_softc *sc = cookie;
	struct dai_device *dai;
	const struct audio_hw_if *hwif;
	int error = ENXIO;
	int i;

	for (i = 0; i < nitems(sc->sc_dai_codec); i++) {
		dai = sc->sc_dai_codec[i];
		if (dai == NULL)
			continue;
		hwif = dai->dd_hw_if;
		if (hwif->get_port)
			error = hwif->get_port(dai->dd_cookie, cp);
	}

	return error;
}

int
aplaudio_query_devinfo(void *cookie, mixer_devinfo_t *dip)
{
	struct aplaudio_softc *sc = cookie;
	struct dai_device *dai;
	const struct audio_hw_if *hwif;
	int i;

	for (i = 0; i < nitems(sc->sc_dai_codec); i++) {
		dai = sc->sc_dai_codec[i];
		if (dai == NULL)
			continue;
		hwif = dai->dd_hw_if;
		if (hwif->query_devinfo)
			return hwif->query_devinfo(dai->dd_cookie, dip);
	}

	return ENXIO;
}

int
aplaudio_round_blocksize(void *cookie, int block)
{
	struct aplaudio_softc *sc = cookie;
	struct dai_device *dai = sc->sc_dai_cpu;
	const struct audio_hw_if *hwif = dai->dd_hw_if;

	if (hwif->round_blocksize)
		return hwif->round_blocksize(dai->dd_cookie, block);

	return block;
}

size_t
aplaudio_round_buffersize(void *cookie, int direction, size_t bufsize)
{
	struct aplaudio_softc *sc = cookie;
	struct dai_device *dai = sc->sc_dai_cpu;
	const struct audio_hw_if *hwif = dai->dd_hw_if;

	if (hwif->round_buffersize)
		return hwif->round_buffersize(dai->dd_cookie,
		    direction, bufsize);

	return bufsize;
}

int
aplaudio_trigger_output(void *cookie, void *start, void *end, int blksize,
    void (*intr)(void *), void *arg, struct audio_params *param)
{
	struct aplaudio_softc *sc = cookie;
	struct dai_device *dai;
	const struct audio_hw_if *hwif;
	int error;
	int i;

	for (i = 0; i < nitems(sc->sc_dai_codec); i++) {
		dai = sc->sc_dai_codec[i];
		if (dai == NULL)
			continue;
		hwif = dai->dd_hw_if;
		if (hwif->trigger_output) {
			error = hwif->trigger_output(dai->dd_cookie,
			    start, end, blksize, intr, arg, param);
			if (error) {
				aplaudio_halt_output(cookie);
				return error;
			}
		}
	}

	dai = sc->sc_dai_cpu;
	hwif = dai->dd_hw_if;
	if (hwif->trigger_output) {
		error = hwif->trigger_output(dai->dd_cookie,
		    start, end, blksize, intr, arg, param);
		if (error) {
			aplaudio_halt_output(cookie);
			return error;
		}
	}

	return 0;
}

int
aplaudio_trigger_input(void *cookie, void *start, void *end, int blksize,
    void (*intr)(void *), void *arg, struct audio_params *param)
{
	struct aplaudio_softc *sc = cookie;
	struct dai_device *dai;
	const struct audio_hw_if *hwif;
	int error;
	int i;

	for (i = 0; i < nitems(sc->sc_dai_codec); i++) {
		dai = sc->sc_dai_codec[i];
		if (dai == NULL)
			continue;
		hwif = dai->dd_hw_if;
		if (hwif->trigger_input) {
			error = hwif->trigger_input(dai->dd_cookie,
			    start, end, blksize, intr, arg, param);
			if (error) {
				aplaudio_halt_input(cookie);
				return error;
			}
		}
	}

	dai = sc->sc_dai_cpu;
	hwif = dai->dd_hw_if;
	if (hwif->trigger_input) {
		error = hwif->trigger_input(dai->dd_cookie,
		    start, end, blksize, intr, arg, param);
		if (error) {
			aplaudio_halt_input(cookie);
			return error;
		}
	}

	return 0;
}

int
aplaudio_halt_output(void *cookie)
{
	struct aplaudio_softc *sc = cookie;
	struct dai_device *dai;
	const struct audio_hw_if *hwif;
	int i;

	for (i = 0; i < nitems(sc->sc_dai_codec); i++) {
		dai = sc->sc_dai_codec[i];
		if (dai == NULL)
			continue;
		hwif = dai->dd_hw_if;
		if (hwif->halt_output)
			hwif->halt_output(dai->dd_cookie);
	}

	dai = sc->sc_dai_cpu;
	hwif = dai->dd_hw_if;
	if (hwif->halt_output)
		hwif->halt_output(dai->dd_cookie);

	return 0;
}

int
aplaudio_halt_input(void *cookie)
{
	struct aplaudio_softc *sc = cookie;
	struct dai_device *dai;
	const struct audio_hw_if *hwif;
	int i;

	for (i = 0; i < nitems(sc->sc_dai_codec); i++) {
		dai = sc->sc_dai_codec[i];
		if (dai == NULL)
			continue;
		hwif = dai->dd_hw_if;
		if (hwif->halt_input)
			hwif->halt_input(dai->dd_cookie);
	}

	dai = sc->sc_dai_cpu;
	hwif = dai->dd_hw_if;
	if (hwif->halt_input)
		hwif->halt_input(dai->dd_cookie);

	return 0;
}
