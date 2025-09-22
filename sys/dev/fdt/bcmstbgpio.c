/*	$OpenBSD: bcmstbgpio.c,v 1.2 2025/09/08 19:32:18 kettenis Exp $	*/
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
#include <sys/evcount.h>
#include <sys/malloc.h>

#include <machine/intr.h>
#include <machine/bus.h>
#include <machine/fdt.h>

#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_gpio.h>
#include <dev/ofw/fdt.h>

/* Registers. */
#define GIO_DATA(_bank)		(0x04 + (_bank) * 32)
#define GIO_IODIR(_bank)	(0x08 + (_bank) * 32)
#define GIO_EC(_bank)		(0x0c + (_bank) * 32)
#define GIO_EI(_bank)		(0x10 + (_bank) * 32)
#define GIO_MASK(_bank)		(0x14 + (_bank) * 32)
#define GIO_LEVEL(_bank)	(0x18 + (_bank) * 32)
#define GIO_STAT(_bank)		(0x1c + (_bank) * 32)

#define HREAD4(sc, reg)							\
	(bus_space_read_4((sc)->sc_iot, (sc)->sc_ioh, (reg)))
#define HWRITE4(sc, reg, val)						\
	bus_space_write_4((sc)->sc_iot, (sc)->sc_ioh, (reg), (val))
#define HSET4(sc, reg, bits)						\
	HWRITE4((sc), (reg), HREAD4((sc), (reg)) | (bits))
#define HCLR4(sc, reg, bits)						\
	HWRITE4((sc), (reg), HREAD4((sc), (reg)) & ~(bits))

struct intrhand {
	int (*ih_func)(void *);
	void *ih_arg;
	int ih_ipl;
	int ih_irq;
	int ih_edge;
	struct evcount	ih_count;
	char *ih_name;
	void *ih_sc;
};

struct bcmstbgpio_softc {
	struct device		sc_dev;
	bus_space_tag_t		sc_iot;
	bus_space_handle_t	sc_ioh;
	u_int			sc_nbanks;
	int			sc_node;

	void			*sc_ih;
	int			sc_ipl;
	struct intrhand		**sc_handlers;
	struct interrupt_controller sc_ic;

	struct gpio_controller	sc_gc;
};

int bcmstbgpio_match(struct device *, void *, void *);
void bcmstbgpio_attach(struct device *, struct device *, void *);

const struct cfattach	bcmstbgpio_ca = {
	sizeof (struct bcmstbgpio_softc), bcmstbgpio_match, bcmstbgpio_attach
};

struct cfdriver bcmstbgpio_cd = {
	NULL, "bcmstbgpio", DV_DULL
};

void	bcmstbgpio_config_pin(void *, uint32_t *, int);
int	bcmstbgpio_get_pin(void *, uint32_t *);
void	bcmstbgpio_set_pin(void *, uint32_t *, int);
void 	*bcmstbgpio_intr_establish_pin(void *, uint32_t *, int,
	    struct cpu_info *, int (*)(void *), void *, char *);

int	bcmstbgpio_intr(void *);
void	*bcmstbgpio_intr_establish(void *, int *, int, struct cpu_info *,
	    int (*)(void *), void *, char *);
void	bcmstbgpio_intr_enable(void *);
void	bcmstbgpio_intr_disable(void *);
void	bcmstbgpio_intr_barrier(void *);

int
bcmstbgpio_match(struct device *parent, void *match, void *aux)
{
	struct fdt_attach_args *faa = aux;

	return OF_is_compatible(faa->fa_node, "brcm,brcmstb-gpio");
}

