/*	$OpenBSD: sfgpio.c,v 1.3 2024/10/17 01:57:18 jsg Exp $	*/
/*
 * Copyright (c) 2022 Mark Kettenis <kettenis@openbsd.org>
 * Copyright (c) 2019 Patrick Wildt <patrick@blueri.se>
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
#include <dev/ofw/ofw_gpio.h>
#include <dev/ofw/fdt.h>

/* Registers. */
#define GPIO_INPUT_VAL		0x0000
#define GPIO_INPUT_EN		0x0004
#define GPIO_OUTPUT_EN		0x0008
#define GPIO_OUTPUT_VAL		0x000c
#define GPIO_PUE		0x0010
#define GPIO_DS			0x0014
#define GPIO_RISE_IE		0x0018
#define GPIO_RISE_IP		0x001c
#define GPIO_FALL_IE		0x0020
#define GPIO_FALL_IP		0x0024
#define GPIO_HIGH_IE		0x0028
#define GPIO_HIGH_IP		0x002c
#define GPIO_LOW_IE		0x0030
#define GPIO_LOW_IP		0x0034
#define GPIO_IOF_EN		0x0038
#define GPIO_IOF_SEL		0x003C
#define GPIO_OUT_XOR		0x0040

#define GPIO_NUM_PINS		16

#define HREAD4(sc, reg)							\
	(bus_space_read_4((sc)->sc_iot, (sc)->sc_ioh, (reg)))
#define HWRITE4(sc, reg, val)						\
	bus_space_write_4((sc)->sc_iot, (sc)->sc_ioh, (reg), (val))
#define HSET4(sc, reg, bits)						\
	HWRITE4((sc), (reg), HREAD4((sc), (reg)) | (bits))
#define HCLR4(sc, reg, bits)						\
	HWRITE4((sc), (reg), HREAD4((sc), (reg)) & ~(bits))

struct intrhand {
	int (*ih_func)(void *);		/* handler */
	void *ih_arg;			/* arg for handler */
	int ih_pin;			/* pin number */
	int ih_level;			/* trigger level */
	void *ih_sc;
};

struct sfgpio_softc {
	struct device		sc_dev;
	bus_space_tag_t		sc_iot;
	bus_space_handle_t	sc_ioh;
	int			sc_node;

	void			*sc_ih[GPIO_NUM_PINS];
	struct interrupt_controller sc_ic;

	struct gpio_controller	sc_gc;
};

int sfgpio_match(struct device *, void *, void *);
void sfgpio_attach(struct device *, struct device *, void *);

const struct cfattach sfgpio_ca = {
	sizeof (struct sfgpio_softc), sfgpio_match, sfgpio_attach
};

struct cfdriver sfgpio_cd = {
	NULL, "sfgpio", DV_DULL
};

void	sfgpio_config_pin(void *, uint32_t *, int);
int	sfgpio_get_pin(void *, uint32_t *);
void	sfgpio_set_pin(void *, uint32_t *, int);

int	sfgpio_intr(void *);
void	*sfgpio_intr_establish(void *, int *, int, struct cpu_info *,
	    int (*)(void *), void *, char *);
void	sfgpio_intr_disestablish(void *);
void	sfgpio_intr_enable(void *);
void	sfgpio_intr_disable(void *);
void	sfgpio_intr_barrier(void *);

int
sfgpio_match(struct device *parent, void *match, void *aux)
{
	struct fdt_attach_args *faa = aux;

	return OF_is_compatible(faa->fa_node, "sifive,gpio0");
}

