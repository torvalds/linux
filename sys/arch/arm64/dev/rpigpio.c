/* $OpenBSD: rpigpio.c,v 1.2 2025/09/16 08:42:59 kettenis Exp $ */
/*
 * Copyright (c) 2024 Patrick Wildt <patrick@blueri.se>
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

#define GPIOx_STATUS(x)			(0x000 + (x) * 0x8)
#define  GPIOx_STATUS_OUTFROMPERI		(1 << 8)
#define  GPIOx_STATUS_OUTFROMPAD		(1 << 9)
#define  GPIOx_STATUS_OEFROMPERI		(1 << 12)
#define  GPIOx_STATUS_OEFROMPAD			(1 << 13)
#define  GPIOx_STATUS_INISDIRECT		(1 << 16)
#define  GPIOx_STATUS_INFROMPAD			(1 << 17)
#define  GPIOx_STATUS_INFILTERED		(1 << 18)
#define  GPIOx_STATUS_INTOPERI			(1 << 19)
#define  GPIOx_STATUS_EVENT_EDGE_LOW		(1 << 20)
#define  GPIOx_STATUS_EVENT_EDGE_HIGH		(1 << 21)
#define  GPIOx_STATUS_EVENT_LEVEL_LOW		(1 << 22)
#define  GPIOx_STATUS_EVENT_LEVEL_HIGH		(1 << 23)
#define  GPIOx_STATUS_EVENT_F_EDGE_LOW		(1 << 24)
#define  GPIOx_STATUS_EVENT_F_EDGE_HIGH		(1 << 25)
#define  GPIOx_STATUS_EVENT_DB_LEVEL_LOW	(1 << 26)
#define  GPIOx_STATUS_EVENT_DB_LEVEL_HIGH	(1 << 27)
#define  GPIOx_STATUS_IRQCOMBINED		(1 << 28)
#define  GPIOx_STATUS_IRQTOPROC			(1 << 29)
#define GPIOx_CTRL(x)			(0x004 + (x) * 0x8)
#define  GPIOx_CTRL_FUNCSEL_MASK		(0x1f << 0)
#define  GPIOx_CTRL_FUNCSEL(x)			((x) << 0)
#define  GPIOx_CTRL_FUNCSEL_GPIO		((5) << 0)
#define  GPIOx_CTRL_OUTOVER_MASK		(0x3 << 12)
#define  GPIOx_CTRL_OUTOVER_NRMFUNC		(0 << 12)
#define  GPIOx_CTRL_OUTOVER_INVFUNC		(1 << 12)
#define  GPIOx_CTRL_OUTOVER_LOW			(2 << 12)
#define  GPIOx_CTRL_OUTOVER_HIGH		(3 << 12)
#define  GPIOx_CTRL_OEOVER_MASK			(0x3 << 14)
#define  GPIOx_CTRL_OEOVER_NRMFUNC		(0 << 14)
#define  GPIOx_CTRL_OEOVER_INVFUNC		(1 << 14)
#define  GPIOx_CTRL_OEOVER_DIS			(2 << 14)
#define  GPIOx_CTRL_OEOVER_ENA			(3 << 14)
#define  GPIOx_CTRL_INOVER_MASK			(0x3 << 16)
#define  GPIOx_CTRL_INOVER_NRMFUNC		(0 << 16)
#define  GPIOx_CTRL_INOVER_INVFUNC		(1 << 16)
#define  GPIOx_CTRL_INOVER_LOW			(2 << 16)
#define  GPIOx_CTRL_INOVER_HIGH			(3 << 16)
#define  GPIOx_CTRL_IRQMASK_EDGE_LOW		(1 << 20)
#define  GPIOx_CTRL_IRQMASK_EDGE_HIGH		(1 << 21)
#define  GPIOx_CTRL_IRQMASK_LEVEL_LOW		(1 << 22)
#define  GPIOx_CTRL_IRQMASK_LEVEL_HIGH		(1 << 23)
#define  GPIOx_CTRL_IRQMASK_F_EDGE_LOW		(1 << 24)
#define  GPIOx_CTRL_IRQMASK_F_EDGE_HIGH		(1 << 25)
#define  GPIOx_CTRL_IRQMASK_DB_LEVEL_LOW	(1 << 26)
#define  GPIOx_CTRL_IRQMASK_DB_LEVEL_HIGH	(1 << 27)
#define  GPIOx_CTRL_IRQRESET			(1 << 28)
#define  GPIOx_CTRL_IRQOVER_MASK		(0x3U << 30)
#define  GPIOx_CTRL_IRQOVER_NRMFUNC		(0U << 30)
#define  GPIOx_CTRL_IRQOVER_INVFUNC		(1U << 30)
#define  GPIOx_CTRL_IRQOVER_LOW			(2U << 30)
#define  GPIOx_CTRL_IRQOVER_HIGH		(3U << 30)

#define RIO_OUT				0x000
#define RIO_OE				0x004
#define RIO_IN				0x008

#define PAD_CTRL(x)			(0x000 + (x) * 0x4)
#define  PAD_CTRL_IN_EN				(1 << 6)
#define  PAD_CTRL_OUT_DIS			(1 << 7)

#define GPIO_NUM_PINS		54

#define PAD_BIAS_MASK		0xc
#define PAD_BIAS_DISABLE	0x0
#define PAD_BIAS_PULL_DOWN	0x4
#define PAD_BIAS_PULL_UP	0x8

#define FUNC_MAX		8

struct rpigpiopinctrl_pin {
	char name[8];
	uint8_t pin;
	const char *func[FUNC_MAX + 1];
};

const struct rpigpiopinctrl_pin rpigpiopinctrl_pins[] = {
	{ "gpio45", 45, { "pwm1", "i2c5", "spi7", "spi6", "i2s2", "gpio",
	    "proc_rio", } },
};

struct rpigpio_softc {
	struct device		sc_dev;
	bus_space_tag_t		sc_iot;
	bus_space_handle_t	sc_gpio_ioh;
	bus_space_handle_t	sc_rio_ioh;
	bus_space_handle_t	sc_pads_ioh;
	int			sc_node;

	struct gpio_controller	sc_gc;

	const struct rpigpiopinctrl_pin *sc_pins;
	u_int			sc_npins;
};

int rpigpio_match(struct device *, void *, void *);
void rpigpio_attach(struct device *, struct device *, void *);

const struct rpigpio_bank *rpigpio_get_bank(struct rpigpio_softc *, uint32_t);
int rpigpio_pinctrl(uint32_t, void *);
void rpigpio_config_func(struct rpigpio_softc *, const char *,
    const char *, uint32_t);
void rpigpio_config_pin(void *, uint32_t *, int);
int rpigpio_get_pin(void *, uint32_t *);
void rpigpio_set_pin(void *, uint32_t *, int);

const struct cfattach rpigpio_ca = {
	sizeof (struct rpigpio_softc), rpigpio_match, rpigpio_attach
};

struct cfdriver rpigpio_cd = {
	NULL, "rpigpio", DV_DULL
};

int
rpigpio_match(struct device *parent, void *match, void *aux)
{
	struct fdt_attach_args *faa = aux;

	return OF_is_compatible(faa->fa_node, "raspberrypi,rp1-gpio");
}

void
rpigpio_attach(struct device *parent, struct device *self, void *aux)
{
	struct rpigpio_softc *sc = (struct rpigpio_softc *)self;
	struct fdt_attach_args *faa = aux;

	if (faa->fa_nreg < 3)
		return;

	sc->sc_node = faa->fa_node;
	sc->sc_iot = faa->fa_iot;
	if (bus_space_map(sc->sc_iot, faa->fa_reg[0].addr,
	    faa->fa_reg[0].size, 0, &sc->sc_gpio_ioh)) {
		printf(": can't map GPIO registers\n");
		return;
	}
	if (bus_space_map(sc->sc_iot, faa->fa_reg[1].addr,
	    faa->fa_reg[1].size, 0, &sc->sc_rio_ioh)) {
		bus_space_unmap(sc->sc_iot, sc->sc_gpio_ioh,
		    faa->fa_reg[0].size);
		printf(": can't map RIO registers\n");
		return;
	}
	if (bus_space_map(sc->sc_iot, faa->fa_reg[2].addr,
	    faa->fa_reg[2].size, 0, &sc->sc_pads_ioh)) {
		bus_space_unmap(sc->sc_iot, sc->sc_rio_ioh,
		    faa->fa_reg[1].size);
		bus_space_unmap(sc->sc_iot, sc->sc_gpio_ioh,
		    faa->fa_reg[0].size);
		printf(": can't map PADS registers\n");
		return;
	}

	sc->sc_pins = rpigpiopinctrl_pins;
	sc->sc_npins = nitems(rpigpiopinctrl_pins);
	pinctrl_register(faa->fa_node, rpigpio_pinctrl, sc);

	sc->sc_gc.gc_node = faa->fa_node;
	sc->sc_gc.gc_cookie = sc;
	sc->sc_gc.gc_config_pin = rpigpio_config_pin;
	sc->sc_gc.gc_get_pin = rpigpio_get_pin;
	sc->sc_gc.gc_set_pin = rpigpio_set_pin;
	gpio_controller_register(&sc->sc_gc);

	printf("\n");
}

struct rpigpio_bank {
	uint32_t start;
	uint32_t num;
	uint32_t gpio;
	uint32_t rio;
	uint32_t pads;
};

const struct rpigpio_bank rpigpio_banks[] = {
	{  0, 28, 0x0000, 0x0000, 0x0004 },
	{ 28,  6, 0x4000, 0x4000, 0x4004 },
	{ 34, 20, 0x8000, 0x8000, 0x8004 },
};

const struct rpigpio_bank *
rpigpio_get_bank(struct rpigpio_softc *sc, uint32_t pin)
{
	const struct rpigpio_bank *bank;
	int i;

	for (i = 0; i < nitems(rpigpio_banks); i++) {
		bank = &rpigpio_banks[i];
		if (pin >= bank->start && pin < bank->start + bank->num) {
			return bank;
		}
	}

	printf("%s: can't find pin %d\n", sc->sc_dev.dv_xname, pin);
	return NULL;
}

int
rpigpio_pinctrl(uint32_t phandle, void *cookie)
{
	struct rpigpio_softc *sc = cookie;
	int node;
	char function[16];
	char *pins;
	char *pin;
	uint32_t bias;
	int len;

	node = OF_getnodebyphandle(phandle);
	if (node == 0)
		return -1;

	/* Function */
	memset(function, 0, sizeof(function));
	OF_getprop(node, "function", function, sizeof(function));
	function[sizeof(function) - 1] = 0;

	/* Bias */
	if (OF_getproplen(node, "bias-pull-up") == 0)
		bias = PAD_BIAS_PULL_UP;
	else if (OF_getproplen(node, "bias-pull-down") == 0)
		bias = PAD_BIAS_PULL_DOWN;
	else
		bias = PAD_BIAS_DISABLE;

	len = OF_getproplen(node, "pins");
	if (len <= 0) {
		printf("%s: 0x%08x\n", __func__, phandle);
		return -1;
	}

	pins = malloc(len, M_TEMP, M_WAITOK);
	OF_getprop(node, "pins", pins, len);

	pin = pins;
	while (pin < pins + len) {
		rpigpio_config_func(sc, pin, function, bias);
		pin += strlen(pin) + 1;
	}

	free(pins, M_TEMP, len);

	return 0;
}

