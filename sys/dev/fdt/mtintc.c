/*	$OpenBSD: mtintc.c,v 1.1 2025/01/30 00:26:44 hastings Exp $	*/
/*
 * Copyright (c) 2025 James Hastings <hastings@openbsd.org>
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
#include <sys/device.h>
#include <sys/malloc.h>
#include <sys/systm.h>

#include <machine/bus.h>
#include <machine/fdt.h>
#include <machine/intr.h>

#include <dev/ofw/openfirm.h>
#include <dev/ofw/fdt.h>

struct mtintc_softc {
	struct device			sc_dev;
	bus_space_tag_t			sc_iot;
	bus_space_handle_t		sc_ioh;
	bus_size_t			sc_ios;

	int				sc_nirq;
	uint32_t			*sc_irq_cfg;

	struct interrupt_controller	sc_ic;
};

int	mtintc_match(struct device *, void *, void *);
void	mtintc_attach(struct device *, struct device *, void *);
int	mtintc_activate(struct device *, int);

void	*mtintc_establish_fdt(void *, int *, int, struct cpu_info *,
	    int (*)(void *), void *, char *);

const struct cfattach mtintc_ca = {
	sizeof(struct mtintc_softc), mtintc_match, mtintc_attach, NULL,
	mtintc_activate
};

struct cfdriver mtintc_cd = {
	NULL, "mtintc", DV_DULL
};

int
mtintc_match(struct device *parent, void *match, void *aux)
{
	struct fdt_attach_args *faa = aux;

	return OF_is_compatible(faa->fa_node, "mediatek,mt6577-sysirq");
}

void
mtintc_attach(struct device *parent, struct device *self, void *aux)
{
	struct mtintc_softc *sc = (struct mtintc_softc *)self;
	struct fdt_attach_args *faa = aux;

	if (faa->fa_nreg < 1) {
		printf(": no registers\n");
		return;
	}

	sc->sc_iot = faa->fa_iot;
	sc->sc_ios = faa->fa_reg[0].size;
	if (bus_space_map(sc->sc_iot, faa->fa_reg[0].addr,
	    sc->sc_ios, 0, &sc->sc_ioh))
		panic("%s: bus_space_map failed!", __func__);

	sc->sc_nirq = sc->sc_ios * 8;
	sc->sc_irq_cfg = malloc(sc->sc_ios, M_DEVBUF, M_WAITOK);

	sc->sc_ic.ic_node = faa->fa_node;
	sc->sc_ic.ic_cookie = sc;
	sc->sc_ic.ic_establish = mtintc_establish_fdt;

	printf(" nirq %d\n", sc->sc_nirq);

	fdt_intr_register(&sc->sc_ic);
}

int
mtintc_activate(struct device *self, int act)
{
	struct mtintc_softc *sc = (struct mtintc_softc *)self;

	switch (act) {
	case DVACT_SUSPEND:
		bus_space_read_region_4(sc->sc_iot, sc->sc_ioh, 0,
		    sc->sc_irq_cfg, sc->sc_ios / sizeof(uint32_t));
		break;
	case DVACT_RESUME:
		bus_space_write_region_4(sc->sc_iot, sc->sc_ioh, 0,
		    sc->sc_irq_cfg, sc->sc_ios / sizeof(uint32_t));
		break;
	}

	return 0;
}

void
mtintc_invert(struct mtintc_softc *sc, int irq)
{
	int reg = (irq / 32) * 4;
	int bit = (irq % 32);

	bus_space_write_4(sc->sc_iot, sc->sc_ioh, reg,
	    bus_space_read_4(sc->sc_iot, sc->sc_ioh, reg) | 1U << bit);
}

void *
mtintc_establish_fdt(void *cookie, int *cells, int level,
    struct cpu_info *ci, int (*func)(void *), void *arg, char *name)
{
	struct mtintc_softc *sc = cookie;
	int irq = cells[1];
	int flags = cells[2];

	KASSERT(cells[0] == 0);
	KASSERT(irq >= 0 && irq < sc->sc_nirq);

#ifdef DEBUG_INTC
	printf("%s: irq %d level %d flags 0x%x [%s]\n", __func__, irq, level,
	    flags, name);
#endif

	if (flags & 0xa)
		mtintc_invert(sc, irq);

	return fdt_intr_parent_establish(&sc->sc_ic, cells, level, ci, func,
	    arg, name);
}
