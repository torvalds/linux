/*	$OpenBSD: bcmstbpinctrl.c,v 1.3 2025/09/19 08:36:19 mglocker Exp $	*/
/*
 * Copyright (c) 2025 Mark Kettenis <kettenis@openbsd.org>
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
#include <dev/ofw/ofw_pinctrl.h>
#include <dev/ofw/fdt.h>

#define BIAS_DISABLE	0x0
#define BIAS_PULL_DOWN	0x1
#define BIAS_PULL_UP	0x2
#define BIAS_MASK	0x3
#define FUNC_MASK	0xf

#define FUNC_MAX	9

#define HREAD4(sc, reg)							\
	(bus_space_read_4((sc)->sc_iot, (sc)->sc_ioh, (reg)))
#define HWRITE4(sc, reg, val)						\
	bus_space_write_4((sc)->sc_iot, (sc)->sc_ioh, (reg), (val))

struct bcmstbpinctrl_pin {
	char name[12];
	uint8_t func_reg, func_off;
	uint8_t bias_reg, bias_off;
	const char *func[FUNC_MAX + 1];
};

const struct bcmstbpinctrl_pin bcmstbpinctrl_c0_pins[] = {
	{ "gpio20", 2, 4, 8, 12, { "gpio" } },
	{ "gpio28", 3, 4, 9, 5, { "gpio" } },
	{ "gpio30", 3, 6, 9, 7, { "gpio", NULL, NULL, NULL, "sd2" } },
	{ "gpio31", 3, 7, 9, 8, { "gpio", NULL, NULL, NULL, "sd2" } },
	{ "gpio32", 4, 0, 9, 9, { "gpio", NULL, NULL, NULL, "sd2" } },
	{ "gpio33", 4, 1, 9, 10, { "gpio", NULL, NULL, "sd2" } },
	{ "gpio34", 4, 2, 9, 11, { "gpio", NULL, NULL, NULL, "sd2" } },
	{ "gpio35", 4, 3, 9, 12, { "gpio", NULL, NULL, "sd2" } },
	{ "emmc_cmd", 0, 0, 11, 1 /* no mux */ },
	{ "emmc_dat0", 0, 0, 11, 4 /* no mux */ },
	{ "emmc_dat1", 0, 0, 11, 5 /* no mux */ },
	{ "emmc_dat2", 0, 0, 11, 8 /* no mux */ },
	{ "emmc_dat3", 0, 0, 11, 7 /* no mux */ },
	{ "emmc_dat4", 0, 0, 11, 8 /* no mux */ },
	{ "emmc_dat5", 0, 0, 11, 9 /* no mux */ },
	{ "emmc_dat6", 0, 0, 11, 10 /* no mux */ },
	{ "emmc_dat7", 0, 0, 11, 11 /* no mux */ },
};

const struct bcmstbpinctrl_pin bcmstbpinctrl_c0_aon_pins[] = {
	{ "aon_gpio5", 3, 5, 7, 0,
	  { "gpio", NULL, NULL, NULL, NULL, NULL, "sd_card_g" } },
};

const struct bcmstbpinctrl_pin bcmstbpinctrl_d0_pins[] = {
	{ "gpio20", 1, 4, 5, 2, { "gpio" } },
	{ "gpio28", 2, 4, 5, 10, { "gpio" } },
	{ "gpio30", 2, 6, 5, 12, { "gpio", "sd2" } },
	{ "gpio31", 2, 7, 5, 13, { "gpio", "sd2" } },
	{ "gpio32", 3, 0, 5, 14, { "gpio", "sd2" } },
	{ "gpio33", 3, 1, 6, 0, { "gpio", "sd2" } },
	{ "gpio34", 3, 2, 6, 1, { "gpio", "sd2" } },
	{ "gpio35", 3, 3, 6, 2, { "gpio", "sd2" } },
	{ "emmc_cmd", 0, 0, 6, 3 /* no mux */ },
	{ "emmc_dat0", 0, 0, 6, 6 /* no mux */ },
	{ "emmc_dat1", 0, 0, 6, 7 /* no mux */ },
	{ "emmc_dat2", 0, 0, 6, 8 /* no mux */ },
	{ "emmc_dat3", 0, 0, 6, 9 /* no mux */ },
	{ "emmc_dat4", 0, 0, 6, 10 /* no mux */ },
	{ "emmc_dat5", 0, 0, 6, 11 /* no mux */ },
	{ "emmc_dat6", 0, 0, 6, 12 /* no mux */ },
	{ "emmc_dat7", 0, 0, 6, 13 /* no mux */ },
};

const struct bcmstbpinctrl_pin bcmstbpinctrl_d0_aon_pins[] = {
	{ "aon_gpio5", 3, 5, 5, 14,
	  { "gpio", NULL, NULL, NULL, "sd_card_g" } },
};

struct bcmstbpinctrl_softc {
	struct device		sc_dev;
	bus_space_tag_t		sc_iot;
	bus_space_handle_t	sc_ioh;

	const struct bcmstbpinctrl_pin *sc_pins;
	u_int			sc_npins;
};

int	bcmstbpinctrl_match(struct device *, void *, void *);
void	bcmstbpinctrl_attach(struct device *, struct device *, void *);

const struct cfattach bcmstbpinctrl_ca = {
	sizeof(struct bcmstbpinctrl_softc),
	bcmstbpinctrl_match, bcmstbpinctrl_attach
};

struct cfdriver bcmstbpinctrl_cd = {
	NULL, "bcmstbpinctrl", DV_DULL
};