void
rpigpio_config_func(struct rpigpio_softc *sc, const char *name,
    const char *function, uint32_t bias)
{
	const struct rpigpio_bank *bank;
	uint32_t val;
	int pin, func;

	/* Find pin. */
	for (pin = 0; pin < sc->sc_npins; pin++) {
		if (strcmp(name, sc->sc_pins[pin].name) == 0)
			break;
	}
	if (pin == sc->sc_npins) {
		printf("%s: %s\n", __func__, name);
		return;
	}

	/* Find function. */
	for (func = 0; func <= FUNC_MAX; func++) {
		if (sc->sc_pins[pin].func[func] &&
		    strcmp(function, sc->sc_pins[pin].func[func]) == 0)
			break;
	}
	if (func > FUNC_MAX) {
		printf("%s: %s %s\n", __func__, name, function);
		return;
	}
	pin = sc->sc_pins[pin].pin;

	bank = rpigpio_get_bank(sc, pin);
	if (bank == NULL)
		return;
	pin -= bank->start;

	/* Configure pad. */
	val = bus_space_read_4(sc->sc_iot, sc->sc_pads_ioh,
	    bank->pads + PAD_CTRL(pin));

	/* Set bias. */
	val &= ~PAD_BIAS_MASK;
	val |= bias;

	/* Enable input/output. */
	val &= ~PAD_CTRL_OUT_DIS;
	val |= PAD_CTRL_IN_EN;

	bus_space_write_4(sc->sc_iot, sc->sc_pads_ioh,
	    bank->pads + PAD_CTRL(pin), val);

	/* Set function. */
	val = bus_space_read_4(sc->sc_iot, sc->sc_gpio_ioh,
	    bank->gpio + GPIOx_CTRL(pin));

	val &= ~GPIOx_CTRL_FUNCSEL_MASK;
	val |= GPIOx_CTRL_FUNCSEL(func);
	val &= ~GPIOx_CTRL_OEOVER_MASK;
	val |= GPIOx_CTRL_OEOVER_NRMFUNC;
	val &= ~GPIOx_CTRL_OUTOVER_MASK;
	val |= GPIOx_CTRL_OUTOVER_NRMFUNC;

	bus_space_write_4(sc->sc_iot, sc->sc_gpio_ioh,
	    bank->gpio + GPIOx_CTRL(pin), val);
}

