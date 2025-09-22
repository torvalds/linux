/* $OpenBSD: exgpio.c,v 1.8 2021/10/24 17:52:27 mpi Exp $ */
/*
 * Copyright (c) 2007,2009 Dale Rahn <drahn@openbsd.org>
 * Copyright (c) 2012-2013 Patrick Wildt <patrick@blueri.se>
 * Copyright (c) 2017 Mark Kettenis <kettenis@openbsd.org>
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

#define GPXCON(x)	((x) + 0x0000)
#define  GPXCON_INPUT	0
#define  GPXCON_OUTPUT	1
#define GPXDAT(x)	((x) + 0x0004)
#define GPXPUD(x)	((x) + 0x0008)
#define GPXDRV(x)	((x) + 0x000c)

#define GPX_NUM_PINS	8

#define HREAD4(sc, reg)							\
	(bus_space_read_4((sc)->sc_iot, (sc)->sc_ioh, (reg)))
#define HWRITE4(sc, reg, val)						\
	bus_space_write_4((sc)->sc_iot, (sc)->sc_ioh, (reg), (val))

struct exgpio_bank {
	const char name[8];
	bus_addr_t addr;
};

struct exgpio_controller {
	struct gpio_controller ec_gc;
	struct exgpio_bank *ec_bank;
	struct exgpio_softc *ec_sc;
};

struct exgpio_softc {
	struct device		sc_dev;
	bus_space_tag_t		sc_iot;
	bus_space_handle_t	sc_ioh;

	struct exgpio_bank	*sc_banks;
	int			sc_nbanks;
};

int exgpio_match(struct device *, void *, void *);
void exgpio_attach(struct device *, struct device *, void *);

const struct cfattach	exgpio_ca = {
	sizeof (struct exgpio_softc), exgpio_match, exgpio_attach
};

struct cfdriver exgpio_cd = {
	NULL, "exgpio", DV_DULL
};

/* Exynos 5420/5422 */
struct exgpio_bank exynos5420_banks[] = {
	/* Controller 0 */
	{ "gpy7", 0x0000 },
	{ "gpx0", 0x0c00 },
	{ "gpx1", 0x0c20 },
	{ "gpx2", 0x0c40 },
	{ "gpx3", 0x0c60 },

	/* Controller 1 */
	{ "gpc0", 0x0000 },
	{ "gpc1", 0x0020 },
	{ "gpc2", 0x0040 },
	{ "gpc3", 0x0060 },
	{ "gpc4", 0x0080 },
	{ "gpd1", 0x00a0 },
	{ "gpy0", 0x00c0 },
	{ "gpy1", 0x00e0 },
	{ "gpy2", 0x0100 },
	{ "gpy3", 0x0120 },
	{ "gpy4", 0x0140 },
	{ "gpy5", 0x0160 },
	{ "gpy6", 0x0180 },

	/* Controller 2 */
	{ "gpe0", 0x0000 },
	{ "gpe1", 0x0020 },
	{ "gpf0", 0x0040 },
	{ "gpf1", 0x0060 },
	{ "gpg0", 0x0080 },
	{ "gpg1", 0x00a0 },
	{ "gpg2", 0x00c0 },
	{ "gpj4", 0x00e0 },

	/* Controller 3 */
	{ "gpa0", 0x0000 },
	{ "gpa1", 0x0020 },
	{ "gpa2", 0x0040 },
	{ "gpb0", 0x0060 },
	{ "gpb1", 0x0080 },
	{ "gpb2", 0x00a0 },
	{ "gpb3", 0x00c0 },
	{ "gpb4", 0x00e0 },
	{ "gph0", 0x0100 },

	/* Controller 4 */
	{ "gpz", 0x0000 },
};

struct exgpio_bank *exgpio_bank(struct exgpio_softc *, const char *);
int	exgpio_pinctrl(uint32_t, void *);
void	exgpio_config_pin(void *, uint32_t *, int);
int	exgpio_get_pin(void *, uint32_t *);
void	exgpio_set_pin(void *, uint32_t *, int);

int
exgpio_match(struct device *parent, void *match, void *aux)
{
	struct fdt_attach_args *faa = aux;

	return OF_is_compatible(faa->fa_node, "samsung,exynos5420-pinctrl");
}

void
exgpio_attach(struct device *parent, struct device *self, void *aux)
{
	struct exgpio_softc *sc = (struct exgpio_softc *)self;
	struct fdt_attach_args *faa = aux;
	struct exgpio_controller *ec;
	struct exgpio_bank *bank;
	char name[8];
	int node;
	int len;

	sc->sc_iot = faa->fa_iot;

	if (bus_space_map(sc->sc_iot, faa->fa_reg[0].addr,
	    faa->fa_reg[0].size, 0, &sc->sc_ioh))
		panic("%s: bus_space_map failed!", __func__);

	if (OF_is_compatible(faa->fa_node, "samsung,exynos5420-pinctrl")) {
		sc->sc_banks = exynos5420_banks;
		sc->sc_nbanks = nitems(exynos5420_banks);
	}

	KASSERT(sc->sc_banks);
	pinctrl_register(faa->fa_node, exgpio_pinctrl, sc);

	for (node = OF_child(faa->fa_node); node; node = OF_peer(node)) {
		if (OF_getproplen(node, "gpio-controller") < 0)
			continue;

		len = OF_getprop(node, "name", &name, sizeof(name));
		if (len <= 0 || len >= sizeof(name))
			continue;

		bank = exgpio_bank(sc, name);
		if (bank == NULL)
			continue;

		ec = malloc(sizeof(*ec), M_DEVBUF, M_WAITOK);
		ec->ec_bank = exgpio_bank(sc, name);
		ec->ec_sc = sc;
		ec->ec_gc.gc_node = node;
		ec->ec_gc.gc_cookie = ec;
		ec->ec_gc.gc_config_pin = exgpio_config_pin;
		ec->ec_gc.gc_get_pin = exgpio_get_pin;
		ec->ec_gc.gc_set_pin = exgpio_set_pin;
		gpio_controller_register(&ec->ec_gc);
	}

	printf("\n");
}

