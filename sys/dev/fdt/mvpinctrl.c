/* $OpenBSD: mvpinctrl.c,v 1.12 2023/08/15 08:27:30 miod Exp $ */
/*
 * Copyright (c) 2013,2016 Patrick Wildt <patrick@blueri.se>
 * Copyright (c) 2016 Mark Kettenis <kettenis@openbsd.org>
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

#include <machine/intr.h>
#include <machine/bus.h>
#include <machine/fdt.h>

#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_clock.h>
#include <dev/ofw/ofw_gpio.h>
#include <dev/ofw/ofw_misc.h>
#include <dev/ofw/ofw_pinctrl.h>
#include <dev/ofw/fdt.h>

/* Armada 3700 Registers */
#define GPIO_DIRECTION		0x00
#define GPIO_INPUT		0x10
#define GPIO_OUTPUT		0x18

#define HREAD4(sc, reg)							\
	(regmap_read_4((sc)->sc_rm, (reg)))
#define HWRITE4(sc, reg, val)						\
	regmap_write_4((sc)->sc_rm, (reg), (val))
#define HSET4(sc, reg, bits)						\
	HWRITE4((sc), (reg), HREAD4((sc), (reg)) | (bits))
#define HCLR4(sc, reg, bits)						\
	HWRITE4((sc), (reg), HREAD4((sc), (reg)) & ~(bits))

struct mvpinctrl_pin {
	const char *pin;
	const char *function;
	int value;
	int pid;
};

struct mvpinctrl_softc {
	struct device		 sc_dev;
	bus_space_tag_t		 sc_iot;
	bus_space_handle_t	 sc_ioh;
	struct regmap		*sc_rm;
	const struct mvpinctrl_pin *sc_pins;
	int			 sc_npins;
	struct gpio_controller	 sc_gc;
	struct clock_device	 sc_cd_xtal;
};

int	mvpinctrl_match(struct device *, void *, void *);
void	mvpinctrl_attach(struct device *, struct device *, void *);
int	mvpinctrl_pinctrl(uint32_t, void *);

void	mvpinctrl_config_pin(void *, uint32_t *, int);
int	mvpinctrl_get_pin(void *, uint32_t *);
void	mvpinctrl_set_pin(void *, uint32_t *, int);

uint32_t a3700_xtal_get_frequency(void *, uint32_t *);

const struct cfattach mvpinctrl_ca = {
	sizeof (struct mvpinctrl_softc), mvpinctrl_match, mvpinctrl_attach
};

struct cfdriver mvpinctrl_cd = {
	NULL, "mvpinctrl", DV_DULL
};

#define STR_HELPER(x) #x
#define STR(x) STR_HELPER(x)
#define MPP(id, func, val) { STR(mpp ## id), func, val, id }

#include "mvpinctrl_pins.h"

struct mvpinctrl_pins {
	const char *compat;
	const struct mvpinctrl_pin *pins;
	int npins;
};

const struct mvpinctrl_pins mvpinctrl_pins[] = {
	{
		"marvell,mv88f6810-pinctrl",
		armada_38x_pins, nitems(armada_38x_pins)
	},
	{
		"marvell,mv88f6820-pinctrl",
		armada_38x_pins, nitems(armada_38x_pins)
	},
	{
		"marvell,mv88f6828-pinctrl",
		armada_38x_pins, nitems(armada_38x_pins)
	},
	{
		"marvell,ap806-pinctrl",
		armada_ap806_pins, nitems(armada_ap806_pins)
	},
	{
		"marvell,cp110-pinctrl",
		armada_cp110_pins, nitems(armada_cp110_pins)
	},
	{
		"marvell,cp115-standalone-pinctrl",
		armada_cp110_pins, nitems(armada_cp110_pins)
	},
	{
		"marvell,armada-7k-pinctrl",
		armada_cp110_pins, nitems(armada_cp110_pins)
	},
	{
		"marvell,armada-8k-cpm-pinctrl",
		armada_cp110_pins, nitems(armada_cp110_pins)
	},
	{
		"marvell,armada-8k-cps-pinctrl",
		armada_cp110_pins, nitems(armada_cp110_pins)
	},
};

int
mvpinctrl_match(struct device *parent, void *match, void *aux)
{
	struct fdt_attach_args *faa = aux;
	int i;

	for (i = 0; i < nitems(mvpinctrl_pins); i++) {
		if (OF_is_compatible(faa->fa_node, mvpinctrl_pins[i].compat))
			return 10;
	}

	if (OF_is_compatible(faa->fa_node, "marvell,armada3710-nb-pinctrl") ||
	    OF_is_compatible(faa->fa_node, "marvell,armada3710-sb-pinctrl"))
		return 10;

	return 0;
}