void
sfgpio_attach(struct device *parent, struct device *self, void *aux)
{
	struct sfgpio_softc *sc = (struct sfgpio_softc *)self;
	struct fdt_attach_args *faa = aux;

	if (faa->fa_nreg < 1) {
		printf(": no registers\n");
		return;
	}

	sc->sc_node = faa->fa_node;
	sc->sc_iot = faa->fa_iot;

	if (bus_space_map(sc->sc_iot, faa->fa_reg[0].addr,
	    faa->fa_reg[0].size, 0, &sc->sc_ioh)) {
		printf(": can't map registers\n");
		return;
	}

	printf("\n");

	sc->sc_gc.gc_node = faa->fa_node;
	sc->sc_gc.gc_cookie = sc;
	sc->sc_gc.gc_config_pin = sfgpio_config_pin;
	sc->sc_gc.gc_get_pin = sfgpio_get_pin;
	sc->sc_gc.gc_set_pin = sfgpio_set_pin;
	gpio_controller_register(&sc->sc_gc);

	/* Disable all interrupts. */
	HWRITE4(sc, GPIO_RISE_IE, 0);
	HWRITE4(sc, GPIO_FALL_IE, 0);
	HWRITE4(sc, GPIO_HIGH_IE, 0);
	HWRITE4(sc, GPIO_LOW_IE, 0);

	sc->sc_ic.ic_node = faa->fa_node;
	sc->sc_ic.ic_cookie = sc;
	sc->sc_ic.ic_establish = sfgpio_intr_establish;
	sc->sc_ic.ic_disestablish = sfgpio_intr_disestablish;
	sc->sc_ic.ic_enable = sfgpio_intr_enable;
	sc->sc_ic.ic_disable = sfgpio_intr_disable;
	sc->sc_ic.ic_barrier = sfgpio_intr_barrier;
	fdt_intr_register(&sc->sc_ic);
}

void
sfgpio_config_pin(void *cookie, uint32_t *cells, int config)
{
	struct sfgpio_softc *sc = cookie;
	uint32_t pin = cells[0];

	if (pin >= GPIO_NUM_PINS)
		return;

	if (config & GPIO_CONFIG_OUTPUT) {
		HSET4(sc, GPIO_OUTPUT_EN, (1 << pin));
		HCLR4(sc, GPIO_INPUT_EN, (1 << pin));
	} else {
		HSET4(sc, GPIO_INPUT_EN, (1 << pin));
		HCLR4(sc, GPIO_OUTPUT_EN, (1 << pin));
	}
}

int
sfgpio_get_pin(void *cookie, uint32_t *cells)
{
	struct sfgpio_softc *sc = cookie;
	uint32_t pin = cells[0];
	uint32_t flags = cells[1];
	uint32_t reg;
	int val;

	if (pin >= GPIO_NUM_PINS)
		return 0;

	reg = HREAD4(sc, GPIO_INPUT_VAL);
	val = (reg >> pin) & 1;
	if (flags & GPIO_ACTIVE_LOW)
		val = !val;
	return val;
}

void
sfgpio_set_pin(void *cookie, uint32_t *cells, int val)
{
	struct sfgpio_softc *sc = cookie;
	uint32_t pin = cells[0];
	uint32_t flags = cells[1];

	if (pin >= GPIO_NUM_PINS)
		return;

	if (flags & GPIO_ACTIVE_LOW)
		val = !val;
	if (val)
		HSET4(sc, GPIO_OUTPUT_VAL, (1 << pin));
	else
		HCLR4(sc, GPIO_OUTPUT_VAL, (1 << pin));
}

int
sfgpio_intr(void *cookie)
{
	struct intrhand *ih = cookie;
	struct sfgpio_softc *sc = ih->ih_sc;
	int handled;

	handled = ih->ih_func(ih->ih_arg);

	switch (ih->ih_level) {
	case 1: /* rising */
		HSET4(sc, GPIO_RISE_IP, (1 << ih->ih_pin));
		break;
	case 2: /* falling */
		HSET4(sc, GPIO_FALL_IP, (1 << ih->ih_pin));
		break;
	case 4: /* high */
		HSET4(sc, GPIO_HIGH_IP, (1 << ih->ih_pin));
		break;
	case 8: /* low */
		HSET4(sc, GPIO_LOW_IP, (1 << ih->ih_pin));
		break;
	}

	return handled;
}

