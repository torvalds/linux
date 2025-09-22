/*	$OpenBSD: qcipcc.c,v 1.2 2023/05/19 20:54:55 patrick Exp $	*/
/*
 * Copyright (c) 2023 Patrick Wildt <patrick@blueri.se>
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

#include <machine/bus.h>
#include <machine/fdt.h>

#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_misc.h>
#include <dev/ofw/fdt.h>

#define IPCC_SEND_ID			0x0c
#define IPCC_RECV_ID			0x10
#define IPCC_RECV_SIGNAL_ENABLE		0x14
#define IPCC_RECV_SIGNAL_DISABLE	0x18
#define IPCC_RECV_SIGNAL_CLEAR		0x1c

#define IPCC_SIGNAL_ID_SHIFT	0
#define IPCC_SIGNAL_ID_MASK	0xffff
#define IPCC_CLIENT_ID_SHIFT	16
#define IPCC_CLIENT_ID_MASK	0xffff

#define HREAD4(sc, reg)							\
	(bus_space_read_4((sc)->sc_iot, (sc)->sc_ioh, (reg)))
#define HWRITE4(sc, reg, val)						\
	bus_space_write_4((sc)->sc_iot, (sc)->sc_ioh, (reg), (val))

struct qcipcc_intrhand {
	TAILQ_ENTRY(qcipcc_intrhand) ih_q;
	int (*ih_func)(void *);
	void *ih_arg;
	void *ih_sc;
	uint16_t ih_client_id;
	uint16_t ih_signal_id;
};

struct qcipcc_softc {
	struct device		sc_dev;
	bus_space_tag_t		sc_iot;
	bus_space_handle_t	sc_ioh;

	void			*sc_ih;

	struct interrupt_controller sc_ic;
	TAILQ_HEAD(,qcipcc_intrhand) sc_intrq;

	struct mbox_device	sc_md;
};

struct qcipcc_channel {
	struct qcipcc_softc	*ch_sc;
	uint32_t		ch_client_id;
	uint32_t		ch_signal_id;
};

int	qcipcc_match(struct device *, void *, void *);
void	qcipcc_attach(struct device *, struct device *, void *);

const struct cfattach qcipcc_ca = {
	sizeof (struct qcipcc_softc), qcipcc_match, qcipcc_attach
};

struct cfdriver qcipcc_cd = {
	NULL, "qcipcc", DV_DULL
};

int	qcipcc_intr(void *);
void	*qcipcc_intr_establish(void *, int *, int, struct cpu_info *,
	    int (*)(void *), void *, char *);
void	qcipcc_intr_disestablish(void *);
void	qcipcc_intr_enable(void *);
void	qcipcc_intr_disable(void *);
void	qcipcc_intr_barrier(void *);

void	*qcipcc_channel(void *, uint32_t *, struct mbox_client *);
int	qcipcc_send(void *, const void *, size_t);

int
qcipcc_match(struct device *parent, void *match, void *aux)
{
	struct fdt_attach_args *faa = aux;

	return OF_is_compatible(faa->fa_node, "qcom,ipcc");
}

void
qcipcc_attach(struct device *parent, struct device *self, void *aux)
{
	struct qcipcc_softc *sc = (struct qcipcc_softc *)self;
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

	TAILQ_INIT(&sc->sc_intrq);

	sc->sc_ih = fdt_intr_establish(faa->fa_node, IPL_BIO,
	    qcipcc_intr, sc, sc->sc_dev.dv_xname);
	if (sc->sc_ih == NULL) {
		printf(": can't establish interrupt\n");
		return;
	}

	printf("\n");

	sc->sc_ic.ic_node = faa->fa_node;
	sc->sc_ic.ic_cookie = sc;
	sc->sc_ic.ic_establish = qcipcc_intr_establish;
	sc->sc_ic.ic_disestablish = qcipcc_intr_disestablish;
	sc->sc_ic.ic_enable = qcipcc_intr_enable;
	sc->sc_ic.ic_disable = qcipcc_intr_disable;
	sc->sc_ic.ic_barrier = qcipcc_intr_barrier;
	fdt_intr_register(&sc->sc_ic);

	sc->sc_md.md_node = faa->fa_node;
	sc->sc_md.md_cookie = sc;
	sc->sc_md.md_channel = qcipcc_channel;
	sc->sc_md.md_send = qcipcc_send;
	mbox_register(&sc->sc_md);
}

int
qcipcc_intr(void *arg)
{
	struct qcipcc_softc *sc = arg;
	struct qcipcc_intrhand *ih;
	uint16_t client_id, signal_id;
	uint32_t reg;
	int handled = 0;

	while ((reg = HREAD4(sc, IPCC_RECV_ID)) != ~0) {
		HWRITE4(sc, IPCC_RECV_SIGNAL_CLEAR, reg);

		client_id = (reg >> IPCC_CLIENT_ID_SHIFT) &
		    IPCC_CLIENT_ID_MASK;
		signal_id = (reg >> IPCC_SIGNAL_ID_SHIFT) &
		    IPCC_SIGNAL_ID_MASK;

		TAILQ_FOREACH(ih, &sc->sc_intrq, ih_q) {
			if (ih->ih_client_id != client_id ||
			    ih->ih_signal_id != signal_id)
				continue;
			ih->ih_func(ih->ih_arg);
			handled = 1;
		}
	}

	return handled;
}

void *
qcipcc_intr_establish(void *cookie, int *cells, int ipl,
    struct cpu_info *ci, int (*func)(void *), void *arg, char *name)
{
	struct qcipcc_softc *sc = cookie;
	struct qcipcc_intrhand *ih;

	ih = malloc(sizeof(*ih), M_DEVBUF, M_WAITOK | M_ZERO);
	ih->ih_func = func;
	ih->ih_arg = arg;
	ih->ih_sc = sc;
	ih->ih_client_id = cells[0] & IPCC_CLIENT_ID_MASK;
	ih->ih_signal_id = cells[1] & IPCC_SIGNAL_ID_MASK;
	TAILQ_INSERT_TAIL(&sc->sc_intrq, ih, ih_q);

	qcipcc_intr_enable(ih);

	if (ipl & IPL_WAKEUP)
		intr_set_wakeup(sc->sc_ih);

	return ih;
}

void
qcipcc_intr_disestablish(void *cookie)
{
	struct qcipcc_intrhand *ih = cookie;
	struct qcipcc_softc *sc = ih->ih_sc;

	qcipcc_intr_disable(ih);

	TAILQ_REMOVE(&sc->sc_intrq, ih, ih_q);
	free(ih, M_DEVBUF, sizeof(*ih));
}

void
qcipcc_intr_enable(void *cookie)
{
	struct qcipcc_intrhand *ih = cookie;
	struct qcipcc_softc *sc = ih->ih_sc;

	HWRITE4(sc, IPCC_RECV_SIGNAL_ENABLE,
	    (ih->ih_client_id << IPCC_CLIENT_ID_SHIFT) |
	    (ih->ih_signal_id << IPCC_SIGNAL_ID_SHIFT));
}

void
qcipcc_intr_disable(void *cookie)
{
	struct qcipcc_intrhand *ih = cookie;
	struct qcipcc_softc *sc = ih->ih_sc;

	HWRITE4(sc, IPCC_RECV_SIGNAL_DISABLE,
	    (ih->ih_client_id << IPCC_CLIENT_ID_SHIFT) |
	    (ih->ih_signal_id << IPCC_SIGNAL_ID_SHIFT));
}

void
qcipcc_intr_barrier(void *cookie)
{
	struct qcipcc_intrhand *ih = cookie;
	struct qcipcc_softc *sc = ih->ih_sc;

	intr_barrier(sc->sc_ih);
}

void *
qcipcc_channel(void *cookie, uint32_t *cells, struct mbox_client *mc)
{
	struct qcipcc_softc *sc = cookie;
	struct qcipcc_channel *ch;

	ch = malloc(sizeof(*ch), M_DEVBUF, M_WAITOK);
	ch->ch_sc = sc;
	ch->ch_client_id = cells[0] & IPCC_CLIENT_ID_MASK;
	ch->ch_signal_id = cells[1] & IPCC_SIGNAL_ID_MASK;

	return ch;
}

int
qcipcc_send(void *cookie, const void *data, size_t len)
{
	struct qcipcc_channel *ch = cookie;
	struct qcipcc_softc *sc = ch->ch_sc;

	HWRITE4(sc, IPCC_SEND_ID,
	    (ch->ch_client_id << IPCC_CLIENT_ID_SHIFT) |
	    (ch->ch_signal_id << IPCC_SIGNAL_ID_SHIFT));

	return 0;
}
