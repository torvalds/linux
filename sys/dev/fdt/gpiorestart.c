/*	$OpenBSD: gpiorestart.c,v 1.1 2022/06/09 12:13:56 kettenis Exp $	*/
/*
 * Copyright (c) 2022 Mark Kettenis
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
#include <dev/ofw/ofw_gpio.h>
#include <dev/ofw/fdt.h>

extern void (*cpuresetfn)(void);

struct gpiorestart_softc {
	struct device	sc_dev;
	uint32_t	*sc_gpio;
	uint32_t	sc_active_delay;
	uint32_t	sc_inactive_delay;
	uint32_t	sc_wait_delay;
};

int	gpiorestart_match(struct device *, void *, void *);
void	gpiorestart_attach(struct device *, struct device *, void *);

const struct cfattach gpiorestart_ca = {
	sizeof(struct gpiorestart_softc), gpiorestart_match, gpiorestart_attach
};

struct cfdriver gpiorestart_cd = {
	NULL, "gpiorestart", DV_DULL
};

void	gpiorestart_reset(void);

int
gpiorestart_match(struct device *parent, void *match, void *aux)
{
	struct fdt_attach_args *faa = aux;

	return OF_is_compatible(faa->fa_node, "gpio-restart");
}

void
gpiorestart_attach(struct device *parent, struct device *self, void *aux)
{
	struct gpiorestart_softc *sc = (struct gpiorestart_softc *)self;
	struct fdt_attach_args *faa = aux;
	int len;

	len = OF_getproplen(faa->fa_node, "gpios");
	if (len <= 0) {
		printf(": no gpio\n");
		return;
	}

	sc->sc_gpio = malloc(len, M_DEVBUF, M_WAITOK);
	OF_getpropintarray(faa->fa_node, "gpios", sc->sc_gpio, len);

	sc->sc_active_delay =
	    OF_getpropint(faa->fa_node, "active-delay", 100) * 1000;
	sc->sc_inactive_delay =
	    OF_getpropint(faa->fa_node, "inactive-delay", 100) * 1000;
	sc->sc_wait_delay =
	    OF_getpropint(faa->fa_node, "wait-delay", 3000) * 1000;

	if (cpuresetfn == NULL)
		cpuresetfn = gpiorestart_reset;

	printf("\n");
}

void
gpiorestart_reset(void)
{
	struct gpiorestart_softc *sc = gpiorestart_cd.cd_devs[0];

	gpio_controller_config_pin(sc->sc_gpio, GPIO_CONFIG_OUTPUT);
	gpio_controller_set_pin(sc->sc_gpio, 1);
	delay(sc->sc_active_delay);
	gpio_controller_set_pin(sc->sc_gpio, 0);
	delay(sc->sc_inactive_delay);
	gpio_controller_set_pin(sc->sc_gpio, 1);
	delay(sc->sc_wait_delay);
}