void
mvpinctrl_attach(struct device *parent, struct device *self, void *aux)
{
	struct mvpinctrl_softc *sc = (struct mvpinctrl_softc *)self;
	struct fdt_attach_args *faa = aux;
	int i, node;

	if (faa->fa_nreg > 0) {
		sc->sc_iot = faa->fa_iot;
		if (bus_space_map(sc->sc_iot, faa->fa_reg[0].addr,
		    faa->fa_reg[0].size, 0, &sc->sc_ioh)) {
			printf(": can't map registers\n");
			return;
		}

		regmap_register(faa->fa_node, sc->sc_iot, sc->sc_ioh,
		    faa->fa_reg[0].size);
		sc->sc_rm = regmap_bynode(faa->fa_node);
	} else {
		/* No registers; use regmap provided by parent. */
		sc->sc_rm = regmap_bynode(OF_parent(faa->fa_node));
	}

	if (sc->sc_rm == NULL) {
		printf(": no registers\n");
		return;
	}

	printf("\n");

	if (OF_is_compatible(faa->fa_node, "marvell,armada3710-nb-pinctrl")) {
		for (node = OF_child(faa->fa_node); node; node = OF_peer(node)) {
			if (OF_is_compatible(node, "marvell,armada-3700-xtal-clock"))
				break;
		}
		KASSERT(node != 0);
		sc->sc_cd_xtal.cd_node = node;
		sc->sc_cd_xtal.cd_cookie = sc;
		sc->sc_cd_xtal.cd_get_frequency = a3700_xtal_get_frequency;
		clock_register(&sc->sc_cd_xtal);
	}

	if (OF_is_compatible(faa->fa_node, "marvell,armada3710-nb-pinctrl") ||
	    OF_is_compatible(faa->fa_node, "marvell,armada3710-sb-pinctrl")) {
		for (node = OF_child(faa->fa_node); node; node = OF_peer(node)) {
			if (OF_getproplen(node, "gpio-controller") == 0)
				break;
		}
		KASSERT(node != 0);
		sc->sc_gc.gc_node = node;
		sc->sc_gc.gc_cookie = sc;
		sc->sc_gc.gc_config_pin = mvpinctrl_config_pin;
		sc->sc_gc.gc_get_pin = mvpinctrl_get_pin;
		sc->sc_gc.gc_set_pin = mvpinctrl_set_pin;
		gpio_controller_register(&sc->sc_gc);
		return;
	}

	for (i = 0; i < nitems(mvpinctrl_pins); i++) {
		if (OF_is_compatible(faa->fa_node, mvpinctrl_pins[i].compat)) {
			sc->sc_pins = mvpinctrl_pins[i].pins;
			sc->sc_npins = mvpinctrl_pins[i].npins;
			break;
		}
	}

	KASSERT(sc->sc_pins);
	pinctrl_register(faa->fa_node, mvpinctrl_pinctrl, sc);
}

int
mvpinctrl_pinctrl(uint32_t phandle, void *cookie)
{
	struct mvpinctrl_softc *sc = cookie;
	char *pins, *pin, *func;
	int i, flen, plen, node;

	node = OF_getnodebyphandle(phandle);
	if (node == 0)
		return -1;

	flen = OF_getproplen(node, "marvell,function");
	if (flen <= 0)
		return -1;

	func = malloc(flen, M_TEMP, M_WAITOK);
	OF_getprop(node, "marvell,function", func, flen);

	plen = OF_getproplen(node, "marvell,pins");
	if (plen <= 0)
		return -1;

	pin = pins = malloc(plen, M_TEMP, M_WAITOK);
	OF_getprop(node, "marvell,pins", pins, plen);

	while (plen > 0) {
		for (i = 0; i < sc->sc_npins; i++) {
			uint32_t off, shift;

			if (strcmp(sc->sc_pins[i].pin, pin))
				continue;
			if (strcmp(sc->sc_pins[i].function, func))
				continue;

			off = (sc->sc_pins[i].pid / 8) * sizeof(uint32_t);
			shift = (sc->sc_pins[i].pid % 8) * 4;

			HWRITE4(sc, off, (HREAD4(sc, off) & ~(0xf << shift)) |
			    (sc->sc_pins[i].value << shift));
			break;
		}

		if (i == sc->sc_npins)
			printf("%s: unsupported pin %s function %s\n",
			    sc->sc_dev.dv_xname, pin, func);

		plen -= strlen(pin) + 1;
		pin += strlen(pin) + 1;
	}

	free(func, M_TEMP, flen);
	free(pins, M_TEMP, plen);
	return 0;
}

void
mvpinctrl_config_pin(void *cookie, uint32_t *cells, int config)
{
	struct mvpinctrl_softc *sc = cookie;
	uint32_t pin = cells[0];

	if (pin >= 32)
		return;

	if (config & GPIO_CONFIG_OUTPUT)
		HSET4(sc, GPIO_DIRECTION, (1 << pin));
	else
		HCLR4(sc, GPIO_DIRECTION, (1 << pin));
}

int
mvpinctrl_get_pin(void *cookie, uint32_t *cells)
{
	struct mvpinctrl_softc *sc = cookie;
	uint32_t pin = cells[0];
	uint32_t flags = cells[1];
	uint32_t reg;
	int val;

	if (pin >= 32)
		return 0;

	reg = HREAD4(sc, GPIO_INPUT);
	reg &= (1 << pin);
	val = (reg >> pin) & 1;
	if (flags & GPIO_ACTIVE_LOW)
		val = !val;
	return val;
}

void
mvpinctrl_set_pin(void *cookie, uint32_t *cells, int val)
{
	struct mvpinctrl_softc *sc = cookie;
	uint32_t pin = cells[0];
	uint32_t flags = cells[1];

	if (pin >= 32)
		return;

	if (flags & GPIO_ACTIVE_LOW)
		val = !val;
	if (val)
		HSET4(sc, GPIO_OUTPUT, (1 << pin));
	else
		HCLR4(sc, GPIO_OUTPUT, (1 << pin));
}

/* Armada 3700 XTAL block */

#define XTAL			0xc
#define  XTAL_MODE			(1U << 31)

uint32_t
a3700_xtal_get_frequency(void *cookie, uint32_t *cells)
{
	struct mvpinctrl_softc *sc = cookie;

	if (regmap_read_4(sc->sc_rm, XTAL) & XTAL_MODE)
		return 40000000;
	else
		return 25000000;
}
