/*	$OpenBSD: bcmstbintc.c,v 1.1 2025/09/08 19:31:04 kettenis Exp $	*/
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
#include <sys/malloc.h>
#include <sys/evcount.h>

#include <machine/intr.h>
#include <machine/bus.h>
#include <machine/fdt.h>

#include <dev/ofw/openfirm.h>
#include <dev/ofw/fdt.h>

/* Registers. */
#define INTR_STATUS		0x00
#define INTR_MASK_STATUS	0x04
#define INTR_MASK_SET		0x08
#define INTR_MASK_CLEAR		0x0c

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
	struct evcount	ih_count;
	char *ih_name;
	void *ih_sc;
};

struct bcmstbintc_softc {
	struct device		sc_dev;
	bus_space_tag_t		sc_iot;
	bus_space_handle_t	sc_ioh;
	int			sc_node;

	void			*sc_ih;
	int			sc_ipl;
	struct interrupt_controller sc_ic;
	struct intrhand		sc_handlers[32];
};

int bcmstbintc_match(struct device *, void *, void *);
void bcmstbintc_attach(struct device *, struct device *, void *);

const struct cfattach	bcmstbintc_ca = {
	sizeof (struct bcmstbintc_softc), bcmstbintc_match, bcmstbintc_attach
};

struct cfdriver bcmstbintc_cd = {
	NULL, "bcmstbintc", DV_DULL
};

int	bcmstbintc_intr(void *);
void	*bcmstbintc_intr_establish(void *, int *, int, struct cpu_info *,
	    int (*)(void *), void *, char *);
void	bcmstbintc_intr_disestablish(void *);
void	bcmstbintc_intr_barrier(void *);

int
bcmstbintc_match(struct device *parent, void *match, void *aux)
{
	struct fdt_attach_args *faa = aux;

	return OF_is_compatible(faa->fa_node, "brcm,bcm7271-l2-intc");
}

void
bcmstbintc_attach(struct device *parent, struct device *self, void *aux)
{
	struct bcmstbintc_softc *sc = (struct bcmstbintc_softc *)self;
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
	sc->sc_node = faa->fa_node;

	sc->sc_ih = fdt_intr_establish(faa->fa_node, IPL_NONE,
	    bcmstbintc_intr, sc, sc->sc_dev.dv_xname);
	if (sc->sc_ih == NULL) {
		printf(": can't establish interrupt\n");
		return;
	}
	sc->sc_ipl = IPL_NONE;

	printf("\n");

	HWRITE4(sc, INTR_MASK_SET, 0xffffffff);

	sc->sc_ic.ic_node = faa->fa_node;
	sc->sc_ic.ic_cookie = sc;
	sc->sc_ic.ic_establish = bcmstbintc_intr_establish;
	sc->sc_ic.ic_disestablish = bcmstbintc_intr_disestablish;
	sc->sc_ic.ic_barrier = bcmstbintc_intr_barrier;
	fdt_intr_register(&sc->sc_ic);
}

int
bcmstbintc_intr(void *cookie)
{
	struct bcmstbintc_softc *sc = cookie;
	struct intrhand *ih;
	uint32_t status;
	int irq, s;

	status = HREAD4(sc, INTR_STATUS);
	if (status == 0)
		return 0;

	while (status) {
		irq = ffs(status) - 1;
		status &= ~(1U << irq);

		ih = &sc->sc_handlers[irq];
		if (ih != NULL) {
			s = splraise(ih->ih_ipl);
			if (ih->ih_func(ih->ih_arg))
				ih->ih_count.ec_count++;
			splx(s);
		}
	}

	return 1;
}

void
bcmstbintc_recalc_ipl(struct bcmstbintc_softc *sc)
{
	struct intrhand	*ih;
	int max = IPL_NONE;
	int min = IPL_HIGH;
	int irq;

	for (irq = 0; irq < 32; irq++) {
		ih = &sc->sc_handlers[irq];
		if (ih->ih_func == NULL)
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
		    sc->sc_ipl, bcmstbintc_intr, sc, sc->sc_dev.dv_xname);
		KASSERT(sc->sc_ih);
	}
}

void *
bcmstbintc_intr_establish(void *cookie, int *cells, int level,
    struct cpu_info *ci, int (*func)(void *), void *arg, char *name)
{
	struct bcmstbintc_softc *sc = cookie;
	struct intrhand *ih;
	int irq = cells[0];

	if (irq < 0 || irq >= 32)
		return NULL;

	ih = &sc->sc_handlers[irq];
	if (ih->ih_func)
		return NULL;

	ih->ih_func = func;
	ih->ih_arg = arg;
	ih->ih_ipl = level & IPL_IRQMASK;
	ih->ih_irq = irq;
	ih->ih_name = name;
	ih->ih_sc = sc;

	if (name)
		evcount_attach(&ih->ih_count, name, &ih->ih_irq);

	bcmstbintc_recalc_ipl(sc);

	HWRITE4(sc, INTR_MASK_CLEAR, 1U << ih->ih_irq);

	return ih;
}

void
bcmstbintc_intr_disestablish(void *cookie)
{
	struct intrhand *ih = cookie;
	struct bcmstbintc_softc *sc = ih->ih_sc;

	HWRITE4(sc, INTR_MASK_SET, 1U << ih->ih_irq);

	ih->ih_func = NULL;
	if (ih->ih_name)
		evcount_detach(&ih->ih_count);

	bcmstbintc_recalc_ipl(sc);
}

void
bcmstbintc_intr_barrier(void *cookie)
{
	struct intrhand *ih = cookie;
	struct bcmstbintc_softc *sc = ih->ih_sc;

	intr_barrier(sc->sc_ih);
}
