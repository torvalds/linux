/*	$OpenBSD: pwmbl.c,v 1.9 2025/06/12 12:23:28 kettenis Exp $	*/
/*
 * Copyright (c) 2019 Krystian Lewandowski
 * Copyright (c) 2019 Mark Kettenis <kettenis@openbsd.org>
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

#include <dev/wscons/wsconsio.h>
#include <dev/wscons/wsdisplayvar.h>

struct pwmbl_softc {
	struct device		sc_dev;
	uint32_t		*sc_pwm;
	int			sc_pwm_len;
	uint32_t		*sc_levels;	/* NULL if simple ramp */
	int			sc_nlevels;
	uint64_t		sc_max_level;
	uint32_t		sc_def_idx;
	struct pwm_state	sc_ps_saved;
};

struct pwmbl_softc *sc_pwmbl;

int	pwmbl_match(struct device *, void *, void *);
void	pwmbl_attach(struct device *, struct device *, void *);
int	pwmbl_activate(struct device *, int);

const struct cfattach pwmbl_ca = {
	sizeof(struct pwmbl_softc), pwmbl_match, pwmbl_attach, NULL,
	pwmbl_activate
};

struct cfdriver pwmbl_cd = {
	NULL, "pwmbl", DV_DULL
};

void	pwmbl_interpolate(struct pwmbl_softc *, uint32_t);
int	pwmbl_get_brightness(void *, uint32_t *);
int	pwmbl_set_brightness(void *, uint32_t);
int	pwmbl_get_param(struct wsdisplay_param *);
int	pwmbl_set_param(struct wsdisplay_param *);

int
pwmbl_match(struct device *parent, void *match, void *aux)
{
	struct fdt_attach_args *faa = aux;

	return OF_is_compatible(faa->fa_node, "pwm-backlight");
}

void
pwmbl_attach(struct device *parent, struct device *self, void *aux)
{
	struct pwmbl_softc *sc = (struct pwmbl_softc *)self;
	struct fdt_attach_args *faa = aux;
	uint32_t *gpios;
	uint32_t nsteps;
	int len;

	len = OF_getproplen(faa->fa_node, "pwms");
	if (len < 0) {
		printf(": no pwm\n");
		return;
	}

	sc->sc_pwm = malloc(len, M_DEVBUF, M_WAITOK);
	OF_getpropintarray(faa->fa_node, "pwms", sc->sc_pwm, len);
	sc->sc_pwm_len = len;

	len = OF_getproplen(faa->fa_node, "enable-gpios");
	if (len > 0) {
		gpios = malloc(len, M_TEMP, M_WAITOK);
		OF_getpropintarray(faa->fa_node, "enable-gpios", gpios, len);
		gpio_controller_config_pin(&gpios[0], GPIO_CONFIG_OUTPUT);
		gpio_controller_set_pin(&gpios[0], 1);
		free(gpios, M_TEMP, len);
	}

	len = OF_getproplen(faa->fa_node, "brightness-levels");
	if (len >= (int)sizeof(uint32_t)) {
		sc->sc_levels = malloc(len, M_DEVBUF, M_WAITOK);
		OF_getpropintarray(faa->fa_node, "brightness-levels",
		    sc->sc_levels, len);
		sc->sc_nlevels = len / sizeof(uint32_t);
		sc->sc_max_level = sc->sc_levels[sc->sc_nlevels - 1];
		nsteps = OF_getpropint(faa->fa_node,
		    "num-interpolated-steps", 0);
		if (nsteps > 0)
			pwmbl_interpolate(sc, nsteps);
		sc->sc_def_idx = OF_getpropint(faa->fa_node,
		    "default-brightness-level", sc->sc_nlevels - 1);
		if (sc->sc_def_idx >= sc->sc_nlevels)
			sc->sc_def_idx = sc->sc_nlevels - 1;
	} else {
		/* No levels, assume a simple 0..255 ramp. */
		sc->sc_nlevels = 256;
		sc->sc_max_level = sc->sc_nlevels - 1;
		sc->sc_def_idx = sc->sc_nlevels - 1;
	}

	printf("\n");

	pwmbl_set_brightness(sc, sc->sc_def_idx);

	sc_pwmbl = sc;
	ws_get_param = pwmbl_get_param;
	ws_set_param = pwmbl_set_param;
}

