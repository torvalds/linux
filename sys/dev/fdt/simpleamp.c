/*	$OpenBSD: simpleamp.c,v 1.4 2022/04/06 18:59:28 naddy Exp $	*/
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
#include <dev/ofw/ofw_gpio.h>
#include <dev/ofw/ofw_misc.h>
#include <dev/ofw/ofw_regulator.h>

#include <sys/audioio.h>
#include <dev/audio_if.h>
#include <dev/midi_if.h>

int simpleamp_match(struct device *, void *, void *);
void simpleamp_attach(struct device *, struct device *, void *);

int simpleamp_open(void *, int);
void simpleamp_close(void *);

struct simpleamp_softc {
	struct device		sc_dev;
	struct dai_device	sc_dai;

	uint32_t		*sc_gpio;
	int			sc_gpiolen;
	uint32_t		sc_vcc;
};

const struct audio_hw_if simpleamp_hw_if = {
	.open = simpleamp_open,
	.close = simpleamp_close,
};

const struct cfattach simpleamp_ca = {
	sizeof(struct simpleamp_softc), simpleamp_match, simpleamp_attach
};

struct cfdriver simpleamp_cd = {
	NULL, "simpleamp", DV_DULL
};

int
simpleamp_match(struct device *parent, void *match, void *aux)
{
	struct fdt_attach_args *faa = aux;

	return OF_is_compatible(faa->fa_node, "simple-audio-amplifier");
}

void
simpleamp_attach(struct device *parent, struct device *self, void *aux)
{
	struct simpleamp_softc *sc = (struct simpleamp_softc *)self;
	struct fdt_attach_args *faa = aux;

	sc->sc_gpiolen = OF_getproplen(faa->fa_node, "enable-gpios");
	if (sc->sc_gpiolen > 0) {
		sc->sc_gpio = malloc(sc->sc_gpiolen, M_DEVBUF, M_WAITOK);
		OF_getpropintarray(faa->fa_node, "enable-gpios",
		    sc->sc_gpio, sc->sc_gpiolen);
		gpio_controller_config_pin(sc->sc_gpio, GPIO_CONFIG_OUTPUT);
	}
	sc->sc_vcc = OF_getpropint(faa->fa_node, "VCC-supply", 0);

	printf("\n");

	sc->sc_dai.dd_node = faa->fa_node;
	sc->sc_dai.dd_cookie = sc;
	sc->sc_dai.dd_hw_if = &simpleamp_hw_if;
	dai_register(&sc->sc_dai);
}

int
simpleamp_open(void *cookie, int flags)
{
	struct simpleamp_softc *sc = cookie;

	if (sc->sc_gpio)
		gpio_controller_set_pin(sc->sc_gpio, 1);
	if (sc->sc_vcc)
		regulator_enable(sc->sc_vcc);

	return 0;
}

void
simpleamp_close(void *cookie)
{
	struct simpleamp_softc *sc = cookie;

	if (sc->sc_gpio)
		gpio_controller_set_pin(sc->sc_gpio, 0);
	if (sc->sc_vcc)
		regulator_disable(sc->sc_vcc);
}
