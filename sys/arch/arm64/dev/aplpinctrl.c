/*	$OpenBSD: aplpinctrl.c,v 1.8 2023/07/23 11:17:50 kettenis Exp $	*/
/*
 * Copyright (c) 2021 Mark Kettenis <kettenis@openbsd.org>
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
#include <sys/evcount.h>
#include <sys/malloc.h>

#include <machine/bus.h>
#include <machine/fdt.h>
#include <machine/intr.h>

#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_gpio.h>
#include <dev/ofw/ofw_pinctrl.h>
#include <dev/ofw/ofw_power.h>
#include <dev/ofw/fdt.h>

#define APPLE_PIN(pinmux) ((pinmux) & 0xffff)
#define APPLE_FUNC(pinmux) ((pinmux) >> 16)

#define GPIO_PIN(pin)		((pin) * 4)
#define  GPIO_PIN_GROUP_MASK	(7 << 16)
#define  GPIO_PIN_INPUT_ENABLE	(1 << 9)
#define  GPIO_PIN_FUNC_MASK	(3 << 5)
#define  GPIO_PIN_FUNC_SHIFT	5
#define  GPIO_PIN_MODE_MASK	(7 << 1)
#define  GPIO_PIN_MODE_INPUT	(0 << 1)
#define  GPIO_PIN_MODE_OUTPUT	(1 << 1)
#define  GPIO_PIN_MODE_IRQ_HI	(2 << 1)
#define  GPIO_PIN_MODE_IRQ_LO	(3 << 1)
#define  GPIO_PIN_MODE_IRQ_UP	(4 << 1)
#define  GPIO_PIN_MODE_IRQ_DN	(5 << 1)
#define  GPIO_PIN_MODE_IRQ_ANY	(6 << 1)
#define  GPIO_PIN_MODE_IRQ_OFF	(7 << 1)
#define  GPIO_PIN_DATA		(1 << 0)
#define GPIO_IRQ(grp, pin)	(0x800 + (grp) * 64 + ((pin) >> 5) * 4)

#define HREAD4(sc, reg)						\
	(bus_space_read_4((sc)->sc_iot, (sc)->sc_ioh, (reg)))
#define HWRITE4(sc, reg, val)						\
	bus_space_write_4((sc)->sc_iot, (sc)->sc_ioh, (reg), (val))
#define HSET4(sc, reg, bits)						\
	HWRITE4((sc), (reg), HREAD4((sc), (reg)) | (bits))
#define HCLR4(sc, reg, bits)						\
	HWRITE4((sc), (reg), HREAD4((sc), (reg)) & ~(bits))

struct intrhand {
	TAILQ_ENTRY(intrhand) ih_list;
	int (*ih_func)(void *);
	void *ih_arg;
	int ih_irq;
	int ih_type;
	int ih_ipl;
	struct evcount ih_count;
	char *ih_name;
	void *ih_sc;
};

struct aplpinctrl_softc {
	struct device		sc_dev;
	bus_space_tag_t		sc_iot;
	bus_space_handle_t	sc_ioh;

	int			sc_ngpios;
	struct gpio_controller	sc_gc;

	void			*sc_ih;
	TAILQ_HEAD(, intrhand)	*sc_handler;
	struct interrupt_controller sc_ic;
};

int	aplpinctrl_match(struct device *, void *, void *);
void	aplpinctrl_attach(struct device *, struct device *, void *);

const struct cfattach aplpinctrl_ca = {
	sizeof (struct aplpinctrl_softc), aplpinctrl_match, aplpinctrl_attach
};

struct cfdriver aplpinctrl_cd = {
	NULL, "aplpinctrl", DV_DULL
};

int	aplpinctrl_pinctrl(uint32_t, void *);
void	aplpinctrl_config_pin(void *, uint32_t *, int);
int	aplpinctrl_get_pin(void *, uint32_t *);
void	aplpinctrl_set_pin(void *, uint32_t *, int);

int	aplpinctrl_intr(void *);
void	*aplpinctrl_intr_establish(void *, int *, int, struct cpu_info *,
	    int (*)(void *), void *, char *);
void	aplpinctrl_intr_disestablish(void *);
void	aplpinctrl_intr_enable(void *);
void	aplpinctrl_intr_disable(void *);
void	aplpinctrl_intr_barrier(void *);

int
aplpinctrl_match(struct device *parent, void *match, void *aux)
{
	struct fdt_attach_args *faa = aux;

	return OF_is_compatible(faa->fa_node, "apple,pinctrl");
}

void
aplpinctrl_attach(struct device *parent, struct device *self, void *aux)
{
	struct aplpinctrl_softc *sc = (struct aplpinctrl_softc *)self;
	struct fdt_attach_args *faa = aux;
	uint32_t gpio_ranges[4] = {};
	int i;

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

	power_domain_enable(faa->fa_node);

	pinctrl_register(faa->fa_node, aplpinctrl_pinctrl, sc);

	OF_getpropintarray(faa->fa_node, "gpio-ranges",
	    gpio_ranges, sizeof(gpio_ranges));
	sc->sc_ngpios = gpio_ranges[3];
	if (sc->sc_ngpios == 0) {
		printf("\n");
		return;
	}

	sc->sc_gc.gc_node = faa->fa_node;
	sc->sc_gc.gc_cookie = sc;
	sc->sc_gc.gc_config_pin = aplpinctrl_config_pin;
	sc->sc_gc.gc_get_pin = aplpinctrl_get_pin;
	sc->sc_gc.gc_set_pin = aplpinctrl_set_pin;
	gpio_controller_register(&sc->sc_gc);

	sc->sc_ih = fdt_intr_establish_idx(faa->fa_node, 0, IPL_BIO,
	    aplpinctrl_intr, sc, sc->sc_dev.dv_xname);
	if (sc->sc_ih == NULL) {
		printf(": can't establish interrupt\n");
		return;
	}

	sc->sc_handler = mallocarray(sc->sc_ngpios,
	    sizeof(*sc->sc_handler), M_DEVBUF, M_ZERO | M_WAITOK);
	for (i = 0; i < sc->sc_ngpios; i++)
		TAILQ_INIT(&sc->sc_handler[i]);

	sc->sc_ic.ic_node = faa->fa_node;
	sc->sc_ic.ic_cookie = sc;
	sc->sc_ic.ic_establish = aplpinctrl_intr_establish;
	sc->sc_ic.ic_disestablish = aplpinctrl_intr_disestablish;
	sc->sc_ic.ic_enable = aplpinctrl_intr_enable;
	sc->sc_ic.ic_disable = aplpinctrl_intr_disable;
	sc->sc_ic.ic_barrier = aplpinctrl_intr_barrier;
	fdt_intr_register(&sc->sc_ic);

	printf("\n");
}

int
aplpinctrl_pinctrl(uint32_t phandle, void *cookie)
{
	struct aplpinctrl_softc *sc = cookie;
	uint32_t *pinmux;
	int node, len, i;
	uint16_t pin, func;
	uint32_t reg;

	node = OF_getnodebyphandle(phandle);
	if (node == 0)
		return -1;

	len = OF_getproplen(node, "pinmux");
	if (len <= 0)
		return -1;

	pinmux = malloc(len, M_TEMP, M_WAITOK);
	OF_getpropintarray(node, "pinmux", pinmux, len);

	for (i = 0; i < len / sizeof(uint32_t); i++) {
		pin = APPLE_PIN(pinmux[i]);
		func = APPLE_FUNC(pinmux[i]);
		reg = HREAD4(sc, GPIO_PIN(pin));
		reg &= ~GPIO_PIN_FUNC_MASK;
		reg |= (func << GPIO_PIN_FUNC_SHIFT) & GPIO_PIN_FUNC_MASK;
		HWRITE4(sc, GPIO_PIN(pin), reg);
	}

	free(pinmux, M_TEMP, len);
	return 0;
}

void
aplpinctrl_config_pin(void *cookie, uint32_t *cells, int config)
{
	struct aplpinctrl_softc *sc = cookie;
	uint32_t pin = cells[0];
	uint32_t reg;

	KASSERT(pin < sc->sc_ngpios);

	reg = HREAD4(sc, GPIO_PIN(pin));
	reg &= ~GPIO_PIN_FUNC_MASK;
	reg &= ~GPIO_PIN_MODE_MASK;
	if (config & GPIO_CONFIG_OUTPUT)
		reg |= GPIO_PIN_MODE_OUTPUT;
	else
		reg |= GPIO_PIN_MODE_INPUT;
	HWRITE4(sc, GPIO_PIN(pin), reg);
}

int
aplpinctrl_get_pin(void *cookie, uint32_t *cells)
{
	struct aplpinctrl_softc *sc = cookie;
	uint32_t pin = cells[0];
	uint32_t flags = cells[1];
	uint32_t reg;
	int val;

	KASSERT(pin < sc->sc_ngpios);

	reg = HREAD4(sc, GPIO_PIN(pin));
	val = !!(reg & GPIO_PIN_DATA);
	if (flags & GPIO_ACTIVE_LOW)
		val = !val;
	return val;
}

void
aplpinctrl_set_pin(void *cookie, uint32_t *cells, int val)
{
	struct aplpinctrl_softc *sc = cookie;
	uint32_t pin = cells[0];
	uint32_t flags = cells[1];

	KASSERT(pin < sc->sc_ngpios);

	if (flags & GPIO_ACTIVE_LOW)
		val = !val;
	if (val)
		HSET4(sc, GPIO_PIN(pin), GPIO_PIN_DATA);
	else
		HCLR4(sc, GPIO_PIN(pin), GPIO_PIN_DATA);
}

int
aplpinctrl_intr(void *arg)
{
	struct aplpinctrl_softc *sc = arg;
	struct intrhand *ih;
	uint32_t status, pending;
	int base, bit, pin, s;

	for (base = 0; base < sc->sc_ngpios; base += 32) {
		status = HREAD4(sc, GPIO_IRQ(0, base));
		pending = status;

		while (pending) {
			bit = ffs(pending) - 1;
			pin = base + bit;
			TAILQ_FOREACH(ih, &sc->sc_handler[pin], ih_list) {
				s = splraise(ih->ih_ipl);
				if (ih->ih_func(ih->ih_arg))
					ih->ih_count.ec_count++;
				splx(s);
			}

			pending &= ~(1 << bit);
		}

		HWRITE4(sc, GPIO_IRQ(0, base), status);
	}

	return 1;
}

void *
aplpinctrl_intr_establish(void *cookie, int *cells, int ipl,
    struct cpu_info *ci, int (*func)(void *), void *arg, char *name)
{
	struct aplpinctrl_softc *sc = cookie;
	struct intrhand *ih;
	uint32_t pin = cells[0];
	uint32_t type = IST_NONE;
	uint32_t reg;

	KASSERT(pin < sc->sc_ngpios);

	if (ci != NULL && !CPU_IS_PRIMARY(ci))
		return NULL;

	switch (cells[1]) {
	case 1:
		type = IST_EDGE_RISING;
		break;
	case 2:
		type = IST_EDGE_FALLING;
		break;
	case 3:
		type = IST_EDGE_BOTH;
		break;
	case 4:
		type = IST_LEVEL_HIGH;
		break;
	case 8:
		type = IST_LEVEL_LOW;
		break;
	}

	ih = TAILQ_FIRST(&sc->sc_handler[pin]);
	if (ih && ih->ih_type != type)
		return NULL;

	ih = malloc(sizeof(*ih), M_DEVBUF, M_WAITOK);
	ih->ih_func = func;
	ih->ih_arg = arg;
	ih->ih_irq = pin;
	ih->ih_type = type;
	ih->ih_ipl = ipl & IPL_IRQMASK;
	ih->ih_name = name;
	ih->ih_sc = sc;
	if (name != NULL)
		evcount_attach(&ih->ih_count, name, &ih->ih_irq);
	TAILQ_INSERT_TAIL(&sc->sc_handler[pin], ih, ih_list);

	reg = HREAD4(sc, GPIO_PIN(pin));
	reg &= ~GPIO_PIN_DATA;
	reg &= ~GPIO_PIN_FUNC_MASK;
	reg &= ~GPIO_PIN_MODE_MASK;
	switch (type) {
	case IST_NONE:
		reg |= GPIO_PIN_MODE_IRQ_OFF;
		break;
	case IST_EDGE_RISING:
		reg |= GPIO_PIN_MODE_IRQ_UP;
		break;
	case IST_EDGE_FALLING:
		reg |= GPIO_PIN_MODE_IRQ_DN;
		break;
	case IST_EDGE_BOTH:
		reg |= GPIO_PIN_MODE_IRQ_ANY;
		break;
	case IST_LEVEL_HIGH:
		reg |= GPIO_PIN_MODE_IRQ_HI;
		break;
	case IST_LEVEL_LOW:
		reg |= GPIO_PIN_MODE_IRQ_LO;
		break;
	}
	reg |= GPIO_PIN_INPUT_ENABLE;
	reg &= ~GPIO_PIN_GROUP_MASK;
	HWRITE4(sc, GPIO_PIN(pin), reg);

	return ih;
}

void
aplpinctrl_intr_disestablish(void *cookie)
{
	struct intrhand *ih = cookie;
	struct aplpinctrl_softc *sc = ih->ih_sc;
	uint32_t reg;
	int s;

	s = splhigh();

	TAILQ_REMOVE(&sc->sc_handler[ih->ih_irq], ih, ih_list);
	if (ih->ih_name)
		evcount_detach(&ih->ih_count);

	if (TAILQ_EMPTY(&sc->sc_handler[ih->ih_irq])) {
		reg = HREAD4(sc, GPIO_PIN(ih->ih_irq));
		reg &= ~GPIO_PIN_MODE_MASK;
		reg |= GPIO_PIN_MODE_IRQ_OFF;
		HWRITE4(sc, GPIO_PIN(ih->ih_irq), reg);
	}

	free(ih, M_DEVBUF, sizeof(*ih));

	splx(s);
}

void
aplpinctrl_intr_enable(void *cookie)
{
	struct intrhand *ih = cookie;
	struct aplpinctrl_softc *sc = ih->ih_sc;
	uint32_t reg;
	int s;

	s = splhigh();
	reg = HREAD4(sc, GPIO_PIN(ih->ih_irq));
	reg &= ~GPIO_PIN_MODE_MASK;
	switch (ih->ih_type) {
	case IST_NONE:
		reg |= GPIO_PIN_MODE_IRQ_OFF;
		break;
	case IST_EDGE_RISING:
		reg |= GPIO_PIN_MODE_IRQ_UP;
		break;
	case IST_EDGE_FALLING:
		reg |= GPIO_PIN_MODE_IRQ_DN;
		break;
	case IST_EDGE_BOTH:
		reg |= GPIO_PIN_MODE_IRQ_ANY;
		break;
	case IST_LEVEL_HIGH:
		reg |= GPIO_PIN_MODE_IRQ_HI;
		break;
	case IST_LEVEL_LOW:
		reg |= GPIO_PIN_MODE_IRQ_LO;
		break;
	}
	HWRITE4(sc, GPIO_PIN(ih->ih_irq), reg);
	splx(s);
}

void
aplpinctrl_intr_disable(void *cookie)
{
	struct intrhand *ih = cookie;
	struct aplpinctrl_softc *sc = ih->ih_sc;
	uint32_t reg;
	int s;

	s = splhigh();
	reg = HREAD4(sc, GPIO_PIN(ih->ih_irq));
	reg &= ~GPIO_PIN_MODE_MASK;
	reg |= GPIO_PIN_MODE_IRQ_OFF;
	HWRITE4(sc, GPIO_PIN(ih->ih_irq), reg);
	splx(s);
}

void
aplpinctrl_intr_barrier(void *cookie)
{
	struct intrhand *ih = cookie;
	struct aplpinctrl_softc *sc = ih->ih_sc;

	intr_barrier(sc->sc_ih);
}
