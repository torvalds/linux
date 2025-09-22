/*	$OpenBSD: bcm2835_gpio.c,v 1.6 2025/07/31 11:03:37 jsg Exp $	*/
/*
 * Copyright (c) 2020 Mark Kettenis <kettenis@openbsd.org>
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
#include <sys/device.h>
#include <sys/gpio.h>
#include <sys/malloc.h>
#include <sys/systm.h>

#include <machine/bus.h>
#include <machine/fdt.h>

#include <dev/gpio/gpiovar.h>
#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_gpio.h>
#include <dev/ofw/ofw_pinctrl.h>
#include <dev/ofw/fdt.h>

#include "gpio.h"

/* Registers */
#define GPFSEL(n)		(0x00 + ((n) * 4))
#define  GPFSEL_MASK		0x7
#define  GPFSEL_GPIO_IN		0x0
#define  GPFSEL_GPIO_OUT	0x1
#define  GPFSEL_ALT0		0x4
#define  GPFSEL_ALT1		0x5
#define  GPFSEL_ALT2		0x6
#define  GPFSEL_ALT3		0x7
#define  GPFSEL_ALT4		0x3
#define  GPFSEL_ALT5		0x2
#define GPSET(n)		(0x1c + ((n) * 4))
#define GPCLR(n)		(0x28 + ((n) * 4))
#define GPLEV(n)		(0x34 + ((n) * 4))
#define GPPUD			0x94
#define  GPPUD_PUD		0x3
#define  GPPUD_PUD_OFF		0x0
#define  GPPUD_PUD_DOWN		0x1
#define  GPPUD_PUD_UP		0x2
#define GPPUDCLK(n)		(0x98 + ((n) * 4))
#define GPPULL(n)		(0xe4 + ((n) * 4))
#define  GPPULL_MASK		0x3

#define BCMGPIO_MAX_PINS	58

#define HREAD4(sc, reg)							\
	(bus_space_read_4((sc)->sc_iot, (sc)->sc_ioh, (reg)))
#define HWRITE4(sc, reg, val)						\
	bus_space_write_4((sc)->sc_iot, (sc)->sc_ioh, (reg), (val))

struct bcmgpio_softc {
	struct device		sc_dev;
	bus_space_tag_t		sc_iot;
	bus_space_handle_t	sc_ioh;

	void	(*sc_config_pull)(struct bcmgpio_softc *, int, int);
	int			sc_num_pins;

	struct gpio_controller	sc_gc;

	struct gpio_chipset_tag	sc_gpio_tag;
	gpio_pin_t		sc_gpio_pins[BCMGPIO_MAX_PINS];
	int			sc_gpio_claimed[BCMGPIO_MAX_PINS];
};

int	bcmgpio_match(struct device *, void *, void *);
void	bcmgpio_attach(struct device *, struct device *, void *);

const struct cfattach bcmgpio_ca = {
	sizeof (struct bcmgpio_softc), bcmgpio_match, bcmgpio_attach
};

struct cfdriver bcmgpio_cd = {
	NULL, "bcmgpio", DV_DULL
};

void	bcm2711_config_pull(struct bcmgpio_softc *, int, int);
void	bcm2835_config_pull(struct bcmgpio_softc *, int, int);
int	bcmgpio_pinctrl(uint32_t, void *);
void	bcmgpio_config_pin(void *, uint32_t *, int);
int	bcmgpio_get_pin(void *, uint32_t *);
void	bcmgpio_set_pin(void *, uint32_t *, int);
void	bcmgpio_attach_gpio(struct device *);

int
bcmgpio_match(struct device *parent, void *match, void *aux)
{
	struct fdt_attach_args *faa = aux;

	return (OF_is_compatible(faa->fa_node, "brcm,bcm2711-gpio") ||
	    OF_is_compatible(faa->fa_node, "brcm,bcm2835-gpio"));
}

void
bcmgpio_attach(struct device *parent, struct device *self, void *aux)
{
	struct bcmgpio_softc *sc = (struct bcmgpio_softc *)self;
	struct fdt_attach_args *faa = aux;

	if (faa->fa_nreg < 1) {
		printf(": no registers\n");
		return;
	}

	sc->sc_iot = faa->fa_iot;
	if (bus_space_map(sc->sc_iot, faa->fa_reg[0].addr,
	    faa->fa_reg[0].size, 0, &sc->sc_ioh)) {
		printf(": can't map registers\n");
		return;
	}

	printf("\n");

	if (OF_is_compatible(faa->fa_node, "brcm,bcm2711-gpio")) {
	    sc->sc_config_pull = bcm2711_config_pull;
	    sc->sc_num_pins = 58;
	} else {
	    sc->sc_config_pull = bcm2835_config_pull;
	    sc->sc_num_pins = 54;
	}

	pinctrl_register(faa->fa_node, bcmgpio_pinctrl, sc);

	sc->sc_gc.gc_node = faa->fa_node;
	sc->sc_gc.gc_cookie = sc;
	sc->sc_gc.gc_config_pin = bcmgpio_config_pin;
	sc->sc_gc.gc_get_pin = bcmgpio_get_pin;
	sc->sc_gc.gc_set_pin = bcmgpio_set_pin;
	gpio_controller_register(&sc->sc_gc);

	config_mountroot(self, bcmgpio_attach_gpio);
}