struct exgpio_bank *
exgpio_bank(struct exgpio_softc *sc, const char *name)
{
	int i;

	for (i = 0; i < sc->sc_nbanks; i++) {
		if (strcmp(name, sc->sc_banks[i].name) == 0)
			return &sc->sc_banks[i];
	}

	return NULL;
}

int
exgpio_pinctrl(uint32_t phandle, void *cookie)
{
	struct exgpio_softc *sc = cookie;
	char *pins, *bank_name, *pin_name;
	struct exgpio_bank *bank;
	uint32_t func, val, pud, drv;
	uint32_t reg;
	int node;
	int len;
	int pin;

	node = OF_getnodebyphandle(phandle);
	if (node == 0)
		return -1;

	len = OF_getproplen(node, "samsung,pins");
	if (len <= 0)
		return -1;

	pins = malloc(len, M_TEMP, M_WAITOK);
	OF_getprop(node, "samsung,pins", pins, len);

	func = OF_getpropint(node, "samsung,pin-function", 0);
	val = OF_getpropint(node, "samsung,pin-val", 0);
	pud = OF_getpropint(node, "samsung,pin-pud", 1);
	drv = OF_getpropint(node, "samsung,pin-drv", 0);

	bank_name = pins;
	while (bank_name < pins + len) {
		pin_name = strchr(bank_name, '-');
		if (pin_name == NULL)
			goto fail;
		*pin_name++ = 0;
		pin = *pin_name - '0';
		if (pin < 0 || pin >= GPX_NUM_PINS)
			goto fail;

		bank = exgpio_bank(sc, bank_name);
		if (bank == NULL)
			goto fail;

		reg = HREAD4(sc, GPXCON(bank->addr));
		reg &= ~(0xf << (pin * 4));
		reg |= (func << (pin * 4));
		HWRITE4(sc, GPXCON(bank->addr), reg);

		reg = HREAD4(sc, GPXDAT(bank->addr));
		if (val)
			reg |= (1 << pin);
		else
			reg &= ~(1 << pin);
		HWRITE4(sc, GPXDAT(bank->addr), reg);

		reg = HREAD4(sc, GPXPUD(bank->addr));
		reg &= ~(0x3 << (pin * 2));
		reg |= (pud << (pin * 2));
		HWRITE4(sc, GPXPUD(bank->addr), reg);

		reg = HREAD4(sc, GPXDRV(bank->addr));
		reg &= ~(0x3 << (pin * 2));
		reg |= (drv << (pin * 2));
		HWRITE4(sc, GPXDRV(bank->addr), reg);

		bank_name = pin_name + 2;
	}

	free(pins, M_TEMP, len);
	return 0;

fail:
	free(pins, M_TEMP, len);
	return -1;
}

void
exgpio_config_pin(void *cookie, uint32_t *cells, int config)
{
	struct exgpio_controller *ec = cookie;
	uint32_t pin = cells[0];
	uint32_t val;
	int func;

	if (pin >= GPX_NUM_PINS)
		return;

	func = (config & GPIO_CONFIG_OUTPUT) ? GPXCON_OUTPUT : GPXCON_INPUT;
	val = HREAD4(ec->ec_sc, GPXCON(ec->ec_bank->addr));
	val &= ~(0xf << (pin * 4));
	val |= (func << (pin * 4));
	HWRITE4(ec->ec_sc, GPXCON(ec->ec_bank->addr), val);
}

int
exgpio_get_pin(void *cookie, uint32_t *cells)
{
	struct exgpio_controller *ec = cookie;
	uint32_t pin = cells[0];
	uint32_t flags = cells[1];
	uint32_t reg;
	int val;

	if (pin >= GPX_NUM_PINS)
		return 0;

	reg = HREAD4(ec->ec_sc, GPXDAT(ec->ec_bank->addr));
	reg &= (1 << pin);
	val = (reg >> pin) & 1;
	if (flags & GPIO_ACTIVE_LOW)
		val = !val;
	return val;
}

void
exgpio_set_pin(void *cookie, uint32_t *cells, int val)
{
	struct exgpio_controller *ec = cookie;
	uint32_t pin = cells[0];
	uint32_t flags = cells[1];
	uint32_t reg;

	if (pin >= GPX_NUM_PINS)
		return;

	reg = HREAD4(ec->ec_sc, GPXDAT(ec->ec_bank->addr));
	if (flags & GPIO_ACTIVE_LOW)
		val = !val;
	if (val)
		reg |= (1 << pin);
	else
		reg &= ~(1 << pin);
	HWRITE4(ec->ec_sc, GPXDAT(ec->ec_bank->addr), reg);
}
