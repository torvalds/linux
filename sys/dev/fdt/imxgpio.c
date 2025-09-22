/* $OpenBSD: imxgpio.c,v 1.7 2023/03/05 14:45:07 patrick Exp $ */
/*
 * Copyright (c) 2007,2009 Dale Rahn <drahn@openbsd.org>
 * Copyright (c) 2012-2013 Patrick Wildt <patrick@blueri.se>
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
#include <sys/queue.h>
#include <sys/device.h>
#include <sys/malloc.h>
#include <sys/evcount.h>

#include <machine/bus.h>
#include <machine/fdt.h>
#include <machine/intr.h>

#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_gpio.h>
#include <dev/ofw/fdt.h>

/* iMX6 registers */
#define GPIO_DR			0x00
#define GPIO_GDIR		0x04
#define GPIO_PSR		0x08
#define GPIO_ICR1		0x0C
#define GPIO_ICR2		0x10
#define GPIO_IMR		0x14
#define GPIO_ISR		0x18
#define GPIO_EDGE_SEL		0x1C

#define GPIO_NUM_PINS		32

struct intrhand {
	int (*ih_func)(void *);		/* handler */
	void *ih_arg;			/* arg for handler */
	int ih_ipl;			/* IPL_* */
	int ih_irq;			/* IRQ number */
	int ih_level;			/* GPIO level */
	struct evcount	ih_count;
	char *ih_name;
	void *ih_sc;
};

struct imxgpio_softc {
	struct device		sc_dev;
	bus_space_tag_t		sc_iot;
	bus_space_handle_t	sc_ioh;
	int			sc_node;

	void			*sc_ih_h;
	void			*sc_ih_l;
	int			sc_ipl;
	int			sc_irq;
	struct intrhand		*sc_handlers[GPIO_NUM_PINS];
	struct interrupt_controller sc_ic;

	struct gpio_controller sc_gc;
};

int imxgpio_match(struct device *, void *, void *);
void imxgpio_attach(struct device *, struct device *, void *);

void imxgpio_config_pin(void *, uint32_t *, int);
int imxgpio_get_pin(void *, uint32_t *);
void imxgpio_set_pin(void *, uint32_t *, int);

int imxgpio_intr(void *);
void *imxgpio_intr_establish(void *, int *, int, struct cpu_info *,
    int (*)(void *), void *, char *);
void imxgpio_intr_disestablish(void *);
void imxgpio_recalc_ipl(struct imxgpio_softc *);
void imxgpio_intr_enable(void *);
void imxgpio_intr_disable(void *);
void imxgpio_intr_barrier(void *);


const struct cfattach	imxgpio_ca = {
	sizeof (struct imxgpio_softc), imxgpio_match, imxgpio_attach
};

struct cfdriver imxgpio_cd = {
	NULL, "imxgpio", DV_DULL
};

int
imxgpio_match(struct device *parent, void *match, void *aux)
{
	struct fdt_attach_args *faa = aux;

	return OF_is_compatible(faa->fa_node, "fsl,imx35-gpio");
}

void
imxgpio_attach(struct device *parent, struct device *self, void *aux)
{
	struct imxgpio_softc *sc = (struct imxgpio_softc *)self;
	struct fdt_attach_args *faa = aux;

	if (faa->fa_nreg < 1)
		return;

	sc->sc_node = faa->fa_node;
	sc->sc_iot = faa->fa_iot;
	if (bus_space_map(sc->sc_iot, faa->fa_reg[0].addr,
	    faa->fa_reg[0].size, 0, &sc->sc_ioh))
		panic("imxgpio_attach: bus_space_map failed!");

	sc->sc_gc.gc_node = faa->fa_node;
	sc->sc_gc.gc_cookie = sc;
	sc->sc_gc.gc_config_pin = imxgpio_config_pin;
	sc->sc_gc.gc_get_pin = imxgpio_get_pin;
	sc->sc_gc.gc_set_pin = imxgpio_set_pin;
	gpio_controller_register(&sc->sc_gc);

	sc->sc_ipl = IPL_NONE;
	bus_space_write_4(sc->sc_iot, sc->sc_ioh, GPIO_IMR, 0);
	bus_space_write_4(sc->sc_iot, sc->sc_ioh, GPIO_ISR, ~0);
	bus_space_write_4(sc->sc_iot, sc->sc_ioh, GPIO_EDGE_SEL, 0);

	sc->sc_ic.ic_node = faa->fa_node;
	sc->sc_ic.ic_cookie = sc;
	sc->sc_ic.ic_establish = imxgpio_intr_establish;
	sc->sc_ic.ic_disestablish = imxgpio_intr_disestablish;
	sc->sc_ic.ic_enable = imxgpio_intr_enable;
	sc->sc_ic.ic_disable = imxgpio_intr_disable;
	sc->sc_ic.ic_barrier = imxgpio_intr_barrier;
	fdt_intr_register(&sc->sc_ic);

	printf("\n");

	/* XXX - SYSCONFIG */
	/* XXX - CTRL */
	/* XXX - DEBOUNCE */
}