int	bcmstbpinctrl_pinctrl(uint32_t, void *);

int
bcmstbpinctrl_match(struct device *parent, void *match, void *aux)
{
	struct fdt_attach_args *faa = aux;

	return (OF_is_compatible(faa->fa_node, "brcm,bcm2712-pinctrl") ||
	    OF_is_compatible(faa->fa_node, "brcm,bcm2712-aon-pinctrl") ||
	    OF_is_compatible(faa->fa_node, "brcm,bcm2712d0-pinctrl") ||
	    OF_is_compatible(faa->fa_node, "brcm,bcm2712d0-aon-pinctrl"));
}

void
bcmstbpinctrl_attach(struct device *parent, struct device *self, void *aux)
{
	struct bcmstbpinctrl_softc *sc = (struct bcmstbpinctrl_softc *)self;
	struct fdt_attach_args *faa = aux;

	sc->sc_iot = faa->fa_iot;
	if (bus_space_map(sc->sc_iot, faa->fa_reg[0].addr,
	    faa->fa_reg[0].size, 0, &sc->sc_ioh)) {
		printf(": can't map registers\n");
		return;
	}

	printf("\n");

	if (OF_is_compatible(faa->fa_node, "brcm,bcm2712-pinctrl")) {
		sc->sc_pins = bcmstbpinctrl_c0_pins;
		sc->sc_npins = nitems(bcmstbpinctrl_c0_pins);
	}
	if (OF_is_compatible(faa->fa_node, "brcm,bcm2712-aon-pinctrl")) {
		sc->sc_pins = bcmstbpinctrl_c0_aon_pins;
		sc->sc_npins = nitems(bcmstbpinctrl_c0_aon_pins);
	}
	if (OF_is_compatible(faa->fa_node, "brcm,bcm2712d0-pinctrl")) {
		sc->sc_pins = bcmstbpinctrl_d0_pins;
		sc->sc_npins = nitems(bcmstbpinctrl_d0_pins);
	}
	if (OF_is_compatible(faa->fa_node, "brcm,bcm2712d0-aon-pinctrl")) {
		sc->sc_pins = bcmstbpinctrl_d0_aon_pins;
		sc->sc_npins = nitems(bcmstbpinctrl_d0_aon_pins);
	}
	KASSERT(sc->sc_pins != NULL);

	pinctrl_register(faa->fa_node, bcmstbpinctrl_pinctrl, sc);
}

void
bcmstbpinctrl_config_pin(struct bcmstbpinctrl_softc *sc, const char *name,
    const char *function, int bias)
{
	uint32_t val;
	int pin, func;

	for (pin = 0; pin < sc->sc_npins; pin++) {
		if (strcmp(name, sc->sc_pins[pin].name) == 0)
			break;
	}
	if (pin == sc->sc_npins) {
		printf("%s: %s\n", __func__, name);
		return;
	}

	if (strlen(function) > 0) {
		for (func = 0; func <= FUNC_MAX; func++) {
			if (sc->sc_pins[pin].func[func] &&
			    strcmp(function, sc->sc_pins[pin].func[func]) == 0)
				break;
		}
		if (func > FUNC_MAX) {
			printf("%s: %s %s\n", __func__, name, function);
			return;
		}

		val = HREAD4(sc, sc->sc_pins[pin].func_reg * 4);
		val &= ~(FUNC_MASK << (sc->sc_pins[pin].func_off * 4));
		val |= (func << (sc->sc_pins[pin].func_off * 4));
		HWRITE4(sc, sc->sc_pins[pin].func_reg * 4, val);
	}

	val = HREAD4(sc, sc->sc_pins[pin].bias_reg * 4);
	val &= ~(BIAS_MASK << (sc->sc_pins[pin].bias_off * 2));
	val |= (bias << (sc->sc_pins[pin].bias_off * 2));
	HWRITE4(sc, sc->sc_pins[pin].bias_reg * 4, val);
}

void
bcmstbpinctrl_config(struct bcmstbpinctrl_softc *sc, int node)
{
	char function[16];
	char *pins;
	char *pin;
	int bias, len;

	/* Function */
	memset(function, 0, sizeof(function));
	OF_getprop(node, "function", function, sizeof(function));
	function[sizeof(function) - 1] = 0;

	/* Bias */
	if (OF_getproplen(node, "bias-pull-up") == 0)
		bias = BIAS_PULL_UP;
	else if (OF_getproplen(node, "bias-pull-down") == 0)
		bias = BIAS_PULL_DOWN;
	else
		bias = BIAS_DISABLE;

	len = OF_getproplen(node, "pins");
	if (len <= 0)
		return;

	pins = malloc(len, M_TEMP, M_WAITOK);
	OF_getprop(node, "pins", pins, len);

	pin = pins;
	while (pin < pins + len) {
		bcmstbpinctrl_config_pin(sc, pin, function, bias);
		pin += strlen(pin) + 1;
	}

	free(pins, M_TEMP, len);
}

int
bcmstbpinctrl_pinctrl(uint32_t phandle, void *cookie)
{
	struct bcmstbpinctrl_softc *sc = cookie;
	int node, child;

	node = OF_getnodebyphandle(phandle);
	if (node == 0)
		return -1;

	if (OF_getproplen(node, "pins") > 0) {
		/* Single node. */
		bcmstbpinctrl_config(sc, node);
	} else {
		/* Grouping of multiple nodes. */
		for (child = OF_child(node); child; child = OF_peer(child))
			bcmstbpinctrl_config(sc, child);
	}

	return 0;
}