void
bcmstbgpio_attach(struct device *parent, struct device *self, void *aux)
{
	struct bcmstbgpio_softc *sc = (struct bcmstbgpio_softc *)self;
	struct fdt_attach_args *faa = aux;
	u_int bank;

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
	sc->sc_nbanks = faa->fa_reg[0].size / 32;
	sc->sc_node = faa->fa_node;

	sc->sc_ih = fdt_intr_establish(faa->fa_node, IPL_NONE,
	    bcmstbgpio_intr, sc, sc->sc_dev.dv_xname);
	sc->sc_ipl = IPL_NONE;

	printf("\n");

	for (bank = 0; bank < sc->sc_nbanks; bank++)
		HWRITE4(sc, GIO_MASK(bank), 0);

	sc->sc_handlers = mallocarray(sc->sc_nbanks * 32,
	    sizeof(struct intrhand *), M_DEVBUF, M_WAITOK | M_ZERO);

	sc->sc_ic.ic_node = faa->fa_node;
	sc->sc_ic.ic_cookie = sc;
	sc->sc_ic.ic_establish = bcmstbgpio_intr_establish;
	sc->sc_ic.ic_enable = bcmstbgpio_intr_enable;
	sc->sc_ic.ic_disable = bcmstbgpio_intr_enable;
	sc->sc_ic.ic_barrier = bcmstbgpio_intr_barrier;
	fdt_intr_register(&sc->sc_ic);

	sc->sc_gc.gc_node = faa->fa_node;
	sc->sc_gc.gc_cookie = sc;
	sc->sc_gc.gc_config_pin = bcmstbgpio_config_pin;
	sc->sc_gc.gc_get_pin = bcmstbgpio_get_pin;
	sc->sc_gc.gc_set_pin = bcmstbgpio_set_pin;
	sc->sc_gc.gc_intr_establish = bcmstbgpio_intr_establish_pin;
	gpio_controller_register(&sc->sc_gc);
}

void
bcmstbgpio_config_pin(void *cookie, uint32_t *cells, int config)
{
	struct bcmstbgpio_softc *sc = cookie;
	u_int bank = cells[0] / 32;
	u_int pin = cells[0] % 32;

	if (bank >= sc->sc_nbanks)
		return;

	if (config & GPIO_CONFIG_OUTPUT)
		HCLR4(sc, GIO_IODIR(bank), 1U << pin);
	else
		HSET4(sc, GIO_IODIR(bank), 1U << pin);
}

int
bcmstbgpio_get_pin(void *cookie, uint32_t *cells)
{
	struct bcmstbgpio_softc *sc = cookie;
	uint32_t bank = cells[0] / 32;
	uint32_t pin = cells[0] % 32;
	uint32_t flags = cells[1];
	uint32_t reg;
	int val;

	if (bank >= sc->sc_nbanks)
		return 0;

	reg = HREAD4(sc, GIO_DATA(bank));
	val = !!(reg & (1U << pin));
	if (flags & GPIO_ACTIVE_LOW)
		val = !val;
	return val;
}

void
bcmstbgpio_set_pin(void *cookie, uint32_t *cells, int val)
{
	struct bcmstbgpio_softc *sc = cookie;
	uint32_t bank = cells[0] / 32;;
	uint32_t pin = cells[0] % 32;
	uint32_t flags = cells[1];

	if (bank >= sc->sc_nbanks)
		return;

	if (flags & GPIO_ACTIVE_LOW)
		val = !val;
	if (val)
		HSET4(sc, GIO_DATA(bank), 1U << pin);
	else
		HCLR4(sc, GIO_DATA(bank), 1U << pin);
}

void *
bcmstbgpio_intr_establish_pin(void *cookie, uint32_t *cells, int ipl,
    struct cpu_info *ci, int (*func)(void *), void *arg, char *name)
{
	struct bcmstbgpio_softc *sc = cookie;
	uint32_t icells[2];

	icells[0] = cells[0];
	icells[1] = 3; /* both edges */

	return bcmstbgpio_intr_establish(sc, icells, ipl, ci, func, arg, name);
}

int
bcmstbgpio_intr(void *arg)
{
	struct bcmstbgpio_softc *sc = arg;
	u_int bank;
	int handled = 0;

	for (bank = 0; bank < sc->sc_nbanks; bank++) {
		struct intrhand *ih;
		uint32_t mask, stat;
		u_int pin;
		int s;

		mask = HREAD4(sc, GIO_MASK(bank));
		stat = HREAD4(sc, GIO_STAT(bank)) & mask;
		if (stat == 0)
			continue;

		while (stat) {
			pin = ffs(stat) - 1;
			stat &= ~(1U << pin);

			ih = sc->sc_handlers[bank * 32 + pin];
			KASSERT(ih);

			if (ih->ih_edge)
				HWRITE4(sc, GIO_STAT(bank), 1U << pin);

			s = splraise(ih->ih_ipl);
			if (ih->ih_func(ih->ih_arg))
				ih->ih_count.ec_count++;
			splx(s);

			if (!ih->ih_edge)
				HWRITE4(sc, GIO_STAT(bank), 1U << pin);

			handled = 1;
		}
	}

	return handled;
}

