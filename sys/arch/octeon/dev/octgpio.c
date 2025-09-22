/*	$OpenBSD: octgpio.c,v 1.2 2019/09/29 04:28:52 visa Exp $	*/

/*
 * Copyright (c) 2019 Visa Hankala
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
 * Driver for OCTEON GPIO controller.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/device.h>
#include <sys/malloc.h>

#include <dev/ofw/fdt.h>
#include <dev/ofw/ofw_gpio.h>
#include <dev/ofw/openfirm.h>

#include <machine/fdt.h>
#include <machine/octeonvar.h>
#include <machine/octeon_model.h>

#define GPIO_BIT_CFG(x)		(0x0000u + (x) * 8)
#define   GPIO_BIT_CFG_OUTPUT_SEL_M	0x00000000001f0000ull
#define   GPIO_BIT_CFG_OUTPUT_SEL_S	16
#define   GPIO_BIT_CFG_INT_EN		0x0000000000000004ull
#define   GPIO_BIT_CFG_RX_XOR		0x0000000000000002ull
#define   GPIO_BIT_CFG_TX_OE		0x0000000000000001ull
#define GPIO_XBIT_CFG(x)	(0x0100u + (x) * 8)
#define GPIO_RX_DAT		0x0080u
#define GPIO_TX_SET		0x0088u
#define GPIO_TX_CLR		0x0090u

#define GPIO_RD_8(sc, reg) \
	bus_space_read_8((sc)->sc_iot, (sc)->sc_ioh, (reg))
#define GPIO_WR_8(sc, reg, val) \
	bus_space_write_8((sc)->sc_iot, (sc)->sc_ioh, (reg), (val))

struct octgpio_softc {
	struct device		 sc_dev;
	bus_space_tag_t		 sc_iot;
	bus_space_handle_t	 sc_ioh;
	struct gpio_controller	 sc_gc;
	uint32_t		 sc_npins;
	uint32_t		 sc_xbit;
};

int	octgpio_match(struct device *, void *, void *);
void	octgpio_attach(struct device *, struct device *, void *);

void	octgpio_config_pin(void *, uint32_t *, int);
int	octgpio_get_pin(void *, uint32_t *);
void	octgpio_set_pin(void *, uint32_t *, int);

const struct cfattach octgpio_ca = {
	sizeof(struct octgpio_softc), octgpio_match, octgpio_attach
};

struct cfdriver octgpio_cd = {
	NULL, "octgpio", DV_DULL
};

int
octgpio_match(struct device *parent, void *match, void *aux)
{
	struct fdt_attach_args *faa = aux;

	return OF_is_compatible(faa->fa_node, "cavium,octeon-3860-gpio") ||
	    OF_is_compatible(faa->fa_node, "cavium,octeon-7890-gpio");
}

void
octgpio_attach(struct device *parent, struct device *self, void *aux)
{
	struct fdt_attach_args *faa = aux;
	struct octgpio_softc *sc = (struct octgpio_softc *)self;
	uint32_t chipid;

	if (faa->fa_nreg != 1) {
		printf(": no registers\n");
		return;
	}

	sc->sc_iot = faa->fa_iot;
	if (bus_space_map(sc->sc_iot, faa->fa_reg[0].addr, faa->fa_reg[0].size,
	    0, &sc->sc_ioh)) {
		printf(": can't map registers\n");
		return;
	}

	chipid = octeon_get_chipid();
	switch (octeon_model_family(chipid)) {
	case OCTEON_MODEL_FAMILY_CN61XX:
	case OCTEON_MODEL_FAMILY_CN63XX:
	case OCTEON_MODEL_FAMILY_CN66XX:
	case OCTEON_MODEL_FAMILY_CN68XX:
	case OCTEON_MODEL_FAMILY_CN71XX:
		sc->sc_npins = 20;
		sc->sc_xbit = 16;
		break;
	case OCTEON_MODEL_FAMILY_CN73XX:
		sc->sc_npins = 32;
		sc->sc_xbit = 0;
		break;
	case OCTEON_MODEL_FAMILY_CN78XX:
		sc->sc_npins = 20;
		sc->sc_xbit = 0;
		break;
	default:
		sc->sc_npins = 24;
		sc->sc_xbit = 16;
		break;
	}

	sc->sc_gc.gc_node = faa->fa_node;
	sc->sc_gc.gc_cookie = sc;
	sc->sc_gc.gc_config_pin = octgpio_config_pin;
	sc->sc_gc.gc_get_pin = octgpio_get_pin;
	sc->sc_gc.gc_set_pin = octgpio_set_pin;
	gpio_controller_register(&sc->sc_gc);

	printf(": %u pins, xbit %u\n", sc->sc_npins, sc->sc_xbit);
}

void
octgpio_config_pin(void *cookie, uint32_t *cells, int config)
{
	struct octgpio_softc *sc = cookie;
	uint64_t output_sel, reg, value;
	uint32_t pin = cells[0];

	if (pin >= sc->sc_npins)
		return;
	if (pin >= sc->sc_xbit)
		reg = GPIO_XBIT_CFG(pin - sc->sc_xbit);
	else
		reg = GPIO_BIT_CFG(pin);

	value = GPIO_RD_8(sc, reg);
	if (config & GPIO_CONFIG_OUTPUT) {
		value |= GPIO_BIT_CFG_TX_OE;

		switch (config & GPIO_CONFIG_MD_OUTPUT_SEL_MASK) {
		case GPIO_CONFIG_MD_USB0_VBUS_CTRL:
			output_sel = 0x14;
			break;
		case GPIO_CONFIG_MD_USB1_VBUS_CTRL:
			output_sel = 0x19;
			break;
		default:
			output_sel = 0;
			break;
		}
		value &= ~GPIO_BIT_CFG_OUTPUT_SEL_M;
		value |= output_sel << GPIO_BIT_CFG_OUTPUT_SEL_S;
	} else
		value &= ~(GPIO_BIT_CFG_TX_OE | GPIO_BIT_CFG_RX_XOR);
	/* There is no INT_EN bit on true XBIT pins. */
	value &= ~GPIO_BIT_CFG_INT_EN;
	GPIO_WR_8(sc, reg, value);
}

int
octgpio_get_pin(void *cookie, uint32_t *cells)
{
	struct octgpio_softc *sc = cookie;
	uint32_t pin = cells[0];
	uint32_t flags = cells[1];
	int value;

	if (pin >= sc->sc_npins)
		return 0;

	value = (GPIO_RD_8(sc, GPIO_RX_DAT) >> pin) & 1;
	if (flags & GPIO_ACTIVE_LOW)
		value = !value;
	return value;
}

void
octgpio_set_pin(void *cookie, uint32_t *cells, int value)
{
	struct octgpio_softc *sc = cookie;
	uint32_t pin = cells[0];
	uint32_t flags = cells[1];

	if (pin >= sc->sc_npins)
		return;

	if (flags & GPIO_ACTIVE_LOW)
		value = !value;
	if (value)
		GPIO_WR_8(sc, GPIO_TX_SET, 1ul << pin);
	else
		GPIO_WR_8(sc, GPIO_TX_CLR, 1ul << pin);
}
