/*	$OpenBSD: qcmtx.c,v 1.1 2023/05/17 23:30:58 patrick Exp $	*/
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

#define QCMTX_OFF(idx)		((idx) * 0x1000)
#define QCMTX_NUM_LOCKS		32
#define QCMTX_APPS_PROC_ID	1

#define HREAD4(sc, reg)							\
	(bus_space_read_4((sc)->sc_iot, (sc)->sc_ioh, (reg)))
#define HWRITE4(sc, reg, val)						\
	bus_space_write_4((sc)->sc_iot, (sc)->sc_ioh, (reg), (val))

struct qcmtx_softc {
	struct device		sc_dev;
	bus_space_tag_t		sc_iot;
	bus_space_handle_t	sc_ioh;
	int			sc_node;

	struct hwlock_device	sc_hd;
};

int	qcmtx_match(struct device *, void *, void *);
void	qcmtx_attach(struct device *, struct device *, void *);

int	qcmtx_lock(void *, uint32_t *, int);

const struct cfattach qcmtx_ca = {
	sizeof (struct qcmtx_softc), qcmtx_match, qcmtx_attach
};

struct cfdriver qcmtx_cd = {
	NULL, "qcmtx", DV_DULL
};

int
qcmtx_match(struct device *parent, void *match, void *aux)
{
	struct fdt_attach_args *faa = aux;

	return OF_is_compatible(faa->fa_node, "qcom,tcsr-mutex");
}

void
qcmtx_attach(struct device *parent, struct device *self, void *aux)
{
	struct qcmtx_softc *sc = (struct qcmtx_softc *)self;
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

	sc->sc_hd.hd_node = faa->fa_node;
	sc->sc_hd.hd_cookie = sc;
	sc->sc_hd.hd_lock = qcmtx_lock;
	hwlock_register(&sc->sc_hd);
}

int
qcmtx_lock(void *cookie, uint32_t *cells, int lock)
{
	struct qcmtx_softc *sc = cookie;
	int idx = cells[0];

	if (idx >= QCMTX_NUM_LOCKS)
		return ENXIO;

	if (lock) {
		HWRITE4(sc, QCMTX_OFF(idx), QCMTX_APPS_PROC_ID);
		if (HREAD4(sc, QCMTX_OFF(idx)) !=
		    QCMTX_APPS_PROC_ID)
			return EAGAIN;
		KASSERT(HREAD4(sc, QCMTX_OFF(idx)) == QCMTX_APPS_PROC_ID);
	} else {
		KASSERT(HREAD4(sc, QCMTX_OFF(idx)) == QCMTX_APPS_PROC_ID);
		HWRITE4(sc, QCMTX_OFF(idx), 0);
	}

	return 0;
}
