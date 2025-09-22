/*	$OpenBSD: plgpio.c,v 1.3 2021/10/24 17:52:26 mpi Exp $	*/
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
#include <dev/ofw/fdt.h>

/* Registers. */
#define GPIODATA(pin)		((1 << pin) << 2)
#define GPIODIR			0x400

#define HREAD1(sc, reg)							\
	(bus_space_read_1((sc)->sc_iot, (sc)->sc_ioh, (reg)))
#define HWRITE1(sc, reg, val)						\
	bus_space_write_1((sc)->sc_iot, (sc)->sc_ioh, (reg), (val))
#define HSET1(sc, reg, bits)						\
	HWRITE1((sc), (reg), HREAD1((sc), (reg)) | (bits))
#define HCLR1(sc, reg, bits)						\
	HWRITE1((sc), (reg), HREAD1((sc), (reg)) & ~(bits))

struct plgpio_softc {
	struct device		sc_dev;
	bus_space_tag_t		sc_iot;
	bus_space_handle_t	sc_ioh;

	struct gpio_controller	sc_gc;
};

int plgpio_match(struct device *, void *, void *);
void plgpio_attach(struct device *, struct device *, void *);

const struct cfattach	plgpio_ca = {
	sizeof (struct plgpio_softc), plgpio_match, plgpio_attach
};

struct cfdriver plgpio_cd = {
	NULL, "plgpio", DV_DULL
};

void	plgpio_config_pin(void *, uint32_t *, int);
int	plgpio_get_pin(void *, uint32_t *);
void	plgpio_set_pin(void *, uint32_t *, int);

int
plgpio_match(struct device *parent, void *match, void *aux)
{
	struct fdt_attach_args *faa = aux;

	return OF_is_compatible(faa->fa_node, "arm,pl061");
}

void
plgpio_attach(struct device *parent, struct device *self, void *aux)
{
	struct plgpio_softc *sc = (struct plgpio_softc *)self;
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

	sc->sc_gc.gc_node = faa->fa_node;
	sc->sc_gc.gc_cookie = sc;
	sc->sc_gc.gc_config_pin = plgpio_config_pin;
	sc->sc_gc.gc_get_pin = plgpio_get_pin;
	sc->sc_gc.gc_set_pin = plgpio_set_pin;
	gpio_controller_register(&sc->sc_gc);

	printf("\n");
}

void
plgpio_config_pin(void *cookie, uint32_t *cells, int config)
{
	struct plgpio_softc *sc = cookie;
	uint32_t pin = cells[0];

	if (pin >= 8)
		return;

	if (config & GPIO_CONFIG_OUTPUT)
		HSET1(sc, GPIODIR, (1 << pin));
	else
		HCLR1(sc, GPIODIR, (1 << pin));
}

int
plgpio_get_pin(void *cookie, uint32_t *cells)
{
	struct plgpio_softc *sc = cookie;
	uint32_t pin = cells[0];
	uint32_t flags = cells[1];
	uint32_t reg;
	int val;

	if (pin >= 8)
		return 0;

	reg = HREAD1(sc, GPIODATA(pin));
	val = !!reg;
	if (flags & GPIO_ACTIVE_LOW)
		val = !val;
	return val;
}

void
plgpio_set_pin(void *cookie, uint32_t *cells, int val)
{
	struct plgpio_softc *sc = cookie;
	uint32_t pin = cells[0];
	uint32_t flags = cells[1];

	if (pin >= 8)
		return;

	if (flags & GPIO_ACTIVE_LOW)
		val = !val;
	if (val)
		HWRITE1(sc, GPIODATA(pin), (1 << pin));
	else
		HWRITE1(sc, GPIODATA(pin), 0);
}
