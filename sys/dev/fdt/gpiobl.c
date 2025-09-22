/*	$OpenBSD: gpiobl.c,v 1.4 2025/04/07 12:26:06 jsg Exp $	*/
/*
 * Copyright (c) 2022 Tobias Heider <tobhe@openbsd.org>
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
#include <sys/task.h>

#include <machine/fdt.h>
#include <machine/bus.h>

#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_gpio.h>
#include <dev/ofw/ofw_misc.h>

#include <dev/wscons/wsconsio.h>
#include <dev/wscons/wsdisplayvar.h>

extern void (*simplefb_burn_hook)(u_int);

struct gpiobl_softc {
	struct device		sc_dev;
	int			sc_on;
	uint32_t		sc_gpio[3];
	struct task		sc_task;
};

struct gpiobl_softc *sc_gpiobl;

int	gpiobl_match(struct device *, void *, void *);
void	gpiobl_attach(struct device *, struct device *, void *);
int	gpiobl_activate(struct device *, int);

const struct cfattach gpiobl_ca = {
	sizeof(struct gpiobl_softc), gpiobl_match, gpiobl_attach, NULL,
	gpiobl_activate
};

struct cfdriver gpiobl_cd = {
	NULL, "gpiobl", DV_DULL
};

void gpiobl_set(u_int);
void gpiobl_task(void *);

int
gpiobl_match(struct device *parent, void *match, void *aux)
{
	struct fdt_attach_args *faa = aux;

	return OF_is_compatible(faa->fa_node, "gpio-backlight");
}


void
gpiobl_attach(struct device *parent, struct device *self, void *aux)
{
	struct gpiobl_softc *sc = (struct gpiobl_softc *)self;
	struct fdt_attach_args *faa = aux;
	int len;

	len = OF_getproplen(faa->fa_node, "gpios");
	if (len <= 0)
		return;
	OF_getpropintarray(faa->fa_node, "gpios", sc->sc_gpio, len);
	gpio_controller_config_pin(sc->sc_gpio, GPIO_CONFIG_OUTPUT);

	sc->sc_on = OF_getpropbool(faa->fa_node, "default-on");
	sc_gpiobl = sc;

	task_set(&sc->sc_task, gpiobl_task, sc);

	simplefb_burn_hook = gpiobl_set;

	printf("\n");
}

int
gpiobl_activate(struct device *self, int act)
{
	struct gpiobl_softc *sc = (struct gpiobl_softc *)self;

	switch (act) {
	case DVACT_QUIESCE:
		gpio_controller_set_pin(&sc->sc_gpio[0], 0);
		break;
	case DVACT_WAKEUP:
		gpio_controller_set_pin(&sc->sc_gpio[0], sc->sc_on);
		break;
	}

	return 0;
}

void
gpiobl_set(u_int on)
{
	struct gpiobl_softc *sc = (struct gpiobl_softc *)sc_gpiobl;
	sc->sc_on = on;
	task_add(systq, &sc->sc_task);
}

void
gpiobl_task(void *args)
{
	struct gpiobl_softc *sc = args;
	gpio_controller_set_pin(&sc->sc_gpio[0], sc->sc_on);
}
