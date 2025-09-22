/*	$OpenBSD: pwmleds.c,v 1.2 2023/04/25 11:12:38 tobhe Exp $	*/
/*
 * Copyright (c) 2022 Mark Kettenis <kettenis@openbsd.org>
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

#include <dev/wscons/wsconsio.h>

extern int (*wskbd_get_backlight)(struct wskbd_backlight *);
extern int (*wskbd_set_backlight)(struct wskbd_backlight *);

struct pwmleds_softc {
	struct device		sc_dev;

	/* Keyboard backlight. */
	uint32_t		*sc_pwm;
	int			 sc_pwm_len;
	uint32_t		 sc_max_brightness;
	struct pwm_state	 sc_ps_saved;
};

int	pwmleds_match(struct device *, void *, void *);
void	pwmleds_attach(struct device *, struct device *, void *);
int	pwmleds_activate(struct device *, int);

const struct cfattach pwmleds_ca = {
	sizeof (struct pwmleds_softc), pwmleds_match, pwmleds_attach, NULL,
	pwmleds_activate
};

struct cfdriver pwmleds_cd = {
	NULL, "pwmleds", DV_DULL
};

int pwmleds_get_kbd_backlight(struct wskbd_backlight *);
int pwmleds_set_kbd_backlight(struct wskbd_backlight *);

int
pwmleds_match(struct device *parent, void *match, void *aux)
{
	const struct fdt_attach_args	*faa = aux;

	return OF_is_compatible(faa->fa_node, "pwm-leds");
}

void
pwmleds_attach(struct device *parent, struct device *self, void *aux)
{
	struct pwmleds_softc *sc = (struct pwmleds_softc *)self;
	struct fdt_attach_args	*faa = aux;
	char *function;
	int len, node;

	printf("\n");

	for (node = OF_child(faa->fa_node); node; node = OF_peer(node)) {
		len = OF_getproplen(node, "function");
		if (len <= 0)
			continue;

		function = malloc(len, M_TEMP, M_WAITOK);
		OF_getprop(node, "function", function, len);
		if (strcmp(function, "kbd_backlight") != 0) {
			free(function, M_TEMP, len);
			continue;
		}
		free(function, M_TEMP, len);

		len = OF_getproplen(node, "pwms");
		if (len <= 0)
			continue;

		sc->sc_pwm = malloc(len, M_DEVBUF, M_WAITOK);
		OF_getpropintarray(node, "pwms", sc->sc_pwm, len);
		sc->sc_pwm_len = len;

		sc->sc_max_brightness =
		    OF_getpropint(node, "max-brightness", 0);
		wskbd_get_backlight = pwmleds_get_kbd_backlight;
		wskbd_set_backlight = pwmleds_set_kbd_backlight;
	}
}

int
pwmleds_activate(struct device *self, int act)
{
	struct pwmleds_softc *sc = (struct pwmleds_softc *)self;
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

struct pwmleds_softc *
pwmleds_kbd_backlight(void)
{
	struct pwmleds_softc *sc;
	int i;

	for (i = 0; i < pwmleds_cd.cd_ndevs; i++) {
		sc = pwmleds_cd.cd_devs[i];
		if (sc == NULL)
			continue;
		if (sc->sc_max_brightness > 0)
			return sc;
	}

	return NULL;
}

int
pwmleds_get_kbd_backlight(struct wskbd_backlight *kbl)
{
	struct pwmleds_softc *sc;
	struct pwm_state ps;
	int error;

	sc = pwmleds_kbd_backlight();
	if (sc == NULL)
		return ENOTTY;

	error = pwm_get_state(sc->sc_pwm, &ps);
	if (error)
		return error;

	kbl->min = 0;
	kbl->max = sc->sc_max_brightness;
	kbl->curval = (ps.ps_enabled) ?
	    ((uint64_t)ps.ps_pulse_width * kbl->max) / ps.ps_period : 0;
	return 0;
}

int
pwmleds_set_kbd_backlight(struct wskbd_backlight *kbl)
{
	struct pwmleds_softc *sc;
	struct pwm_state ps;

	sc = pwmleds_kbd_backlight();
	if (sc == NULL)
		return ENOTTY;

	if (kbl->curval < 0 || kbl->curval > sc->sc_max_brightness)
		return EINVAL;

	pwm_init_state(sc->sc_pwm, &ps);
	ps.ps_pulse_width =
	    ((uint64_t)kbl->curval * ps.ps_period) / sc->sc_max_brightness;
	ps.ps_enabled = (ps.ps_pulse_width > 0);
	return pwm_set_state(sc->sc_pwm, &ps);
}