void
imxgpio_config_pin(void *cookie, uint32_t *cells, int config)
{
	struct imxgpio_softc *sc = cookie;
	uint32_t pin = cells[0];
	uint32_t val;

	if (pin >= GPIO_NUM_PINS)
		return;

	val = bus_space_read_4(sc->sc_iot, sc->sc_ioh, GPIO_GDIR);
	if (config & GPIO_CONFIG_OUTPUT)
		val |= 1 << pin;
	else
		val &= ~(1 << pin);
	bus_space_write_4(sc->sc_iot, sc->sc_ioh, GPIO_GDIR, val);
}

int
imxgpio_get_pin(void *cookie, uint32_t *cells)
{
	struct imxgpio_softc *sc = cookie;
	uint32_t pin = cells[0];
	uint32_t flags = cells[1];
	uint32_t reg;
	int val;

	reg = bus_space_read_4(sc->sc_iot, sc->sc_ioh, GPIO_DR);
	reg &= (1 << pin);
	val = (reg >> pin) & 1;
	if (flags & GPIO_ACTIVE_LOW)
		val = !val;
	return val;
}

void
imxgpio_set_pin(void *cookie, uint32_t *cells, int val)
{
	struct imxgpio_softc *sc = cookie;
	uint32_t pin = cells[0];
	uint32_t flags = cells[1];
	uint32_t reg;

	reg = bus_space_read_4(sc->sc_iot, sc->sc_ioh, GPIO_DR);
	if (flags & GPIO_ACTIVE_LOW)
		val = !val;
	if (val)
		reg |= (1 << pin);
	else
		reg &= ~(1 << pin);
	bus_space_write_4(sc->sc_iot, sc->sc_ioh, GPIO_DR, reg);
}

int
imxgpio_intr(void *cookie)
{
	struct imxgpio_softc	*sc = (struct imxgpio_softc *)cookie;
	struct intrhand		*ih;
	uint32_t		 status, pending, mask;
	int			 pin, s;

	status = bus_space_read_4(sc->sc_iot, sc->sc_ioh, GPIO_ISR);
	mask = bus_space_read_4(sc->sc_iot, sc->sc_ioh, GPIO_IMR);

	status &= mask;
	pending = status;

	while (pending) {
		pin = ffs(pending) - 1;

		if ((ih = sc->sc_handlers[pin]) != NULL) {
			s = splraise(ih->ih_ipl);
			if (ih->ih_func(ih->ih_arg))
				ih->ih_count.ec_count++;
			splx(s);
		}

		pending &= ~(1 << pin);
	}

	bus_space_write_4(sc->sc_iot, sc->sc_ioh, GPIO_ISR, status);

	return 1;
}

void *
imxgpio_intr_establish(void *cookie, int *cells, int ipl,
    struct cpu_info *ci, int (*func)(void *), void *arg, char *name)
{
	struct imxgpio_softc	*sc = (struct imxgpio_softc *)cookie;
	struct intrhand		*ih;
	int			 s, val, reg, shift;
	int			 irqno = cells[0];
	int			 level = cells[1];

	if (irqno < 0 || irqno >= GPIO_NUM_PINS)
		panic("%s: bogus irqnumber %d: %s", __func__,
		     irqno, name);

	if (sc->sc_handlers[irqno] != NULL)
		panic("%s: irqnumber %d reused: %s", __func__,
		     irqno, name);

	if (ci != NULL && !CPU_IS_PRIMARY(ci))
		return NULL;

	ih = malloc(sizeof(*ih), M_DEVBUF, M_WAITOK);
	ih->ih_func = func;
	ih->ih_arg = arg;
	ih->ih_ipl = ipl & IPL_IRQMASK;
	ih->ih_irq = irqno;
	ih->ih_name = name;
	ih->ih_level = level;
	ih->ih_sc = sc;

	s = splhigh();

	sc->sc_handlers[irqno] = ih;

	if (name != NULL)
		evcount_attach(&ih->ih_count, name, &ih->ih_irq);

#ifdef DEBUG_INTC
	printf("%s: irq %d ipl %d [%s]\n", __func__, ih->ih_irq, ih->ih_ipl,
	    ih->ih_name);
#endif

