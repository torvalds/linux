/*	$OpenBSD: mpfgpio.c,v 1.1 2022/02/18 10:51:43 visa Exp $	*/

/*
 * Copyright (c) 2022 Visa Hankala
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

/*
 * Driver for PolarFire SoC MSS GPIO controller.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/device.h>
#include <sys/gpio.h>

#include <machine/bus.h>
#include <machine/fdt.h>

#include <dev/gpio/gpiovar.h>

#include <dev/ofw/fdt.h>
#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_clock.h>
#include <dev/ofw/ofw_gpio.h>

#include "gpio.h"

#define MPFGPIO_CONFIG(i)	(0x0000 + (i) * 4)
#define  MPFGPIO_CONFIG_EN_INT		(1 << 3)
#define  MPFGPIO_CONFIG_EN_OE_BUF	(1 << 2)
#define  MPFGPIO_CONFIG_EN_IN		(1 << 1)
#define  MPFGPIO_CONFIG_EN_OUT		(1 << 0)
#define MPFGPIO_GPIN		0x0084
#define MPFGPIO_GPOUT		0x0088
#define MPFGPIO_CLEAR_BITS	0x00a0
#define MPFGPIO_SET_BITS	0x00a4

#define MPFGPIO_MAX_PINS	32

struct mpfgpio_softc {
	struct device		sc_dev;
	bus_space_tag_t		sc_iot;
	bus_space_handle_t	sc_ioh;
	uint32_t		sc_npins;

	struct gpio_controller	sc_gc;

	struct gpio_chipset_tag	sc_gpio_tag;
	gpio_pin_t		sc_gpio_pins[MPFGPIO_MAX_PINS];
	uint8_t			sc_gpio_claimed[MPFGPIO_MAX_PINS];
};

#define HREAD4(sc, reg) \
	(bus_space_read_4((sc)->sc_iot, (sc)->sc_ioh, (reg)))
#define HWRITE4(sc, reg, val) \
	bus_space_write_4((sc)->sc_iot, (sc)->sc_ioh, (reg), (val))

int	mpfgpio_match(struct device *, void *, void*);
void	mpfgpio_attach(struct device *, struct device *, void *);

void	mpfgpio_config_pin(void *, uint32_t *, int);
int	mpfgpio_get_pin(void *, uint32_t *);
void	mpfgpio_set_pin(void *, uint32_t *, int);

int	mpfgpio_pin_read(void *, int);
void	mpfgpio_pin_write(void *, int, int);
void	mpfgpio_pin_ctl(void *, int, int);
void	mpfgpio_attach_gpio(struct device *);

const struct cfattach mpfgpio_ca = {
	sizeof(struct mpfgpio_softc), mpfgpio_match, mpfgpio_attach
};

struct cfdriver mpfgpio_cd = {
	NULL, "mpfgpio", DV_DULL
};

int
mpfgpio_match(struct device *parent, void *match, void *aux)
{
	struct fdt_attach_args *faa = aux;

	if (faa->fa_nreg < 1)
		return 0;
	return OF_is_compatible(faa->fa_node, "microchip,mpfs-gpio");
}

void
mpfgpio_attach(struct device *parent, struct device *self, void *aux)
{
	struct fdt_attach_args *faa = aux;
	struct mpfgpio_softc *sc = (struct mpfgpio_softc *)self;
	unsigned int unit;

	sc->sc_iot = faa->fa_iot;

	unit = (faa->fa_reg[0].addr >> 12) & 0x3;
	switch (unit) {
	case 0:
		sc->sc_npins = 14;
		break;
	case 1:
		sc->sc_npins = 24;
		break;
	case 2:
		sc->sc_npins = 32;
		break;
	default:
		printf(": unexpected GPIO unit %u\n", unit);
		return;
	}
	KASSERT(sc->sc_npins <= MPFGPIO_MAX_PINS);

	if (bus_space_map(sc->sc_iot, faa->fa_reg[0].addr, faa->fa_reg[0].size,
	    0, &sc->sc_ioh) != 0) {
		printf(": can't map registers\n");
		return;
	}

	clock_enable_all(faa->fa_node);

	printf(": unit %u\n", unit);

	sc->sc_gc.gc_node = faa->fa_node;
	sc->sc_gc.gc_cookie = sc;
	sc->sc_gc.gc_config_pin = mpfgpio_config_pin;
	sc->sc_gc.gc_get_pin = mpfgpio_get_pin;
	sc->sc_gc.gc_set_pin = mpfgpio_set_pin;
	gpio_controller_register(&sc->sc_gc);

#if NGPIO > 0
	config_mountroot(self, mpfgpio_attach_gpio);
#endif
}

void
mpfgpio_config_pin(void *cookie, uint32_t *cells, int config)
{
	struct mpfgpio_softc *sc = cookie;
	uint32_t pin = cells[0];
	uint32_t val;

	if (pin >= sc->sc_npins)
		return;

	val = HREAD4(sc, MPFGPIO_CONFIG(pin));
	if (config & GPIO_CONFIG_OUTPUT) {
		val &= ~MPFGPIO_CONFIG_EN_IN;
		val |= MPFGPIO_CONFIG_EN_OUT;
	} else {
		val |= MPFGPIO_CONFIG_EN_IN;
		val &= ~MPFGPIO_CONFIG_EN_OUT;
	}
	val &= ~MPFGPIO_CONFIG_EN_INT;
	HWRITE4(sc, MPFGPIO_CONFIG(pin), val);

	sc->sc_gpio_claimed[pin] = 1;
}

int
mpfgpio_get_pin(void *cookie, uint32_t *cells)
{
	struct mpfgpio_softc *sc = cookie;
	uint32_t pin = cells[0];
	uint32_t flags = cells[1];
	int val;

	if (pin >= sc->sc_npins)
		return 0;

	val = (HREAD4(sc, MPFGPIO_GPIN) >> pin) & 1;
	if (flags & GPIO_ACTIVE_LOW)
		val = !val;
	return val;
}

void
mpfgpio_set_pin(void *cookie, uint32_t *cells, int val)
{
	struct mpfgpio_softc *sc = cookie;
	uint32_t pin = cells[0];
	uint32_t flags = cells[1];

	if (pin >= sc->sc_npins)
		return;

	if (flags & GPIO_ACTIVE_LOW)
		val = !val;
	if (val)
		HWRITE4(sc, MPFGPIO_SET_BITS, (1U << (pin % 32)));
	else
		HWRITE4(sc, MPFGPIO_CLEAR_BITS, (1U << (pin % 32)));
}

#if NGPIO > 0
int
mpfgpio_pin_read(void *cookie, int pin)
{
	struct mpfgpio_softc *sc = cookie;
	uint32_t cells[2];

	cells[0] = pin;
	cells[1] = 0;

	return mpfgpio_get_pin(sc, cells) ? GPIO_PIN_HIGH : GPIO_PIN_LOW;
}

void
mpfgpio_pin_write(void *cookie, int pin, int val)
{
	struct mpfgpio_softc *sc = cookie;
	uint32_t cells[2];

	cells[0] = pin;
	cells[1] = 0;

	mpfgpio_set_pin(sc, cells, val);
}

void
mpfgpio_pin_ctl(void *cookie, int pin, int flags)
{
	struct mpfgpio_softc *sc = cookie;
	uint32_t cells[2];
	uint32_t config = 0;

	cells[0] = pin;
	cells[1] = 0;

	if (flags & GPIO_PIN_OUTPUT)
		config |= GPIO_CONFIG_OUTPUT;

	mpfgpio_config_pin(sc, cells, config);
}

static const struct gpio_chipset_tag mpfgpio_gpio_tag = {
	.gp_pin_read	= mpfgpio_pin_read,
	.gp_pin_write	= mpfgpio_pin_write,
	.gp_pin_ctl	= mpfgpio_pin_ctl,
};

void
mpfgpio_attach_gpio(struct device *parent)
{
	struct gpiobus_attach_args gba;
	struct mpfgpio_softc *sc = (struct mpfgpio_softc *)parent;
	uint32_t cfgreg, pin;
	int flags, state;

	for (pin = 0; pin < sc->sc_npins; pin++) {
		/* Skip pins claimed by other devices. */
		if (sc->sc_gpio_claimed[pin])
			continue;

		cfgreg = HREAD4(sc, MPFGPIO_CONFIG(pin));
		if (cfgreg & MPFGPIO_CONFIG_EN_OUT)
			flags = GPIO_PIN_SET | GPIO_PIN_OUTPUT;
		else if (cfgreg & MPFGPIO_CONFIG_EN_IN)
			flags = GPIO_PIN_SET | GPIO_PIN_INPUT;
		else
			flags = GPIO_PIN_SET;

		state = (HREAD4(sc, MPFGPIO_GPIN) >> pin) & 1;

		sc->sc_gpio_pins[pin].pin_caps =
		    GPIO_PIN_INPUT | GPIO_PIN_OUTPUT;
		sc->sc_gpio_pins[pin].pin_flags = flags;
		sc->sc_gpio_pins[pin].pin_state = state;
		sc->sc_gpio_pins[pin].pin_num = pin;
	}

	sc->sc_gpio_tag = mpfgpio_gpio_tag;
	sc->sc_gpio_tag.gp_cookie = sc;

	gba.gba_name = "gpio";
	gba.gba_gc = &sc->sc_gpio_tag;
	gba.gba_pins = sc->sc_gpio_pins;
	gba.gba_npins = sc->sc_npins;

	config_found(&sc->sc_dev, &gba, gpiobus_print);
}
#endif