void
rpigpio_config_pin(void *cookie, uint32_t *cells, int config)
{
	struct rpigpio_softc *sc = cookie;
	const struct rpigpio_bank *bank;
	uint32_t pin = cells[0];
	uint32_t val;

	bank = rpigpio_get_bank(sc, pin);
	if (bank == NULL)
		return;
	pin -= bank->start;

	/* Configure pin to be input or output */
	val = bus_space_read_4(sc->sc_iot, sc->sc_rio_ioh, bank->rio + RIO_OE);
	if (config & GPIO_CONFIG_OUTPUT)
		val |= 1 << pin;
	else
		val &= ~(1 << pin);
	bus_space_write_4(sc->sc_iot, sc->sc_rio_ioh, bank->rio + RIO_OE, val);

	/* Enable input/output on pad */
	val = bus_space_read_4(sc->sc_iot, sc->sc_pads_ioh, bank->pads +
	    PAD_CTRL(pin));
	val &= ~PAD_CTRL_OUT_DIS;
	val |= PAD_CTRL_IN_EN;
	bus_space_write_4(sc->sc_iot, sc->sc_pads_ioh, bank->pads +
	    PAD_CTRL(pin), val);

	/* Configure pin as GPIO in standard mode */
	val = bus_space_read_4(sc->sc_iot, sc->sc_gpio_ioh, bank->gpio +
	    GPIOx_CTRL(pin));
	val &= ~(GPIOx_CTRL_FUNCSEL_MASK | GPIOx_CTRL_OUTOVER_MASK |
	    GPIOx_CTRL_OEOVER_MASK);
	val |= (GPIOx_CTRL_FUNCSEL_GPIO | GPIOx_CTRL_OUTOVER_NRMFUNC |
	    GPIOx_CTRL_OEOVER_NRMFUNC);
	bus_space_write_4(sc->sc_iot, sc->sc_gpio_ioh, bank->gpio +
	    GPIOx_CTRL(pin), val);
}

