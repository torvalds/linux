/*	$OpenBSD: skgpio.c,v 1.5 2022/04/06 18:59:29 naddy Exp $ */

/*
 * Copyright (c) 2014 Matt Dainty <matt@bodgit-n-scarper.com>
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

/*
 * Soekris net6501 GPIO and LEDs as implemented by the onboard Xilinx FPGA 
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/device.h>
#include <sys/gpio.h>

#include <machine/bus.h>

#include <dev/isa/isavar.h>

#include <dev/gpio/gpiovar.h>

#define	SKGPIO_BASE		0x680	/* Base address of FPGA I/O */
#define	SKGPIO_IOSIZE		32	/* I/O region size */

#define	SKGPIO_NPINS		16	/* Number of Pins */
#define	SKGPIO_GPIO_INPUT	0x000	/* Current state of pins */
#define	SKGPIO_GPIO_OUTPUT	0x004	/* Set state of output pins */
#define	SKGPIO_GPIO_RESET	0x008	/* Reset output pins */
#define	SKGPIO_GPIO_SET		0x00c	/* Set output pins */
#define	SKGPIO_GPIO_DIR		0x010	/* Direction, set for output */

#define	SKGPIO_NLEDS		2	/* Number of LEDs */
#define	SKGPIO_LED_ERROR	0x01c	/* Offset to error LED */
#define	SKGPIO_LED_READY	0x01d	/* Offset to ready LED */

const u_int skgpio_led_offset[SKGPIO_NLEDS] = {
	SKGPIO_LED_ERROR, SKGPIO_LED_READY
};

struct skgpio_softc {
	struct device		 sc_dev;

	bus_space_tag_t		 sc_iot;
	bus_space_handle_t	 sc_ioh;

	struct gpio_chipset_tag	 sc_gpio_gc;
	gpio_pin_t		 sc_gpio_pins[SKGPIO_NPINS];

	/* Fake GPIO device for the LEDs */
	struct gpio_chipset_tag	 sc_led_gc;
	gpio_pin_t		 sc_led_pins[SKGPIO_NLEDS];
};

int	 skgpio_match(struct device *, void *, void *);
void	 skgpio_attach(struct device *, struct device *, void *);
int	 skgpio_gpio_read(void *, int);
void	 skgpio_gpio_write(void *, int, int);
void	 skgpio_gpio_ctl(void *, int, int);
int	 skgpio_led_read(void *, int);
void	 skgpio_led_write(void *, int, int);
void	 skgpio_led_ctl(void *, int, int);

const struct cfattach skgpio_ca = {
	sizeof(struct skgpio_softc), skgpio_match, skgpio_attach
};

struct cfdriver skgpio_cd = {
	NULL, "skgpio", DV_DULL
};

int
skgpio_match(struct device *parent, void *match, void *aux)
{
	struct isa_attach_args *ia = aux;
	bus_space_handle_t ioh;

	if (hw_vendor == NULL || hw_prod == NULL ||
	    strcmp(hw_vendor, "Soekris Engineering") != 0 ||
	    strcmp(hw_prod, "net6501") != 0)
		return (0);

	if (ia->ia_iobase != SKGPIO_BASE || bus_space_map(ia->ia_iot,
	    ia->ia_iobase, SKGPIO_IOSIZE, 0, &ioh) != 0)
		return (0);

	bus_space_unmap(ia->ia_iot, ioh, SKGPIO_IOSIZE);
	ia->ia_iosize = SKGPIO_IOSIZE;
	ia->ipa_nio = 1;
	ia->ipa_nmem = 0;
	ia->ipa_nirq = 0;
	ia->ipa_ndrq = 0;

	return (1);
}