int
pwmbl_activate(struct device *self, int act)
{
	struct pwmbl_softc *sc = (struct pwmbl_softc *)self;
	struct pwm_state ps;
	int error;

	switch (act) {
	case DVACT_QUIESCE:
		error = pwm_get_state(sc->sc_pwm, &sc->sc_ps_saved);
		if (error)
			return error;

		pwm_init_state(sc->sc_pwm, &ps);
		ps.ps_pulse_width = 0;
		ps.ps_enabled = 0;
		return pwm_set_state(sc->sc_pwm, &ps);
	case DVACT_WAKEUP:
		return pwm_set_state(sc->sc_pwm, &sc->sc_ps_saved);
	}
	return 0;
}

void
pwmbl_interpolate(struct pwmbl_softc *sc, uint32_t nsteps)
{
	uint32_t *levels;
	uint64_t delta;
	uint32_t base;
	int nlevels;
	int i, j;

	nlevels = (sc->sc_nlevels - 1) * nsteps + 1;
	levels = mallocarray(nlevels, sizeof(uint32_t), M_DEVBUF, M_WAITOK);
	for (i = 0; i < sc->sc_nlevels - 1; i++) {
		delta = sc->sc_levels[i + 1] - sc->sc_levels[i];
		base = sc->sc_levels[i];
		for (j = 0; j < nsteps; j++)
			levels[i * nsteps + j] = base + (j * delta) / nsteps;
	}
	levels[nlevels - 1] = sc->sc_levels[sc->sc_nlevels - 1];
	free(sc->sc_levels, M_DEVBUF, sc->sc_nlevels * sizeof(uint32_t));
	sc->sc_levels = levels;
	sc->sc_nlevels = nlevels;
}

uint32_t
pwmbl_find_idx(struct pwmbl_softc *sc, uint32_t level)
{
	uint32_t mid;
	int idx;

	if (sc->sc_levels == NULL)
		return level < sc->sc_nlevels ? level : sc->sc_nlevels - 1;

	for (idx = 0; idx < sc->sc_nlevels - 1; idx++) {
		mid = (sc->sc_levels[idx] + sc->sc_levels[idx + 1]) / 2;
		if (sc->sc_levels[idx] <= level && level <= mid)
			return idx;
		if (mid < level && level <= sc->sc_levels[idx + 1])
			return idx + 1;
	}
	if (level < sc->sc_levels[0])
		return 0;
	else
		return idx;
}

int
pwmbl_get_brightness(void *cookie, uint32_t *idx)
{
	struct pwmbl_softc *sc = cookie;
	struct pwm_state ps;
	uint64_t level;

	if (pwm_get_state(sc->sc_pwm, &ps))
		return EINVAL;

	level = (ps.ps_pulse_width * sc->sc_max_level) / ps.ps_period;
	*idx = pwmbl_find_idx(sc, level);
	return 0;
}

int
pwmbl_set_brightness(void *cookie, uint32_t idx)
{
	struct pwmbl_softc *sc = cookie;
	struct pwm_state ps;
	uint64_t level;

	if (pwm_init_state(sc->sc_pwm, &ps))
		return EINVAL;

	if (sc->sc_levels)
		level = sc->sc_levels[idx];
	else
		level = idx;

	ps.ps_enabled = 1;
	ps.ps_pulse_width = (ps.ps_period * level) / sc->sc_max_level;
	return pwm_set_state(sc->sc_pwm, &ps);
}

int
pwmbl_get_param(struct wsdisplay_param *dp)
{
	struct pwmbl_softc *sc = (struct pwmbl_softc *)sc_pwmbl;
	uint32_t level;

	switch (dp->param) {
	case WSDISPLAYIO_PARAM_BRIGHTNESS:
		if (pwmbl_get_brightness(sc, &level))
			return -1;

		dp->min = 0;
		dp->max = sc->sc_nlevels - 1;
		dp->curval = level;
		return 0;
	default:
		return -1;
	}
}

int
pwmbl_set_param(struct wsdisplay_param *dp)
{
	struct pwmbl_softc *sc = (struct pwmbl_softc *)sc_pwmbl;

	switch (dp->param) {
	case WSDISPLAYIO_PARAM_BRIGHTNESS:
		if (pwmbl_set_brightness(sc, dp->curval))
			return -1;
		return 0;
	default:
		return -1;
	}
}