	imxgpio_recalc_ipl(sc);

	switch (level) {
		case 1: /* rising */
			val = 2;
			break;
		case 2: /* falling */
			val = 3;
			break;
		case 4: /* high */
			val = 1;
			break;
		case 8: /* low */
			val = 0;
			break;
		default:
			panic("%s: unsupported trigger type", __func__);
	}

	if (irqno < 16) {
		reg = GPIO_ICR1;
		shift = irqno << 1;
	} else {
		reg = GPIO_ICR2;
		shift = (irqno - 16) << 1;
	}

	bus_space_write_4(sc->sc_iot, sc->sc_ioh, reg,
	    bus_space_read_4(sc->sc_iot, sc->sc_ioh, reg) & ~(0x3 << shift));
	bus_space_write_4(sc->sc_iot, sc->sc_ioh, reg,
	    bus_space_read_4(sc->sc_iot, sc->sc_ioh, reg) | val << shift);

	bus_space_write_4(sc->sc_iot, sc->sc_ioh, GPIO_IMR,
	    bus_space_read_4(sc->sc_iot, sc->sc_ioh, GPIO_IMR) | 1 << irqno);

	splx(s);
	return (ih);
}

void
imxgpio_intr_disestablish(void *cookie)
{
	struct intrhand		*ih = cookie;
	struct imxgpio_softc	*sc = ih->ih_sc;
	uint32_t		 mask;
	int			 s;

	s = splhigh();

#ifdef DEBUG_INTC
	printf("%s: irq %d ipl %d [%s]\n", __func__, ih->ih_irq, ih->ih_ipl,
	    ih->ih_name);
#endif

	mask = bus_space_read_4(sc->sc_iot, sc->sc_ioh, GPIO_IMR);
	mask &= ~(1 << ih->ih_irq);
	bus_space_write_4(sc->sc_iot, sc->sc_ioh, GPIO_IMR, mask);

	sc->sc_handlers[ih->ih_irq] = NULL;
	if (ih->ih_name != NULL)
		evcount_detach(&ih->ih_count);
	free(ih, M_DEVBUF, sizeof(*ih));

	imxgpio_recalc_ipl(sc);

	splx(s);
}

void
imxgpio_recalc_ipl(struct imxgpio_softc *sc)
{
	struct intrhand		*ih;
	int			 pin;
	int			 max = IPL_NONE;
	int			 min = IPL_HIGH;

	for (pin = 0; pin < GPIO_NUM_PINS; pin++) {
		ih = sc->sc_handlers[pin];
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

		if (sc->sc_ih_l != NULL)
			fdt_intr_disestablish(sc->sc_ih_l);

		if (sc->sc_ih_h != NULL)
			fdt_intr_disestablish(sc->sc_ih_h);

		if (sc->sc_ipl != IPL_NONE) {
			sc->sc_ih_l = fdt_intr_establish_idx(sc->sc_node, 0,
			    sc->sc_ipl, imxgpio_intr, sc, sc->sc_dev.dv_xname);
			sc->sc_ih_h = fdt_intr_establish_idx(sc->sc_node, 1,
			    sc->sc_ipl, imxgpio_intr, sc, sc->sc_dev.dv_xname);
		}
	}
}

void
imxgpio_intr_enable(void *cookie)
{
	struct intrhand		*ih = cookie;
	struct imxgpio_softc	*sc = ih->ih_sc;
	uint32_t		 mask;
	int			 s;

	s = splhigh();
	mask = bus_space_read_4(sc->sc_iot, sc->sc_ioh, GPIO_IMR);
	mask |= (1 << ih->ih_irq);
	bus_space_write_4(sc->sc_iot, sc->sc_ioh, GPIO_IMR, mask);
	splx(s);
}

void
imxgpio_intr_disable(void *cookie)
{
	struct intrhand		*ih = cookie;
	struct imxgpio_softc	*sc = ih->ih_sc;
	uint32_t		 mask;
	int			 s;

	s = splhigh();
	mask = bus_space_read_4(sc->sc_iot, sc->sc_ioh, GPIO_IMR);
	mask &= ~(1 << ih->ih_irq);
	bus_space_write_4(sc->sc_iot, sc->sc_ioh, GPIO_IMR, mask);
	splx(s);
}

void
imxgpio_intr_barrier(void *cookie)
{
	struct intrhand		*ih = cookie;
	struct imxgpio_softc	*sc = ih->ih_sc;

	if (sc->sc_ih_h && ih->ih_irq >= 16)
		intr_barrier(sc->sc_ih_h);
	else if (sc->sc_ih_l)
		intr_barrier(sc->sc_ih_l);
}
