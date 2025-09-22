/*	$OpenBSD: isagpio.c,v 1.5 2022/04/06 18:59:28 naddy Exp $	*/

/*
 * Copyright (c) 2006 Oleg Safiullin <form@pdp-11.org.ru>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice unmodified, this list of conditions, and the following
 *    disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/device.h>
#include <sys/gpio.h>

#include <machine/bus.h>
#include <machine/intr.h>

#include <dev/isa/isavar.h>

#include <dev/gpio/gpiovar.h>

#define ISAGPIO_IOSIZE		1
#define ISAGPIO_NPINS		8

struct isagpio_softc {
	struct device		sc_dev;

	bus_space_tag_t		sc_iot;
	bus_space_handle_t	sc_ioh;

	struct gpio_chipset_tag	sc_gpio_gc;
	struct gpio_pin		sc_gpio_pins[ISAGPIO_NPINS];
	u_int8_t		sc_gpio_mask;
};

int	isagpio_match(struct device *, void *, void *);
void	isagpio_attach(struct device *, struct device *, void *);
int	isagpio_pin_read(void *, int);
void	isagpio_pin_write(void *, int, int);
void	isagpio_pin_ctl(void *, int, int);

const struct cfattach isagpio_ca = {
	sizeof(struct isagpio_softc), isagpio_match, isagpio_attach
};

struct cfdriver isagpio_cd = {
	NULL, "isagpio", DV_DULL
};

int
isagpio_match(struct device *parent, void *match, void *aux)
{
	struct isa_attach_args *ia = aux;
	bus_space_handle_t ioh;

	if (ia->ia_iobase == -1 || bus_space_map(ia->ia_iot, ia->ia_iobase,
	    ISAGPIO_IOSIZE, 0, &ioh) != 0)
		return (0);

	bus_space_unmap(ia->ia_iot, ioh, ISAGPIO_IOSIZE);
	ia->ia_iosize = ISAGPIO_IOSIZE;
	ia->ipa_nio = 1;
	ia->ipa_nmem = 0;
	ia->ipa_nirq = 0;
	ia->ipa_ndrq = 0;

	return (1);
}

void
isagpio_attach(struct device *parent, struct device *self, void *aux)
{
	struct isagpio_softc *sc = (void *)self;
	struct isa_attach_args *ia = aux;
	struct gpiobus_attach_args gba;
	int i;

	if (bus_space_map(ia->ia_iot, ia->ia_iobase, ia->ia_iosize, 0,
	    &sc->sc_ioh) != 0) {
		printf(": can't map i/o space\n");
		return;
	}

	printf("\n");

	sc->sc_iot = ia->ia_iot;
	sc->sc_gpio_mask = 0;

	for (i = 0; i < ISAGPIO_NPINS; i++) {
		sc->sc_gpio_pins[i].pin_num = i;
		sc->sc_gpio_pins[i].pin_caps = GPIO_PIN_INPUT | GPIO_PIN_OUTPUT;
		sc->sc_gpio_pins[i].pin_state = GPIO_PIN_LOW;
	}

	sc->sc_gpio_gc.gp_cookie = sc;
	sc->sc_gpio_gc.gp_pin_read = isagpio_pin_read;
	sc->sc_gpio_gc.gp_pin_write = isagpio_pin_write;
	sc->sc_gpio_gc.gp_pin_ctl = isagpio_pin_ctl;

	gba.gba_name = "gpio";
	gba.gba_gc = &sc->sc_gpio_gc;
	gba.gba_pins = sc->sc_gpio_pins;
	gba.gba_npins = ISAGPIO_NPINS;

	(void)config_found(&sc->sc_dev, &gba, gpiobus_print);
}

int
isagpio_pin_read(void *arg, int pin)
{
	struct isagpio_softc *sc = arg;
	u_int8_t mask;

	mask = bus_space_read_1(sc->sc_iot, sc->sc_ioh, 0);
	return ((mask >> pin) & 0x01);
}

void
isagpio_pin_write(void *arg, int pin, int value)
{
	struct isagpio_softc *sc = arg;

	if (value == GPIO_PIN_LOW)
		sc->sc_gpio_mask &= ~(0x01 << pin);
	else if (value == GPIO_PIN_HIGH)
		sc->sc_gpio_mask |= 0x01 << pin;
	bus_space_write_1(sc->sc_iot, sc->sc_ioh, 0, sc->sc_gpio_mask);
}

void
isagpio_pin_ctl(void *arg, int pin, int flags)
{
}
