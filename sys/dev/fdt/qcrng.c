/*	$OpenBSD: qcrng.c,v 1.1 2023/04/28 05:13:37 phessler Exp $	*/
/*
 * Copyright (c) 2019 Mark Kettenis <kettenis@openbsd.org>
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
#include <sys/timeout.h>

#include <machine/bus.h>
#include <machine/fdt.h>

#include <dev/ofw/openfirm.h>
#include <dev/ofw/fdt.h>

/* Registers */
#define RNG_DATA		0x0000
#define RNG_STATUS		0x0004

#define HREAD4(sc, reg)							\
	(bus_space_read_4((sc)->sc_iot, (sc)->sc_ioh, (reg)))

struct qcrng_softc {
	struct device		sc_dev;
	bus_space_tag_t		sc_iot;
	bus_space_handle_t	sc_ioh;

	struct timeout		sc_to;
};

int	qcrng_match(struct device *, void *, void *);
void	qcrng_attach(struct device *, struct device *, void *);

const struct cfattach	qcrng_ca = {
	sizeof (struct qcrng_softc), qcrng_match, qcrng_attach
};

struct cfdriver qcrng_cd = {
	NULL, "qcrng", DV_DULL
};

void	qcrng_rnd(void *);

int
qcrng_match(struct device *parent, void *match, void *aux)
{
	struct fdt_attach_args *faa = aux;

	return OF_is_compatible(faa->fa_node, "qcom,prng-ee");
}

void
qcrng_attach(struct device *parent, struct device *self, void *aux)
{
	struct qcrng_softc *sc = (struct qcrng_softc *)self;
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

	printf("\n");

	timeout_set(&sc->sc_to, qcrng_rnd, sc);
	qcrng_rnd(sc);
}

void
qcrng_rnd(void *arg)
{
	struct qcrng_softc *sc = arg;

	if (HREAD4(sc, RNG_STATUS) & 0x1)
		enqueue_randomness(HREAD4(sc, RNG_DATA));

	timeout_add_sec(&sc->sc_to, 1);
}
