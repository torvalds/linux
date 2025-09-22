/*	$OpenBSD: stfpinctrl.c,v 1.4 2024/10/17 01:57:18 jsg Exp $	*/
/*
 * Copyright (c) 2022 Mark Kettenis <kettenis@openbsd.org>
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

#include <machine/bus.h>
#include <machine/fdt.h>

#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_clock.h>
#include <dev/ofw/ofw_gpio.h>
#include <dev/ofw/fdt.h>

/* Registers. */

#define GPIODIN(pin)			(0x0048 + (((pin) / 32) * 4))
#define GPIO_DOUT_CFG(pin)		(0x0050 + ((pin) * 8))
#define GPIO_DOEN_CFG(pin)		(0x0054 + ((pin) * 8))
#define  GPO_ENABLE			0
#define  GPO_DISABLE			1

#define PAD_GPIO(pin)			(0x0000 + (((pin) / 2) * 4))
#define  PAD_INPUT_ENABLE		(1 << 7)
#define  PAD_INPUT_SCHMITT_ENABLE	(1 << 6)
#define  PAD_SHIFT(pin)			((pin % 2) * 16)
#define PAD_FUNC_SHARE(pin)		(0x0080 + (((pin) / 2) * 4))
#define IO_PADSHARE_SEL			0x01a0

#define JH7110_DOEN(pin)		(0x0000 + (((pin) / 4) * 4))
#define  JH7110_DOEN_SHIFT(pin)		(((pin) % 4) * 8)
#define  JH7110_DOEN_MASK		0x3f
#define  JH7110_DOEN_ENABLE		0
#define  JH7110_DOEN_DISABLE		1
#define JH7110_DOUT(pin)		(0x0040 + (((pin) / 4) * 4))
#define  JH7110_DOUT_SHIFT(pin)		(((pin) % 4) * 8)
#define  JH7110_DOUT_MASK		0x7f
#define JH7110_GPIOIN(pin)		(0x0118 + (((pin) / 32) * 4))
#define JH7110_PADCFG(pin)		(0x0120 + ((pin) * 4))
#define  JH7110_PADCFG_IE		(1 << 0)
#define  JH7110_PADCFG_PU		(1 << 3)
#define  JH7110_PADCFG_PD		(1 << 4)
#define  JH7110_PADCFG_SMT		(1 << 6)

#define GPIO_NUM_PINS		64

struct stfpinctrl_softc {
	struct device		sc_dev;
	bus_space_tag_t		sc_iot;
	bus_space_handle_t	sc_gpio_ioh;
	bus_space_handle_t	sc_padctl_ioh;
	bus_size_t		sc_padctl_gpio;
	int			sc_node;

	struct gpio_controller	sc_gc;
};

int stfpinctrl_match(struct device *, void *, void *);
void stfpinctrl_attach(struct device *, struct device *, void *);

const struct cfattach stfpinctrl_ca = {
	sizeof (struct stfpinctrl_softc), stfpinctrl_match, stfpinctrl_attach
};

struct cfdriver stfpinctrl_cd = {
	NULL, "stfpinctrl", DV_DULL
};

void	stfpinctrl_jh7100_config_pin(void *, uint32_t *, int);
int	stfpinctrl_jh7100_get_pin(void *, uint32_t *);
void	stfpinctrl_jh7100_set_pin(void *, uint32_t *, int);

void	stfpinctrl_jh7110_config_pin(void *, uint32_t *, int);
int	stfpinctrl_jh7110_get_pin(void *, uint32_t *);
void	stfpinctrl_jh7110_set_pin(void *, uint32_t *, int);

int
stfpinctrl_match(struct device *parent, void *match, void *aux)
{
	struct fdt_attach_args *faa = aux;

	return OF_is_compatible(faa->fa_node, "starfive,jh7100-pinctrl") ||
	    OF_is_compatible(faa->fa_node, "starfive,jh7110-sys-pinctrl");
}

