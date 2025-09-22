/*	$OpenBSD: pwmfan.c,v 1.3 2025/09/16 08:46:33 kettenis Exp $	*/
/*
 * Copyright (c) 2019 Krystian Lewandowski
 * Copyright (c) 2019 Mark Kettenis <kettenis@openbsd.org>
 * Copyright (c) 2019 Patrick Wildt <patrick@blueri.se>
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
#include <machine/bus.h>

#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_gpio.h>
#include <dev/ofw/ofw_misc.h>
#include <dev/ofw/ofw_thermal.h>

struct pwmfan_softc {
	struct device		sc_dev;
	uint32_t		*sc_pwm;
	int			sc_pwm_len;
	uint32_t		*sc_levels;
	int			sc_nlevels;
	int			sc_curlevel;

	struct cooling_device	sc_cd;
};

int	pwmfan_match(struct device *, void *, void *);
void	pwmfan_attach(struct device *, struct device *, void *);

const struct cfattach pwmfan_ca = {
	sizeof(struct pwmfan_softc), pwmfan_match, pwmfan_attach
};

struct cfdriver pwmfan_cd = {
	NULL, "pwmfan", DV_DULL
};

uint32_t pwmfan_get_cooling_level(void *, uint32_t *);
void	pwmfan_set_cooling_level(void *, uint32_t *, uint32_t);

int
pwmfan_match(struct device *parent, void *match, void *aux)
{
	struct fdt_attach_args *faa = aux;

	return OF_is_compatible(faa->fa_node, "pwm-fan");
}

void
pwmfan_attach(struct device *parent, struct device *self, void *aux)
{
	struct pwmfan_softc *sc = (struct pwmfan_softc *)self;
	struct fdt_attach_args *faa = aux;
	int len;

	len = OF_getproplen(faa->fa_node, "pwms");
	if (len < 0) {
		printf(": no pwm\n");
		return;
	}

	sc->sc_pwm = malloc(len, M_DEVBUF, M_WAITOK);
	OF_getpropintarray(faa->fa_node, "pwms", sc->sc_pwm, len);
	sc->sc_pwm_len = len;

	len = OF_getproplen(faa->fa_node, "cooling-levels");
	if (len < 0) {
		free(sc->sc_pwm, M_DEVBUF, sc->sc_pwm_len);
		printf(": no cooling levels\n");
		return;
	}

	sc->sc_levels = malloc(len, M_DEVBUF, M_WAITOK);
	OF_getpropintarray(faa->fa_node, "cooling-levels",
	    sc->sc_levels, len);
	sc->sc_nlevels = len / sizeof(uint32_t);

	printf("\n");

	/* Start fan at maximum speed. */
	pwmfan_set_cooling_level(sc, NULL, sc->sc_nlevels - 1);

	sc->sc_cd.cd_node = faa->fa_node;
	sc->sc_cd.cd_cookie = sc;
	sc->sc_cd.cd_get_level = pwmfan_get_cooling_level;
	sc->sc_cd.cd_set_level = pwmfan_set_cooling_level;
	cooling_device_register(&sc->sc_cd);
}

uint32_t
pwmfan_get_cooling_level(void *cookie, uint32_t *cells)
{
	struct pwmfan_softc *sc = cookie;

	return sc->sc_curlevel;
}

void
pwmfan_set_cooling_level(void *cookie, uint32_t *cells, uint32_t level)
{
	struct pwmfan_softc *sc = cookie;
	struct pwm_state ps;

	if (level == sc->sc_curlevel || level > sc->sc_nlevels ||
	    sc->sc_levels[level] > 255)
		return;

	if (pwm_init_state(sc->sc_pwm, &ps))
		return;

	sc->sc_curlevel = level;
	level = sc->sc_levels[level];

	ps.ps_enabled = level ? 1 : 0;
	ps.ps_pulse_width = (ps.ps_period * level) / 255;
	pwm_set_state(sc->sc_pwm, &ps);
}