void *
sfgpio_intr_establish(void *cookie, int *cells, int ipl,
    struct cpu_info *ci, int (*func)(void *), void *arg, char *name)
{
	struct sfgpio_softc *sc = (struct sfgpio_softc *)cookie;
	struct intrhand	*ih;
	int pin = cells[0];
	int level = cells[1];

	if (pin < 0 || pin >= GPIO_NUM_PINS)
		panic("%s: bogus pin %d: %s", __func__, pin, name);

	if (sc->sc_ih[pin] != NULL)
		panic("%s: pin %d reused: %s", __func__, pin, name);

	ih = malloc(sizeof(*ih), M_DEVBUF, M_WAITOK);
	ih->ih_func = func;
	ih->ih_arg = arg;
	ih->ih_pin = pin;
	ih->ih_level = level;
	ih->ih_sc = sc;

	sc->sc_ih[pin] = fdt_intr_establish_idx_cpu(sc->sc_node, pin, ipl,
	    ci, sfgpio_intr, ih, name);
	if (sc->sc_ih[pin] == NULL) {
		free(ih, M_DEVBUF, sizeof(*ih));
		return NULL;
	}

	HSET4(sc, GPIO_INPUT_EN, (1 << pin));

	switch (level) {
	case 1: /* rising */
		HSET4(sc, GPIO_RISE_IP, (1 << pin));
		HSET4(sc, GPIO_RISE_IE, (1 << pin));
		break;
	case 2: /* falling */
		HSET4(sc, GPIO_FALL_IP, (1 << pin));
		HSET4(sc, GPIO_FALL_IE, (1 << pin));
		break;
	case 4: /* high */
		HSET4(sc, GPIO_HIGH_IP, (1 << pin));
		HSET4(sc, GPIO_HIGH_IE, (1 << pin));
		break;
	case 8: /* low */
		HSET4(sc, GPIO_LOW_IP, (1 << pin));
		HSET4(sc, GPIO_LOW_IE, (1 << pin));
		break;
	default:
		panic("%s: unsupported trigger type", __func__);
	}

	return ih;
}

void
sfgpio_intr_disestablish(void *cookie)
{
	struct intrhand	*ih = cookie;
	struct sfgpio_softc *sc = ih->ih_sc;

	sfgpio_intr_disable(ih);

	fdt_intr_disestablish(sc->sc_ih[ih->ih_pin]);
	sc->sc_ih[ih->ih_pin] = NULL;
	free(ih, M_DEVBUF, sizeof(*ih));
}

void
sfgpio_intr_enable(void *cookie)
{
	struct intrhand	*ih = cookie;
	struct sfgpio_softc *sc = ih->ih_sc;

	switch (ih->ih_level) {
	case 1: /* rising */
		HSET4(sc, GPIO_RISE_IE, (1 << ih->ih_pin));
		break;
	case 2: /* falling */
		HSET4(sc, GPIO_FALL_IE, (1 << ih->ih_pin));
		break;
	case 4: /* high */
		HSET4(sc, GPIO_HIGH_IE, (1 << ih->ih_pin));
		break;
	case 8: /* low */
		HSET4(sc, GPIO_LOW_IE, (1 << ih->ih_pin));
		break;
	}
}

void
sfgpio_intr_disable(void *cookie)
{
	struct intrhand *ih = cookie;
	struct sfgpio_softc *sc = ih->ih_sc;

	switch (ih->ih_level) {
	case 1: /* rising */
		HCLR4(sc, GPIO_RISE_IE, (1 << ih->ih_pin));
		break;
	case 2: /* falling */
		HCLR4(sc, GPIO_FALL_IE, (1 << ih->ih_pin));
		break;
	case 4: /* high */
		HCLR4(sc, GPIO_HIGH_IE, (1 << ih->ih_pin));
		break;
	case 8: /* low */
		HCLR4(sc, GPIO_LOW_IE, (1 << ih->ih_pin));
		break;
	}
}

void
sfgpio_intr_barrier(void *cookie)
{
	struct intrhand	*ih = cookie;
	struct sfgpio_softc *sc = ih->ih_sc;

	intr_barrier(sc->sc_ih[ih->ih_pin]);
}
