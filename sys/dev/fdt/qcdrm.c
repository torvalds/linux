/*	$OpenBSD: qcdrm.c,v 1.1 2025/07/17 15:52:10 kettenis Exp $	*/
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

#include <machine/bus.h>
#include <machine/fdt.h>
#include <machine/simplebusvar.h>

#include <dev/ofw/openfirm.h>
#include <dev/ofw/fdt.h>

#define MDSS_HW_INTR_STATUS	0x10

struct qcdrm_intrhand {
	int (*ih_func)(void *);
	void *ih_arg;
	int ih_ipl;
	void *ih_sc;
};

struct qcdrm_softc {
	struct simplebus_softc	sc_sbus;
	bus_space_tag_t		sc_iot;
	bus_space_handle_t	sc_ioh;
	void			*sc_ih;

	struct qcdrm_intrhand	sc_handlers[32];
	struct interrupt_controller sc_ic;
};

int	qcdrm_match(struct device *, void *, void *);
void	qcdrm_attach(struct device *, struct device *, void *);

const struct cfattach qcdrm_ca = {
	sizeof(struct qcdrm_softc), qcdrm_match, qcdrm_attach
};

struct cfdriver qcdrm_cd = {
	NULL, "qcdrm", DV_DULL
};

void	*qcdrm_intr_establish(void *, int *, int, struct cpu_info *,
	    int (*)(void *), void *, char *);
void	qcdrm_intr_disestablish(void *);
void	qcdrm_intr_barrier(void *);
int	qcdrm_intr(void *);

int
qcdrm_match(struct device *parent, void *match, void *aux)
{
	struct fdt_attach_args *faa = aux;

	return (OF_is_compatible(faa->fa_node, "qcom,sc8280xp-mdss") ||
	    OF_is_compatible(faa->fa_node, "qcom,x1e80100-mdss"));
}

void
qcdrm_attach(struct device *parent, struct device *self, void *aux)
{
	struct qcdrm_softc *sc = (struct qcdrm_softc *)self;
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

	sc->sc_ih = fdt_intr_establish(faa->fa_node, IPL_BIO, qcdrm_intr,
	    sc, sc->sc_sbus.sc_dev.dv_xname);
	if (sc->sc_ih == NULL) {
		printf(": can't establish interrupt\n");
		return;
	}

	sc->sc_ic.ic_node = faa->fa_node;
	sc->sc_ic.ic_cookie = sc;
	sc->sc_ic.ic_establish = qcdrm_intr_establish;
	sc->sc_ic.ic_disestablish = qcdrm_intr_disestablish;
	sc->sc_ic.ic_barrier = qcdrm_intr_barrier;
	fdt_intr_register(&sc->sc_ic);

	simplebus_attach(parent, &sc->sc_sbus.sc_dev, faa);
}

void *
qcdrm_intr_establish(void *cookie, int *cells, int ipl,
    struct cpu_info *ci, int (*func)(void *), void *arg, char *name)
{
	struct qcdrm_softc *sc = cookie;
	int bit = cells[0];

	if (bit < 0 || bit >= 32)
		return NULL;

	sc->sc_handlers[bit].ih_func = func;
	sc->sc_handlers[bit].ih_arg = arg;
	sc->sc_handlers[bit].ih_ipl = ipl & IPL_IRQMASK;
	sc->sc_handlers[bit].ih_sc = sc;

	return &sc->sc_handlers[bit];
}

void
qcdrm_intr_disestablish(void *cookie)
{
	struct qcdrm_intrhand *ih = cookie;

	ih->ih_func = NULL;
}

void
qcdrm_intr_barrier(void *cookie)
{
	struct qcdrm_intrhand *ih = cookie;
	struct qcdrm_softc *sc = ih->ih_sc;

	intr_barrier(sc->sc_ih);
}

int
qcdrm_intr(void *arg)
{
	struct qcdrm_softc *sc = arg;
	int bit, handled = 0;
	uint32_t stat;
	int s;

	stat = bus_space_read_4(sc->sc_iot, sc->sc_ioh, MDSS_HW_INTR_STATUS);

	for (bit = 0; bit < 32; bit++) {
		if ((stat & (1U << bit)) == 0)
			continue;

		if (sc->sc_handlers[bit].ih_func == NULL)
			continue;

		s = splraise(sc->sc_handlers[bit].ih_ipl);
		sc->sc_handlers[bit].ih_func(sc->sc_handlers[bit].ih_arg);
		splx(s);

		handled = 1;
	}

	return handled;
}