void
stfpinctrl_attach(struct device *parent, struct device *self, void *aux)
{
	struct stfpinctrl_softc *sc = (struct stfpinctrl_softc *)self;
	struct fdt_attach_args *faa = aux;
	uint32_t sel;

	if (faa->fa_nreg < 1) {
		printf(": no registers\n");
		return;
	}

	if (OF_is_compatible(faa->fa_node, "starfive,jh7100-pinctrl") &&
	    faa->fa_nreg < 2) {
		printf(": no padctl registers\n");
		return;
	}

	sc->sc_node = faa->fa_node;
	sc->sc_iot = faa->fa_iot;

	if (bus_space_map(sc->sc_iot, faa->fa_reg[0].addr,
	    faa->fa_reg[0].size, 0, &sc->sc_gpio_ioh)) {
		printf(": can't map registers\n");
		return;
	}

	if (OF_is_compatible(faa->fa_node, "starfive,jh7100-pinctrl") &&
	    bus_space_map(sc->sc_iot, faa->fa_reg[1].addr,
	    faa->fa_reg[1].size, 0, &sc->sc_padctl_ioh)) {
		bus_space_unmap(sc->sc_iot, sc->sc_gpio_ioh,
		    faa->fa_reg[0].size);
		printf(": can't map registers\n");
		return;
	}

	printf("\n");

	if (OF_is_compatible(faa->fa_node, "starfive,jh7100-pinctrl")) {
		sel = bus_space_read_4(sc->sc_iot, sc->sc_padctl_ioh,
		    IO_PADSHARE_SEL);
		switch (sel) {
		case 0:
		default:
			/* No GPIOs available. */
			return;
		case 1:
			sc->sc_padctl_gpio = PAD_GPIO(0);
			break;
		case 2:
			sc->sc_padctl_gpio = PAD_FUNC_SHARE(72);
			break;
		case 3:
			sc->sc_padctl_gpio = PAD_FUNC_SHARE(70);
			break;
		case 4:
		case 5:
		case 6:
			sc->sc_padctl_gpio = PAD_FUNC_SHARE(0);
			break;
		}
	} else {
		reset_deassert(faa->fa_node, NULL);
		clock_enable(faa->fa_node, NULL);
	}

	sc->sc_gc.gc_node = faa->fa_node;
	sc->sc_gc.gc_cookie = sc;
	if (OF_is_compatible(faa->fa_node, "starfive,jh7100-pinctrl")) {
		sc->sc_gc.gc_config_pin = stfpinctrl_jh7100_config_pin;
		sc->sc_gc.gc_get_pin = stfpinctrl_jh7100_get_pin;
		sc->sc_gc.gc_set_pin = stfpinctrl_jh7100_set_pin;
	} else {
		sc->sc_gc.gc_config_pin = stfpinctrl_jh7110_config_pin;
		sc->sc_gc.gc_get_pin = stfpinctrl_jh7110_get_pin;
		sc->sc_gc.gc_set_pin = stfpinctrl_jh7110_set_pin;
	}
	gpio_controller_register(&sc->sc_gc);
}

/* JH7100 */

void
stfpinctrl_jh7100_config_pin(void *cookie, uint32_t *cells, int config)
{
	struct stfpinctrl_softc *sc = cookie;
	uint32_t pin = cells[0];
	uint32_t reg;

	if (pin >= GPIO_NUM_PINS)
		return;

	if (config & GPIO_CONFIG_OUTPUT) {
		bus_space_write_4(sc->sc_iot, sc->sc_gpio_ioh,
		    GPIO_DOEN_CFG(pin), GPO_ENABLE);
	} else {
		reg = bus_space_read_4(sc->sc_iot, sc->sc_padctl_ioh,
		    sc->sc_padctl_gpio + PAD_GPIO(pin));
		reg |= (PAD_INPUT_ENABLE << PAD_SHIFT(pin));
		reg |= (PAD_INPUT_SCHMITT_ENABLE << PAD_SHIFT(pin));
		bus_space_write_4(sc->sc_iot, sc->sc_padctl_ioh,
		    sc->sc_padctl_gpio + PAD_GPIO(pin), reg);
		bus_space_write_4(sc->sc_iot, sc->sc_gpio_ioh,
		    GPIO_DOEN_CFG(pin), GPO_DISABLE);
	}
}