void
skgpio_attach(struct device *parent, struct device *self, void *aux)
{
	struct skgpio_softc *sc = (void *)self;
	struct isa_attach_args *ia = aux;
	struct gpiobus_attach_args gba1, gba2;
	u_int data;
	int i;

	if (bus_space_map(ia->ia_iot, ia->ia_iobase, ia->ia_iosize, 0,
	    &sc->sc_ioh) != 0) {
		printf(": can't map i/o space\n");
		return;
	}

	printf("\n");

	sc->sc_iot = ia->ia_iot;

	data = bus_space_read_2(sc->sc_iot, sc->sc_ioh, SKGPIO_GPIO_DIR);

	for (i = 0; i < SKGPIO_NPINS; i++) {
		sc->sc_gpio_pins[i].pin_num = i;
		sc->sc_gpio_pins[i].pin_caps = GPIO_PIN_INPUT |
		    GPIO_PIN_OUTPUT;
		sc->sc_gpio_pins[i].pin_flags = (data & (1 << i)) ?
		    GPIO_PIN_OUTPUT : GPIO_PIN_INPUT;
		sc->sc_gpio_pins[i].pin_state = skgpio_gpio_read(sc, i);
	}

	sc->sc_gpio_gc.gp_cookie = sc;
	sc->sc_gpio_gc.gp_pin_read = skgpio_gpio_read;
	sc->sc_gpio_gc.gp_pin_write = skgpio_gpio_write;
	sc->sc_gpio_gc.gp_pin_ctl = skgpio_gpio_ctl;

	gba1.gba_name = "gpio";
	gba1.gba_gc = &sc->sc_gpio_gc;
	gba1.gba_pins = sc->sc_gpio_pins;
	gba1.gba_npins = SKGPIO_NPINS;

	(void)config_found(&sc->sc_dev, &gba1, gpiobus_print);

	for (i = 0; i < SKGPIO_NLEDS; i++) {
		sc->sc_led_pins[i].pin_num = i;
		sc->sc_led_pins[i].pin_caps = GPIO_PIN_OUTPUT;
		sc->sc_led_pins[i].pin_flags = GPIO_PIN_OUTPUT;
		sc->sc_led_pins[i].pin_state = skgpio_led_read(sc, i);
	}

	sc->sc_led_gc.gp_cookie = sc;
	sc->sc_led_gc.gp_pin_read = skgpio_led_read;
	sc->sc_led_gc.gp_pin_write = skgpio_led_write;
	sc->sc_led_gc.gp_pin_ctl = skgpio_led_ctl;

	gba2.gba_name = "gpio";
	gba2.gba_gc = &sc->sc_led_gc;
	gba2.gba_pins = sc->sc_led_pins;
	gba2.gba_npins = SKGPIO_NLEDS;

	(void)config_found(&sc->sc_dev, &gba2, gpiobus_print);
}

int
skgpio_gpio_read(void *arg, int pin)
{
	struct skgpio_softc *sc = arg;
	u_int16_t data;

	data = bus_space_read_2(sc->sc_iot, sc->sc_ioh, SKGPIO_GPIO_INPUT);

	return (data & (1 << pin)) ? GPIO_PIN_HIGH : GPIO_PIN_LOW;
}

void
skgpio_gpio_write(void *arg, int pin, int value)
{
	struct skgpio_softc *sc = arg;
	u_int16_t data;

	data = bus_space_read_2(sc->sc_iot, sc->sc_ioh, SKGPIO_GPIO_INPUT);

	if (value == GPIO_PIN_LOW)
		data &= ~(1 << pin);
	else if (value == GPIO_PIN_HIGH)
		data |= (1 << pin);

	bus_space_write_2(sc->sc_iot, sc->sc_ioh, SKGPIO_GPIO_OUTPUT, data);
}

void
skgpio_gpio_ctl(void *arg, int pin, int flags)
{
	struct skgpio_softc *sc = arg;
	u_int16_t data;

	data = bus_space_read_2(sc->sc_iot, sc->sc_ioh, SKGPIO_GPIO_DIR);

	if (flags & GPIO_PIN_INPUT)
		data &= ~(1 << pin);
	if (flags & GPIO_PIN_OUTPUT)
		data |= (1 << pin);

	bus_space_write_2(sc->sc_iot, sc->sc_ioh, SKGPIO_GPIO_DIR, data);
}

int
skgpio_led_read(void *arg, int pin)
{
	struct skgpio_softc *sc = arg;
	u_int8_t value;

	value = bus_space_read_1(sc->sc_iot, sc->sc_ioh,
	    skgpio_led_offset[pin]);

	return (value & 0x1) ? GPIO_PIN_HIGH : GPIO_PIN_LOW;
}

void
skgpio_led_write(void *arg, int pin, int value)
{
	struct skgpio_softc *sc = arg;

	bus_space_write_1(sc->sc_iot, sc->sc_ioh, skgpio_led_offset[pin],
	    value);
}

void
skgpio_led_ctl(void *arg, int pin, int flags)
{
}
