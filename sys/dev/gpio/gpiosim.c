/*      $OpenBSD: gpiosim.c,v 1.2 2022/04/06 18:59:28 naddy Exp $	*/

/*
 * Copyright (c) 2007 Marc Balmer <mbalmer@openbsd.org>
 * All rights reserved.
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF MIND, USE, DATA OR PROFITS, WHETHER IN
 * AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT
 * OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

/* 32 bit wide GPIO simulator  */
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/device.h>
#include <sys/gpio.h>
#include <sys/malloc.h>
#include <sys/sysctl.h>
#include <sys/ioccom.h>

#include <dev/gpio/gpiovar.h>
#include <dev/biovar.h>

#define	GPIOSIM_NPINS	32

struct gpiosim_softc {
	struct device		sc_dev;
	u_int32_t		sc_state;
	struct gpio_chipset_tag	sc_gpio_gc;
	gpio_pin_t		sc_gpio_pins[GPIOSIM_NPINS];
};

struct gpiosim_op {
	void *cookie;
	u_int32_t mask;
	u_int32_t state;
};
#define GPIOSIMREAD	_IOWR('G', 0, struct gpiosim_op)
#define GPIOSIMWRITE	_IOW('G', 1, struct gpiosim_op)

int	gpiosim_match(struct device *, void *, void *);
void	gpiosim_attach(struct device *, struct device *, void *);
int	gpiosim_ioctl(struct device *, u_long cmd, caddr_t data);

int	gpiosim_pin_read(void *, int);
void	gpiosim_pin_write(void *, int, int);
void	gpiosim_pin_ctl(void *, int, int);

const struct cfattach gpiosim_ca = {
	sizeof(struct gpiosim_softc), gpiosim_match, gpiosim_attach
};

struct cfdriver gpiosim_cd = {
	NULL, "gpiosim", DV_DULL
};

int
gpiosim_match(struct device *parent, void *match, void *aux)
{
	return 1;
}

void
gpiosim_attach(struct device *parent, struct device *self, void *aux)
{
	struct gpiosim_softc *sc = (void *)self;
	struct gpiobus_attach_args gba;
	int i;

	/* initialize pin array */
	for (i = 0; i < GPIOSIM_NPINS; i++) {
		sc->sc_gpio_pins[i].pin_num = i;
		sc->sc_gpio_pins[i].pin_caps = GPIO_PIN_INPUT |
		    GPIO_PIN_OUTPUT | GPIO_PIN_OPENDRAIN |
		    GPIO_PIN_PULLUP | GPIO_PIN_PULLDOWN |
		    GPIO_PIN_INVIN | GPIO_PIN_INVOUT;

		/* read initial state */
		sc->sc_gpio_pins[i].pin_flags = GPIO_PIN_INPUT;
		sc->sc_state = 0;

		/* create controller tag */
		sc->sc_gpio_gc.gp_cookie = sc;
		sc->sc_gpio_gc.gp_pin_read = gpiosim_pin_read;
		sc->sc_gpio_gc.gp_pin_write = gpiosim_pin_write;
		sc->sc_gpio_gc.gp_pin_ctl = gpiosim_pin_ctl;

		gba.gba_name = "gpio";
		gba.gba_gc = &sc->sc_gpio_gc;
		gba.gba_pins = sc->sc_gpio_pins;
		gba.gba_npins = GPIOSIM_NPINS;
	}
	printf("\n");
	config_found(&sc->sc_dev, &gba, gpiobus_print);
	bio_register(&sc->sc_dev, gpiosim_ioctl);
}

int
gpiosim_ioctl(struct device *self, u_long cmd, caddr_t data)
{
	struct gpiosim_softc *sc = (void *)self;
	struct gpiosim_op *op = (void *)data;

	switch (cmd) {
		case GPIOSIMREAD:
			op->state = sc->sc_state;
			break;
		case GPIOSIMWRITE:
			sc->sc_state = (sc->sc_state & ~op->mask) |
			    (op->state & op->mask);
			break;
	}
	return 0;
}

int
gpiosim_pin_read(void *arg, int pin)
{
	struct gpiosim_softc *sc = (struct gpiosim_softc *)arg;

	if (sc->sc_state & (1 << pin))
		return GPIO_PIN_HIGH;
	else
		return GPIO_PIN_LOW;
}

void
gpiosim_pin_write(void *arg, int pin, int value)
{
	struct gpiosim_softc *sc = (struct gpiosim_softc *)arg;

	if (value == 0)
		sc->sc_state &= ~(1 << pin);
	else
		sc->sc_state |= (1 << pin);
}

void
gpiosim_pin_ctl(void *arg, int pin, int flags)
{
	struct gpiosim_softc *sc = (struct gpiosim_softc *)arg;

	sc->sc_gpio_pins[pin].pin_flags = flags;
}