int
stfpinctrl_jh7100_get_pin(void *cookie, uint32_t *cells)
{
	struct stfpinctrl_softc *sc = cookie;
	uint32_t pin = cells[0];
	uint32_t flags = cells[1];
	uint32_t reg;
	int val;

	if (pin >= GPIO_NUM_PINS)
		return 0;

	reg = bus_space_read_4(sc->sc_iot, sc->sc_gpio_ioh, GPIODIN(pin));
	val = (reg >> (pin % 32)) & 1;
	if (flags & GPIO_ACTIVE_LOW)
		val = !val;
	return val;
}

void
stfpinctrl_jh7100_set_pin(void *cookie, uint32_t *cells, int val)
{
	struct stfpinctrl_softc *sc = cookie;
	uint32_t pin = cells[0];
	uint32_t flags = cells[1];

	if (pin >= GPIO_NUM_PINS)
		return;

	if (flags & GPIO_ACTIVE_LOW)
		val = !val;
	bus_space_write_4(sc->sc_iot, sc->sc_gpio_ioh,
	    GPIO_DOUT_CFG(pin), val);
}

/* JH7110 */

void
stfpinctrl_jh7110_config_pin(void *cookie, uint32_t *cells, int config)
{
	struct stfpinctrl_softc *sc = cookie;
	uint32_t pin = cells[0];
	uint32_t doen, padcfg;

	if (pin >= GPIO_NUM_PINS)
		return;

	doen = bus_space_read_4(sc->sc_iot, sc->sc_gpio_ioh, JH7110_DOEN(pin));
	padcfg = bus_space_read_4(sc->sc_iot, sc->sc_gpio_ioh,
	    JH7110_PADCFG(pin));
	doen &= ~(JH7110_DOEN_MASK << JH7110_DOEN_SHIFT(pin));
	if (config & GPIO_CONFIG_OUTPUT) {
		doen |= (JH7110_DOEN_ENABLE << JH7110_DOEN_SHIFT(pin));
		bus_space_write_4(sc->sc_iot, sc->sc_gpio_ioh,
		    JH7110_DOEN(pin), doen);
		/* Disable input, Schmitt trigger and bias. */
		padcfg &= ~(JH7110_PADCFG_IE | JH7110_PADCFG_SMT);
		padcfg &= ~(JH7110_PADCFG_PU | JH7110_PADCFG_PD);
		bus_space_write_4(sc->sc_iot, sc->sc_gpio_ioh,
		    JH7110_PADCFG(pin), padcfg);
	} else {
		/* Enable input and Schmitt trigger. */
		padcfg |= JH7110_PADCFG_IE | JH7110_PADCFG_SMT;
		bus_space_write_4(sc->sc_iot, sc->sc_gpio_ioh,
		    JH7110_PADCFG(pin), padcfg);
		doen |= (JH7110_DOEN_DISABLE << JH7110_DOEN_SHIFT(pin));
		bus_space_write_4(sc->sc_iot, sc->sc_gpio_ioh,
		    JH7110_DOEN(pin), doen);
	}
}

int
stfpinctrl_jh7110_get_pin(void *cookie, uint32_t *cells)
{
	struct stfpinctrl_softc *sc = cookie;
	uint32_t pin = cells[0];
	uint32_t flags = cells[1];
	uint32_t reg;
	int val;

	if (pin >= GPIO_NUM_PINS)
		return 0;

	reg = bus_space_read_4(sc->sc_iot, sc->sc_gpio_ioh,
	    JH7110_GPIOIN(pin));
	val = (reg >> (pin % 32)) & 1;
	if (flags & GPIO_ACTIVE_LOW)
		val = !val;
	return val;
}

void
stfpinctrl_jh7110_set_pin(void *cookie, uint32_t *cells, int val)
{
	struct stfpinctrl_softc *sc = cookie;
	uint32_t pin = cells[0];
	uint32_t flags = cells[1];
	uint32_t reg;

	if (pin >= GPIO_NUM_PINS)
		return;

	if (flags & GPIO_ACTIVE_LOW)
		val = !val;

	reg = bus_space_read_4(sc->sc_iot, sc->sc_gpio_ioh, JH7110_DOUT(pin));
	reg &= ~(JH7110_DOUT_MASK << JH7110_DOUT_SHIFT(pin));
	reg |= (val << JH7110_DOUT_SHIFT(pin));
	bus_space_write_4(sc->sc_iot, sc->sc_gpio_ioh, JH7110_DOUT(pin), reg);
}
