/*	$OpenBSD: mvgpio.c,v 1.3 2021/10/24 17:52:26 mpi Exp $	*/
/*
 * Copyright (c) 2018 Mark Kettenis <kettenis@openbsd.org>
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

#include <machine/intr.h>
#include <machine/bus.h>
#include <machine/fdt.h>

#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_gpio.h>
#include <dev/ofw/ofw_misc.h>
#include <dev/ofw/fdt.h>

/* Registers. */
#define GPIO_DOUT		0x0000
#define GPIO_DOUTEN		0x0004
#define GPIO_DINACTLOW		0x000c
#define GPIO_DIN		0x0010

#define HREAD4(sc, reg)							\
	(regmap_read_4((sc)->sc_rm, (sc)->sc_offset + (reg)))
#define HWRITE4(sc, reg, val)						\
	regmap_write_4((sc)->sc_rm, (sc)->sc_offset + (reg), (val))
#define HSET4(sc, reg, bits)						\
	HWRITE4((sc), (reg), HREAD4((sc), (reg)) | (bits))
#define HCLR4(sc, reg, bits)						\
	HWRITE4((sc), (reg), HREAD4((sc), (reg)) & ~(bits))

struct mvgpio_softc {
	struct device		sc_dev;
	struct regmap		*sc_rm;
	bus_size_t		sc_offset;

	struct gpio_controller	sc_gc;
};

int mvgpio_match(struct device *, void *, void *);
void mvgpio_attach(struct device *, struct device *, void *);

const struct cfattach	mvgpio_ca = {
	sizeof (struct mvgpio_softc), mvgpio_match, mvgpio_attach
};

struct cfdriver mvgpio_cd = {
	NULL, "mvgpio", DV_DULL
};

void	mvgpio_config_pin(void *, uint32_t *, int);
int	mvgpio_get_pin(void *, uint32_t *);
void	mvgpio_set_pin(void *, uint32_t *, int);

int
mvgpio_match(struct device *parent, void *match, void *aux)
{
	struct fdt_attach_args *faa = aux;

	return OF_is_compatible(faa->fa_node, "marvell,armada-8k-gpio");
}

void
mvgpio_attach(struct device *parent, struct device *self, void *aux)
{
	struct mvgpio_softc *sc = (struct mvgpio_softc *)self;
	struct fdt_attach_args *faa = aux;

	sc->sc_rm = regmap_bynode(OF_parent(faa->fa_node));
	if (sc->sc_rm == NULL) {
		printf(": no registers\n");
		return;
	}
	sc->sc_offset = OF_getpropint(faa->fa_node, "offset", 0);

	sc->sc_gc.gc_node = faa->fa_node;
	sc->sc_gc.gc_cookie = sc;
	sc->sc_gc.gc_config_pin = mvgpio_config_pin;
	sc->sc_gc.gc_get_pin = mvgpio_get_pin;
	sc->sc_gc.gc_set_pin = mvgpio_set_pin;
	gpio_controller_register(&sc->sc_gc);

	printf("\n");
}

void
mvgpio_config_pin(void *cookie, uint32_t *cells, int config)
{
	struct mvgpio_softc *sc = cookie;
	uint32_t pin = cells[0];

	if (pin >= 32)
		return;

	if (config & GPIO_CONFIG_OUTPUT)
		HCLR4(sc, GPIO_DOUTEN, (1 << pin));
	else
		HSET4(sc, GPIO_DOUTEN, (1 << pin));
}

int
mvgpio_get_pin(void *cookie, uint32_t *cells)
{
	struct mvgpio_softc *sc = cookie;
	uint32_t pin = cells[0];
	uint32_t flags = cells[1];
	uint32_t reg;
	int val;

	if (pin >= 32)
		return 0;

	reg = HREAD4(sc, GPIO_DIN);
	reg ^= HREAD4(sc, GPIO_DINACTLOW);
	reg &= (1 << pin);
	val = (reg >> pin) & 1;
	if (flags & GPIO_ACTIVE_LOW)
		val = !val;
	return val;
}

void
mvgpio_set_pin(void *cookie, uint32_t *cells, int val)
{
	struct mvgpio_softc *sc = cookie;
	uint32_t pin = cells[0];
	uint32_t flags = cells[1];

	if (pin >= 32)
		return;

	if (flags & GPIO_ACTIVE_LOW)
		val = !val;
	if (val)
		HSET4(sc, GPIO_DOUT, (1 << pin));
	else
		HCLR4(sc, GPIO_DOUT, (1 << pin));
}