void
bcmgpio_config_func(struct bcmgpio_softc *sc, int pin, int func)
{
	int reg = (pin / 10);
	int shift = (pin % 10) * 3;
	uint32_t val;

	val = HREAD4(sc, GPFSEL(reg));
	val &= ~(GPFSEL_MASK << shift);
	HWRITE4(sc, GPFSEL(reg), val);
	val |= ((func & GPFSEL_MASK) << shift);
	HWRITE4(sc, GPFSEL(reg), val);
}

void
bcm2711_config_pull(struct bcmgpio_softc *sc, int pin, int pull)
{
	int reg = (pin / 16);
	int shift = (pin % 16) * 2;
	uint32_t val;

	val = HREAD4(sc, GPPULL(reg));
	val &= ~(GPPULL_MASK << shift);
	pull = ((pull & 1) << 1) | ((pull & 2) >> 1);
	val |= (pull << shift);
	HWRITE4(sc, GPPULL(reg), val);
}

void
bcm2835_config_pull(struct bcmgpio_softc *sc, int pin, int pull)
{
	int reg = (pin / 32);
	int shift = (pin % 32);

	HWRITE4(sc, GPPUD, pull & GPPUD_PUD);
	delay(1);
	HWRITE4(sc, GPPUDCLK(reg), 1 << shift);
	delay(1);
	HWRITE4(sc, GPPUDCLK(reg), 0);
}

int
bcmgpio_pinctrl(uint32_t phandle, void *cookie)
{
	struct bcmgpio_softc *sc = cookie;
	uint32_t *pins, *pull = NULL;
	int len, plen = 0;
	int node, i;
	int func;

	node = OF_getnodebyphandle(phandle);
	if (node == 0)
		return -1;

	len = OF_getproplen(node, "brcm,pins");
	if (len <= 0)
		return -1;

	pins = malloc(len, M_TEMP, M_WAITOK);
	if (OF_getpropintarray(node, "brcm,pins", pins, len) != len)
		goto fail;
	func = OF_getpropint(node, "brcm,function", -1);

	plen = OF_getproplen(node, "brcm,pull");
	if (plen > 0) {
		pull = malloc(plen, M_TEMP, M_WAITOK);
		if (OF_getpropintarray(node, "brcm,pull", pull, plen) != plen)
			goto fail;
	}

	for (i = 0; i < len / sizeof(uint32_t); i++) {
		bcmgpio_config_func(sc, pins[i], func);
		if (plen > 0 && i < plen / sizeof(uint32_t))
			sc->sc_config_pull(sc, pins[i], pull[i]);
		sc->sc_gpio_claimed[pins[i]] = 1;
	}

	free(pull, M_TEMP, plen);
	free(pins, M_TEMP, len);
	return 0;

fail:
	free(pull, M_TEMP, plen);
	free(pins, M_TEMP, len);
	return -1;
}

void
bcmgpio_config_pin(void *cookie, uint32_t *cells, int config)
{
	struct bcmgpio_softc *sc = cookie;
	uint32_t pin = cells[0];

	if (pin >= sc->sc_num_pins)
		return;

	if (config & GPIO_CONFIG_OUTPUT)
		bcmgpio_config_func(sc, pin, GPFSEL_GPIO_OUT);
	else
		bcmgpio_config_func(sc, pin, GPFSEL_GPIO_IN);
	if (config & GPIO_CONFIG_PULL_UP)
		sc->sc_config_pull(sc, pin, GPPUD_PUD_UP);
	else if (config & GPIO_CONFIG_PULL_DOWN)
		sc->sc_config_pull(sc, pin, GPPUD_PUD_DOWN);
	else
		sc->sc_config_pull(sc, pin, GPPUD_PUD_OFF);
}

int
bcmgpio_get_pin(void *cookie, uint32_t *cells)
{
	struct bcmgpio_softc *sc = cookie;
	uint32_t pin = cells[0];
	uint32_t flags = cells[1];
	uint32_t reg;
	int val;

	if (pin >= sc->sc_num_pins)
		return 0;

	reg = HREAD4(sc, GPLEV(pin / 32));
	val = (reg >> (pin % 32)) & 1;
	if (flags & GPIO_ACTIVE_LOW)
		val = !val;
	return val;
}