void
bcmstbgpio_recalc_ipl(struct bcmstbgpio_softc *sc)
{
	struct intrhand	*ih;
	int max = IPL_NONE;
	int min = IPL_HIGH;
	int irq;

	for (irq = 0; irq < sc->sc_nbanks * 32; irq++) {
		ih = sc->sc_handlers[irq];
		if (ih == NULL)
			continue;

		if (ih->ih_ipl > max)
			max = ih->ih_ipl;

		if (ih->ih_ipl < min)
			min = ih->ih_ipl;
	}

	if (max == IPL_NONE)
		min = IPL_NONE;

	if (sc->sc_ipl != max) {
		sc->sc_ipl = max;

		fdt_intr_disestablish(sc->sc_ih);
		sc->sc_ih = fdt_intr_establish(sc->sc_node,
		    sc->sc_ipl, bcmstbgpio_intr, sc, sc->sc_dev.dv_xname);
		KASSERT(sc->sc_ih);
	}
}

void *
bcmstbgpio_intr_establish(void *cookie, int *cells, int ipl,
    struct cpu_info *ci, int (*func)(void *), void *arg, char *name)
{
	struct bcmstbgpio_softc *sc = cookie;
	struct intrhand *ih;
	int irq = cells[0];
	int type = cells[1];
	u_int bank = irq / 32;;
	u_int pin = irq % 32;
	uint32_t ec, ei, level;
	int edge = 0;

	/* Interrupt handling is an optional feature. */
	if (sc->sc_ih == NULL)
		return NULL;

	if (bank >= sc->sc_nbanks)
		return NULL;

	/* We don't support interrupt sharing. */
	if (sc->sc_handlers[irq])
		return NULL;

	ec = HREAD4(sc, GIO_EC(bank)) & ~(1U << pin);
	ei = HREAD4(sc, GIO_EI(bank)) & ~(1U << pin);
	level = HREAD4(sc, GIO_LEVEL(bank)) & ~(1U << pin);

	switch (type) {
	case 1: /* rising */
		ec |= (1U << pin);
		edge = 1;
		break;
	case 2: /* falling */
		edge = 1;
		break;
	case 3: /* both */
		ei |= (1U << pin);
		edge = 1;
		break;
	case 4: /* high */
		ec |= (1U << pin);
		level |= (1U << pin);
		break;
	case 8: /* low */
		level |= (1U << pin);
		break;
	default:
		return NULL;
	}

	ih = malloc(sizeof(*ih), M_DEVBUF, M_WAITOK);
	ih->ih_func = func;
	ih->ih_arg = arg;
	ih->ih_ipl = ipl & IPL_IRQMASK;
	ih->ih_irq = irq;
	ih->ih_edge = edge;
	ih->ih_name = name;
	ih->ih_sc = sc;

	if (name)
		evcount_attach(&ih->ih_count, name, &ih->ih_irq);

	sc->sc_handlers[ih->ih_irq] = ih;

	bcmstbgpio_recalc_ipl(sc);

	HWRITE4(sc, GIO_EC(bank), ec);
	HWRITE4(sc, GIO_EI(bank), ei);
	HWRITE4(sc, GIO_LEVEL(bank), level);

	HWRITE4(sc, GIO_STAT(bank), 1U << pin);
	HSET4(sc, GIO_MASK(bank), 1U << pin);

	return ih;
}

void
bcmstbgpio_intr_enable(void *cookie)
{
	struct intrhand *ih = cookie;
	struct bcmstbgpio_softc *sc = ih->ih_sc;
	uint32_t bank = ih->ih_irq / 32;
	uint32_t pin = ih->ih_irq % 32;

	HSET4(sc, GIO_MASK(bank), 1U << pin);
}

void
bcmstbgpio_intr_disable(void *cookie)
{
	struct intrhand *ih = cookie;
	struct bcmstbgpio_softc *sc = ih->ih_sc;
	uint32_t bank = ih->ih_irq / 32;
	uint32_t pin = ih->ih_irq % 32;

	HCLR4(sc, GIO_MASK(bank), 1U << pin);
}

void
bcmstbgpio_intr_barrier(void *cookie)
{
	struct intrhand *ih = cookie;
	struct bcmstbgpio_softc *sc = ih->ih_sc;

	intr_barrier(sc->sc_ih);
}