int
rpigpio_get_pin(void *cookie, uint32_t *cells)
{
	struct rpigpio_softc *sc = cookie;
	const struct rpigpio_bank *bank;
	uint32_t pin = cells[0];
	uint32_t flags = cells[1];
	uint32_t reg;
	int val;

	bank = rpigpio_get_bank(sc, pin);
	if (bank == NULL)
		return 0;
	pin -= bank->start;

	reg = bus_space_read_4(sc->sc_iot, sc->sc_rio_ioh, bank->rio + RIO_IN);
	reg &= (1 << pin);
	val = (reg >> pin) & 1;
	if (flags & GPIO_ACTIVE_LOW)
		val = !val;
	return val;
}

void
rpigpio_set_pin(void *cookie, uint32_t *cells, int val)
{
	struct rpigpio_softc *sc = cookie;
	const struct rpigpio_bank *bank;
	uint32_t pin = cells[0];
	uint32_t flags = cells[1];
	uint32_t reg;

	bank = rpigpio_get_bank(sc, pin);
	if (bank == NULL)
		return;
	pin -= bank->start;

	reg = bus_space_read_4(sc->sc_iot, sc->sc_rio_ioh, bank->rio + RIO_OUT);
	if (flags & GPIO_ACTIVE_LOW)
		val = !val;
	if (val)
		reg |= (1 << pin);
	else
		reg &= ~(1 << pin);
	bus_space_write_4(sc->sc_iot, sc->sc_rio_ioh, bank->rio + RIO_OUT, reg);
}