void
bcmgpio_set_pin(void *cookie, uint32_t *cells, int val)
{
	struct bcmgpio_softc *sc = cookie;
	uint32_t pin = cells[0];
	uint32_t flags = cells[1];

	if (pin >= sc->sc_num_pins)
		return;

	if (flags & GPIO_ACTIVE_LOW)
		val = !val;
	if (val)
		HWRITE4(sc, GPSET(pin / 32), (1 << (pin % 32)));
	else
		HWRITE4(sc, GPCLR(pin / 32), (1 << (pin % 32)));
}

/*
 * GPIO support code
 */

int	bcmgpio_pin_read(void *, int);
void	bcmgpio_pin_write(void *, int, int);
void	bcmgpio_pin_ctl(void *, int, int);

static const struct gpio_chipset_tag bcmgpio_gpio_tag = {
	.gp_pin_read = bcmgpio_pin_read,
	.gp_pin_write = bcmgpio_pin_write,
	.gp_pin_ctl = bcmgpio_pin_ctl
};

int
bcmgpio_pin_read(void *cookie, int pin)
{
	struct bcmgpio_softc *sc = cookie;
	uint32_t cells[2];

	cells[0] = pin;
	cells[1] = 0;

	return bcmgpio_get_pin(sc, cells) ? GPIO_PIN_HIGH : GPIO_PIN_LOW;
}

void
bcmgpio_pin_write(void *cookie, int pin, int val)
{
	struct bcmgpio_softc *sc = cookie;
	uint32_t cells[2];

	cells[0] = pin;
	cells[1] = 0;

	bcmgpio_set_pin(sc, cells, val);
}

void
bcmgpio_pin_ctl(void *cookie, int pin, int flags)
{
	struct bcmgpio_softc *sc = cookie;
	uint32_t cells[2];
	uint32_t config;

	cells[0] = pin;
	cells[1] = 0;

	config = 0;
	if (ISSET(flags, GPIO_PIN_OUTPUT))
		config |= GPIO_CONFIG_OUTPUT;
	if (ISSET(flags, GPIO_PIN_PULLUP))
		config |= GPIO_CONFIG_PULL_UP;
	if (ISSET(flags, GPIO_PIN_PULLDOWN))
		config |= GPIO_CONFIG_PULL_DOWN;

	bcmgpio_config_pin(sc, cells, config);
}

void
bcmgpio_attach_gpio(struct device *parent)
{
	struct bcmgpio_softc *sc = (struct bcmgpio_softc *)parent;
	struct gpiobus_attach_args gba;
	uint32_t reg;
	int func, state, flags;
	int pin;

	for (pin = 0; pin < sc->sc_num_pins; pin++) {
		/* Skip pins claimed by other devices. */
		if (sc->sc_gpio_claimed[pin])
			continue;

		/* Get pin configuration. */
		reg = HREAD4(sc, GPFSEL(pin / 10));
		func = (reg >> ((pin % 10) * 3)) & GPFSEL_MASK;

		switch (func) {
		case GPFSEL_GPIO_IN:
			flags = GPIO_PIN_SET | GPIO_PIN_INPUT;
			break;
		case GPFSEL_GPIO_OUT:
			flags = GPIO_PIN_SET | GPIO_PIN_OUTPUT;
			break;
		default:
			/* Ignore pins with an assigned function. */
			continue;
		}

		/* Get pin state. */
		reg = HREAD4(sc, GPLEV(pin / 32));
		state = (reg >> (pin % 32)) & 1;

		sc->sc_gpio_pins[pin].pin_caps =
		    GPIO_PIN_INPUT | GPIO_PIN_OUTPUT |
		    GPIO_PIN_PULLUP | GPIO_PIN_PULLDOWN;
		sc->sc_gpio_pins[pin].pin_flags = flags;
		sc->sc_gpio_pins[pin].pin_state = state;
		sc->sc_gpio_pins[pin].pin_num = pin;
	}

	memcpy(&sc->sc_gpio_tag, &bcmgpio_gpio_tag, sizeof(bcmgpio_gpio_tag));
	sc->sc_gpio_tag.gp_cookie = sc;

	gba.gba_name = "gpio";
	gba.gba_gc = &sc->sc_gpio_tag;
	gba.gba_pins = &sc->sc_gpio_pins[0];
	gba.gba_npins = sc->sc_num_pins;

#if NGPIO > 0
	config_found(&sc->sc_dev, &gba, gpiobus_print);
#endif
}
